//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgOperations.cpp
//  Purpose: Implements the document-level Boolean commands.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgOperations.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentBrushReplacement.h"
#include "CypherGeometry_DocumentMeshSet.h"
#include "CypherGeometry_DocumentMeshes.h"

#include <algorithm>
#include <new>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// Gives every missing ID in the description a fresh one.
geometry_status_t AssignIds( mesh_source_description_t *pDesc, geometry_source_id_allocator_t *pIds ) noexcept
{
    auto fresh = [&]( geometry_source_id_t *pId ) noexcept -> geometry_status_t {
        if ( GeometrySourceId_IsValid( *pId ) ) { return geometry_status_t::OK; }
        const geometry_source_id_result_t r = GeometrySourceIdAllocator_Allocate( pIds );
        *pId = r.id;
        return r.status;
    };
    geometry_status_t st = fresh( &pDesc->sourceId );
    for ( usize v = 0u; st == geometry_status_t::OK && v < pDesc->vertices.nCount; ++v ) { st = fresh( &pDesc->vertices.pData[v].sourceId ); }
    for ( usize f = 0u; st == geometry_status_t::OK && f < pDesc->faces.nCount; ++f ) { st = fresh( &pDesc->faces.pData[f].sourceId ); }
    return st;
}

// Drops the IDs the result took from operand B (vertices B contributed,
// faces cut from B), for when B stays in the document.
void ForgetOperandIds( csg_mesh_result_t *pR, u32 iOperand ) noexcept
{
    for ( usize v = 0u; v < pR->mesh.vertices.nCount; ++v ) {
        const csg_point_key_t &k = pR->vertexKeys.pData[v];
        if ( k.kind == csg_point_kind_t::VERTEX && k.a == iOperand ) { pR->mesh.vertices.pData[v].sourceId = GEOMETRY_SOURCE_ID_INVALID; }
    }
    for ( usize f = 0u; f < pR->mesh.faces.nCount; ++f ) {
        if ( pR->faceOperand.pData[f] == iOperand ) { pR->mesh.faces.pData[f].sourceId = GEOMETRY_SOURCE_ID_INVALID; }
    }
}

