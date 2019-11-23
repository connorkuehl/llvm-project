//===- unittest/AST/RandstructTest.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains tests for Clang's structure field layout randomization.
//
//===----------------------------------------------------------------------===//

/*
 * Build this test suite by running `make ASTTests` in the build folder.
 *
 * Run this test suite by running the following in the build folder:
 * ` ./tools/clang/unittests/AST/ASTTests --gtest_filter=StructureLayoutRandomization*`
 */

#include "clang/AST/Randstruct.h"
#include "gtest/gtest.h"

#include "DeclMatcher.h"
#include "Language.h"
#include "clang/AST/RecordLayout.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/Tooling.h"

#include <vector>

using namespace clang::randstruct;

namespace clang {
namespace ast_matchers {

static std::unique_ptr<ASTUnit> makeAST(const std::string &SourceCode,
                                        Language Lang) {
  const auto Args = getBasicRunOptionsForLanguage(Lang);
  auto AST = tooling::buildASTFromCodeWithArgs(SourceCode, Args, "input.cc");
  return AST;
}

static RecordDecl *getRecordDeclFromAST(const ASTContext &C,
                                        const std::string &Name) {
  return FirstDeclMatcher<RecordDecl>().match(C.getTranslationUnitDecl(),
                                              recordDecl(hasName(Name)));
}

static std::vector<std::string> getFieldNamesFromRecord(const RecordDecl *RD) {
  std::vector<std::string> Fields;
  Fields.reserve(8);
  for (auto *Field : RD->fields())
    Fields.push_back(Field->getNameAsString());
  return Fields;
}

static bool isSubsequence(const std::vector<std::string> &Seq,
                          const std::vector<std::string> &Subseq) {
  const auto SeqLen = Seq.size();
  const auto SubLen = Subseq.size();

  auto IsSubseq = false;
  for (auto I = 0u; I < SeqLen; ++I) {
    if (Seq[I] == Subseq[0]) {
      IsSubseq = true;
      for (auto J = 0u; J + I < SeqLen && J < SubLen; ++J) {
        if (Seq[J + I] != Subseq[J]) {
          IsSubseq = false;
          break;
        }
      }
    }
  }
  return IsSubseq;
}

#define RANDSTRUCT_TEST_SUITE_TEST StructureLayoutRandomizationTestSuiteTest

TEST(RANDSTRUCT_TEST_SUITE_TEST, CanDetermineIfSubsequenceExists) {
  const std::vector<std::string> S0 = {"a", "b", "c", "d"};
  ASSERT_TRUE(isSubsequence(S0, {"b", "c"}));
  ASSERT_TRUE(isSubsequence(S0, {"a", "b", "c", "d"}));
  ASSERT_TRUE(isSubsequence(S0, {"b", "c", "d"}));
  ASSERT_TRUE(isSubsequence(S0, {"a"}));
  ASSERT_FALSE(isSubsequence(S0, {"a", "d"}));
}

#define RANDSTRUCT_TEST StructureLayoutRandomization

TEST(RANDSTRUCT_TEST, UnmarkedStructuresAreNotRandomized) {
  std::string Code =
      R"(
        struct dont_randomize_me {
            int potato;
            float tomato;
            long cabbage;
        };
        )";

  const auto AST = makeAST(Code, Lang_C);
  const auto *RD = getRecordDeclFromAST(AST->getASTContext(), "dont_randomize_me");
  const std::vector<std::string> Expected = {"potato", "tomato", "cabbage"};
  const std::vector<std::string> Actual = getFieldNamesFromRecord(RD);

  ASSERT_EQ(Expected, Actual);
}

TEST(RANDSTRUCT_TEST, StructuresCanBeMarkedWithRandomizeLayoutAttr) {
  std::string Code =
      R"(
        struct marked {
            int bacon;
            long lettuce;
        } __attribute__((randomize_layout));

        struct not_marked {
            double cookies;
        };
        )";

  const auto AST = makeAST(Code, Lang_C);
  const auto *RD0 = getRecordDeclFromAST(AST->getASTContext(), "marked");
  const auto *RD1 = getRecordDeclFromAST(AST->getASTContext(), "not_marked");

