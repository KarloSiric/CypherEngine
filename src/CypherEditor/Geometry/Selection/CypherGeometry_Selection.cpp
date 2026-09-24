//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Selection.cpp
//  Purpose: Implements component references and selection sets.
//  Details: Edge and vertex naming scans face rings; authoring brushes have
//           at most a few hundred faces, and naming runs on selection
//           gestures, not per frame. Every set mutation that can grow the
//           set is built in scratch and copied in only after capacity is
//           secured.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Selection.h"

#include "CypherCommon_Sort.h"

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::u32;
using common::u64;
using common::usize;
using math::vec3d_t;

bool_t IsReady( const geometry_selection_t *pSelection ) noexcept
{
    return pSelection != nullptr && pSelection->items.pAllocator != nullptr;
}

struct ref_less_t {
    CYPHER_NODISCARD bool_t operator()(
        const geometry_component_ref_t &a, const geometry_component_ref_t &b ) const noexcept
    {
        return ComponentRef_Less( a, b );
    }
};

u64 FaceSideId( const geometry_brush_value_t *pValue, usize iFace ) noexcept
{
    return pValue->brush.sides.pData[pValue->boundary.faces.pData[iFace].iSide].sourceId.value;
}

// True when face iFace's ring has the directed or reversed step (v0, v1).
bool_t FaceHasEdge( const brush_boundary_t &boundary, usize iFace, u32 v0, u32 v1 ) noexcept
{
    const brush_boundary_face_t &face = boundary.faces.pData[iFace];
    for ( u32 i = 0u; i < face.cVertices; ++i ) {
        const u32 a = boundary.faceVertexIndices.pData[face.iFirstIndex + i];
        const u32 b = boundary.faceVertexIndices.pData[face.iFirstIndex + ( i + 1u ) % face.cVertices];
        if ( ( a == v0 && b == v1 ) || ( a == v1 && b == v0 ) ) {
            return true;
        }
    }
    return false;
}

bool_t FaceHasVertex( const brush_boundary_t &boundary, usize iFace, u32 v ) noexcept
{
    const brush_boundary_face_t &face = boundary.faces.pData[iFace];
    for ( u32 i = 0u; i < face.cVertices; ++i ) {
        if ( boundary.faceVertexIndices.pData[face.iFirstIndex + i] == v ) {
            return true;
        }
    }
    return false;
}

vec3d_t FaceCentroid( const brush_boundary_t &boundary, usize iFace ) noexcept
{
    const brush_boundary_face_t &face = boundary.faces.pData[iFace];
    vec3d_t sum = math::CY_VEC3D_ZERO;
    for ( u32 i = 0u; i < face.cVertices; ++i ) {
        sum = math::Vec3d_Add(
            sum, boundary.vertices.pData[boundary.faceVertexIndices.pData[face.iFirstIndex + i]] );
    }
    return math::Vec3d_Scale( sum, 1.0 / static_cast<math::f64>( face.cVertices ) );
}

