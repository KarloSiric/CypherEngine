//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceComposition.cpp
//  Purpose: Implements exact composition operations for authored mesh sources.
//  Details: Join first proves every input and identity set, then constructs a
//           complete staged canonical description with offset local indices.
//           MeshSource_TryBuild is the sole publication path, so no failed
//           allocation or validation can expose a partial output.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSourceComposition.h"

#include <algorithm>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

bool OutputIsCanonicalEmpty( const mesh_source_t *pOutput ) noexcept
{
    return pOutput != nullptr &&
           !EditableMesh_IsInitialized( &pOutput->mesh ) &&
           pOutput->attributes.faces.pAllocator == nullptr &&
           pOutput->attributes.edges.pAllocator == nullptr &&
           pOutput->attributes.corners.pAllocator == nullptr &&
           pOutput->vertexIds.pAllocator == nullptr &&
           pOutput->faceIds.pAllocator == nullptr &&
           !GeometrySourceId_IsValid( pOutput->sourceId );
}

geometry_status_t StatusFromValidation(
    const mesh_source_validation_t &validation ) noexcept
{
    switch ( validation.fault ) {
        case mesh_source_fault_t::NONE:
            return geometry_status_t::OK;
        case mesh_source_fault_t::NOT_INITIALIZED:
        case mesh_source_fault_t::INVALID_ROOT_ID:
        case mesh_source_fault_t::MISSING_VERTEX_ID:
        case mesh_source_fault_t::MISSING_FACE_ID:
            return geometry_status_t::INVALID_ARGUMENT;
        case mesh_source_fault_t::DUPLICATE_SOURCE_ID:
            return geometry_status_t::IDENTITY_CONFLICT;
        case mesh_source_fault_t::NON_FINITE:
        case mesh_source_fault_t::COORDINATE_RANGE:
            return geometry_status_t::NUMERIC_FAILURE;
        case mesh_source_fault_t::VALIDATION_INCOMPLETE:
            return geometry_status_t::ALLOCATION_FAILED;
        case mesh_source_fault_t::INVALID_TOPOLOGY:
        case mesh_source_fault_t::INVALID_ATTRIBUTES:
        default:
            return geometry_status_t::INVALID_TOPOLOGY;
    }
}

bool TryAdd( usize value, usize *pTotal ) noexcept
{
    if ( pTotal == nullptr || value > CY_USIZE_MAX - *pTotal ) {
        return false;
    }
    *pTotal += value;
    return true;
}

} // namespace

