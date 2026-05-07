//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MicrosoftABIRuntime.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Utility/LLDBLog.h"

using namespace lldb;
using namespace lldb_private;

// Microsoft C++ ABI vtable symbols are demangled in forms such as:
//   const Foo::`vftable'
//   const Foo::`vftable'{for `Bar'}
//   const ns::Foo::`vftable'
// The class name (the most-derived type) is the part before "::`vftable'".
static constexpr llvm::StringLiteral g_vftable_separator = "::`vftable'";
static constexpr llvm::StringLiteral g_const_prefix = "const ";

static bool IsMSVCVTableSymbol(llvm::StringRef demangled_name) {
  return demangled_name.contains(g_vftable_separator);
}

static llvm::StringRef ExtractClassName(llvm::StringRef demangled_name) {
  demangled_name.consume_front(g_const_prefix);
  size_t pos = demangled_name.find(g_vftable_separator);
  if (pos == llvm::StringRef::npos)
    return {};
  return demangled_name.substr(0, pos);
}

MicrosoftABIRuntime::MicrosoftABIRuntime(Process *process)
    : m_process(process) {}

TypeAndOrName MicrosoftABIRuntime::GetTypeInfo(
    ValueObject &in_value, const LanguageRuntime::VTableInfo &vtable_info) {
  if (!vtable_info.addr.IsSectionOffset())
    return TypeAndOrName();

  // See if we have cached info for this type already.
  TypeAndOrName type_info = GetDynamicTypeInfo(vtable_info.addr);
  if (type_info)
    return type_info;

  if (!vtable_info.symbol)
    return TypeAndOrName();

  Log *log = GetLog(LLDBLog::Object);
  llvm::StringRef symbol_name =
      vtable_info.symbol->GetMangled().GetDemangledName().GetStringRef();
  LLDB_LOGF(log,
            "0x%16.16" PRIx64 ": static-type = '%s' has vftable symbol '%s'\n",
            in_value.GetPointerValue().address,
            in_value.GetTypeName().GetCString(), symbol_name.str().c_str());

  llvm::StringRef class_name = ExtractClassName(symbol_name);
  if (class_name.empty())
    return TypeAndOrName();

  std::string lookup_name("::");
  lookup_name.append(class_name.data(), class_name.size());

  type_info.SetName(class_name);
  ConstString const_lookup_name(lookup_name);
  TypeList class_types;
  ModuleSP module_sp = vtable_info.symbol->CalculateSymbolContextModule();
  // First look in the module that the vtable symbol came from and look for a
  // single exact match.
  TypeResults results;
  TypeQuery query(const_lookup_name.GetStringRef(),
                  TypeQueryOptions::e_exact_match |
                      TypeQueryOptions::e_strict_namespaces |
                      TypeQueryOptions::e_find_one);
  if (module_sp) {
    module_sp->FindTypes(query, results);
    TypeSP type_sp = results.GetFirstType();
    if (type_sp)
      class_types.Insert(type_sp);
  }

  // If we didn't find a type, then move on to the entire module list in the
  // target and get as many unique matches as possible.
  if (class_types.Empty()) {
    query.SetFindOne(false);
    m_process->GetTarget().GetImages().FindTypes(nullptr, query, results);
    for (const auto &type_sp : results.GetTypeMap().Types())
      class_types.Insert(type_sp);
  }

  lldb::TypeSP type_sp;
  if (class_types.Empty()) {
    LLDB_LOGF(log, "0x%16.16" PRIx64 ": is not dynamic\n",
              in_value.GetPointerValue().address);
    return TypeAndOrName();
  }
  if (class_types.GetSize() == 1) {
    type_sp = class_types.GetTypeAtIndex(0);
    if (type_sp &&
        TypeSystemClang::IsCXXClassType(type_sp->GetForwardCompilerType())) {
      LLDB_LOGF(log,
                "0x%16.16" PRIx64
                ": static-type = '%s' has dynamic type: uid={0x%" PRIx64
                "}, type-name='%s'\n",
                in_value.GetPointerValue().address,
                in_value.GetTypeName().AsCString(""), type_sp->GetID(),
                type_sp->GetName().GetCString());
      type_info.SetTypeSP(type_sp);
    }
  } else {
    for (size_t i = 0; i < class_types.GetSize(); ++i) {
      type_sp = class_types.GetTypeAtIndex(i);
      if (type_sp &&
          TypeSystemClang::IsCXXClassType(type_sp->GetForwardCompilerType())) {
        LLDB_LOGF(log,
                  "0x%16.16" PRIx64 ": static-type = '%s' has multiple "
                  "matching dynamic types, picking "
                  "this one: uid={0x%" PRIx64 "}, type-name='%s'\n",
                  in_value.GetPointerValue().address,
                  in_value.GetTypeName().AsCString(""), type_sp->GetID(),
                  type_sp->GetName().GetCString());
        type_info.SetTypeSP(type_sp);
        break;
      }
    }
    if (!type_info)
      LLDB_LOGF(log,
                "0x%16.16" PRIx64
                ": static-type = '%s' has multiple matching dynamic "
                "types, didn't find a C++ match\n",
                in_value.GetPointerValue().address,
                in_value.GetTypeName().AsCString(""));
  }

  if (type_info)
    SetDynamicTypeInfo(vtable_info.addr, type_info);
  return type_info;
}

