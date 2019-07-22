//===- include/AST/Randstruct.h --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the interface for Clang's structure field layout
// randomization.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_INCLUDE_AST_RANDSTRUCT_H_
#define CLANG_INCLUDE_AST_RANDSTRUCT_H_

#include "Decl.h"

namespace clang {
namespace randstruct {

bool ShouldRandomize(const RecordDecl* RD);
void RandomizeStructureLayout(const ASTContext& C, const RecordDecl* RD);

} // namespace randstruct
} // namespace clang

#endif // CLANG_INCLUDE_AST_RANDSTRUCT_H_
