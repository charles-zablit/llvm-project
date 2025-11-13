//===-- ProcessLauncherWindows.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/windows/ProcessLauncherWindows.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/ProcessLaunchInfo.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Program.h"

#include <string>
#include <vector>

using namespace lldb;
using namespace lldb_private;

static void CreateEnvironmentBuffer(const Environment &env,
                                    std::vector<wchar_t> &buffer) {

  std::vector<std::wstring> env_entries;
  for (const auto &KV : env) {
    std::wstring wentry;
    if (llvm::ConvertUTF8toWide(Environment::compose(KV), wentry)) {
      env_entries.push_back(std::move(wentry));
    }
  }
  std::sort(env_entries.begin(), env_entries.end(),
            [](const std::wstring &a, const std::wstring &b) {
              return _wcsicmp(a.c_str(), b.c_str()) < 0;
            });

  buffer.clear();
  for (const auto &env_entry : env_entries) {
    buffer.insert(buffer.end(), env_entry.begin(), env_entry.end());
    buffer.push_back(L'\0');
  }
  buffer.push_back(L'\0');
}

static bool GetFlattenedWindowsCommandString(Args args, std::wstring &command) {
  if (args.empty())
    return false;

  std::vector<llvm::StringRef> args_ref;
  for (auto &entry : args.entries())
    args_ref.push_back(entry.ref());

  llvm::ErrorOr<std::wstring> result =
      llvm::sys::flattenWindowsCommandLine(args_ref);
  if (result.getError())
    return false;

  command = *result;
  return true;
}

std::string g_foo;

