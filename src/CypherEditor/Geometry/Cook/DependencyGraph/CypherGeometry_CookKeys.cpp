//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookKeys.cpp
//  Purpose: Implements cook source hashing, product keys, and key diffs.
//  Details: Mesh sources hash their canonical description, not their pool
//           contents, so two meshes that are the same authored mesh hash
//           identically even if one was rebuilt through undo or load and has
//           a different slot layout.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CookKeys.h"

#include "CypherGeometry_CookBytes.h"

#include <algorithm>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

void EncodeBrush( cook_byte_writer_t *pW, const brush_solid_t *pBrush ) noexcept
{
    CookBytes_U64( pW, pBrush->sourceId.value );
    CookBytes_U64( pW, pBrush->sides.nCount );
    for ( usize i = 0u; i < pBrush->sides.nCount; ++i ) {
        const brush_solid_side_t &side = pBrush->sides.pData[i];
        CookBytes_U64( pW, side.sourceId.value );
        CookBytes_F64( pW, side.plane.normal.x );
        CookBytes_F64( pW, side.plane.normal.y );
        CookBytes_F64( pW, side.plane.normal.z );
        CookBytes_F64( pW, side.plane.d );
        CookBytes_U32( pW, side.iAttributeIndex );
    }
}

void EncodeMesh( cook_byte_writer_t *pW, const mesh_source_description_t &d ) noexcept
{
    CookBytes_U64( pW, d.sourceId.value );
    CookBytes_U64( pW, d.vertices.nCount );
    for ( usize i = 0u; i < d.vertices.nCount; ++i ) {
        CookBytes_U64( pW, d.vertices.pData[i].sourceId.value );
        CookBytes_F64( pW, d.vertices.pData[i].position.x );
        CookBytes_F64( pW, d.vertices.pData[i].position.y );
        CookBytes_F64( pW, d.vertices.pData[i].position.z );
    }
    CookBytes_U64( pW, d.faces.nCount );
    for ( usize i = 0u; i < d.faces.nCount; ++i ) {
        const mesh_source_face_t &f = d.faces.pData[i];
        CookBytes_U64( pW, f.sourceId.value );
        CookBytes_U32( pW, f.cCorners );
        CookBytes_U64( pW, f.attributes.material.value );
        CookBytes_U32( pW, f.attributes.smoothingGroups );
        for ( u32 k = 0u; k < f.cCorners; ++k ) {
            const mesh_source_corner_t &c = d.corners.pData[f.iFirstCorner + k];
            CookBytes_U32( pW, c.iVertex );
            CookBytes_F64( pW, c.attributes.uv0.x );
            CookBytes_F64( pW, c.attributes.uv0.y );
            CookBytes_F64( pW, c.attributes.uv1.x );
            CookBytes_F64( pW, c.attributes.uv1.y );
            CookBytes_U32( pW, c.attributes.colorRgba );
        }
    }
    CookBytes_U64( pW, d.edges.nCount );
    for ( usize i = 0u; i < d.edges.nCount; ++i ) {
        const mesh_source_edge_t &e = d.edges.pData[i];
        CookBytes_U32( pW, e.iVertexA );
        CookBytes_U32( pW, e.iVertexB );
        CookBytes_U32( pW, e.attributes.flags );
        CookBytes_F64( pW, e.creaseWeight );
    }
}

content_hash_t HashU64s( const u64 *pValues, usize cValues ) noexcept
{
    // Small fixed encodings (product keys) use a stack buffer: no
    // allocation on the hot path of incremental cooks.
    byte buffer[8u * 8u];
    usize cb = 0u;
    for ( usize i = 0u; i < cValues && cb + 8u <= sizeof( buffer ); ++i ) {
        for ( int b = 0; b < 8; ++b ) { buffer[cb++] = static_cast<byte>( ( pValues[i] >> ( 8 * b ) ) & 0xFFu ); }
    }
    return ContentHash_Data( binary_block_t{ buffer, cb } );
}

} // namespace