// Drops IDs in `later` that `earlier` already uses.
geometry_status_t ForgetShared( const mesh_source_description_t &earlier, mesh_source_description_t *pLater, const allocator_t *pA ) noexcept
{
    vector_t<u64> used{};
    if ( !Vector_Init( &used, pA ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize v = 0u; v < earlier.vertices.nCount; ++v ) {
        if ( GeometrySourceId_IsValid( earlier.vertices.pData[v].sourceId ) && !Vector_PushBack( &used, earlier.vertices.pData[v].sourceId.value ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    for ( usize f = 0u; f < earlier.faces.nCount; ++f ) {
        if ( GeometrySourceId_IsValid( earlier.faces.pData[f].sourceId ) && !Vector_PushBack( &used, earlier.faces.pData[f].sourceId.value ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    std::sort( used.pData, used.pData + used.nCount );
    auto clash = [&]( geometry_source_id_t id ) noexcept { return GeometrySourceId_IsValid( id ) && std::binary_search( used.pData, used.pData + used.nCount, id.value ); };
    for ( usize v = 0u; v < pLater->vertices.nCount; ++v ) {
        if ( clash( pLater->vertices.pData[v].sourceId ) ) { pLater->vertices.pData[v].sourceId = GEOMETRY_SOURCE_ID_INVALID; }
    }
    for ( usize f = 0u; f < pLater->faces.nCount; ++f ) {
        if ( clash( pLater->faces.pData[f].sourceId ) ) { pLater->faces.pData[f].sourceId = GEOMETRY_SOURCE_ID_INVALID; }
    }
    return geometry_status_t::OK;
}

// The document's mesh order with `replace` swapped for `with` (up to two
// roots, invalid ones skipped) and `drop` left out.
geometry_status_t FinalOrder( const geometry_document_t *pDoc, geometry_source_id_t replace, const geometry_source_id_t *pWith, u32 cWith,
                              geometry_source_id_t drop, vector_t<geometry_source_id_t> *pOut ) noexcept
{
    for ( usize i = 0u; i < GeometryDocument_MeshCount( pDoc ); ++i ) {
        const geometry_source_id_t id = GeometryDocument_MeshAt( pDoc, i )->sourceId;
        if ( id.value == drop.value ) { continue; }
        if ( id.value == replace.value ) {
            for ( u32 k = 0u; k < cWith; ++k ) {
                if ( GeometrySourceId_IsValid( pWith[k] ) && !Vector_PushBack( pOut, pWith[k] ) ) { return geometry_status_t::ALLOCATION_FAILED; }
            }
            continue;
        }
        if ( !Vector_PushBack( pOut, id ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    }
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t GeometryDocument_TryCsgMeshes( geometry_document_t *pDoc, geometry_source_id_t meshA, geometry_source_id_t meshB,
                                                 const csg_document_options_t &options, geometry_source_id_t *pResultIdOut, csg_diagnostics_t *pDiag ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDoc ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const mesh_source_t *pA = GeometryDocument_FindMesh( pDoc, meshA );
    const mesh_source_t *pB = GeometryDocument_FindMesh( pDoc, meshB );
    if ( pA == nullptr || pB == nullptr ) { return geometry_status_t::INVALID_HANDLE; }
    if ( meshA.value == meshB.value ) { return geometry_status_t::INVALID_ARGUMENT; }
    const allocator_t *pAlloc = pDoc->pAllocator;
    csg_mesh_result_t r{};
    geometry_status_t st = CsgResult_Init( &r, pAlloc );
    if ( st == geometry_status_t::OK ) { st = CsgMesh_TryEvaluate( pA, pB, options.mesh, &r, pDiag ); }
    geometry_source_id_allocator_t ids = pDoc->sourceIds.allocator;
    const bool bEmpty = st == geometry_status_t::OK && r.mesh.faces.nCount == 0u;
    if ( st == geometry_status_t::OK && !bEmpty ) {
        if ( options.bKeepB ) { ForgetOperandIds( &r, kCsgOperandB ); }
        r.mesh.sourceId = meshA;
        st = AssignIds( &r.mesh, &ids );
    }
    vector_t<geometry_source_id_t> order{};
    if ( st == geometry_status_t::OK && !Vector_Init( &order, pAlloc ) ) { st = geometry_status_t::ALLOCATION_FAILED; }
    const geometry_source_id_t with = bEmpty ? GEOMETRY_SOURCE_ID_INVALID : meshA;
    if ( st == geometry_status_t::OK ) { st = FinalOrder( pDoc, meshA, &with, 1u, options.bKeepB ? GEOMETRY_SOURCE_ID_INVALID : meshB, &order ); }
    if ( st == geometry_status_t::OK ) {
        const geometry_source_id_t remove[2] = { meshA, meshB };
        const mesh_source_description_t *replacement[1] = { &r.mesh };
        st = GeometryDocument_TryPublishMeshSetExact( pDoc, span_t<const geometry_source_id_t>{ remove, options.bKeepB ? 1u : 2u },
                                                      span_t<const mesh_source_description_t *const>{ replacement, bEmpty ? 0u : 1u },
                                                      Vector_Span( static_cast<const vector_t<geometry_source_id_t> *>( &order ) ) );
    }
    if ( st == geometry_status_t::OK && pResultIdOut != nullptr ) { *pResultIdOut = with; }
    CsgResult_Shutdown( &r );
    return st;
}

geometry_status_t GeometryDocument_TryCsgSplitMesh( geometry_document_t *pDoc, geometry_source_id_t meshA, geometry_source_id_t meshB,
                                                    const csg_mesh_options_t &options, geometry_source_id_t *pInsideIdOut, geometry_source_id_t *pOutsideIdOut,
                                                    csg_diagnostics_t *pDiag ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDoc ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const mesh_source_t *pA = GeometryDocument_FindMesh( pDoc, meshA );
    const mesh_source_t *pB = GeometryDocument_FindMesh( pDoc, meshB );
    if ( pA == nullptr || pB == nullptr ) { return geometry_status_t::INVALID_HANDLE; }
    if ( meshA.value == meshB.value ) { return geometry_status_t::INVALID_ARGUMENT; }
    const allocator_t *pAlloc = pDoc->pAllocator;
    csg_mesh_result_t inside{}, outside{};
    csg_mesh_options_t o = options;
    geometry_status_t st = CsgResult_Init( &inside, pAlloc );
    if ( st == geometry_status_t::OK ) { st = CsgResult_Init( &outside, pAlloc ); }
    o.op = csg_operator_t::INTERSECTION;
    if ( st == geometry_status_t::OK ) { st = CsgMesh_TryEvaluate( pA, pB, o, &inside, pDiag ); }
    o.op = csg_operator_t::DIFFERENCE;
    if ( st == geometry_status_t::OK ) { st = CsgMesh_TryEvaluate( pA, pB, o, &outside, pDiag ); }
    // B stays, so neither part may keep B's IDs; the inside part keeps A's
    // root and the A IDs it has, the outside part only those left over.
    const bool bIn = st == geometry_status_t::OK && inside.mesh.faces.nCount > 0u;
    const bool bOut = st == geometry_status_t::OK && outside.mesh.faces.nCount > 0u;
    geometry_source_id_allocator_t ids = pDoc->sourceIds.allocator;
    if ( st == geometry_status_t::OK ) {
        ForgetOperandIds( &inside, kCsgOperandB );
        ForgetOperandIds( &outside, kCsgOperandB );
        inside.mesh.sourceId = bIn ? meshA : GEOMETRY_SOURCE_ID_INVALID;
        outside.mesh.sourceId = bIn ? GEOMETRY_SOURCE_ID_INVALID : meshA;
        if ( bIn ) { st = ForgetShared( inside.mesh, &outside.mesh, pAlloc ); }
    }
    if ( st == geometry_status_t::OK && bIn ) { st = AssignIds( &inside.mesh, &ids ); }
    if ( st == geometry_status_t::OK && bOut ) { st = AssignIds( &outside.mesh, &ids ); }
    vector_t<geometry_source_id_t> order{};
    const geometry_source_id_t with[2] = { bIn ? inside.mesh.sourceId : GEOMETRY_SOURCE_ID_INVALID, bOut ? outside.mesh.sourceId : GEOMETRY_SOURCE_ID_INVALID };
    if ( st == geometry_status_t::OK && !Vector_Init( &order, pAlloc ) ) { st = geometry_status_t::ALLOCATION_FAILED; }
    if ( st == geometry_status_t::OK ) { st = FinalOrder( pDoc, meshA, with, 2u, GEOMETRY_SOURCE_ID_INVALID, &order ); }
    if ( st == geometry_status_t::OK ) {
        const mesh_source_description_t *replacement[2]{};
        u32 cReplacement = 0u;
        if ( bIn ) { replacement[cReplacement++] = &inside.mesh; }
        if ( bOut ) { replacement[cReplacement++] = &outside.mesh; }
        st = GeometryDocument_TryPublishMeshSetExact( pDoc, span_t<const geometry_source_id_t>{ &meshA, 1u },
                                                      span_t<const mesh_source_description_t *const>{ replacement, cReplacement },
                                                      Vector_Span( static_cast<const vector_t<geometry_source_id_t> *>( &order ) ) );
    }
    if ( st == geometry_status_t::OK ) {
        if ( pInsideIdOut != nullptr ) { *pInsideIdOut = with[0]; }
        if ( pOutsideIdOut != nullptr ) { *pOutsideIdOut = with[1]; }
    }
    CsgResult_Shutdown( &inside );
    CsgResult_Shutdown( &outside );
    return st;
}

geometry_status_t GeometryDocument_TryCsgBrushes( geometry_document_t *pDoc, csg_operator_t op, span_t<const geometry_source_id_t> brushesA,
                                                  span_t<const geometry_source_id_t> brushesB, bool bKeepB, vector_t<geometry_source_id_t> *pNewBrushIdsOut ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDoc ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( ( brushesA.nCount > 0u && brushesA.pData == nullptr ) || ( brushesB.nCount > 0u && brushesB.pData == nullptr ) || brushesA.nCount == 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pNewBrushIdsOut != nullptr && pNewBrushIdsOut->pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const allocator_t *pAlloc = pDoc->pAllocator;
    const usize cIn = brushesA.nCount + brushesB.nCount;
    // Every operand named once, and present.
    for ( usize i = 0u; i < cIn; ++i ) {
        const geometry_source_id_t id = i < brushesA.nCount ? brushesA.pData[i] : brushesB.pData[i - brushesA.nCount];
        if ( GeometryDocument_FindBrush( pDoc, id ) == nullptr ) { return geometry_status_t::INVALID_HANDLE; }
        for ( usize j = i + 1u; j < cIn; ++j ) {
            const geometry_source_id_t other = j < brushesA.nCount ? brushesA.pData[j] : brushesB.pData[j - brushesA.nCount];
            if ( other.value == id.value ) { return geometry_status_t::INVALID_ARGUMENT; }
        }
    }
    // Authored copies of the operands (solid + records).
    void *pMem = Allocator_AllocateZeroed( pAlloc, sizeof( brush_source_t ) * cIn, alignof( brush_source_t ) );
    if ( pMem == nullptr ) { return geometry_status_t::ALLOCATION_FAILED; }
    brush_source_t *pSources = static_cast<brush_source_t *>( pMem );
    for ( usize i = 0u; i < cIn; ++i ) { new ( &pSources[i] ) brush_source_t{}; }
    vector_t<const brush_source_t *> ptrs{};
    geometry_status_t st = Vector_Init( &ptrs, pAlloc ) && Vector_Resize( &ptrs, cIn ) ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
    for ( usize i = 0u; st == geometry_status_t::OK && i < cIn; ++i ) {
        const geometry_source_id_t id = i < brushesA.nCount ? brushesA.pData[i] : brushesB.pData[i - brushesA.nCount];
        st = GeometryDocument_TryCopyBrushSource( pDoc, id, pAlloc, &pSources[i] );
        ptrs.pData[i] = &pSources[i];
    }
    geometry_fragment_t frag{};
    if ( st == geometry_status_t::OK ) { st = GeometryFragment_Init( &frag, pAlloc ); }
    geometry_source_id_allocator_t ids = pDoc->sourceIds.allocator;
    if ( st == geometry_status_t::OK ) {
        st = CsgBrush_TryEvaluate( op, span_t<const brush_source_t *const>{ ptrs.pData, brushesA.nCount },
                                   span_t<const brush_source_t *const>{ ptrs.pData + brushesA.nCount, brushesB.nCount }, pDoc->policy, &ids, &frag, nullptr );
    }
    // Contiguous result array for the atomic replacement.
    const usize cOut = frag.brushes.nCount;
    brush_source_t *pOut = nullptr;
    void *pOutMem = nullptr;
    if ( st == geometry_status_t::OK && cOut > 0u ) {
        pOutMem = Allocator_AllocateZeroed( pAlloc, sizeof( brush_source_t ) * cOut, alignof( brush_source_t ) );
        if ( pOutMem == nullptr ) { st = geometry_status_t::ALLOCATION_FAILED; }
        pOut = static_cast<brush_source_t *>( pOutMem );
        for ( usize i = 0u; st == geometry_status_t::OK && i < cOut; ++i ) { new ( &pOut[i] ) brush_source_t{}; }
        for ( usize i = 0u; st == geometry_status_t::OK && i < cOut; ++i ) { st = BrushSource_TryClone( frag.brushes.pData[i], pAlloc, pDoc->policy, &pOut[i] ); }
    }
    if ( st == geometry_status_t::OK && pNewBrushIdsOut != nullptr && !Vector_Reserve( pNewBrushIdsOut, pNewBrushIdsOut->nCount + cOut ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    if ( st == geometry_status_t::OK ) {
        vector_t<geometry_source_id_t> remove{};
        st = Vector_Init( &remove, pAlloc ) ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
        for ( usize i = 0u; st == geometry_status_t::OK && i < ( bKeepB ? brushesA.nCount : cIn ); ++i ) {
            if ( !Vector_PushBack( &remove, i < brushesA.nCount ? brushesA.pData[i] : brushesB.pData[i - brushesA.nCount] ) ) {
                st = geometry_status_t::ALLOCATION_FAILED;
            }
        }
        if ( st == geometry_status_t::OK ) {
            st = GeometryDocument_TryReplaceBrushSourcesExact( pDoc, Vector_Span( static_cast<const vector_t<geometry_source_id_t> *>( &remove ) ),
                                                               span_t<const brush_source_t>{ pOut, cOut } );
        }
    }
    if ( st == geometry_status_t::OK && pNewBrushIdsOut != nullptr ) {
        for ( usize i = 0u; i < cOut; ++i ) { (void)Vector_PushBack( pNewBrushIdsOut, pOut[i].solid.sourceId ); }
    }
    for ( usize i = 0u; pOut != nullptr && i < cOut; ++i ) {
        BrushSource_Shutdown( &pOut[i] );
        pOut[i].~brush_source_t();
    }
    if ( pOutMem != nullptr ) { Allocator_Free( pAlloc, pOutMem, sizeof( brush_source_t ) * cOut, alignof( brush_source_t ) ); }
    GeometryFragment_Shutdown( &frag );
    for ( usize i = 0u; i < cIn; ++i ) {
        BrushSource_Shutdown( &pSources[i] );
        pSources[i].~brush_source_t();
    }
    Allocator_Free( pAlloc, pMem, sizeof( brush_source_t ) * cIn, alignof( brush_source_t ) );
    return st;
}

} // namespace cypher::editor::geometry
