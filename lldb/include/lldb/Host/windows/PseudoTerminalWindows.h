#ifndef liblldb_Host_Windows_PseudoTerminalWindows_H_
#define liblldb_Host_Windows_PseudoTerminalWindows_H_

#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Host/windows/windows.h"

namespace lldb_private {

class PseudoTerminalWindows: public PseudoTerminal {

public:
void Close() override;

HPCON GetPseudoTerminalHandle() override;

HANDLE GetPrimaryHandle() override;

llvm::Error OpenFirstAvailablePrimary(int oflag) override;

protected:
  HANDLE m_conpty_handle = INVALID_HANDLE_VALUE;
  HANDLE m_conpty_output = INVALID_HANDLE_VALUE;
  HANDLE m_conpty_input = INVALID_HANDLE_VALUE;
};
};

#endif // liblldb_Host_Windows_PseudoTerminalWindows_H_