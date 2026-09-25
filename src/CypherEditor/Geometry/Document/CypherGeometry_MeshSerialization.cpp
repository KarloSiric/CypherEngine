//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSerialization.cpp
//  Purpose: Implements writing and reading the schema-3 "meshes" section.
//  Details: Reading validates channel lengths against the counts implied by
//           the required arrays before building anything, so a truncated or
//           mismatched file fails with the offending field name instead of
//           surfacing later as a topology error.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSerialization.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

string_view_t Sv( const char *pText ) noexcept
{
    return string_view_t{ pText, std::strlen( pText ) };
}

geometry_serialization_result_t Fail(
    geometry_serialization_status_t status,
    const char *pField = nullptr,
    usize iElement = CY_INVALID_SIZE ) noexcept
{
    geometry_serialization_result_t r{};
    r.status = status;
    if ( pField != nullptr ) {
        std::strncpy( r.field, pField, sizeof( r.field ) - 1u );
        r.field[sizeof( r.field ) - 1u] = '\0';
    }
    r.iElement = iElement;
    return r;
}

// ---------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------

struct writer_t {
    key_value_document_t *pDoc;
    bool bOk{ true };

    key_value_t *Array( key_value_t *pParent, const char *pName ) noexcept
    {
        key_value_t *p = bOk ? KeyValue_ObjectInsert( pDoc, pParent, Sv( pName ), key_value_type_t::ARRAY ) : nullptr;
        bOk = bOk && p != nullptr;
        return p;
    }
    void U64( key_value_t *pArray, u64 v ) noexcept
    {
        if ( !bOk ) { return; }
        key_value_t *p = KeyValue_ArrayAppend( pDoc, pArray, key_value_type_t::U64 );
        bOk = p != nullptr && KeyValue_SetU64( pDoc, p, v );
    }
    void F64( key_value_t *pArray, f64 v ) noexcept
    {
        if ( !bOk ) { return; }
        key_value_t *p = KeyValue_ArrayAppend( pDoc, pArray, key_value_type_t::F64 );
        bOk = p != nullptr && KeyValue_SetF64( pDoc, p, v );
    }
};

