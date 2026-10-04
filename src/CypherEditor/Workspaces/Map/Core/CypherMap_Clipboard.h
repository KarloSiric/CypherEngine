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
inline constexpr const char *MAP_CLIPBOARD_SCHEMA_ID = "cypher.map.clipboard";
inline constexpr common::u32 MAP_CLIPBOARD_SCHEMA_VERSION = 1u;
// An independent deep text snapshot. Entities include known owned geometry.
// Unsupported objects are rejected. Initialized output is unchanged on failure.
CYPHER_NODISCARD map_status_t MapClipboard_WriteSelection( const map_document_t *pSource,
    common::span_t<const common::u64> ids, common::text_buffer_t *pTextOut ) noexcept;
// PRIVATE destination only; discard it on failure, as for MapEdit operations.
// Fresh root/part IDs, path-based materials, retained authored fields, ownership,
// matching layers. Missing layers use the first destination layer. A child copied
// without its owner is detached. Names/reference strings remain verbatim.
// Initialized output is unchanged on failure. One revision change on success.
CYPHER_NODISCARD map_status_t MapClipboard_Paste( map_document_t *pDestination,
    common::string_view_t text, math::vec3d_t offset, common::vector_t<common::u64> *pIdsOut ) noexcept;
// Bounded envelope/identity/ownership and finite bounds validation. Actual
// geometry is validated under destination policy on paste. Output unchanged
// on failure; may be used for cursor centering before allocating an edit clone.
CYPHER_NODISCARD map_status_t MapClipboard_ReadBounds( const common::allocator_t *pAllocator,
    common::string_view_t text, map_bounds_t *pBoundsOut ) noexcept;
}
#endif
