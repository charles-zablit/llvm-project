import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class TestBreakInLoadedDylib(TestBase):
    """Test that we can set a source regex breakpoint that will take in
    a dlopened library that hasn't loaded when we set the breakpoint."""

    NO_DEBUG_INFO_TESTCASE = True

    @skipIfRemote
    def common_setup(self):
        self.build()
        ctx = self.platformContext
        self.main_spec = lldb.SBFileSpec("main.cpp")
        self.b_spec = lldb.SBFileSpec("b.cpp")
        self.lib_shortname = "lib_b"
        self.lib_fullname = ctx.getFullLibName(self.lib_shortname)
        self.lib_spec = lldb.SBFileSpec(self.lib_fullname)

    @skipIfRemote
    @expectedFailureAll(
        oslist=["windows"],
        bugnumber=(
            "On Windows lldb-server, OnLoadDll just sets a "
            "pending-library-events flag; without an explicit stop on "
            "LOAD_DLL_DEBUG_EVENT the inferior runs straight through "
            "dlopen()'d code and exits before the client has a chance "
            "to resolve the pending breakpoint and plant the int3 byte. "
            "An attempted fix (RequestDllEventBlock + ContinueAsyncDllEvent "
            "predicate gating the DebuggerThread loop) successfully "
            "delivers the stop and the BP byte gets written, but the "
            "subsequent inferior execution still does not trip the int3 -- "
            "needs investigation into the loader / Windows page-protection "
            "interaction with debug writes against just-mapped DLLs."
        ),
    )
    def test_break_in_dlopen_dylib_using_lldbutils(self):
        self.common_setup()
        lldbutil.run_to_source_breakpoint(
            self,
            "Break here in dylib",
            self.b_spec,
            bkpt_module=self.lib_fullname,
            extra_images=[self.lib_shortname],
            has_locations_before_run=False,
        )

    @skipIfRemote
    @expectedFailureAll(
        oslist=["windows"],
        bugnumber="See test_break_in_dlopen_dylib_using_lldbutils.",
    )
    def test_break_in_dlopen_dylib_using_target(self):
        self.common_setup()

        target, process, _, _ = lldbutil.run_to_source_breakpoint(
            self,
            "Break here before we dlopen",
            self.main_spec,
            extra_images=[self.lib_shortname],
        )

        # Now set some breakpoints that won't take till the library is loaded:
        # This one is currently how lldbutils does it but test here in case that changes:
        bkpt1 = target.BreakpointCreateBySourceRegex(
            "Break here in dylib", self.b_spec, self.lib_fullname
        )

        # Try the file list API as well.  Put in some bogus entries too, to make sure those
        # don't trip us up:

        files_list = lldb.SBFileSpecList()
        files_list.Append(self.b_spec)
        files_list.Append(self.main_spec)
        files_list.Append(lldb.SBFileSpec("I_bet_nobody_has_this_file.cpp"))

        modules_list = lldb.SBFileSpecList()
        modules_list.Append(self.lib_spec)
        modules_list.Append(lldb.SBFileSpec("libI_bet_not_this_one_either.dylib"))

        bkpt2 = target.BreakpointCreateBySourceRegex(
            "Break here in dylib", modules_list, files_list
        )

        lldbutil.continue_to_breakpoint(process, bkpt1)
        self.assertEqual(bkpt1.GetHitCount(), 1, "Hit breakpoint 1")
        self.assertEqual(bkpt2.GetHitCount(), 1, "Hit breakpoint 2")
