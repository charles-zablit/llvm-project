//===-- DynamicLoaderWindowsDYLD.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DynamicLoaderWindowsDYLD.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/ThreadPlanStepInstruction.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "llvm/TargetParser/Triple.h"

#ifdef _WIN32
#include "lldb/Host/windows/windows.h"
#include "llvm/Support/ConvertUTF.h"
#endif

using namespace lldb;
using namespace lldb_private;

#ifdef _WIN32
/// On Windows, \c subst drives map a drive letter to an arbitrary path on
/// another volume (e.g. \c "subst S: C:\S" makes \c S:\ equivalent to
/// \c C:\S\ ). When a target is created LLDB records the executable path as
/// provided by the user (e.g. \c S:\...\a.out), but when the process loads
/// the module Windows reports its path using the real underlying path
/// (e.g. \c C:\S\...\a.out). The two paths refer to the same file but compare
/// unequal, causing \c GetOrCreateModule to create a duplicate \c Module.
///
/// This function converts \p file_spec to its subst-drive equivalent when one
/// exists, so that the incoming path matches whatever path the target already
/// uses for the same file.
static FileSpec ResolveToSubstPath(const FileSpec &file_spec) {
  std::string path = file_spec.GetPath();

  std::wstring wpath;
  if (!llvm::ConvertUTF8toWide(path, wpath))
    return file_spec;

  // Enumerate all logical drives and check whether any is a subst drive
  // whose target is a prefix of the incoming path.
  std::array<wchar_t, 512> drive_strings;
  drive_strings[0] = L'\0';
  if (!::GetLogicalDriveStringsW(drive_strings.size(), drive_strings.data()))
    return file_spec;

  std::array<wchar_t, 3> drive_buf = {L'_', L':', L'\0'};
  for (const wchar_t *it = drive_strings.data(); *it != L'\0';
       it += wcslen(it) + 1) {
    drive_buf[0] = it[0];
    std::array<wchar_t, MAX_PATH> device_name;
    if (!::QueryDosDeviceW(drive_buf.data(), device_name.data(),
                           device_name.size()))
      continue;

    // Subst drives appear as "\??\<real-path>" (e.g. "\??\C:\S").
    // Real drives map to "\Device\Harddisk..." — skip those.
    std::wstring_view device(device_name.data());
    if (device.substr(0, 4) != L"\\??\\")
      continue;
    std::wstring_view subst_target = device.substr(4);
    if (subst_target.empty())
      continue;

    // Case-insensitive prefix check — Windows paths are case-insensitive.
    if (wpath.size() < subst_target.size())
      continue;
    if (_wcsnicmp(wpath.c_str(), subst_target.data(), subst_target.size()) != 0)
      continue;

    // The match must land on a path separator (or be the full string).
    size_t n = subst_target.size();
    if (n < wpath.size() && wpath[n] != L'\\' && wpath[n] != L'/')
      continue;

    // Rebuild: <subst-drive-letter> + ":" + remainder
    std::wstring rebuilt(drive_buf.data(), 2); // e.g. L"S:"
    rebuilt += wpath.substr(n);
    std::string new_path;
    if (llvm::convertWideToUTF8(rebuilt, new_path))
      return FileSpec(new_path);
  }

  return file_spec;
}
#endif // _WIN32

LLDB_PLUGIN_DEFINE(DynamicLoaderWindowsDYLD)

DynamicLoaderWindowsDYLD::DynamicLoaderWindowsDYLD(Process *process)
    : DynamicLoader(process) {}

DynamicLoaderWindowsDYLD::~DynamicLoaderWindowsDYLD() = default;

void DynamicLoaderWindowsDYLD::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void DynamicLoaderWindowsDYLD::Terminate() {}

llvm::StringRef DynamicLoaderWindowsDYLD::GetPluginDescriptionStatic() {
  return "Dynamic loader plug-in that watches for shared library "
         "loads/unloads in Windows processes.";
}

