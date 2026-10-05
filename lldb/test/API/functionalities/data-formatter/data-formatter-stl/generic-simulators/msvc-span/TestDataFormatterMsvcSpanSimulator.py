"""
Test the MSVC STL std::span formatter on a simulator of its layout, where a
static extent is not stored in the object.
"""

import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class MsvcSpanSimulatorDataFormatterTestCase(TestBase):
    TEST_WITH_PDB_DEBUG_INFO = True

    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("main.cpp")
        )

        numbers = [
            ValueCheck(name="[0]", value="1"),
            ValueCheck(name="[1]", value="12"),
            ValueCheck(name="[2]", value="123"),
            ValueCheck(name="[3]", value="1234"),
            ValueCheck(name="[4]", value="12345"),
        ]

        for name in ["g_static_span", "static_span", "typedef_span", "dynamic_span"]:
            self.expect_var_path(name, summary="size=5", children=numbers)
        self.expect_var_path("static_zero_span", summary="size=0", children=[])

        self.expect_var_path("static_span[4]", value="12345")
        self.expect_expr(
            "static_span", result_summary="size=5", result_children=numbers
        )
