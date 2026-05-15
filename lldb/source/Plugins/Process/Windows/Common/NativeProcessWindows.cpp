//===-- NativeProcessWindows.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/windows/windows.h"
#include <psapi.h>

#include "NativeProcessWindows.h"
#include "NativeThreadWindows.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostNativeProcessBase.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Host/windows/AutoHandle.h"
#include "lldb/Host/windows/HostThreadWindows.h"
#include "lldb/Host/windows/ProcessLauncherWindows.h"
#include "lldb/Host/windows/PseudoConsole.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/State.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#include "DebuggerThread.h"
#include "ExceptionRecord.h"
#include "ProcessWindowsLog.h"

#include <tlhelp32.h>

#pragma warning(disable : 4005)
#include "winternl.h"
#include <ntstatus.h>

using namespace lldb;
using namespace lldb_private;
using namespace llvm;

namespace lldb_private {

namespace {
// Decode the well-known ExceptionInformation[] payloads for a few exception
// codes into a human-readable suffix. Mirrors
// ProcessWindows::DumpAdditionalExceptionInformation -- that lives on the
// in-process plugin side, so when the client is ProcessGDBRemote (lldb-server)
// the rich text never reaches the user. Generate it here and ship it as the
// stop-info description so both paths look the same.
void AppendExceptionDetails(llvm::raw_ostream &stream,
                            const ExceptionRecord &record) {
  const int addr_min_width = 2 + 8;
  const std::vector<ULONG_PTR> &args = record.GetExceptionArguments();
  switch (record.GetExceptionCode()) {
  case EXCEPTION_ACCESS_VIOLATION: {
    if (args.size() < 2)
      break;
    stream << ": ";
    switch (args[0]) {
    case 0:
      stream << "Access violation reading";
      break;
    case 1:
      stream << "Access violation writing";
      break;
    case 8:
      stream << "User-mode data execution prevention (DEP) violation at";
      break;
    default:
      stream << "Unknown access violation (code " << args[0] << ") at";
      break;
    }
    stream << " location " << llvm::format_hex(args[1], addr_min_width);
    break;
  }
  case EXCEPTION_IN_PAGE_ERROR: {
    if (args.size() < 3)
      break;
    stream << ": ";
    switch (args[0]) {
    case 0:
      stream << "In page error reading";
      break;
    case 1:
      stream << "In page error writing";
      break;
    case 8:
      stream << "User-mode data execution prevention (DEP) violation at";
      break;
    default:
      stream << "Unknown page loading error (code " << args[0] << ") at";
      break;
    }
    stream << " location " << llvm::format_hex(args[1], addr_min_width)
           << " (status code " << llvm::format_hex(args[2], 8) << ")";
    break;
  }
  }
}
} // namespace

NativeProcessWindows::NativeProcessWindows(ProcessLaunchInfo &launch_info,
                                           NativeDelegate &delegate,
                                           llvm::Error &E)
    : NativeProcessProtocol(
          LLDB_INVALID_PROCESS_ID,
          PseudoTerminal::invalid_fd, // TODO: Implement on Windows
          delegate),
      ProcessDebugger(), m_arch(launch_info.GetArchitecture()) {
  ErrorAsOutParameter EOut(&E);
  DebugDelegateSP delegate_sp(new NativeDebugDelegate(*this));
  E = LaunchProcess(launch_info, delegate_sp).ToError();
  if (E)
    return;

  SetID(GetDebuggedProcessId());

  // Take ownership of the PseudoConsole after launch: the launcher reads
  // the child-side handles off launch_info, so they must still be in
  // place across the LaunchProcess call. Once the child is spawned, we
  // need the parent-side STDOUT HANDLE to keep reading after launch_info
  // is cleared.
  m_pty = launch_info.TakePTY();
  StartStdioReaderThread();
}

NativeProcessWindows::~NativeProcessWindows() {
  if (m_stdio_reader_thread.joinable()) {
    // Unblock the reader: signal the stop event and cancel any pending
    // overlapped ReadFile on the STDOUT HANDLE.
    if (m_stdio_stop_event)
      ::SetEvent(m_stdio_stop_event);
    if (m_pty && m_pty->GetSTDOUTHandle() != INVALID_HANDLE_VALUE)
      ::CancelIoEx(m_pty->GetSTDOUTHandle(), nullptr);
    m_stdio_reader_thread.join();
  }
  if (m_stdio_stop_event) {
    ::CloseHandle(m_stdio_stop_event);
    m_stdio_stop_event = nullptr;
  }
}

NativeProcessWindows::NativeProcessWindows(lldb::pid_t pid, int terminal_fd,
                                           NativeDelegate &delegate,
                                           llvm::Error &E)
    : NativeProcessProtocol(pid, terminal_fd, delegate), ProcessDebugger() {
  ErrorAsOutParameter EOut(&E);
  DebugDelegateSP delegate_sp(new NativeDebugDelegate(*this));
  ProcessAttachInfo attach_info;
  attach_info.SetProcessID(pid);
  E = AttachProcess(pid, attach_info, delegate_sp).ToError();
  if (E)
    return;

  SetID(GetDebuggedProcessId());

  ProcessInstanceInfo info;
  if (!Host::GetProcessInfo(pid, info)) {
    E = createStringError(inconvertibleErrorCode(),
                          "Cannot get process information");
    return;
  }
  m_arch = info.GetArchitecture();
}

Status NativeProcessWindows::Resume(const ResumeActionList &resume_actions) {
  Log *log = GetLog(WindowsLog::Process);
  Status error;
  llvm::sys::ScopedLock lock(m_mutex);

  StateType state = GetState();
  if (state == eStateStopped || state == eStateCrashed) {
    LLDB_LOG(log, "process {0} is in state {1}.  Resuming...",
             GetDebuggedProcessId(), state);
    LLDB_LOG(log, "resuming {0} threads.", m_threads.size());

    // Any library events the client was going to consume at the previous
    // stop have had their chance by now; any `library:1;` we send from here
    // on should describe changes since the resume.
    m_pending_library_events = false;

    bool failed = false;
    for (uint32_t i = 0; i < m_threads.size(); ++i) {
      auto thread = static_cast<NativeThreadWindows *>(m_threads[i].get());
      const ResumeAction *const action =
          resume_actions.GetActionForThread(thread->GetID(), true);
      if (action == nullptr)
        continue;

      switch (action->state) {
      case eStateRunning:
      case eStateStepping: {
        Status result = thread->DoResume(action->state);
        if (result.Fail()) {
          failed = true;
          LLDB_LOG(log,
                   "Trying to resume thread at index {0}, but failed with "
                   "error {1}.",
                   i, result);
        }
        break;
      }
      case eStateSuspended:
      case eStateStopped:
        break;

      default:
        return Status::FromErrorStringWithFormat(
            "NativeProcessWindows::%s (): unexpected state %s specified "
            "for pid %" PRIu64 ", tid %" PRIu64,
            __FUNCTION__, StateAsCString(action->state), GetID(),
            thread->GetID());
      }
    }

    if (failed) {
      error = Status::FromErrorString("NativeProcessWindows::DoResume failed");
    } else {
      SetState(eStateRunning);
    }

    // Resume the debug loop.
    ExceptionRecordSP active_exception =
        m_session_data->m_debugger->GetActiveException().lock();
    if (active_exception) {
      // Resume the process and continue processing debug events.  Mask the
      // exception so that from the process's view, there is no indication that
      // anything happened.
      m_session_data->m_debugger->ContinueAsyncException(
          ExceptionResult::MaskException);
    }
  } else {
    LLDB_LOG(log, "error: process {0} is in state {1}.  Returning...",
             GetDebuggedProcessId(), GetState());
  }

  return error;
}

NativeThreadWindows *
NativeProcessWindows::GetThreadByID(lldb::tid_t thread_id) {
  return static_cast<NativeThreadWindows *>(
      NativeProcessProtocol::GetThreadByID(thread_id));
}

Status NativeProcessWindows::Halt() {
  bool caused_stop = false;
  StateType state = GetState();
  if (state != eStateStopped)
    return HaltProcess(caused_stop);
  return Status();
}

Status NativeProcessWindows::Detach() {
  Status error;
  Log *log = GetLog(WindowsLog::Process);
  StateType state = GetState();
  if (state != eStateExited && state != eStateDetached) {
    error = DetachProcess();
    if (error.Success())
      SetState(eStateDetached);
    else
      LLDB_LOG(log, "Detaching process error: {0}", error);
  } else {
    error = Status::FromErrorStringWithFormatv(
        "error: process {0} in state = {1}, but "
        "cannot detach it in this state.",
        GetID(), state);
    LLDB_LOG(log, "error: {0}", error);
  }
  return error;
}

Status NativeProcessWindows::Signal(int signo) {
  Status error;
  error = Status::FromErrorString(
      "Windows does not support sending signals to processes");
  return error;
}

Status NativeProcessWindows::Interrupt() { return Halt(); }

Status NativeProcessWindows::Kill() {
  StateType state = GetState();
  return DestroyProcess(state);
}

Status NativeProcessWindows::IgnoreSignals(llvm::ArrayRef<int> signals) {
  return Status();
}

Status NativeProcessWindows::GetMemoryRegionInfo(lldb::addr_t load_addr,
                                                 MemoryRegionInfo &range_info) {
  return ProcessDebugger::GetMemoryRegionInfo(load_addr, range_info);
}

Status NativeProcessWindows::ReadMemory(lldb::addr_t addr, void *buf,
                                        size_t size, size_t &bytes_read) {
  return ProcessDebugger::ReadMemory(addr, buf, size, bytes_read);
}

Status NativeProcessWindows::WriteMemory(lldb::addr_t addr, const void *buf,
                                         size_t size, size_t &bytes_written) {
  return ProcessDebugger::WriteMemory(addr, buf, size, bytes_written);
}

llvm::Expected<lldb::addr_t>
NativeProcessWindows::AllocateMemory(size_t size, uint32_t permissions) {
  lldb::addr_t addr;
  Status ST = ProcessDebugger::AllocateMemory(size, permissions, addr);
  if (ST.Success())
    return addr;
  return ST.ToError();
}

llvm::Error NativeProcessWindows::DeallocateMemory(lldb::addr_t addr) {
  return ProcessDebugger::DeallocateMemory(addr).ToError();
}

lldb::addr_t NativeProcessWindows::GetSharedLibraryInfoAddress() { return 0; }

bool NativeProcessWindows::IsAlive() const {
  StateType state = GetState();
  switch (state) {
  case eStateCrashed:
  case eStateDetached:
  case eStateExited:
  case eStateInvalid:
  case eStateUnloaded:
    return false;
  default:
    return true;
  }
}

void NativeProcessWindows::SetStopReasonForThread(NativeThreadWindows &thread,
                                                  lldb::StopReason reason,
                                                  std::string description) {
  SetCurrentThreadID(thread.GetID());

  ThreadStopInfo stop_info;
  stop_info.reason = reason;
  // No signal support on Windows but required to provide a 'valid' signum.
  stop_info.signo = SIGTRAP;

  if (reason == StopReason::eStopReasonException) {
    stop_info.details.exception.type = 0;
    stop_info.details.exception.data_count = 0;
  }

  thread.SetStopReason(stop_info, description);
}

void NativeProcessWindows::StopThread(lldb::tid_t thread_id,
                                      lldb::StopReason reason,
                                      std::string description) {
  NativeThreadWindows *thread = GetThreadByID(thread_id);
  if (!thread)
    return;

  for (uint32_t i = 0; i < m_threads.size(); ++i) {
    auto t = static_cast<NativeThreadWindows *>(m_threads[i].get());
    Status error = t->DoStop();
    if (error.Fail())
      exit(1);
  }
  SetStopReasonForThread(*thread, reason, description);
}

size_t NativeProcessWindows::UpdateThreads() { return m_threads.size(); }

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
NativeProcessWindows::GetAuxvData() const {
  // Not available on this target.
  return llvm::errc::not_supported;
}

llvm::Expected<llvm::ArrayRef<uint8_t>>
NativeProcessWindows::GetSoftwareBreakpointTrapOpcode(size_t size_hint) {
  static const uint8_t g_aarch64_opcode[] = {0x00, 0x00, 0x3e,
                                             0xd4};     // brk #0xf000
  static const uint8_t g_thumb_opcode[] = {0xfe, 0xde}; // udf #0xfe

  switch (GetArchitecture().GetMachine()) {
  case llvm::Triple::aarch64:
    return llvm::ArrayRef(g_aarch64_opcode);

  case llvm::Triple::arm:
  case llvm::Triple::thumb:
    return llvm::ArrayRef(g_thumb_opcode);

  default:
    return NativeProcessProtocol::GetSoftwareBreakpointTrapOpcode(size_hint);
  }
}

size_t NativeProcessWindows::GetSoftwareBreakpointPCOffset() {
  // Windows always reports an incremented PC after a breakpoint is hit,
  // even on ARM.
  return cantFail(GetSoftwareBreakpointTrapOpcode(0)).size();
}

bool NativeProcessWindows::FindSoftwareBreakpoint(lldb::addr_t addr) {
  if (m_software_breakpoints.find(addr) != m_software_breakpoints.end())
    return true;
  // Also recognise BPs that were just removed: a queued exception from a
  // sibling thread may arrive after the disable. See
  // m_recently_removed_bps's declaration for full context.
  return m_recently_removed_bps.find(addr) != m_recently_removed_bps.end();
}

Status NativeProcessWindows::SetBreakpoint(lldb::addr_t addr, uint32_t size,
                                           bool hardware) {
  if (hardware)
    return SetHardwareBreakpoint(addr, size);
  // Re-arming a BP at this address means any queued exceptions from before
  // the prior remove can no longer race; drop the shadow entry.
  m_recently_removed_bps.erase(addr);
  return SetSoftwareBreakpoint(addr, size);
}

Status NativeProcessWindows::RemoveBreakpoint(lldb::addr_t addr,
                                              bool hardware) {
  if (hardware)
    return RemoveHardwareBreakpoint(addr);
  // Remember the address so a still-queued STATUS_BREAKPOINT exception from
  // before the remove is treated as a (now-stale) BP hit and not as a user
  // int3.
  m_recently_removed_bps.insert(addr);
  return RemoveSoftwareBreakpoint(addr);
}

Status NativeProcessWindows::CacheLoadedModules() {
  Status error;
  if (!m_loaded_modules.empty())
    return Status();

  // Retrieve loaded modules by a Target/Module free implemenation.
  AutoHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetID()));
  if (snapshot.IsValid()) {
    MODULEENTRY32W me;
    me.dwSize = sizeof(MODULEENTRY32W);
    if (Module32FirstW(snapshot.get(), &me)) {
      do {
        std::string path;
        if (!llvm::convertWideToUTF8(me.szExePath, path))
          continue;

        FileSpec file_spec(path);
        FileSystem::Instance().Resolve(file_spec);
        m_loaded_modules.emplace_back(std::move(file_spec),
                                      (addr_t)me.modBaseAddr);
      } while (Module32Next(snapshot.get(), &me));
    }

    if (!m_loaded_modules.empty())
      return Status();
  }

  error = Status(::GetLastError(), lldb::ErrorType::eErrorTypeWin32);
  return error;
}

