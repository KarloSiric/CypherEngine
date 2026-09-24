//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SpatialIndex.cpp
//  Purpose: Implements the snapshot-bound brush BVH.
//  Details: Rebuild constructs into local vectors and swaps them in only
//           on success. Nodes are appended parent-before-children, so a
//           reverse sweep over the node array refits bottom-up without
//           recursion. Traversal uses a fixed stack: median splits bound
//           the depth by ceil(log2(n)) + 1, far below its capacity.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_SpatialIndex.h"

#include "CypherCommon_Sort.h"

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::u32;
using common::usize;
using math::aabbd_t;
using math::f64;
using math::vec3d_t;

constexpr usize cTraversalStack = 128u;

template <typename type_t>
void SwapVectors( common::vector_t<type_t> *pA, common::vector_t<type_t> *pB ) noexcept
{
    type_t *pData = pA->pData;
    const usize nCount = pA->nCount;
    const usize nCapacity = pA->nCapacity;
    const common::allocator_t *pAllocator = pA->pAllocator;
    pA->pData = pB->pData;
    pA->nCount = pB->nCount;
    pA->nCapacity = pB->nCapacity;
    pA->pAllocator = pB->pAllocator;
    pB->pData = pData;
    pB->nCount = nCount;
    pB->nCapacity = nCapacity;
    pB->pAllocator = pAllocator;
}

bool_t Overlaps( const aabbd_t &a, const aabbd_t &b ) noexcept
{
    return a.minimum.x <= b.maximum.x && b.minimum.x <= a.maximum.x &&
           a.minimum.y <= b.maximum.y && b.minimum.y <= a.maximum.y &&
           a.minimum.z <= b.maximum.z && b.minimum.z <= a.maximum.z;
}

f64 Axis( const vec3d_t &v, u32 axis ) noexcept
{
    return axis == 0u ? v.x : ( axis == 1u ? v.y : v.z );
}

const aabbd_t &LeafBounds( const geometry_document_snapshot_t *pSnapshot, u32 iEntry ) noexcept
{
    return pSnapshot->pBrushes[iEntry].pValue->bounds;
}

struct centroid_less_t {
    const geometry_document_snapshot_t *pSnapshot;
    u32 axis;

    CYPHER_NODISCARD bool_t operator()( const u32 &left, const u32 &right ) const noexcept
    {
        const f64 a = Axis( math::Aabbd_Center( LeafBounds( pSnapshot, left ) ), axis );
        const f64 b = Axis( math::Aabbd_Center( LeafBounds( pSnapshot, right ) ), axis );
        if ( a != b ) {
            return a < b;
        }
        // Entries are sorted by brush ID, so the entry index is the ID tie-break.
        return left < right;
    }
};

aabbd_t RangeBounds(
    const geometry_document_snapshot_t *pSnapshot, const u32 *pOrder, u32 cCount ) noexcept
{
    aabbd_t bounds = math::CY_AABBD_EMPTY;
    for ( u32 i = 0u; i < cCount; ++i ) {
        bounds = math::Aabbd_Union( bounds, LeafBounds( pSnapshot, pOrder[i] ) );
    }
    return bounds;
}

