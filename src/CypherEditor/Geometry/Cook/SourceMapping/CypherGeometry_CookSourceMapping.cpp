//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookSourceMapping.cpp
//  Purpose: Implements the neutral geometry Cook pipeline.
//  Details: Iterates every brush in document order, reconstructs each
//           boundary, tessellates it, and appends the resulting vertices
//           and triangles to a flat output buffer. Per-triangle source
//           records trace each triangle back to its brush and side. A
//           deterministic content hash is computed over the complete
//           vertex and triangle payload so that identical input always
//           produces an identical hash — the key invariant for bounded
//           incremental invalidation.
//
//           Brush order and side order within each brush follow the
//           canonical document order, which serialization preserves.
//           This guarantees that write → load → cook yields the same
//           hash as cook on the original document.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CookSourceMapping.h"

#include "CypherMath_UV.h"

#include <cstring>
#include <limits>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

geometry_cook_status_t FailCook(
    geometry_cook_result_t *pResult,
    geometry_cook_status_t status ) noexcept
{
    Vector_Clear( &pResult->vertices );
    Vector_Clear( &pResult->triangles );
    pResult->contentHash = CY_CONTENT_HASH_INVALID;
    pResult->status = status;
    return status;
}

geometry_cook_status_t FailFullCook(
    geometry_cook_full_result_t *pResult,
    geometry_cook_status_t status ) noexcept
{
    Vector_Clear( &pResult->vertices );
    Vector_Clear( &pResult->triangles );
    pResult->contentHash = CY_CONTENT_HASH_INVALID;
    pResult->status = status;
    return status;
}

bool CanAddressWithU32( usize iBase, usize cAdditional ) noexcept
{
    constexpr usize cMax =
        static_cast<usize>( std::numeric_limits<u32>::max() );
    return iBase <= cMax &&
           ( cAdditional == 0u || cAdditional - 1u <= cMax - iBase );
}

geometry_cook_status_t MapGeometryFailure(
    geometry_status_t status,
    geometry_cook_status_t fallback ) noexcept
{
    if ( status == geometry_status_t::ALLOCATION_FAILED ) {
        return geometry_cook_status_t::OUT_OF_MEMORY;
    }
    if ( status == geometry_status_t::LIMIT_EXCEEDED ) {
        return geometry_cook_status_t::LIMIT_EXCEEDED;
    }
    return fallback;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

geometry_cook_status_t GeometryCook_Init(
    geometry_cook_result_t *pResult,
    const allocator_t *pAllocator ) noexcept
{
    if ( pResult == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return geometry_cook_status_t::INVALID_ARGUMENT;
    }
    if ( pResult->vertices.pAllocator != nullptr ||
         pResult->triangles.pAllocator != nullptr ) {
        return geometry_cook_status_t::INVALID_ARGUMENT;
    }

    if ( !Vector_Init( &pResult->vertices, pAllocator ) ||
         !Vector_Init( &pResult->triangles, pAllocator ) ) {
        GeometryCook_Shutdown( pResult );
        return geometry_cook_status_t::OUT_OF_MEMORY;
    }

    pResult->contentHash = CY_CONTENT_HASH_INVALID;
    pResult->status = geometry_cook_status_t::OK;
    return geometry_cook_status_t::OK;
}

void GeometryCook_Shutdown(
    geometry_cook_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return;
    }
    Vector_Shutdown( &pResult->vertices );
    Vector_Shutdown( &pResult->triangles );
    pResult->contentHash = CY_CONTENT_HASH_INVALID;
    pResult->status = geometry_cook_status_t::OK;
}

// ---------------------------------------------------------------------------
// Cook
// ---------------------------------------------------------------------------

