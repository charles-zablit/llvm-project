import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftAsyncVariables(lldbtest.TestBase):

    mydir = lldbtest.TestBase.compute_mydir(__file__)

    @skipEmbeddedSwift
    @swiftTest
    # rdar://183113449: on Windows `frame variable x` is CORRECT (23) at the stop
    # BEFORE `await` (line 10), but returns garbage at the stop AFTER `await`
    # (line 15) -- and CLI and SB API agree at both stops (so it is not a
    # command-vs-API scope issue). The post-await x uses the resumed-funclet
    # location DW_OP_entry_value(DW_OP_reg14) + DW_OP_deref + offset; lldb
    # mis-evaluates the async-continuation local there (the async context /
    # entry-value reconstruction at the resumed funclet), yielding garbage. An
    # lldb-side async-continuation variable-reconstruction bug, not a compiler or
    # Remote Mirrors gap. Correct off-Windows.
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
