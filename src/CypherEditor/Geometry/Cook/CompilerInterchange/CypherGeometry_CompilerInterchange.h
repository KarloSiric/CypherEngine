//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CompilerInterchange.h
//  Purpose: Declares the compiler interchange: a versioned, bounded,
//           self-checking CYKV snapshot of the cooked map surfaces plus
//           their dependency keys, for headless compilers (BSP/vis,
//           lightmapper, navmesh builder) and peer tools that run outside
//           the editor process.
//  Details: Schema cypher.geometry.interchange v1:
//             revision        the snapshot revision it was cooked from
//             policy_hash     the geometry policy's hash (hex)
//             sources[]       { id, kind, source_hash } per object - the
//                             dependency metadata a compiler uses to skip
//                             unchanged inputs
//             objects[]       { id, kind, first_triangle, triangle_count,
//                             first_vertex, vertex_count, closed, convex }
//             positions       binary, little-endian f64 x y z per vertex
//             triangles       binary, little-endian per triangle:
//                             u32 v0 v1 v2, u32 object index,
//                             u64 element id, u64 material
//             content_hash    hash of everything above (hex)
//           Bulk arrays are binary blobs: exact doubles, compact, and
//           trivially bounded; the small structured parts stay readable.
//           Reading validates the header, every count and index, and the
//           content hash, so a truncated or edited file is rejected rather
//           than compiled. Final runtime map formats are not this (README).
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_COMPILER_INTERCHANGE_H
#define CYPHER_EDITOR_GEOMETRY_COMPILER_INTERCHANGE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CookSurfaces.h"

#include "CypherCommon/Tier1/CypherCommon_StringView.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

namespace cypher::editor::geometry
{

inline constexpr const char *GEOMETRY_INTERCHANGE_SCHEMA_ID = "cypher.geometry.interchange";
inline constexpr common::u32 GEOMETRY_INTERCHANGE_VERSION = 1u;
inline constexpr common::usize kGeometryInterchangeTextMax = 512u * 1024u * 1024u;

struct geometry_interchange_t {
    geometry_revision_t revision{ GEOMETRY_REVISION_INITIAL };
    common::content_hash_t policyHash{};
    common::content_hash_t contentHash{};
    common::vector_t<cook_source_key_t> sources{};
    cook_surface_soup_t soup{}; // problems are not carried
};

CYPHER_NODISCARD geometry_status_t GeometryInterchange_Init( geometry_interchange_t *pOut, const common::allocator_t *pAllocator ) noexcept;
void GeometryInterchange_Shutdown( geometry_interchange_t *pOut ) noexcept;

// Writes the soup and keys (built from the same snapshot revision:
// INVALID_ARGUMENT otherwise) as interchange text into *pTextOut.
CYPHER_NODISCARD geometry_status_t GeometryInterchange_TryWrite(
    const cook_key_set_t *pKeys,
    const cook_surface_soup_t *pSoup,
    common::text_buffer_t *pTextOut ) noexcept;

// Reads interchange text into *pOut (initialized; replaced). UNSUPPORTED
// for another schema or version, CORRUPT_STATE for inconsistent counts,
// indices or hash, INVALID_ARGUMENT for text that is not CYKV. Failure
// leaves *pOut empty.
CYPHER_NODISCARD geometry_status_t GeometryInterchange_TryRead(
    common::string_view_t text,
    geometry_interchange_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_COMPILER_INTERCHANGE_H
