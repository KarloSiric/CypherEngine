//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SurfaceCommands.cpp
//  Purpose: Implements side surfacing commands.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_SurfaceCommands.h"

#include "CypherGeometry_PreviewPlan.h"

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::u32;
using common::usize;
using math::f64;
using math::vec3d_t;

bool_t Targets( common::span_t<const geometry_component_ref_t> targets,
                geometry_source_id_t brushId, geometry_source_id_t sideId ) noexcept
{
    for ( usize i = 0u; i < targets.nCount; ++i ) {
        const geometry_component_ref_t &ref = targets.pData[i];
        if ( ref.brushId.value != brushId.value ) {
            continue;
        }
        if ( ref.kind == geometry_component_kind_t::BRUSH ||
             ( ref.kind == geometry_component_kind_t::BRUSH_SIDE && ref.a.value == sideId.value ) ) {
            return true;
        }
    }
    return false;
}

geometry_status_t ApplyToSide(
    const geometry_numerical_policy_t &policy,
    const geometry_brush_value_t *pValue,
    usize iSide,
    const geometry_surface_op_t &op,
    geometry_brush_side_attributes_t *pRecord ) noexcept
{
    const brush_boundary_t &boundary = pValue->boundary;
    const vec3d_t normal = pValue->brush.sides.pData[iSide].plane.normal;
    usize iFace = 0u;
    if ( BrushBoundary_TryFindFaceForSide( &boundary, iSide, &iFace ) != geometry_status_t::OK ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    const brush_boundary_face_t &face = boundary.faces.pData[iFace];

    // Face points (bounded by the face ring size, at most the vertex count).
    vec3d_t centroid = math::CY_VEC3D_ZERO;
    common::vector_t<vec3d_t> points{};
    if ( !common::Vector_Init( &points, pValue->pAllocator, face.cVertices ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 k = 0u; k < face.cVertices; ++k ) {
        const vec3d_t p = boundary.vertices.pData[boundary.faceVertexIndices.pData[face.iFirstIndex + k]];
        ( void )common::Vector_PushBack( &points, p );
        centroid = math::Vec3d_Add( centroid, p );
    }
    centroid = math::Vec3d_Scale( centroid, 1.0 / static_cast<f64>( face.cVertices ) );
    const vec3d_t pivot = op.bUsePivot ? op.pivot : centroid;
    math::planar_uv_mappingd_t &uv = pRecord->uvProjection;

    switch ( op.kind ) {
        case geometry_surface_op_kind_t::SET_MATERIAL:
            pRecord->material = op.material;
            return geometry_status_t::OK;
        case geometry_surface_op_kind_t::SET_PROJECTION:
            return AttributePropagation_TryRebaseProjection( policy, op.projection, normal, &uv );
        case geometry_surface_op_kind_t::SHIFT:
            return SurfaceUv_TryShift( policy, uv, op.vector, &uv );
        case geometry_surface_op_kind_t::ROTATE:
            return SurfaceUv_TryRotate( policy, uv, op.radians, pivot, &uv );
        case geometry_surface_op_kind_t::SCALE:
            return SurfaceUv_TryScale( policy, uv, op.vector, pivot, &uv );
        case geometry_surface_op_kind_t::FLIP:
            return SurfaceUv_TryFlip( policy, uv, op.bFlipU, op.bFlipV, pivot, &uv );
        case geometry_surface_op_kind_t::FIT:
            return SurfaceUv_TryFit( policy, uv, { points.pData, face.cVertices }, op.vector.x,
                                     op.vector.y, &uv );
        case geometry_surface_op_kind_t::ALIGN_LONGEST_EDGE: {
            u32 iBest = 0u;
            f64 bestSq = -1.0;
            for ( u32 k = 0u; k < face.cVertices; ++k ) {
                const f64 lengthSq = math::Vec3d_DistanceSquared(
                    points.pData[k], points.pData[( k + 1u ) % face.cVertices] );
                if ( lengthSq > bestSq ) {
                    bestSq = lengthSq;
                    iBest = k;
                }
            }
            return SurfaceUv_TryAlignToEdge( policy, uv, points.pData[iBest],
                                             points.pData[( iBest + 1u ) % face.cVertices], normal, &uv );
        }
        case geometry_surface_op_kind_t::RESET:
            return SurfaceUv_TryReset( policy, normal, &uv );
        default:
            return geometry_status_t::INVALID_ARGUMENT;
    }
}

} // namespace

geometry_status_t SurfaceCommand_TryApply(
    geometry_transaction_t *pTransaction,
    common::span_t<const geometry_component_ref_t> targets,
    const geometry_surface_op_t &op,
    geometry_surface_result_t *pResultOut ) noexcept
{
    if ( pResultOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pResultOut = {};
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Span_IsValid( targets ) || targets.nCount == 0u ||
         static_cast<u32>( op.kind ) >= static_cast<u32>( geometry_surface_op_kind_t::COUNT ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_document_t *pDocument = pTransaction->pDocument;
    const geometry_policy_t &policy = pDocument->policy;
    const common::allocator_t *pAllocator = pDocument->pAllocator;

    // Distinct target brushes, in first-seen order.
    common::vector_t<geometry_source_id_t> brushes{};
    if ( !common::Vector_Init( &brushes, pAllocator, targets.nCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < targets.nCount; ++i ) {
        const geometry_component_ref_t &ref = targets.pData[i];
        if ( ref.kind != geometry_component_kind_t::BRUSH &&
             ref.kind != geometry_component_kind_t::BRUSH_SIDE ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        bool_t bKnown = false;
        for ( usize b = 0u; b < common::Vector_Count( &brushes ) && !bKnown; ++b ) {
            bKnown = brushes.pData[b].value == ref.brushId.value;
        }
        if ( !bKnown ) {
            ( void )common::Vector_PushBack( &brushes, ref.brushId );
        }
    }

    common::vector_t<const geometry_brush_value_t *> pinned{};
    if ( !common::Vector_Init( &pinned, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t status = PreviewPlan_TryPin(
        pTransaction, { brushes.pData, common::Vector_Count( &brushes ) }, &pinned );
    geometry_preview_plan_t plan{};
    if ( status == geometry_status_t::OK && PreviewPlan_Init( &plan, pAllocator ) != geometry_status_t::OK ) {
        status = geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_surface_result_t result{};
    for ( usize b = 0u; status == geometry_status_t::OK && b < common::Vector_Count( &brushes ); ++b ) {
        const geometry_brush_value_t *pSource = pinned.pData[b];
        geometry_brush_side_attribute_store_t attributes{};
        status = BrushSideAttributeStore_Init( &attributes, pAllocator );
        if ( status == geometry_status_t::OK ) {
            status = BrushSideAttributeStore_TryCopyFrom( &attributes, &pSource->attributes, policy.limits );
        }
        u32 cTouched = 0u;
        for ( usize s = 0u; status == geometry_status_t::OK && s < BrushSolid_SideCount( &pSource->brush ); ++s ) {
            const brush_solid_side_t &side = pSource->brush.sides.pData[s];
            if ( !Targets( targets, pSource->brush.sourceId, side.sourceId ) ) {
                continue;
            }
            geometry_brush_side_attributes_t record = attributes.records.pData[side.iAttributeIndex];
            status = ApplyToSide( policy.numerical, pSource, s, op, &record );
            if ( status == geometry_status_t::OK ) {
                status = BrushSideAttributeStore_TrySet( &attributes, policy.numerical,
                                                         side.iAttributeIndex, record );
            }
            ++cTouched;
        }
        const geometry_brush_value_t *pValue = nullptr;
        if ( status == geometry_status_t::OK && cTouched > 0u ) {
            status = GeometryDocument_TryCreateBrushValue( pDocument, &pSource->brush, &attributes,
                                                           &pValue, nullptr );
            if ( status == geometry_status_t::OK ) {
                status = PreviewPlan_TryAddValue( &plan, geometry_preview_step_kind_t::REPLACE, pValue );
                if ( status != geometry_status_t::OK ) {
                    BrushValue_Release( pValue );
                }
            }
            ++result.cBrushes;
            result.cSides += cTouched;
        }
        BrushSideAttributeStore_Shutdown( &attributes );
    }
    if ( status == geometry_status_t::OK ) {
        status = PreviewPlan_TryApply( &plan, pTransaction );
    }
    PreviewPlan_ReleasePinned( &pinned );
    if ( status == geometry_status_t::OK ) {
        *pResultOut = result;
    }
    return status;
}

} // namespace cypher::editor::geometry
