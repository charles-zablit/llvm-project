//===-- MangledTest.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TestingSupport/TestUtilities.h"

#include "lldb/Core/DemangledNameInfo.h"
#include "lldb/Core/Mangled.h"

#include "gtest/gtest.h"

using namespace lldb;
using namespace lldb_private;

struct DemanglingPartsTestCase {
  const char *mangled;
  DemangledNameInfo expected_info;
  std::string_view basename;
  std::string_view scope;
  std::string_view qualifiers;
  bool valid_basename = true;
};

DemanglingPartsTestCase g_demangling_parts_test_cases[] = {
    // clang-format off
   { "_ZNVKO3BarIN2ns3QuxIiEEE1CIPFi3FooIS_IiES6_EEE6methodIS6_EENS5_IT_SC_E5InnerIiEESD_SD_",
     { /*.BasenameRange=*/{92, 98}, /*.ScopeRange=*/{36, 92}, /*.ArgumentsRange=*/{ 108, 158 },
       /*.QualifiersRange=*/{158, 176} },
     /*.basename=*/"method",
     /*.scope=*/"Bar<ns::Qux<int>>::C<int (*)(Foo<Bar<int>, Bar<int>>)>::",
     /*.qualifiers=*/" const volatile &&"
   },
   { "_Z7getFuncIfEPFiiiET_",
     { /*.BasenameRange=*/{6, 13}, /*.ScopeRange=*/{6, 6}, /*.ArgumentsRange=*/{ 20, 27 }, /*.QualifiersRange=*/{38, 38} },
     /*.basename=*/"getFunc",
     /*.scope=*/"",
     /*.qualifiers=*/""
   },
   { "_ZN1f1b1c1gEv",
     { /*.BasenameRange=*/{9, 10}, /*.ScopeRange=*/{0, 9}, /*.ArgumentsRange=*/{ 10, 12 },
       /*.QualifiersRange=*/{12, 12} },
     /*.basename=*/"g",
     /*.scope=*/"f::b::c::",
     /*.qualifiers=*/""
   },
   { "_ZN5test73fD1IiEEDTcmtlNS_1DEL_ZNS_1bEEEcvT__EES2_",
     { /*.BasenameRange=*/{45, 48}, /*.ScopeRange=*/{38, 45}, /*.ArgumentsRange=*/{ 53, 58 },
       /*.QualifiersRange=*/{58, 58} },
     /*.basename=*/"fD1",
     /*.scope=*/"test7::",
     /*.qualifiers=*/""
   },
   { "_ZN5test73fD1IiEEDTcmtlNS_1DEL_ZNS_1bINDT1cE1dEEEEEcvT__EES2_",
     { /*.BasenameRange=*/{61, 64}, /*.ScopeRange=*/{54, 61}, /*.ArgumentsRange=*/{ 69, 79 },
       /*.QualifiersRange=*/{79, 79} },
     /*.basename=*/"fD1",
     /*.scope=*/"test7::",
     /*.qualifiers=*/""
   },
   { "_ZN5test7INDT1cE1dINDT1cE1dEEEE3fD1INDT1cE1dINDT1cE1dEEEEEDTcmtlNS_1DEL_ZNS_1bINDT1cE1dEEEEEcvT__EES2_",
     { /*.BasenameRange=*/{120, 123}, /*.ScopeRange=*/{81, 120}, /*.ArgumentsRange=*/{ 155, 168 },
       /*.QualifiersRange=*/{168, 168} },
     /*.basename=*/"fD1",
     /*.scope=*/"test7<decltype(c)::d<decltype(c)::d>>::",
     /*.qualifiers=*/""
   },
   { "_ZN8nlohmann16json_abi_v3_11_310basic_jsonINSt3__13mapENS2_6vectorENS2_12basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEEbxydS8_NS0_14adl_serializerENS4_IhNS8_IhEEEEvE5parseIRA29_KcEESE_OT_NS2_8functionIFbiNS0_6detail13parse_event_tERSE_EEEbb",
     { /*.BasenameRange=*/{687, 692}, /*.ScopeRange=*/{343, 687}, /*.ArgumentsRange=*/{ 713, 1174 },
       /*.QualifiersRange=*/{1174, 1174} },
     /*.basename=*/"parse",
     /*.scope=*/"nlohmann::json_abi_v3_11_3::basic_json<std::__1::map, std::__1::vector, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>, bool, long long, unsigned long long, double, std::__1::allocator, nlohmann::json_abi_v3_11_3::adl_serializer, std::__1::vector<unsigned char, std::__1::allocator<unsigned char>>, void>::",
     /*.qualifiers=*/""
   },
   { "_ZN8nlohmann16json_abi_v3_11_310basic_jsonINSt3__13mapENS2_6vectorENS2_12basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEEbxydS8_NS0_14adl_serializerENS4_IhNS8_IhEEEEvEC1EDn",
     { /*.BasenameRange=*/{344, 354}, /*.ScopeRange=*/{0, 344}, /*.ArgumentsRange=*/{ 354, 370 },
       /*.QualifiersRange=*/{370, 370} },
     /*.basename=*/"basic_json",
     /*.scope=*/"nlohmann::json_abi_v3_11_3::basic_json<std::__1::map, std::__1::vector, std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char>>, bool, long long, unsigned long long, double, std::__1::allocator, nlohmann::json_abi_v3_11_3::adl_serializer, std::__1::vector<unsigned char, std::__1::allocator<unsigned char>>, void>::",
     /*.qualifiers=*/""
   },
   { "_Z3fppIiEPFPFvvEiEf",
     { /*.BasenameRange=*/{10, 13}, /*.ScopeRange=*/{10, 10}, /*.ArgumentsRange=*/{ 18, 25 }, /*.QualifiersRange=*/{34,34} },
     /*.basename=*/"fpp",
     /*.scope=*/"",
     /*.qualifiers=*/""
   },
   { "_Z3fppIiEPFPFvvEN2ns3FooIiEEEf",
     { /*.BasenameRange=*/{10, 13}, /*.ScopeRange=*/{10, 10}, /*.ArgumentsRange=*/{ 18, 25 },
       /*.QualifiersRange=*/{43, 43} },
     /*.basename=*/"fpp",
     /*.scope=*/"",
     /*.qualifiers=*/""
   },
   { "_Z3fppIiEPFPFvPFN2ns3FooIiEENS2_3BarIfE3QuxEEEPFS2_S2_EEf",
     { /*.BasenameRange=*/{10, 13}, /*.ScopeRange=*/{10, 10}, /*.ArgumentsRange=*/{ 18, 25 },
       /*.QualifiersRange=*/{108, 108} },
     /*.basename=*/"fpp",
     /*.scope=*/"",
     /*.qualifiers=*/""
   },
   { "_ZN2ns8HasFuncsINS_3FooINS1_IiE3BarIfE3QuxEEEE3fppIiEEPFPFvvEiEf",
     { /*.BasenameRange=*/{64, 67}, /*.ScopeRange=*/{10, 64}, /*.ArgumentsRange=*/{ 72, 79 },
       /*.QualifiersRange=*/{88, 88} },
     /*.basename=*/"fpp",
     /*.scope=*/"ns::HasFuncs<ns::Foo<ns::Foo<int>::Bar<float>::Qux>>::",
     /*.qualifiers=*/""
   },
   { "_ZN2ns8HasFuncsINS_3FooINS1_IiE3BarIfE3QuxEEEE3fppIiEEPFPFvvES2_Ef",
     { /*.BasenameRange=*/{64, 67}, /*.ScopeRange=*/{10, 64}, /*.ArgumentsRange=*/{ 72, 79 },
       /*.QualifiersRange=*/{97, 97} },
     /*.basename=*/"fpp",
     /*.scope=*/"ns::HasFuncs<ns::Foo<ns::Foo<int>::Bar<float>::Qux>>::",
     /*.qualifiers=*/"",
   },
   { "_ZN2ns8HasFuncsINS_3FooINS1_IiE3BarIfE3QuxEEEE3fppIiEEPFPFvPFS2_S5_EEPFS2_S2_EEf",
     { /*.BasenameRange=*/{64, 67}, /*.ScopeRange=*/{10, 64}, /*.ArgumentsRange=*/{ 72, 79 },
       /*.QualifiersRange=*/{162, 162} },
     /*.basename=*/"fpp",
     /*.scope=*/"ns::HasFuncs<ns::Foo<ns::Foo<int>::Bar<float>::Qux>>::",
     /*.qualifiers=*/"",
   },
   { "_ZNKO2ns3ns23Bar3fooIiEEPFPFNS0_3FooIiEEiENS3_IfEEEi",
     { /*.BasenameRange=*/{37, 40}, /*.ScopeRange=*/{23, 37}, /*.ArgumentsRange=*/{ 45, 50 },
       /*.QualifiersRange=*/{78, 87} },
     /*.basename=*/"foo",
     /*.scope=*/"ns::ns2::Bar::",
     /*.qualifiers=*/" const &&",
   },
   { "_ZTV11ImageLoader",
     { /*.BasenameRange=*/{0, 0}, /*.ScopeRange=*/{0, 0}, /*.ArgumentsRange=*/{ 0, 0 },
       /*.QualifiersRange=*/{0, 0} },
     /*.basename=*/"",
     /*.scope=*/"",
     /*.qualifiers=*/"",
     /*.valid_basename=*/false
   }
    // clang-format on
};

