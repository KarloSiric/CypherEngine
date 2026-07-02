//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMath/CypherMath_Plane.h
//  Purpose: Declares the CypherMath Math Plane module.
//  Details: This file participates in math primitives used by rendering, physics,
//           world queries, and tools. Keep operations deterministic and benchmark
//           important hot paths before adding clever optimizations.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_MATH_PLANE_H
#define CYPHER_ENGINE_MATH_PLANE_H

#pragma once

#include "CypherMath_Types.h"

namespace cypher::engine::math
{

plane_t CypherMath_PlaneFromPointNormal( const vec3_t &point, const vec3_t &normal );

plane_t CypherMath_PlaneFromPoints( const vec3_t &p0, const vec3_t &p1, const vec3_t &p2 );

common::f32 CypherMath_PlaneDistance( const plane_t &plane, const vec3_t &v );

bool CypherMath_PlanePointFront( const plane_t &plane, const vec3_t &v );

bool CypherMath_PlanePointBack( const plane_t &plane, const vec3_t &v );

bool CypherMath_PlanePointOn( const plane_t &plane, const vec3_t &v, common::f32 epsilon );

}       // namespace cypher::engine::math

#endif // CYPHER_ENGINE_MATH_PLANE_H