DynamicLoader *DynamicLoaderWindowsDYLD::CreateInstance(Process *process,
                                                        bool force) {
  bool should_create = force;
  if (!should_create) {
    const llvm::Triple &triple_ref =
        process->GetTarget().GetArchitecture().GetTriple();
    if (triple_ref.getOS() == llvm::Triple::Win32)
      should_create = true;
  }

  if (should_create)
    return new DynamicLoaderWindowsDYLD(process);

  return nullptr;
}

void DynamicLoaderWindowsDYLD::OnLoadModule(lldb::ModuleSP module_sp,
                                            const ModuleSpec module_spec,
                                            lldb::addr_t module_addr) {

  // Resolve the module unless we already have one.
  if (!module_sp) {
    Status error;
    // On Windows the OS reports module paths using the real underlying path
    // (e.g. C:\S\...) while the target may have been created with a subst
    // drive path (e.g. S:\...). Normalize the incoming path so that
    // GetOrCreateModule can find an existing module rather than creating a
    // duplicate with a different path but the same file on disk.
    ModuleSpec resolved_spec(module_spec);
#ifdef _WIN32
    resolved_spec.GetFileSpec() = ResolveToSubstPath(module_spec.GetFileSpec());
#endif
    module_sp = m_process->GetTarget().GetOrCreateModule(resolved_spec,
                                             false /* notify */, &error);
    if (error.Fail())
      return;
  }

  m_loaded_modules[module_addr] = module_sp;
  UpdateLoadedSectionsCommon(module_sp, module_addr, false);
  ModuleList module_list;
  module_list.Append(module_sp);
  m_process->GetTarget().ModulesDidLoad(module_list);
}

void DynamicLoaderWindowsDYLD::OnUnloadModule(lldb::addr_t module_addr) {
  // Look up the module in our own map first — it is cheaper than a section
  // load list walk and works even if the section list has already been cleared.
  auto it = m_loaded_modules.find(module_addr);
  if (it == m_loaded_modules.end())
    return;

  ModuleSP module_sp = it->second;
  m_loaded_modules.erase(it);

  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOGF(log, "OnUnloadModule: unloading %s at 0x%" PRIx64,
            module_sp->GetFileSpec().GetFilename().AsCString("<?>"),
            module_addr);

  // Check whether the same module is still loaded at a different address.
  // On Windows a DLL can be mapped at more than one address simultaneously
  // during startup (e.g. a temporary loader staging address followed by the
  // final ASLR address).  If another entry still exists for this module we
  // must not clear its section load addresses; instead, re-register sections
  // at the surviving address so the reflection context can find them.
  for (const auto &entry : m_loaded_modules) {
    if (entry.second == module_sp) {
      LLDB_LOGF(log,
                "OnUnloadModule: %s still loaded at 0x%" PRIx64
                ", keeping sections",
                module_sp->GetFileSpec().GetFilename().AsCString("<?>"),
                entry.first);
      UpdateLoadedSectionsCommon(module_sp, entry.first, false);
      return;
    }
  }

  // No other active address — proceed with a full unload.
  UnloadSectionsCommon(module_sp);
  ModuleList module_list;
  module_list.Append(module_sp);
  m_process->GetTarget().ModulesDidUnload(module_list, false);
}

lldb::addr_t DynamicLoaderWindowsDYLD::GetLoadAddress(ModuleSP executable) {
  // First, see if the load address is already cached.
  for (const auto &entry : m_loaded_modules) {
    if (entry.second == executable && entry.first != LLDB_INVALID_ADDRESS)
      return entry.first;
  }

  lldb::addr_t load_addr = LLDB_INVALID_ADDRESS;

  // Second, try to get it through the process plugins.  For a remote process,
  // the remote platform will be responsible for providing it.
  FileSpec file_spec(executable->GetPlatformFileSpec());
  bool is_loaded = false;
  Status status =
      m_process->GetFileLoadAddress(file_spec, is_loaded, load_addr);
  // Servers other than lldb server could respond with a bogus address.
  if (status.Success() && is_loaded && load_addr != LLDB_INVALID_ADDRESS) {
    m_loaded_modules[load_addr] = executable;
    return load_addr;
  }

  return LLDB_INVALID_ADDRESS;
}

