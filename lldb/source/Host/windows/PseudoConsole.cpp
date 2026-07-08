//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/windows/PseudoConsole.h"

#include <cstdio>
#include <mutex>

#include "lldb/Host/windows/windows.h"
#include "lldb/Utility/LLDBLog.h"

#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Errc.h"

using namespace lldb_private;

using CreatePseudoConsole_t = HRESULT(WINAPI *)(COORD size, HANDLE hInput,
                                                HANDLE hOutput, DWORD dwFlags,
                                                HPCON *phPC);

using ClosePseudoConsole_t = VOID(WINAPI *)(HPCON hPC);

static constexpr DWORD PSEUDOCONSOLE_INHERIT_CURSOR = 0x1;

struct Kernel32 {
  Kernel32() {
    hModule = LoadLibraryW(L"kernel32.dll");
    if (!hModule) {
      llvm::Error err = llvm::errorCodeToError(
          std::error_code(GetLastError(), std::system_category()));
      LLDB_LOG_ERROR(GetLog(LLDBLog::Host), std::move(err),
                     "Could not load kernel32: {0}");
      return;
    }
    CreatePseudoConsole_ =
        (CreatePseudoConsole_t)GetProcAddress(hModule, "CreatePseudoConsole");
    ClosePseudoConsole_ =
        (ClosePseudoConsole_t)GetProcAddress(hModule, "ClosePseudoConsole");
    isAvailable = (CreatePseudoConsole_ && ClosePseudoConsole_);
  }

  HRESULT CreatePseudoConsole(COORD size, HANDLE hInput, HANDLE hOutput,
                              DWORD dwFlags, HPCON *phPC) {
    assert(CreatePseudoConsole_ && "CreatePseudoConsole is not available!");
    return CreatePseudoConsole_(size, hInput, hOutput, dwFlags, phPC);
  }

  VOID ClosePseudoConsole(HPCON hPC) {
    assert(ClosePseudoConsole_ && "ClosePseudoConsole is not available!");
    return ClosePseudoConsole_(hPC);
  }

  bool IsConPTYAvailable() { return isAvailable; }

private:
  HMODULE hModule = nullptr;
  CreatePseudoConsole_t CreatePseudoConsole_ = nullptr;
  ClosePseudoConsole_t ClosePseudoConsole_ = nullptr;
  bool isAvailable = false;
};

static Kernel32 kernel32;