content_hash_t CookKeys_PolicyHash( const geometry_policy_t &policy ) noexcept
{
    const geometry_numerical_policy_t &n = policy.numerical;
    const geometry_limit_policy_t &l = policy.limits;
    const f64 floats[] = { n.fCoordinateMagnitudeLimit, n.fAbsoluteDistanceTolerance, n.fRelativeDistanceTolerance,
                           n.fMinimumEdgeLength,        n.fMinimumFaceArea,           n.fAngularToleranceRadians,
                           n.fPlanarityTolerance,       n.fCoplanarDistanceTolerance, n.fUnitNormalTolerance,
                           n.fSnapDistance,             n.fWeldDistance,              n.fCanonicalQuantization };
    const u64 limits[] = { l.cBrushesMax,      l.cBrushSidesMax,         l.cBrushSidesPerBrushMax, l.cVerticesMax,
                           l.cHalfEdgesMax,    l.cEdgesMax,              l.cLoopsMax,              l.cFacesMax,
                           l.cShellsMax,       l.cIntersectionEventsMax, l.cJournalRecordsMax,     l.cDiagnosticsMax,
                           l.cTraversalDepthMax, l.cbScratchMax };
    byte buffer[( sizeof( floats ) / sizeof( floats[0] ) + sizeof( limits ) / sizeof( limits[0] ) ) * 8u];
    usize cb = 0u;
    auto put = [&]( u64 v ) noexcept {
        for ( int b = 0; b < 8; ++b ) { buffer[cb++] = static_cast<byte>( ( v >> ( 8 * b ) ) & 0xFFu ); }
    };
    for ( const f64 f : floats ) {
        u64 bits = 0u;
        std::memcpy( &bits, &f, sizeof( bits ) );
        put( bits );
    }
    for ( const u64 v : limits ) { put( v ); }
    return ContentHash_Data( binary_block_t{ buffer, cb } );
}

