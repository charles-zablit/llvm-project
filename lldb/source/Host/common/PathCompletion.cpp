//===-- PathCompletion.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/PathCompletion.h"

#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StringList.h"
#include "lldb/Utility/TildeExpressionResolver.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Path.h"

#include <climits>

using namespace lldb_private;

namespace {

void DiskFilesOrDirectories(const llvm::Twine &partial_name,
                            bool only_directories, CompletionRequest &request,
                            TildeExpressionResolver &Resolver) {
  llvm::SmallString<256> CompletionBuffer;
  llvm::SmallString<256> Storage;
  partial_name.toVector(CompletionBuffer);

  if (CompletionBuffer.size() >= PATH_MAX)
    return;

  namespace path = llvm::sys::path;

  llvm::StringRef SearchDir;
  llvm::StringRef PartialItem;

  if (CompletionBuffer.starts_with("~")) {
    llvm::StringRef Buffer = CompletionBuffer;
    size_t FirstSep =
        Buffer.find_if([](char c) { return path::is_separator(c); });

    llvm::StringRef Username = Buffer.take_front(FirstSep);
    llvm::StringRef Remainder;
    if (FirstSep != llvm::StringRef::npos)
      Remainder = Buffer.drop_front(FirstSep + 1);

    llvm::SmallString<256> Resolved;
    if (!Resolver.ResolveExact(Username, Resolved)) {
      // We couldn't resolve it as a full username.  If there were no slashes
      // then this might be a partial username.   We try to resolve it as such
      // but after that, we're done regardless of any matches.
      if (FirstSep == llvm::StringRef::npos) {
        llvm::StringSet<> MatchSet;
        Resolver.ResolvePartial(Username, MatchSet);
        for (const auto &S : MatchSet) {
          Resolved = S.getKey();
          path::append(Resolved, path::get_separator());
          request.AddCompletion(Resolved, "", CompletionMode::Partial);
        }
      }
      return;
    }

    // If there was no trailing slash, then we're done as soon as we resolve
    // the expression to the correct directory.  Otherwise we need to continue
    // looking for matches within that directory.
    if (FirstSep == llvm::StringRef::npos) {
      // Make sure it ends with a separator.
      path::append(CompletionBuffer, path::get_separator());
      request.AddCompletion(CompletionBuffer, "", CompletionMode::Partial);
      return;
    }

    // We want to keep the form the user typed, so we special case this to
    // search in the fully resolved directory, but CompletionBuffer keeps the
    // unmodified form that the user typed.
    Storage = Resolved;
    llvm::StringRef RemainderDir = path::parent_path(Remainder);
    if (!RemainderDir.empty()) {
      // Append the remaining path to the resolved directory.
      Storage.append(path::get_separator());
      Storage.append(RemainderDir);
    }
    SearchDir = Storage;
  } else if (CompletionBuffer == path::root_directory(CompletionBuffer)) {
    SearchDir = CompletionBuffer;
  } else {
    SearchDir = path::parent_path(CompletionBuffer);
  }

  size_t FullPrefixLen = CompletionBuffer.size();

  PartialItem = path::filename(CompletionBuffer);

  // path::filename() will return "." when the passed path ends with a
  // directory separator or the separator when passed the disk root directory.
  // We have to filter those out, but only when the "." doesn't come from the
  // completion request itself.
  if ((PartialItem == "." || PartialItem == path::get_separator()) &&
      path::is_separator(CompletionBuffer.back()))
    PartialItem = llvm::StringRef();

  if (SearchDir.empty()) {
    llvm::sys::fs::current_path(Storage);
    SearchDir = Storage;
  }
  assert(!PartialItem.contains(path::get_separator()));

  // SearchDir now contains the directory to search in, and Prefix contains the
  // text we want to match against items in that directory.

  FileSystem &fs = FileSystem::Instance();
  std::error_code EC;
  llvm::vfs::directory_iterator Iter = fs.DirBegin(SearchDir, EC);
  llvm::vfs::directory_iterator End;
  for (; Iter != End && !EC && request.ShouldAddCompletions();
       Iter.increment(EC)) {
    auto &Entry = *Iter;
    llvm::ErrorOr<llvm::vfs::Status> Status = fs.GetStatus(Entry.path());

    if (!Status)
      continue;

    auto Name = path::filename(Entry.path());

    // Omit ".", ".."
    if (Name == "." || Name == ".." || !Name.starts_with(PartialItem))
      continue;

    bool is_dir = Status->isDirectory();

    // If it's a symlink, then we treat it as a directory as long as the target
    // is a directory.
    if (Status->isSymlink()) {
      FileSpec symlink_filespec(Entry.path());
      FileSpec resolved_filespec;
      auto error = fs.ResolveSymbolicLink(symlink_filespec, resolved_filespec);
      if (error.Success())
        is_dir = fs.IsDirectory(symlink_filespec);
    }

    if (only_directories && !is_dir)
      continue;

    // Shrink it back down so that it just has the original prefix the user
    // typed and remove the part of the name which is common to the located
    // item and what the user typed.
    CompletionBuffer.resize(FullPrefixLen);
    Name = Name.drop_front(PartialItem.size());
    CompletionBuffer.append(Name);

    if (is_dir) {
      path::append(CompletionBuffer, path::get_separator());
    }

    CompletionMode mode =
        is_dir ? CompletionMode::Partial : CompletionMode::Normal;
    request.AddCompletion(CompletionBuffer, "", mode);
  }
}

void DiskFilesOrDirectories(const llvm::Twine &partial_name,
                            bool only_directories, StringList &matches,
                            TildeExpressionResolver &Resolver) {
  CompletionResult result;
  std::string partial_name_str = partial_name.str();
  CompletionRequest request(partial_name_str, partial_name_str.size(), result);
  DiskFilesOrDirectories(partial_name, only_directories, request, Resolver);
  result.GetMatches(matches);
}

} // namespace

void path_completion::CompleteFiles(const llvm::Twine &partial_path,
                                    StringList &matches,
                                    TildeExpressionResolver &resolver) {
  DiskFilesOrDirectories(partial_path, /*only_directories=*/false, matches,
                         resolver);
}

void path_completion::CompleteDirectories(const llvm::Twine &partial_path,
                                          StringList &matches,
                                          TildeExpressionResolver &resolver) {
  DiskFilesOrDirectories(partial_path, /*only_directories=*/true, matches,
                         resolver);
}

void path_completion::Complete(const llvm::Twine &partial_path,
                               bool only_directories,
                               CompletionRequest &request,
                               TildeExpressionResolver &resolver) {
  DiskFilesOrDirectories(partial_path, only_directories, request, resolver);
}
