"""
Tests const static data members as specified by C++11 [class.static.data]p3
with (u)int128_t types.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@requireExpressionEvaluation
class TestCase(TestBase):
    TEST_WITH_PDB_DEBUG_INFO = True

    # int128 is not available on 32-bit ARM.
    @skipIf(archs=["arm$"])
    def test_int128(self):
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.cpp")
        )

        if self.getDebugInfo() == "pdb":
            # CodeView has the values in S_CONSTANT records. A numeric leaf
            # holds at most 64 bits, which is enough here: MSVC's
            # numeric_limits has no __int128 specialization, so all four
            # members are 0.
            for name in ["int128_max", "uint128_max", "int128_min", "uint128_min"]:
                local = self.frame().FindVariable(name)
                self.expect_expr("A::" + name, result_value=local.GetValue())
            return

        # Try to use the (u)int128_t data members which are not supported at
        # the moment. Just verify that LLDB doesn't report an incorrect value
        # for them and just treats them as normal variables (which will lead
        # to linker errors as they are not defined anywhere).
        self.expect(
            "expr A::int128_max", error=True, substrs=["Couldn't look up symbols:"]
        )
        self.expect(
            "expr A::uint128_max", error=True, substrs=["Couldn't look up symbols:"]
        )
        self.expect(
            "expr A::int128_min", error=True, substrs=["Couldn't look up symbols:"]
        )
        self.expect(
            "expr A::uint128_min", error=True, substrs=["Couldn't look up symbols:"]
        )
