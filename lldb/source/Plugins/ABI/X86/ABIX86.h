//===-- ABIX86.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_X86_ABIX86_H
#define LLDB_SOURCE_PLUGINS_ABI_X86_ABIX86_H

#include "lldb/Target/ABI.h"
#include "lldb/lldb-private.h"

class ABIX86 : public lldb_private::MCBasedABI {
public:
  static void Initialize();
  static void Terminate();

protected:
  void AugmentRegisterInfo(
      std::vector<lldb_private::DynamicRegisterInfo::Register> &regs) override;

  /// Whether this ABI instance is for x86_64 (true) or i386 (false). Used as
  /// a fallback for picking the GPR base-register map when the Target's
  /// architecture isn't yet populated (e.g. ProcessGDBRemote::AddRemoteRegisters
  /// is called from DidAttach before the Target ArchSpec is set).
  virtual bool Is64Bit() const = 0;

private:
  using lldb_private::MCBasedABI::MCBasedABI;
};

#endif
