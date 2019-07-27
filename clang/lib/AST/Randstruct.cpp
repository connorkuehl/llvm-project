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

#include <algorithm>
#include <random>
#include <string>

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/Attr.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/SmallVector.h"

#include "clang/AST/Randstruct.h"

namespace clang {

namespace randstruct {

// FIXME: Replace this with some discovery once that mechanism exists.
const auto CACHE_LINE = 64;

class Bucket {
public:
    std::vector<FieldDecl*>& fields() { return Fields; }
    void addField(FieldDecl* field, int size);
    virtual bool canFit(int size) const { return empty() || Size + size <= CACHE_LINE; }
    virtual bool isBitfieldRun() const { return false; }
    bool full() const { return !empty(); }
    bool empty() const { return Fields.size(); }
protected:
    std::vector<FieldDecl*> Fields;
    int Size = 0;
};

class BitfieldRun : public Bucket {
public:
    bool canFit(int size) const override { return true; }
    bool isBitfieldRun() const override { return true; }
};

void Bucket::addField(FieldDecl* field, int size)
{
    Size += size;
    Fields.push_back(field);
}

}

class Randstruct {
public:
    void Randomize(const ASTContext& C, SmallVectorImpl<FieldDecl*>& fields) const;
    void Commit(const RecordDecl* RD, SmallVectorImpl<Decl*>& NewDeclOrder) const;
};

void Randstruct::Randomize(const ASTContext& C, SmallVectorImpl<FieldDecl*>& fields) const
{
    // FIXME: Replace std::vector with LLVM ADT
    using namespace randstruct;
    // All of the buckets produced by best-effort cache-line algorithm.
    std::vector<std::unique_ptr<Bucket>> buckets;

    //The current bucket of fields that we are trying to fill to a cache-line.
    std::unique_ptr<Bucket> currentBucket = nullptr;
    //The current bucket containing the run of adjacent  bitfields to ensure
    //they remain adjacent.
    std::unique_ptr<Bucket> currentBitfieldRun = nullptr;

    //Tracks the number of fields that we failed to fit to the current bucket,
    // and thus still need to be added later.
    size_t skipped = 0;

    while (!fields.empty()) {
        // If we've skipped more fields than we have remaining to place,
        // that means that they can't fit in our current bucket, and we
        // need to start a new one.
        if (skipped >= fields.size()) {
            skipped = 0;
            buckets.push_back(std::move(currentBucket));
        }

        // Take the first field that needs to be put in a bucket.
        auto field = fields.begin();
        auto *f = llvm::cast<FieldDecl>(*field);

        if (f->isBitField() && !f->isZeroLengthBitField(C)) {
            // Start a bitfield run if this is the first bitfield
            // we have found.
            if (!currentBitfieldRun) {
                currentBitfieldRun = llvm::make_unique<BitfieldRun>();
            }

            // We've placed the field, and can remove it from the
            // "awaiting buckets" vector called "fields"
            currentBitfieldRun->addField(f, 1);
            fields.erase(field);
        } else {
            // Else, current field is not a bitfield
            // If we were previously in a bitfield run, end it.
            if (currentBitfieldRun) {
                buckets.push_back(std::move(currentBitfieldRun));
            }
            // If we don't have a bucket, make one.
            if (!currentBucket) {
                currentBucket = llvm::make_unique<Bucket>();
            }

            auto width = C.getTypeInfo(f->getType()).Width;

            // If we can fit, add it.
            if (currentBucket->canFit(width)) {
                currentBucket->addField(f, width);
                fields.erase(field);

                // If it's now full, tie off the bucket.
                if (currentBucket->full()) {
                    skipped = 0;
                    buckets.push_back(std::move(currentBucket));
                }
            } else {
                // We can't fit it in our current bucket.
                // Move to the end for processing later.
                ++skipped;  // Mark it skipped.
                    fields.push_back(f);
                fields.erase(field);
            }
        }
    }

    // Done processing the fields awaiting a bucket.

    //   // If we were filling a bucket, tie it off.
    if (currentBucket) {
        buckets.push_back(std::move(currentBucket));
    }

    // If we were processing a bitfield run bucket, tie it off.
    if (currentBitfieldRun) {
        buckets.push_back(std::move(currentBitfieldRun));
    }

    std::mt19937 rng;
    std::shuffle(std::begin(buckets), std::end(buckets), rng);

    // Produce the new ordering of the elements from our buckets.
    SmallVector<FieldDecl*, 16> finalOrder;
    for (auto &bucket : buckets) {
        auto randomized = bucket->fields();
        if (!bucket->isBitfieldRun()) {
            std::shuffle(std::begin(randomized), std::end(randomized), rng);
        }
        finalOrder.insert(finalOrder.end(), randomized.begin(), randomized.end());
    }

    fields = finalOrder;
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
    FieldDecl* vla = nullptr;

    for (auto decl : RD->decls()) {
        if (isa<FieldDecl>(decl)) {
            auto field = cast<FieldDecl>(decl);
            if (field->getType()->isIncompleteArrayType()) {
                vla = field;
            } else {
                fields.push_back(field);
            }
        } else {
            others.push_back(decl);
        }
    }

    Randstruct randstruct;

    randstruct.Randomize(C, fields);

    SmallVector<Decl*, SMALL_VEC_SZ> new_order = others;
    new_order.insert(new_order.end(), fields.begin(), fields.end());
    if (vla) {
        new_order.push_back(vla);
    }

    randstruct.Commit(RD, new_order);
}

} // namespace randstruct
} // namespace clang
