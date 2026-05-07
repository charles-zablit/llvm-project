//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_CPLUSPLUS_MICROSOFTABIRUNTIME_H
#define LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_CPLUSPLUS_MICROSOFTABIRUNTIME_H

#include "lldb/Target/LanguageRuntime.h"
#include "lldb/ValueObject/ValueObject.h"

namespace lldb_private {

class MicrosoftABIRuntime {
public:
  MicrosoftABIRuntime(Process *process);

  llvm::Expected<LanguageRuntime::VTableInfo>
  GetVTableInfo(ValueObject &in_value, bool check_type);

  bool GetDynamicTypeAndAddress(ValueObject &in_value,
                                lldb::DynamicValueType use_dynamic,
                                TypeAndOrName &class_type_or_name,
                                Address &dynamic_address,
                                Value::ValueType &value_type);

private:
  TypeAndOrName GetTypeInfo(ValueObject &in_value,
                            const LanguageRuntime::VTableInfo &vtable_info);

  llvm::Error TypeHasVTable(CompilerType type);

  TypeAndOrName GetDynamicTypeInfo(const lldb_private::Address &vtable_addr);

  void SetDynamicTypeInfo(const lldb_private::Address &vtable_addr,
                          const TypeAndOrName &type_info);

  using DynamicTypeCache = std::map<Address, TypeAndOrName>;
  using VTableInfoCache = std::map<Address, LanguageRuntime::VTableInfo>;

  DynamicTypeCache m_dynamic_type_map;
  VTableInfoCache m_vtable_info_map;
  std::mutex m_mutex;

  Process *m_process;
};

} // namespace lldb_private

#endif