Status NativeProcessWindows::GetLoadedModuleFileSpec(const char *module_path,
                                                     FileSpec &file_spec) {
  Status error = CacheLoadedModules();
  if (error.Fail())
    return error;

  FileSpec module_file_spec(module_path);
  FileSystem::Instance().Resolve(module_file_spec);
  for (auto &it : m_loaded_modules) {
    if (it.first == module_file_spec) {
      file_spec = it.first;
      return Status();
    }
  }
  return Status::FromErrorStringWithFormat(
      "Module (%s) not found in process %" PRIu64 "!",
      module_file_spec.GetPath().c_str(), GetID());
}

Status
NativeProcessWindows::GetFileLoadAddress(const llvm::StringRef &file_name,
                                         lldb::addr_t &load_addr) {
  Status error = CacheLoadedModules();
  if (error.Fail())
    return error;

  load_addr = LLDB_INVALID_ADDRESS;
  FileSpec file_spec(file_name);
  FileSystem::Instance().Resolve(file_spec);
  for (auto &it : m_loaded_modules) {
    if (it.first == file_spec) {
      load_addr = it.second;
      return Status();
    }
  }
  return Status::FromErrorStringWithFormat(
      "Can't get loaded address of file (%s) in process %" PRIu64 "!",
      file_spec.GetPath().c_str(), GetID());
}

