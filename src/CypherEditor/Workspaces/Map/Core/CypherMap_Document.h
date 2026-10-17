//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Document.h
//  Purpose: Declares the map document: the in-memory form of one `.cymap`
//           file (cypher.map V11, CYMAP.md), with loading, saving, identity,
//           placement, and object operations.
//  Details: The whole level is one file. Its layers hold the objects, each
//           layer divided into sections by kind (entities, brushes, ...), so
//           opening the file shows everything in the level and where it is.
//
//           In memory each layer's sections are kept as their own parsed
//           tree (a layer store), so everything the editor does not
//           understand - future object kinds, plugin data, members a newer
//           build wrote - is written back unchanged, and an object that
//           cannot be read stays exactly where it was. Geometry is lifted out
//           of the stores into one geometry document that editing tools work
//           on directly; its map-owned members (name, comment, visgroups,
//           terrain paint, mesh modifiers) stay in the store record and are
//           written back beside the regenerated geometry.
//
//           No file I/O happens here. Loading takes the file's text and
//           saving produces it; the editor, the map compiler, and tests
//           share this code.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - Reworked on 2026-09-27 for map-owned readable geometry (CYMAP.md 6)
//  - Reworked on 2026-10-12 for V11: one file, objects nested in their
//    layers; `.cymapchunk` files, cells, and the chunk sink are gone
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_DOCUMENT_H
#define CYPHER_EDITOR_MAP_DOCUMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Geometry.h"

#include "CypherGeometry_Document.h"

#include "CypherCommon/Tier1/CypherCommon_Span.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"
#include "CypherCommon/Tier1/CypherCommon_UniqueId.h"
#include "CypherCommon/Tier1/CypherCommon_Vector.h"
#include "CypherCommon/Tier2/CypherCommon_SettingsDocument.h"

namespace cypher::editor::map
{

// `@schema "cypher.map" 11` is the file's identity and version; together
// with `@cykv 1`, which must be its first bytes, it is the magic a tool
// checks before reading anything else (CYMAP.md 17).
inline constexpr const char *MAP_SCHEMA_ID = "cypher.map";
inline constexpr common::u32 MAP_SCHEMA_VERSION = 11u;
// V10 and older split a map into `.cymapchunk` files; they are not read.
inline constexpr common::u32 MAP_SCHEMA_VERSION_CHUNKED_LAST = 10u;
inline constexpr const char *MAP_FILE_EXTENSION = ".cymap";

// Safety bounds against hostile or corrupt input, not design targets. The
// whole level is one file, so the bounds cover the level: a box brush is
// about 127 values, so the value budget holds roughly 60,000 brushes and
// the text bound sits above what that many values print as.
inline constexpr common::usize MAP_TEXT_MAX = 128u * common::CY_MIB;
inline constexpr common::usize MAP_VALUES_MAX = 8u * 1024u * 1024u;
inline constexpr common::usize MAP_LAYERS_MAX = 256u;
inline constexpr common::usize MAP_VISGROUPS_MAX = 4096u;
inline constexpr common::usize MAP_SELECTION_SETS_MAX = 4096u;
inline constexpr common::usize MAP_CORDONS_MAX = 256u;
// Objects in one layer; under the reader's per-list bound (2^20) so every
// layer the writer accepts is one the reader accepts.
inline constexpr common::usize MAP_LAYER_OBJECTS_MAX = 1000000u;
inline constexpr common::usize MAP_FOLIAGE_INSTANCES_MAX = 1000000u;
inline constexpr common::usize MAP_LAYER_ID_CAPACITY = 65u;   // 64 bytes + NUL.
inline constexpr common::usize MAP_NAME_MAX_LENGTH = 128u;
inline constexpr common::f64 MAP_DEFAULT_UNITS_PER_METER = 39.37;
// Compact layout width: an ordinary brush face fits on one line (CYMAP.md 3).
// 200 columns of content plus the 8 the layer nesting adds, so every record
// that fit on one line in a V10 chunk still does.
inline constexpr common::u16 MAP_LINE_WIDTH = 208u;

enum class map_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    OUT_OF_MEMORY,
    ROOT_UNREADABLE,     // Not CYKV, another schema, or another version (VERSION_UNSUPPORTED says which).
    ROOT_INVALID,        // A required root member is missing or invalid; see the problem list.
    READ_ONLY,           // Duplicate IDs: the map opened, but cannot be saved.
    SAVE_BLOCKED,        // A section kept verbatim would be overwritten; fix it in the file first.
    LIMIT_EXCEEDED,      // The map would exceed a format limit (MAP_TEXT_MAX, MAP_VALUES_MAX, per layer).
    GEOMETRY_FAILED,     // The geometry library refused an operation.
    UNKNOWN_OBJECT,      // No object with that ID.
    UNKNOWN_LAYER,       // No layer with that ID.
    MODIFIER_TRANSFORM_UNSUPPORTED, // Live modifier parameters cannot preserve this root transform.
    PREFAB_TRANSFORM_UNSUPPORTED // The linked instance would require an unrepresentable shear.
};

