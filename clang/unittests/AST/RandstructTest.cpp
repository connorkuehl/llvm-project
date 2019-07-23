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

#include <vector>

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/Tooling.h"

#include "DeclMatcher.h"
#include "Language.h"

#include "clang/AST/Randstruct.h"
#include "gtest/gtest.h"

using namespace clang::randstruct;

namespace clang {
namespace ast_matchers {

static std::unique_ptr<ASTUnit> MakeAST(const std::string& SourceCode, Language Lang)
{
    auto Args = getBasicRunOptionsForLanguage(Lang);
    auto AST = tooling::buildASTFromCodeWithArgs(SourceCode, Args, "input.cc");
    return AST;
}

static RecordDecl* GetRecordDeclFromAST(const ASTContext& C, const std::string& Name)
{
    return FirstDeclMatcher<RecordDecl>().match(C.getTranslationUnitDecl(), recordDecl(hasName(Name)));
}

static std::vector<std::string> GetFieldNamesFromRecord(const RecordDecl* RD)
{
    std::vector<std::string> fields;
    fields.reserve(8);
    for (auto f : RD->fields())
        fields.push_back(f->getNameAsString());
    return fields;
}

static bool IsSubsequence(const std::vector<std::string>& Seq, const std::vector<std::string>& Subseq)
{
    auto seq_len = Seq.size();
    auto sseq_len = Subseq.size();

    auto is_subseq = false;
    for (auto i = 0u; i < seq_len; ++i) {
        if (Seq[i] == Subseq[0]) {
            is_subseq = true;
            for (auto j = 0u; j + i < seq_len && j < sseq_len; ++j) {
                if (Seq[j + i] != Subseq[j]) {
                    is_subseq = false;
                    break;
                }
            }
        }
    }
    return is_subseq;
}

#define RANDSTRUCT_TEST_SUITE_TEST StructureLayoutRandomizationTestSuiteTest

TEST(RANDSTRUCT_TEST_SUITE_TEST, CanDetermineIfSubsequenceExists)
{
    std::vector<std::string> S0 = {"a", "b", "c", "d"};
    ASSERT_TRUE(IsSubsequence(S0, {"b", "c"}));
    ASSERT_TRUE(IsSubsequence(S0, {"a", "b", "c", "d"}));
    ASSERT_TRUE(IsSubsequence(S0, {"b", "c", "d"}));
    ASSERT_TRUE(IsSubsequence(S0, {"a"}));
    ASSERT_FALSE(IsSubsequence(S0, {"a", "d"}));
}

#define RANDSTRUCT_TEST StructureLayoutRandomization

TEST(RANDSTRUCT_TEST, UnmarkedStructuresAreNotRandomized)
{
    std::string Code =
        R"(
        struct dont_randomize_me {
            int potato;
            float tomato;
            long cabbage;
        };
        )";

    auto AST = MakeAST(Code, Lang_C);
    auto RD = GetRecordDeclFromAST(AST->getASTContext(), "dont_randomize_me");
    const std::vector<std::string> expected = {"potato", "tomato", "cabbage"};
    const std::vector<std::string> actual = GetFieldNamesFromRecord(RD);

    ASSERT_EQ(actual, expected);
}

TEST(RANDSTRUCT_TEST, StructuresCanBeMarkedWithRandomizeLayoutAttr)
{
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

    auto AST = MakeAST(Code, Lang_C);
    auto RD0 = GetRecordDeclFromAST(AST->getASTContext(), "marked");
    auto RD1 = GetRecordDeclFromAST(AST->getASTContext(), "not_marked");

    ASSERT_TRUE(RD0->getAttr<RandomizeLayoutAttr>());
    ASSERT_FALSE(RD1->getAttr<RandomizeLayoutAttr>());
}

TEST(RANDSTRUCT_TEST, StructuresCanBeMarkedWithNoRandomizeLayoutAttr)
{
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

    auto AST = MakeAST(Code, Lang_C);
    auto RD0 = GetRecordDeclFromAST(AST->getASTContext(), "marked");
    auto RD1 = GetRecordDeclFromAST(AST->getASTContext(), "not_marked");

    ASSERT_TRUE(RD0->getAttr<NoRandomizeLayoutAttr>());
    ASSERT_FALSE(RD1->getAttr<NoRandomizeLayoutAttr>());
}

TEST(RANDSTRUCT_TEST, StructuresWithRandomizeLayoutAttrHaveFieldsRandomized)
{
    std::string Code =
        R"(
        struct test_struct {
            int a;
            int b;
            int c;
            int d;
            int e;
            int f;
        } __attribute__((randomize_layout));
        )";