llvm::Expected<std::vector<LoadedLibraryInfo>>
NativeProcessWindows::GetLoadedLibraries() {
  if (Status error = CacheLoadedModules(); error.Fail())
    return error.ToError();

  std::vector<LoadedLibraryInfo> libs;
  libs.reserve(m_loaded_modules.size());
  for (const auto &[file_spec, base] : m_loaded_modules) {
    LoadedLibraryInfo info;
    info.name = file_spec.GetPath();
    info.base_addr = base;
    libs.push_back(std::move(info));
  }
  return libs;
}

bool NativeProcessWindows::HasPendingLibraryEvents() {
  return m_pending_library_events;
}

void NativeProcessWindows::OnExitProcess(uint32_t exit_code) {
  Log *log = GetLog(WindowsLog::Process);
  LLDB_LOG(log, "Process {0} exited with code {1}", GetID(), exit_code);

  ProcessDebugger::OnExitProcess(exit_code);

  // No signal involved.  It is just an exit event.
  WaitStatus wait_status(WaitStatus::Exit, exit_code);
  SetExitStatus(wait_status, true);

  // Notify the native delegate.
  SetState(eStateExited, true);
}

void NativeProcessWindows::OnDebuggerConnected(lldb::addr_t image_base) {
  Log *log = GetLog(WindowsLog::Process);
  LLDB_LOG(log, "Debugger connected to process {0}. Image base = {1:x}",
           GetDebuggedProcessId(), image_base);

  // This is the earliest chance we can resolve the process ID and
  // architecture if we don't know them yet.
  if (GetID() == LLDB_INVALID_PROCESS_ID)
    SetID(GetDebuggedProcessId());

  if (GetArchitecture().GetMachine() == llvm::Triple::UnknownArch) {
    ProcessInstanceInfo process_info;
    if (!Host::GetProcessInfo(GetDebuggedProcessId(), process_info)) {
      LLDB_LOG(log, "Cannot get process information during debugger connecting "
                    "to process");
      return;
    }
    SetArchitecture(process_info.GetArchitecture());
  }

  // The very first one shall always be the main thread.
  assert(m_threads.empty());
  m_threads.push_back(std::make_unique<NativeThreadWindows>(
      *this, m_session_data->m_debugger->GetMainThread()));
}