geometry_cook_status_t GeometryCook_TryCook(
    geometry_cook_result_t *pResult,
    const geometry_document_t *pDocument,
    const geometry_policy_t &policy ) noexcept
{
    if ( pResult == nullptr || pDocument == nullptr ||
         !GeometryPolicy_IsValid( policy ) ) {
        return geometry_cook_status_t::INVALID_ARGUMENT;
    }
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return geometry_cook_status_t::NOT_INITIALIZED;
    }

    const allocator_t *pAllocator = pResult->vertices.pAllocator;
    if ( !Allocator_IsValid( pAllocator ) ||
         pResult->triangles.pAllocator != pAllocator ) {
        return geometry_cook_status_t::INVALID_ARGUMENT;
    }

    // Clear any previous cook output.
    Vector_Clear( &pResult->vertices );
    Vector_Clear( &pResult->triangles );
    pResult->contentHash = CY_CONTENT_HASH_INVALID;

    const usize cBrushes = GeometryDocument_BrushCount( pDocument );

    // Per-brush temporaries — boundary and tessellation are rebuilt for
    // each brush and destroyed after their data is copied into the flat
    // output buffers.
    for ( usize iBrush = 0u; iBrush < cBrushes; ++iBrush ) {
        const brush_solid_t *pBrush = pDocument->brushes.pData[iBrush];
        if ( pBrush == nullptr ) {
            return FailCook(
                pResult, geometry_cook_status_t::BOUNDARY_FAILED );
        }

        // Reconstruct boundary from planes.
        brush_boundary_t boundary{};
        if ( BrushBoundary_Init( &boundary, pAllocator ) !=
             geometry_status_t::OK ) {
            return FailCook(
                pResult, geometry_cook_status_t::OUT_OF_MEMORY );
        }

        const geometry_status_t boundaryStatus =
            BrushBoundary_TryReconstruct( &boundary, pBrush, policy );
        if ( boundaryStatus != geometry_status_t::OK ) {
            BrushBoundary_Shutdown( &boundary );
            return FailCook(
                pResult,
                MapGeometryFailure(
                    boundaryStatus,
                    geometry_cook_status_t::BOUNDARY_FAILED ) );
        }

        // Tessellate boundary faces into triangles.
        brush_tessellation_t tess{};
        if ( BrushTessellation_Init( &tess, pAllocator ) !=
             geometry_status_t::OK ) {
            BrushBoundary_Shutdown( &boundary );
            return FailCook(
                pResult, geometry_cook_status_t::OUT_OF_MEMORY );
        }

        const geometry_status_t tessellationStatus =
            BrushTessellation_TryBuild( &tess, &boundary );
        if ( tessellationStatus != geometry_status_t::OK ) {
            BrushTessellation_Shutdown( &tess );
            BrushBoundary_Shutdown( &boundary );
            return FailCook(
                pResult,
                MapGeometryFailure(
                    tessellationStatus,
                    geometry_cook_status_t::TESSELLATION_FAILED ) );
        }

        // Copy boundary vertices into the flat output, offsetting
        // triangle indices by the current vertex base.
        const usize cVerts = BrushBoundary_VertexCount( &boundary );
        if ( !CanAddressWithU32( pResult->vertices.nCount, cVerts ) ) {
            BrushTessellation_Shutdown( &tess );
            BrushBoundary_Shutdown( &boundary );
            return FailCook(
                pResult, geometry_cook_status_t::LIMIT_EXCEEDED );
        }
        const u32 vertexBase =
            static_cast<u32>( pResult->vertices.nCount );
        for ( usize iV = 0u; iV < cVerts; ++iV ) {
            if ( !Vector_PushBack(
                     &pResult->vertices,
                     boundary.vertices.pData[iV] ) ) {
                BrushTessellation_Shutdown( &tess );
                BrushBoundary_Shutdown( &boundary );
                return FailCook(
                    pResult, geometry_cook_status_t::OUT_OF_MEMORY );
            }
        }

        // Copy tessellation triangles with source provenance.
        const usize cTris =
            BrushTessellation_TriangleCount( &tess );
        for ( usize iT = 0u; iT < cTris; ++iT ) {
            brush_tessellation_triangle_t srcTri{};
            if ( BrushTessellation_TryGetTriangle(
                     &tess, iT, &srcTri ) != geometry_status_t::OK ) {
                BrushTessellation_Shutdown( &tess );
                BrushBoundary_Shutdown( &boundary );
                return FailCook(
                    pResult,
                    geometry_cook_status_t::TESSELLATION_FAILED );
            }

            // Look up the side's source ID for the provenance record.
            brush_solid_side_t side{};
            if ( BrushSolid_TryGetSide(
                     pBrush, srcTri.iSourceSide, &side ) !=
                 geometry_status_t::OK ) {
                BrushTessellation_Shutdown( &tess );
                BrushBoundary_Shutdown( &boundary );
                return FailCook(
                    pResult,
                    geometry_cook_status_t::TESSELLATION_FAILED );
            }

            // Zero all bytes including struct padding so the raw-byte
            // content hash is deterministic across allocator states.
            cook_triangle_t tri;
            std::memset( &tri, 0, sizeof( tri ) );
            tri.iVertex0 = vertexBase + srcTri.iVertex0;
            tri.iVertex1 = vertexBase + srcTri.iVertex1;
            tri.iVertex2 = vertexBase + srcTri.iVertex2;
            tri.source.brushSourceId = pBrush->sourceId;
            tri.source.sideSourceId = side.sourceId;
            tri.source.iSideIndex = srcTri.iSourceSide;

            if ( !Vector_PushBack( &pResult->triangles, tri ) ) {
                BrushTessellation_Shutdown( &tess );
                BrushBoundary_Shutdown( &boundary );
                return FailCook(
                    pResult, geometry_cook_status_t::OUT_OF_MEMORY );
            }
        }

        BrushTessellation_Shutdown( &tess );
        BrushBoundary_Shutdown( &boundary );
    }

    // Compute deterministic content hash over the cooked payload.
    // Hash vertices and triangles as raw byte spans, then combine.
    const binary_block_t vertexBlock{
        reinterpret_cast<const u8 *>( pResult->vertices.pData ),
        pResult->vertices.nCount * sizeof( math::vec3d_t )
    };
    const binary_block_t triangleBlock{
        reinterpret_cast<const u8 *>( pResult->triangles.pData ),
        pResult->triangles.nCount * sizeof( cook_triangle_t )
    };

    content_hash_t hashVertices = ContentHash_Data( vertexBlock );
    content_hash_t hashTriangles = ContentHash_Data( triangleBlock );
    pResult->contentHash =
        ContentHash_Combine( hashVertices, hashTriangles );

    pResult->status = geometry_cook_status_t::OK;
    return geometry_cook_status_t::OK;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

