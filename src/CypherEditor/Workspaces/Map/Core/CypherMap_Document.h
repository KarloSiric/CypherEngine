//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Document.h
//  Purpose: Declares the map document: the in-memory form of a `.cymap` root
//           and its `.cymapchunk` chunks (cypher.map V10, CYMAP.md), with
//           loading, saving, identity, placement, and object operations.
//  Details: Each chunk's parsed tree is kept, so everything the editor does
//           not understand - future object kinds, plugin data, members a
//           newer build wrote - is written back unchanged, and an object
//           that cannot be read stays exactly where it was. Geometry is
//           lifted out of the chunks into one geometry document that editing
//           tools work on directly; its map-owned members (name, comment,
//           visgroups, terrain paint, mesh modifiers) stay in the chunk
//           record and are written back beside the regenerated geometry.
//
//           No file I/O happens here. Loading takes the root text and every
//           chunk file's path and text; saving hands each chunk's text to a
//           sink, root last, and names chunk files that should be deleted.
//           The editor, the map compiler, and tests share this code.
//
//           Chunk paths are relative to the map's chunk directory:
//           "<layer>/<cell>.cymapchunk" (CYMAP.md section 1).
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - Reworked on 2026-09-27 for map-owned readable geometry (CYMAP.md 6)
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_DOCUMENT_H
#define CYPHER_EDITOR_MAP_DOCUMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Cell.h"
#include "CypherMap_Geometry.h"

#include "CypherGeometry_Document.h"

#include "CypherCommon/Tier1/CypherCommon_Span.h"
#include "CypherCommon/Tier1/CypherCommon_UniqueId.h"
#include "CypherCommon/Tier1/CypherCommon_Vector.h"
#include "CypherCommon/Tier2/CypherCommon_SettingsDocument.h"

namespace cypher::editor::map
{

inline constexpr const char *MAP_SCHEMA_ID = "cypher.map";
inline constexpr const char *MAP_CHUNK_SCHEMA_ID = "cypher.map_chunk";
inline constexpr common::u32 MAP_SCHEMA_VERSION = 10u;
inline constexpr const char *MAP_FILE_EXTENSION = ".cymap";
inline constexpr const char *MAP_CHUNK_FILE_EXTENSION = ".cymapchunk";

inline constexpr common::usize MAP_ROOT_TEXT_MAX = 1u * common::CY_MIB;
inline constexpr common::usize MAP_CHUNK_TEXT_MAX = 16u * common::CY_MIB; // = CY_SETTINGS_TEXT_MAX_BYTES
// Values (CYKV nodes) per chunk; under the reader's 2^20 budget so every
// chunk the writer accepts is one the reader accepts. A box brush is about
// 127 values, so dense brush work reaches this before the text limit.
inline constexpr common::usize MAP_CHUNK_VALUES_MAX = 1000000u;
inline constexpr common::usize MAP_LAYERS_MAX = 256u;
inline constexpr common::usize MAP_VISGROUPS_MAX = 4096u;
inline constexpr common::usize MAP_SELECTION_SETS_MAX = 4096u;
inline constexpr common::usize MAP_CORDONS_MAX = 256u;
inline constexpr common::usize MAP_CHUNK_OBJECTS_MAX = 65536u;
inline constexpr common::usize MAP_FOLIAGE_INSTANCES_MAX = 1000000u;
inline constexpr common::usize MAP_LAYER_ID_CAPACITY = 65u;   // 64 bytes + NUL.
inline constexpr common::usize MAP_CHUNK_PATH_CAPACITY = 160u;
inline constexpr common::usize MAP_NAME_MAX_LENGTH = 128u;
inline constexpr common::f64 MAP_DEFAULT_UNITS_PER_METER = 39.37;
inline constexpr common::u16 MAP_LINE_WIDTH = 200u;            // Compact layout width: an ordinary brush face fits on one line (CYMAP.md 3).

enum class map_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    OUT_OF_MEMORY,
    ROOT_UNREADABLE,     // Root is not CYKV, has another schema, or another version.
    ROOT_INVALID,        // A required root member is missing or invalid; see the problem list.
    READ_ONLY,           // Duplicate IDs: the map opened, but cannot be saved.
    SAVE_BLOCKED,        // A damaged chunk would be overwritten, or two files disagree; resolve it first.
    SINK_FAILED,         // The caller's write or remove callback reported failure.
    LIMIT_EXCEEDED,      // A chunk would exceed a format limit; cut the layer into smaller cells.
    GEOMETRY_FAILED,     // The geometry library refused an operation.
    UNKNOWN_OBJECT,      // No object with that ID.
    UNKNOWN_LAYER        // No layer with that ID.
};

