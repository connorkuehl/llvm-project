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

static bool FieldsInOrder(const RecordDecl* RD, std::vector<std::string>& expected)
{
    auto Field = expected.begin();
    for (auto f : RD->fields()) {
        if (*Field == f->getNameAsString())
            ++Field;
    }

    /* If the iterator has not advanced to the end then that means the expected
     * order has not been satisfied, e.g:
     *
     *   Actual fields: a, c, b
     *        Expected: a, b, c
     *     1. a == a, iterator advances to b now
     *     2. c != b, iterator unchanged
     *     3. b == b, iterator advances to c now
     *     4. Done iterating, iterator is pointing to c, not the end, this means
     *        the expected order of the structure fields does not match the actual
     *        order.
     */
    return Field == expected.end();
}

TEST(StructureLayoutRandomization, UnmarkedStructuresAreNotRandomized)
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
    std::vector<std::string> expected = {"potato", "tomato", "cabbage"};

    ASSERT_TRUE(FieldsInOrder(RD, expected));
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