llvm::Error MicrosoftABIRuntime::TypeHasVTable(CompilerType type) {
  CompilerType original_type = type;
  if (type.IsPointerOrReferenceType()) {
    CompilerType pointee_type = type.GetPointeeType();
    if (pointee_type)
      type = pointee_type;
  }

  if ((type.GetTypeClass() & (eTypeClassStruct | eTypeClassClass)) == 0)
    return llvm::createStringError(
        std::errc::invalid_argument,
        "type \"%s\" is not a class or struct or a pointer to one",
        original_type.GetTypeName().AsCString("<invalid>"));

  if (!type.IsPolymorphicClass())
    return llvm::createStringError(std::errc::invalid_argument,
                                   "type \"%s\" doesn't have a vtable",
                                   type.GetTypeName().AsCString("<invalid>"));
  return llvm::Error::success();
}

llvm::Expected<LanguageRuntime::VTableInfo>
MicrosoftABIRuntime::GetVTableInfo(ValueObject &in_value, bool check_type) {
  CompilerType type = in_value.GetCompilerType();
  if (check_type) {
    if (llvm::Error err = TypeHasVTable(type))
      return std::move(err);
  }
  ExecutionContext exe_ctx(in_value.GetExecutionContextRef());
  Process *process = exe_ctx.GetProcessPtr();
  if (process == nullptr)
    return llvm::createStringError(std::errc::invalid_argument,
                                   "invalid process");

  auto [original_ptr, address_type] =
      type.IsPointerOrReferenceType()
          ? in_value.GetPointerValue()
          : in_value.GetAddressOf(/*scalar_is_load_address=*/true);
  if (original_ptr == LLDB_INVALID_ADDRESS || address_type != eAddressTypeLoad)
    return llvm::createStringError(std::errc::invalid_argument,
                                   "failed to get the address of the value");

  Status error;
  lldb::addr_t vtable_load_addr =
      process->ReadPointerFromMemory(original_ptr, error);

  if (!error.Success() || vtable_load_addr == LLDB_INVALID_ADDRESS)
    return llvm::createStringError(
        std::errc::invalid_argument,
        "failed to read vtable pointer from memory at 0x%" PRIx64,
        original_ptr);

  // Find the symbol that contains "vtable_load_addr".
  Address vtable_addr;
  if (!process->GetTarget().ResolveLoadAddress(vtable_load_addr, vtable_addr))
    return llvm::createStringError(std::errc::invalid_argument,
                                   "failed to resolve vtable pointer 0x%" PRIx64
                                   "to a section",
                                   vtable_load_addr);

  {
    std::lock_guard<std::mutex> locker(m_mutex);
    auto pos = m_vtable_info_map.find(vtable_addr);
    if (pos != m_vtable_info_map.end())
      return pos->second;
  }

  Symbol *symbol = vtable_addr.CalculateSymbolContextSymbol();
  if (symbol == nullptr)
    return llvm::createStringError(std::errc::invalid_argument,
                                   "no symbol found for 0x%" PRIx64,
                                   vtable_load_addr);
  llvm::StringRef name = symbol->GetMangled().GetDemangledName().GetStringRef();
  if (IsMSVCVTableSymbol(name)) {
    LanguageRuntime::VTableInfo info = {vtable_addr, symbol};
    std::lock_guard<std::mutex> locker(m_mutex);
    m_vtable_info_map[vtable_addr] = info;
    return info;
  }
  return llvm::createStringError(std::errc::invalid_argument,
                                 "symbol found that contains 0x%" PRIx64
                                 " is not a vftable symbol",
                                 vtable_load_addr);
}

