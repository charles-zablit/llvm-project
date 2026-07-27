import lldb
import platform
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil


class TestCase(lldbtest.TestBase):
    @skipEmbeddedSwift
    @swiftTest
    @skipIf(oslist=["linux"])
    @skipIf(macos_version=["<", "26.0"], asan=True) # rdar://138777205
    def test(self):
        """Test conditions for async step-in."""
        self.build()

        src = lldb.SBFileSpec("main.swift")
        target, _, thread, _ = lldbutil.run_to_source_breakpoint(self, "await f()", src)
        self.assertEqual(thread.frame[0].function.mangled, "$s1a5entryO4mainyyYaFZ")

        sym_ctx_list = target.FindFunctions("$s1a5entryO4mainyyYaFZTQ0_")
        self.assertEqual(sym_ctx_list.GetSize(), 1)
        function = sym_ctx_list[0].function
        self.assertIsNotNone(function)
        instructions = list(function.GetInstructions(target))
        self.assertGreater(len(instructions), 0)
        # Expected to be a trampoline that tail calls `swift_task_switch`.
        # The tail call is the final instruction. On Windows the callee lives
        # in another DLL, so the jump is indirect through the import address
        # table (`movq __imp_swift_task_switch(%rip), %rax` ... `jmpq *%rax`)
        # and the symbol name is attached to that address load rather than to
        # the jump, with the stack epilogue in between. Scan the tail so both
        # the direct and the indirect form are accepted.
        tail_call = " ".join(inst.GetComment(target) for inst in instructions[-4:])
        self.assertIn("swift_task_switch", tail_call)

        # Using the line table, build a set of the non-zero line numbers for
        # this this function - and verify that there is exactly one line.
        lines = {inst.addr.line_entry.line for inst in instructions}
        lines.discard(0)
        self.assertEqual(lines, {3})

        # Required for builds that have debug info.
        lldbutil.ignore_swift_concurrency_when_stepping(platform, self)
        thread.StepInto()
        frame = thread.frame[0]
        # Step in from `main` should progress through to `f`.
        self.assertEqual(frame.name, "a.f() async -> Swift.Int")
