"""Test that we are able to evaluate expressions when the inferior is blocked in a syscall"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class ExprSyscallTestCase(TestBase):
    @expectedFailureNetBSD
    @skipIf(
        oslist=["windows"],
        bugnumber=(
            "FIXME: passes under the in-process Windows debugger, fails under "
            "lldb-server. After SendAsyncInterrupt, lldb-server-on-Windows "
            "halts via DebugBreakProcess, which injects a fresh thread that "
            "fires int3 in ntdll!DbgUiRemoteBreakin. The original main thread "
            "is left suspended at the user-mode `ret` of the NtDelayExecution "
            "stub. When the function-call expression `(int)GetCurrentProcessId()` "
            "runs on that suspended-mid-syscall thread, the JIT trampoline's "
            "post-call `mov [rsi], eax` faults at offset 0x2b: RSI is "
            "clobbered to a non-canonical address by the time the call returns "
            "(despite Win64 ABI marking RSI as non-volatile). Running the same "
            "expression on the injected DbgUiRemoteBreakin thread succeeds, so "
            "the bug is specific to function calls on syscall-suspended "
            "threads under lldb-server. The test relies on lldb's default "
            "thread-selection logic picking the syscall thread; on Linux that "
            "thread cleanly returns from the syscall after SIGSTOP, on Windows "
            "it doesn't."
        ),
    )
    def test_setpgid(self):
        self.build()

        # Create a target by the debugger.
        target = self.createTestTarget()

        listener = lldb.SBListener("my listener")

        # launch the inferior and don't wait for it to stop
        self.dbg.SetAsync(True)
        error = lldb.SBError()
        flags = target.GetLaunchInfo().GetLaunchFlags()
        process = target.Launch(
            listener,
            None,  # argv
            None,  # envp
            None,  # stdin_path
            None,  # stdout_path
            None,  # stderr_path
            None,  # working directory
            flags,  # launch flags
            False,  # Stop at entry
            error,
        )  # error

        self.assertTrue(process and process.IsValid(), PROCESS_IS_VALID)

        event = lldb.SBEvent()

        # Give the child enough time to reach the syscall,
        # while clearing out all the pending events.
        # The last WaitForEvent call will time out after 2 seconds.
        while listener.WaitForEvent(2, event):
            pass

        # now the process should be running (blocked in the syscall)
        self.assertEqual(process.GetState(), lldb.eStateRunning, "Process is running")

        # send the process a signal
        process.SendAsyncInterrupt()
        while listener.WaitForEvent(2, event):
            pass

        # as a result the process should stop
        # in all likelihood we have stopped in the middle of the sleep()
        # syscall
        self.assertEqual(process.GetState(), lldb.eStateStopped, PROCESS_STOPPED)
        thread = process.GetSelectedThread()

        # try evaluating a couple of expressions in this state
        self.expect_expr("release_flag = 1", result_value="1")
        func = (
            "GetCurrentProcessId"
            if lldbplatformutil.getPlatform() == "windows"
            else "getpid"
        )
        self.expect_expr(f"(int){func}()", result_value=str(process.GetProcessID()))

        # and run the process to completion
        process.Continue()

        # process all events
        while listener.WaitForEvent(10, event):
            new_state = lldb.SBProcess.GetStateFromEvent(event)
            if new_state == lldb.eStateExited:
                break

        self.assertState(process.GetState(), lldb.eStateExited)
        self.assertEqual(process.GetExitStatus(), 0)
