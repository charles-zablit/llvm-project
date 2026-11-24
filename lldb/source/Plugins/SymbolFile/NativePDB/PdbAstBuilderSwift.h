//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_PDBASTBUILDERSWIFT_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_PDBASTBUILDERSWIFT_H

#include "PdbAstBuilder.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm::codeview {
class ClassRecord;
class UDTSym;
}

namespace llvm::pdb {
class TpiStream;
}

namespace lldb_private {

class TypeSystemSwiftTypeRef;

namespace npdb {

class PdbAstBuilderSwift : public PdbAstBuilder {
public:
  PdbAstBuilderSwift(TypeSystemSwiftTypeRef &swift_ts);

  CompilerDecl GetOrCreateDeclForUid(PdbSymUid uid) override { return {}; }
  CompilerDeclContext GetOrCreateDeclContextForUid(PdbSymUid uid) override {
    return {};
  }
  CompilerDeclContext GetParentDeclContext(PdbSymUid uid) override {
    return {};
  }

  void EnsureFunction(PdbCompilandSymId func_id) override {}
  void EnsureInlinedFunction(PdbCompilandSymId inlinesite_id) override {}
  void EnsureBlock(PdbCompilandSymId block_id) override {}
  void EnsureVariable(PdbCompilandSymId scope_id,
                      PdbCompilandSymId var_id) override {}
  void EnsureVariable(PdbGlobalSymId var_id) override {}

  CompilerType GetOrCreateType(PdbTypeSymId type) override;
  /// The Swift type alias of the S_UDT \p id, if it is one.
  CompilerType GetOrCreateTypedefType(PdbGlobalSymId id) override;
  bool CompleteType(CompilerType ct) override { return true; }

  void ParseDeclsForContext(CompilerDeclContext context) override {}

  CompilerDeclContext FindNamespaceDecl(CompilerDeclContext parent_ctx,
                                        llvm::StringRef name) override {
    return {};
  }

  std::string GetQualifiedName(llvm::StringRef name) override {
    return name.str();
  }

  void Dump(Stream &stream, llvm::StringRef filter, bool show_color) override;

  /// The type of the Swift function \p mangled_name, as DWARFASTParserSwift
  /// gives it: CodeView only has a C-like signature.
  CompilerType GetFunctionType(llvm::StringRef mangled_name);

  /// The mangled name of the Swift type alias \p udt would be: older
  /// compilers qualified it by the scopes of the alias (`Swift::$ss5CCharaD`).
  static llvm::StringRef GetTypeAliasName(const llvm::codeview::UDTSym &udt);

  /// To avoid issues with uniquing, bound generics are emitted as follows in DWARF:
  /// - A sized outer structure with no name or identifier
  /// - A sizeless inner structure with the mangled name as the name and no identifier.
  /// The latter is an unnamed member of the former.
  /// CodeView deviates in two major ways:
  /// - Unnamed types are emitted as module name + `::<unnamed-tag>` instead of having an empty name.
  /// - Unnamed forward declared members are dropped entirely. To get around this, we name
  ///   the member with the mangled name of the inner type, since this is illegal in a
  ///   user-written type. See https://github.com/swiftlang/swift/issues/87093
  /// This function returns:
  /// - an error if deserialization fails while checking.
  /// - an empty string if `cr` is not shaped like a bound generic.
  /// - the mangled name of the inner type if `cr` is shaped like a bound generic.
  static llvm::Expected<llvm::StringRef>
  MaybeUnwrapBoundGeneric(const llvm::codeview::ClassRecord &cr,
                          llvm::pdb::TpiStream &tpi);

private:
  CompilerType CreateType(PdbTypeSymId type, llvm::pdb::TpiStream &tpi);

  TypeSystemSwiftTypeRef &m_swift_ts;
  llvm::DenseMap<lldb::user_id_t, CompilerType> m_uid_to_type;
};

} // namespace npdb
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_PDBASTBUILDERSWIFT_H