usize GeometryCook_VertexCount(
    const geometry_cook_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return 0u;
    }
    return pResult->vertices.nCount;
}

usize GeometryCook_TriangleCount(
    const geometry_cook_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return 0u;
    }
    return pResult->triangles.nCount;
}

content_hash_t GeometryCook_ContentHash(
    const geometry_cook_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return CY_CONTENT_HASH_INVALID;
    }
    return pResult->contentHash;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

const char *GeometryCook_StatusName(
    geometry_cook_status_t status ) noexcept
{
    switch ( status ) {
        case geometry_cook_status_t::OK:
            return "OK";
        case geometry_cook_status_t::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case geometry_cook_status_t::NOT_INITIALIZED:
            return "NOT_INITIALIZED";
        case geometry_cook_status_t::OUT_OF_MEMORY:
            return "OUT_OF_MEMORY";
        case geometry_cook_status_t::LIMIT_EXCEEDED:
            return "LIMIT_EXCEEDED";
        case geometry_cook_status_t::BOUNDARY_FAILED:
            return "BOUNDARY_FAILED";
        case geometry_cook_status_t::TESSELLATION_FAILED:
            return "TESSELLATION_FAILED";
        case geometry_cook_status_t::ATTRIBUTE_FAILED:
            return "ATTRIBUTE_FAILED";
        case geometry_cook_status_t::HASH_FAILED:
            return "HASH_FAILED";
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// Full Cook (normals + UVs)
// ---------------------------------------------------------------------------

geometry_cook_status_t GeometryCookFull_Init(
    geometry_cook_full_result_t *pResult,
    const allocator_t *pAllocator ) noexcept
{
    if ( pResult == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return geometry_cook_status_t::INVALID_ARGUMENT;
    }
    if ( pResult->vertices.pAllocator != nullptr ||
         pResult->triangles.pAllocator != nullptr ) {
        return geometry_cook_status_t::INVALID_ARGUMENT;
    }

    if ( !Vector_Init( &pResult->vertices, pAllocator ) ||
         !Vector_Init( &pResult->triangles, pAllocator ) ) {
        GeometryCookFull_Shutdown( pResult );
        return geometry_cook_status_t::OUT_OF_MEMORY;
    }

    pResult->contentHash = CY_CONTENT_HASH_INVALID;
    pResult->status = geometry_cook_status_t::OK;
    return geometry_cook_status_t::OK;
}

void GeometryCookFull_Shutdown(
    geometry_cook_full_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return;
    }
    Vector_Shutdown( &pResult->vertices );
    Vector_Shutdown( &pResult->triangles );
    pResult->contentHash = CY_CONTENT_HASH_INVALID;
    pResult->status = geometry_cook_status_t::OK;
}

namespace
{

// Builds a default axis-aligned planar UV projection from a face normal.
// The dominant axis determines the projection plane:
//   dominant X → project on YZ, U along Z, V along Y
//   dominant Y → project on XZ, U along X, V along Z
//   dominant Z → project on XY, U along X, V along Y
math::vec2d_t DefaultPlanarUV(
    math::vec3d_t position,
    math::vec3d_t faceNormal ) noexcept
{
    const f64 ax = math::Scalar_Abs( faceNormal.x );
    const f64 ay = math::Scalar_Abs( faceNormal.y );
    const f64 az = math::Scalar_Abs( faceNormal.z );

    if ( ax >= ay && ax >= az ) {
        return math::Vec2d_Make( position.z, position.y );
    }
    if ( ay >= ax && ay >= az ) {
        return math::Vec2d_Make( position.x, position.z );
    }
    return math::Vec2d_Make( position.x, position.y );
}

// Projects a world point through one explicitly referenced attribute record.
// A provided store is authoritative, so a dangling index or invalid mapping is
// a cook error rather than a request for the default projection.
bool TryProjectUVFromAttributes(
    math::vec3d_t position,
    const geometry_brush_side_attribute_store_t *pStore,
    usize iAttribute,
    const geometry_numerical_policy_t &policy,
    math::vec2d_t *pUvOut ) noexcept
{
    *pUvOut = {};
    geometry_brush_side_attributes_t attrs{};
    if ( BrushSideAttributeStore_TryGet( pStore, iAttribute, &attrs ) !=
         geometry_status_t::OK ) {
        return false;
    }
    if ( BrushSideAttributes_Validate( policy, attrs ) !=
         geometry_status_t::OK ) {
        return false;
    }

    return math::Uvd_TryProjectPlanarPoint(
        attrs.uvProjection, position,
        policy.fAbsoluteDistanceTolerance, pUvOut );
}

bool TryMapBoundaryVertexToSplitIndex(
    const brush_boundary_t &boundary,
    const brush_boundary_face_t &face,
    u32 iBoundaryVertex,
    u32 splitBase,
    u32 *pSplitIndexOut ) noexcept
{
    *pSplitIndexOut = std::numeric_limits<u32>::max();
    for ( u32 iFaceVertex = 0u;
          iFaceVertex < face.cVertices;
          ++iFaceVertex ) {
        const usize iFaceIndex =
            static_cast<usize>( face.iFirstIndex ) + iFaceVertex;
        if ( iFaceIndex >= boundary.faceVertexIndices.nCount ) {
            return false;
        }
        if ( boundary.faceVertexIndices.pData[iFaceIndex] ==
             iBoundaryVertex ) {
            *pSplitIndexOut = splitBase + iFaceVertex;
            return true;
        }
    }
    return false;
}

} // namespace

geometry_cook_status_t GeometryCook_TryCookFull(
    geometry_cook_full_result_t *pResult,
    const geometry_document_t *pDocument,
    const geometry_policy_t &policy,
    const cook_brush_attributes_t *pBrushAttributes,
    usize cBrushAttributes ) noexcept
{
    if ( pResult == nullptr || pDocument == nullptr ||
         !GeometryPolicy_IsValid( policy ) ||
         ( pBrushAttributes == nullptr && cBrushAttributes != 0u ) ) {
        return geometry_cook_status_t::INVALID_ARGUMENT;
    }
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return geometry_cook_status_t::NOT_INITIALIZED;
    }

    const allocator_t *pAllocator = pResult->vertices.pAllocator;
    if ( !Allocator_IsValid( pAllocator ) ||
         pResult->triangles.pAllocator != pAllocator ) {
        return geometry_cook_status_t::INVALID_ARGUMENT;
    }

    Vector_Clear( &pResult->vertices );
    Vector_Clear( &pResult->triangles );
    pResult->contentHash = CY_CONTENT_HASH_INVALID;

    const usize cBrushes = GeometryDocument_BrushCount( pDocument );

    for ( usize iBrush = 0u; iBrush < cBrushes; ++iBrush ) {
        const brush_solid_t *pBrush = pDocument->brushes.pData[iBrush];
        if ( pBrush == nullptr ) {
            return FailFullCook(
                pResult, geometry_cook_status_t::BOUNDARY_FAILED );
        }

        // Reconstruct boundary.
        brush_boundary_t boundary{};
        if ( BrushBoundary_Init( &boundary, pAllocator ) !=
             geometry_status_t::OK ) {
            return FailFullCook(
                pResult, geometry_cook_status_t::OUT_OF_MEMORY );
        }

        const geometry_status_t boundaryStatus =
            BrushBoundary_TryReconstruct( &boundary, pBrush, policy );
        if ( boundaryStatus != geometry_status_t::OK ) {
            BrushBoundary_Shutdown( &boundary );
            return FailFullCook(
                pResult,
                MapGeometryFailure(
                    boundaryStatus,
                    geometry_cook_status_t::BOUNDARY_FAILED ) );
        }

        // Tessellate.
        brush_tessellation_t tess{};
        if ( BrushTessellation_Init( &tess, pAllocator ) !=
             geometry_status_t::OK ) {
            BrushBoundary_Shutdown( &boundary );
            return FailFullCook(
                pResult, geometry_cook_status_t::OUT_OF_MEMORY );
        }

        const geometry_status_t tessellationStatus =
            BrushTessellation_TryBuild( &tess, &boundary );
        if ( tessellationStatus != geometry_status_t::OK ) {
            BrushTessellation_Shutdown( &tess );
            BrushBoundary_Shutdown( &boundary );
            return FailFullCook(
                pResult,
                MapGeometryFailure(
                    tessellationStatus,
                    geometry_cook_status_t::TESSELLATION_FAILED ) );
        }

        // Resolve the attribute store for this brush (may be null).
        const geometry_brush_side_attribute_store_t *pAttrStore =
            nullptr;
        if ( pBrushAttributes != nullptr && iBrush < cBrushAttributes ) {
            pAttrStore = pBrushAttributes[iBrush].pStore;
        }

        // Emit per-face split vertices with normals and UVs.
        // Build a local remap table: for each boundary face, emit its
        // vertices and record the new global index for each face-vertex.
        const usize cFaces = boundary.faces.nCount;
        for ( usize iFace = 0u; iFace < cFaces; ++iFace ) {
            const brush_boundary_face_t &face =
                boundary.faces.pData[iFace];

            // Get the face normal from the brush side plane.
            brush_solid_side_t side{};
            if ( BrushSolid_TryGetSide(
                     pBrush, face.iSide, &side ) !=
                 geometry_status_t::OK ) {
                BrushTessellation_Shutdown( &tess );
                BrushBoundary_Shutdown( &boundary );
                return FailFullCook(
                    pResult,
                    geometry_cook_status_t::TESSELLATION_FAILED );
            }
            const math::vec3d_t faceNormal = side.plane.normal;

            if ( !CanAddressWithU32(
                     pResult->vertices.nCount, face.cVertices ) ) {
                BrushTessellation_Shutdown( &tess );
                BrushBoundary_Shutdown( &boundary );
                return FailFullCook(
                    pResult, geometry_cook_status_t::LIMIT_EXCEEDED );
            }

            // Emit this face's vertices.
            const u32 splitBase =
                static_cast<u32>( pResult->vertices.nCount );
            for ( u32 iVert = 0u; iVert < face.cVertices; ++iVert ) {
                const usize iFaceIndex =
                    static_cast<usize>( face.iFirstIndex ) + iVert;
                if ( iFaceIndex >= boundary.faceVertexIndices.nCount ) {
                    BrushTessellation_Shutdown( &tess );
                    BrushBoundary_Shutdown( &boundary );
                    return FailFullCook(
                        pResult,
                        geometry_cook_status_t::TESSELLATION_FAILED );
                }
                const u32 iBoundaryVert =
                    boundary.faceVertexIndices.pData[iFaceIndex];

                if ( iBoundaryVert >= boundary.vertices.nCount ) {
                    BrushTessellation_Shutdown( &tess );
                    BrushBoundary_Shutdown( &boundary );
                    return FailFullCook(
                        pResult,
                        geometry_cook_status_t::TESSELLATION_FAILED );
                }

                const math::vec3d_t pos =
                    boundary.vertices.pData[iBoundaryVert];

                cook_vertex_t cv;
                std::memset( &cv, 0, sizeof( cv ) );
                cv.position = pos;
                cv.normal = faceNormal;

                if ( pAttrStore != nullptr ) {
                    if ( !TryProjectUVFromAttributes(
                             pos, pAttrStore, side.iAttributeIndex,
                             policy.numerical,
                             &cv.uv ) ) {
                        BrushTessellation_Shutdown( &tess );
                        BrushBoundary_Shutdown( &boundary );
                        return FailFullCook(
                            pResult,
                            geometry_cook_status_t::ATTRIBUTE_FAILED );
                    }
                } else {
                    cv.uv = DefaultPlanarUV( pos, faceNormal );
                }

                if ( !Vector_PushBack( &pResult->vertices, cv ) ) {
                    BrushTessellation_Shutdown( &tess );
                    BrushBoundary_Shutdown( &boundary );
                    return FailFullCook(
                        pResult, geometry_cook_status_t::OUT_OF_MEMORY );
                }
            }

            // Remap tessellation triangles for this face.
            const usize cTris =
                BrushTessellation_TriangleCount( &tess );
            for ( usize iT = 0u; iT < cTris; ++iT ) {
                brush_tessellation_triangle_t srcTri{};
                if ( BrushTessellation_TryGetTriangle(
                         &tess, iT, &srcTri ) !=
                     geometry_status_t::OK ) {
                    BrushTessellation_Shutdown( &tess );
                    BrushBoundary_Shutdown( &boundary );
                    return FailFullCook(
                        pResult,
                        geometry_cook_status_t::TESSELLATION_FAILED );
                }

                // Only process triangles that belong to this face.
                if ( srcTri.iSourceSide != face.iSide ) {
                    continue;
                }

                cook_triangle_t tri;
                std::memset( &tri, 0, sizeof( tri ) );
                if ( !TryMapBoundaryVertexToSplitIndex(
                         boundary, face, srcTri.iVertex0,
                         splitBase, &tri.iVertex0 ) ||
                     !TryMapBoundaryVertexToSplitIndex(
                         boundary, face, srcTri.iVertex1,
                         splitBase, &tri.iVertex1 ) ||
                     !TryMapBoundaryVertexToSplitIndex(
                         boundary, face, srcTri.iVertex2,
                         splitBase, &tri.iVertex2 ) ) {
                    BrushTessellation_Shutdown( &tess );
                    BrushBoundary_Shutdown( &boundary );
                    return FailFullCook(
                        pResult,
                        geometry_cook_status_t::TESSELLATION_FAILED );
                }
                tri.source.brushSourceId = pBrush->sourceId;
                tri.source.sideSourceId = side.sourceId;
                tri.source.iSideIndex = srcTri.iSourceSide;

                if ( !Vector_PushBack( &pResult->triangles, tri ) ) {
                    BrushTessellation_Shutdown( &tess );
                    BrushBoundary_Shutdown( &boundary );
                    return FailFullCook(
                        pResult, geometry_cook_status_t::OUT_OF_MEMORY );
                }
            }
        }

        BrushTessellation_Shutdown( &tess );
        BrushBoundary_Shutdown( &boundary );
    }

    // Compute deterministic content hash.
    const binary_block_t vertexBlock{
        reinterpret_cast<const u8 *>( pResult->vertices.pData ),
        pResult->vertices.nCount * sizeof( cook_vertex_t )
    };
    const binary_block_t triangleBlock{
        reinterpret_cast<const u8 *>( pResult->triangles.pData ),
        pResult->triangles.nCount * sizeof( cook_triangle_t )
    };

    content_hash_t hV = ContentHash_Data( vertexBlock );
    content_hash_t hT = ContentHash_Data( triangleBlock );
    pResult->contentHash = ContentHash_Combine( hV, hT );

    pResult->status = geometry_cook_status_t::OK;
    return geometry_cook_status_t::OK;
}

usize GeometryCookFull_VertexCount(
    const geometry_cook_full_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return 0u;
    }
    return pResult->vertices.nCount;
}

usize GeometryCookFull_TriangleCount(
    const geometry_cook_full_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return 0u;
    }
    return pResult->triangles.nCount;
}

content_hash_t GeometryCookFull_ContentHash(
    const geometry_cook_full_result_t *pResult ) noexcept
{
    if ( pResult == nullptr ) {
        return CY_CONTENT_HASH_INVALID;
    }
    return pResult->contentHash;
}

} // namespace cypher::editor::geometry
