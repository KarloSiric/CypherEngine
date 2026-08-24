//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Engine/CypherCommon.h
//  Purpose: Declares the CypherCommon Common module.
//  Details: This file holds engine-scoped common metadata or helpers used across
//           runtime subsystems. Keep it narrow so it does not become a dumping
//           ground.
//
//  History:
//  - Created by Karlo Siric on 2026-04-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_COMMON_H
#define CYPHER_ENGINE_COMMON_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <cstddef>     // std::size_t.
#include <cstdint>     // Fixed-width integer types.
#include <limits>      // Numeric limits for invalid handles and constants.

#define CYPHER_STRINGIFY( x ) #x

#define CYPHER_TOSTRING( x ) CYPHER_STRINGIFY( x )

#define CYPHER_FILE_LINE __FILE__ ":" CYPHER_TOSTRING( __LINE__ )

namespace cypher::engine::common {

/*
================
Core Scalar Types

Engine-wide fixed-size aliases. These keep every subsystem speaking the same
integer, float and id language.
================
*/
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using byte = std::uint8_t;

using f32 = float;
using f64 = double;

using bool_t = bool;

using usize = std::size_t;

/*
================
Common Boolean Types

Use bool_t for normal runtime predicates. Use Bool when a stable one-byte
boolean representation matters for files, networking or serialized engine data.
================
*/
enum class Bool : u8 {
    False = 0u,
    True = 1u
};

constexpr bool_t COM_FALSE = false;
constexpr bool_t COM_TRUE = true;

using frame_index_t = u64;
using entity_id_t = u32;

constexpr frame_index_t COM_INVALID_FRAME_INDEX = std::numeric_limits<frame_index_t>::max();
constexpr entity_id_t COM_INVALID_ENTITY_ID = std::numeric_limits<entity_id_t>::max();

constexpr frame_index_t INVALID_FRAME_INDEX = COM_INVALID_FRAME_INDEX;
constexpr entity_id_t INVALID_ENTITY_ID = COM_INVALID_ENTITY_ID;

/*
================
Common Math Constants
This was moved to math_types header and will be kept there.
This is a fallback just in case.
================
*/
/*
constexpr f32 COM_PI_F = 3.14159265358979323846f;
constexpr f32 COM_TAU_F = 6.28318530717958647692f;
constexpr f32 COM_DEG2RAD_F = COM_PI_F / 180.0f;
constexpr f32 COM_RAD2DEG_F = 180.0f / COM_PI_F;
constexpr f32 COM_EPSILON_F = 1.0e-6f;
constexpr f32 COM_INFINITY_F = std::numeric_limits<f32>::infinity();

constexpr f32 PI_F = COM_PI_F;
constexpr f32 TAU_F = COM_TAU_F;
constexpr f32 DEG2RAD_F = COM_DEG2RAD_F;
constexpr f32 RAD2DEG_F = COM_RAD2DEG_F;
constexpr f32 EPSILON_F = COM_EPSILON_F;
constexpr f32 INFINITY_F = COM_INFINITY_F;

static_assert( sizeof( i8 ) == 1, "i8 must be 1 byte" );
static_assert( sizeof( i16 ) == 2, "i16 must be 2 bytes" );
static_assert( sizeof( i32 ) == 4, "i32 must be 4 bytes" );
static_assert( sizeof( i64 ) == 8, "i64 must be 8 bytes" );

static_assert( sizeof( u8 ) == 1, "u8 must be 1 byte" );
static_assert( sizeof( u16 ) == 2, "u16 must be 2 bytes" );
static_assert( sizeof( u32 ) == 4, "u32 must be 4 bytes" );
static_assert( sizeof( u64 ) == 8, "u64 must be 8 bytes" );
*/

/*
================
Product Information

Static identity data printed by the engine and exposed to commands/config.
================
*/
struct version_t {
	common::u32 major{ 0u }; // Breaking compatibility generation.
	common::u32 minor{ 1u }; // Backward-compatible feature generation.
	common::u32 patch{ 0u }; // Compatible fixes.
	common::u32 build{ 0u }; // Build-system revision.
};

struct product_info_t {
	const char *name{ nullptr };         // User-facing product name.
	const char *szInternalName{ nullptr }; // Stable filesystem/process identifier.
	const char *description{ nullptr };  // Short user-facing product description.

	version_t version{}; // Product semantic version.

	const char *szOrganizationName{ "CypherEngine" }; // Publisher/organization.
	const char *szCopyrightOwner{ "Karlo Siric" }; // Copyright holder.

	const char *szLicenseName{ "CypherEngine Proprietary License" }; // Display label.
	const char *szLicenseSpdx{ "LicenseRef-CypherEngine-Proprietary" }; // Stable ID.
};

constexpr product_info_t COM_GAME_INFO{
	.name = "REAP",
	.szInternalName = "reap",
	.description = "A from scratch 3D arena survival wave first person shooter inspired by Quake, powered by custom internal CypherEngine",
	.version = { 0u, 1u, 0u, 0u },
	.szOrganizationName = "CypherEngine",
	.szCopyrightOwner = "Karlo Siric",
	.szLicenseName = "CypherEngine Proprietary License",
	.szLicenseSpdx = "LicenseRef-CypherEngine-Proprietary"
};

constexpr product_info_t COM_ENGINE_INFO{
	.name = "CypherEngine",
	.szInternalName = "CypherEngine",
	.description = "A custom native 3D engine runtime used for powering REAP.",
	.version = { 0u, 1u, 0u, 0u },
	.szOrganizationName = "CypherEngine",
	.szCopyrightOwner = "Karlo Siric",
	.szLicenseName = "CypherEngine Proprietary License",
	.szLicenseSpdx = "LicenseRef-CypherEngine-Proprietary"
};

} // namespace cypher::engine::common

#endif // CYPHER_ENGINE_COMMON_H
