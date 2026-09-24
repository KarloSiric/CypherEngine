//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushTransform.cpp
//  Purpose: Implements brush transforms and side drags.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushTransform.h"

#include "CypherGeometry_PreviewPlan.h"

#include <cmath>

namespace cypher::editor::geometry
{

namespace
{

using common::usize;
using math::affine3d_t;
using math::f64;
using math::vec3d_t;

// T(pivot) * L * T(-pivot) for a linear part given by columns.
affine3d_t AboutPivot( vec3d_t c0, vec3d_t c1, vec3d_t c2, vec3d_t pivot ) noexcept
{
    const affine3d_t linear = math::Affine3d_FromColumns( c0, c1, c2, math::CY_VEC3D_ZERO );
    const vec3d_t moved = math::Affine3d_TransformDirection( linear, pivot );
    return math::Affine3d_FromColumns( c0, c1, c2, math::Vec3d_Subtract( pivot, moved ) );
}

bool TryUnit( vec3d_t v, vec3d_t *pOut ) noexcept
{
    return math::Vec3d_IsFinite( v ) && math::Vec3d_TryNormalize( v, 1.0e-12, pOut, nullptr );
}

// Builds the transformed value of one brush.
geometry_status_t TransformValue(
    const geometry_document_t *pDocument,
    const geometry_brush_value_t *pSource,
    const affine3d_t &transform,
    geometry_texture_lock_t lock,
    const geometry_brush_value_t **ppValueOut ) noexcept
{
    const geometry_policy_t &policy = pDocument->policy;
    const common::allocator_t *pAllocator = pDocument->pAllocator;
    brush_solid_t brush{};
    geometry_brush_side_attribute_store_t attributes{};
    geometry_status_t status = BrushSolid_Init( &brush, pAllocator, pSource->brush.sourceId );
    if ( status == geometry_status_t::OK ) {
        status = BrushSolid_TryCopyFrom( &brush, &pSource->brush, policy.limits );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushSideAttributeStore_Init( &attributes, pAllocator );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushSideAttributeStore_TryCopyFrom( &attributes, &pSource->attributes, policy.limits );
    }
    const usize cSides = BrushSolid_SideCount( &brush );
    for ( usize i = 0u; status == geometry_status_t::OK && i < cSides; ++i ) {
        brush_solid_side_t &side = brush.sides.pData[i];
        math::planed_t moved{};
        if ( !math::Planed_TryTransform( side.plane, transform,
                                         policy.numerical.fAbsoluteDistanceTolerance,
                                         1.0e-12, &moved ) ) {
            status = geometry_status_t::DEGENERATE;
            break;
        }
        geometry_brush_side_attributes_t record = attributes.records.pData[side.iAttributeIndex];
        status = AttributePropagation_TryTransformProjection(
            policy.numerical, record.uvProjection, transform, lock, moved.normal,
            &record.uvProjection );
        if ( status == geometry_status_t::OK ) {
            status = BrushSideAttributeStore_TrySet(
                &attributes, policy.numerical, side.iAttributeIndex, record );
        }
        side.plane = moved;
    }
    if ( status == geometry_status_t::OK ) {
        status = GeometryDocument_TryCreateBrushValue( pDocument, &brush, &attributes, ppValueOut,
                                                       nullptr );
    }
    BrushSideAttributeStore_Shutdown( &attributes );
    BrushSolid_Shutdown( &brush );
    return status;
}

// Rebuilds one brush with a single side's plane (and projection) replaced.
geometry_status_t ReplaceSide(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    geometry_source_id_t sideId,
    math::planed_t plane,
    const math::planar_uv_mappingd_t *pProjection ) noexcept
{
    geometry_document_t *pDocument = pTransaction->pDocument;
    const geometry_policy_t &policy = pDocument->policy;
    const geometry_brush_value_t *pSource = nullptr;
    if ( GeometryTransaction_TryGetPreview( pTransaction, brushId, &pSource ) !=
         geometry_status_t::OK ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    usize iSide = common::CY_USIZE_MAX;
    for ( usize i = 0u; i < BrushSolid_SideCount( &pSource->brush ); ++i ) {
        if ( pSource->brush.sides.pData[i].sourceId.value == sideId.value ) {
            iSide = i;
            break;
        }
    }
    if ( iSide == common::CY_USIZE_MAX ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    brush_solid_t brush{};
    geometry_brush_side_attribute_store_t attributes{};
    geometry_status_t status = BrushSolid_Init( &brush, pDocument->pAllocator, brushId );
    if ( status == geometry_status_t::OK ) {
        status = BrushSolid_TryCopyFrom( &brush, &pSource->brush, policy.limits );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushSolid_TrySetSidePlane( &brush, iSide, plane );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushSideAttributeStore_Init( &attributes, pDocument->pAllocator );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushSideAttributeStore_TryCopyFrom( &attributes, &pSource->attributes, policy.limits );
    }
    if ( status == geometry_status_t::OK && pProjection != nullptr ) {
        const common::u32 iRecord = brush.sides.pData[iSide].iAttributeIndex;
        geometry_brush_side_attributes_t record = attributes.records.pData[iRecord];
        record.uvProjection = *pProjection;
        status = BrushSideAttributeStore_TrySet( &attributes, policy.numerical, iRecord, record );
    }
    const geometry_brush_value_t *pValue = nullptr;
    if ( status == geometry_status_t::OK ) {
        status = GeometryDocument_TryCreateBrushValue( pDocument, &brush, &attributes, &pValue,
                                                       nullptr );
    }
    BrushSideAttributeStore_Shutdown( &attributes );
    BrushSolid_Shutdown( &brush );
    if ( status == geometry_status_t::OK ) {
        status = GeometryTransaction_TryPreviewReplace( pTransaction, pValue );
    }
    BrushValue_Release( pValue );
    return status;
}

const brush_solid_side_t *FindSide( const geometry_brush_value_t *pValue,
                                    geometry_source_id_t sideId ) noexcept
{
    for ( usize i = 0u; i < BrushSolid_SideCount( &pValue->brush ); ++i ) {
        if ( pValue->brush.sides.pData[i].sourceId.value == sideId.value ) {
            return &pValue->brush.sides.pData[i];
        }
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

affine3d_t BrushTransform_MakeTranslation( vec3d_t offset ) noexcept
{
    return math::Affine3d_FromTranslation( offset );
}

geometry_status_t BrushTransform_TryMakeRotation(
    vec3d_t pivot, vec3d_t axis, f64 radians, affine3d_t *pTransformOut ) noexcept
{
    if ( pTransformOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pTransformOut = math::CY_AFFINE3D_IDENTITY;
    vec3d_t k{};
    if ( !math::Vec3d_IsFinite( pivot ) || !math::Scalar_IsFinite( radians ) || !TryUnit( axis, &k ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // Rodrigues: R = cI + s[k]x + (1 - c) k k^T, written as columns.
    const f64 c = std::cos( radians );
    const f64 s = std::sin( radians );
    const f64 t = 1.0 - c;
    const vec3d_t c0 = math::Vec3d_Make( c + t * k.x * k.x, t * k.x * k.y + s * k.z, t * k.x * k.z - s * k.y );
    const vec3d_t c1 = math::Vec3d_Make( t * k.x * k.y - s * k.z, c + t * k.y * k.y, t * k.y * k.z + s * k.x );
    const vec3d_t c2 = math::Vec3d_Make( t * k.x * k.z + s * k.y, t * k.y * k.z - s * k.x, c + t * k.z * k.z );
    *pTransformOut = AboutPivot( c0, c1, c2, pivot );
    return geometry_status_t::OK;
}

geometry_status_t BrushTransform_TryMakeScale(
    vec3d_t pivot, vec3d_t factors, affine3d_t *pTransformOut ) noexcept
{
    if ( pTransformOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pTransformOut = math::CY_AFFINE3D_IDENTITY;
    if ( !math::Vec3d_IsFinite( pivot ) || !math::Vec3d_IsFinite( factors ) ||
         factors.x == 0.0 || factors.y == 0.0 || factors.z == 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pTransformOut = AboutPivot( math::Vec3d_Make( factors.x, 0.0, 0.0 ),
                                 math::Vec3d_Make( 0.0, factors.y, 0.0 ),
                                 math::Vec3d_Make( 0.0, 0.0, factors.z ), pivot );
    return geometry_status_t::OK;
}

geometry_status_t BrushTransform_TryMakeReflection(
    vec3d_t pivot, vec3d_t normal, affine3d_t *pTransformOut ) noexcept
{
    if ( pTransformOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pTransformOut = math::CY_AFFINE3D_IDENTITY;
    vec3d_t n{};
    if ( !math::Vec3d_IsFinite( pivot ) || !TryUnit( normal, &n ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // Householder: I - 2 n n^T.
    const vec3d_t c0 = math::Vec3d_Make( 1.0 - 2.0 * n.x * n.x, -2.0 * n.x * n.y, -2.0 * n.x * n.z );
    const vec3d_t c1 = math::Vec3d_Make( -2.0 * n.x * n.y, 1.0 - 2.0 * n.y * n.y, -2.0 * n.y * n.z );
    const vec3d_t c2 = math::Vec3d_Make( -2.0 * n.x * n.z, -2.0 * n.y * n.z, 1.0 - 2.0 * n.z * n.z );
    *pTransformOut = AboutPivot( c0, c1, c2, pivot );
    return geometry_status_t::OK;
}

geometry_status_t BrushTransform_TryMakeShear(
    vec3d_t pivot, vec3d_t shearAxis, vec3d_t sourceAxis, f64 factor,
    affine3d_t *pTransformOut ) noexcept
{
    if ( pTransformOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pTransformOut = math::CY_AFFINE3D_IDENTITY;
    vec3d_t a{};
    vec3d_t b{};
    if ( !math::Vec3d_IsFinite( pivot ) || !math::Scalar_IsFinite( factor ) ||
         !TryUnit( shearAxis, &a ) || !TryUnit( sourceAxis, &b ) ||
         math::Scalar_Abs( math::Vec3d_Dot( a, b ) ) > 1.0e-9 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // L = I + factor * a b^T; column j is e_j + factor * b_j * a.
    const vec3d_t c0 = math::Vec3d_Add( math::Vec3d_Make( 1.0, 0.0, 0.0 ), math::Vec3d_Scale( a, factor * b.x ) );
    const vec3d_t c1 = math::Vec3d_Add( math::Vec3d_Make( 0.0, 1.0, 0.0 ), math::Vec3d_Scale( a, factor * b.y ) );
    const vec3d_t c2 = math::Vec3d_Add( math::Vec3d_Make( 0.0, 0.0, 1.0 ), math::Vec3d_Scale( a, factor * b.z ) );
    *pTransformOut = AboutPivot( c0, c1, c2, pivot );
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

geometry_status_t BrushTransform_TryApply(
    geometry_transaction_t *pTransaction,
    common::span_t<const geometry_source_id_t> brushIds,
    const affine3d_t &transform,
    geometry_texture_lock_t lock ) noexcept
{
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Span_IsValid( brushIds ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Affine3d_IsFinite( transform ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    const geometry_document_t *pDocument = pTransaction->pDocument;
    common::vector_t<const geometry_brush_value_t *> pinned{};
    if ( !common::Vector_Init( &pinned, pDocument->pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t status = PreviewPlan_TryPin( pTransaction, brushIds, &pinned );
    geometry_preview_plan_t plan{};
    if ( status == geometry_status_t::OK &&
         PreviewPlan_Init( &plan, pDocument->pAllocator ) != geometry_status_t::OK ) {
        status = geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; status == geometry_status_t::OK && i < brushIds.nCount; ++i ) {
        const geometry_brush_value_t *pValue = nullptr;
        status = TransformValue( pDocument, pinned.pData[i], transform, lock, &pValue );
        if ( status == geometry_status_t::OK ) {
            status = PreviewPlan_TryAddValue( &plan, geometry_preview_step_kind_t::REPLACE, pValue );
            if ( status != geometry_status_t::OK ) {
                BrushValue_Release( pValue );
            }
        }
    }
    if ( status == geometry_status_t::OK ) {
        status = PreviewPlan_TryApply( &plan, pTransaction );
    }
    PreviewPlan_ReleasePinned( &pinned );
    return status;
}

geometry_status_t BrushTransform_TryMoveSide(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    geometry_source_id_t sideId,
    f64 distance,
    geometry_texture_lock_t lock ) noexcept
{
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Scalar_IsFinite( distance ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const geometry_brush_value_t *pSource = nullptr;
    if ( GeometryTransaction_TryGetPreview( pTransaction, brushId, &pSource ) !=
         geometry_status_t::OK ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    const brush_solid_side_t *pSide = FindSide( pSource, sideId );
    if ( pSide == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    math::planed_t plane = pSide->plane;
    plane.d -= distance;
    if ( lock == geometry_texture_lock_t::WORLD_LOCKED ) {
        return ReplaceSide( pTransaction, brushId, sideId, plane, nullptr );
    }
    math::planar_uv_mappingd_t projection =
        pSource->attributes.records.pData[pSide->iAttributeIndex].uvProjection;
    projection.origin =
        math::Vec3d_Add( projection.origin, math::Vec3d_Scale( pSide->plane.normal, distance ) );
    return ReplaceSide( pTransaction, brushId, sideId, plane, &projection );
}

geometry_status_t BrushTransform_TrySetSidePlane(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    geometry_source_id_t sideId,
    math::planed_t plane,
    geometry_texture_lock_t lock ) noexcept
{
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const geometry_brush_value_t *pSource = nullptr;
    if ( GeometryTransaction_TryGetPreview( pTransaction, brushId, &pSource ) !=
         geometry_status_t::OK ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    const brush_solid_side_t *pSide = FindSide( pSource, sideId );
    if ( pSide == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Planed_IsFinite( plane ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    const geometry_numerical_policy_t &numerical = pTransaction->pDocument->policy.numerical;
    const math::planar_uv_mappingd_t &source =
        pSource->attributes.records.pData[pSide->iAttributeIndex].uvProjection;
    math::planar_uv_mappingd_t projection{};
    geometry_status_t status = geometry_status_t::OK;
    if ( lock == geometry_texture_lock_t::GEOMETRY_LOCKED ) {
        // Rotate the projection with the face: the minimal rotation taking
        // the old normal to the new one, about the old plane's closest
        // point to the projection origin.
        const vec3d_t from = pSide->plane.normal;
        const vec3d_t to = plane.normal;
        const vec3d_t axis = math::Vec3d_Cross( from, to );
        const f64 sine = std::sqrt( math::Vec3d_LengthSquared( axis ) );
        const f64 cosine = math::Vec3d_Dot( from, to );
        affine3d_t rotation = math::CY_AFFINE3D_IDENTITY;
        if ( sine > 1.0e-12 ) {
            const vec3d_t pivot = math::Vec3d_Subtract(
                source.origin,
                math::Vec3d_Scale( from, math::Planed_SignedDistance( pSide->plane, source.origin ) ) );
            status = BrushTransform_TryMakeRotation( pivot, axis, std::atan2( sine, cosine ), &rotation );
        }
        if ( status == geometry_status_t::OK ) {
            status = AttributePropagation_TryTransformProjection(
                numerical, source, rotation, lock, plane.normal, &projection );
        }
    } else {
        status = AttributePropagation_TryRebaseProjection( numerical, source, plane.normal, &projection );
    }
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    return ReplaceSide( pTransaction, brushId, sideId, plane, &projection );
}

} // namespace cypher::editor::geometry
