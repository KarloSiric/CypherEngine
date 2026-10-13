//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: examples/CypherRender/CypherRender_CubeNormal.h
//  Purpose: Packs one cube sample normal transform for a std140 uniform block.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_RENDER_CUBE_NORMAL_H
#define CYPHER_RENDER_CUBE_NORMAL_H

#include "CypherCommon/Mathlib/CypherMath_Affine3.h"

namespace cypher::samples::render
{

// std140 mat3 uses three 16-byte columns; the fourth float in each is padding.
struct alignas( 16 ) cube_normal_columns_t {
    float columns[3][4]{};
};
static_assert( sizeof( cube_normal_columns_t ) == 48u );
static_assert( alignof( cube_normal_columns_t ) == 16u );

// Computes transpose(inverse(linear(localToWorld))) once per object. Reports
// whether the transform reverses handedness when requested. Rejects singular
// or nonfinite transforms and leaves both caller outputs unchanged.
[[nodiscard]] bool CubeNormal_TryPack(
    math::affine3_t localToWorld,
    cube_normal_columns_t *columnsOut,
    bool *mirroredOut = nullptr ) noexcept;

} // namespace cypher::samples::render

#endif // CYPHER_RENDER_CUBE_NORMAL_H