bool WriteMesh( writer_t &w, key_value_t *pMeshes, const mesh_source_description_t &d ) noexcept
{
    key_value_t *pMesh = KeyValue_ArrayAppend( w.pDoc, pMeshes, key_value_type_t::OBJECT );
    if ( pMesh == nullptr ) { return false; }
    key_value_t *pId = KeyValue_ObjectInsert( w.pDoc, pMesh, Sv( "source_id" ), key_value_type_t::U64 );
    if ( pId == nullptr || !KeyValue_SetU64( w.pDoc, pId, d.sourceId.value ) ) { return false; }

    key_value_t *pVertexIds = w.Array( pMesh, "vertex_ids" );
    key_value_t *pPositions = w.Array( pMesh, "positions" );
    for ( usize i = 0u; w.bOk && i < d.vertices.nCount; ++i ) {
        const mesh_source_vertex_t &v = d.vertices.pData[i];
        w.U64( pVertexIds, v.sourceId.value );
        w.F64( pPositions, v.position.x );
        w.F64( pPositions, v.position.y );
        w.F64( pPositions, v.position.z );
    }

    key_value_t *pFaceIds = w.Array( pMesh, "face_ids" );
    key_value_t *pCounts = w.Array( pMesh, "face_corner_counts" );
    key_value_t *pCorners = w.Array( pMesh, "corners" );
    bool bMaterials = false, bSmoothing = false;
    const mesh_face_attributes_t faceDefault{};
    for ( usize i = 0u; w.bOk && i < d.faces.nCount; ++i ) {
        const mesh_source_face_t &f = d.faces.pData[i];
        w.U64( pFaceIds, f.sourceId.value );
        w.U64( pCounts, f.cCorners );
        bMaterials = bMaterials || f.attributes.material.value != faceDefault.material.value;
        bSmoothing = bSmoothing || f.attributes.smoothingGroups != faceDefault.smoothingGroups;
    }
    bool bUv0 = false, bUv1 = false, bColors = false;
    const mesh_corner_attributes_t cornerDefault{};
    for ( usize i = 0u; w.bOk && i < d.corners.nCount; ++i ) {
        const mesh_source_corner_t &c = d.corners.pData[i];
        w.U64( pCorners, c.iVertex );
        bUv0 = bUv0 || c.attributes.uv0.x != cornerDefault.uv0.x || c.attributes.uv0.y != cornerDefault.uv0.y;
        bUv1 = bUv1 || c.attributes.uv1.x != cornerDefault.uv1.x || c.attributes.uv1.y != cornerDefault.uv1.y;
        bColors = bColors || c.attributes.colorRgba != cornerDefault.colorRgba;
    }

    // Optional channels: only when some element is not default.
    if ( bMaterials ) {
        key_value_t *p = w.Array( pMesh, "face_materials" );
        for ( usize i = 0u; w.bOk && i < d.faces.nCount; ++i ) { w.U64( p, d.faces.pData[i].attributes.material.value ); }
    }
    if ( bSmoothing ) {
        key_value_t *p = w.Array( pMesh, "face_smoothing" );
        for ( usize i = 0u; w.bOk && i < d.faces.nCount; ++i ) { w.U64( p, d.faces.pData[i].attributes.smoothingGroups ); }
    }
    if ( bUv0 ) {
        key_value_t *p = w.Array( pMesh, "corner_uv0" );
        for ( usize i = 0u; w.bOk && i < d.corners.nCount; ++i ) {
            w.F64( p, d.corners.pData[i].attributes.uv0.x );
            w.F64( p, d.corners.pData[i].attributes.uv0.y );
        }
    }
    if ( bUv1 ) {
        key_value_t *p = w.Array( pMesh, "corner_uv1" );
        for ( usize i = 0u; w.bOk && i < d.corners.nCount; ++i ) {
            w.F64( p, d.corners.pData[i].attributes.uv1.x );
            w.F64( p, d.corners.pData[i].attributes.uv1.y );
        }
    }
    if ( bColors ) {
        key_value_t *p = w.Array( pMesh, "corner_colors" );
        for ( usize i = 0u; w.bOk && i < d.corners.nCount; ++i ) { w.U64( p, d.corners.pData[i].attributes.colorRgba ); }
    }
    if ( d.edges.nCount > 0u ) {
        key_value_t *pV = w.Array( pMesh, "edge_vertices" );
        key_value_t *pF = w.Array( pMesh, "edge_flags" );
        key_value_t *pC = w.Array( pMesh, "edge_creases" );
        for ( usize i = 0u; w.bOk && i < d.edges.nCount; ++i ) {
            const mesh_source_edge_t &e = d.edges.pData[i];
            w.U64( pV, e.iVertexA );
            w.U64( pV, e.iVertexB );
            w.U64( pF, e.attributes.flags );
            w.F64( pC, e.creaseWeight );
        }
    }
    return w.bOk;
}

// ---------------------------------------------------------------------------
// Read
// ---------------------------------------------------------------------------

// Resolves an array child of pObj with an exact expected length.
// bOptional: a missing array is fine (returns nullptr with OK).
geometry_serialization_result_t FindArray(
    const key_value_t *pObj,
    const char *pName,
    usize cExpected,
    bool bOptional,
    usize iMesh,
    const key_value_t **ppOut ) noexcept
{
    *ppOut = nullptr;
    const key_value_t *p = KeyValue_Find( pObj, Sv( pName ) );
    if ( p == nullptr ) {
        return bOptional ? geometry_serialization_result_t{}
                         : Fail( geometry_serialization_status_t::MISSING_FIELD, pName, iMesh );
    }
    if ( KeyValue_Type( p ) != key_value_type_t::ARRAY ) {
        return Fail( geometry_serialization_status_t::TYPE_MISMATCH, pName, iMesh );
    }
    if ( cExpected != CY_INVALID_SIZE && KeyValue_ChildCount( p ) != cExpected ) {
        return Fail( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, pName, iMesh );
    }
    *ppOut = p;
    return {};
}

bool ReadU64At( const key_value_t *pArray, usize i, u64 *pOut ) noexcept
{
    const key_value_t *p = KeyValue_ChildAt( pArray, i );
    return p != nullptr && KeyValue_Type( p ) == key_value_type_t::U64 && KeyValue_GetU64( p, pOut );
}

bool ReadF64At( const key_value_t *pArray, usize i, f64 *pOut ) noexcept
{
    const key_value_t *p = KeyValue_ChildAt( pArray, i );
    return p != nullptr && KeyValue_Type( p ) == key_value_type_t::F64 && KeyValue_GetF64( p, pOut );
}

