"""
Test lldb-dap launch request.
"""

import lldbdap_testcase
import tempfile


class TestDAP_launch_stdio_redirection(lldbdap_testcase.DAPTestCaseBase):
    """
    Test stdio redirection.
    """

    def test(self):
        self.build_and_create_debug_adapter()
        program = self.getBuildArtifact("a.out")

        with tempfile.NamedTemporaryFile("rt") as f:
            self.launch_and_configurationDone(program, stdio=[None, f.name])
            self.verify_process_exited()
            lines = f.readlines()
            # Same separator normalisation as TestDAP_launch_basic: the
            # gdb-remote vRun packet sends the executable path with forward
            # slashes, so on Windows argv[0] uses '/' even though `program`
            # uses '\\'.
            self.assertIn(
                program.replace("\\", "/"),
                lines[0],
                "make sure program path is in first argument",
            )
