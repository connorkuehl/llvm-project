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

// This RUN_ALL_RANDSTRUCT_TESTS conditional compilation can go away once development
// is over.
#ifdef RUN_ALL_RANDSTRUCT_TESTS

/*
  * Structures marked for randomization are randomized
  * Structures marked for NO RANDOMIZATION remain the same
  * Structures marked for both randomization and NO randomization remain the same
    and a warning should be emitted.
  * Alignment attribute
  * Packed attribute
  * No unique address attribute
  * Bit-fields
  * Zero-width bit-field
  * Zero or unsized array at end of struct
  * C++ inheritance with vtables
  * C++ virtual inheritance
  * Anonymous unions (and the anonymous struct extension)
  * Types with common initial sequence: http://eel.is/c++draft/class.mem#22
*/

#endif

} // ast_matchers
} // clang