struct mesh_wire_layout_t {
    u64 sourceId{ 0u };
    usize cVertices{ 0u };
    usize cHalfEdges{ 0u };
    usize cFaces{ 0u };
    usize cEdgeEntries{ 0u };

    const key_value_t *pVertexIds{ nullptr };
    const key_value_t *pPositions{ nullptr };
    const key_value_t *pFaceIds{ nullptr };
    const key_value_t *pCounts{ nullptr };
    const key_value_t *pCorners{ nullptr };
    const key_value_t *pMaterials{ nullptr };
    const key_value_t *pSmoothing{ nullptr };
    const key_value_t *pUv0{ nullptr };
    const key_value_t *pUv1{ nullptr };
    const key_value_t *pColors{ nullptr };
    const key_value_t *pEdgeVertices{ nullptr };
    const key_value_t *pEdgeFlags{ nullptr };
    const key_value_t *pEdgeCreases{ nullptr };
};

// Performs every allocation-free container/count check needed to size a mesh
// description. ReadMesh uses the same result, keeping load-time budget
// preflight and actual decoding on one interpretation of the wire format.
geometry_serialization_result_t InspectMeshWireLayout(
    const key_value_t *pMesh,
    usize iMesh,
    mesh_wire_layout_t *pLayout ) noexcept
{
    if ( pLayout == nullptr ) {
        return Fail( geometry_serialization_status_t::INVALID_ARGUMENT );
    }
    *pLayout = {};
    if ( pMesh == nullptr || KeyValue_Type( pMesh ) != key_value_type_t::OBJECT ) {
        return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "meshes[]", iMesh );
    }

    const key_value_t *pId = KeyValue_Find( pMesh, Sv( "source_id" ) );
    if ( pId == nullptr ) {
        return Fail( geometry_serialization_status_t::MISSING_FIELD, "meshes[].source_id", iMesh );
    }
    if ( KeyValue_Type( pId ) != key_value_type_t::U64 ||
         !KeyValue_GetU64( pId, &pLayout->sourceId ) ) {
        return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "meshes[].source_id", iMesh );
    }
    if ( pLayout->sourceId == 0u ) {
        return Fail( geometry_serialization_status_t::INVALID_SOURCE_ID, "meshes[].source_id", iMesh );
    }

    geometry_serialization_result_t r = FindArray(
        pMesh, "vertex_ids", CY_INVALID_SIZE, false, iMesh,
        &pLayout->pVertexIds );
    if ( r.status != geometry_serialization_status_t::OK ) { return r; }
    pLayout->cVertices = KeyValue_ChildCount( pLayout->pVertexIds );
    if ( pLayout->cVertices > kMeshSourceVerticesMax ) {
        return Fail( geometry_serialization_status_t::LIMIT_EXCEEDED, "vertex_ids", iMesh );
    }
    r = FindArray(
        pMesh, "positions", pLayout->cVertices * 3u, false, iMesh,
        &pLayout->pPositions );
    if ( r.status != geometry_serialization_status_t::OK ) { return r; }

    r = FindArray(
        pMesh, "face_ids", CY_INVALID_SIZE, false, iMesh,
        &pLayout->pFaceIds );
    if ( r.status != geometry_serialization_status_t::OK ) { return r; }
    pLayout->cFaces = KeyValue_ChildCount( pLayout->pFaceIds );
    if ( pLayout->cFaces > kMeshSourceFacesMax ) {
        return Fail( geometry_serialization_status_t::LIMIT_EXCEEDED, "face_ids", iMesh );
    }
    r = FindArray(
        pMesh, "face_corner_counts", pLayout->cFaces, false, iMesh,
        &pLayout->pCounts );
    if ( r.status != geometry_serialization_status_t::OK ) { return r; }

    u64 cCorners = 0u;
    for ( usize f = 0u; f < pLayout->cFaces; ++f ) {
        u64 cFaceCorners = 0u;
        if ( !ReadU64At( pLayout->pCounts, f, &cFaceCorners ) ) {
            return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "face_corner_counts", f );
        }
        if ( cFaceCorners < 3u || cFaceCorners > kMeshSourceCornersPerFaceMax ) {
            return Fail( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "face_corner_counts", f );
        }
        cCorners += cFaceCorners;
    }
    if ( cCorners > kMeshSourceCornersMax ) {
        return Fail( geometry_serialization_status_t::LIMIT_EXCEEDED, "corners", iMesh );
    }
    pLayout->cHalfEdges = static_cast<usize>( cCorners );
    r = FindArray(
        pMesh, "corners", pLayout->cHalfEdges, false, iMesh,
        &pLayout->pCorners );
    if ( r.status != geometry_serialization_status_t::OK ) { return r; }

    if ( ( r = FindArray( pMesh, "face_materials", pLayout->cFaces, true, iMesh,
                          &pLayout->pMaterials ) ).status != geometry_serialization_status_t::OK ||
         ( r = FindArray( pMesh, "face_smoothing", pLayout->cFaces, true, iMesh,
                          &pLayout->pSmoothing ) ).status != geometry_serialization_status_t::OK ||
         ( r = FindArray( pMesh, "corner_uv0", pLayout->cHalfEdges * 2u, true, iMesh,
                          &pLayout->pUv0 ) ).status != geometry_serialization_status_t::OK ||
         ( r = FindArray( pMesh, "corner_uv1", pLayout->cHalfEdges * 2u, true, iMesh,
                          &pLayout->pUv1 ) ).status != geometry_serialization_status_t::OK ||
         ( r = FindArray( pMesh, "corner_colors", pLayout->cHalfEdges, true, iMesh,
                          &pLayout->pColors ) ).status != geometry_serialization_status_t::OK ||
         ( r = FindArray( pMesh, "edge_vertices", CY_INVALID_SIZE, true, iMesh,
                          &pLayout->pEdgeVertices ) ).status != geometry_serialization_status_t::OK ) {
        return r;
    }

    const usize cEdgeScalars = pLayout->pEdgeVertices != nullptr
        ? KeyValue_ChildCount( pLayout->pEdgeVertices )
        : 0u;
    if ( cEdgeScalars % 2u != 0u ) {
        return Fail( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "edge_vertices", iMesh );
    }
    pLayout->cEdgeEntries = cEdgeScalars / 2u;
    if ( pLayout->cEdgeEntries > pLayout->cHalfEdges ) {
        return Fail( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "edge_vertices", iMesh );
    }
    if ( ( r = FindArray( pMesh, "edge_flags", pLayout->cEdgeEntries,
                          pLayout->pEdgeVertices == nullptr, iMesh,
                          &pLayout->pEdgeFlags ) ).status != geometry_serialization_status_t::OK ||
         ( r = FindArray( pMesh, "edge_creases", pLayout->cEdgeEntries,
                          pLayout->pEdgeVertices == nullptr, iMesh,
                          &pLayout->pEdgeCreases ) ).status != geometry_serialization_status_t::OK ) {
        return r;
    }
    if ( pLayout->pEdgeVertices == nullptr &&
         ( pLayout->pEdgeFlags != nullptr || pLayout->pEdgeCreases != nullptr ) ) {
        return Fail( geometry_serialization_status_t::MISSING_FIELD, "edge_vertices", iMesh );
    }
    return {};
}

