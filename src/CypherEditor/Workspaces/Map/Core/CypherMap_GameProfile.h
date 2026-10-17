//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_GameProfile.h
//  Purpose: Declares decoding game profiles (`.cygame`, cypher.game V1,
//           CYGAME.md): a game's entity classes with their properties,
//           inputs, outputs and editor look, its map settings, editor
//           defaults and migrations - the editor's FGD.
//  Details: Decoding is tolerant (CYGAME.md 7): an unusable class or
//           property is skipped and reported, a missing base or a base
//           cycle is reported and the class keeps what resolves, and the
//           first of two classes with one name wins. Classes come out with
//           their bases folded in, in base order, a later definition
//           replacing an earlier one with the same key or name.
//
//  History:
//  - Created by Karlo Siric on 2026-10-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GAME_PROFILE_H
#define CYPHER_EDITOR_MAP_GAME_PROFILE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cypher::editor::map
{

inline constexpr const char *MAP_GAME_SCHEMA_ID = "cypher.game";
inline constexpr common::u32 MAP_GAME_SCHEMA_VERSION = 1u;
inline constexpr common::usize MAP_GAME_CLASSES_MAX = 4096u;
inline constexpr common::usize MAP_GAME_PROPERTIES_MAX = 256u;
inline constexpr common::usize MAP_GAME_IO_MAX = 128u;
inline constexpr common::u32 MAP_GAME_DEPTH_MAX = 16u; // Base chains and includes.
inline constexpr common::usize MAP_GAME_FILE_MAX = 16u * 1024u * 1024u;

enum class map_game_type_t : common::u8 {
    BOOL = 0u, INTEGER, REAL, STRING, CHOICE, FLAGS, COLOR, VECTOR, ANGLES, ASSET, TARGET, OBJECT, OBJECTS, SHAPE, FACES
};
enum class map_game_kind_t : common::u8 { POINT = 0u, SOLID, ABSTRACT };

struct map_game_property_t {
    std::string key{}, label{}, description{}, group{}, unit{};
    map_game_type_t type{ map_game_type_t::STRING };
    const common::key_value_t *pDefault{ nullptr }; // In the profile's documents; null without a usable default.
    bool bMin{ false }, bMax{ false };
    common::f64 min{ 0.0 }, max{ 0.0 };
    std::vector<std::string> choices{}, extensions{}, targetClasses{};
    bool bRequired{ false }, bReadOnly{ false }, bHidden{ false };
};

struct map_game_io_t {
    std::string name{}, type{ "void" }, description{};
};

struct map_game_helper_t {
    std::string type{}, property{}, angle{}, range{}, distance{};
};

struct map_game_class_t {
    std::string name{}, label{}, category{}, description{};
    map_game_kind_t kind{ map_game_kind_t::POINT };
    std::vector<std::string> bases{};
    // Editor look (CYGAME.md 4).
    bool bSize{ false };
    common::f64 size[6]{};
    std::string color{}, colorFrom{}, model{}, modelFrom{}, sizeFrom{}, icon{}, material{};
    std::vector<map_game_helper_t> helpers{};
    // Own and inherited.
    std::vector<map_game_property_t> properties{};
    std::vector<map_game_io_t> inputs{}, outputs{};
};

enum class map_game_migration_kind_t : common::u8 { RENAME_CLASS = 0u, RENAME_PROPERTY };
struct map_game_migration_t {
    common::u64 version{ 0u };
    map_game_migration_kind_t kind{ map_game_migration_kind_t::RENAME_CLASS };
    std::string className{}, from{}, to{};
};

struct map_game_document_delete_t { void operator()( common::key_value_document_t *pDocument ) const noexcept; };

struct map_game_profile_t {
    std::string id{}, name{};
    common::u64 version{ 0u };
    common::f64 unitsPerMeter{ 39.37 };
    std::string defaultPointClass{}, defaultSolidClass{}, sprites{};
    std::vector<std::string> categories{};
    std::vector<map_game_property_t> mapSettings{};
    std::vector<map_game_class_t> classes{}; // File order, includes after the file that names them.
    std::vector<map_game_migration_t> migrations{};
    std::vector<std::string> problems{};     // What was skipped, and why.
    std::vector<std::string> files{};        // Every file read, the profile first.
    std::vector<std::unique_ptr<common::key_value_document_t, map_game_document_delete_t>> documents{}; // Defaults point here.
};

// Reads a file by canonical virtual path; false when it cannot.
using map_game_read_fn = std::function<bool( const std::string &virtualPath, std::string &textOut )>;

// Loads the profile at `path` and its includes. ROOT_UNREADABLE: the
// profile file cannot be read or parsed, or is not cypher.game V1;
// ROOT_INVALID: it lacks id, name or version. Problems in included files
// and definitions are reported in `problems` and do not fail the load.
CYPHER_NODISCARD map_status_t MapGame_Load( const std::string &path, const map_game_read_fn &read, map_game_profile_t &out );

CYPHER_NODISCARD const map_game_class_t *MapGame_FindClass( const map_game_profile_t &profile, common::string_view_t name ) noexcept;
CYPHER_NODISCARD const map_game_property_t *MapGame_FindProperty( const map_game_class_t &cls, common::string_view_t key ) noexcept;
CYPHER_NODISCARD const char *MapGame_TypeName( map_game_type_t type ) noexcept;

// Whether `value` is a valid value of the property (type, choices, limits);
// *pWhyOut says what is wrong.
CYPHER_NODISCARD bool MapGame_CheckValue( const map_game_property_t &property, const common::key_value_t *pValue, std::string *pWhyOut = nullptr );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GAME_PROFILE_H