ExceptionResult
NativeProcessWindows::OnDebugException(bool first_chance,
                                       const ExceptionRecord &record) {
  Log *log = GetLog(WindowsLog::Exception);
  llvm::sys::ScopedLock lock(m_mutex);

  // Let the debugger establish the internal status.
  ProcessDebugger::OnDebugException(first_chance, record);

  switch (record.GetExceptionCode()) {
  case DWORD(STATUS_SINGLE_STEP):
  case STATUS_WX86_SINGLE_STEP: {
#ifndef __aarch64__
    uint32_t wp_id = LLDB_INVALID_INDEX32;
    if (NativeThreadWindows *thread = GetThreadByID(record.GetThreadID())) {
      NativeRegisterContextWindows &reg_ctx = thread->GetRegisterContext();
      Status error =
          reg_ctx.GetWatchpointHitIndex(wp_id, record.GetExceptionAddress());
      if (error.Fail())
        LLDB_LOG(log,
                 "received error while checking for watchpoint hits, pid = "
                 "{0}, error = {1}",
                 thread->GetID(), error);
      if (wp_id != LLDB_INVALID_INDEX32) {
        addr_t wp_addr = reg_ctx.GetWatchpointAddress(wp_id);
        addr_t wp_hit_addr = reg_ctx.GetWatchpointHitAddress(wp_id);
        std::string desc =
            formatv("{0} {1} {2}", wp_addr, wp_id, wp_hit_addr).str();
        StopThread(record.GetThreadID(), StopReason::eStopReasonWatchpoint,
                   desc);
      }
    }
    if (wp_id == LLDB_INVALID_INDEX32)
#endif
      StopThread(record.GetThreadID(), StopReason::eStopReasonTrace);

    SetState(eStateStopped, true);

    // Mirror ProcessWindows (in-process): block until client issues continue.
    // If we return MaskException the DebuggerThread will immediately drain the
    // next queued exception (e.g. a sibling thread that hit the same BP a
    // microsecond after us) and stamp a stale stop_info on a bystander thread.
    // That bystander then votes "stop" in ThreadList::ShouldStop on the next
    // round, deflecting our step-over plan's auto-continue and surfacing the
    // wrong stop reason to the user.
    return ExceptionResult::BreakInDebugger;
  }
  case DWORD(STATUS_BREAKPOINT):
  case STATUS_WX86_BREAKPOINT:

    if (NativeThreadWindows *stop_thread =
            GetThreadByID(record.GetThreadID())) {
      auto &reg_ctx = stop_thread->GetRegisterContext();
      const auto exception_addr = record.GetExceptionAddress();
      const auto thread_id = record.GetThreadID();

      if (FindSoftwareBreakpoint(exception_addr)) {
        LLDB_LOG(log, "Hit non-loader breakpoint at address {0:x}.",
                 exception_addr);
        StopThread(thread_id, StopReason::eStopReasonBreakpoint);
        // The current PC is AFTER the BP opcode, on all architectures.
        reg_ctx.SetPC(reg_ctx.GetPC() - GetSoftwareBreakpointPCOffset());
        SetState(eStateStopped, true);
        // See comment above STATUS_SINGLE_STEP: serialize against client
        // continues to keep queued sibling-thread exceptions from being drained
        // before we deliver this stop.
        return ExceptionResult::BreakInDebugger;
      } else {
        // This block of code will only be entered in case of a hardware
        // watchpoint or breakpoint hit on AArch64. However, we only handle
        // hardware watchpoints below as breakpoints are not yet supported.
        const std::vector<ULONG_PTR> &args = record.GetExceptionArguments();
        // Check that the ExceptionInformation array of EXCEPTION_RECORD
        // contains at least two elements: the first is a read-write flag
        // indicating the type of data access operation (read or write) while
        // the second contains the virtual address of the accessed data.
        if (args.size() >= 2) {
          uint32_t hw_id = LLDB_INVALID_INDEX32;
          Status error = reg_ctx.GetWatchpointHitIndex(hw_id, args[1]);
          if (error.Fail())
            LLDB_LOG(log,
                     "received error while checking for watchpoint hits, pid = "
                     "{0}, error = {1}",
                     thread_id, error);

          if (hw_id != LLDB_INVALID_INDEX32) {
            std::string desc =
                formatv("{0} {1} {2}", reg_ctx.GetWatchpointAddress(hw_id),
                        hw_id, exception_addr)
                    .str();
            StopThread(thread_id, StopReason::eStopReasonWatchpoint, desc);
            SetState(eStateStopped, true);
            return ExceptionResult::BreakInDebugger;
          }
        }
      }
    }

    if (m_initial_system_bps_remaining > 0) {
      --m_initial_system_bps_remaining;
      LLDB_LOG(log,
               "Hit loader breakpoint at address {0:x}, setting initial stop "
               "event.",
               record.GetExceptionAddress());

      // We are required to report the reason for the first stop after
      // launching or being attached.
      if (NativeThreadWindows *thread = GetThreadByID(record.GetThreadID()))
        SetStopReasonForThread(*thread, StopReason::eStopReasonBreakpoint);

      // Do not notify the native delegate (e.g. llgs) since at this moment
      // the program hasn't returned from Manager::Launch() and the delegate
      // might not have an valid native process to operate on.
      SetState(eStateStopped, false);

      // Hit the initial stop. Continue the application.
      return ExceptionResult::BreakInDebugger;
    }

    // Any remaining STATUS_BREAKPOINT is a breakpoint instruction in the
    // program's own code (e.g. `int3`, `__debugbreak()`, or
    // `__builtin_debugtrap()`) that lldb did not plant itself. Stop the
    // debugger and let the user decide what to do. We BreakInDebugger here
    // for the same reason as user-planted BPs (don't drain queued exceptions
    // before the client reacts) and we cannot use SendToApplication: an
    // unhandled STATUS_BREAKPOINT is fatal to the process on Windows so the
    // subsequent user `continue` would have nothing to resume.
    {
      std::string desc =
          formatv("Exception {0} encountered at address {1}",
                  llvm::format_hex(record.GetExceptionCode(), 8),
                  llvm::format_hex(record.GetExceptionAddress(), 8))
              .str();
      StopThread(record.GetThreadID(), StopReason::eStopReasonException,
                 std::move(desc));
      SetState(eStateStopped, true);
    }
    return ExceptionResult::BreakInDebugger;
  default:
    LLDB_LOG(log,
             "Debugger thread reported exception {0:x} at address {1:x} "
             "(first_chance={2})",
             record.GetExceptionCode(), record.GetExceptionAddress(),
             first_chance);

    // On a first-chance non-breakpoint exception (typically
    // STATUS_ACCESS_VIOLATION from a crash), let the inferior's SEH chain
    // run before stopping the debugger. If no handler claims it, Windows
    // re-raises the same exception as a second-chance event and we land
    // back here with first_chance == 0.
    //
    // Crucially, do NOT call StopThread or SetState on first chance. The
    // in-process ProcessWindows::OnDebugException is deliberately a
    // no-op here for the same reason: stopping all threads while we
    // simultaneously tell Windows DBG_EXCEPTION_NOT_HANDLED blocks SEH
    // dispatch, and the second-chance event never fires.
    if (first_chance)
      return ExceptionResult::SendToApplication;

    {
      std::string desc;
      llvm::raw_string_ostream desc_stream(desc);
      desc_stream << "Exception "
                  << llvm::format_hex(record.GetExceptionCode(), 8)
                  << " encountered at address "
                  << llvm::format_hex(record.GetExceptionAddress(), 8);
      AppendExceptionDetails(desc_stream, record);
      StopThread(record.GetThreadID(), StopReason::eStopReasonException,
                 desc.c_str());

      SetState(eStateStopped, true);
    }

    return ExceptionResult::BreakInDebugger;
  }
}

