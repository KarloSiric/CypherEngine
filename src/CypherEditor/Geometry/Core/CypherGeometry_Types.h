//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Types.h
//  Purpose: Defines identity, representation, handle, and result vocabulary for
//           editable geometry.
//  Details: Persistent source IDs survive serialization and undo. Live handles
//           carry a slot generation and are qualified by their owning pool tag.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//  - Representation ownership and handle typing corrected on 2026-09-18
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
#include "CypherCommon_GenerationPool.h"

#include <type_traits>

namespace cypher::editor::geometry {

using common::u32;
using common::u64;
using common::u8;

// Source IDs are document-local, monotonic, and never zero. Clipboard, import,
// merge, and duplication explicitly remap them into the destination document.
// Live handles are never serialized in place of these identities.
struct geometry_source_id_t {
	u64 value{ 0u };
};

inline constexpr geometry_source_id_t GEOMETRY_SOURCE_ID_INVALID{};

// This enum is deliberately not an inventory of every geometric concept. It
// discriminates only persistent canonical authoring representations. Primitives,
// component domains, neutral exchange records, Boolean intermediates, modifiers,
// queries, semantic scene volumes, and cooked products have separate contracts.
// Explicit values aid diagnostics; source formats still use reviewed wire codes
// rather than serializing the compiler enum representation.
enum class geometry_source_representation_kind_t : u8 {
	INVALID = 0u,
	BRUSH_SOLID = 1u,
	EDITABLE_MESH = 2u,
	PLANAR_REGION = 3u,
	PATCH_SURFACE = 4u,
	CURVE_NETWORK = 5u,
	HEIGHT_FIELD = 6u,
	COUNT = 7u
};

enum class geometry_status_t : u8 {
	OK = 0u,
	INVALID_ARGUMENT,
	NOT_INITIALIZED,
	ALREADY_INITIALIZED,
	CORRUPT_STATE,
	INVALID_HANDLE,
	STALE_HANDLE,
	INVALID_TOPOLOGY,
	NON_MANIFOLD,
	DEGENERATE,
	NON_PLANAR,
	SELF_INTERSECTING,
	OPEN_VOLUME,
	NUMERIC_FAILURE,
	LIMIT_EXCEEDED,
	INSUFFICIENT_CAPACITY,
	ALLOCATION_FAILED,
	IDENTITY_CONFLICT,
	UNSUPPORTED,
	COUNT
};

// Pool tags deliberately have no runtime state. Their only job is to prevent
// components with identical-looking handles from becoming interchangeable C++
// types. Pools are document-owned; a live handle has meaning only with the pool
// that produced it. Cross-document transfer uses source IDs and explicit remaps.
struct geometry_brush_tag_t {};
struct geometry_brush_side_tag_t {};

struct geometry_mesh_tag_t {};
struct geometry_mesh_shell_tag_t {};
struct geometry_mesh_vertex_tag_t {};
struct geometry_mesh_half_edge_tag_t {};
struct geometry_mesh_edge_tag_t {};
struct geometry_mesh_loop_tag_t {};
struct geometry_mesh_face_tag_t {};

struct geometry_planar_region_tag_t {};
struct geometry_planar_polygon_tag_t {};
struct geometry_planar_contour_tag_t {};
struct geometry_planar_vertex_tag_t {};
struct geometry_planar_segment_tag_t {};

struct geometry_patch_tag_t {};
struct geometry_patch_control_point_tag_t {};

struct geometry_curve_network_tag_t {};
struct geometry_curve_tag_t {};
struct geometry_curve_control_point_tag_t {};

struct geometry_height_field_tag_t {};
struct geometry_height_field_tile_tag_t {};
struct geometry_height_field_sample_tag_t {};

template <typename pool_tag_t>
using geometry_handle_t = common::generation_handle_t<pool_tag_t>;

template <typename source_tag_t>
struct geometry_source_ref_t {
	geometry_source_id_t id{};
};

template <typename pool_tag_t>
struct geometry_handle_result_t {
	geometry_status_t status{ geometry_status_t::INVALID_HANDLE };
	geometry_handle_t<pool_tag_t> handle{};
};

using geometry_brush_handle_t = geometry_handle_t<geometry_brush_tag_t>;
using geometry_brush_side_handle_t = geometry_handle_t<geometry_brush_side_tag_t>;

using geometry_mesh_handle_t = geometry_handle_t<geometry_mesh_tag_t>;
using geometry_mesh_shell_handle_t = geometry_handle_t<geometry_mesh_shell_tag_t>;
using geometry_mesh_vertex_handle_t = geometry_handle_t<geometry_mesh_vertex_tag_t>;
using geometry_mesh_half_edge_handle_t =
	geometry_handle_t<geometry_mesh_half_edge_tag_t>;
using geometry_mesh_edge_handle_t = geometry_handle_t<geometry_mesh_edge_tag_t>;
using geometry_mesh_loop_handle_t = geometry_handle_t<geometry_mesh_loop_tag_t>;
using geometry_mesh_face_handle_t = geometry_handle_t<geometry_mesh_face_tag_t>;

using geometry_planar_region_handle_t =
	geometry_handle_t<geometry_planar_region_tag_t>;
using geometry_planar_polygon_handle_t =
	geometry_handle_t<geometry_planar_polygon_tag_t>;
using geometry_planar_contour_handle_t =
	geometry_handle_t<geometry_planar_contour_tag_t>;
using geometry_planar_vertex_handle_t =
	geometry_handle_t<geometry_planar_vertex_tag_t>;
using geometry_planar_segment_handle_t =
	geometry_handle_t<geometry_planar_segment_tag_t>;

using geometry_patch_handle_t = geometry_handle_t<geometry_patch_tag_t>;
using geometry_patch_control_point_handle_t =
	geometry_handle_t<geometry_patch_control_point_tag_t>;

using geometry_curve_network_handle_t =
	geometry_handle_t<geometry_curve_network_tag_t>;
using geometry_curve_handle_t = geometry_handle_t<geometry_curve_tag_t>;
using geometry_curve_control_point_handle_t =
	geometry_handle_t<geometry_curve_control_point_tag_t>;

using geometry_height_field_handle_t =
	geometry_handle_t<geometry_height_field_tag_t>;
using geometry_height_field_tile_handle_t =
	geometry_handle_t<geometry_height_field_tile_tag_t>;
using geometry_height_field_sample_handle_t =
	geometry_handle_t<geometry_height_field_sample_tag_t>;

template <typename pool_tag_t>
inline constexpr geometry_handle_t<pool_tag_t> GEOMETRY_HANDLE_INVALID =
	common::GENERATION_HANDLE_INVALID<pool_tag_t>;

template <typename source_tag_t>
inline constexpr geometry_source_ref_t<source_tag_t> GEOMETRY_SOURCE_REF_INVALID{};

[[nodiscard]] constexpr bool GeometrySourceId_IsValid(
	geometry_source_id_t id ) noexcept {
	return id.value != 0u;
}

template <typename pool_tag_t>
[[nodiscard]] constexpr bool GeometryHandle_IsValid(
	geometry_handle_t<pool_tag_t> handle ) noexcept {
	return common::GenerationHandle_IsValid( handle );
}

template <typename source_tag_t>
[[nodiscard]] constexpr bool GeometrySourceRef_IsValid(
	geometry_source_ref_t<source_tag_t> ref ) noexcept {
	return GeometrySourceId_IsValid( ref.id );
}

[[nodiscard]] constexpr bool GeometrySourceRepresentationKind_IsValid(
	geometry_source_representation_kind_t kind ) noexcept {
	return static_cast<u8>( kind ) > static_cast<u8>( geometry_source_representation_kind_t::INVALID ) && static_cast<u8>( kind ) < static_cast<u8>( geometry_source_representation_kind_t::COUNT );
}

[[nodiscard]] constexpr geometry_status_t GeometryStatus_FromGenerationPoolStatus(
	common::generation_pool_status_t status ) noexcept {
	switch ( status ) {
		case common::generation_pool_status_t::OK:
			return geometry_status_t::OK;
		case common::generation_pool_status_t::INVALID_ARGUMENT:
			return geometry_status_t::INVALID_ARGUMENT;
		case common::generation_pool_status_t::NOT_INITIALIZED:
			return geometry_status_t::NOT_INITIALIZED;
		case common::generation_pool_status_t::ALREADY_INITIALIZED:
			return geometry_status_t::ALREADY_INITIALIZED;
		case common::generation_pool_status_t::INVALID_HANDLE:
			return geometry_status_t::INVALID_HANDLE;
		case common::generation_pool_status_t::STALE_HANDLE:
			return geometry_status_t::STALE_HANDLE;
		case common::generation_pool_status_t::LIMIT_EXCEEDED:
			return geometry_status_t::LIMIT_EXCEEDED;
		case common::generation_pool_status_t::ALLOCATION_FAILED:
			return geometry_status_t::ALLOCATION_FAILED;
		case common::generation_pool_status_t::CORRUPT_STATE:
			return geometry_status_t::CORRUPT_STATE;
		case common::generation_pool_status_t::COUNT:
			break;
	}
	return geometry_status_t::CORRUPT_STATE;
}

static_assert( sizeof( geometry_source_id_t ) == sizeof( u64 ) );
static_assert( sizeof( geometry_mesh_vertex_handle_t ) == sizeof( u32 ) * 2u );
static_assert( std::is_standard_layout_v<geometry_source_id_t> );
static_assert( std::is_trivially_copyable_v<geometry_source_id_t> );
static_assert( std::is_standard_layout_v<geometry_mesh_vertex_handle_t> );
static_assert( std::is_trivially_copyable_v<geometry_mesh_vertex_handle_t> );

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_TYPES_H
