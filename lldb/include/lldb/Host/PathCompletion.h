//===-- PathCompletion.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_PATHCOMPLETION_H
#define LLDB_HOST_PATHCOMPLETION_H

#include "llvm/ADT/Twine.h"

namespace lldb_private {

class CompletionRequest;
class StringList;
class TildeExpressionResolver;

namespace path_completion {

// Append every regular file in the directory containing `partial_path` whose
// name starts with the basename of `partial_path` to `matches`, after tilde
// expansion via `resolver`. This is the leaf-lib equivalent of
// CommandCompletions::DiskFiles -- it lives in lldbHost so that lldb-server
// can offer remote path completion without linking lldbCommands.
void CompleteFiles(const llvm::Twine &partial_path, StringList &matches,
                   TildeExpressionResolver &resolver);

// Same, restricted to subdirectories.
void CompleteDirectories(const llvm::Twine &partial_path, StringList &matches,
                         TildeExpressionResolver &resolver);

// CompletionRequest-based variant used by the command-completion machinery in
// lldbCommands. Setting `only_directories=true` mirrors CompleteDirectories.
void Complete(const llvm::Twine &partial_path, bool only_directories,
              CompletionRequest &request, TildeExpressionResolver &resolver);

} // namespace path_completion
} // namespace lldb_private

#endif // LLDB_HOST_PATHCOMPLETION_H
