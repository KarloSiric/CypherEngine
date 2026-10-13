//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: examples/CypherRender/CypherRender_CubeNormal.cpp
//  Purpose: Builds the cube sample's padded world-normal matrix.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_CubeNormal.h"

namespace cypher::samples::render
{

bool CubeNormal_TryPack(
    math::affine3_t localToWorld,
    cube_normal_columns_t *columnsOut ) noexcept
{
    if ( columnsOut == nullptr || !math::Affine3_IsFinite( localToWorld ) ) {
        return false;
    }

    math::mat3_t inverse{};
    if ( !math::Mat3_TryInverse(
             math::Affine3_LinearPart( localToWorld ), 0.000001f, &inverse ) ) {
        return false;
    }

    const math::mat3_t normal = math::Mat3_Transpose( inverse );
    cube_normal_columns_t packed{};
    for ( ::cypher::common::u32 column = 0u; column < 3u; ++column ) {
        const math::vec3_t value = math::Mat3_Column( normal, column );
        packed.columns[column][0] = value.x;
        packed.columns[column][1] = value.y;
        packed.columns[column][2] = value.z;
    }
    *columnsOut = packed;
    return true;
}

} // namespace cypher::samples::render
