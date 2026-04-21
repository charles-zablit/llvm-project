//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ExpectedMustTakeErrorCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using namespace clang::ast_matchers;

namespace clang::tidy::llvm_check {

void ExpectedMustTakeErrorCheck::registerMatchers(MatchFinder *Finder) {
  auto ExpectedType =
      qualType(hasDeclaration(namedDecl(hasName("::llvm::Expected"))));
  Finder->addMatcher(
      varDecl(unless(parmVarDecl()), hasAutomaticStorageDuration(),
              hasType(ExpectedType))
          .bind("var"),
      this);
}

namespace {

// Walk only the statement tree rooted at S.  DeclStmts are handled by
// recursing into each VarDecl's initializer (and nothing else in the decl
// hierarchy) so we never accidentally enter ObjC category/interface cycles or
// template instantiation graphs.  LambdaExprs are skipped because a lambda
// body is a different function scope.
struct StmtWalker {
  virtual void visitExpr(const Expr *E) = 0;
  virtual void visitReturn(const ReturnStmt *R) = 0;

  void walk(const Stmt *S) {
    if (!S)
      return;
    if (isa<LambdaExpr>(S))
      return;
    if (const auto *DS = dyn_cast<DeclStmt>(S)) {
      for (const Decl *D : DS->decls())
        if (const auto *VD = dyn_cast<VarDecl>(D))
          if (VD->hasInit())
            walk(VD->getInit());
      return;
    }
    if (const auto *E = dyn_cast<Expr>(S))
      visitExpr(E);
    if (const auto *R = dyn_cast<ReturnStmt>(S))
      visitReturn(R);
    for (const Stmt *Child : S->children())
      walk(Child);
  }
};

struct TakeErrorFinder : StmtWalker {
  const VarDecl *Target;
  bool Found = false;

  void visitExpr(const Expr *E) override {
    const auto *MCE = dyn_cast<CXXMemberCallExpr>(E);
    if (!MCE || !MCE->getMethodDecl())
      return;
    const IdentifierInfo *II = MCE->getMethodDecl()->getIdentifier();
    if (!II || II->getName() != "takeError")
      return;
    const Expr *Obj = MCE->getImplicitObjectArgument();
    if (Obj)
      Obj = Obj->IgnoreParenImpCasts();
    if (const auto *DRE = dyn_cast_or_null<DeclRefExpr>(Obj))
      if (DRE->getDecl() == Target)
        Found = true;
  }
  void visitReturn(const ReturnStmt *) override {}
};

struct ReturnVarFinder : StmtWalker {
  const VarDecl *Target;
  bool Found = false;

  void visitExpr(const Expr *) override {}
  void visitReturn(const ReturnStmt *R) override {
    if (!R->getRetValue())
      return;
    if (isDirectRef(R->getRetValue()->IgnoreImplicit()))
      Found = true;
  }

private:
  bool isDirectRef(const Expr *E) {
    if (const auto *CE = dyn_cast<CXXConstructExpr>(E))
      if (CE->getNumArgs() == 1)
        return isDirectRef(CE->getArg(0)->IgnoreImplicit());
    if (const auto *Call = dyn_cast<CallExpr>(E))
      if (Call->getNumArgs() == 1)
        if (const FunctionDecl *FD = Call->getDirectCallee())
          if (const IdentifierInfo *II = FD->getIdentifier())
            if (II->getName() == "move")
              return isDirectRef(Call->getArg(0)->IgnoreImplicit());
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
      return DRE->getDecl() == Target;
    return false;
  }
};

} // namespace

void ExpectedMustTakeErrorCheck::check(
    const MatchFinder::MatchResult &Result) {
  const auto *Var = Result.Nodes.getNodeAs<VarDecl>("var");

  const auto *FD =
      dyn_cast_or_null<FunctionDecl>(Var->getParentFunctionOrMethod());
  if (!FD || !FD->hasBody())
    return;

  TakeErrorFinder TEFinder;
  TEFinder.Target = Var;
  TEFinder.walk(FD->getBody());
  if (TEFinder.Found)
    return;

  ReturnVarFinder RFinder;
  RFinder.Target = Var;
  RFinder.walk(FD->getBody());
  if (RFinder.Found)
    return;

  diag(Var->getLocation(),
       "llvm::Expected variable %0 must have .takeError() called or be "
       "returned to propagate the error to the caller")
      << Var;
}

} // namespace clang::tidy::llvm_check