    auto AST = MakeAST(Code, Lang_C);
    auto RD = GetRecordDeclFromAST(AST->getASTContext(), "test_struct");
    RandomizeStructureLayout(AST->getASTContext(), RD);
    std::vector<std::string> before = {"a", "b", "c", "d", "e", "f"};
    std::vector<std::string> after = GetFieldNamesFromRecord(RD);

    // FIXME: Could this be a brittle test? Planning on having a separate unit
    // test for reproducible randomizations with seed.
    ASSERT_NE(before, after);
}

TEST(RANDSTRUCT_TEST, StructuresMarkedWithNoRandomizeLayoutShouldBeRejectedAndUnchanged)
{
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

    auto AST = MakeAST(Code, Lang_C);
    auto RD = GetRecordDeclFromAST(AST->getASTContext(), "test_struct");

    ASSERT_FALSE(randstruct::ShouldRandomize(AST->getASTContext(), RD));

    std::vector<std::string> expected = {"a", "b", "c", "d", "e", "f"};
    std::vector<std::string> actual = GetFieldNamesFromRecord(RD);
    // FIXME: Is it messy to call getASTRecordLayout? Thinking that ShouldRandomize() and
    // RandomizeStructureLayout() are the functions under test but this tests proper decision
    // making on ShouldRandomize()'s part and also verifies Randstruct doesn't do anything to it.
    static_cast<void>(AST->getASTContext().getASTRecordLayout(RD));
    ASSERT_EQ(actual, expected);
}

// FIXME: Clang trips an assertion in the DiagnosticsEngine when the warning is emitted
// while running under the test suite: clang/lib/Frontend/TextDiagnosticPrinter.cpp:150:
// virtual void clang::TextDiagnosticPrinter::HandleDiagnostic(clang::DiagnosticsEngine::Level, const clang::Diagnostic&):
// Assertion `TextDiag && "Unexpected diagnostic outside source file processing"' failed.
//
// Although the test *technically* is marked as pass; outside of the test suite this functionality
// works and no assertion is tripped.
TEST(RANDSTRUCT_TEST, DISABLED_EmitWarningWhenStructureIsMarkedWithBothRandomizeAndNoRandomizeAttributes)
{
    std::string Code =
        R"(
        struct test_struct {
            int a;
            int b;
            int c;
        } __attribute__((no_randomize_layout)) __attribute__((randomize_layout));
        )";

    auto AST = MakeAST(Code, Lang_C);
    ASSERT_EQ(AST->getASTContext().getDiagnostics().getNumWarnings(), 1UL);
}

// FIXME: Clang trips an assertion in the DiagnosticsEngine when the warning is emitted
// while running under the test suite: clang/lib/Frontend/TextDiagnosticPrinter.cpp:150:
// virtual void clang::TextDiagnosticPrinter::HandleDiagnostic(clang::DiagnosticsEngine::Level, const clang::Diagnostic&):
// Assertion `TextDiag && "Unexpected diagnostic outside source file processing"' failed.
//
// Although the test *technically* is marked as pass; outside of the test suite this functionality
// works and no assertion is tripped.
TEST(RANDSTRUCT_TEST, DISABLED_StructureMarkedWithBothAttributesRemainsUnchanged)
{
    std::string Code =
        R"(
        struct test_struct {
            int a;
            int b;
            int c;
        } __attribute__((no_randomize_layout)) __attribute__((randomize_layout));
        )";

    auto AST = MakeAST(Code, Lang_C);
    auto RD = GetRecordDeclFromAST(AST->getASTContext(), "test_struct");
    static_cast<void>(AST->getASTContext().getASTRecordLayout(RD));

    std::vector<std::string> expected = {"a", "b", "c"};
    std::vector<std::string> actual = GetFieldNamesFromRecord(RD);
    ASSERT_EQ(actual, expected);
}

TEST(RANDSTRUCT_TEST, AdjacentBitfieldsRemainAdjacentAfterRandomization)
{
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

    auto AST = MakeAST(Code, Lang_C);
    auto RD = GetRecordDeclFromAST(AST->getASTContext(), "test_struct");
    static_cast<void>(AST->getASTContext().getASTRecordLayout(RD));

    std::vector<std::string> actual = GetFieldNamesFromRecord(RD);
    std::vector<std::string> subseq = {"x", "y", "z"};
    ASSERT_TRUE(IsSubsequence(actual, subseq));
}

} // ast_matchers
} // clang
