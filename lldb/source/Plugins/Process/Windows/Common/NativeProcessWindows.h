//===-- NativeProcessWindows.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NativeProcessWindows_h_
#define liblldb_NativeProcessWindows_h_

#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/lldb-forward.h"

#include "IDebugDelegate.h"
#include "ProcessDebugger.h"

#include <atomic>
#include <memory>
#include <thread>
#include <unordered_set>

namespace lldb_private {

class HostProcess;
class NativeProcessWindows;
class NativeThreadWindows;
class NativeDebugDelegate;
class PseudoConsole;

typedef std::shared_ptr<NativeDebugDelegate> NativeDebugDelegateSP;

//------------------------------------------------------------------
// NativeProcessWindows
//------------------------------------------------------------------
class NativeProcessWindows : public NativeProcessProtocol,
                             public ProcessDebugger {

public:
  class Manager : public NativeProcessProtocol::Manager {
  public:
    using NativeProcessProtocol::Manager::Manager;

    llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
    Launch(ProcessLaunchInfo &launch_info,
           NativeDelegate &native_delegate) override;

    llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
    Attach(lldb::pid_t pid, NativeDelegate &native_delegate) override;

    Extension GetSupportedExtensions() const override {
      return Extension::libraries;
    }
  };

  ~NativeProcessWindows() override;

  Status Resume(const ResumeActionList &resume_actions) override;

  Status Halt() override;

  Status Detach() override;

  Status Signal(int signo) override;

  Status Interrupt() override;

  Status Kill() override;

  Status IgnoreSignals(llvm::ArrayRef<int> signals) override;

  Status GetMemoryRegionInfo(lldb::addr_t load_addr,
                             MemoryRegionInfo &range_info) override;

  Status ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                    size_t &bytes_read) override;

  Status WriteMemory(lldb::addr_t addr, const void *buf, size_t size,
                     size_t &bytes_written) override;

  llvm::Expected<lldb::addr_t> AllocateMemory(size_t size,
                                              uint32_t permissions) override;

  llvm::Error DeallocateMemory(lldb::addr_t addr) override;

  lldb::addr_t GetSharedLibraryInfoAddress() override;

  bool IsAlive() const override;

  size_t UpdateThreads() override;

  const ArchSpec &GetArchitecture() const override { return m_arch; }

  void SetArchitecture(const ArchSpec &arch_spec) { m_arch = arch_spec; }

  Status SetBreakpoint(lldb::addr_t addr, uint32_t size,
                       bool hardware) override;

  Status RemoveBreakpoint(lldb::addr_t addr, bool hardware = false) override;

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  GetAuxvData() const override;

  Status GetLoadedModuleFileSpec(const char *module_path,
                                 FileSpec &file_spec) override;

  Status GetFileLoadAddress(const llvm::StringRef &file_name,
                            lldb::addr_t &load_addr) override;

  llvm::Expected<std::vector<LoadedLibraryInfo>> GetLoadedLibraries() override;

  bool HasPendingLibraryEvents() override;

  // ProcessDebugger Overrides
  void OnExitProcess(uint32_t exit_code) override;
  void OnDebuggerConnected(lldb::addr_t image_base) override;
  ExceptionResult OnDebugException(bool first_chance,
                                   const ExceptionRecord &record) override;
  void OnCreateThread(const HostThread &thread) override;
  void OnExitThread(lldb::tid_t thread_id, uint32_t exit_code) override;
  void OnLoadDll(const ModuleSpec &module_spec,
                 lldb::addr_t module_addr) override;
  void OnUnloadDll(lldb::addr_t module_addr) override;

protected:
  NativeThreadWindows *GetThreadByID(lldb::tid_t thread_id);

  llvm::Expected<llvm::ArrayRef<uint8_t>>
  GetSoftwareBreakpointTrapOpcode(size_t size_hint) override;

  size_t GetSoftwareBreakpointPCOffset() override;

  bool FindSoftwareBreakpoint(lldb::addr_t addr);

  void StopThread(lldb::tid_t thread_id, lldb::StopReason reason,
                  std::string description = "");

  void SetStopReasonForThread(NativeThreadWindows &thread,
                              lldb::StopReason reason,
                              std::string description = "");

private:
  ArchSpec m_arch;

  NativeProcessWindows(ProcessLaunchInfo &launch_info, NativeDelegate &delegate,
                       llvm::Error &E);

  NativeProcessWindows(lldb::pid_t pid, int terminal_fd,
                       NativeDelegate &delegate, llvm::Error &E);

  Status CacheLoadedModules();
  // Stored in Toolhelp32 enumeration order (which is the OS loader order)
  // rather than std::map's lexicographic order, so the qXfer:libraries:read
  // response and `target modules list` reflect what the loader actually
  // mapped in. The legacy in-process plugin already preserves loader order
  // and several Shell tests pin to it.
  std::vector<std::pair<lldb_private::FileSpec, lldb::addr_t>>
      m_loaded_modules;