geometry_serialization_result_t ReadMesh(
    const key_value_t *pMesh,
    usize iMesh,
    mesh_source_description_t *pDesc ) noexcept
{
    mesh_wire_layout_t layout{};
    geometry_serialization_result_t r = InspectMeshWireLayout(
        pMesh, iMesh, &layout );
    if ( r.status != geometry_serialization_status_t::OK ) { return r; }
    MeshSourceDescription_Clear(
        pDesc, geometry_source_id_t{ layout.sourceId } );

    if ( !Vector_Reserve( &pDesc->vertices, layout.cVertices ) ||
         !Vector_Reserve( &pDesc->faces, layout.cFaces ) ||
         !Vector_Reserve( &pDesc->corners, layout.cHalfEdges ) ||
         !Vector_Reserve( &pDesc->edges, layout.cEdgeEntries ) ) {
        return Fail( geometry_serialization_status_t::OUT_OF_MEMORY );
    }
    for ( usize i = 0u; i < layout.cVertices; ++i ) {
        mesh_source_vertex_t v{};
        if ( !ReadU64At( layout.pVertexIds, i, &v.sourceId.value ) ) {
            return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "vertex_ids", i );
        }
        if ( !ReadF64At( layout.pPositions, i * 3u, &v.position.x ) ||
             !ReadF64At( layout.pPositions, i * 3u + 1u, &v.position.y ) ||
             !ReadF64At( layout.pPositions, i * 3u + 2u, &v.position.z ) ) {
            return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "positions", i );
        }
        (void)Vector_PushBack( &pDesc->vertices, v );
    }
    u32 iCorner = 0u;
    for ( usize f = 0u; f < layout.cFaces; ++f ) {
        mesh_source_face_t face{};
        u64 n = 0u, material = 0u, smoothing = 1u;
        (void)ReadU64At( layout.pCounts, f, &n ); // validated above
        if ( !ReadU64At( layout.pFaceIds, f, &face.sourceId.value ) ) {
            return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "face_ids", f );
        }
        if ( layout.pMaterials && !ReadU64At( layout.pMaterials, f, &material ) ) {
            return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "face_materials", f );
        }
        if ( layout.pSmoothing &&
             ( !ReadU64At( layout.pSmoothing, f, &smoothing ) || smoothing > CY_U32_MAX ) ) {
            return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "face_smoothing", f );
        }
        face.iFirstCorner = iCorner;
        face.cCorners = static_cast<u32>( n );
        face.attributes.material.value = material;
        face.attributes.smoothingGroups = static_cast<u32>( smoothing );
        (void)Vector_PushBack( &pDesc->faces, face );
        iCorner += static_cast<u32>( n );
    }
    for ( usize c = 0u; c < layout.cHalfEdges; ++c ) {
        mesh_source_corner_t corner{};
        u64 iv = 0u, color = corner.attributes.colorRgba;
        if ( !ReadU64At( layout.pCorners, c, &iv ) ||
             iv >= layout.cVertices ) {
            return Fail( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "corners", c );
        }
        corner.iVertex = static_cast<u32>( iv );
        if ( layout.pUv0 &&
             ( !ReadF64At( layout.pUv0, c * 2u, &corner.attributes.uv0.x ) ||
               !ReadF64At( layout.pUv0, c * 2u + 1u, &corner.attributes.uv0.y ) ) ) {
            return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "corner_uv0", c );
        }
        if ( layout.pUv1 &&
             ( !ReadF64At( layout.pUv1, c * 2u, &corner.attributes.uv1.x ) ||
               !ReadF64At( layout.pUv1, c * 2u + 1u, &corner.attributes.uv1.y ) ) ) {
            return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "corner_uv1", c );
        }
        if ( layout.pColors &&
             ( !ReadU64At( layout.pColors, c, &color ) || color > CY_U32_MAX ) ) {
            return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "corner_colors", c );
        }
        corner.attributes.colorRgba = static_cast<u32>( color );
        (void)Vector_PushBack( &pDesc->corners, corner );
    }
    for ( usize e = 0u; e < layout.cEdgeEntries; ++e ) {
        u64 a = 0u, b = 0u, flags = 0u;
        f64 crease = 0.0;
        if ( !ReadU64At( layout.pEdgeVertices, e * 2u, &a ) ||
             !ReadU64At( layout.pEdgeVertices, e * 2u + 1u, &b ) ||
             a >= b || b >= layout.cVertices ) {
            return Fail( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "edge_vertices", e );
        }
        if ( !ReadU64At( layout.pEdgeFlags, e, &flags ) || flags > 0xFFu ) {
            return Fail( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "edge_flags", e );
        }
        if ( !ReadF64At( layout.pEdgeCreases, e, &crease ) ) {
            return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "edge_creases", e );
        }
        if ( !std::isfinite( crease ) || crease < 0.0 || crease > 1.0 ) {
            return Fail( geometry_serialization_status_t::VALUE_OUT_OF_RANGE, "edge_creases", e );
        }
        mesh_source_edge_t edge{};
        edge.iVertexA = static_cast<u32>( a );
        edge.iVertexB = static_cast<u32>( b );
        edge.attributes.flags = static_cast<u8>( flags );
        edge.creaseWeight = crease;
        (void)Vector_PushBack( &pDesc->edges, edge );
    }
    return {};
}

