//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Types.h
//  Purpose: Defines identity and result vocabulary for editable geometry.
//  Details: Persistent source IDs survive serialization and undo; live topology
//           handles carry a slot generation and remain local to an owning mesh.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_TYPES_H
#define CYPHER_EDITOR_GEOMETRY_TYPES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BaseTypes.h"

#include <type_traits>

namespace cypher::editor::geometry
{

using common::u8;
using common::u32;
using common::u64;

// Source IDs are document-local, monotonic, and never zero. Clipboard, import,
// merge, and duplication operations explicitly remap them into the destination
// document. They identify authored elements across save/load and undo/redo.
struct geometry_source_id_t {
    u64 value{ 0u };
};

inline constexpr geometry_source_id_t GEOMETRY_SOURCE_ID_INVALID{};

enum class geometry_element_kind_t : u8 {
    MESH = 0u,
    SHELL,
    VERTEX,
    HALF_EDGE,
    EDGE,
    LOOP,
    FACE,
    COUNT
};

// Live handles are meaningful only to their owning mesh. Generation zero and
// the all-one slot are invalid. Separate template instantiations prevent a face
// handle from being passed accidentally where a vertex handle is required.
template<geometry_element_kind_t kind_v>
struct geometry_handle_t {
    u32 nSlot{ common::CY_INVALID_INDEX };
    u32 nGeneration{ 0u };
};

using geometry_mesh_handle_t =
    geometry_handle_t<geometry_element_kind_t::MESH>;
using geometry_shell_handle_t =
    geometry_handle_t<geometry_element_kind_t::SHELL>;
using geometry_vertex_handle_t =
    geometry_handle_t<geometry_element_kind_t::VERTEX>;
using geometry_half_edge_handle_t =
    geometry_handle_t<geometry_element_kind_t::HALF_EDGE>;
using geometry_edge_handle_t =
    geometry_handle_t<geometry_element_kind_t::EDGE>;
using geometry_loop_handle_t =
    geometry_handle_t<geometry_element_kind_t::LOOP>;
using geometry_face_handle_t =
    geometry_handle_t<geometry_element_kind_t::FACE>;

template<geometry_element_kind_t kind_v>
inline constexpr geometry_handle_t<kind_v> GEOMETRY_HANDLE_INVALID{};

struct geometry_element_ref_t {
    geometry_source_id_t id{};
    geometry_element_kind_t kind{ geometry_element_kind_t::COUNT };
};

enum class geometry_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    NOT_INITIALIZED,
    ALREADY_INITIALIZED,
    INVALID_HANDLE,
    STALE_HANDLE,
    INVALID_TOPOLOGY,
    NON_MANIFOLD,
    DEGENERATE,
    NON_PLANAR,
    SELF_INTERSECTING,
    OPEN_VOLUME,
    NUMERIC_FAILURE,
    INSUFFICIENT_CAPACITY,
    ALLOCATION_FAILED,
    UNSUPPORTED,
    COUNT
};

[[nodiscard]] constexpr bool GeometrySourceId_IsValid(
    geometry_source_id_t id ) noexcept
{
    return id.value != 0u;
}

template<geometry_element_kind_t kind_v>
[[nodiscard]] constexpr bool GeometryHandle_IsValid(
    geometry_handle_t<kind_v> handle ) noexcept
{
    return handle.nSlot != common::CY_INVALID_INDEX &&
           handle.nGeneration != 0u;
}

[[nodiscard]] constexpr bool GeometryElementKind_IsValid(
    geometry_element_kind_t kind ) noexcept
{
    return static_cast<u8>( kind ) <
           static_cast<u8>( geometry_element_kind_t::COUNT );
}

[[nodiscard]] constexpr bool GeometryElementRef_IsValid(
    geometry_element_ref_t ref ) noexcept
{
    return GeometrySourceId_IsValid( ref.id ) &&
           GeometryElementKind_IsValid( ref.kind );
}

static_assert( sizeof( geometry_source_id_t ) == sizeof( u64 ) );
static_assert( sizeof( geometry_vertex_handle_t ) == sizeof( u32 ) * 2u );
static_assert( std::is_standard_layout_v<geometry_source_id_t> );
static_assert( std::is_trivially_copyable_v<geometry_source_id_t> );
static_assert( std::is_standard_layout_v<geometry_vertex_handle_t> );
static_assert( std::is_trivially_copyable_v<geometry_vertex_handle_t> );

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_TYPES_H

