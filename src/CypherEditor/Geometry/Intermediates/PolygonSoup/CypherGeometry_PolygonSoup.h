//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PolygonSoup.h
//  Purpose: Declares PolygonSoup, the bounded neutral record for indexed
//           polygons that carry no authoritative adjacency.
//  Details: A soup is what importers, exporters, repair, and CSG stages
//           exchange before (or instead of) a checked representation. It
//           deliberately promises nothing about manifoldness, orientation
//           consistency, or welding: those are *findings* reported by
//           PolygonSoup_Validate and *decisions* taken by Sanitation, never
//           silent fixes (ARCHITECTURE.md, Intermediates README).
//
//           Storage: positions[], corners[] (vertex index per corner), and
//           faces[] = {first corner, corner count, source ID, group}.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_POLYGON_SOUP_H
#define CYPHER_EDITOR_GEOMETRY_POLYGON_SOUP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherCommon_Vector.h"
#include "CypherCommon_Span.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

// Bounds match the editable mesh defaults so a soup that fits can always
// be sized into a mesh without re-checking.
inline constexpr common::usize kPolygonSoupVerticesMax = 65536u;
inline constexpr common::usize kPolygonSoupFacesMax = 65536u;
inline constexpr common::usize kPolygonSoupCornersMax = 262144u;
inline constexpr common::usize kPolygonSoupFaceCornersMax = 256u;

struct polygon_soup_face_t {
    common::u32 iFirstCorner{ 0u };
    common::u32 cCorners{ 0u };
    geometry_source_id_t sourceId{};  // may be invalid for anonymous input
    common::u32 iGroup{ 0u };         // importer-defined grouping (material, object)
};

struct polygon_soup_t {
    common::vector_t<math::vec3d_t> positions{};
    common::vector_t<common::u32> corners{};
    common::vector_t<polygon_soup_face_t> faces{};
};

// ---------------------------------------------------------------------------
// Lifecycle and building
// ---------------------------------------------------------------------------

CYPHER_NODISCARD geometry_status_t PolygonSoup_Init(
    polygon_soup_t *pSoup,
    const common::allocator_t *pAllocator ) noexcept;

void PolygonSoup_Shutdown( polygon_soup_t *pSoup ) noexcept;

CYPHER_NODISCARD bool PolygonSoup_IsInitialized( const polygon_soup_t *pSoup ) noexcept;

void PolygonSoup_Clear( polygon_soup_t *pSoup ) noexcept;

// Appends a vertex. Non-finite positions are *accepted* (a soup records
// input as given) and reported by Validate; only bounds are enforced here.
CYPHER_NODISCARD geometry_status_t PolygonSoup_TryAddVertex(
    polygon_soup_t *pSoup,
    math::vec3d_t position,
    common::u32 *pIndexOut ) noexcept;

// Appends a face. Indices are stored as given (out-of-range indices are a
// Validate finding), but corner count must be in [1, FaceCornersMax] and
// global bounds hold. Failure-atomic.
CYPHER_NODISCARD geometry_status_t PolygonSoup_TryAddFace(
    polygon_soup_t *pSoup,
    common::span_t<const common::u32> vertexIndices,
    geometry_source_id_t sourceId,
    common::u32 iGroup,
    common::u32 *pFaceIndexOut ) noexcept;

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

CYPHER_NODISCARD common::usize PolygonSoup_VertexCount( const polygon_soup_t *pSoup ) noexcept;
CYPHER_NODISCARD common::usize PolygonSoup_FaceCount( const polygon_soup_t *pSoup ) noexcept;

// Vertex indices of one face, or empty for an out-of-range index.
CYPHER_NODISCARD common::span_t<const common::u32> PolygonSoup_FaceCorners(
    const polygon_soup_t *pSoup,
    common::usize iFace ) noexcept;

// Newell vector area of a face (length = 2 x area, direction = normal by
// CCW winding). Zero for faces with invalid indices.
CYPHER_NODISCARD math::vec3d_t PolygonSoup_FaceVectorArea(
    const polygon_soup_t *pSoup,
    common::usize iFace ) noexcept;

// ---------------------------------------------------------------------------
// Validation (findings only; never mutates)
// ---------------------------------------------------------------------------

enum class polygon_soup_fault_t : common::u8 {
    NONE              = 0u,
    NON_FINITE        = 1u, // iVertex
    INDEX_OUT_OF_RANGE = 2u, // iFace, iCorner
    TOO_FEW_CORNERS   = 3u, // iFace
    REPEATED_CORNER   = 4u, // iFace, iCorner (vertex appears twice in one face)
    DEGENERATE_AREA   = 5u  // iFace (vector area below fMinimumFaceArea)
};

struct polygon_soup_validation_t {
    geometry_status_t status{ geometry_status_t::OK };
    polygon_soup_fault_t fault{ polygon_soup_fault_t::NONE };
    common::u32 iFace{ CY_INVALID_INDEX };
    common::u32 iCorner{ CY_INVALID_INDEX };
    common::u32 iVertex{ CY_INVALID_INDEX };
    // Counts over the whole soup (validation keeps scanning after the first
    // fault so importers can report totals).
    common::u32 cFaultyFaces{ 0u };
    common::u32 cNonFiniteVertices{ 0u };
};

// Per-face structural checks. Status of the first fault:
// NON_FINITE -> NUMERIC_FAILURE, INDEX_OUT_OF_RANGE -> INVALID_ARGUMENT,
// TOO_FEW_CORNERS / REPEATED_CORNER / DEGENERATE_AREA -> DEGENERATE.
// Manifoldness is not checked here — it is a property of how faces fit
// together, decided by Sanitation.
CYPHER_NODISCARD polygon_soup_validation_t PolygonSoup_Validate(
    const polygon_soup_t *pSoup,
    common::f64 fMinimumFaceArea ) noexcept;

CYPHER_NODISCARD const char *PolygonSoupFault_Name( polygon_soup_fault_t fault ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_POLYGON_SOUP_H
