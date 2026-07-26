import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil


@skipIfAsan  # rdar://138777205
class TestCase(lldbtest.TestBase):

    def check_x_is_available(self, frame):
        x_var = frame.FindVariable("x")
        self.assertTrue(x_var.IsValid(), f"Failed to find x in {frame}")
        self.assertEqual(x_var.GetValueAsUnsigned(), 30)

    def check_is_in_line(self, thread, linenum):
        frame = thread.frames[0]
        line_entry = frame.GetLineEntry()
        self.assertEqual(linenum, line_entry.GetLine())

    @skipEmbeddedSwift
    @swiftTest
    # rdar://183113449: async step-over is broken on Windows. Stepping over
    # `let x = await f()` (line 3) stops back on line 3 (the await resume point,
    # in main's TQ0_ funclet) instead of continuing to line 4, then the next step
    # descends into f(). Root cause: the frame CFA increases across the await
    # (e.g. 0x..5b0 -> 0x..660), so CompareCurrentFrameToStartFrame returns
    # eFrameCompareOlder, and ThreadPlanStepOverRange::ShouldStop's Older branch
    # (ThreadPlanStepOverRange.cpp ~line 160) does NOT consult IsEquivalentContext
    # -- unlike the eFrameCompareYounger branch (~line 189) -- so the resumed
    # funclet is mistaken for a return to the caller even though it is the same
    # async function (AreFuncletsOfSameAsyncFunction). Fix direction: in the Older
    # branch, when IsEquivalentContext(current) is true, re-establish the line
    # range in the current funclet and keep stepping instead of stopping. This
    # touches core step-over for all languages, so it needs broad regression
    # testing. (On Darwin the async CFA is stable across the await -> the frames
    # compare Equal -> the bug doesn't manifest.) The sibling
    # test_efficient_step_over_packets subtest is debugserver/MultiMemRead
    # specific and does not apply to the Windows process plugin.
    # TWO-PART bug (verified on host): (1) the resumed-funclet CFA bump makes the
    # step-over ThreadPlan mistake the async continuation for a return to the
    # caller and stop early on line 3 (the eFrameCompareOlder branch at
    # ThreadPlanStepOverRange.cpp:160 doesn't consult IsEquivalentContext);
    # routing that case to the in-range handling (which re-anchors via
    # InRange()'s "same line, different range" path) fixes part 1 -- but then
    # (2) the step descends INTO f() (line 15) instead of stopping on line 4,
    # because stepping over the async CALL (`await f()`) doesn't step over the
    # callee. Part 2 is the deeper async-call step-over problem and is still open.
    @skipIf(oslist=["windows", "linux"])
    def test(self):
        """Test conditions for async step-over."""
        self.build()

        source_file = lldb.SBFileSpec("main.swift")
        target, process, thread, bkpt = lldbutil.run_to_source_breakpoint(
            self, "BREAK HERE", source_file
        )
        bkpt.SetEnabled(False) # avoid hitting multiple locations in async breakpoints

        expected_line_nums = [4]  # print(x)
        expected_line_nums += [5, 6, 7, 8, 5, 6, 7, 8, 5]  # two runs over the loop
        expected_line_nums += [9, 10]  # if line + if block
        for expected_line_num in expected_line_nums:
            thread.StepOver()
            stop_reason = thread.GetStopReason()
            self.assertStopReason(stop_reason, lldb.eStopReasonPlanComplete)
            self.check_is_in_line(thread, expected_line_num)
            self.check_x_is_available(thread.frames[0])

    @skipEmbeddedSwift
    @skipIfOutOfTreeDebugserver
    @swiftTest
    @skipIf(oslist=["windows", "linux"])
    def test_efficient_step_over_packets(self):
        """Test that MultiMemRead is used while stepping"""
        logfile = os.path.join(self.getBuildDir(), "log.txt")
        self.runCmd(f"log enable -f {logfile} gdb-remote packets process")
        self.addTearDownHook(lambda: self.runCmd("log disable gdb-remote packets"))

        self.build()

        source_file = lldb.SBFileSpec("main.swift")
        target, process, thread, bkpt = lldbutil.run_to_source_breakpoint(
            self, "BREAK HERE", source_file
        )
        bkpt.SetEnabled(False) # avoid hitting multiple locations in async breakpoints

        self.runCmd(f"proc plugin packet send StartTesting", check=False)
        thread.StepOver()
        self.runCmd(f"proc plugin packet send EndTesting", check=False)
        stop_reason = thread.GetStopReason()
        self.assertStopReason(stop_reason, lldb.eStopReasonPlanComplete)

        self.assertTrue(os.path.exists(logfile))
        log_text = open(logfile).read()
        log_text = log_text.split("StartTesting", 1)[-1].split("EndTesting", 1)[0]
        self.assertIn("MultiMemRead:", log_text)
        self.assertNotIn("MultiMemRead error", log_text)