  ASSERT_TRUE(RD0->getAttr<RandomizeLayoutAttr>());
  ASSERT_FALSE(RD1->getAttr<RandomizeLayoutAttr>());
}

TEST(RANDSTRUCT_TEST, StructuresCanBeMarkedWithNoRandomizeLayoutAttr) {
  std::string Code =
      R"(
        struct marked {
            int bacon;
            long lettuce;
        } __attribute__((no_randomize_layout));

        struct not_marked {
            double cookies;
        };
        )";

  const auto AST = makeAST(Code, Lang_C);
  const auto *RD0 = getRecordDeclFromAST(AST->getASTContext(), "marked");
  const auto *RD1 = getRecordDeclFromAST(AST->getASTContext(), "not_marked");

  ASSERT_TRUE(RD0->getAttr<NoRandomizeLayoutAttr>());
  ASSERT_FALSE(RD1->getAttr<NoRandomizeLayoutAttr>());
}

TEST(RANDSTRUCT_TEST, StructuresLayoutFieldLocationsCanBeRandomized) {
  std::string Code =
      R"(
        struct test_struct {
            int a;
            int b;
            int c;
            int d;
            int e;
            int f;
        };
        )";

  const auto AST = makeAST(Code, Lang_C);
  const auto *RD = getRecordDeclFromAST(AST->getASTContext(), "test_struct");
  randomizeStructureLayout(AST->getASTContext(), RD);
  const std::vector<std::string> Before = {"a", "b", "c", "d", "e", "f"};
  const std::vector<std::string> After = getFieldNamesFromRecord(RD);

  // FIXME: Could this be a brittle test? Planning on having a separate unit
  // test for reproducible randomizations with seed.
  ASSERT_NE(Before, After);
}

TEST(RANDSTRUCT_TEST,
     StructuresMarkedWithNoRandomizeLayoutShouldBeRejectedAndUnchanged) {
  std::string Code =
      R"(
        struct test_struct {
            int a;
            int b;
            int c;
            int d;
            int e;
            int f;
        } __attribute__((no_randomize_layout));
        )";

  const auto AST = makeAST(Code, Lang_C);
  const auto *RD = getRecordDeclFromAST(AST->getASTContext(), "test_struct");

  ASSERT_FALSE(randstruct::shouldRandomize(AST->getASTContext(), RD));
}

// FIXME: Clang trips an assertion in the DiagnosticsEngine when the warning is
// emitted while running under the test suite:
// clang/lib/Frontend/TextDiagnosticPrinter.cpp:150: virtual void
// clang::TextDiagnosticPrinter::HandleDiagnostic(clang::DiagnosticsEngine::Level,
// const clang::Diagnostic&): Assertion `TextDiag && "UnExpected diagnostic
// outside source file processing"' failed.
//
// Although the test *technically* is marked as pass; outside of the test suite
// this functionality works and no assertion is tripped.
TEST(
    RANDSTRUCT_TEST,
    DISABLED_EmitWarningWhenStructureIsMarkedWithBothRandomizeAndNoRandomizeAttributes) {
  std::string Code =
      R"(
        struct test_struct {
            int a;
            int b;
            int c;
        } __attribute__((no_randomize_layout)) __attribute__((randomize_layout));
        )";

  const auto AST = makeAST(Code, Lang_C);
  ASSERT_EQ(AST->getASTContext().getDiagnostics().getNumWarnings(), 1UL);
}

TEST(RANDSTRUCT_TEST,
     DISABLED_StructureMarkedWithBothAttributesRemainsUnchanged) {
  std::string Code =
      R"(
        struct test_struct {
            int a;
            int b;
            int c;
        } __attribute__((no_randomize_layout)) __attribute__((randomize_layout));
        )";

  const auto AST = makeAST(Code, Lang_C);
  const auto *RD = getRecordDeclFromAST(AST->getASTContext(), "test_struct");
  static_cast<void>(AST->getASTContext().getASTRecordLayout(RD));

  const std::vector<std::string> Expected = {"a", "b", "c"};
  const std::vector<std::string> Actual = getFieldNamesFromRecord(RD);
  ASSERT_EQ(Expected, Actual);
}