llvm::Error PseudoConsole::CreateOverlappedPipePair(HANDLE &out_read,
                                                    HANDLE &out_write,
                                                    bool inheritable) {
  wchar_t pipe_name[MAX_PATH];
  swprintf(pipe_name, MAX_PATH, L"\\\\.\\pipe\\conpty-lldb-%d-%p",
           GetCurrentProcessId(), this);
  out_read =
      CreateNamedPipeW(pipe_name, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                       PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
  if (out_read == INVALID_HANDLE_VALUE)
    return llvm::errorCodeToError(
        std::error_code(GetLastError(), std::system_category()));
  SECURITY_ATTRIBUTES write_sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  out_write = CreateFileW(pipe_name, GENERIC_WRITE, 0,
                          inheritable ? &write_sa : nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
  if (out_write == INVALID_HANDLE_VALUE) {
    CloseHandle(out_read);
    out_read = INVALID_HANDLE_VALUE;
    return llvm::errorCodeToError(
        std::error_code(GetLastError(), std::system_category()));
  }

  return llvm::Error::success();
}

PseudoConsole::~PseudoConsole() { Reset(); }

llvm::Error PseudoConsole::OpenPseudoConsole(uint16_t req_cols,
                                             uint16_t req_rows) {
  Reset();

  if (!kernel32.IsConPTYAvailable())
    return llvm::make_error<llvm::StringError>("ConPTY is not available",
                                               llvm::errc::io_error);
  // A 4096 bytes buffer should be large enough for the majority of console
  // burst outputs.
  wchar_t pipe_name[MAX_PATH];
  swprintf(pipe_name, MAX_PATH, L"\\\\.\\pipe\\conpty-lldb-%d-%p",
           GetCurrentProcessId(), this);
  HANDLE hOutputRead = INVALID_HANDLE_VALUE;
  HANDLE hOutputWrite = INVALID_HANDLE_VALUE;
  if (auto err = CreateOverlappedPipePair(hOutputRead, hOutputWrite, false))
    return err;

  HANDLE hInputRead = INVALID_HANDLE_VALUE;
  HANDLE hInputWrite = INVALID_HANDLE_VALUE;
  if (!CreatePipe(&hInputRead, &hInputWrite, nullptr, 0)) {
    CloseHandle(hOutputRead);
    CloseHandle(hOutputWrite);
    return llvm::errorCodeToError(
        std::error_code(GetLastError(), std::system_category()));
  }

  COORD consoleSize{80, 25};
  // Cursor position within the visible window, 1-indexed for VT sequences.
  // Defaults to the last row so ConPTY won't scroll back over existing output
  // if we can't query the real console.
  int cursorRow = consoleSize.Y;
  int cursorCol = 1;
  if (req_cols != 0 && req_rows != 0) {
    consoleSize = {static_cast<SHORT>(req_cols), static_cast<SHORT>(req_rows)};
    cursorRow = consoleSize.Y;
    cursorCol = 1;
  } else {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
      consoleSize = {
          static_cast<SHORT>(csbi.srWindow.Right - csbi.srWindow.Left + 1),
          static_cast<SHORT>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1)};
      cursorRow = csbi.dwCursorPosition.Y - csbi.srWindow.Top + 1;
      cursorCol = csbi.dwCursorPosition.X + 1;
    }
  }
  HPCON hPC = INVALID_HANDLE_VALUE;
  HRESULT hr =
      kernel32.CreatePseudoConsole(consoleSize, hInputRead, hOutputWrite,
                                   PSEUDOCONSOLE_INHERIT_CURSOR, &hPC);
  CloseHandle(hInputRead);
  CloseHandle(hOutputWrite);

  if (FAILED(hr)) {
    CloseHandle(hInputWrite);
    CloseHandle(hOutputRead);
    return llvm::make_error<llvm::StringError>(
        "Failed to create Windows ConPTY pseudo terminal",
        llvm::errc::io_error);
  }

  m_conpty_handle = hPC;
  m_conpty_output = hOutputRead;
  m_conpty_input = hInputWrite;
  m_mode = Mode::ConPTY;

  // PSEUDOCONSOLE_INHERIT_CURSOR causes ConPTY to emit ESC[6n on the output
  // pipe to query the current cursor position before it finishes initializing.
  // Write the cursor position response to the input pipe so ConPTY can read it
  // and initialize without clearing the screen or overwriting LLDB's prompt.
  {
    llvm::SmallString<32> response =
        llvm::formatv("\x1b[{0};{1}R", cursorRow, cursorCol).sstr<32>();
    DWORD nwritten = 0;
    WriteFile(m_conpty_input, response.data(), response.size(), &nwritten,
              nullptr);
  }

  return llvm::Error::success();
}

bool PseudoConsole::IsConnected() const {
  if (m_mode == Mode::Pipe || m_mode == Mode::ClientPipe)
    return m_conpty_input != INVALID_HANDLE_VALUE &&
           m_conpty_output != INVALID_HANDLE_VALUE;
  return m_conpty_handle != INVALID_HANDLE_VALUE &&
         m_conpty_input != INVALID_HANDLE_VALUE &&
         m_conpty_output != INVALID_HANDLE_VALUE;
}

void PseudoConsole::Close() {
  SetStopping(true);
  std::unique_lock<std::mutex> guard(m_mutex);
  if (m_conpty_handle != INVALID_HANDLE_VALUE)
    kernel32.ClosePseudoConsole(m_conpty_handle);
  if ((m_mode == Mode::Pipe || m_mode == Mode::ClientPipe) &&
      m_conpty_output != INVALID_HANDLE_VALUE)
    CancelIoEx(m_conpty_output, nullptr);
  if (m_mode == Mode::ClientPipe && m_client_stderr != INVALID_HANDLE_VALUE)
    CancelIoEx(m_client_stderr, nullptr);
  m_conpty_handle = INVALID_HANDLE_VALUE;
  SetStopping(false);
  m_cv.notify_all();
}

void PseudoConsole::ClosePseudoConsolePipes() {
  if (m_conpty_input != INVALID_HANDLE_VALUE)
    CloseHandle(m_conpty_input);
  if (m_conpty_output != INVALID_HANDLE_VALUE)
    CloseHandle(m_conpty_output);
  if (m_client_stderr != INVALID_HANDLE_VALUE)
    CloseHandle(m_client_stderr);

  m_conpty_input = INVALID_HANDLE_VALUE;
  m_conpty_output = INVALID_HANDLE_VALUE;
  m_client_stderr = INVALID_HANDLE_VALUE;
}

