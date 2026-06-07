//===-- CommonCompletionTrampoline.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Trampoline implementation of CommandCompletions::InvokeCommonCompletionCallbacks
// that routes through a function-pointer hook. lldbCommands installs the real
// dispatcher (which knows about every CommandCompletions::Modules / Symbols /
// Breakpoints / etc. leaf) by calling SetCommonCompletionDispatcher from its
// registration entry point. Tools that don't link lldbCommands -- notably
// lldb-server -- get a no-op, which is fine because they don't drive
// completion-aware option parsing.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/CommandCompletions.h"

namespace {
lldb_private::CommandCompletions::CommonCompletionDispatcher g_dispatcher =
    nullptr;
}

namespace lldb_private {

void CommandCompletions::SetCommonCompletionDispatcher(
    CommonCompletionDispatcher dispatcher) {
  g_dispatcher = dispatcher;
}

bool CommandCompletions::InvokeCommonCompletionCallbacks(
    CommandInterpreter &interpreter, uint32_t completion_mask,
    CompletionRequest &request, SearchFilter *searcher) {
  if (g_dispatcher)
    return g_dispatcher(interpreter, completion_mask, request, searcher);
  return false;
}

} // namespace lldb_private