  // Number of initial system STATUS_BREAKPOINTs we still need to swallow
  // silently before the first user stop. CreateProcess(DEBUG_PROCESS, ...)
  // produces one (LdrpDoDebuggerBreak in ntdll); DebugActiveProcess
  // produces two (DbgUiRemoteBreakin injected by the kernel + the loader
  // BP once the primary thread is resumed). Initialised in the
  // constructors below.
  int m_initial_system_bps_remaining = 1;

  // Set when Halt() / Interrupt() schedules a DebugBreakProcess injection;
  // consumed by OnDebugException's user-int3 fallback to recognise the
  // injected breakpoint as the halt acknowledgement (rather than a user
  // int3 instruction in the program). Lets us surface the stop as
  // eStopReasonSignal (SIGSTOP) on a user thread, matching the gdb-remote
  // semantics that other backends produce for `Ctrl-C` / `process interrupt`.
  bool m_pending_halt = false;

  // Addresses where lldb has previously planted a software BP that has since
  // been removed (z0). Cleared on next Z0 at the same address. Used by
  // OnDebugException to recognise STATUS_BREAKPOINT exceptions that the
  // kernel had already queued *before* lldb removed the BP, e.g. a sibling
  // thread that hit the BP a microsecond before our step-over plan disabled
  // it. Without this, FindSoftwareBreakpoint() returns false for those
  // queued exceptions and we mis-classify them as user `int3`s, leaving the
  // PC one byte past the BP and crashing the inferior on the next continue.
  // Mirrors how ProcessWindows (in-process) keeps the BreakpointSite around
  // across a logical disable.
  std::unordered_set<lldb::addr_t> m_recently_removed_bps;

  // Set whenever an OS DLL load/unload event has been seen since the last
  // stop reply. The stop-reply builder flips it back to false and emits
  // `library:1;` so the client knows to re-read the module list. Initialised
  // to true so the first stop always advertises the initial module set.
  bool m_pending_library_events = true;

  // Retained PseudoConsole for the lldb-server stdio-forwarding path. The
  // launcher installs the child-side handles and hands the parent-side
  // back here via ProcessLaunchInfo::TakePTY(); the reader thread below
  // pulls data off its STDOUT HANDLE using overlapped ReadFile and
  // forwards it into `NewProcessOutput` on the delegate.
  std::shared_ptr<PseudoConsole> m_pty;

  // Event signalled during destruction to unblock a pending overlapped
  // read in the reader thread. Created with CreateEventW; owned here.
  void *m_stdio_stop_event = nullptr;

  // Background thread that reads the inferior stdout/stderr pipe. Joined
  // in the destructor after m_stdio_stop_event is signalled and any
  // pending ReadFile is cancelled.
  std::thread m_stdio_reader_thread;

  // Spawn m_stdio_reader_thread on m_pty's STDOUT HANDLE. No-op if the
  // PTY is null, not connected, or not in Pipe mode.
  void StartStdioReaderThread();

  // Reader-thread body: issues overlapped ReadFile calls and invokes
  // delegate.NewProcessOutput with each chunk.
  void StdioReaderThreadLoop();
};

//------------------------------------------------------------------
// NativeDebugDelegate
//------------------------------------------------------------------
class NativeDebugDelegate : public IDebugDelegate {
public:
  NativeDebugDelegate(NativeProcessWindows &process) : m_process(process) {}

  void OnExitProcess(uint32_t exit_code) override {
    m_process.OnExitProcess(exit_code);
  }

  void OnDebuggerConnected(lldb::addr_t image_base) override {
    m_process.OnDebuggerConnected(image_base);
  }

  ExceptionResult OnDebugException(bool first_chance,
                                   const ExceptionRecord &record) override {
    return m_process.OnDebugException(first_chance, record);
  }

  void OnCreateThread(const HostThread &thread) override {
    m_process.OnCreateThread(thread);
  }

  void OnExitThread(lldb::tid_t thread_id, uint32_t exit_code) override {
    m_process.OnExitThread(thread_id, exit_code);
  }

  void OnLoadDll(const lldb_private::ModuleSpec &module_spec,
                 lldb::addr_t module_addr) override {
    m_process.OnLoadDll(module_spec, module_addr);
  }

  void OnUnloadDll(lldb::addr_t module_addr) override {
    m_process.OnUnloadDll(module_addr);
  }

  void OnDebugString(const std::string &string) override {
    m_process.OnDebugString(string);
  }

  void OnDebuggerError(const Status &error, uint32_t type) override {
    return m_process.OnDebuggerError(error, type);
  }

private:
  NativeProcessWindows &m_process;
};

} // namespace lldb_private

#endif // #ifndef liblldb_NativeProcessWindows_h_