void NativeProcessWindows::OnCreateThread(const HostThread &new_thread) {
  llvm::sys::ScopedLock lock(m_mutex);

  auto thread = std::make_unique<NativeThreadWindows>(*this, new_thread);
  thread->GetRegisterContext().ClearAllHardwareWatchpoints();
  for (const auto &pair : GetWatchpointMap()) {
    const NativeWatchpoint &wp = pair.second;
    thread->SetWatchpoint(wp.m_addr, wp.m_size, wp.m_watch_flags,
                          wp.m_hardware);
  }

  m_threads.push_back(std::move(thread));
}

void NativeProcessWindows::OnExitThread(lldb::tid_t thread_id,
                                        uint32_t exit_code) {
  llvm::sys::ScopedLock lock(m_mutex);
  NativeThreadWindows *thread = GetThreadByID(thread_id);
  if (!thread)
    return;

  for (auto t = m_threads.begin(); t != m_threads.end();) {
    if ((*t)->GetID() == thread_id) {
      t = m_threads.erase(t);
    } else {
      ++t;
    }
  }
}

void NativeProcessWindows::OnLoadDll(const ModuleSpec &module_spec,
                                     lldb::addr_t module_addr) {
  // Invalidate the cached loaded modules so the next GetLoadedLibraries()
  // call re-snapshots, and flag that a library-list change happened so the
  // next stop reply carries `library:1;`.
  m_loaded_modules.clear();
  m_pending_library_events = true;
}