// Builds the tree for pSnapshot into empty, initialized vectors.
geometry_status_t BuildTree(
    const geometry_document_snapshot_t *pSnapshot,
    common::vector_t<geometry_spatial_node_t> *pNodes,
    common::vector_t<u32> *pOrder ) noexcept
{
    const usize cBrushes = pSnapshot->cBrushes;
    if ( cBrushes > static_cast<usize>( common::CY_U32_MAX / 2u ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !common::Vector_Resize( pOrder, cBrushes ) ||
         !common::Vector_Reserve( pNodes, cBrushes == 0u ? 0u : 2u * cBrushes ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cBrushes; ++i ) {
        pOrder->pData[i] = static_cast<u32>( i );
    }
    if ( cBrushes == 0u ) {
        return geometry_status_t::OK;
    }

    struct pending_t {
        u32 iNode;
    } stack[cTraversalStack];
    usize cStack = 0u;

    ( void )common::Vector_PushBack(
        pNodes, geometry_spatial_node_t{ math::CY_AABBD_EMPTY, 0u,
                                         static_cast<u32>( cBrushes ), SPATIAL_NODE_NONE,
                                         SPATIAL_NODE_NONE } );
    stack[cStack++] = { 0u };

    while ( cStack > 0u ) {
        const u32 iNode = stack[--cStack].iNode;
        geometry_spatial_node_t node = pNodes->pData[iNode];
        u32 *pRange = pOrder->pData + node.iFirst;
        node.bounds = RangeBounds( pSnapshot, pRange, node.cLeaves );

        if ( node.cLeaves <= SPATIAL_LEAF_SIZE_MAX ) {
            pNodes->pData[iNode] = node;
            continue;
        }

        aabbd_t centroids = math::CY_AABBD_EMPTY;
        for ( u32 i = 0u; i < node.cLeaves; ++i ) {
            centroids = math::Aabbd_ExpandPoint(
                centroids, math::Aabbd_Center( LeafBounds( pSnapshot, pRange[i] ) ) );
        }
        const vec3d_t extent = math::Vec3d_Subtract( centroids.maximum, centroids.minimum );
        u32 axis = 0u;
        if ( extent.y > Axis( extent, axis ) ) {
            axis = 1u;
        }
        if ( extent.z > Axis( extent, axis ) ) {
            axis = 2u;
        }
        common::Sort_Unstable( common::span_t<u32>{ pRange, node.cLeaves },
                               centroid_less_t{ pSnapshot, axis } );

        const u32 cLeft = node.cLeaves / 2u;
        const u32 iLeft = static_cast<u32>( common::Vector_Count( pNodes ) );
        const u32 iRight = iLeft + 1u;
        // Cannot fail: 2n - 1 nodes reserved above.
        ( void )common::Vector_PushBack(
            pNodes, geometry_spatial_node_t{ math::CY_AABBD_EMPTY, node.iFirst, cLeft,
                                             SPATIAL_NODE_NONE, SPATIAL_NODE_NONE } );
        ( void )common::Vector_PushBack(
            pNodes, geometry_spatial_node_t{ math::CY_AABBD_EMPTY, node.iFirst + cLeft,
                                             node.cLeaves - cLeft, SPATIAL_NODE_NONE,
                                             SPATIAL_NODE_NONE } );
        node.cLeaves = 0u;
        node.iLeft = iLeft;
        node.iRight = iRight;
        pNodes->pData[iNode] = node;

        if ( cStack + 2u > cTraversalStack ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        stack[cStack++] = { iRight };
        stack[cStack++] = { iLeft };
    }
    return geometry_status_t::OK;
}

void Refit( geometry_spatial_index_t *pIndex ) noexcept
{
    const usize cNodes = common::Vector_Count( &pIndex->nodes );
    for ( usize i = cNodes; i > 0u; --i ) {
        geometry_spatial_node_t &node = pIndex->nodes.pData[i - 1u];
        if ( node.cLeaves > 0u ) {
            node.bounds = RangeBounds( pIndex->pSnapshot,
                                       pIndex->leafOrder.pData + node.iFirst, node.cLeaves );
        } else {
            node.bounds = math::Aabbd_Union( pIndex->nodes.pData[node.iLeft].bounds,
                                             pIndex->nodes.pData[node.iRight].bounds );
        }
    }
}

bool_t SameBrushSet(
    const geometry_document_snapshot_t *pA, const geometry_document_snapshot_t *pB ) noexcept
{
    if ( pA == nullptr || pB == nullptr || pA->cBrushes != pB->cBrushes ) {
        return false;
    }
    for ( usize i = 0u; i < pA->cBrushes; ++i ) {
        if ( pA->pBrushes[i].brushId.value != pB->pBrushes[i].brushId.value ) {
            return false;
        }
    }
    return true;
}

// Slab test of the ray segment [0, tMax] against a box.
bool_t RayHitsBox(
    const aabbd_t &box, vec3d_t origin, vec3d_t direction, f64 tMax ) noexcept
{
    f64 tMin = 0.0;
    for ( u32 axis = 0u; axis < 3u; ++axis ) {
        const f64 o = Axis( origin, axis );
        const f64 d = Axis( direction, axis );
        const f64 lo = Axis( box.minimum, axis );
        const f64 hi = Axis( box.maximum, axis );
        if ( d == 0.0 ) {
            if ( o < lo || o > hi ) {
                return false;
            }
            continue;
        }
        f64 t0 = ( lo - o ) / d;
        f64 t1 = ( hi - o ) / d;
        if ( t0 > t1 ) {
            const f64 swap = t0;
            t0 = t1;
            t1 = swap;
        }
        tMin = t0 > tMin ? t0 : tMin;
        tMax = t1 < tMax ? t1 : tMax;
        if ( tMin > tMax ) {
            return false;
        }
    }
    return true;
}

struct id_less_t {
    CYPHER_NODISCARD bool_t operator()(
        const geometry_source_id_t &a, const geometry_source_id_t &b ) const noexcept
    {
        return a.value < b.value;
    }
};

} // namespace

geometry_spatial_index_t::~geometry_spatial_index_t() noexcept
{
    SpatialIndex_Shutdown( this );
}

geometry_status_t SpatialIndex_Init(
    geometry_spatial_index_t *pIndex,
    const common::allocator_t *pAllocator ) noexcept
{
    if ( pIndex == nullptr || !common::Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pIndex->bInitialized ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !common::Vector_Init( &pIndex->nodes, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !common::Vector_Init( &pIndex->leafOrder, pAllocator, 0u ) ) {
        common::Vector_Shutdown( &pIndex->nodes );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pIndex->pAllocator = pAllocator;
    pIndex->pSnapshot = nullptr;
    pIndex->bInitialized = true;
    return geometry_status_t::OK;
}

void SpatialIndex_Shutdown( geometry_spatial_index_t *pIndex ) noexcept
{
    if ( pIndex == nullptr || !pIndex->bInitialized ) {
        return;
    }
    GeometrySnapshot_Release( pIndex->pSnapshot );
    pIndex->pSnapshot = nullptr;
    common::Vector_Shutdown( &pIndex->nodes );
    common::Vector_Shutdown( &pIndex->leafOrder );
    pIndex->pAllocator = nullptr;
    pIndex->bInitialized = false;
}

geometry_status_t SpatialIndex_TryRebuild(
    geometry_spatial_index_t *pIndex,
    const geometry_document_snapshot_t *pSnapshot ) noexcept
{
    if ( pIndex == nullptr || !pIndex->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pSnapshot == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    common::vector_t<geometry_spatial_node_t> nodes{};
    common::vector_t<u32> order{};
    if ( !common::Vector_Init( &nodes, pIndex->pAllocator, 0u ) ||
         !common::Vector_Init( &order, pIndex->pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const geometry_status_t status = BuildTree( pSnapshot, &nodes, &order );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    // Commit: the old tree ends up in the locals and is freed on return.
    SwapVectors( &pIndex->nodes, &nodes );
    SwapVectors( &pIndex->leafOrder, &order );
    GeometrySnapshot_AddRef( pSnapshot );
    GeometrySnapshot_Release( pIndex->pSnapshot );
    pIndex->pSnapshot = pSnapshot;
    return geometry_status_t::OK;
}

geometry_status_t SpatialIndex_TrySync(
    geometry_spatial_index_t *pIndex,
    const geometry_document_snapshot_t *pSnapshot,
    bool_t *pRebuiltOut ) noexcept
{
    if ( pRebuiltOut != nullptr ) {
        *pRebuiltOut = false;
    }
    if ( pIndex == nullptr || !pIndex->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pSnapshot == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pSnapshot == pIndex->pSnapshot ) {
        return geometry_status_t::OK;
    }
    if ( SameBrushSet( pIndex->pSnapshot, pSnapshot ) ) {
        GeometrySnapshot_AddRef( pSnapshot );
        GeometrySnapshot_Release( pIndex->pSnapshot );
        pIndex->pSnapshot = pSnapshot;
        Refit( pIndex );
        return geometry_status_t::OK;
    }
    const geometry_status_t status = SpatialIndex_TryRebuild( pIndex, pSnapshot );
    if ( status == geometry_status_t::OK && pRebuiltOut != nullptr ) {
        *pRebuiltOut = true;
    }
    return status;
}

common::u64 SpatialIndex_Revision( const geometry_spatial_index_t *pIndex ) noexcept
{
    return pIndex == nullptr ? 0u : GeometrySnapshot_Revision( pIndex->pSnapshot );
}

geometry_status_t SpatialIndex_TryQueryAabb(
    const geometry_spatial_index_t *pIndex,
    aabbd_t query,
    common::vector_t<geometry_source_id_t> *pBrushIdsOut ) noexcept
{
    if ( pBrushIdsOut == nullptr || pBrushIdsOut->pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    common::Vector_Clear( pBrushIdsOut );
    if ( pIndex == nullptr || !pIndex->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Aabbd_IsFinite( query ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( common::Vector_Count( &pIndex->nodes ) == 0u ) {
        return geometry_status_t::OK;
    }

    u32 stack[cTraversalStack];
    usize cStack = 0u;
    stack[cStack++] = 0u;
    while ( cStack > 0u ) {
        const geometry_spatial_node_t &node = pIndex->nodes.pData[stack[--cStack]];
        if ( !Overlaps( node.bounds, query ) ) {
            continue;
        }
        if ( node.cLeaves > 0u ) {
            for ( u32 i = 0u; i < node.cLeaves; ++i ) {
                const u32 iEntry = pIndex->leafOrder.pData[node.iFirst + i];
                if ( Overlaps( LeafBounds( pIndex->pSnapshot, iEntry ), query ) &&
                     !common::Vector_PushBack( pBrushIdsOut,
                                               pIndex->pSnapshot->pBrushes[iEntry].brushId ) ) {
                    common::Vector_Clear( pBrushIdsOut );
                    return geometry_status_t::ALLOCATION_FAILED;
                }
            }
            continue;
        }
        if ( cStack + 2u > cTraversalStack ) {
            common::Vector_Clear( pBrushIdsOut );
            return geometry_status_t::CORRUPT_STATE;
        }
        stack[cStack++] = node.iRight;
        stack[cStack++] = node.iLeft;
    }
    common::Sort_Unstable(
        common::span_t<geometry_source_id_t>{ pBrushIdsOut->pData, pBrushIdsOut->nCount },
        id_less_t{} );
    return geometry_status_t::OK;
}

geometry_status_t SpatialIndex_TryPick(
    const geometry_spatial_index_t *pIndex,
    const geometry_numerical_policy_t &policy,
    vec3d_t origin,
    vec3d_t direction,
    f64 maxT,
    geometry_spatial_pick_t *pPickOut ) noexcept
{
    if ( pPickOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pPickOut = {};
    if ( pIndex == nullptr || !pIndex->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Vec3d_IsFinite( origin ) || !math::Vec3d_IsFinite( direction ) ||
         !math::Scalar_IsFinite( maxT ) || maxT < 0.0 ||
         math::Vec3d_LengthSquared( direction ) == 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( common::Vector_Count( &pIndex->nodes ) == 0u ) {
        return geometry_status_t::OK;
    }

    f64 bestT = maxT;
    u32 stack[cTraversalStack];
    usize cStack = 0u;
    stack[cStack++] = 0u;
    while ( cStack > 0u ) {
        const geometry_spatial_node_t &node = pIndex->nodes.pData[stack[--cStack]];
        if ( !RayHitsBox( node.bounds, origin, direction, bestT ) ) {
            continue;
        }
        if ( node.cLeaves == 0u ) {
            if ( cStack + 2u > cTraversalStack ) {
                *pPickOut = {};
                return geometry_status_t::CORRUPT_STATE;
            }
            stack[cStack++] = node.iRight;
            stack[cStack++] = node.iLeft;
            continue;
        }
        for ( u32 i = 0u; i < node.cLeaves; ++i ) {
            const geometry_snapshot_brush_t &entry =
                pIndex->pSnapshot->pBrushes[pIndex->leafOrder.pData[node.iFirst + i]];
            geometry_brush_ray_hit_t hit{};
            const geometry_status_t status = BrushQuery_TryRaycast(
                &entry.pValue->brush, policy, origin, direction, bestT, &hit );
            if ( status != geometry_status_t::OK ) {
                *pPickOut = {};
                return status;
            }
            if ( !hit.bHit || hit.bStartsInside ) {
                continue;
            }
            const bool_t bCloser = !pPickOut->bHit || hit.t < pPickOut->t ||
                                   ( hit.t == pPickOut->t &&
                                     entry.brushId.value < pPickOut->brushId.value );
            if ( bCloser ) {
                pPickOut->bHit = true;
                pPickOut->brushId = entry.brushId;
                pPickOut->sideId = entry.pValue->brush.sides.pData[hit.iSide].sourceId;
                pPickOut->t = hit.t;
                pPickOut->point = hit.point;
                pPickOut->normal = hit.normal;
                bestT = hit.t;
            }
        }
    }
    return geometry_status_t::OK;
}

bool_t SpatialIndex_ValidateDeep( const geometry_spatial_index_t *pIndex ) noexcept
{
    if ( pIndex == nullptr || !pIndex->bInitialized ) {
        return false;
    }
    const usize cBrushes = pIndex->pSnapshot == nullptr ? 0u : pIndex->pSnapshot->cBrushes;
    const usize cNodes = common::Vector_Count( &pIndex->nodes );
    if ( common::Vector_Count( &pIndex->leafOrder ) != cBrushes ||
         ( cBrushes == 0u ) != ( cNodes == 0u ) ) {
        return false;
    }

    // Every entry index appears exactly once across leaves.
    usize cCovered = 0u;
    for ( usize i = 0u; i < cNodes; ++i ) {
        const geometry_spatial_node_t &node = pIndex->nodes.pData[i];
        if ( node.cLeaves > 0u ) {
            if ( node.iFirst > cBrushes || node.cLeaves > cBrushes - node.iFirst ) {
                return false;
            }
            for ( u32 k = 0u; k < node.cLeaves; ++k ) {
                const u32 iEntry = pIndex->leafOrder.pData[node.iFirst + k];
                if ( iEntry >= cBrushes ||
                     !math::Aabbd_ContainsAabb( node.bounds, LeafBounds( pIndex->pSnapshot, iEntry ) ) ) {
                    return false;
                }
            }
            cCovered += node.cLeaves;
        } else {
            if ( node.iLeft <= i || node.iRight <= i || node.iLeft >= cNodes ||
                 node.iRight >= cNodes ||
                 !math::Aabbd_ContainsAabb( node.bounds, pIndex->nodes.pData[node.iLeft].bounds ) ||
                 !math::Aabbd_ContainsAabb( node.bounds, pIndex->nodes.pData[node.iRight].bounds ) ) {
                return false;
            }
        }
    }
    if ( cCovered != cBrushes ) {
        return false;
    }
    common::vector_t<common::u8> seen{};
    if ( !common::Vector_Init( &seen, pIndex->pAllocator, cBrushes ) ||
         !common::Vector_Resize( &seen, cBrushes ) ) {
        return false;
    }
    for ( usize i = 0u; i < cBrushes; ++i ) {
        seen.pData[i] = 0u;
    }
    for ( usize i = 0u; i < cBrushes; ++i ) {
        common::u8 &mark = seen.pData[pIndex->leafOrder.pData[i]];
        if ( mark != 0u ) {
            return false;
        }
        mark = 1u;
    }
    return true;
}

} // namespace cypher::editor::geometry