geometry_status_t MeshSource_TryJoinExact(
    span_t<const mesh_source_t *const> inputs,
    geometry_source_id_t retainedRootId,
    const allocator_t *pAllocator,
    mesh_source_t *pOutput,
    mesh_source_join_report_t *pReportOut ) noexcept
{
    if ( pReportOut != nullptr ) {
        *pReportOut = {};
    }
    if ( inputs.pData == nullptr || inputs.nCount < 2u ||
         !GeometrySourceId_IsValid( retainedRootId ) ||
         !Allocator_IsValid( pAllocator ) || pOutput == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !OutputIsCanonicalEmpty( pOutput ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( inputs.nCount > static_cast<usize>( CY_U32_MAX ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    vector_t<geometry_source_id_t> allIds{};
    if ( !Vector_Init( &allIds, pAllocator ) ) {
        Vector_Shutdown( &allIds );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    auto cleanupIds = [&]() noexcept {
        Vector_Shutdown( &allIds );
    };

    usize cVertices = 0u;
    usize cFaces = 0u;
    usize cCorners = 0u;
    u32 cRetainedRoots = 0u;
    geometry_status_t status = geometry_status_t::OK;
    for ( usize i = 0u; i < inputs.nCount; ++i ) {
        const mesh_source_t *const pInput = inputs.pData[i];
        if ( pInput == nullptr || pInput == pOutput ||
             !MeshSource_IsInitialized( pInput ) ) {
            status = geometry_status_t::INVALID_ARGUMENT;
            break;
        }
        const mesh_source_validation_t validation =
            MeshSource_Validate( pInput, pAllocator );
        status = StatusFromValidation( validation );
        if ( status != geometry_status_t::OK ) {
            break;
        }
        cRetainedRoots += pInput->sourceId.value == retainedRootId.value ? 1u : 0u;

        for ( usize j = 0u; j < i; ++j ) {
            if ( inputs.pData[j] == pInput ) {
                status = geometry_status_t::INVALID_ARGUMENT;
                break;
            }
        }
        if ( status != geometry_status_t::OK ) {
            break;
        }

        vector_t<geometry_source_id_t> inputIds{};
        if ( !Vector_Init( &inputIds, pAllocator ) ) {
            status = geometry_status_t::ALLOCATION_FAILED;
            break;
        }
        status = MeshSource_TryCollectSourceIds( pInput, &inputIds );
        if ( status == geometry_status_t::OK ) {
            if ( !Vector_Append(
                     &allIds,
                     span_t<const geometry_source_id_t>{
                         inputIds.pData, inputIds.nCount } ) ) {
                status = geometry_status_t::ALLOCATION_FAILED;
            }
        }
        Vector_Shutdown( &inputIds );
        if ( status != geometry_status_t::OK ) {
            break;
        }

        const usize inputVertices = EditableMesh_VertexCount( &pInput->mesh );
        const usize inputFaces = EditableMesh_FaceCount( &pInput->mesh );
        const usize inputCorners = EditableMesh_HalfEdgeCount( &pInput->mesh );
        if ( !TryAdd( inputVertices, &cVertices ) ||
             !TryAdd( inputFaces, &cFaces ) ||
             !TryAdd( inputCorners, &cCorners ) ) {
            status = geometry_status_t::LIMIT_EXCEEDED;
            break;
        }
    }
    if ( status == geometry_status_t::OK && cRetainedRoots != 1u ) {
        status = geometry_status_t::INVALID_ARGUMENT;
    }
    if ( status == geometry_status_t::OK &&
         ( cVertices > kMeshSourceVerticesMax ||
           cFaces > kMeshSourceFacesMax ||
           cCorners > kMeshSourceCornersMax ) ) {
        status = geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( status == geometry_status_t::OK ) {
        std::sort(
            allIds.pData, allIds.pData + allIds.nCount,
            []( geometry_source_id_t a, geometry_source_id_t b ) noexcept {
                return a.value < b.value;
            } );
        for ( usize i = 1u; i < allIds.nCount; ++i ) {
            if ( allIds.pData[i - 1u].value == allIds.pData[i].value ) {
                status = geometry_status_t::IDENTITY_CONFLICT;
                break;
            }
        }
    }
    if ( status != geometry_status_t::OK ) {
        cleanupIds();
        return status;
    }

    mesh_source_description_t joined{};
    status = MeshSourceDescription_Init(
        &joined, pAllocator, retainedRootId );
    if ( status != geometry_status_t::OK ) {
        cleanupIds();
        return status;
    }
    auto cleanup = [&]() noexcept {
        MeshSourceDescription_Shutdown( &joined );
        cleanupIds();
    };
    if ( !Vector_Reserve( &joined.vertices, cVertices ) ||
         !Vector_Reserve( &joined.faces, cFaces ) ||
         !Vector_Reserve( &joined.corners, cCorners ) ||
         !Vector_Reserve( &joined.edges, cCorners ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( usize iInput = 0u;
          iInput < inputs.nCount && status == geometry_status_t::OK;
          ++iInput ) {
        mesh_source_description_t part{};
        status = MeshSourceDescription_Init(
            &part, pAllocator, inputs.pData[iInput]->sourceId );
        if ( status != geometry_status_t::OK ) {
            break;
        }
        status = MeshSource_TryDescribe( inputs.pData[iInput], &part );
        if ( status != geometry_status_t::OK ) {
            MeshSourceDescription_Shutdown( &part );
            break;
        }

        const usize vertexOffset = joined.vertices.nCount;
        const usize cornerOffset = joined.corners.nCount;
        if ( vertexOffset > CY_U32_MAX || cornerOffset > CY_U32_MAX ) {
            status = geometry_status_t::LIMIT_EXCEEDED;
        }
        for ( usize i = 0u;
              i < part.vertices.nCount && status == geometry_status_t::OK;
              ++i ) {
            if ( !Vector_PushBack( &joined.vertices, part.vertices.pData[i] ) ) {
                status = geometry_status_t::ALLOCATION_FAILED;
            }
        }
        for ( usize i = 0u;
              i < part.corners.nCount && status == geometry_status_t::OK;
              ++i ) {
            mesh_source_corner_t corner = part.corners.pData[i];
            const usize iVertex = vertexOffset + corner.iVertex;
            if ( iVertex > CY_U32_MAX ) {
                status = geometry_status_t::LIMIT_EXCEEDED;
                break;
            }
            corner.iVertex = static_cast<u32>( iVertex );
            if ( !Vector_PushBack( &joined.corners, corner ) ) {
                status = geometry_status_t::ALLOCATION_FAILED;
            }
        }
        for ( usize i = 0u;
              i < part.faces.nCount && status == geometry_status_t::OK;
              ++i ) {
            mesh_source_face_t face = part.faces.pData[i];
            const usize iFirstCorner = cornerOffset + face.iFirstCorner;
            if ( iFirstCorner > CY_U32_MAX ) {
                status = geometry_status_t::LIMIT_EXCEEDED;
                break;
            }
            face.iFirstCorner = static_cast<u32>( iFirstCorner );
            if ( !Vector_PushBack( &joined.faces, face ) ) {
                status = geometry_status_t::ALLOCATION_FAILED;
            }
        }
        for ( usize i = 0u;
              i < part.edges.nCount && status == geometry_status_t::OK;
              ++i ) {
            mesh_source_edge_t edge = part.edges.pData[i];
            const usize iVertexA = vertexOffset + edge.iVertexA;
            const usize iVertexB = vertexOffset + edge.iVertexB;
            if ( iVertexA > CY_U32_MAX || iVertexB > CY_U32_MAX ) {
                status = geometry_status_t::LIMIT_EXCEEDED;
                break;
            }
            edge.iVertexA = static_cast<u32>( iVertexA );
            edge.iVertexB = static_cast<u32>( iVertexB );
            if ( !Vector_PushBack( &joined.edges, edge ) ) {
                status = geometry_status_t::ALLOCATION_FAILED;
            }
        }
        MeshSourceDescription_Shutdown( &part );
    }

    if ( status == geometry_status_t::OK ) {
        status = MeshSource_TryBuild(
            &joined, pAllocator, pOutput, nullptr );
    }
    if ( status == geometry_status_t::OK && pReportOut != nullptr ) {
        pReportOut->cInputSources = static_cast<u32>( inputs.nCount );
        pReportOut->cVertices = static_cast<u32>( cVertices );
        pReportOut->cFaces = static_cast<u32>( cFaces );
        pReportOut->cCorners = static_cast<u32>( cCorners );
        pReportOut->cAttributedEdges =
            static_cast<u32>( joined.edges.nCount );
        pReportOut->cShells = static_cast<u32>(
            EditableMesh_ShellCount( &pOutput->mesh ) );
    }
    cleanup();
    return status;
}

} // namespace cypher::editor::geometry
