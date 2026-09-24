//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCsgCommands.cpp
//  Purpose: Implements document-level brush CSG commands.
//  Details: Every command pins its operand previews, computes all results,
//           materializes them into a preview plan, and applies the plan
//           atomically. Nothing touches the transaction until every result
//           exists.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushCsgCommands.h"

#include "CypherGeometry_PreviewPlan.h"

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::usize;

struct pinned_t {
    common::vector_t<const geometry_brush_value_t *> values{};
    ~pinned_t() noexcept { PreviewPlan_ReleasePinned( &values ); }
};

struct piece_holder_t {
    geometry_brush_piece_t piece{};
    ~piece_holder_t() noexcept { BrushPiece_Shutdown( &piece ); }
};

struct list_holder_t {
    geometry_piece_list_t list{};
    ~list_holder_t() noexcept { PieceList_Shutdown( &list ); }
};

// Holds pieces for a span of values.
struct piece_array_t {
    common::vector_t<geometry_brush_piece_t *> pieces{};
    const common::allocator_t *pAllocator{ nullptr };
    ~piece_array_t() noexcept
    {
        for ( usize i = 0u; i < common::Vector_Count( &pieces ); ++i ) {
            BrushPiece_Shutdown( pieces.pData[i] );
            pieces.pData[i]->~geometry_brush_piece_t();
            common::Allocator_Free( pAllocator, pieces.pData[i],
                                    sizeof( geometry_brush_piece_t ),
                                    alignof( geometry_brush_piece_t ) );
        }
    }
    CYPHER_NODISCARD geometry_status_t TryBuild(
        const common::allocator_t *pAlloc,
        const geometry_brush_value_t *const *ppValues,
        usize cValues ) noexcept
    {
        pAllocator = pAlloc;
        if ( !common::Vector_Init( &pieces, pAlloc, cValues ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
        for ( usize i = 0u; i < cValues; ++i ) {
            void *pBlock = common::Allocator_Allocate(
                pAlloc, sizeof( geometry_brush_piece_t ), alignof( geometry_brush_piece_t ) );
            if ( pBlock == nullptr ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
            geometry_brush_piece_t *pPiece = new ( pBlock ) geometry_brush_piece_t{};
            ( void )common::Vector_PushBack( &pieces, pPiece );
            geometry_status_t status = BrushPiece_Init( pPiece, pAlloc );
            if ( status == geometry_status_t::OK ) {
                status = GeometryMaterialize_TryPieceFromValue( ppValues[i], pPiece );
            }
            if ( status != geometry_status_t::OK ) {
                return status;
            }
        }
        return geometry_status_t::OK;
    }
    CYPHER_NODISCARD common::span_t<const geometry_brush_piece_t *const> Span() const noexcept
    {
        return { pieces.pData, common::Vector_Count( &pieces ) };
    }
};

bool_t Contains( common::span_t<const geometry_source_id_t> ids, geometry_source_id_t id ) noexcept
{
    for ( usize i = 0u; i < ids.nCount; ++i ) {
        if ( ids.pData[i].value == id.value ) {
            return true;
        }
    }
    return false;
}

bool_t HasDuplicates( common::span_t<const geometry_source_id_t> ids ) noexcept
{
    for ( usize i = 0u; i < ids.nCount; ++i ) {
        for ( usize j = 0u; j < i; ++j ) {
            if ( ids.pData[i].value == ids.pData[j].value ) {
                return true;
            }
        }
    }
    return false;
}

// Materializes one piece and adds it to the plan as REPLACE (keeping
// brushId) or INSERT (fresh identity).
geometry_status_t PlanPiece(
    geometry_transaction_t *pTransaction,
    geometry_preview_plan_t *pPlan,
    const geometry_brush_piece_t *pPiece,
    geometry_source_id_t keepBrushId,
    common::span_t<const geometry_brush_value_t *const> donors ) noexcept
{
    geometry_materialize_desc_t desc{};
    desc.pPiece = pPiece;
    desc.brushId = keepBrushId;
    desc.bReuseSideIds = GeometrySourceId_IsValid( keepBrushId );
    desc.donors = donors;
    const geometry_brush_value_t *pValue = nullptr;
    geometry_status_t status = GeometryMaterialize_TryPiece( pTransaction, desc, &pValue );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = PreviewPlan_TryAddValue(
        pPlan,
        GeometrySourceId_IsValid( keepBrushId ) ? geometry_preview_step_kind_t::REPLACE
                                                 : geometry_preview_step_kind_t::INSERT,
        pValue );
    if ( status != geometry_status_t::OK ) {
        BrushValue_Release( pValue );
    }
    return status;
}

// Plans "brush becomes these fragments": first keeps the ID, rest are new,
// none removes it.
geometry_status_t PlanFragments(
    geometry_transaction_t *pTransaction,
    geometry_preview_plan_t *pPlan,
    geometry_source_id_t brushId,
    const geometry_piece_list_t *pFragments,
    common::span_t<const geometry_brush_value_t *const> donors,
    geometry_csg_command_result_t *pResult ) noexcept
{
    const usize cFragments = PieceList_Count( pFragments );
    if ( cFragments == 0u ) {
        ++pResult->cRemoved;
        return PreviewPlan_TryAddRemove( pPlan, brushId );
    }
    piece_holder_t piece{};
    geometry_status_t status = BrushPiece_Init( &piece.piece, pTransaction->pDocument->pAllocator );
    for ( usize i = 0u; status == geometry_status_t::OK && i < cFragments; ++i ) {
        status = PieceList_TryGet( pFragments, i, &piece.piece );
        if ( status == geometry_status_t::OK ) {
            status = PlanPiece( pTransaction, pPlan, &piece.piece,
                                i == 0u ? brushId : GEOMETRY_SOURCE_ID_INVALID, donors );
        }
        if ( status == geometry_status_t::OK ) {
            ++( i == 0u ? pResult->cModified : pResult->cCreated );
        }
    }
    return status;
}

geometry_status_t Begin(
    geometry_transaction_t *pTransaction,
    geometry_csg_command_result_t *pResultOut ) noexcept
{
    if ( pResultOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pResultOut = {};
    return GeometryTransaction_IsActive( pTransaction ) ? geometry_status_t::OK
                                                        : geometry_status_t::NOT_INITIALIZED;
}

} // namespace

// ---------------------------------------------------------------------------
// Subtract
// ---------------------------------------------------------------------------

geometry_status_t CsgCommand_TrySubtract(
    geometry_transaction_t *pTransaction,
    common::span_t<const geometry_source_id_t> cutterIds,
    common::span_t<const geometry_source_id_t> targetIds,
    bool_t bRemoveCutters,
    geometry_csg_command_result_t *pResultOut ) noexcept
{
    geometry_status_t status = Begin( pTransaction, pResultOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !common::Span_IsValid( cutterIds ) || !common::Span_IsValid( targetIds ) ||
         cutterIds.nCount == 0u || HasDuplicates( cutterIds ) || HasDuplicates( targetIds ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const common::allocator_t *pAllocator = pTransaction->pDocument->pAllocator;
    const geometry_policy_t &policy = pTransaction->pDocument->policy;

    pinned_t cutters{};
    pinned_t targets{};
    if ( !common::Vector_Init( &cutters.values, pAllocator, 0u ) ||
         !common::Vector_Init( &targets.values, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    status = PreviewPlan_TryPin( pTransaction, cutterIds, &cutters.values );
    if ( status == geometry_status_t::OK ) {
        status = PreviewPlan_TryPin( pTransaction, targetIds, &targets.values );
    }
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    piece_array_t cutterPieces{};
    status = cutterPieces.TryBuild( pAllocator, cutters.values.pData, cutterIds.nCount );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    geometry_preview_plan_t plan{};
    list_holder_t fragments{};
    piece_holder_t minuend{};
    common::vector_t<const geometry_brush_value_t *> donors{};
    if ( PreviewPlan_Init( &plan, pAllocator ) != geometry_status_t::OK ||
         PieceList_Init( &fragments.list, pAllocator ) != geometry_status_t::OK ||
         BrushPiece_Init( &minuend.piece, pAllocator ) != geometry_status_t::OK ||
         !common::Vector_Init( &donors, pAllocator, cutterIds.nCount + 1u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( usize t = 0u; status == geometry_status_t::OK && t < targetIds.nCount; ++t ) {
        if ( Contains( cutterIds, targetIds.pData[t] ) ) {
            continue;
        }
        const geometry_brush_value_t *pTarget = targets.values.pData[t];
        status = GeometryMaterialize_TryPieceFromValue( pTarget, &minuend.piece );
        PieceList_Clear( &fragments.list );
        bool_t bChanged = false;
        if ( status == geometry_status_t::OK ) {
            status = BrushCsg_TrySubtractAll( &minuend.piece, cutterPieces.Span(), policy,
                                              CSG_FRAGMENTS_PER_TARGET_MAX, &fragments.list,
                                              &bChanged );
        }
        if ( status != geometry_status_t::OK || !bChanged ) {
            continue;
        }
        // Donors: the target first (its own sides win ties), then cutters.
        common::Vector_Clear( &donors );
        ( void )common::Vector_PushBack( &donors, pTarget );
        for ( usize c = 0u; c < cutterIds.nCount; ++c ) {
            ( void )common::Vector_PushBack( &donors, cutters.values.pData[c] );
        }
        status = PlanFragments( pTransaction, &plan, targetIds.pData[t], &fragments.list,
                                { donors.pData, common::Vector_Count( &donors ) }, pResultOut );
    }
    for ( usize c = 0u; status == geometry_status_t::OK && bRemoveCutters && c < cutterIds.nCount; ++c ) {
        status = PreviewPlan_TryAddRemove( &plan, cutterIds.pData[c] );
        ++pResultOut->cRemoved;
    }
    if ( status == geometry_status_t::OK ) {
        status = PreviewPlan_TryApply( &plan, pTransaction );
    }
    if ( status != geometry_status_t::OK ) {
        *pResultOut = {};
    }
    return status;
}

// ---------------------------------------------------------------------------
// Intersect / merge
// ---------------------------------------------------------------------------

namespace
{

enum class fold_kind_t : common::u8 { INTERSECT, MERGE };

geometry_status_t FoldCommand(
    geometry_transaction_t *pTransaction,
    common::span_t<const geometry_source_id_t> brushIds,
    fold_kind_t kind,
    geometry_csg_command_result_t *pResultOut ) noexcept
{
    geometry_status_t status = Begin( pTransaction, pResultOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !common::Span_IsValid( brushIds ) || brushIds.nCount < 2u || HasDuplicates( brushIds ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const common::allocator_t *pAllocator = pTransaction->pDocument->pAllocator;
    const geometry_policy_t &policy = pTransaction->pDocument->policy;

    pinned_t operands{};
    if ( !common::Vector_Init( &operands.values, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    status = PreviewPlan_TryPin( pTransaction, brushIds, &operands.values );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    piece_array_t pieces{};
    status = pieces.TryBuild( pAllocator, operands.values.pData, brushIds.nCount );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    piece_holder_t result{};
    piece_holder_t scratch{};
    if ( BrushPiece_Init( &result.piece, pAllocator ) != geometry_status_t::OK ||
         BrushPiece_Init( &scratch.piece, pAllocator ) != geometry_status_t::OK ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( kind == fold_kind_t::MERGE ) {
        status = BrushCsg_TryMerge( pieces.Span(), policy, &result.piece );
    } else {
        status = BrushPiece_TryCopy( &result.piece, pieces.pieces.pData[0] );
        geometry_piece_extent_t extent = geometry_piece_extent_t::SOLID;
        for ( usize i = 1u; status == geometry_status_t::OK && i < brushIds.nCount; ++i ) {
            status = BrushCsg_TryIntersect( &result.piece, pieces.pieces.pData[i], policy,
                                            &scratch.piece, &extent );
            if ( status == geometry_status_t::OK ) {
                status = BrushPiece_TryCopy( &result.piece, &scratch.piece );
            }
            if ( status == geometry_status_t::OK && extent == geometry_piece_extent_t::EMPTY ) {
                status = geometry_status_t::DEGENERATE;
            }
        }
    }
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    geometry_preview_plan_t plan{};
    if ( PreviewPlan_Init( &plan, pAllocator ) != geometry_status_t::OK ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    status = PlanPiece( pTransaction, &plan, &result.piece, brushIds.pData[0],
                        { operands.values.pData, brushIds.nCount } );
    for ( usize i = 1u; status == geometry_status_t::OK && i < brushIds.nCount; ++i ) {
        status = PreviewPlan_TryAddRemove( &plan, brushIds.pData[i] );
    }
    if ( status == geometry_status_t::OK ) {
        status = PreviewPlan_TryApply( &plan, pTransaction );
    }
    if ( status == geometry_status_t::OK ) {
        pResultOut->cModified = 1u;
        pResultOut->cRemoved = static_cast<common::u32>( brushIds.nCount - 1u );
        pResultOut->resultBrushId = brushIds.pData[0];
    }
    return status;
}

} // namespace

geometry_status_t CsgCommand_TryIntersect(
    geometry_transaction_t *pTransaction,
    common::span_t<const geometry_source_id_t> brushIds,
    geometry_csg_command_result_t *pResultOut ) noexcept
{
    return FoldCommand( pTransaction, brushIds, fold_kind_t::INTERSECT, pResultOut );
}

geometry_status_t CsgCommand_TryMerge(
    geometry_transaction_t *pTransaction,
    common::span_t<const geometry_source_id_t> brushIds,
    geometry_csg_command_result_t *pResultOut ) noexcept
{
    return FoldCommand( pTransaction, brushIds, fold_kind_t::MERGE, pResultOut );
}

// ---------------------------------------------------------------------------
// Hollow / expand
// ---------------------------------------------------------------------------

geometry_status_t CsgCommand_TryHollow(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    math::f64 thickness,
    geometry_csg_command_result_t *pResultOut ) noexcept
{
    geometry_status_t status = Begin( pTransaction, pResultOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    const common::allocator_t *pAllocator = pTransaction->pDocument->pAllocator;
    pinned_t source{};
    if ( !common::Vector_Init( &source.values, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    status = PreviewPlan_TryPin( pTransaction, { &brushId, 1u }, &source.values );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    piece_holder_t piece{};
    list_holder_t walls{};
    geometry_preview_plan_t plan{};
    if ( BrushPiece_Init( &piece.piece, pAllocator ) != geometry_status_t::OK ||
         PieceList_Init( &walls.list, pAllocator ) != geometry_status_t::OK ||
         PreviewPlan_Init( &plan, pAllocator ) != geometry_status_t::OK ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    status = GeometryMaterialize_TryPieceFromValue( source.values.pData[0], &piece.piece );
    if ( status == geometry_status_t::OK ) {
        status = BrushCsg_TryHollow( &piece.piece, thickness, pTransaction->pDocument->policy,
                                     &walls.list );
    }
    if ( status == geometry_status_t::OK ) {
        status = PlanFragments( pTransaction, &plan, brushId, &walls.list,
                                { source.values.pData, 1u }, pResultOut );
    }
    if ( status == geometry_status_t::OK ) {
        status = PreviewPlan_TryApply( &plan, pTransaction );
    }
    if ( status != geometry_status_t::OK ) {
        *pResultOut = {};
    }
    return status;
}

geometry_status_t CsgCommand_TryExpand(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    math::f64 distance ) noexcept
{
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Scalar_IsFinite( distance ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_document_t *pDocument = pTransaction->pDocument;
    const geometry_brush_value_t *pSource = nullptr;
    if ( GeometryTransaction_TryGetPreview( pTransaction, brushId, &pSource ) !=
         geometry_status_t::OK ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    brush_solid_t brush{};
    geometry_status_t status = BrushSolid_Init( &brush, pDocument->pAllocator, brushId );
    if ( status == geometry_status_t::OK ) {
        status = BrushSolid_TryCopyFrom( &brush, &pSource->brush, pDocument->policy.limits );
    }
    for ( usize i = 0u; status == geometry_status_t::OK && i < BrushSolid_SideCount( &brush ); ++i ) {
        brush.sides.pData[i].plane.d -= distance;
    }
    const geometry_brush_value_t *pValue = nullptr;
    if ( status == geometry_status_t::OK ) {
        status = GeometryDocument_TryCreateBrushValue(
            pDocument, &brush, &pSource->attributes, &pValue, nullptr );
    }
    BrushSolid_Shutdown( &brush );
    if ( status == geometry_status_t::OK ) {
        status = GeometryTransaction_TryPreviewReplace( pTransaction, pValue );
    }
    BrushValue_Release( pValue );
    return status;
}

} // namespace cypher::editor::geometry
