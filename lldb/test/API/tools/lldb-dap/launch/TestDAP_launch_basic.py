"""
Test lldb-dap launch request.
"""

from lldbsuite.test.decorators import expectedFailureWindows
import lldbdap_testcase


class TestDAP_launch_basic(lldbdap_testcase.DAPTestCaseBase):
    """
    Tests the default launch of a simple program. No arguments,
    environment, or anything else is specified.
    """

    def test(self):
        program = self.getBuildArtifact("a.out")
        self.build_and_launch(program)
        self.continue_to_exit()
        # Now get the STDOUT and verify our program argument is correct
        output = self.get_stdout()
        self.assertTrue(output and len(output) > 0, "expect program output")
        lines = output.splitlines()
        # The lldb-server gdb-remote vRun packet sends the executable path in
        # FileSpec's normalised form (forward slashes), so on Windows the
        # inferior's argv[0] uses '/' even though `program` here uses '\\'.
        # Compare against the normalised form to be path-separator-tolerant.
        normalized = program.replace("\\", "/")
        self.assertIn(
            normalized, lines[0], "make sure program path is in first argument"
        )