geometry_serialization_status_t MapStatus( geometry_status_t st ) noexcept
{
    switch ( st ) {
    case geometry_status_t::ALLOCATION_FAILED:
        return geometry_serialization_status_t::OUT_OF_MEMORY;
    case geometry_status_t::LIMIT_EXCEEDED:
        return geometry_serialization_status_t::LIMIT_EXCEEDED;
    case geometry_status_t::IDENTITY_CONFLICT:
        return geometry_serialization_status_t::DUPLICATE_SOURCE_ID;
    case geometry_status_t::NUMERIC_FAILURE:
        return geometry_serialization_status_t::VALUE_OUT_OF_RANGE;
    default:
        return geometry_serialization_status_t::CORRUPT_DOCUMENT;
    }
}

} // namespace

geometry_serialization_result_t MeshSerialization_TryWriteMeshes(
    key_value_document_t *pKvDocument,
    key_value_t *pRoot,
    const geometry_document_t *pDocument,
    const allocator_t *pScratchAllocator ) noexcept
{
    if ( pKvDocument == nullptr || pRoot == nullptr || !GeometryDocument_IsInitialized( pDocument ) ||
         !Allocator_IsValid( pScratchAllocator ) ) {
        return Fail( geometry_serialization_status_t::INVALID_ARGUMENT );
    }
    const usize cMeshes = GeometryDocument_MeshCount( pDocument );
    if ( cMeshes > GEOMETRY_SERIALIZATION_MAX_MESHES ) {
        return Fail( geometry_serialization_status_t::LIMIT_EXCEEDED, "meshes" );
    }
    writer_t w{ pKvDocument };
    key_value_t *pMeshes = w.Array( pRoot, "meshes" );
    if ( !w.bOk ) { return Fail( geometry_serialization_status_t::OUT_OF_MEMORY, "meshes" ); }

    mesh_source_description_t desc{};
    if ( MeshSourceDescription_Init( &desc, pScratchAllocator, GEOMETRY_SOURCE_ID_INVALID ) != geometry_status_t::OK ) {
        return Fail( geometry_serialization_status_t::OUT_OF_MEMORY );
    }
    geometry_serialization_result_t r{};
    for ( usize i = 0u; i < cMeshes; ++i ) {
        const geometry_status_t st = MeshSource_TryDescribe( GeometryDocument_MeshAt( pDocument, i ), &desc );
        if ( st != geometry_status_t::OK ) {
            r = Fail( st == geometry_status_t::ALLOCATION_FAILED ? geometry_serialization_status_t::OUT_OF_MEMORY
                                                                  : geometry_serialization_status_t::CORRUPT_DOCUMENT,
                      "meshes[]", i );
            break;
        }
        if ( !WriteMesh( w, pMeshes, desc ) ) {
            r = Fail( geometry_serialization_status_t::OUT_OF_MEMORY, "meshes[]", i );
            break;
        }
    }
    MeshSourceDescription_Shutdown( &desc );
    return r;
}

