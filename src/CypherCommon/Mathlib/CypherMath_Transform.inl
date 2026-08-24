//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Transform.inl
//  Purpose: Implements constexpr transform construction.
//  Details: Runtime operations remain out of line because they normalize,
//           validate, or invoke quaternion and matrix algorithms.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Transform Template Definitions

Operations follow CypherMath coordinate, storage, and multiplication conventions. Inputs may
alias only where documented, and normalization handles degenerate values explicitly. Template
definitions remain visible at the call site so each concrete instantiation can be compiled
without a separate registration step.
================
*/

#ifndef CYPHER_COMMON_MATH_TRANSFORM_INL
#define CYPHER_COMMON_MATH_TRANSFORM_INL

#ifndef CYPHER_COMMON_MATH_TRANSFORM_H
    #include "CypherMath_Transform.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::math
{

// This constructor preserves caller values verbatim; validation is a separate query.
constexpr transform_t Transform_Make(
    vec3_t position,
    quat_t rotation,
    vec3_t scale ) noexcept
{
    return { position, rotation, scale };
}

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_TRANSFORM_INL
