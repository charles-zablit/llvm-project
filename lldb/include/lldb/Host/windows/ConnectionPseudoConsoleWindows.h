//===-- ConnectionGenericFileWindows.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Host_windows_ConnectionPseudoConsoleWindows_h_
#define liblldb_Host_windows_ConnectionPseudoConsoleWindows_h_

#include "lldb/Host/windows/windows.h"
#include "lldb/Utility/Connection.h"
#include "lldb/Host/windows/PseudoTerminalWindows.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

class Status;

class ConnectionPseudoConsole : public lldb_private::Connection {
public:
  ConnectionPseudoConsole();

  ConnectionPseudoConsole(std::shared_ptr<PseudoTerminal> pty, bool owns_file);

  ~ConnectionPseudoConsole() override;

  bool IsConnected() const override;

  lldb::ConnectionStatus Connect(llvm::StringRef url, Status *error_ptr) override;

  lldb::ConnectionStatus Disconnect(Status *error_ptr) override;

  size_t Read(void *dst, size_t dst_len, const Timeout<std::micro> &timeout,
              lldb::ConnectionStatus &status, Status *error_ptr) override;

  size_t Write(const void *src, size_t src_len, lldb::ConnectionStatus &status,
               Status *error_ptr) override;

  std::string GetURI() override {
    return "";
  };

  bool InterruptRead() override {
    return false;
  };

protected:
  std::shared_ptr<PseudoTerminal> m_pty;
  bool m_owns_file;

private:
  ConnectionPseudoConsole(const ConnectionPseudoConsole &) = delete;
  const ConnectionPseudoConsole &
  operator=(const ConnectionPseudoConsole &) = delete;
};
}

#endif
