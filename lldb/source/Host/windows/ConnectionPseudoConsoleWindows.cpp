//===-- ConnectionPseudoConsoleWindowsWindows.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/windows/ConnectionPseudoConsoleWindows.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Timeout.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ConvertUTF.h"

using namespace lldb;
using namespace lldb_private;

namespace {
// This is a simple helper class to package up the information needed to return
// from a Read/Write operation function.  Since there is a lot of code to be
// run before exit regardless of whether the operation succeeded or failed,
// combined with many possible return paths, this is the cleanest way to
// represent it.
class ReturnInfo {
public:
  void Set(size_t bytes, ConnectionStatus status, DWORD error_code) {
    m_error = Status(error_code, eErrorTypeWin32);
    m_bytes = bytes;
    m_status = status;
  }

  void Set(size_t bytes, ConnectionStatus status, llvm::StringRef error_msg) {
    m_error = Status::FromErrorString(error_msg.data());
    m_bytes = bytes;
    m_status = status;
  }

  size_t GetBytes() const { return m_bytes; }
  ConnectionStatus GetStatus() const { return m_status; }
  const Status &GetError() const { return m_error; }

private:
  Status m_error;
  size_t m_bytes;
  ConnectionStatus m_status;
};
}

ConnectionPseudoConsole::ConnectionPseudoConsole()
    : m_pty(nullptr), m_owns_file(false) {
}

ConnectionPseudoConsole::ConnectionPseudoConsole(std::shared_ptr<PseudoTerminal> pty, bool owns_file)
    : m_pty(pty), m_owns_file(owns_file) {
}

ConnectionPseudoConsole::~ConnectionPseudoConsole() {}

lldb::ConnectionStatus ConnectionPseudoConsole::Connect(llvm::StringRef url, Status *error_ptr) {
  if (IsConnected())
    return eConnectionStatusSuccess;
  return eConnectionStatusNoConnection;
}

bool ConnectionPseudoConsole::IsConnected() const {
  return m_pty && (m_pty->GetPrimaryHandle() != INVALID_HANDLE_VALUE);
}

lldb::ConnectionStatus ConnectionPseudoConsole::Disconnect(Status *error_ptr) {
  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOGF(log, "%p ConnectionPseudoConsole::Disconnect ()",
            static_cast<void *>(this));

  if (!IsConnected())
    return eConnectionStatusSuccess;

  m_pty->Close();
  return eConnectionStatusSuccess;
}

size_t ConnectionPseudoConsole::Read(void *dst, size_t dst_len,
                                   const Timeout<std::micro> &timeout,
                                   lldb::ConnectionStatus &status,
                                   Status *error_ptr) {
  DWORD bytes_read = 0;

  if (error_ptr)
    error_ptr->Clear();

  if (!IsConnected()) {
    status = eConnectionStatusNoConnection;
    return 0;
  }

  BOOL ok = ReadFile(m_pty->GetPrimaryHandle(), dst, static_cast<DWORD>(dst_len), &bytes_read, nullptr);
  if (!ok) {
    DWORD err = GetLastError();
    switch (err) {
      case ERROR_OPERATION_ABORTED:
      case ERROR_INVALID_HANDLE:
      case ERROR_BROKEN_PIPE:
        status = lldb::eConnectionStatusEndOfFile; // handle closed mid-read
        break;
      default:
        status = lldb::eConnectionStatusError;
        if (error_ptr)
          *error_ptr = Status(err, eErrorTypeWin32);
        break;
    }
    return 0;
  }

  status = lldb::eConnectionStatusSuccess;
  return bytes_read;
}

size_t ConnectionPseudoConsole::Write(const void *src, size_t src_len,
                                    lldb::ConnectionStatus &status,
                                    Status *error_ptr) {
  // TODO:
  ReturnInfo return_info;
  DWORD bytes_written = 0;

  if (error_ptr)
    error_ptr->Clear();

  if (!IsConnected()) {
    return_info.Set(0, eConnectionStatusNoConnection, ERROR_INVALID_HANDLE);
  }
  return 0;
}