void PseudoConsole::CloseAnonymousPipes() {
  if (m_pipe_child_stdin != INVALID_HANDLE_VALUE)
    CloseHandle(m_pipe_child_stdin);
  if (m_pipe_child_stdout != INVALID_HANDLE_VALUE)
    CloseHandle(m_pipe_child_stdout);

  m_pipe_child_stdin = INVALID_HANDLE_VALUE;
  m_pipe_child_stdout = INVALID_HANDLE_VALUE;
}

void PseudoConsole::Reset() {
  Close();
  ClosePseudoConsolePipes();
  CloseAnonymousPipes();
  m_mode = Mode::None;
}

llvm::Error PseudoConsole::OpenAnonymousPipes() {
  Reset();

  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE hStdinRead = INVALID_HANDLE_VALUE;
  HANDLE hStdinWrite = INVALID_HANDLE_VALUE;
  if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0))
    return llvm::errorCodeToError(
        std::error_code(GetLastError(), std::system_category()));
  // Parent write end must not be inherited by the child.
  SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

  HANDLE hStdoutRead = INVALID_HANDLE_VALUE;
  HANDLE hStdoutWrite = INVALID_HANDLE_VALUE;
  if (auto err = CreateOverlappedPipePair(hStdoutRead, hStdoutWrite, true)) {
    CloseHandle(hStdinRead);
    CloseHandle(hStdinWrite);
    return err;
  }

  m_conpty_input = hStdinWrite;
  m_conpty_output = hStdoutRead;
  m_pipe_child_stdin = hStdinRead;
  m_pipe_child_stdout = hStdoutWrite;
  m_mode = Mode::Pipe;
  return llvm::Error::success();
}

llvm::Error PseudoConsole::OpenClientStdioPipes() {
  Reset();

  // Unique base name per process + object so concurrent launches don't collide.
  char base[128];
  ::snprintf(base, sizeof(base), "\\\\.\\pipe\\lldb-stdio-%lu-%p",
             static_cast<unsigned long>(GetCurrentProcessId()),
             static_cast<void *>(this));
  m_stdin_pipe_path = std::string(base) + "-in";
  m_stdout_pipe_path = std::string(base) + "-out";
  m_stderr_pipe_path = std::string(base) + "-err";

  // We are the pipe server for all three; the inferior (launched by lldb-server)
  // opens the client ends by name via CreateFileW. stdin flows server->client
  // (PIPE_ACCESS_OUTBOUND: we write, inferior reads); stdout/stderr flow
  // client->server (PIPE_ACCESS_INBOUND: inferior writes, we read). The read
  // ends are overlapped so ThreadedCommunication's read thread can be cancelled
  // at teardown via CancelIoEx (see Close()).
  auto create_pipe = [](const std::string &path, DWORD access) -> HANDLE {
    std::wstring wpath;
    llvm::ConvertUTF8toWide(path, wpath);
    return CreateNamedPipeW(wpath.c_str(), access,
                            PIPE_TYPE_BYTE | PIPE_WAIT, /*nMaxInstances=*/1,
                            /*nOutBufferSize=*/4096, /*nInBufferSize=*/4096,
                            /*nDefaultTimeOut=*/0, /*lpSecurityAttributes=*/nullptr);
  };

  HANDLE stdin_write = create_pipe(m_stdin_pipe_path, PIPE_ACCESS_OUTBOUND);
  if (stdin_write == INVALID_HANDLE_VALUE)
    return llvm::errorCodeToError(
        std::error_code(GetLastError(), std::system_category()));

  HANDLE stdout_read =
      create_pipe(m_stdout_pipe_path, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED);
  if (stdout_read == INVALID_HANDLE_VALUE) {
    DWORD err = GetLastError();
    CloseHandle(stdin_write);
    return llvm::errorCodeToError(std::error_code(err, std::system_category()));
  }

  HANDLE stderr_read =
      create_pipe(m_stderr_pipe_path, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED);
  if (stderr_read == INVALID_HANDLE_VALUE) {
    DWORD err = GetLastError();
    CloseHandle(stdin_write);
    CloseHandle(stdout_read);
    return llvm::errorCodeToError(std::error_code(err, std::system_category()));
  }

  m_conpty_input = stdin_write;
  m_conpty_output = stdout_read;
  m_client_stderr = stderr_read;
  m_mode = Mode::ClientPipe;

  // NOTE(verify-on-windows): we rely on the inferior having opened the client
  // ends (during lldb-server's CreateProcessW) before the client wires up its
  // read threads, which happens only after the launch stop-reply arrives. If a
  // race is observed (reads failing with ERROR_PIPE_LISTENING), issue an
  // overlapped ConnectNamedPipe here and gate the read threads on it.
  return llvm::Error::success();
}