geometry_status_t CookKeySet_Init( cook_key_set_t *pSet, const allocator_t *pAllocator ) noexcept
{
    if ( pSet == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( pSet->keys.pAllocator != nullptr ) { return geometry_status_t::ALREADY_INITIALIZED; }
    return Vector_Init( &pSet->keys, pAllocator ) ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
}

void CookKeySet_Shutdown( cook_key_set_t *pSet ) noexcept
{
    if ( pSet == nullptr ) { return; }
    Vector_Shutdown( &pSet->keys );
    pSet->policyHash = CY_CONTENT_HASH_INVALID;
    pSet->revision = GEOMETRY_REVISION_INITIAL;
}

geometry_status_t CookKeySet_TryBuild( cook_key_set_t *pSet, const geometry_snapshot_t *pSnapshot ) noexcept
{
    if ( pSet == nullptr || pSet->keys.pAllocator == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !GeometrySnapshot_IsInitialized( pSnapshot ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    const allocator_t *pAllocator = pSet->keys.pAllocator;
    Vector_Clear( &pSet->keys );
    const usize cBrushes = GeometrySnapshot_BrushCount( pSnapshot );
    const usize cMeshes = GeometrySnapshot_MeshCount( pSnapshot );
    if ( !Vector_Reserve( &pSet->keys, cBrushes + cMeshes ) ) { return geometry_status_t::ALLOCATION_FAILED; }

    cook_byte_writer_t w{};
    mesh_source_description_t desc{};
    if ( !CookBytes_Init( &w, pAllocator ) ||
         MeshSourceDescription_Init( &desc, pAllocator, GEOMETRY_SOURCE_ID_INVALID ) != geometry_status_t::OK ) {
        CookBytes_Shutdown( &w );
        MeshSourceDescription_Shutdown( &desc );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t st = geometry_status_t::OK;
    for ( usize i = 0u; i < cBrushes && st == geometry_status_t::OK; ++i ) {
        const brush_solid_t *pBrush = pSnapshot->brushes.pData[i];
        CookBytes_Clear( &w );
        EncodeBrush( &w, pBrush );
        if ( !w.bOk ) {
            st = geometry_status_t::ALLOCATION_FAILED;
            break;
        }
        (void)Vector_PushBack( &pSet->keys,
                               cook_source_key_t{ pBrush->sourceId, cook_source_kind_t::BRUSH, CookBytes_Hash( &w ) } );
    }
    for ( usize i = 0u; i < cMeshes && st == geometry_status_t::OK; ++i ) {
        const mesh_source_t *pMesh = GeometrySnapshot_MeshAt( pSnapshot, i );
        st = MeshSource_TryDescribe( pMesh, &desc );
        if ( st != geometry_status_t::OK ) { break; }
        CookBytes_Clear( &w );
        EncodeMesh( &w, desc );
        if ( !w.bOk ) {
            st = geometry_status_t::ALLOCATION_FAILED;
            break;
        }
        (void)Vector_PushBack( &pSet->keys,
                               cook_source_key_t{ pMesh->sourceId, cook_source_kind_t::MESH, CookBytes_Hash( &w ) } );
    }
    CookBytes_Shutdown( &w );
    MeshSourceDescription_Shutdown( &desc );
    if ( st != geometry_status_t::OK ) {
        Vector_Clear( &pSet->keys );
        return st;
    }
    std::sort( pSet->keys.pData, pSet->keys.pData + pSet->keys.nCount,
               []( const cook_source_key_t &a, const cook_source_key_t &b ) { return a.sourceId.value < b.sourceId.value; } );
    pSet->policyHash = CookKeys_PolicyHash( pSnapshot->policy );
    pSet->revision = GeometrySnapshot_GetRevision( pSnapshot );
    return geometry_status_t::OK;
}

content_hash_t CookKeys_ProductKey( const cook_key_set_t *pSet, const cook_source_key_t &source, cook_product_kind_t product ) noexcept
{
    if ( pSet == nullptr ) { return CY_CONTENT_HASH_INVALID; }
    const u64 values[] = { kCookKeysVersion,
                           static_cast<u64>( source.kind ),
                           static_cast<u64>( product ),
                           pSet->policyHash.low,
                           pSet->policyHash.high,
                           source.sourceHash.low,
                           source.sourceHash.high,
                           source.sourceId.value };
    return HashU64s( values, sizeof( values ) / sizeof( values[0] ) );
}

const cook_source_key_t *CookKeySet_Find( const cook_key_set_t *pSet, geometry_source_id_t sourceId ) noexcept
{
    if ( pSet == nullptr || pSet->keys.pData == nullptr ) { return nullptr; }
    const cook_source_key_t *pBegin = pSet->keys.pData, *pEnd = pBegin + pSet->keys.nCount;
    const cook_source_key_t *p = std::lower_bound(
        pBegin, pEnd, sourceId.value, []( const cook_source_key_t &k, u64 v ) { return k.sourceId.value < v; } );
    return ( p != pEnd && p->sourceId.value == sourceId.value ) ? p : nullptr;
}

geometry_status_t CookKeyDiff_Init( cook_key_diff_t *pDiff, const allocator_t *pAllocator ) noexcept
{
    if ( pDiff == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Init( &pDiff->added, pAllocator ) || !Vector_Init( &pDiff->removed, pAllocator ) ||
         !Vector_Init( &pDiff->changed, pAllocator ) || !Vector_Init( &pDiff->unchanged, pAllocator ) ) {
        CookKeyDiff_Shutdown( pDiff );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pDiff->bPolicyChanged = false;
    return geometry_status_t::OK;
}

void CookKeyDiff_Shutdown( cook_key_diff_t *pDiff ) noexcept
{
    if ( pDiff == nullptr ) { return; }
    Vector_Shutdown( &pDiff->added );
    Vector_Shutdown( &pDiff->removed );
    Vector_Shutdown( &pDiff->changed );
    Vector_Shutdown( &pDiff->unchanged );
}

geometry_status_t CookKeyDiff_TryCompute( const cook_key_set_t *pPrevious, const cook_key_set_t *pNext, cook_key_diff_t *pDiff ) noexcept
{
    if ( pPrevious == nullptr || pNext == nullptr || pDiff == nullptr || pDiff->added.pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    Vector_Clear( &pDiff->added );
    Vector_Clear( &pDiff->removed );
    Vector_Clear( &pDiff->changed );
    Vector_Clear( &pDiff->unchanged );
    pDiff->bPolicyChanged = !ContentHash_Equals( pPrevious->policyHash, pNext->policyHash );
    usize i = 0u, j = 0u;
    bool bOk = true;
    const usize cA = pPrevious->keys.nCount, cB = pNext->keys.nCount;
    while ( bOk && ( i < cA || j < cB ) ) {
        const cook_source_key_t *a = i < cA ? &pPrevious->keys.pData[i] : nullptr;
        const cook_source_key_t *b = j < cB ? &pNext->keys.pData[j] : nullptr;
        if ( b == nullptr || ( a != nullptr && a->sourceId.value < b->sourceId.value ) ) {
            bOk = Vector_PushBack( &pDiff->removed, a->sourceId );
            ++i;
        } else if ( a == nullptr || b->sourceId.value < a->sourceId.value ) {
            bOk = Vector_PushBack( &pDiff->added, b->sourceId );
            ++j;
        } else {
            // Same ID but a different kind is a replacement, not an edit.
            const bool bSame = !pDiff->bPolicyChanged && a->kind == b->kind &&
                               ContentHash_Equals( a->sourceHash, b->sourceHash );
            bOk = Vector_PushBack( bSame ? &pDiff->unchanged : &pDiff->changed, b->sourceId );
            ++i;
            ++j;
        }
    }
    return bOk ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
}

} // namespace cypher::editor::geometry
