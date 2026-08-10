"""
Test that creating a target through a symlinked (or otherwise aliased)
executable does not create a duplicate module at launch.

When a target is created via a path that is not the file's canonical path (a
symlink, or on Windows a subst/mapped drive), the running process reports its
*real* image path at launch. Module matching needs to compare the canonicalized
paths.
"""

import os
import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestBreakpointViaSymlink(TestBase):
    NO_DEBUG_INFO_TESTCASE = True

    def make_symlink(self, real_exe):
        """Create a symlink to real_exe whose real path differs, skipping the
        test if the platform can't create one."""
        link_exe = self.getBuildArtifact("a.out.symlink")
        try:
            if os.path.lexists(link_exe):
                os.remove(link_exe)
            os.symlink(real_exe, link_exe)
        except (OSError, NotImplementedError) as e:
            # Windows needs Developer Mode / SeCreateSymbolicLinkPrivilege.
            self.skipTest("could not create a symlink: %s" % e)
        self.addTearDownHook(lambda: os.remove(link_exe))
        self.assertNotEqual(os.path.realpath(link_exe), link_exe)
        return link_exe

    @skipIfRemote
    def test_breakpoint_resolves_once_via_symlink(self):
        self.build()
        real_exe = self.getBuildArtifact("a.out")
        link_exe = self.make_symlink(real_exe)

        target = self.dbg.CreateTarget(link_exe)
        self.assertTrue(target, VALID_TARGET)

        bkpt = target.BreakpointCreateBySourceRegex(
            "break here", lldb.SBFileSpec("main.c")
        )
        # Resolves against the preloaded module: exactly one location.
        self.assertEqual(
            bkpt.GetNumLocations(), 1, "one breakpoint location before launch"
        )

        process = target.LaunchSimple(None, None, self.get_process_working_directory())
        self.assertState(process.GetState(), lldb.eStateStopped)

        # After launch the running image may report its real path. With module
        # matching canonicalizing paths, the loaded module is the one lldb already
        # preloaded, so the breakpoint keeps exactly one resolved location.
        self.assertEqual(bkpt.GetNumLocations(), 1)
        self.assertEqual(bkpt.GetNumResolvedLocations(), 1)

        # The executable appears as a single module (no duplicate). The module
        # keeps the path it was created with (the symlink), so compare by real
        # path.
        exe_real = os.path.realpath(real_exe)
        matches = [
            m
            for m in target.module_iter()
            if os.path.realpath(m.GetFileSpec().fullpath) == exe_real
        ]
        self.assertEqual(
            len(matches), 1, "exactly one executable module, got %d" % len(matches)
        )

    @skipIfRemote
    def test_arg0_preserved_when_module_reused_via_symlink(self):
        """Canonical-path module matching lets a second target reuse the module
        of a first target that named the same file differently. That must not
        change how the second target is launched: argv[0] has to stay the path
        the user created *that* target with (multicall binaries dispatch on it).
        """
        self.build()
        real_exe = self.getBuildArtifact("a.out")
        link_exe = self.make_symlink(real_exe)

        # Create a target with the *real* path first.
        real_target = self.dbg.CreateTarget(real_exe)
        self.assertTrue(real_target, VALID_TARGET)

        # Create a second target via the *symlink*.
        link_target = self.dbg.CreateTarget(link_exe)
        self.assertTrue(link_target, VALID_TARGET)

        wd = self.get_process_working_directory()
        process = link_target.LaunchSimple(None, None, wd)
        self.assertTrue(process, PROCESS_IS_VALID)
        self.assertState(process.GetState(), lldb.eStateExited)
        arg0 = lldbutil.read_file_from_process_wd(self, "arg0.txt").strip()

        self.assertEqual(
            os.path.basename(arg0),
            os.path.basename(link_exe),
            "argv[0] should be the symlink used to create the target, got %r" % arg0,
        )
        self.assertNotEqual(
            os.path.basename(arg0),
            os.path.basename(real_exe),
            "argv[0] leaked the reused module's real path: %r" % arg0,
        )