void DynamicLoaderWindowsDYLD::DidAttach() {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOGF(log, "DynamicLoaderWindowsDYLD::%s()", __FUNCTION__);

  ModuleSP executable = GetTargetExecutable();

  if (!executable.get())
    return;

  // Try to fetch the load address of the file from the process, since there
  // could be randomization of the load address.
  lldb::addr_t load_addr = GetLoadAddress(executable);
  if (load_addr == LLDB_INVALID_ADDRESS)
    return;

  // Request the process base address.
  lldb::addr_t image_base = m_process->GetImageInfoAddress();
  if (image_base == load_addr)
    return;

  // Rebase the process's modules if there is a mismatch.
  UpdateLoadedSections(executable, LLDB_INVALID_ADDRESS, load_addr, false);

  ModuleList module_list;
  module_list.Append(executable);
  m_process->GetTarget().ModulesDidLoad(module_list);
  auto error = m_process->LoadModules();
  LLDB_LOG_ERROR(log, std::move(error), "failed to load modules: {0}");
}

void DynamicLoaderWindowsDYLD::DidLaunch() {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  LLDB_LOGF(log, "DynamicLoaderWindowsDYLD::%s()", __FUNCTION__);

  ModuleSP executable = GetTargetExecutable();
  if (!executable.get())
    return;

  lldb::addr_t load_addr = GetLoadAddress(executable);
  if (load_addr != LLDB_INVALID_ADDRESS) {
    // Update the loaded sections so that the breakpoints can be resolved.
    UpdateLoadedSections(executable, LLDB_INVALID_ADDRESS, load_addr, false);

    ModuleList module_list;
    module_list.Append(executable);
    m_process->GetTarget().ModulesDidLoad(module_list);
    auto error = m_process->LoadModules();
    LLDB_LOG_ERROR(log, std::move(error), "failed to load modules: {0}");
  }
}

Status DynamicLoaderWindowsDYLD::CanLoadImage() { return Status(); }

ThreadPlanSP
DynamicLoaderWindowsDYLD::GetStepThroughTrampolinePlan(Thread &thread,
                                                       bool stop) {
  auto arch = m_process->GetTarget().GetArchitecture();
  if (arch.GetMachine() != llvm::Triple::x86) {
    return ThreadPlanSP();
  }

  uint64_t pc = thread.GetRegisterContext()->GetPC();
  // Max size of an instruction in x86 is 15 bytes.
  AddressRange range(pc, 2 * 15);

  DisassemblerSP disassembler_sp = Disassembler::DisassembleRange(
      arch, nullptr, nullptr, nullptr, nullptr, m_process->GetTarget(), range);
  if (!disassembler_sp) {
    return ThreadPlanSP();
  }

  InstructionList *insn_list = &disassembler_sp->GetInstructionList();
  if (insn_list == nullptr) {
    return ThreadPlanSP();
  }

  // First instruction in a x86 Windows trampoline is going to be an indirect
  // jump through the IAT and the next one will be a nop (usually there for
  // alignment purposes). e.g.:
  //     0x70ff4cfc <+956>: jmpl   *0x7100c2a8
  //     0x70ff4d02 <+962>: nop

  auto first_insn = insn_list->GetInstructionAtIndex(0);
  auto second_insn = insn_list->GetInstructionAtIndex(1);

  ExecutionContext exe_ctx(m_process->GetTarget());
  if (first_insn == nullptr || second_insn == nullptr ||
      strcmp(first_insn->GetMnemonic(&exe_ctx), "jmpl") != 0 ||
      strcmp(second_insn->GetMnemonic(&exe_ctx), "nop") != 0) {
    return ThreadPlanSP();
  }

  assert(first_insn->DoesBranch() && !second_insn->DoesBranch());

  return ThreadPlanSP(new ThreadPlanStepInstruction(
      thread, false, false, eVoteNoOpinion, eVoteNoOpinion));
}
