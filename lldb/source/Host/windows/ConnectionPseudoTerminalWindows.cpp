//===-- ConnectionPseudoConsoleWindowsWindows.cpp
//----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/windows/ConnectionPseudoTerminalWindows.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Timeout.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ConvertUTF.h"

using namespace lldb;
using namespace lldb_private;

ConnectionPseudoTerminal::ConnectionPseudoTerminal()
    : m_pty(nullptr), m_owns_file(false) {}

ConnectionPseudoTerminal::ConnectionPseudoTerminal(
    std::shared_ptr<PseudoTerminal> pty, bool owns_file)
    : m_pty(pty), m_owns_file(owns_file) {}

ConnectionPseudoTerminal::~ConnectionPseudoTerminal() {}

lldb::ConnectionStatus ConnectionPseudoTerminal::Connect(llvm::StringRef url,
                                                         Status *error_ptr) {
  if (IsConnected())
    return eConnectionStatusSuccess;
  return eConnectionStatusNoConnection;
}

bool ConnectionPseudoTerminal::IsConnected() const {
  return m_pty && (m_pty->GetPrimaryHandle() != INVALID_HANDLE_VALUE);
}

lldb::ConnectionStatus ConnectionPseudoTerminal::Disconnect(Status *error_ptr) {
  Log *log = GetLog(LLDBLog::Connection);
  LLDB_LOGF(log, "%p ConnectionPseudoTerminal::Disconnect ()",
            static_cast<void *>(this));

  if (!IsConnected())
    return eConnectionStatusSuccess;

  m_pty->Close();
  return eConnectionStatusSuccess;
}

size_t ConnectionPseudoTerminal::Read(void *dst, size_t dst_len,
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

  BOOL ok = ReadFile(m_pty->GetPrimaryHandle(), dst,
                     static_cast<DWORD>(dst_len), &bytes_read, nullptr);
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

size_t ConnectionPseudoTerminal::Write(const void *src, size_t src_len,
                                       lldb::ConnectionStatus &status,
                                       Status *error_ptr) {
  // TODO:
  DWORD bytes_written = 0;

  if (error_ptr)
    error_ptr->Clear();

  if (!IsConnected()) {
  }
  return 0;
}