enum class map_problem_code_t : common::u8 {
    ROOT_MEMBER_INVALID = 0u, // A required root member (named in `member`).
    ROOT_MEMBER_IGNORED,      // An optional root member was invalid; its default applies.
    TEXT_UNREADABLE,          // Not CYKV or not a cypher.map file; `member` holds the reason and line.
    VERSION_UNSUPPORTED,      // `member` names the declared version; V10 and older are chunked maps.
    SECTION_UNREADABLE,       // A layer section that is not a list; kept verbatim, its layer cannot gain that kind.
    OBJECT_UNREADABLE,        // An object that cannot be read; kept verbatim (`member` names the section and reason).
    ID_ASSIGNED,              // An object or part had no ID; a fresh one was assigned.
    DUPLICATE_ID,             // Two things share an ID; the map is read-only.
    ID_ABOVE_NEXT_ID,         // An ID not below next_id; next_id was raised.
    FOLIAGE_PROPERTIES_DIFFER // Records of one model in one layer disagree; the first is kept on save.
};

struct map_problem_t {
    map_problem_code_t code{ map_problem_code_t::ROOT_MEMBER_INVALID };
    char layer[MAP_LAYER_ID_CAPACITY]{};  // Layer the problem is in, empty for the root.
    char member[48]{};                    // Offending member or section, when one is named.
    common::u64 id{ 0u };                 // Object ID, when one is involved.
};

struct map_layer_t {
    char id[MAP_LAYER_ID_CAPACITY]{};
};

// One layer's objects: the layer's sections (entities, brushes, ...) as a
// tree whose root holds the section lists. There is at most one store per
// layer; a layer with no objects may have none.
struct map_layer_store_t {
    map_layer_store_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( map_layer_store_t );
    ~map_layer_store_t() noexcept = default;

    common::settings_document_t store{}; // Section lists; geometry records hold only map-owned members.
    char layer[MAP_LAYER_ID_CAPACITY]{};
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

    common::settings_document_t root{};                        // cypher.map V11 tree without the layers' sections.
    common::vector_t<map_layer_store_t *> layerStores{};       // Owned; heap-allocated (non-movable); one per layer at most.
    common::vector_t<map_layer_t> layers{};                    // Index for index with the root's `layers`.
    geometry::geometry_document_t geometry{};                  // All of the map's geometry.
    geometry::geometry_policy_t geometryPolicy{};
    common::vector_t<map_geometry_record_t> geometryRecords{}; // Sorted by ID.
    map_materials_t materials{};                               // Material paths used by faces and patches.
    common::vector_t<map_problem_t> problems{};
    common::unique_id_t mapId{};
    common::u64 nextId{ 1u };
    common::bool_t bReadOnly{ common::CY_FALSE };
    common::bool_t bIdsAssigned{ common::CY_FALSE };           // Load gave IDs; saving writes them.
    const common::allocator_t *pAllocator{ nullptr };
    // The layer new objects go to when the operation creating them names
    // none: the editor's current layer (TrenchBroom). Empty, or a layer the
    // map no longer has, means the first layer. Editor state; not saved.
    char creationLayer[MAP_LAYER_ID_CAPACITY]{};
};

struct map_create_desc_t {
    common::string_view_t name{};  // Display name.
    common::string_view_t game{};  // Game profile ID (stable identifier).
    common::string_view_t layer{}; // First layer ID; "default" when empty.
};

enum map_load_flags_t : common::u32 {
    MAP_LOAD_FLAG_NONE = 0u,
    // Give every repeated ID after its first occurrence (in layer order, then
    // document order) a fresh ID: the "Reassign Duplicate IDs" command.
    MAP_LOAD_FLAG_REASSIGN_DUPLICATES = 1u << 0u
};

// Creates an empty map with a fresh random map_id.
CYPHER_NODISCARD map_status_t MapDocument_Create(
    map_document_t *pMap,
    const common::allocator_t *pAllocator,
    const map_create_desc_t &desc ) noexcept;

// Loads a map from the text of its `.cymap` file. pMap must be
// default-constructed. ROOT_* failures leave it empty except for problems;
// damage inside a layer (an unreadable object or section) is recorded in
// problems and the rest loads. Layers are processed in file order, so IDs
// assigned on load are the same on every machine.
CYPHER_NODISCARD map_status_t MapDocument_Load(
    map_document_t *pMap,
    const common::allocator_t *pAllocator,
    common::string_view_t text,
    common::u32 flags = MAP_LOAD_FLAG_NONE ) noexcept;

void MapDocument_Shutdown( map_document_t *pMap ) noexcept;

// Produces the whole `.cymap` text: root members, then every layer with its
// objects in sections, sorted by ID, in the canonical member order and
// compact layout. pTextOut must be initialized; it is replaced only on
// success, and then the document reflects the saved layout. A caller that
// fails to write the text should restore bIdsAssigned.
CYPHER_NODISCARD map_status_t MapDocument_Save( map_document_t *pMap, common::text_buffer_t *pTextOut ) noexcept;

