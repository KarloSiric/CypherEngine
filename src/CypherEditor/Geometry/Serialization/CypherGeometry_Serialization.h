//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Serialization.h
//  Purpose: Declares the versioned, deterministic binary format for
//           authored geometry documents.
//  Details: Wire layout, all fixed-width little-endian, version 1:
//
//             header   u32 magic "CYGD", u16 version, u16 flags (0),
//                      u64 nextSourceId, u32 brushCount, u32 reserved (0),
//                      u64 payloadBytes
//             brush    u64 brushId, u32 sideCount, u32 reserved (0),
//                      then sideCount sides
//             side     u64 sideId, f64 normal.xyz, f64 d, u64 material,
//                      f64 uv origin.xyz, uAxis.xyz, vAxis.xyz,
//                      normal.xyz, worldUnitsPerUv.xy, rotation,
//                      offset.xy
//             footer   u64 StableHash of header and payload
//
//           Brushes are written in ascending brush ID and sides in their
//           authored order, each carrying its own surfacing, so the bytes
//           are a pure function of the authored state: write, read, write
//           is byte-identical. Derived data (boundaries, bounds, handles,
//           caches) is never written; the reader rebuilds and fully
//           validates every brush.
//
//           The reader is hostile-input safe: it checks every count against
//           the policy's limits and the declared sizes against the actual
//           byte length before allocating, verifies the checksum before
//           trusting any field, requires canonical ordering and zeroed
//           reserved fields, and reports the byte offset of the first
//           problem.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SERIALIZATION_H
#define CYPHER_EDITOR_GEOMETRY_SERIALIZATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Snapshot.h"

#include "CypherCommon_Span.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 GEOMETRY_SERIALIZATION_MAGIC = 0x44475943u; // "CYGD"
inline constexpr common::u16 GEOMETRY_SERIALIZATION_VERSION = 1u;
inline constexpr common::usize GEOMETRY_SERIALIZATION_HEADER_BYTES = 32u;
inline constexpr common::usize GEOMETRY_SERIALIZATION_BRUSH_BYTES = 16u;
inline constexpr common::usize GEOMETRY_SERIALIZATION_SIDE_BYTES = 184u;
inline constexpr common::usize GEOMETRY_SERIALIZATION_FOOTER_BYTES = 8u;

struct geometry_serialize_report_t {
    geometry_status_t status{ geometry_status_t::OK };
    // Offset of the field where reading stopped (0 when not applicable).
    common::u64 byteOffset{ 0u };
    // Brush being read when a brush-level check failed.
    geometry_source_id_t brushId{};
};

// Exact encoded size of a snapshot, for callers that manage buffers.
CYPHER_NODISCARD geometry_status_t GeometrySerialize_TryMeasure(
    const geometry_document_snapshot_t *pSnapshot, common::usize *pBytesOut ) noexcept;

// Encodes a snapshot. nextSourceId is the document's identity high-water
// mark (GeometryDocument_NextSourceId) and must exceed every ID written.
// pBytesOut must be initialized; it is resized to exactly the encoded size.
// On failure it is left empty.
CYPHER_NODISCARD geometry_status_t GeometrySerialize_TryWriteSnapshot(
    const geometry_document_snapshot_t *pSnapshot,
    geometry_source_id_t nextSourceId,
    common::vector_t<common::byte> *pBytesOut ) noexcept;

// Convenience: snapshots the document at its current revision and encodes
// it.
CYPHER_NODISCARD geometry_status_t GeometrySerialize_TryWriteDocument(
    geometry_document_t *pDocument,
    common::vector_t<common::byte> *pBytesOut ) noexcept;

// Decodes bytes into a default-constructed document, initializing it with
// desc and publishing every brush as revision 2 (revision 1 is the empty
// document). On any failure the document is shut down (left default) and
// the report says what and where. Statuses: CORRUPT_STATE for structural
// or checksum problems, UNSUPPORTED for an unknown version or flags,
// LIMIT_EXCEEDED for counts over policy, IDENTITY_CONFLICT for duplicate
// IDs, and any brush validation status for invalid geometry.
CYPHER_NODISCARD geometry_status_t GeometrySerialize_TryReadDocument(
    common::span_t<const common::byte> bytes,
    const geometry_document_desc_t &desc,
    geometry_document_t *pDocument,
    geometry_serialize_report_t *pReportOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SERIALIZATION_H
