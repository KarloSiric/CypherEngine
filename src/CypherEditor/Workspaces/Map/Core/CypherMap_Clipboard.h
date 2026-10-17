//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Portable authored map selections. No Qt or system clipboard access.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_EDITOR_MAP_CLIPBOARD_H
#define CYPHER_EDITOR_MAP_CLIPBOARD_H
#include "CypherMap_Document.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"
namespace cypher::editor::map
{
inline constexpr common::usize MAP_CLIPBOARD_TEXT_MAX = 16u * common::CY_MIB;
// A paste is bounded on its own, well under what a map holds, so a hostile
// clipboard cannot make the editor build an enormous selection.
inline constexpr common::usize MAP_CLIPBOARD_VALUES_MAX = 1000000u;
inline constexpr common::usize MAP_CLIPBOARD_OBJECTS_MAX = 65536u;
inline constexpr const char *MAP_CLIPBOARD_SCHEMA_ID = "cypher.map.clipboard";
inline constexpr common::u32 MAP_CLIPBOARD_SCHEMA_VERSION = 1u;
// An independent deep text snapshot. Entities include recursive attached entities
// and their known owned geometry. Original selected-root order is preserved.
// Unsupported objects are rejected. Initialized output is unchanged on failure.
CYPHER_NODISCARD map_status_t MapClipboard_WriteSelection( const map_document_t *pSource,
    common::span_t<const common::u64> ids, common::text_buffer_t *pTextOut ) noexcept;
// PRIVATE destination only; discard it on failure, as for MapEdit operations.
// Fresh root/part IDs, path-based materials, retained authored fields, ownership
// and internal entity parents remapped to the fresh IDs,
// matching layers. Missing layers use the first destination layer. A child copied
// without its owner/parent is detached. Names/reference strings remain verbatim.
// Initialized output is unchanged on failure. One revision change on success.
CYPHER_NODISCARD map_status_t MapClipboard_Paste( map_document_t *pDestination,
    common::string_view_t text, math::vec3d_t offset, common::vector_t<common::u64> *pIdsOut ) noexcept;
// Bounded envelope/identity/ownership, acyclic internal parents and finite bounds
// validation. Every object must be reachable from a selected root. Actual
// geometry is validated under destination policy on paste. Output unchanged
// on failure; may be used for cursor centering before allocating an edit clone.
CYPHER_NODISCARD map_status_t MapClipboard_ReadBounds( const common::allocator_t *pAllocator,
    common::string_view_t text, map_bounds_t *pBoundsOut ) noexcept;
}
#endif