// Lower bound of ref in the sorted items.
usize LowerBound( const geometry_selection_t *pSelection, const geometry_component_ref_t &ref ) noexcept
{
    usize lo = 0u;
    usize hi = common::Vector_Count( &pSelection->items );
    while ( lo < hi ) {
        const usize mid = lo + ( hi - lo ) / 2u;
        if ( ComponentRef_Less( pSelection->items.pData[mid], ref ) ) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo;
}

// Sorts and deduplicates scratch, then replaces the set's contents.
geometry_status_t CommitScratch(
    geometry_selection_t *pSelection, common::vector_t<geometry_component_ref_t> *pScratch ) noexcept
{
    const usize cScratch = common::Vector_Count( pScratch );
    common::Sort_Unstable( common::span_t<geometry_component_ref_t>{ pScratch->pData, cScratch },
                           ref_less_t{} );
    usize cUnique = 0u;
    for ( usize i = 0u; i < cScratch; ++i ) {
        if ( cUnique == 0u || !ComponentRef_Equals( pScratch->pData[cUnique - 1u], pScratch->pData[i] ) ) {
            pScratch->pData[cUnique++] = pScratch->pData[i];
        }
    }
    if ( !common::Vector_Reserve( &pSelection->items, cUnique ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    common::Vector_Clear( &pSelection->items );
    for ( usize i = 0u; i < cUnique; ++i ) {
        ( void )common::Vector_PushBack( &pSelection->items, pScratch->pData[i] );
    }
    return geometry_status_t::OK;
}

u32 IdSlots( geometry_component_kind_t kind ) noexcept
{
    switch ( kind ) {
        case geometry_component_kind_t::BRUSH_SIDE: return 1u;
        case geometry_component_kind_t::BRUSH_EDGE: return 2u;
        case geometry_component_kind_t::BRUSH_VERTEX: return 3u;
        default: return 0u;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// References
// ---------------------------------------------------------------------------

bool_t ComponentRef_Less(
    const geometry_component_ref_t &l, const geometry_component_ref_t &r ) noexcept
{
    if ( l.kind != r.kind ) {
        return static_cast<u32>( l.kind ) < static_cast<u32>( r.kind );
    }
    if ( l.brushId.value != r.brushId.value ) {
        return l.brushId.value < r.brushId.value;
    }
    if ( l.a.value != r.a.value ) {
        return l.a.value < r.a.value;
    }
    if ( l.b.value != r.b.value ) {
        return l.b.value < r.b.value;
    }
    return l.c.value < r.c.value;
}

bool_t ComponentRef_Equals(
    const geometry_component_ref_t &l, const geometry_component_ref_t &r ) noexcept
{
    return l.kind == r.kind && l.brushId.value == r.brushId.value && l.a.value == r.a.value &&
           l.b.value == r.b.value && l.c.value == r.c.value;
}

bool_t ComponentRef_IsWellFormed( const geometry_component_ref_t &ref ) noexcept
{
    if ( ref.kind == geometry_component_kind_t::INVALID ||
         static_cast<u32>( ref.kind ) >= static_cast<u32>( geometry_component_kind_t::COUNT ) ||
         !GeometrySourceId_IsValid( ref.brushId ) ) {
        return false;
    }
    const u32 cSlots = IdSlots( ref.kind );
    const u64 ids[3] = { ref.a.value, ref.b.value, ref.c.value };
    for ( u32 i = 0u; i < 3u; ++i ) {
        if ( i < cSlots ) {
            if ( ids[i] == 0u || ( i > 0u && ids[i] <= ids[i - 1u] ) ) {
                return false;
            }
        } else if ( ids[i] != 0u ) {
            return false;
        }
    }
    return true;
}

geometry_component_ref_t ComponentRef_Brush( geometry_source_id_t brushId ) noexcept
{
    geometry_component_ref_t ref{};
    ref.kind = geometry_component_kind_t::BRUSH;
    ref.brushId = brushId;
    return ref;
}

geometry_component_ref_t ComponentRef_Side(
    geometry_source_id_t brushId, geometry_source_id_t sideId ) noexcept
{
    geometry_component_ref_t ref{};
    ref.kind = geometry_component_kind_t::BRUSH_SIDE;
    ref.brushId = brushId;
    ref.a = sideId;
    return ref;
}

geometry_status_t ComponentRef_TryFromEdge(
    const geometry_brush_value_t *pValue, usize iEdge, geometry_component_ref_t *pRefOut ) noexcept
{
    if ( pRefOut == nullptr || pValue == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pRefOut = {};
    const brush_boundary_t &boundary = pValue->boundary;
    if ( iEdge >= common::Vector_Count( &boundary.edges ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const brush_boundary_edge_t edge = boundary.edges.pData[iEdge];
    u64 sides[2]{};
    u32 cFound = 0u;
    for ( usize f = 0u; f < common::Vector_Count( &boundary.faces ); ++f ) {
        if ( FaceHasEdge( boundary, f, edge.iVertex0, edge.iVertex1 ) ) {
            if ( cFound == 2u ) {
                return geometry_status_t::CORRUPT_STATE;
            }
            sides[cFound++] = FaceSideId( pValue, f );
        }
    }
    if ( cFound != 2u ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    pRefOut->kind = geometry_component_kind_t::BRUSH_EDGE;
    pRefOut->brushId = pValue->brush.sourceId;
    pRefOut->a.value = sides[0] < sides[1] ? sides[0] : sides[1];
    pRefOut->b.value = sides[0] < sides[1] ? sides[1] : sides[0];
    return geometry_status_t::OK;
}

geometry_status_t ComponentRef_TryFromVertex(
    const geometry_brush_value_t *pValue, usize iVertex, geometry_component_ref_t *pRefOut ) noexcept
{
    if ( pRefOut == nullptr || pValue == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pRefOut = {};
    const brush_boundary_t &boundary = pValue->boundary;
    if ( iVertex >= common::Vector_Count( &boundary.vertices ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // Keep the three smallest side IDs among faces touching the vertex.
    u64 lowest[3] = { common::CY_U64_MAX, common::CY_U64_MAX, common::CY_U64_MAX };
    u32 cFound = 0u;
    for ( usize f = 0u; f < common::Vector_Count( &boundary.faces ); ++f ) {
        if ( !FaceHasVertex( boundary, f, static_cast<u32>( iVertex ) ) ) {
            continue;
        }
        ++cFound;
        u64 id = FaceSideId( pValue, f );
        for ( u32 i = 0u; i < 3u; ++i ) {
            if ( id < lowest[i] ) {
                const u64 swap = lowest[i];
                lowest[i] = id;
                id = swap;
            }
        }
    }
    if ( cFound < 3u ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    pRefOut->kind = geometry_component_kind_t::BRUSH_VERTEX;
    pRefOut->brushId = pValue->brush.sourceId;
    pRefOut->a.value = lowest[0];
    pRefOut->b.value = lowest[1];
    pRefOut->c.value = lowest[2];
    return geometry_status_t::OK;
}

geometry_status_t ComponentRef_TryResolve(
    const geometry_brush_value_t *pValue, const geometry_component_ref_t &ref,
    usize *pIndexOut ) noexcept
{
    if ( pIndexOut == nullptr || pValue == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pIndexOut = 0u;
    if ( !ComponentRef_IsWellFormed( ref ) || pValue->brush.sourceId.value != ref.brushId.value ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    switch ( ref.kind ) {
        case geometry_component_kind_t::BRUSH:
            return geometry_status_t::OK;
        case geometry_component_kind_t::BRUSH_SIDE:
            for ( usize i = 0u; i < BrushSolid_SideCount( &pValue->brush ); ++i ) {
                if ( pValue->brush.sides.pData[i].sourceId.value == ref.a.value ) {
                    *pIndexOut = i;
                    return geometry_status_t::OK;
                }
            }
            return geometry_status_t::INVALID_ARGUMENT;
        case geometry_component_kind_t::BRUSH_EDGE:
            for ( usize i = 0u; i < common::Vector_Count( &pValue->boundary.edges ); ++i ) {
                geometry_component_ref_t candidate{};
                if ( ComponentRef_TryFromEdge( pValue, i, &candidate ) == geometry_status_t::OK &&
                     ComponentRef_Equals( candidate, ref ) ) {
                    *pIndexOut = i;
                    return geometry_status_t::OK;
                }
            }
            return geometry_status_t::INVALID_ARGUMENT;
        case geometry_component_kind_t::BRUSH_VERTEX:
            for ( usize i = 0u; i < common::Vector_Count( &pValue->boundary.vertices ); ++i ) {
                geometry_component_ref_t candidate{};
                if ( ComponentRef_TryFromVertex( pValue, i, &candidate ) == geometry_status_t::OK &&
                     ComponentRef_Equals( candidate, ref ) ) {
                    *pIndexOut = i;
                    return geometry_status_t::OK;
                }
            }
            return geometry_status_t::INVALID_ARGUMENT;
        default:
            return geometry_status_t::INVALID_ARGUMENT;
    }
}

geometry_status_t ComponentRef_TryCenter(
    const geometry_brush_value_t *pValue, const geometry_component_ref_t &ref,
    vec3d_t *pCenterOut ) noexcept
{
    if ( pCenterOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pCenterOut = math::CY_VEC3D_ZERO;
    usize index = 0u;
    const geometry_status_t status = ComponentRef_TryResolve( pValue, ref, &index );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    const brush_boundary_t &boundary = pValue->boundary;
    switch ( ref.kind ) {
        case geometry_component_kind_t::BRUSH:
            *pCenterOut = math::Aabbd_Center( pValue->bounds );
            break;
        case geometry_component_kind_t::BRUSH_SIDE: {
            usize iFace = 0u;
            if ( BrushBoundary_TryFindFaceForSide( &boundary, index, &iFace ) != geometry_status_t::OK ) {
                return geometry_status_t::CORRUPT_STATE;
            }
            *pCenterOut = FaceCentroid( boundary, iFace );
            break;
        }
        case geometry_component_kind_t::BRUSH_EDGE: {
            const brush_boundary_edge_t edge = boundary.edges.pData[index];
            *pCenterOut = math::Vec3d_Scale(
                math::Vec3d_Add( boundary.vertices.pData[edge.iVertex0],
                                 boundary.vertices.pData[edge.iVertex1] ), 0.5 );
            break;
        }
        case geometry_component_kind_t::BRUSH_VERTEX:
            *pCenterOut = boundary.vertices.pData[index];
            break;
        default:
            return geometry_status_t::INVALID_ARGUMENT;
    }
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Sets
// ---------------------------------------------------------------------------

geometry_status_t Selection_Init(
    geometry_selection_t *pSelection, const common::allocator_t *pAllocator ) noexcept
{
    if ( pSelection == nullptr || !common::Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( IsReady( pSelection ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    return common::Vector_Init( &pSelection->items, pAllocator, 0u )
        ? geometry_status_t::OK
        : geometry_status_t::ALLOCATION_FAILED;
}

void Selection_Shutdown( geometry_selection_t *pSelection ) noexcept
{
    if ( IsReady( pSelection ) ) {
        common::Vector_Shutdown( &pSelection->items );
    }
}

void Selection_Clear( geometry_selection_t *pSelection ) noexcept
{
    if ( IsReady( pSelection ) ) {
        common::Vector_Clear( &pSelection->items );
    }
}

usize Selection_Count( const geometry_selection_t *pSelection ) noexcept
{
    return IsReady( pSelection ) ? common::Vector_Count( &pSelection->items ) : 0u;
}

geometry_status_t Selection_TryGet(
    const geometry_selection_t *pSelection, usize iIndex, geometry_component_ref_t *pRefOut ) noexcept
{
    if ( pRefOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pRefOut = {};
    if ( !IsReady( pSelection ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iIndex >= common::Vector_Count( &pSelection->items ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pRefOut = pSelection->items.pData[iIndex];
    return geometry_status_t::OK;
}

bool_t Selection_Contains(
    const geometry_selection_t *pSelection, const geometry_component_ref_t &ref ) noexcept
{
    if ( !IsReady( pSelection ) ) {
        return false;
    }
    const usize i = LowerBound( pSelection, ref );
    return i < common::Vector_Count( &pSelection->items ) &&
           ComponentRef_Equals( pSelection->items.pData[i], ref );
}

geometry_status_t Selection_TryAdd(
    geometry_selection_t *pSelection, const geometry_component_ref_t &ref ) noexcept
{
    if ( !IsReady( pSelection ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !ComponentRef_IsWellFormed( ref ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const usize i = LowerBound( pSelection, ref );
    if ( i < common::Vector_Count( &pSelection->items ) &&
         ComponentRef_Equals( pSelection->items.pData[i], ref ) ) {
        return geometry_status_t::OK;
    }
    return common::Vector_Insert( &pSelection->items, i, ref ) ? geometry_status_t::OK
                                                               : geometry_status_t::ALLOCATION_FAILED;
}

geometry_status_t Selection_TryRemove(
    geometry_selection_t *pSelection, const geometry_component_ref_t &ref ) noexcept
{
    if ( !IsReady( pSelection ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const usize i = LowerBound( pSelection, ref );
    if ( i < common::Vector_Count( &pSelection->items ) &&
         ComponentRef_Equals( pSelection->items.pData[i], ref ) ) {
        common::Vector_Erase( &pSelection->items, i );
    }
    return geometry_status_t::OK;
}

geometry_status_t Selection_TryToggle(
    geometry_selection_t *pSelection, const geometry_component_ref_t &ref ) noexcept
{
    return Selection_Contains( pSelection, ref ) ? Selection_TryRemove( pSelection, ref )
                                                 : Selection_TryAdd( pSelection, ref );
}

geometry_status_t Selection_TryPromoteToBrushes( geometry_selection_t *pSelection ) noexcept
{
    if ( !IsReady( pSelection ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    common::vector_t<geometry_component_ref_t> scratch{};
    const usize cItems = common::Vector_Count( &pSelection->items );
    if ( !common::Vector_Init( &scratch, pSelection->items.pAllocator, cItems ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cItems; ++i ) {
        ( void )common::Vector_PushBack( &scratch, ComponentRef_Brush( pSelection->items.pData[i].brushId ) );
    }
    return CommitScratch( pSelection, &scratch );
}

geometry_status_t Selection_TryAddAllComponents(
    geometry_selection_t *pSelection, const geometry_brush_value_t *pValue,
    geometry_component_kind_t kind ) noexcept
{
    if ( !IsReady( pSelection ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pValue == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    common::vector_t<geometry_component_ref_t> scratch{};
    if ( !common::Vector_Init( &scratch, pSelection->items.pAllocator,
                               common::Vector_Count( &pSelection->items ) ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < common::Vector_Count( &pSelection->items ); ++i ) {
        ( void )common::Vector_PushBack( &scratch, pSelection->items.pData[i] );
    }
    usize cCount = 0u;
    switch ( kind ) {
        case geometry_component_kind_t::BRUSH: cCount = 1u; break;
        case geometry_component_kind_t::BRUSH_SIDE: cCount = BrushSolid_SideCount( &pValue->brush ); break;
        case geometry_component_kind_t::BRUSH_EDGE: cCount = common::Vector_Count( &pValue->boundary.edges ); break;
        case geometry_component_kind_t::BRUSH_VERTEX: cCount = common::Vector_Count( &pValue->boundary.vertices ); break;
        default: return geometry_status_t::INVALID_ARGUMENT;
    }
    for ( usize i = 0u; i < cCount; ++i ) {
        geometry_component_ref_t ref{};
        geometry_status_t status = geometry_status_t::OK;
        if ( kind == geometry_component_kind_t::BRUSH ) {
            ref = ComponentRef_Brush( pValue->brush.sourceId );
        } else if ( kind == geometry_component_kind_t::BRUSH_SIDE ) {
            ref = ComponentRef_Side( pValue->brush.sourceId, pValue->brush.sides.pData[i].sourceId );
        } else if ( kind == geometry_component_kind_t::BRUSH_EDGE ) {
            status = ComponentRef_TryFromEdge( pValue, i, &ref );
        } else {
            status = ComponentRef_TryFromVertex( pValue, i, &ref );
        }
        if ( status != geometry_status_t::OK ) {
            return status;
        }
        if ( !common::Vector_PushBack( &scratch, ref ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    return CommitScratch( pSelection, &scratch );
}

geometry_status_t Selection_TryPrune(
    geometry_selection_t *pSelection, const geometry_document_snapshot_t *pSnapshot,
    usize *pDroppedOut ) noexcept
{
    if ( pDroppedOut != nullptr ) {
        *pDroppedOut = 0u;
    }
    if ( !IsReady( pSelection ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pSnapshot == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // In-place compaction cannot fail.
    usize cKept = 0u;
    const usize cItems = common::Vector_Count( &pSelection->items );
    for ( usize i = 0u; i < cItems; ++i ) {
        const geometry_component_ref_t ref = pSelection->items.pData[i];
        const geometry_brush_value_t *pValue = nullptr;
        usize index = 0u;
        if ( GeometrySnapshot_TryFindBrush( pSnapshot, ref.brushId, &pValue ) == geometry_status_t::OK &&
             ComponentRef_TryResolve( pValue, ref, &index ) == geometry_status_t::OK ) {
            pSelection->items.pData[cKept++] = ref;
        }
    }
    while ( common::Vector_Count( &pSelection->items ) > cKept ) {
        common::Vector_PopBack( &pSelection->items );
    }
    if ( pDroppedOut != nullptr ) {
        *pDroppedOut = cItems - cKept;
    }
    return geometry_status_t::OK;
}

geometry_status_t Selection_TryBounds(
    const geometry_selection_t *pSelection, const geometry_document_snapshot_t *pSnapshot,
    math::aabbd_t *pBoundsOut ) noexcept
{
    if ( pBoundsOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pBoundsOut = math::CY_AABBD_EMPTY;
    if ( !IsReady( pSelection ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pSnapshot == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    math::aabbd_t bounds = math::CY_AABBD_EMPTY;
    bool_t bAny = false;
    for ( usize i = 0u; i < common::Vector_Count( &pSelection->items ); ++i ) {
        const geometry_component_ref_t &ref = pSelection->items.pData[i];
        const geometry_brush_value_t *pValue = nullptr;
        if ( GeometrySnapshot_TryFindBrush( pSnapshot, ref.brushId, &pValue ) != geometry_status_t::OK ) {
            continue;
        }
        if ( ref.kind == geometry_component_kind_t::BRUSH ) {
            bounds = math::Aabbd_Union( bounds, pValue->bounds );
            bAny = true;
            continue;
        }
        vec3d_t center{};
        if ( ComponentRef_TryCenter( pValue, ref, &center ) == geometry_status_t::OK ) {
            bounds = math::Aabbd_ExpandPoint( bounds, center );
            bAny = true;
        }
    }
    if ( !bAny ) {
        return geometry_status_t::DEGENERATE;
    }
    *pBoundsOut = bounds;
    return geometry_status_t::OK;
}

geometry_status_t Selection_TryRemap(
    geometry_selection_t *pSelection,
    common::span_t<const geometry_selection_remap_t> remaps,
    geometry_selection_remap_report_t *pReportOut ) noexcept
{
    if ( pReportOut != nullptr ) {
        *pReportOut = {};
    }
    if ( !IsReady( pSelection ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Span_IsValid( remaps ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_selection_remap_report_t report{};
    common::vector_t<geometry_component_ref_t> scratch{};
    if ( !common::Vector_Init( &scratch, pSelection->items.pAllocator,
                               common::Vector_Count( &pSelection->items ) ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < common::Vector_Count( &pSelection->items ); ++i ) {
        const geometry_component_ref_t ref = pSelection->items.pData[i];
        const geometry_selection_remap_t *pRemap = nullptr;
        for ( usize r = 0u; r < remaps.nCount && pRemap == nullptr; ++r ) {
            if ( remaps.pData[r].source.value == ref.brushId.value ) {
                pRemap = &remaps.pData[r];
            }
        }
        if ( pRemap == nullptr ) {
            if ( !common::Vector_PushBack( &scratch, ref ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
            continue;
        }
        if ( pRemap->cDestinations > 0u && pRemap->pDestinations == nullptr ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        if ( pRemap->cDestinations == 0u ) {
            ++report.cDropped;
            continue;
        }
        const bool_t bSplit = pRemap->cDestinations > 1u &&
                              ref.kind != geometry_component_kind_t::BRUSH;
        if ( bSplit ) {
            ++report.cAmbiguous;
        } else {
            ++report.cRemapped;
        }
        for ( u32 d = 0u; d < pRemap->cDestinations; ++d ) {
            geometry_component_ref_t moved = ref;
            moved.brushId = pRemap->pDestinations[d];
            if ( bSplit ) {
                moved = ComponentRef_Brush( pRemap->pDestinations[d] );
            }
            if ( !ComponentRef_IsWellFormed( moved ) ) {
                return geometry_status_t::INVALID_ARGUMENT;
            }
            if ( !common::Vector_PushBack( &scratch, moved ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
    }
    const geometry_status_t status = CommitScratch( pSelection, &scratch );
    if ( status == geometry_status_t::OK && pReportOut != nullptr ) {
        *pReportOut = report;
    }
    return status;
}

} // namespace cypher::editor::geometry