enum class map_problem_code_t : common::u8 {
    ROOT_MEMBER_INVALID = 0u, // A required root member (named in `member`).
    ROOT_MEMBER_IGNORED,      // An optional root member was invalid; its default applies.
    CHUNK_UNREADABLE,         // Not CYKV, not cypher.map_chunk V10, or malformed sections; file kept untouched.
    CHUNK_FOREIGN_MAP,        // map_id belongs to another map; file kept untouched.
    CHUNK_LAYER_ADOPTED,      // Chunk names a layer the root does not declare; layer added.
    CHUNK_CELL_INVALID,       // `cell` unreadable; objects are placed by position on save.
    CHUNK_PATH_MISMATCH,      // File path differs from the chunk's layer and cell; rewritten on save.
    OBJECT_UNREADABLE,        // An object that cannot be read; kept verbatim (`member` names the section and reason).
    ID_ASSIGNED,              // An object or part had no ID; a fresh one was assigned.
    DUPLICATE_ID,             // Two things share an ID; the map is read-only.
    ID_ABOVE_NEXT_ID,         // An ID not below next_id; next_id was raised.
    FOLIAGE_PROPERTIES_DIFFER // Records of one model in one layer disagree; the first is kept on save.
};

struct map_problem_t {
    map_problem_code_t code{ map_problem_code_t::ROOT_MEMBER_INVALID };
    char path[MAP_CHUNK_PATH_CAPACITY]{}; // Chunk path, empty for the root.
    char member[48]{};                    // Offending member or section, when one is named.
    common::u64 id{ 0u };                 // Object ID, when one is involved.
};

struct map_layer_t {
    char id[MAP_LAYER_ID_CAPACITY]{};
};

// One chunk file's worth of objects: one layer, one cell.
struct map_chunk_t {
    map_chunk_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( map_chunk_t );
    ~map_chunk_t() noexcept = default;

    common::settings_document_t store{};         // The chunk tree; geometry records hold only map-owned members.
    char layer[MAP_LAYER_ID_CAPACITY]{};
    map_cell_t cell{};
    char sourcePath[MAP_CHUNK_PATH_CAPACITY]{};   // Path it was read from; empty when new.
    common::bool_t bDamaged{ common::CY_FALSE }; // Never written, never deleted.
};

// Where a geometry object belongs: its layer, and the entity that owns it
// (0 for world geometry). The geometry library itself knows neither.
struct map_geometry_record_t {
    common::u64 id{ 0u };
    common::u32 iLayer{ 0u };  // Index into map_document_t::layers.
    common::u64 owner{ 0u };   // Owning entity ID, or 0.
};

struct map_document_t {
    map_document_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( map_document_t );
    ~map_document_t() noexcept;

    common::settings_document_t root{};                        // cypher.map V10 tree.
    common::vector_t<map_chunk_t *> chunks{};                  // Owned; heap-allocated (non-movable).
    common::vector_t<map_layer_t> layers{};                    // Root layers, then adopted ones.
    geometry::geometry_document_t geometry{};                  // All of the map's geometry.
    geometry::geometry_policy_t geometryPolicy{};
    common::vector_t<map_geometry_record_t> geometryRecords{}; // Sorted by ID.
    map_materials_t materials{};                               // Material paths used by faces and patches.
    common::vector_t<map_problem_t> problems{};
    common::unique_id_t mapId{};
    map_cell_grid_t grid{};
    common::u64 nextId{ 1u };
    common::bool_t bReadOnly{ common::CY_FALSE };
    common::bool_t bIdsAssigned{ common::CY_FALSE };           // Load gave IDs; saving writes them.
    const common::allocator_t *pAllocator{ nullptr };
};

struct map_create_desc_t {
    common::string_view_t name{};  // Display name.
    common::string_view_t game{};  // Game profile ID (stable identifier).
    common::string_view_t layer{}; // First layer ID; "default" when empty.
};

struct map_chunk_input_t {
    common::string_view_t path{}; // Relative to the chunk directory.
    common::string_view_t text{};
};

enum map_load_flags_t : common::u32 {
    MAP_LOAD_FLAG_NONE = 0u,
    // Give every repeated ID after its first occurrence (in path order, then
    // document order) a fresh ID: the "Reassign Duplicate IDs" command.
    MAP_LOAD_FLAG_REASSIGN_DUPLICATES = 1u << 0u
};

// Receives save output. Chunks come first, the root last; a false return
// stops the save with SINK_FAILED.
struct map_save_sink_t {
    void *pContext{ nullptr };
    common::bool_t ( *pfnWriteChunk )( void *pContext, common::string_view_t path, common::string_view_t text ) noexcept{ nullptr };
    common::bool_t ( *pfnRemoveChunk )( void *pContext, common::string_view_t path ) noexcept{ nullptr };
    common::bool_t ( *pfnWriteRoot )( void *pContext, common::string_view_t text ) noexcept{ nullptr };
};

