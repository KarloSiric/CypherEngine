//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushSerialization.h
//  Purpose: Declares deterministic CYKV persistence for brush geometry.
//  Details: Serializes brush_solid_t planes, source IDs, and attribute
//           indices into a versioned CYKV 1 text format. Loading validates
//           all fields, rejects malformed input within bounded budgets,
//           and produces an initialized brush or a diagnostic result.
//
//           The wire format uses explicit field names and never serializes
//           raw C++ enum ordinals, COUNT sentinels, or live handles.
//
//           Schema: cypher.geometry, version 2.
//
//           Version 2 persists the document identity high-water mark and the
//           complete claimed-ID set. This prevents an ID retired before save
//           from becoming admissible again after load. Version 1 remains
//           readable and is migrated by reconstructing identity ownership from
//           the live brushes present in that older format.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_SERIALIZATION_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_SERIALIZATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_Document.h"
#include "CypherGeometry_Policy.h"

#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueWriter.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

namespace cypher::editor::geometry
{

// Schema identifier and version stamped in the CYKV header. The schema
// covers the brush-source wire format only — cooked products, tile maps,
// and scene documents use separate schemas.
inline constexpr const char *GEOMETRY_SCHEMA_ID = "cypher.geometry";
inline constexpr common::u32 GEOMETRY_SCHEMA_VERSION_OLDEST_SUPPORTED = 1u;
// Version 3 adds the optional "meshes" section (MeshSerialization.h).
inline constexpr common::u32 GEOMETRY_SCHEMA_VERSION = 3u;

// Budget limits for parsing untrusted input. These prevent a malformed
// file from consuming unbounded memory before validation rejects it.
inline constexpr common::usize GEOMETRY_SERIALIZATION_MAX_TEXT_BYTES =
    32u * common::CY_MIB;
inline constexpr common::usize GEOMETRY_SERIALIZATION_MAX_BRUSHES =
    65536u;
inline constexpr common::usize GEOMETRY_SERIALIZATION_MAX_SIDES_PER_BRUSH =
    256u;
// A claimed-ID entry needs at least four source bytes in canonical CYKV text
// (one digit, unsigned suffix, separator/newline). The text budget therefore
// imposes this tighter bound before the parser can construct an excessive
// scalar-node array from hostile input.
inline constexpr common::usize GEOMETRY_SERIALIZATION_MAX_CLAIMED_SOURCE_IDS =
    GEOMETRY_SERIALIZATION_MAX_TEXT_BYTES / 4u;

// ---------------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------------

enum class geometry_serialization_status_t : common::u8 {
    OK = 0u,

    INVALID_ARGUMENT,
    DESTINATION_NOT_EMPTY,
    NOT_INITIALIZED,
    LIMIT_EXCEEDED,
    OUT_OF_MEMORY,

    CYKV_WRITE_FAILED,
    CYKV_PARSE_FAILED,
    HEADER_MISMATCH,
    SCHEMA_MISMATCH,
    VERSION_UNSUPPORTED,

    MISSING_FIELD,
    TYPE_MISMATCH,
    VALUE_OUT_OF_RANGE,
    INVALID_SOURCE_ID,
    DUPLICATE_SOURCE_ID,
    PLANE_NOT_NORMALIZED,
    CORRUPT_DOCUMENT,

    DOCUMENT_INIT_FAILED
};

struct geometry_serialization_result_t {
    geometry_serialization_status_t status{
        geometry_serialization_status_t::OK
    };

    // Lower-level status from the CYKV layer, if applicable.
    common::key_value_parse_status_t parseStatus{
        common::key_value_parse_status_t::OK
    };
    common::key_value_write_status_t writeStatus{
        common::key_value_write_status_t::OK
    };

    // Logical path to the field that caused the failure.
    char field[96]{};

    // Array element index when the failure is inside a repeated field.
    common::usize iElement{ common::CY_INVALID_SIZE };

    // Character count of the serialized text on success.
    common::usize cchText{ 0u };
};

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

// Serializes a geometry document's brush source data into deterministic
// CYKV 1 text. The output replaces the contents of pTextOut only after
// a complete successful encode. The destination must be an initialized
// TextBuffer with an allocator binding.
CYPHER_NODISCARD geometry_serialization_result_t
GeometrySerialization_SaveToText(
    const geometry_document_t *pDocument,
    common::text_buffer_t *pTextOut ) noexcept;

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

// Loads brush source data from CYKV text into a fresh geometry document.
// The destination must be default-initialized (not yet Init'd). On
// success the document is fully initialized with the deserialized
// brushes. On failure the destination is left untouched.
CYPHER_NODISCARD geometry_serialization_result_t
GeometrySerialization_LoadFromText(
    common::string_view_t text,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_document_t *pDocumentOut ) noexcept;

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

CYPHER_NODISCARD const char *GeometrySerialization_StatusName(
    geometry_serialization_status_t status ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_SERIALIZATION_H
