//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshObjectCommands.cpp
//  Purpose: Implements document commands for exact mesh Join and Separate.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshObjectCommands.h"

#include <algorithm>
#include <new>
#include <utility>

namespace cypher::editor::geometry
{

namespace
{

struct owned_descriptions_t {
    common::vector_t<mesh_source_description_t *> values{};
};

mesh_source_description_t *AllocateDescription(
    const common::allocator_t *pAllocator ) noexcept
{
    void *pMemory = common::Allocator_AllocateZeroed(
        pAllocator,
        sizeof( mesh_source_description_t ),
        alignof( mesh_source_description_t ) );
    return pMemory != nullptr
        ? new ( pMemory ) mesh_source_description_t{}
        : nullptr;
}

void FreeDescription(
    const common::allocator_t *pAllocator,
    mesh_source_description_t *pDescription ) noexcept
{
    if ( pDescription == nullptr ) {
        return;
    }
    MeshSourceDescription_Shutdown( pDescription );
    pDescription->~mesh_source_description_t();
    common::Allocator_Free(
        pAllocator,
        pDescription,
        sizeof( mesh_source_description_t ),
        alignof( mesh_source_description_t ) );
}

void OwnedDescriptions_Shutdown(
    owned_descriptions_t *pDescriptions ) noexcept
{
    if ( pDescriptions == nullptr ) {
        return;
    }
    const common::allocator_t *pAllocator =
        pDescriptions->values.pAllocator;
    if ( pAllocator != nullptr ) {
        for ( common::usize i = 0u;
              i < pDescriptions->values.nCount;
              ++i ) {
            FreeDescription(
                pAllocator, pDescriptions->values.pData[i] );
        }
    }
    common::Vector_Shutdown( &pDescriptions->values );
}

geometry_status_t OwnedDescriptions_TryInit(
    owned_descriptions_t *pDescriptions,
    const common::allocator_t *pAllocator,
    common::usize cCapacity ) noexcept
{
    return common::Vector_Init(
               &pDescriptions->values, pAllocator, cCapacity )
        ? geometry_status_t::OK
        : geometry_status_t::ALLOCATION_FAILED;
}

geometry_status_t OwnedDescriptions_TryAppendEmpty(
    owned_descriptions_t *pDescriptions,
    geometry_source_id_t rootId,
    mesh_source_description_t **ppDescriptionOut ) noexcept
{
    const common::allocator_t *pAllocator =
        pDescriptions->values.pAllocator;
    mesh_source_description_t *pDescription =
        AllocateDescription( pAllocator );
    if ( pDescription == nullptr ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t status = MeshSourceDescription_Init(
        pDescription, pAllocator, rootId );
    if ( status != geometry_status_t::OK ) {
        FreeDescription( pAllocator, pDescription );
        return status;
    }
    if ( !common::Vector_PushBack(
             &pDescriptions->values, pDescription ) ) {
        FreeDescription( pAllocator, pDescription );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    *ppDescriptionOut = pDescription;
    return geometry_status_t::OK;
}

geometry_status_t CaptureDocumentDescriptions(
    const geometry_document_t &document,
    const common::allocator_t *pAllocator,
    owned_descriptions_t *pDescriptionsOut ) noexcept
{
    geometry_status_t status = OwnedDescriptions_TryInit(
        pDescriptionsOut, pAllocator, document.meshes.nCount );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    for ( common::usize i = 0u; i < document.meshes.nCount; ++i ) {
        mesh_source_description_t *pDescription = nullptr;
        status = OwnedDescriptions_TryAppendEmpty(
            pDescriptionsOut,
            document.meshes.pData[i]->sourceId,
            &pDescription );
        if ( status == geometry_status_t::OK ) {
            status = MeshSource_TryDescribe(
                document.meshes.pData[i], pDescription );
        }
        if ( status != geometry_status_t::OK ) {
            OwnedDescriptions_Shutdown( pDescriptionsOut );
            return status;
        }
    }
    return geometry_status_t::OK;
}

common::usize FindDocumentMeshIndex(
    const geometry_document_t &document,
    geometry_source_id_t rootId ) noexcept
{
    for ( common::usize i = 0u; i < document.meshes.nCount; ++i ) {
        if ( document.meshes.pData[i] != nullptr &&
             document.meshes.pData[i]->sourceId.value == rootId.value ) {
            return i;
        }
    }
    return document.meshes.nCount;
}

bool RootInSpan(
    common::span_t<const geometry_source_id_t> roots,
    geometry_source_id_t root ) noexcept
{
    for ( common::usize i = 0u; i < roots.nCount; ++i ) {
        if ( roots.pData[i].value == root.value ) {
            return true;
        }
    }
    return false;
}

void SwapDescriptionLists(
    geometry_mesh_description_list_t &a,
    geometry_mesh_description_list_t &b ) noexcept
{
    std::swap( a.meshes.pData, b.meshes.pData );
    std::swap( a.meshes.nCount, b.meshes.nCount );
    std::swap( a.meshes.nCapacity, b.meshes.nCapacity );
    std::swap( a.meshes.pAllocator, b.meshes.pAllocator );
}

void ReplaceDeltaWithPrepared(
    geometry_mesh_set_delta_t *pDestination,
    geometry_mesh_set_delta_t *pPrepared ) noexcept
{
    SwapDescriptionLists( pDestination->before, pPrepared->before );
    SwapDescriptionLists( pDestination->after, pPrepared->after );
    std::swap( pDestination->bHasPayload, pPrepared->bHasPayload );
}

geometry_status_t CommitPreparedTransition(
    geometry_document_t *pDocument,
    common::span_t<const mesh_source_description_t *const> before,
    common::span_t<const mesh_source_description_t *const> after,
    geometry_mesh_set_delta_t *pDeltaOut,
    geometry_revision_t *pNewRevisionOut ) noexcept
{
    const common::allocator_t *pDeltaAllocator =
        pDeltaOut->before.meshes.pAllocator;
    geometry_mesh_set_delta_t prepared{};
    geometry_status_t status = GeometryMeshSetDelta_Init(
        &prepared, pDeltaAllocator );
    if ( status == geometry_status_t::OK ) {
        status = GeometryMeshSetDelta_TryAssign(
            &prepared, before, after );
    }
    if ( status == geometry_status_t::OK ) {
        status = GeometryMeshSetDelta_TryApply(
            &prepared, pDocument, pNewRevisionOut );
    }
    if ( status == geometry_status_t::OK ) {
        ReplaceDeltaWithPrepared( pDeltaOut, &prepared );
    }
    GeometryMeshSetDelta_Shutdown( &prepared );
    return status;
}

geometry_status_t ValidateCommandBoundary(
    const geometry_document_t *pDocument,
    const geometry_mesh_set_delta_t *pDeltaOut ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometryMeshSetDelta_IsInitialized( pDeltaOut ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return geometry_status_t::OK;
}

struct face_edge_t {
    common::u32 a{ 0u };
    common::u32 b{ 0u };
    common::u32 iFace{ 0u };
};

struct component_t {
    common::u32 dsuRoot{ 0u };
    common::u64 minFaceId{ 0u };
};

common::u32 DsuFind(
    common::vector_t<common::u32> *pParents,
    common::u32 value ) noexcept
{
    common::u32 root = value;
    while ( pParents->pData[root] != root ) {
        root = pParents->pData[root];
    }
    while ( pParents->pData[value] != value ) {
        const common::u32 next = pParents->pData[value];
        pParents->pData[value] = root;
        value = next;
    }
    return root;
}

void DsuUnion(
    common::vector_t<common::u32> *pParents,
    common::u32 a,
    common::u32 b ) noexcept
{
    a = DsuFind( pParents, a );
    b = DsuFind( pParents, b );
    if ( a == b ) {
        return;
    }
    if ( b < a ) {
        std::swap( a, b );
    }
    pParents->pData[b] = a;
}

common::usize FindComponentIndex(
    const common::vector_t<component_t> &components,
    common::u32 dsuRoot ) noexcept
{
    for ( common::usize i = 0u; i < components.nCount; ++i ) {
        if ( components.pData[i].dsuRoot == dsuRoot ) {
            return i;
        }
    }
    return components.nCount;
}

geometry_status_t DiscoverFaceComponents(
    const mesh_source_description_t &source,
    common::vector_t<common::u32> *pFaceComponentsOut,
    common::vector_t<component_t> *pComponentsOut ) noexcept
{
    const common::allocator_t *pAllocator = source.faces.pAllocator;
    common::vector_t<common::u32> parents{};
    common::vector_t<face_edge_t> edges{};
    auto cleanup = [&]() noexcept {
        common::Vector_Shutdown( &edges );
        common::Vector_Shutdown( &parents );
    };
    if ( !common::Vector_Init( &parents, pAllocator ) ||
         !common::Vector_Resize( &parents, source.faces.nCount ) ||
         !common::Vector_Init( &edges, pAllocator ) ||
         !common::Vector_Reserve( &edges, source.corners.nCount ) ||
         !common::Vector_Init( pFaceComponentsOut, pAllocator ) ||
         !common::Vector_Resize(
             pFaceComponentsOut, source.faces.nCount ) ||
         !common::Vector_Init( pComponentsOut, pAllocator ) ) {
        common::Vector_Shutdown( pComponentsOut );
        common::Vector_Shutdown( pFaceComponentsOut );
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( common::usize iFace = 0u;
          iFace < source.faces.nCount;
          ++iFace ) {
        parents.pData[iFace] = static_cast<common::u32>( iFace );
        const mesh_source_face_t &face = source.faces.pData[iFace];
        for ( common::u32 iCorner = 0u;
              iCorner < face.cCorners;
              ++iCorner ) {
            const common::u32 a = source.corners.pData[
                face.iFirstCorner + iCorner].iVertex;
            const common::u32 b = source.corners.pData[
                face.iFirstCorner + ( iCorner + 1u ) % face.cCorners].iVertex;
            if ( !common::Vector_PushBack(
                     &edges,
                     face_edge_t{
                         std::min( a, b ),
                         std::max( a, b ),
                         static_cast<common::u32>( iFace ) } ) ) {
                common::Vector_Shutdown( pComponentsOut );
                common::Vector_Shutdown( pFaceComponentsOut );
                cleanup();
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
    }
    std::sort(
        edges.pData,
        edges.pData + edges.nCount,
        []( const face_edge_t &left, const face_edge_t &right ) noexcept {
            if ( left.a != right.a ) {
                return left.a < right.a;
            }
            if ( left.b != right.b ) {
                return left.b < right.b;
            }
            return left.iFace < right.iFace;
        } );
    for ( common::usize i = 0u; i < edges.nCount; ) {
        common::usize iEnd = i + 1u;
        while ( iEnd < edges.nCount &&
                edges.pData[iEnd].a == edges.pData[i].a &&
                edges.pData[iEnd].b == edges.pData[i].b ) {
            DsuUnion(
                &parents,
                edges.pData[i].iFace,
                edges.pData[iEnd].iFace );
            ++iEnd;
        }
        i = iEnd;
    }

    for ( common::usize iFace = 0u;
          iFace < source.faces.nCount;
          ++iFace ) {
        const common::u32 root = DsuFind(
            &parents, static_cast<common::u32>( iFace ) );
        common::usize iComponent = FindComponentIndex(
            *pComponentsOut, root );
        if ( iComponent == pComponentsOut->nCount ) {
            if ( !common::Vector_PushBack(
                     pComponentsOut,
                     component_t{
                         root, source.faces.pData[iFace].sourceId.value } ) ) {
                common::Vector_Shutdown( pComponentsOut );
                common::Vector_Shutdown( pFaceComponentsOut );
                cleanup();
                return geometry_status_t::ALLOCATION_FAILED;
            }
            iComponent = pComponentsOut->nCount - 1u;
        }
        pFaceComponentsOut->pData[iFace] =
            static_cast<common::u32>( iComponent );
        pComponentsOut->pData[iComponent].minFaceId = std::min(
            pComponentsOut->pData[iComponent].minFaceId,
            source.faces.pData[iFace].sourceId.value );
    }
    cleanup();
    return geometry_status_t::OK;
}

geometry_status_t BuildOneComponentDescription(
    const mesh_source_description_t &source,
    const common::vector_t<common::u32> &faceComponents,
    common::u32 iComponent,
    geometry_source_id_t outputRoot,
    mesh_source_description_t *pOutput ) noexcept
{
    const common::allocator_t *pAllocator = source.faces.pAllocator;
    common::vector_t<common::u32> vertexMap{};
    if ( !common::Vector_Init( &vertexMap, pAllocator ) ||
         !common::Vector_Resize( &vertexMap, source.vertices.nCount ) ) {
        common::Vector_Shutdown( &vertexMap );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < vertexMap.nCount; ++i ) {
        vertexMap.pData[i] = common::CY_INVALID_INDEX;
    }

    geometry_status_t status = geometry_status_t::OK;
    common::u32 localCorners[kMeshSourceCornersPerFaceMax]{};
    for ( common::usize iFace = 0u;
          iFace < source.faces.nCount && status == geometry_status_t::OK;
          ++iFace ) {
        if ( faceComponents.pData[iFace] != iComponent ) {
            continue;
        }
        const mesh_source_face_t &face = source.faces.pData[iFace];
        for ( common::u32 iCorner = 0u;
              iCorner < face.cCorners;
              ++iCorner ) {
            const common::u32 sourceVertex = source.corners.pData[
                face.iFirstCorner + iCorner].iVertex;
            if ( vertexMap.pData[sourceVertex] == common::CY_INVALID_INDEX ) {
                const mesh_source_vertex_t &vertex =
                    source.vertices.pData[sourceVertex];
                status = MeshSourceDescription_TryAddVertex(
                    pOutput,
                    vertex.position,
                    vertex.sourceId,
                    &vertexMap.pData[sourceVertex] );
                if ( status != geometry_status_t::OK ) {
                    break;
                }
            }
            localCorners[iCorner] = vertexMap.pData[sourceVertex];
        }
        if ( status != geometry_status_t::OK ) {
            break;
        }
        common::u32 iNewFace = 0u;
        status = MeshSourceDescription_TryAddFace(
            pOutput,
            { localCorners, face.cCorners },
            face.sourceId,
            face.attributes,
            &iNewFace );
        if ( status == geometry_status_t::OK ) {
            const mesh_source_face_t &newFace =
                pOutput->faces.pData[iNewFace];
            for ( common::u32 iCorner = 0u;
                  iCorner < face.cCorners;
                  ++iCorner ) {
                pOutput->corners.pData[
                    newFace.iFirstCorner + iCorner].attributes =
                    source.corners.pData[
                        face.iFirstCorner + iCorner].attributes;
            }
        }
    }

    for ( common::usize iEdge = 0u;
          iEdge < source.edges.nCount && status == geometry_status_t::OK;
          ++iEdge ) {
        const mesh_source_edge_t &edge = source.edges.pData[iEdge];
        const common::u32 a = vertexMap.pData[edge.iVertexA];
        const common::u32 b = vertexMap.pData[edge.iVertexB];
        if ( a != common::CY_INVALID_INDEX &&
             b != common::CY_INVALID_INDEX ) {
            status = MeshSourceDescription_TrySetEdge(
                pOutput,
                a,
                b,
                edge.attributes,
                edge.creaseWeight );
        }
    }
    common::Vector_Shutdown( &vertexMap );
    if ( status != geometry_status_t::OK ) {
        MeshSourceDescription_Clear( pOutput, outputRoot );
    }
    return status;
}

geometry_status_t BuildSeparateOutputs(
    const geometry_document_t &document,
    const mesh_source_description_t &source,
    geometry_source_id_t retainedFaceId,
    owned_descriptions_t *pOutputsOut,
    mesh_object_command_report_t *pReportOut ) noexcept
{
    common::vector_t<common::u32> faceComponents{};
    common::vector_t<component_t> components{};
    geometry_status_t status = DiscoverFaceComponents(
        source, &faceComponents, &components );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    auto cleanupComponents = [&]() noexcept {
        common::Vector_Shutdown( &components );
        common::Vector_Shutdown( &faceComponents );
    };
    if ( components.nCount < 2u ) {
        cleanupComponents();
        return geometry_status_t::DEGENERATE;
    }

    common::usize iRetainedFace = source.faces.nCount;
    for ( common::usize i = 0u; i < source.faces.nCount; ++i ) {
        if ( source.faces.pData[i].sourceId.value == retainedFaceId.value ) {
            iRetainedFace = i;
            break;
        }
    }
    if ( iRetainedFace >= source.faces.nCount ) {
        cleanupComponents();
        return geometry_status_t::INVALID_HANDLE;
    }
    const common::u32 retainedComponent =
        faceComponents.pData[iRetainedFace];

    common::vector_t<common::u32> outputOrder{};
    if ( !common::Vector_Init(
             &outputOrder, document.pAllocator, components.nCount ) ||
         !common::Vector_PushBack( &outputOrder, retainedComponent ) ) {
        common::Vector_Shutdown( &outputOrder );
        cleanupComponents();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    common::vector_t<component_t> sorted{};
    if ( !common::Vector_Init(
             &sorted, document.pAllocator, components.nCount - 1u ) ) {
        common::Vector_Shutdown( &outputOrder );
        cleanupComponents();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < components.nCount; ++i ) {
        if ( i != retainedComponent &&
             !common::Vector_PushBack( &sorted, components.pData[i] ) ) {
            common::Vector_Shutdown( &sorted );
            common::Vector_Shutdown( &outputOrder );
            cleanupComponents();
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    std::sort(
        sorted.pData,
        sorted.pData + sorted.nCount,
        []( const component_t &a, const component_t &b ) noexcept {
            return a.minFaceId < b.minFaceId;
        } );
    for ( common::usize i = 0u; i < sorted.nCount; ++i ) {
        const common::usize index = FindComponentIndex(
            components, sorted.pData[i].dsuRoot );
        (void)common::Vector_PushBack(
            &outputOrder, static_cast<common::u32>( index ) );
    }
    common::Vector_Shutdown( &sorted );

    const common::usize cNewRoots = components.nCount - 1u;
    const common::usize cClaimed =
        GeometrySourceIdRegistry_ClaimedCount( &document.sourceIds );
    if ( cNewRoots > document.sourceIds.cEntriesMax - cClaimed ) {
        common::Vector_Shutdown( &outputOrder );
        cleanupComponents();
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    status = OwnedDescriptions_TryInit(
        pOutputsOut, document.pAllocator, components.nCount );
    geometry_source_id_allocator_t stagedIds = document.sourceIds.allocator;
    geometry_source_id_t firstNewRoot{};
    for ( common::usize iOutput = 0u;
          status == geometry_status_t::OK &&
          iOutput < outputOrder.nCount;
          ++iOutput ) {
        geometry_source_id_t rootId = source.sourceId;
        if ( iOutput > 0u ) {
            const geometry_source_id_result_t allocated =
                GeometrySourceIdAllocator_Allocate( &stagedIds );
            if ( allocated.status != geometry_status_t::OK ) {
                status = allocated.status;
                break;
            }
            rootId = allocated.id;
            if ( iOutput == 1u ) {
                firstNewRoot = rootId;
            }
        }
        mesh_source_description_t *pOutput = nullptr;
        status = OwnedDescriptions_TryAppendEmpty(
            pOutputsOut, rootId, &pOutput );
        if ( status == geometry_status_t::OK ) {
            status = BuildOneComponentDescription(
                source,
                faceComponents,
                outputOrder.pData[iOutput],
                rootId,
                pOutput );
        }
    }

    if ( status == geometry_status_t::OK ) {
        pReportOut->retainedRootId = source.sourceId;
        pReportOut->firstNewRootId = firstNewRoot;
        pReportOut->cInputObjects = 1u;
        pReportOut->cOutputObjects =
            static_cast<common::u32>( components.nCount );
        pReportOut->cNewRoots = static_cast<common::u32>( cNewRoots );
        pReportOut->cVertices =
            static_cast<common::u32>( source.vertices.nCount );
        pReportOut->cFaces =
            static_cast<common::u32>( source.faces.nCount );
        pReportOut->cShells =
            static_cast<common::u32>( components.nCount );
    } else {
        OwnedDescriptions_Shutdown( pOutputsOut );
    }
    common::Vector_Shutdown( &outputOrder );
    cleanupComponents();
    return status;
}

} // namespace

geometry_status_t GeometryMeshObjectCommand_TryJoinExact(
    geometry_document_t *pDocument,
    common::span_t<const geometry_source_id_t> inputRootIds,
    geometry_source_id_t retainedRootId,
    geometry_mesh_set_delta_t *pDeltaOut,
    mesh_object_command_report_t *pReportOut,
    geometry_revision_t *pNewRevisionOut ) noexcept
{
    if ( pReportOut != nullptr ) {
        *pReportOut = {};
    }
    geometry_status_t status = ValidateCommandBoundary(
        pDocument, pDeltaOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !common::Span_IsValid( inputRootIds ) ||
         inputRootIds.nCount < 2u ||
         !GeometrySourceId_IsValid( retainedRootId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    common::vector_t<const mesh_source_t *> inputs{};
    if ( !common::Vector_Init(
             &inputs, pDocument->pAllocator, inputRootIds.nCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    common::u32 cRetainedRoots = 0u;
    for ( common::usize i = 0u; i < inputRootIds.nCount; ++i ) {
        const geometry_source_id_t root = inputRootIds.pData[i];
        if ( !GeometrySourceId_IsValid( root ) ) {
            status = geometry_status_t::INVALID_ARGUMENT;
            break;
        }
        for ( common::usize j = 0u; j < i; ++j ) {
            if ( inputRootIds.pData[j].value == root.value ) {
                status = geometry_status_t::IDENTITY_CONFLICT;
                break;
            }
        }
        if ( status != geometry_status_t::OK ) {
            break;
        }
        const mesh_source_t *pMesh = GeometryDocument_FindMesh(
            pDocument, root );
        if ( pMesh == nullptr ) {
            status = geometry_status_t::INVALID_HANDLE;
            break;
        }
        if ( !common::Vector_PushBack( &inputs, pMesh ) ) {
            status = geometry_status_t::ALLOCATION_FAILED;
            break;
        }
        cRetainedRoots += root.value == retainedRootId.value ? 1u : 0u;
    }
    if ( status == geometry_status_t::OK && cRetainedRoots != 1u ) {
        status = geometry_status_t::INVALID_ARGUMENT;
    }
    if ( status != geometry_status_t::OK ) {
        common::Vector_Shutdown( &inputs );
        return status;
    }

    mesh_source_t joined{};
    mesh_source_join_report_t joinReport{};
    status = MeshSource_TryJoinExact(
        { inputs.pData, inputs.nCount },
        retainedRootId,
        pDocument->pAllocator,
        &joined,
        &joinReport );
    common::Vector_Shutdown( &inputs );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const common::allocator_t *pDeltaAllocator =
        pDeltaOut->before.meshes.pAllocator;
    owned_descriptions_t before{};
    status = CaptureDocumentDescriptions(
        *pDocument, pDeltaAllocator, &before );
    mesh_source_description_t joinedDescription{};
    if ( status == geometry_status_t::OK ) {
        status = MeshSourceDescription_Init(
            &joinedDescription, pDeltaAllocator, retainedRootId );
    }
    if ( status == geometry_status_t::OK ) {
        status = MeshSource_TryDescribe( &joined, &joinedDescription );
    }
    MeshSource_Shutdown( &joined );
    if ( status != geometry_status_t::OK ) {
        MeshSourceDescription_Shutdown( &joinedDescription );
        OwnedDescriptions_Shutdown( &before );
        return status;
    }

    common::vector_t<const mesh_source_description_t *> after{};
    if ( !common::Vector_Init(
             &after,
             pDeltaAllocator,
             before.values.nCount - inputRootIds.nCount + 1u ) ) {
        MeshSourceDescription_Shutdown( &joinedDescription );
        OwnedDescriptions_Shutdown( &before );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < before.values.nCount; ++i ) {
        const geometry_source_id_t root = before.values.pData[i]->sourceId;
        if ( root.value == retainedRootId.value ) {
            const mesh_source_description_t *pJoined = &joinedDescription;
            (void)common::Vector_PushBack( &after, pJoined );
        } else if ( !RootInSpan( inputRootIds, root ) ) {
            const mesh_source_description_t *pRetained =
                before.values.pData[i];
            (void)common::Vector_PushBack( &after, pRetained );
        }
    }

    mesh_object_command_report_t report{};
    report.retainedRootId = retainedRootId;
    report.cInputObjects = static_cast<common::u32>( inputRootIds.nCount );
    report.cOutputObjects = 1u;
    report.cVertices = joinReport.cVertices;
    report.cFaces = joinReport.cFaces;
    report.cShells = joinReport.cShells;
    status = CommitPreparedTransition(
        pDocument,
        { before.values.pData, before.values.nCount },
        { after.pData, after.nCount },
        pDeltaOut,
        pNewRevisionOut );
    if ( status == geometry_status_t::OK && pReportOut != nullptr ) {
        *pReportOut = report;
    }

    common::Vector_Shutdown( &after );
    MeshSourceDescription_Shutdown( &joinedDescription );
    OwnedDescriptions_Shutdown( &before );
    return status;
}

geometry_status_t GeometryMeshObjectCommand_TrySeparateByShell(
    geometry_document_t *pDocument,
    geometry_source_id_t sourceRootId,
    geometry_source_id_t retainedShellFaceId,
    geometry_mesh_set_delta_t *pDeltaOut,
    mesh_object_command_report_t *pReportOut,
    geometry_revision_t *pNewRevisionOut ) noexcept
{
    if ( pReportOut != nullptr ) {
        *pReportOut = {};
    }
    geometry_status_t status = ValidateCommandBoundary(
        pDocument, pDeltaOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !GeometrySourceId_IsValid( sourceRootId ) ||
         !GeometrySourceId_IsValid( retainedShellFaceId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const common::usize iSource = FindDocumentMeshIndex(
        *pDocument, sourceRootId );
    if ( iSource >= pDocument->meshes.nCount ) {
        return geometry_status_t::INVALID_HANDLE;
    }

    const common::allocator_t *pDeltaAllocator =
        pDeltaOut->before.meshes.pAllocator;
    owned_descriptions_t before{};
    status = CaptureDocumentDescriptions(
        *pDocument, pDeltaAllocator, &before );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    owned_descriptions_t outputs{};
    mesh_object_command_report_t report{};
    status = BuildSeparateOutputs(
        *pDocument,
        *before.values.pData[iSource],
        retainedShellFaceId,
        &outputs,
        &report );
    if ( status != geometry_status_t::OK ) {
        OwnedDescriptions_Shutdown( &before );
        return status;
    }

    const common::usize cAfter =
        before.values.nCount - 1u + outputs.values.nCount;
    common::vector_t<const mesh_source_description_t *> after{};
    if ( !common::Vector_Init( &after, pDeltaAllocator, cAfter ) ) {
        OwnedDescriptions_Shutdown( &outputs );
        OwnedDescriptions_Shutdown( &before );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u; i < before.values.nCount; ++i ) {
        if ( i == iSource ) {
            for ( common::usize j = 0u; j < outputs.values.nCount; ++j ) {
                const mesh_source_description_t *pOutput =
                    outputs.values.pData[j];
                (void)common::Vector_PushBack(
                    &after, pOutput );
            }
        } else {
            const mesh_source_description_t *pRetained =
                before.values.pData[i];
            (void)common::Vector_PushBack( &after, pRetained );
        }
    }

    status = CommitPreparedTransition(
        pDocument,
        { before.values.pData, before.values.nCount },
        { after.pData, after.nCount },
        pDeltaOut,
        pNewRevisionOut );
    if ( status == geometry_status_t::OK && pReportOut != nullptr ) {
        *pReportOut = report;
    }
    common::Vector_Shutdown( &after );
    OwnedDescriptions_Shutdown( &outputs );
    OwnedDescriptions_Shutdown( &before );
    return status;
}

} // namespace cypher::editor::geometry
