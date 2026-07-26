import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil


@skipIfAsan  # rdar://138777205
class TestCase(lldbtest.TestBase):

    def check_is_in_line(self, thread, linenum):
        frame = thread.frames[0]
        line_entry = frame.GetLineEntry()
        self.assertEqual(linenum, line_entry.GetLine())

    @skipEmbeddedSwift
    @swiftTest
    # rdar://183113449: async-let step-over is still broken on Windows/Linux even
    # after the ThreadPlanStepOverRange async-call step-over fix (which enables
    # the sibling step_over test). Here StepOver stops with eStopReasonNone
    # instead of eStopReasonPlanComplete: `async let` spawns a child task, so
    # stepping over it involves a task switch, which the step plan does not yet
    # follow. This is the same task-switching gap tracked for the queues /
    # task-switch tests, not the plain async-call step-over that was fixed.
    @skipIf(oslist=["windows", "linux"])
    def test_nothrow(self):
        """Test conditions for async step-over."""
        self.build()

        source_file = lldb.SBFileSpec("main.swift")
        target, process, thread, bkpt = lldbutil.run_to_source_breakpoint(
            self, "BREAK_NOTHROW", source_file
        )

        # Step over should reach every line in the interval [10, 20]
        expected_line_nums = range(10, 21)
        for expected_line_num in expected_line_nums:
            thread.StepOver()
            stop_reason = thread.GetStopReason()
            self.assertStopReason(stop_reason, lldb.eStopReasonPlanComplete)
            self.check_is_in_line(thread, expected_line_num)

    @skipEmbeddedSwift
    @swiftTest
    # See test_nothrow: async-let step-over needs task-switch following, which is
    # not yet implemented. Same task-switching gap as the queues / task-switch
    # tests.
    @skipIf(oslist=["windows", "linux"])
    def test_throw(self):
        """Test conditions for async step-over."""
        self.build()

        source_file = lldb.SBFileSpec("main.swift")
        target, process, thread, bkpt = lldbutil.run_to_source_breakpoint(
            self, "BREAK_THROW", source_file
        )

        # Step over should reach every line in the interval [34, 40]
        expected_line_nums = range(34, 41)
        for expected_line_num in expected_line_nums:
            thread.StepOver()
            stop_reason = thread.GetStopReason()
            self.assertStopReason(stop_reason, lldb.eStopReasonPlanComplete)
            self.check_is_in_line(thread, expected_line_num)