geometry_serialization_result_t MeshSerialization_TryPreflightReadBudgets(
    const key_value_t *pRoot,
    const geometry_policy_t &policy ) noexcept
{
    if ( pRoot == nullptr || !GeometryPolicy_IsValid( policy ) ) {
        return Fail( geometry_serialization_status_t::INVALID_ARGUMENT );
    }
    if ( KeyValue_Type( pRoot ) != key_value_type_t::OBJECT ) {
        return Fail( geometry_serialization_status_t::TYPE_MISMATCH, "root" );
    }

    const key_value_t *pMeshes = nullptr;
    geometry_serialization_result_t r = FindArray(
        pRoot, "meshes", CY_INVALID_SIZE, false, CY_INVALID_SIZE,
        &pMeshes );
    if ( r.status != geometry_serialization_status_t::OK ) { return r; }

    const usize cMeshes = KeyValue_ChildCount( pMeshes );
    if ( cMeshes > GEOMETRY_SERIALIZATION_MAX_MESHES ) {
        return Fail( geometry_serialization_status_t::LIMIT_EXCEEDED, "meshes" );
    }

    u64 cVertices = 0u;
    u64 cHalfEdges = 0u;
    u64 cEdgesLowerBound = 0u;
    u64 cLoops = 0u;
    u64 cFaces = 0u;
    u64 cShellsLowerBound = 0u;

    const auto TryAccumulate = [](
        u64 *pTotal,
        u64 cAdd,
        u64 cMaximum ) noexcept -> bool {
        if ( pTotal == nullptr || *pTotal > cMaximum ||
             cAdd > cMaximum - *pTotal ) {
            return false;
        }
        *pTotal += cAdd;
        return true;
    };

    for ( usize i = 0u; i < cMeshes; ++i ) {
        mesh_wire_layout_t layout{};
        r = InspectMeshWireLayout(
            KeyValue_ChildAt( pMeshes, i ), i, &layout );
        if ( r.status != geometry_serialization_status_t::OK ) { return r; }

        const u64 cV = static_cast<u64>( layout.cVertices );
        const u64 cH = static_cast<u64>( layout.cHalfEdges );
        const u64 cF = static_cast<u64>( layout.cFaces );
        const u64 cEdgeFloor = std::max(
            cV,
            cH / 2u + cH % 2u );
        const u64 cShellFloor = cF > 0u ? 1u : 0u;

        if ( !TryAccumulate( &cVertices, cV,
                             policy.limits.cVerticesMax ) ||
             !TryAccumulate( &cHalfEdges, cH,
                             policy.limits.cHalfEdgesMax ) ||
             !TryAccumulate( &cEdgesLowerBound, cEdgeFloor,
                             policy.limits.cEdgesMax ) ||
             !TryAccumulate( &cLoops, cF,
                             policy.limits.cLoopsMax ) ||
             !TryAccumulate( &cFaces, cF,
                             policy.limits.cFacesMax ) ||
             !TryAccumulate( &cShellsLowerBound, cShellFloor,
                             policy.limits.cShellsMax ) ) {
            return Fail(
                geometry_serialization_status_t::LIMIT_EXCEEDED,
                "meshes", i );
        }
    }
    return {};
}