// Creates an empty map with a fresh random map_id.
CYPHER_NODISCARD map_status_t MapDocument_Create(
    map_document_t *pMap,
    const common::allocator_t *pAllocator,
    const map_create_desc_t &desc ) noexcept;

// Loads a map. pMap must be default-constructed. ROOT_* failures leave it
// empty except for problems; chunk-level damage is recorded in problems and
// the rest loads. Chunks are processed in path order whatever the input
// order, so IDs assigned on load are the same on every machine.
CYPHER_NODISCARD map_status_t MapDocument_Load(
    map_document_t *pMap,
    const common::allocator_t *pAllocator,
    common::string_view_t rootText,
    common::span_t<const map_chunk_input_t> chunks,
    common::u32 flags = MAP_LOAD_FLAG_NONE ) noexcept;

void MapDocument_Shutdown( map_document_t *pMap ) noexcept;

// Writes every chunk (objects placed by layer and cell, sorted by ID, in the
// canonical member order and compact layout), names chunk files that no
// longer have objects, then writes the root. Nothing is written unless every
// chunk can be. On success the document reflects the saved layout.
CYPHER_NODISCARD map_status_t MapDocument_Save( map_document_t *pMap, const map_save_sink_t &sink ) noexcept;

// Allocates a fresh object ID, keeping geometry allocation above it.
CYPHER_NODISCARD common::u64 MapDocument_AllocateId( map_document_t *pMap ) noexcept;

CYPHER_NODISCARD common::bool_t MapDocument_HasLayer( const map_document_t *pMap, common::string_view_t layer ) noexcept;

// Adds a layer to the root; an existing ID is OK.
CYPHER_NODISCARD map_status_t MapDocument_AddLayer(
    map_document_t *pMap,
    common::string_view_t layer,
    common::string_view_t name ) noexcept;

// Records the layer of a geometry object (by root ID). Geometry loaded from
// a chunk keeps that chunk's layer; geometry a tool adds without calling
// this belongs to the first layer.
CYPHER_NODISCARD map_status_t MapDocument_SetGeometryLayer(
    map_document_t *pMap,
    common::u64 geometryId,
    common::string_view_t layer ) noexcept;

// Ties a geometry object to an entity ("Tie to Entity"), or back to the
// world with owner 0 ("Move to World"). Owned geometry is written inside
// its entity and lives in the entity's chunk.
CYPHER_NODISCARD map_status_t MapDocument_SetGeometryOwner(
    map_document_t *pMap,
    common::u64 geometryId,
    common::u64 ownerEntityId ) noexcept;

// PRIVATE map operation: retain a stripped authored record beside live geometry.
// The root must exist with the requested kind, layer, and owner, and must not
// already have a retained record. Clones all fields into the map-owned tree;
// consumes no identity and does not advance the geometry revision.
CYPHER_NODISCARD map_status_t MapDocument_InsertGeometryRecord(
    map_document_t *pMap, map_geometry_kind_t kind,
    const common::key_value_t *pRecord ) noexcept;

// Adds a point entity; it lands in the chunk its origin selects on the next
// save. Returns its ID in *pIdOut.
CYPHER_NODISCARD map_status_t MapDocument_AddEntity(
    map_document_t *pMap,
    common::string_view_t layer,
    common::string_view_t className,
    math::vec3d_t origin,
    common::u64 *pIdOut ) noexcept;

// Finds the record of an entity, shape, note, group, prefab instance, or
// geometry object (world or entity-owned) by ID. Geometry a tool created
// and that has not been saved yet has no record.
CYPHER_NODISCARD common::key_value_t *MapDocument_FindObject(
    map_document_t *pMap,
    common::u64 id,
    map_chunk_t **ppChunkOut ) noexcept;

// Removes an object. Removing an entity removes the geometry it owns.
CYPHER_NODISCARD map_status_t MapDocument_RemoveObject( map_document_t *pMap, common::u64 id ) noexcept;

// Sets an entity's origin; the entity moves chunk on the next save if its
// cell changes.
CYPHER_NODISCARD map_status_t MapDocument_SetEntityOrigin(
    map_document_t *pMap,
    common::u64 id,
    math::vec3d_t origin ) noexcept;

// The material reference for a path (added on first use), for tools that
// assign materials to faces and patches.
CYPHER_NODISCARD map_status_t MapDocument_MaterialRef(
    map_document_t *pMap,
    common::string_view_t path,
    common::u64 *pRefOut ) noexcept;

CYPHER_NODISCARD common::usize MapDocument_ChunkPath(
    const map_chunk_t &chunk,
    char ( &buffer )[MAP_CHUNK_PATH_CAPACITY] ) noexcept;

CYPHER_NODISCARD const char *MapDocument_StatusName( map_status_t status ) noexcept;
CYPHER_NODISCARD const char *MapDocument_ProblemName( map_problem_code_t code ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_DOCUMENT_H
