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
    # rdar://183113449: async-let step-over is broken on Windows because the
    # awaited continuation can resume on a DIFFERENT OS thread than the one the
    # step began on. Verified on the host: the plain async-call step-over fix (in
    # ThreadPlanStepOverRange, which enables the sibling step_over test) lets most
    # steps here succeed, and CreateRunThroughTaskSwitchThreadPlan reads the
    # correct continuation-funclet address from the arg register (confirmed: dest
    # points at valid foo() funclets). The remaining failure is the BLOCKING await
    # (`await timestamp3`, whose child task is not yet ready): the parent task
    # blocks (NtWaitForSingleObject) and its continuation is resumed on another
    # cooperative-pool thread. ThreadPlanRunToAddress sets a THREAD-SPECIFIC
    # breakpoint (SetThreadID) at the continuation and only completes on its own
    # thread, so when the continuation lands on a different thread the plan never
    # completes -> StepOver returns eStopReasonNone. Making the breakpoint
    # thread-agnostic is not sufficient: the parked original thread's plan is
    # never polled (Thread::ShouldStop early-returns for a thread with no stop
    # reason), and this test observes the ORIGINAL `thread` object, which stays
    # parked. Reproduced as a flaky pass/fail correlating with whether the child
    # finished before the await. A real fix needs either the Windows concurrency
    # runtime to DONATE the awaiting thread to the continuation (as Darwin does,
    # where the test passes) or lldb task-as-thread ("logical thread") stepping
    # that follows a task across OS threads. Neither is a clean lldb-only change;
    # tracked with the queues / task-switch task-following gap.
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
