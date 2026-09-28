"""
Tests that C++ expression evaluation can
disambiguate between rvalue and lvalue
reference-qualified functions.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@requireExpressionEvaluation
class TestFunctionRefQualifiers(TestBase):
    TEST_WITH_PDB_DEBUG_INFO = True

    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "Break here", lldb.SBFileSpec("main.cpp")
        )

        # CodeView has no type records for typedefs, so with PDB the methods
        # return the underlying types.
        is_pdb = self.getDebugInfo() == "pdb"
        u32 = "unsigned int" if is_pdb else "uint32_t"
        i64 = "long long" if is_pdb else "int64_t"

        # const lvalue
        self.expect_expr("const_foo.func()", result_type=u32, result_value="0")

        # const rvalue
        self.expect_expr(
            "static_cast<Foo const&&>(Foo{}).func()",
            result_type=i64,
            result_value="1",
        )

        # non-const lvalue
        self.expect_expr("foo.func()", result_type=u32, result_value="2")

        # non-const rvalue
        self.expect_expr("Foo{}.func()", result_type=i64, result_value="3")

        self.filecheck(
            "target modules dump ast",
            __file__,
            "--check-prefix=" + ("PDB" if is_pdb else "CHECK"),
        )
        # CHECK:      |-CXXMethodDecl {{.*}} func 'uint32_t () const &'
        # CHECK-NEXT: | `-AsmLabelAttr {{.*}}
        # CHECK-NEXT: |-CXXMethodDecl {{.*}} func 'int64_t () const &&'
        # CHECK-NEXT: | `-AsmLabelAttr {{.*}}
        # CHECK-NEXT: |-CXXMethodDecl {{.*}} func 'uint32_t () &'
        # CHECK-NEXT: | `-AsmLabelAttr {{.*}}
        # CHECK-NEXT: `-CXXMethodDecl {{.*}} func 'int64_t () &&'
        # CHECK-NEXT:   `-AsmLabelAttr {{.*}}
        # PDB:      |-CXXMethodDecl {{.*}} func 'unsigned int () const &'
        # PDB-NEXT: | `-AsmLabelAttr {{.*}}
        # PDB-NEXT: |-CXXMethodDecl {{.*}} func 'long long () const &&'
        # PDB-NEXT: | `-AsmLabelAttr {{.*}}
        # PDB-NEXT: |-CXXMethodDecl {{.*}} func 'unsigned int () &'
        # PDB-NEXT: | `-AsmLabelAttr {{.*}}
        # PDB-NEXT: `-CXXMethodDecl {{.*}} func 'long long () &&'
        # PDB-NEXT:   `-AsmLabelAttr {{.*}}
