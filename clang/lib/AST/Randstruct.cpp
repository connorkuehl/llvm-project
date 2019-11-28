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

#include "clang/AST/Randstruct.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/Attr.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <random>
#include <string>

namespace clang {

namespace randstruct {

// FIXME: Replace this with some discovery once that mechanism exists.
const auto CACHE_LINE = 64;

class Bucket {
public:
  std::vector<FieldDecl *> &fields() { return Fields; }
  void addField(FieldDecl *Field, int FieldSize);
  virtual bool canFit(int FieldSize) const {
    return Size + FieldSize <= CACHE_LINE;
  }
  virtual bool isBitfieldRun() const { return false; }
  bool full() const { return Size >= CACHE_LINE; }

private:
  std::vector<FieldDecl *> Fields;
  int Size = 0;
};

class BitfieldRun : public Bucket {
public:
  bool canFit(int FieldSize) const override { return true; }
  bool isBitfieldRun() const override { return true; }
};

void Bucket::addField(FieldDecl *Field, int FieldSize) {
  Size += FieldSize;
  Fields.push_back(Field);
}

} // namespace randstruct

class Randstruct {
public:
  void randomize(const ASTContext &Context,
                 SmallVectorImpl<FieldDecl *> &OutFields) const;
  void commit(const RecordDecl *RD,
              SmallVectorImpl<Decl *> &NewDeclOrder) const;
};

void Randstruct::randomize(const ASTContext &Context,
                           SmallVectorImpl<FieldDecl *> &FieldsOut) const {
  // FIXME: Replace std::vector with LLVM ADT
  using namespace randstruct;
  // All of the Buckets produced by best-effort cache-line algorithm.
  std::vector<std::unique_ptr<Bucket>> Buckets;

  // The current bucket of fields that we are trying to fill to a cache-line.
  std::unique_ptr<Bucket> CurrentBucket = nullptr;
  // The current bucket containing the run of adjacent  bitfields to ensure
  // they remain adjacent.
  std::unique_ptr<Bucket> CurrentBitfieldRun = nullptr;

  // Tracks the number of fields that we failed to fit to the current bucket,
  // and thus still need to be added later.
  auto Skipped = 0ul;

  while (!FieldsOut.empty()) {
    // If we've Skipped more fields than we have remaining to place,
    // that means that they can't fit in our current bucket, and we
    // need to start a new one.
    if (Skipped >= FieldsOut.size()) {
      Skipped = 0;
      Buckets.push_back(std::move(CurrentBucket));
    }

    // Take the first field that needs to be put in a bucket.
    auto Field = FieldsOut.begin();
    auto *F = llvm::cast<FieldDecl>(*Field);

    if (F->isBitField() && !F->isZeroLengthBitField(Context)) {
      // Start a bitfield run if this is the first bitfield
      // we have found.
      if (!CurrentBitfieldRun) {
        CurrentBitfieldRun = std::make_unique<BitfieldRun>();
      }

      // We've placed the field, and can remove it from the
      // "awaiting Buckets" vector called "Fields"
      CurrentBitfieldRun->addField(F, /*FieldSize is irrelevant here*/ 1);
      FieldsOut.erase(Field);
    } else {
      // Else, current field is not a bitfield
      // If we were previously in a bitfield run, end it.
      if (CurrentBitfieldRun) {
        Buckets.push_back(std::move(CurrentBitfieldRun));
      }
      // If we don't have a bucket, make one.
      if (!CurrentBucket) {
        CurrentBucket = std::make_unique<Bucket>();
      }

      auto Width = Context.getTypeInfo(F->getType()).Width;
      if (Width >= CACHE_LINE) {
          std::unique_ptr<Bucket> OverSized = std::make_unique<Bucket>();
          OverSized->addField(F, Width);
          FieldsOut.erase(Field);
          Buckets.push_back(std::move(OverSized));
          continue;
      }

      // If we can fit, add it.
      if (CurrentBucket->canFit(Width)) {
        CurrentBucket->addField(F, Width);
        FieldsOut.erase(Field);

        // If it's now full, tie off the bucket.
        if (CurrentBucket->full()) {
          Skipped = 0;
          Buckets.push_back(std::move(CurrentBucket));
        }
      } else {
        // We can't fit it in our current bucket.
        // Move to the end for processing later.
        ++Skipped; // Mark it skipped.
        FieldsOut.push_back(F);
        FieldsOut.erase(Field);
      }
    }
  }

  // Done processing the fields awaiting a bucket.

  // If we were filling a bucket, tie it off.
  if (CurrentBucket) {
    Buckets.push_back(std::move(CurrentBucket));
  }

  // If we were processing a bitfield run bucket, tie it off.
  if (CurrentBitfieldRun) {
    Buckets.push_back(std::move(CurrentBitfieldRun));
  }

  std::mt19937 RNG;
  std::shuffle(std::begin(Buckets), std::end(Buckets), RNG);

  // Produce the new ordering of the elements from our Buckets.
  SmallVector<FieldDecl *, 16> FinalOrder;
  for (auto &Bucket : Buckets) {
    auto &Randomized = Bucket->fields();
    if (!Bucket->isBitfieldRun()) {
      std::shuffle(std::begin(Randomized), std::end(Randomized), RNG);
    }
    FinalOrder.insert(FinalOrder.end(), Randomized.begin(), Randomized.end());
  }

  FieldsOut = FinalOrder;
}

void Randstruct::commit(const RecordDecl *RD,
                        SmallVectorImpl<Decl *> &NewDeclOrder) const {
  Decl *First = nullptr;
  Decl *Last = nullptr;
  std::tie(First, Last) = DeclContext::BuildDeclChain(NewDeclOrder, false);
  RD->FirstDecl = First;
  RD->LastDecl = Last;
}

namespace randstruct {

bool shouldRandomize(const ASTContext &Context, const RecordDecl *RD) {
  if (RD->isUnion()) {
      return false;
  }

  const auto HasRandAttr = RD->getAttr<RandomizeLayoutAttr>() != nullptr;
  const auto HasNoRandAttr = RD->getAttr<NoRandomizeLayoutAttr>() != nullptr;
  if (HasRandAttr && HasNoRandAttr) {
    Context.getDiagnostics().Report(RD->getLocation(),
                                    diag::warn_randomize_attr_conflict);
  }

  return !HasNoRandAttr && HasRandAttr;
}

void randomizeStructureLayout(const ASTContext &Context, const RecordDecl *RD) {
  const auto SMALL_VEC_SIZE = 16ul;
  SmallVector<Decl *, SMALL_VEC_SIZE> Others;
  SmallVector<FieldDecl *, SMALL_VEC_SIZE> Fields;
  FieldDecl *VLA = nullptr;

  for (auto *Decl : RD->decls()) {
    if (isa<FieldDecl>(Decl)) {
      auto *Field = cast<FieldDecl>(Decl);
      if (Field->getType()->isIncompleteArrayType()) {
        VLA = Field;
      } else {
        Fields.push_back(Field);
      }
    } else {
      Others.push_back(Decl);
    }
  }

  Randstruct Rand;
  Rand.randomize(Context, Fields);

  SmallVector<Decl *, SMALL_VEC_SIZE> NewOrder = Others;
  NewOrder.insert(NewOrder.end(), Fields.begin(), Fields.end());
  if (VLA) {
    NewOrder.push_back(VLA);
  }

  Rand.commit(RD, NewOrder);
}

} // namespace randstruct
} // namespace clang
