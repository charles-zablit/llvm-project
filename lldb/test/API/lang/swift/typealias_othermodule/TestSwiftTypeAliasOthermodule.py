from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftTypeAliasOtherModule(TestBase):
    NO_DEBUG_INFO_TESTCASE = True

    @skipEmbeddedSwift
    @swiftTest
    def test_frame_variable(self):
        """Test that type aliases can be imported from reflection metadata"""
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("main.swift"), extra_images=["Dylib"]
        )
        self.expect("frame variable -- payload", substrs=["Bool", "true"])
        self.expect("continue")
        self.expect("frame variable -- payload", substrs=["Bool", "true"])

    @skipEmbeddedSwift
    @swiftTest
    # rdar://177523573: the typealias itself resolves (the result is correctly
    # typed Dylib.Impl.Payload), but the expression evaluator yields the
    # underlying storage -- "(_value = 1)" -- rather than running it through
    # the Bool formatter, so the printed value never matches. `frame variable`
    # on the same expression is fine.
    @skipIfWindows
    def test_expression(self):
        """Test that type aliases can be imported into expressions from reflection metadata"""
        self.build()
        self.expect('settings set symbols.swift-load-conformances true')
        lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("main.swift"), extra_images=["Dylib"]
        )
        self.expect("expr -- payload", substrs=["Dylib.Impl.Payload", "true"])
        self.expect("continue")
        self.expect(
            "expr -- payload", substrs=["Dylib.GenericImpl<Bool>.Payload", "true"]
        )
