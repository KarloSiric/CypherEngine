//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Typed entity authoring operations over a private map working copy.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_EDITOR_MAP_ENTITY_EDIT_H
#define CYPHER_EDITOR_MAP_ENTITY_EDIT_H
#pragma once

#include "CypherMap_Document.h"

namespace cypher::editor::map
{
inline constexpr common::usize MAP_ENTITY_PROPERTY_KEY_MAX = 128u;
inline constexpr common::usize MAP_ENTITY_PROPERTIES_MAX = 512u;
inline constexpr common::usize MAP_ENTITY_RECORD_MEMBERS_MAX = 512u;
inline constexpr common::usize MAP_ENTITY_PROPERTY_VALUE_DEPTH_MAX = 64u;
inline constexpr common::usize MAP_ENTITY_PROPERTY_VALUE_NODES_MAX = 4096u;
inline constexpr common::usize MAP_ENTITY_PROPERTY_VALUE_BYTES_MAX = common::CY_MIB;

// Typed semantic equality: object key order is irrelevant, array order matters,
// and numeric kinds remain distinct (1, 1u, and 1.0 are different values).
// Both null pointers are equal; malformed or over-budget trees compare unequal.
CYPHER_NODISCARD bool MapEntityEdit_ValuesEqual( const common::key_value_t *pLeft,
    const common::key_value_t *pRight ) noexcept;

// Validated top-level entity lookup for inspectors, including read-only maps.
// Returns null for an unknown/unusable entity or invalid map. Borrowed node
// remains usable only until mutation or replacement of the owning document.
// Optional chunk output is cleared first and assigned only on success.
CYPHER_NODISCARD const common::key_value_t *MapEntityEdit_FindEntity(
    const map_document_t *pMap, common::u64 entityId, map_chunk_t **ppChunkOut = nullptr ) noexcept;

// Selection may contain entities or their live owned brushes/meshes/patches.
// World geometry and unusable records reject the whole selection. Owners are
// deduplicated in selection order. The result must be initialized and empty;
// it remains empty on failure. Read-only maps can be inspected with this query.
CYPHER_NODISCARD map_status_t MapEntityEdit_ResolveOwners( const map_document_t *pMap,
    common::span_t<const common::u64> selection, common::vector_t<common::u64> *pOwnersOut ) noexcept;

// Mutations require direct entity IDs and a PRIVATE map working copy. On any
// failure discard that copy; a batch may already have changed earlier owners.
// *pChangedOut is required, starts false, and is true only on successful actual
// authored changes. No-op edits return OK/false. Geometry revisions, IDs,
// outputs, ownership, and unrelated fields are untouched.
// Keys are case-sensitive nonempty UTF-8 names without NUL, up to 128 bytes.
// Values preserve CYKV types and nested data. Duplicate object keys, malformed
// properties, nonfinite reals, and values exceeding the bounded policy reject
// the batch. A value borrowed from the map is safe: it is staged before editing.
CYPHER_NODISCARD map_status_t MapEntityEdit_SetProperty( map_document_t *pMap,
    common::span_t<const common::u64> entityIds, common::string_view_t key,
    const common::key_value_t *pValue, bool *pChangedOut ) noexcept;
CYPHER_NODISCARD map_status_t MapEntityEdit_RemoveProperty( map_document_t *pMap,
    common::span_t<const common::u64> entityIds, common::string_view_t key, bool *pChangedOut ) noexcept;
// Missing source keys are no-ops. Every destination collision is checked before
// any owner changes. Renaming to the same key is a no-op.
CYPHER_NODISCARD map_status_t MapEntityEdit_RenameProperty( map_document_t *pMap,
    common::span_t<const common::u64> entityIds, common::string_view_t oldKey,
    common::string_view_t newKey, bool *pChangedOut ) noexcept;
// Only the canonical CYMAP fields "class" and "name" are editable here.
// Class must be nonempty; name is the intentionally nonunique I/O target name.
CYPHER_NODISCARD map_status_t MapEntityEdit_SetIdentityField( map_document_t *pMap,
    common::span_t<const common::u64> entityIds, common::string_view_t field,
    common::string_view_t value, bool *pChangedOut ) noexcept;
} // namespace cypher::editor::map
#endif
