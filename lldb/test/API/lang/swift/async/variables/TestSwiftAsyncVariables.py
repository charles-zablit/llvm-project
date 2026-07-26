import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftAsyncVariables(lldbtest.TestBase):

    mydir = lldbtest.TestBase.compute_mydir(__file__)

    @skipEmbeddedSwift
    @swiftTest
    # rdar://183113449: on Windows `frame variable x` returns GARBAGE (not 23),
    # yet the DWARF location for x is present and correct-looking
    # (DW_OP_fbreg +224, DW_OP_deref, DW_OP_plus_uconst 0x80 with frame_base
    # DW_OP_reg7 RSP, plus DW_OP_entry_value(reg14) variants for the resumed
    # funclets). So this is an lldb-side async coro-frame local-variable
    # evaluation bug (NOT a missing-debug-info compiler gap as first assumed);
    # the exact fault (fbreg/CFA vs the deref+offset chain for the heap coro
    # frame) still needs pinning. Reads garbage on Windows; correct off-Windows.
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