// The tree MapDocument_Save would write, without saving or changing what
// the document holds: the whole map as its file shows it, for views of the
// file's structure. Read-only maps build too. *ppOut receives a new tree the
// caller destroys with KeyValue_DestroyDocument.
CYPHER_NODISCARD map_status_t MapDocument_BuildTree( map_document_t *pMap, common::key_value_document_t **ppOut ) noexcept;

// The store of a layer, created empty when the layer has none yet; null for
// a layer the map does not declare, or when out of memory.
CYPHER_NODISCARD map_layer_store_t *MapDocument_LayerStore( map_document_t *pMap, common::string_view_t layer ) noexcept;

// Allocates a fresh object ID, keeping geometry allocation above it.
CYPHER_NODISCARD common::u64 MapDocument_AllocateId( map_document_t *pMap ) noexcept;

CYPHER_NODISCARD common::bool_t MapDocument_HasLayer( const map_document_t *pMap, common::string_view_t layer ) noexcept;

// Where new objects go: creationLayer when the map has it, else the first
// layer (empty for a map without layers).
CYPHER_NODISCARD common::string_view_t MapDocument_CreationLayer( const map_document_t *pMap ) noexcept;

// Adds a layer to the root; an existing ID is OK.
CYPHER_NODISCARD map_status_t MapDocument_AddLayer(
    map_document_t *pMap,
    common::string_view_t layer,
    common::string_view_t name ) noexcept;

// Records the layer of a geometry object (by root ID). Geometry loaded from
// a layer keeps that layer; geometry a tool adds without calling this
// belongs to the first layer.
CYPHER_NODISCARD map_status_t MapDocument_SetGeometryLayer(
    map_document_t *pMap,
    common::u64 geometryId,
    common::string_view_t layer ) noexcept;

// Ties a geometry object to an entity ("Tie to Entity"), or back to the
// world with owner 0 ("Move to World"). Owned geometry is written inside
// its entity and lives in the entity's layer.
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

// Adds a point entity to a layer. Returns its ID in *pIdOut.
CYPHER_NODISCARD map_status_t MapDocument_AddEntity(
    map_document_t *pMap,
    common::string_view_t layer,
    common::string_view_t className,
    math::vec3d_t origin,
    common::u64 *pIdOut ) noexcept;

// Groups (CYMAP.md 6.10): a record with a name and sorted member IDs, kept
// in the first layer until the save places it in its first member's layer.
// Members may be groups themselves (nesting).
CYPHER_NODISCARD map_status_t MapDocument_AddGroup( map_document_t *pMap, common::string_view_t name,
    common::span_t<const common::u64> members, common::u64 *pIdOut ) noexcept;
// The outermost group containing id, directly or through nested groups;
// 0 when it belongs to none.
CYPHER_NODISCARD common::u64 MapDocument_GroupOf( map_document_t *pMap, common::u64 id ) noexcept;
// A group's members with nested groups replaced by their members (the
// objects that select and move as one). pOut must be initialized.
CYPHER_NODISCARD map_status_t MapDocument_GroupMembers( map_document_t *pMap, common::u64 groupId,
    common::vector_t<common::u64> *pOut ) noexcept;

// Visgroups (CYMAP.md 4 root `visgroups`, 6.1 object `visgroups`): named
// sets an object can belong to several of, for showing and hiding areas.
// Adding a member that has no retained record yet (geometry a tool made)
// gives it one carrying only the membership.
CYPHER_NODISCARD map_status_t MapDocument_AddVisgroup( map_document_t *pMap, common::string_view_t name, common::u64 *pIdOut ) noexcept;
CYPHER_NODISCARD map_status_t MapDocument_AddToVisgroup( map_document_t *pMap, common::u64 visgroupId, common::u64 objectId ) noexcept;
// Objects whose records list the visgroup. pOut must be initialized.
CYPHER_NODISCARD map_status_t MapDocument_VisgroupMembers( map_document_t *pMap, common::u64 visgroupId,
    common::vector_t<common::u64> *pOut ) noexcept;

// Finds the record of an entity, shape, note, group, prefab instance, or
// geometry object (world or entity-owned) by ID. Geometry a tool created
// and that has not been saved yet has no record.
CYPHER_NODISCARD common::key_value_t *MapDocument_FindObject(
    map_document_t *pMap,
    common::u64 id,
    map_layer_store_t **ppStoreOut ) noexcept;

// Removes an object. Removing an entity removes the geometry it owns.
CYPHER_NODISCARD map_status_t MapDocument_RemoveObject( map_document_t *pMap, common::u64 id ) noexcept;

// Sets an entity's origin.
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

CYPHER_NODISCARD const char *MapDocument_StatusName( map_status_t status ) noexcept;
CYPHER_NODISCARD const char *MapDocument_ProblemName( map_problem_code_t code ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_DOCUMENT_H
