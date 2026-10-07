"""
Test that typing into a running inferior does not stop it.

lldb-server has no pseudo-terminal it can share with lldb on Windows. Without
QSetSTDINForwardingPipe, each chunk of stdin is a gdb-remote `I` packet, and
lldb has to interrupt the running inferior to send it.
"""

import os
import re

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *


class STDINTestCase(TestBase):
    NO_DEBUG_INFO_TESTCASE = True

    @staticmethod
    def _strip_vt(text):
        """Strip the VT sequences ConPTY adds around the inferior's output."""
        return re.sub(r"\x1b(?:\[[0-9;?]*[A-Za-z]|\][^\x07]*\x07)", "", text)

    @staticmethod
    def _read_stdout(process):
        output = ""
        while chunk := process.GetSTDOUT(1024):
            output += chunk
        return output

    def _set_use_pipes(self, use_pipes):
        """Pick anonymous pipes or a ConPTY for the inferior's stdio."""
        saved = os.environ.get("LLDB_LAUNCH_FLAG_USE_PIPES")

        def restore():
            if saved is None:
                os.environ.pop("LLDB_LAUNCH_FLAG_USE_PIPES", None)
            else:
                os.environ["LLDB_LAUNCH_FLAG_USE_PIPES"] = saved

        self.addTearDownHook(restore)
        if use_pipes:
            os.environ["LLDB_LAUNCH_FLAG_USE_PIPES"] = "1"
        else:
            os.environ.pop("LLDB_LAUNCH_FLAG_USE_PIPES", None)

    def _type_into_running_process(self):
        self.build()
        log_file = self.getBuildArtifact(self.testMethodName + "-packets.log")
        self.runCmd('log enable -f "{}" gdb-remote packets'.format(log_file))

        target = self.dbg.CreateTarget(self.getBuildArtifact("a.out"))
        self.assertTrue(target, VALID_TARGET)

        self.dbg.SetAsync(True)
        listener = self.dbg.GetListener()
        process = target.LaunchSimple(None, None, self.get_process_working_directory())
        self.assertTrue(process, PROCESS_IS_VALID)

        event = lldb.SBEvent()
        output = ""
        while "ready" not in self._strip_vt(output):
            self.assertTrue(
                listener.WaitForEvent(30, event),
                "inferior did not print 'ready', output so far: %r" % output,
            )
            output += self._read_stdout(process)

        # Type the way IOHandlerProcessSTDIOWindows forwards the console: one
        # character at a time, with Enter read as "\r\n".
        for c in "foofoofoo\r\n":
            process.PutSTDIN(c)

        stops = []
        while True:
            self.assertTrue(
                listener.WaitForEvent(30, event),
                "inferior did not exit, output so far: %r" % output,
            )
            output += self._read_stdout(process)
            # STDOUT events carry the process state too, as of when the output
            # arrived, which can still be the stop at launch.
            if not event.GetType() & lldb.SBProcess.eBroadcastBitStateChanged:
                continue
            state = lldb.SBProcess.GetStateFromEvent(event)
            if state == lldb.eStateStopped:
                stream = lldb.SBStream()
                event.GetDescription(stream)
                stops.append(stream.GetData())
            if state == lldb.eStateExited:
                break
        output += self._read_stdout(process)
        self.runCmd("log disable gdb-remote packets")

        self.assertEqual(stops, [], "typing stopped the inferior")
        self.assertIn("Input was 'foofoofoo'", self._strip_vt(output))
        self.assertEqual(process.GetExitStatus(), 0)
        with open(log_file) as f:
            stdin_packets = [line for line in f if "send packet: $I" in line]
        self.assertEqual(stdin_packets, [], "stdin went through `I` packets")

    @requireWindows
    def test_pipes(self):
        """Stdin typed into an inferior using anonymous pipes."""
        self._set_use_pipes(True)
        self._type_into_running_process()

    @requireWindows
    @skipUnlessWindowsConPTY
    @skipIf(oslist=["windows"], archs=["aarch64"], bugnumber="#194069")
    def test_conpty(self):
        """Stdin typed into an inferior attached to a ConPTY.

        lldb-server only gets a ConPTY for the inferior when lldb runs in a
        console, so under lit this uses pipes again with lldb-server.
        """
        self._set_use_pipes(False)
        self._type_into_running_process()