geometry_serialization_result_t MeshSerialization_TryReadMeshes(
    const key_value_t *pRoot,
    geometry_document_t *pDocument,
    bool bRequireClaimed ) noexcept
{
    if ( pRoot == nullptr || !GeometryDocument_IsInitialized( pDocument ) ) {
        return Fail( geometry_serialization_status_t::INVALID_ARGUMENT );
    }
    geometry_serialization_result_t r =
        MeshSerialization_TryPreflightReadBudgets(
            pRoot, pDocument->policy );
    if ( r.status != geometry_serialization_status_t::OK ) { return r; }

    const key_value_t *pMeshes = nullptr;
    r = FindArray( pRoot, "meshes", CY_INVALID_SIZE, false,
                   CY_INVALID_SIZE, &pMeshes );
    if ( r.status != geometry_serialization_status_t::OK ) { return r; }
    const usize cMeshes = KeyValue_ChildCount( pMeshes );
    if ( cMeshes > GEOMETRY_SERIALIZATION_MAX_MESHES ) {
        return Fail( geometry_serialization_status_t::LIMIT_EXCEEDED, "meshes" );
    }

    const allocator_t *pAllocator = pDocument->pAllocator;
    mesh_source_description_t desc{};
    if ( MeshSourceDescription_Init( &desc, pAllocator, GEOMETRY_SOURCE_ID_INVALID ) != geometry_status_t::OK ) {
        return Fail( geometry_serialization_status_t::OUT_OF_MEMORY );
    }
    for ( usize i = 0u; i < cMeshes && r.status == geometry_serialization_status_t::OK; ++i ) {
        r = ReadMesh( KeyValue_ChildAt( pMeshes, i ), i, &desc );
        if ( r.status != geometry_serialization_status_t::OK ) { break; }

        if ( bRequireClaimed ) {
            // Files that persist identity state must list every ID they use
            // as claimed; an unclaimed ID means the file was edited by hand
            // or is corrupt, and admitting it could collide with a future
            // allocation.
            bool bClaimed = HashSet_Contains( &pDocument->sourceIds.claimedIds, desc.sourceId );
            for ( usize v = 0u; bClaimed && v < desc.vertices.nCount; ++v ) {
                bClaimed = HashSet_Contains( &pDocument->sourceIds.claimedIds, desc.vertices.pData[v].sourceId );
            }
            for ( usize f = 0u; bClaimed && f < desc.faces.nCount; ++f ) {
                bClaimed = HashSet_Contains( &pDocument->sourceIds.claimedIds, desc.faces.pData[f].sourceId );
            }
            if ( !bClaimed ) {
                r = Fail( geometry_serialization_status_t::INVALID_SOURCE_ID, "meshes[]", i );
                break;
            }
        }

        mesh_source_t mesh{};
        geometry_status_t st = MeshSource_TryBuild( &desc, pAllocator, &mesh );
        if ( st == geometry_status_t::OK ) { st = GeometryDocument_TryAddMesh( pDocument, &mesh ); }
        MeshSource_Shutdown( &mesh );
        if ( st != geometry_status_t::OK ) { r = Fail( MapStatus( st ), "meshes[]", i ); }
    }
    MeshSourceDescription_Shutdown( &desc );
    return r;
}

} // namespace cypher::editor::geometry