void NativeProcessWindows::OnUnloadDll(lldb::addr_t module_addr) {
  m_loaded_modules.clear();
  m_pending_library_events = true;
}

llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
NativeProcessWindows::Manager::Launch(
    ProcessLaunchInfo &launch_info,
    NativeProcessProtocol::NativeDelegate &native_delegate) {
  Error E = Error::success();
  auto process_up = std::unique_ptr<NativeProcessWindows>(
      new NativeProcessWindows(launch_info, native_delegate, E));
  if (E)
    return std::move(E);
  return std::move(process_up);
}

llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
NativeProcessWindows::Manager::Attach(
    lldb::pid_t pid, NativeProcessProtocol::NativeDelegate &native_delegate) {
  Error E = Error::success();
  // Set pty primary fd invalid since it is not available.
  auto process_up = std::unique_ptr<NativeProcessWindows>(
      new NativeProcessWindows(pid, -1, native_delegate, E));
  if (E)
    return std::move(E);
  return std::move(process_up);
}

void NativeProcessWindows::StartStdioReaderThread() {
  if (!m_pty || m_pty->GetMode() != PseudoConsole::Mode::Pipe)
    return;
  HANDLE stdout_handle = m_pty->GetSTDOUTHandle();
  if (stdout_handle == INVALID_HANDLE_VALUE || stdout_handle == nullptr)
    return;

  // Manual-reset stop event; SetEvent from the destructor unblocks
  // WaitForMultipleObjects in the reader loop.
  m_stdio_stop_event = ::CreateEventW(nullptr, /*bManualReset=*/TRUE,
                                      /*bInitialState=*/FALSE, nullptr);
  if (!m_stdio_stop_event)
    return;

  m_stdio_reader_thread =
      std::thread(&NativeProcessWindows::StdioReaderThreadLoop, this);
}