struct DemanglingPartsTestFixture
    : public ::testing::TestWithParam<DemanglingPartsTestCase> {};

namespace {
class TestAllocator {
  llvm::BumpPtrAllocator Alloc;

public:
  void reset() { Alloc.Reset(); }

  template <typename T, typename... Args> T *makeNode(Args &&...args) {
    return new (Alloc.Allocate(sizeof(T), alignof(T)))
        T(std::forward<Args>(args)...);
  }

  void *allocateNodeArray(size_t sz) {
    return Alloc.Allocate(sizeof(llvm::itanium_demangle::Node *) * sz,
                          alignof(llvm::itanium_demangle::Node *));
  }
};
} // namespace

TEST_P(DemanglingPartsTestFixture, DemanglingParts) {
  const auto &[mangled, info, basename, scope, qualifiers, valid_basename] =
      GetParam();

  llvm::itanium_demangle::ManglingParser<TestAllocator> Parser(
      mangled, mangled + ::strlen(mangled));

  const auto *Root = Parser.parse();

  ASSERT_NE(nullptr, Root);

  TrackingOutputBuffer OB;
  Root->print(OB);
  auto demangled = std::string_view(OB);

  ASSERT_EQ(OB.NameInfo.hasBasename(), valid_basename);

  EXPECT_EQ(OB.NameInfo.BasenameRange, info.BasenameRange);
  EXPECT_EQ(OB.NameInfo.ScopeRange, info.ScopeRange);
  EXPECT_EQ(OB.NameInfo.ArgumentsRange, info.ArgumentsRange);
  EXPECT_EQ(OB.NameInfo.QualifiersRange, info.QualifiersRange);

  auto get_part = [&](const std::pair<size_t, size_t> &loc) {
    return demangled.substr(loc.first, loc.second - loc.first);
  };

  EXPECT_EQ(get_part(OB.NameInfo.BasenameRange), basename);
  EXPECT_EQ(get_part(OB.NameInfo.ScopeRange), scope);
  EXPECT_EQ(get_part(OB.NameInfo.QualifiersRange), qualifiers);
}

INSTANTIATE_TEST_SUITE_P(DemanglingPartsTests, DemanglingPartsTestFixture,
                         ::testing::ValuesIn(g_demangling_parts_test_cases));
