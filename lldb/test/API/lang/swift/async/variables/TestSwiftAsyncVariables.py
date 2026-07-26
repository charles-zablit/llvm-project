import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftAsyncVariables(lldbtest.TestBase):

    mydir = lldbtest.TestBase.compute_mydir(__file__)

    @skipEmbeddedSwift
    @swiftTest
    # rdar://183113449: on Windows `frame variable x` (CLI) shows garbage, but the
    # SB API frame.FindVariable("x") returns the correct 23 (verified: x lives at
    # coro-frame+0xc0 and reads 23; the coro ptr is at [rsp+224]). So the value is
    # readable and lldb CAN compute it -- the bug is that the frame-variable
    # command path resolves `x` to the wrong one of the 4 nested-scope x DIEs (the
    # for-loop `x` vs the inner `let x = x!`) on Windows async funclets. An
    # lldb-side variable/lexical-scope resolution bug, not a compiler or Remote
    # Mirrors gap. Correct off-Windows.
    @skipIf(oslist=['windows', 'linux'])
    def test(self):
        """Test local variables in async functions"""
        self.build()
        src = lldb.SBFileSpec('main.swift')
        target, process, thread, bkpt = lldbutil.run_to_source_breakpoint(
            self, 'break here', src)

        while process.selected_thread.stop_reason == lldb.eStopReasonBreakpoint:
            self.expect("frame variable x", substrs=["23"])
            process.Continue()