bool MicrosoftABIRuntime::GetDynamicTypeAndAddress(
    ValueObject &in_value, lldb::DynamicValueType use_dynamic,
    TypeAndOrName &class_type_or_name, Address &dynamic_address,
    Value::ValueType &value_type) {
  // For the Microsoft ABI, polymorphic objects start with a vfptr that points
  // to a vftable. The vftable symbol's demangled name encodes the most-derived
  // class. The pointer-sized slot immediately before the vftable holds the
  // address of an RTTI Complete Object Locator (COL); the COL's "offset" field
  // (4 bytes at offset 4) is the offset of the vfptr from the start of the
  // most-derived object, so the dynamic object starts at static_address minus
  // that offset.

  llvm::Expected<LanguageRuntime::VTableInfo> vtable_info_or_err =
      GetVTableInfo(in_value, /*check_type=*/false);
  if (!vtable_info_or_err) {
    llvm::consumeError(vtable_info_or_err.takeError());
    return false;
  }

  const LanguageRuntime::VTableInfo &vtable_info = vtable_info_or_err.get();
  class_type_or_name = GetTypeInfo(in_value, vtable_info);

  if (!class_type_or_name)
    return false;

  CompilerType type = class_type_or_name.GetCompilerType();
  // There can only be one type with a given name, so we've just found
  // duplicate definitions, and this one will do as well as any other. We
  // don't consider something to have a dynamic type if it is the same as
  // the static type. So compare against the value we were handed.
  if (!type)
    return true;

  if (TypeSystemClang::AreTypesSame(in_value.GetCompilerType(), type))
    return false;

  Target &target = m_process->GetTarget();
  const addr_t vtable_load_addr = vtable_info.addr.GetLoadAddress(&target);
  if (vtable_load_addr == LLDB_INVALID_ADDRESS)
    return false;

  const uint32_t addr_byte_size = m_process->GetAddressByteSize();
  // Read the COL pointer from the slot immediately before the vftable.
  const lldb::addr_t col_ptr_location = vtable_load_addr - addr_byte_size;
  if (col_ptr_location >= vtable_load_addr)
    return false;
  Status error;
  const lldb::addr_t col_addr =
      m_process->ReadPointerFromMemory(col_ptr_location, error);
  if (!error.Success() || col_addr == LLDB_INVALID_ADDRESS)
    return false;

  // Read the offset field of the COL: 4-byte unsigned at offset 4.
  const uint64_t offset_field_size = 4;
  const uint64_t offset = target.ReadUnsignedIntegerFromMemory(
      Address(col_addr + 4), offset_field_size, UINT64_MAX, error);
  if (!error.Success() || offset == UINT64_MAX)
    return false;

  // The dynamic (most-derived) object starts at static_address - offset.
  const lldb::addr_t static_addr = in_value.GetPointerValue().address;
  if (offset > static_addr)
    return false;
  const lldb::addr_t dynamic_addr = static_addr - offset;
  if (!target.ResolveLoadAddress(dynamic_addr, dynamic_address))
    dynamic_address.SetRawAddress(dynamic_addr);
  return true;
}

TypeAndOrName MicrosoftABIRuntime::GetDynamicTypeInfo(
    const lldb_private::Address &vtable_addr) {
  std::lock_guard<std::mutex> locker(m_mutex);
  DynamicTypeCache::const_iterator pos = m_dynamic_type_map.find(vtable_addr);
  if (pos == m_dynamic_type_map.end())
    return TypeAndOrName();
  return pos->second;
}

void MicrosoftABIRuntime::SetDynamicTypeInfo(
    const lldb_private::Address &vtable_addr, const TypeAndOrName &type_info) {
  std::lock_guard<std::mutex> locker(m_mutex);
  m_dynamic_type_map[vtable_addr] = type_info;
}