// End of FIXME regarding DiagnosticsEngine assertion tests.

TEST(RANDSTRUCT_TEST, AdjacentBitfieldsRemainAdjacentAfterRandomization) {
  std::string Code =
      R"(
        struct test_struct {
            int a;
            int b;
            int x : 1;
            int y : 1;
            int z : 1;
            int c;
        } __attribute__((randomize_layout));
        )";

  const auto AST = makeAST(Code, Lang_C);
  const auto *RD = getRecordDeclFromAST(AST->getASTContext(), "test_struct");
  randomizeStructureLayout(AST->getASTContext(), RD);

  const std::vector<std::string> Actual = getFieldNamesFromRecord(RD);
  const std::vector<std::string> Subseq = {"x", "y", "z"};
  ASSERT_TRUE(isSubsequence(Actual, Subseq));
}

TEST(RANDSTRUCT_TEST, VariableLengthArrayMemberRemainsAtEndOfStructure) {
  std::string Code =
      R"(
        struct test_struct {
            int a;
            double b;
            short c;
            char name[];
        };
        )";

  const auto AST = makeAST(Code, Lang_C);
  const auto *RD = getRecordDeclFromAST(AST->getASTContext(), "test_struct");
  randomizeStructureLayout(AST->getASTContext(), RD);

  std::vector<std::string> Fields = getFieldNamesFromRecord(RD);
  const auto VLA = std::find(Fields.begin(), Fields.end(), "name");
  ASSERT_TRUE(VLA + 1 == Fields.end());
}

TEST(RANDSTRUCT_TEST, RandstructDoesNotOverrideThePackedAttr) {
  std::string Code =
      R"(
        struct test_struct {
            char a;
            short b;
            int c;
        } __attribute__((packed, randomize_layout));

        struct another_struct {
            char a;
            int c;
        } __attribute__((packed, randomize_layout));

        struct last_struct {
            char a;
            long long b;
        } __attribute__((packed, randomize_layout));
        )";

  const auto AST = makeAST(Code, Lang_C);
  const auto *RD = getRecordDeclFromAST(AST->getASTContext(), "test_struct");
  const auto &Layout = AST->getASTContext().getASTRecordLayout(RD);

  ASSERT_EQ(7, Layout.getSize().getQuantity());

  const auto *RD1 = getRecordDeclFromAST(AST->getASTContext(), "another_struct");
  const auto &Layout1 = AST->getASTContext().getASTRecordLayout(RD1);

  ASSERT_EQ(5, Layout1.getSize().getQuantity());

  const auto *RD2 = getRecordDeclFromAST(AST->getASTContext(), "last_struct");
  const auto &Layout2 = AST->getASTContext().getASTRecordLayout(RD2);

  ASSERT_EQ(9, Layout2.getSize().getQuantity());
}

TEST(RANDSTRUCT_TEST, ZeroWidthBitfieldsSeparateAllocationUnits) {
  std::string Code =
      R"(
        struct test_struct {
            int a : 1;
            int   : 0;
            int b : 1;
        };
        )";

  const auto AST = makeAST(Code, Lang_C);
  const auto *RD = getRecordDeclFromAST(AST->getASTContext(), "test_struct");
  const std::vector<std::string> Before = getFieldNamesFromRecord(RD);
  ASSERT_TRUE(isSubsequence(Before, {"a", "", "b"}));

  randomizeStructureLayout(AST->getASTContext(), RD);
  const std::vector<std::string> After= getFieldNamesFromRecord(RD);
  ASSERT_FALSE(isSubsequence(After, {"a", "", "b"}));
}

TEST(RANDSTRUCT_TEST, RandstructDoesNotRandomizeUnionFieldOrder) {
  std::string Code =
      R"(
        union test_union {
            int a;
            int b;
            int c;
            int d;
            int e;
            int f;
            int g;
        } __attribute((randomize_layout));
        )";

  const auto AST = makeAST(Code, Lang_C);
  const auto *RD = getRecordDeclFromAST(AST->getASTContext(), "test_union");
  ASSERT_FALSE(shouldRandomize(AST->getASTContext(), RD));
}

} // namespace ast_matchers
} // namespace clang