HostProcess
ProcessLauncherWindows::LaunchProcess(const ProcessLaunchInfo &launch_info,
                                      Status &error) {
  error.Clear();

  std::string executable;
  std::vector<wchar_t> environment;
  STARTUPINFOEXW startupinfoex = {};
  STARTUPINFOW &startupinfo = startupinfoex.StartupInfo;
  PROCESS_INFORMATION pi = {};

  std::vector<HANDLE> inherited_handles;
  PseudoTerminal &pty = launch_info.GetPTY();
  HPCON hPC = pty.GetPseudoTerminalHandle();
  // const char *hide_console_var =
  //     getenv("LLDB_LAUNCH_INFERIORS_WITHOUT_CONSOLE");
  // if (hide_console_var &&
  //     llvm::StringRef(hide_console_var).equals_insensitive("true")) {
  //   startupinfo.dwFlags |= STARTF_USESHOWWINDOW;
  //   startupinfo.wShowWindow = SW_HIDE;
  // }
  startupinfo.dwFlags |= STARTF_USESTDHANDLES;


  DWORD flags = CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_NEW_CONSOLE;
  if (launch_info.GetFlags().Test(eLaunchFlagDebug))
    flags |= DEBUG_ONLY_THIS_PROCESS;

  if (launch_info.GetFlags().Test(eLaunchFlagDisableSTDIO))
    flags &= ~CREATE_NEW_CONSOLE;

  startupinfo.cb = sizeof(startupinfoex);

  SIZE_T attributelist_size = 0;
  InitializeProcThreadAttributeList(/*lpAttributeList=*/nullptr,
                                    /*dwAttributeCount=*/1, /*dwFlags=*/0,
                                    &attributelist_size);

  startupinfoex.lpAttributeList =
      static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(malloc(attributelist_size));
  auto free_attributelist =
      llvm::make_scope_exit([&] { free(startupinfoex.lpAttributeList); });
  if (!InitializeProcThreadAttributeList(startupinfoex.lpAttributeList,
                                         /*dwAttributeCount=*/1, /*dwFlags=*/0,
                                         &attributelist_size)) {
    error = Status(::GetLastError(), eErrorTypeWin32);
    return HostProcess();
  }
  auto delete_attributelist = llvm::make_scope_exit(
      [&] { DeleteProcThreadAttributeList(startupinfoex.lpAttributeList); });

  if (!UpdateProcThreadAttribute(startupinfoex.lpAttributeList, 0,
                                 PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC,
                                 sizeof(hPC), NULL, NULL)) {
    error = Status(::GetLastError(), eErrorTypeWin32);
    return HostProcess();
  }

  ::CreateEnvironmentBuffer(launch_info.GetEnvironment(), environment);
  LPVOID env_block = environment.empty() ? nullptr : environment.data();

  executable = launch_info.GetExecutableFile().GetPath();
  std::wstring wcommandLine;
  GetFlattenedWindowsCommandString(launch_info.GetArguments(), wcommandLine);

  std::wstring wexecutable, wworkingDirectory;
  llvm::ConvertUTF8toWide(executable, wexecutable);
  llvm::ConvertUTF8toWide(launch_info.GetWorkingDirectory().GetPath(),
                          wworkingDirectory);
  // If the command line is empty, it's best to pass a null pointer to tell
  // CreateProcessW to use the executable name as the command line.  If the
  // command line is not empty, its contents may be modified by CreateProcessW.
  WCHAR *pwcommandLine = wcommandLine.empty() ? nullptr : &wcommandLine[0];

  // BOOL result = ::CreateProcessW(
  //     wexecutable.c_str(), pwcommandLine, NULL, NULL,
  //     /*bInheritHandles=*/!inherited_handles.empty(), flags, env_block,
  //     wworkingDirectory.size() == 0 ? NULL : wworkingDirectory.c_str(),
  //     reinterpret_cast<STARTUPINFO *>(&startupinfoex), &pi);

  BOOL result = ::CreateProcessW(
      wexecutable.c_str(), pwcommandLine, NULL, NULL,
      FALSE, flags, env_block,
      wworkingDirectory.size() == 0 ? NULL : wworkingDirectory.c_str(),
      reinterpret_cast<STARTUPINFOW*>(&startupinfoex), &pi);

  // std::thread([fd = pty.GetPrimaryFileDescriptor()]() {
  //     return;
  //     DWORD bytesAvailable = 0;
  //     char buf[4096];
  //     HANDLE hPipe = (HANDLE)_get_osfhandle(fd);
  //     while (true) {
  //         // Wait for the pipe to become signaled (data available or broken)
  //         DWORD waitResult = WaitForSingleObject(hPipe, INFINITE);
  //         if (waitResult != WAIT_OBJECT_0) {
  //             break; // handle closed or error
  //         }

  //         // Optional: check how much data is ready
  //         if (!PeekNamedPipe(hPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
  //           break; // pipe closed or error
  //         }
  //         if (bytesAvailable == 0) {
  //             continue;
  //         }

  //         DWORD bytesRead = 0;
  //         if (!ReadFile(hPipe, buf, sizeof(buf), &bytesRead, nullptr)) {
  //           break; // EOF or error
  //         }

  //         if (bytesRead > 0) {
  //             std::string foo(buf, bytesRead);
  //             g_foo.append(foo);
  //             llvm::errs() << "[OUT] " << llvm::StringRef(buf, bytesRead);
  //             llvm::errs().flush();
  //         }
  //     }
  // }).detach();
  if (!result) {
    // Call GetLastError before we make any other system calls.
    error = Status(::GetLastError(), eErrorTypeWin32);
    // Note that error 50 ("The request is not supported") will occur if you
    // try debug a 64-bit inferior from a 32-bit LLDB.
  }

  if (result) {
    // Do not call CloseHandle on pi.hProcess, since we want to pass that back
    // through the HostProcess.
    ::CloseHandle(pi.hThread);
  }

  if (!result)
    return HostProcess();

  return HostProcess(pi.hProcess);
}

HANDLE
ProcessLauncherWindows::GetStdioHandle(const ProcessLaunchInfo &launch_info,
                                       int fd) {
  const FileAction *action = launch_info.GetFileActionForFD(fd);
  if (action == nullptr)
    return NULL;
  SECURITY_ATTRIBUTES secattr = {};
  secattr.nLength = sizeof(SECURITY_ATTRIBUTES);
  secattr.bInheritHandle = TRUE;

  llvm::StringRef path = action->GetPath();
  DWORD access = 0;
  DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  DWORD create = 0;
  DWORD flags = 0;
  if (fd == STDIN_FILENO) {
    access = GENERIC_READ;
    create = OPEN_EXISTING;
    flags = FILE_ATTRIBUTE_READONLY;
  }
  if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
    access = GENERIC_WRITE;
    create = CREATE_ALWAYS;
    if (fd == STDERR_FILENO)
      flags = FILE_FLAG_WRITE_THROUGH;
  }

  std::wstring wpath;
  llvm::ConvertUTF8toWide(path, wpath);
  HANDLE result = ::CreateFileW(wpath.c_str(), access, share, &secattr, create,
                                flags, NULL);
  return (result == INVALID_HANDLE_VALUE) ? NULL : result;
}
