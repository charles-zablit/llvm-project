//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_LLVM_EXPECTEDMUSTTAKEERRORCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_LLVM_EXPECTEDMUSTTAKEERRORCHECK_H

#include "../ClangTidyCheck.h"

namespace clang::tidy::llvm_check {

/// Checks that every local variable of type \c llvm::Expected<T> has
/// \c .takeError() called on it in the same function, or is returned to
/// propagate the error to the caller.
///
/// Failing to call \c takeError() before an \c llvm::Expected goes out of
/// scope results in an assertion failure in the destructor.
class ExpectedMustTakeErrorCheck : public ClangTidyCheck {
public:
  ExpectedMustTakeErrorCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context) {}
  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.CPlusPlus;
  }
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
};

} // namespace clang::tidy::llvm_check

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_LLVM_EXPECTEDMUSTTAKEERRORCHECK_H