void NativeProcessWindows::StdioReaderThreadLoop() {
  llvm::set_thread_name("lldb.nproc.stdio");

  Log *log = GetLog(WindowsLog::Process);
  HANDLE stdout_handle = m_pty->GetSTDOUTHandle();

  // The PseudoConsole in Pipe mode creates the parent-side STDOUT pipe
  // with FILE_FLAG_OVERLAPPED, so ReadFile must be async. Use a manual-
  // reset event for the overlapped hEvent so WaitForMultipleObjects and
  // GetOverlappedResult agree on the signalled state.
  HANDLE read_event = ::CreateEventW(nullptr, /*bManualReset=*/TRUE,
                                     /*bInitialState=*/FALSE, nullptr);
  if (!read_event) {
    LLDB_LOG(log, "Failed to create overlapped event for stdio reader");
    return;
  }

  HANDLE wait_handles[2] = {read_event, m_stdio_stop_event};
  std::array<char, 1024> buffer;

  while (true) {
    OVERLAPPED ov{};
    ov.hEvent = read_event;
    ::ResetEvent(read_event);

    DWORD bytes_read = 0;
    BOOL ok =
        ::ReadFile(stdout_handle, buffer.data(),
                   static_cast<DWORD>(buffer.size()), &bytes_read, &ov);
    DWORD err = ::GetLastError();

    if (!ok && err != ERROR_IO_PENDING) {
      // Pipe closed by the child or otherwise broken → EOF.
      if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF)
        break;
      LLDB_LOG(log, "stdio reader: ReadFile failed with error {0}", err);
      break;
    }

    if (!ok) {
      DWORD wait = ::WaitForMultipleObjects(2, wait_handles, /*bWaitAll=*/FALSE,
                                            INFINITE);
      if (wait == WAIT_OBJECT_0 + 1) {
        // Destructor signalled shutdown; cancel and exit.
        ::CancelIoEx(stdout_handle, &ov);
        ::GetOverlappedResult(stdout_handle, &ov, &bytes_read, /*bWait=*/TRUE);
        break;
      }
      if (wait != WAIT_OBJECT_0) {
        LLDB_LOG(log, "stdio reader: wait failed, result={0}, err={1}", wait,
                 ::GetLastError());
        break;
      }
      if (!::GetOverlappedResult(stdout_handle, &ov, &bytes_read,
                                 /*bWait=*/FALSE)) {
        DWORD get_err = ::GetLastError();
        if (get_err == ERROR_BROKEN_PIPE || get_err == ERROR_HANDLE_EOF ||
            get_err == ERROR_OPERATION_ABORTED)
          break;
        LLDB_LOG(log, "stdio reader: GetOverlappedResult failed, err={0}",
                 get_err);
        break;
      }
    }

    if (bytes_read == 0)
      break; // EOF

    m_delegate.NewProcessOutput(
        this, llvm::StringRef(buffer.data(), bytes_read));
  }

  ::CloseHandle(read_event);
}

} // namespace lldb_private
