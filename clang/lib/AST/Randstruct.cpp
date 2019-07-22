//===- lib/AST/Randstruct.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation for Clang's structure field layout
// randomization.
//
//===----------------------------------------------------------------------===//

#include <string>

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/Attr.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/SmallVector.h"

#include "clang/AST/Randstruct.h"

namespace clang {

class Randstruct {
public:
    void Randomize(const ASTContext& C, SmallVectorImpl<FieldDecl*>& fields) const;
    void Commit(const RecordDecl* RD, SmallVectorImpl<Decl*>& NewDeclOrder) const;
};

void Randstruct::Randomize(const ASTContext& C, SmallVectorImpl<FieldDecl*>& fields) const
{
    // FIXME: Implement actual randomization; these are just being reversed for testing
    // purposes.
    SmallVector<FieldDecl*, 16UL> new_order(fields.rbegin(), fields.rend());
    fields = new_order;
}

void Randstruct::Commit(const RecordDecl* RD, SmallVectorImpl<Decl*>& NewDeclOrder) const
{
    Decl* First = nullptr;
    Decl* Last = nullptr;
    std::tie(First, Last) = DeclContext::BuildDeclChain(NewDeclOrder, false);
    RD->FirstDecl = First;
    RD->LastDecl = Last;
}

namespace randstruct {

bool ShouldRandomize(const ASTContext& C, const RecordDecl* RD)
{
    auto has_rand_attr = RD->getAttr<RandomizeLayoutAttr>();
    auto has_norand_attr = RD->getAttr<NoRandomizeLayoutAttr>();
    if (has_rand_attr && has_norand_attr) {
        C.getDiagnostics().Report(RD->getLocation(), diag::warn_randomize_attr_conflict);
    }

    return !has_norand_attr && has_rand_attr;
}

void RandomizeStructureLayout(const ASTContext& C, const RecordDecl* RD)
{
    const auto SMALL_VEC_SZ = 16UL;
    SmallVector<Decl*, SMALL_VEC_SZ> others;
    SmallVector<FieldDecl*, SMALL_VEC_SZ> fields;
    for (auto decl : RD->decls()) {
        if (isa<FieldDecl>(decl)) {
            fields.push_back(cast<FieldDecl>(decl));
        } else {
            others.push_back(decl);
        }
    }

    Randstruct randstruct;

    randstruct.Randomize(C, fields);

    SmallVector<Decl*, SMALL_VEC_SZ> new_order = others;
    for (auto f : fields) {
        new_order.push_back(cast<FieldDecl>(f));
    }

    randstruct.Commit(RD, new_order);
}

} // namespace randstruct
} // namespace clang
