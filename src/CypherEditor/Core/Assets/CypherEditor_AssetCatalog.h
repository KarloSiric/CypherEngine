//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_AssetCatalog.h
//  Purpose: Declares the asset catalogue behind the editor's asset browser:
//           every asset file under the project's content roots, classified
//           by kind, searchable, and able to name what a recipe refers to.
//  Details: Qt-free (ADR 0008). The caller walks the content roots - the
//           GUI with Qt, a tool with the VFS - and adds each file with its
//           virtual path (relative to its root, forward slashes, as recipes
//           and maps write them). Finish sorts by path and drops paths a
//           higher-priority root already gave, the way mounts resolve.
//           Kinds follow FORMAT_CATALOG.md: Cypher recipes (.cymat, .cytex,
//           .cymesh, ...) and the source files they are built from (.png,
//           .fbx, .wav, ...), which the browser shows as sources.
//           Paths live back to back in one text block; entries hold
//           offsets, so a 50k-asset project is two allocations that grow.
//
//  History:
//  - Created by Karlo Siric on 2026-10-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_CORE_ASSET_CATALOG_H
#define CYPHER_EDITOR_CORE_ASSET_CATALOG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"
#include "CypherCommon/Tier1/CypherCommon_Vector.h"

namespace cypher::editor
{

// Browser tabs follow this order (Hammer 5's asset browser, plus the kinds
// Cypher adds).
enum class editor_asset_kind_t : common::u8 {
    MATERIAL = 0u, // .cymat
    TEXTURE,       // .cytex; sources .png .tga .jpg .jpeg .bmp .exr .hdr
    MODEL,         // .cymesh; sources .fbx .gltf .glb .obj
    PARTICLE,      // .cyparticle
    PREFAB,        // .cyprefab
    SOUND,         // .cysnd; sources .wav .ogg .flac .mp3
    SHADER,        // .cyshader; sources .vert .frag .glsl .hlsl .comp
    MAP,           // .cymap .cytilemap
    FONT,          // .cyfont; sources .ttf .otf
    ANIMATION,     // .cyanim .cyskel .cyanimgraph
    COUNT
};

inline constexpr common::u32 EDITOR_ASSET_KIND_ALL = ( 1u << static_cast<common::u32>( editor_asset_kind_t::COUNT ) ) - 1u;
inline constexpr common::usize EDITOR_ASSET_MAX_PATH = 1024u; // Bytes; longer paths are not assets the engine can name.

constexpr common::u32 EditorAssets_KindBit( editor_asset_kind_t kind ) noexcept
{
    return 1u << static_cast<common::u32>( kind );
}

enum class editor_asset_status_t : common::u8 {
    OK = 0u,
    SKIPPED,          // Not an asset (unknown extension, a hidden file).
    INVALID_ARGUMENT, // Empty, absolute, too long, or not a normalised path.
    OUT_OF_MEMORY,
    NOT_FOUND,        // A recipe without the reference asked for.
    INVALID_DATA      // A recipe that does not parse as its schema.
};

struct editor_asset_t {
    common::u32 iText{ 0u };     // Path start in the catalogue's text.
    common::u32 cchPath{ 0u };
    common::u32 iName{ 0u };     // File name start, relative to the path.
    common::u32 cchName{ 0u };   // File name without extension.
    common::u64 cbSize{ 0u };
    common::i64 modifiedMs{ 0 }; // Milliseconds since the Unix epoch; 0 unknown.
    common::u16 iRoot{ 0u };     // Which content root, in the order roots were walked.
    editor_asset_kind_t kind{ editor_asset_kind_t::COUNT };
    common::bool_t bSource{ common::CY_FALSE }; // A source file a recipe is built from, not a recipe.
};

struct editor_asset_catalog_t {
    editor_asset_catalog_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( editor_asset_catalog_t );

    common::vector_t<editor_asset_t> assets{}; // By path once finished.
    common::vector_t<char> text{};
    common::u32 counts[static_cast<common::usize>( editor_asset_kind_t::COUNT )]{};
    common::u32 nSources{ 0u };
    common::bool_t bFinished{ common::CY_FALSE };
    common::bool_t bInitialized{ common::CY_FALSE };
};

CYPHER_NODISCARD editor_asset_status_t EditorAssets_Init( editor_asset_catalog_t *pCatalog, const common::allocator_t *pAllocator ) noexcept;
void EditorAssets_Shutdown( editor_asset_catalog_t *pCatalog ) noexcept;
void EditorAssets_Clear( editor_asset_catalog_t *pCatalog ) noexcept;

// Adds one file; SKIPPED for files that are not assets, which callers count
// but do not report.
CYPHER_NODISCARD editor_asset_status_t EditorAssets_Add(
    editor_asset_catalog_t *pCatalog,
    common::u16 iRoot,
    common::string_view_t path,
    common::u64 cbSize,
    common::i64 modifiedMs ) noexcept;

// Sorts by path and keeps the first root's file where two roots give the
// same path (roots are walked highest priority first). Find needs it.
void EditorAssets_Finish( editor_asset_catalog_t *pCatalog ) noexcept;

CYPHER_NODISCARD common::usize EditorAssets_Count( const editor_asset_catalog_t *pCatalog ) noexcept;
CYPHER_NODISCARD const editor_asset_t *EditorAssets_At( const editor_asset_catalog_t *pCatalog, common::usize iAsset ) noexcept;
CYPHER_NODISCARD const editor_asset_t *EditorAssets_Find( const editor_asset_catalog_t *pCatalog, common::string_view_t path ) noexcept;
CYPHER_NODISCARD common::u32 EditorAssets_KindCount( const editor_asset_catalog_t *pCatalog, editor_asset_kind_t kind ) noexcept;

CYPHER_NODISCARD common::string_view_t EditorAssets_Path( const editor_asset_catalog_t *pCatalog, const editor_asset_t &asset ) noexcept;
CYPHER_NODISCARD common::string_view_t EditorAssets_Name( const editor_asset_catalog_t *pCatalog, const editor_asset_t &asset ) noexcept;
// "materials/blockout" for "materials/blockout/floor_tile.cymat"; empty at the root.
CYPHER_NODISCARD common::string_view_t EditorAssets_Folder( const editor_asset_catalog_t *pCatalog, const editor_asset_t &asset ) noexcept;
// "cymat"
CYPHER_NODISCARD common::string_view_t EditorAssets_Extension( const editor_asset_catalog_t *pCatalog, const editor_asset_t &asset ) noexcept;

// COUNT when the path is not an asset. Extensions compare case-insensitively.
CYPHER_NODISCARD editor_asset_kind_t EditorAssets_KindOf( common::string_view_t path, common::bool_t *pbSourceOut ) noexcept;
CYPHER_NODISCARD const char *EditorAssets_KindName( editor_asset_kind_t kind ) noexcept;  // "Materials"
CYPHER_NODISCARD const char *EditorAssets_KindLabel( editor_asset_kind_t kind ) noexcept; // "Material"

// Indices of the assets whose kind is in kindMask and whose path matches the
// query (the editor's fuzzy matcher; empty matches everything), best match
// first, then by path. Sources only when bSources.
CYPHER_NODISCARD editor_asset_status_t EditorAssets_Filter(
    const editor_asset_catalog_t *pCatalog,
    common::u32 kindMask,
    common::string_view_t query,
    common::bool_t bSources,
    common::vector_t<common::u32> *pIndicesOut ) noexcept;

// What a recipe names. Material: textures.base_color, else its first
// texture. Texture: source. Writes a virtual path to pPathOut.
CYPHER_NODISCARD editor_asset_status_t EditorAssets_MaterialTexture(
    common::string_view_t materialText,
    const common::allocator_t *pAllocator,
    common::text_buffer_t *pPathOut ) noexcept;
CYPHER_NODISCARD editor_asset_status_t EditorAssets_TextureSource(
    common::string_view_t textureText,
    const common::allocator_t *pAllocator,
    common::text_buffer_t *pPathOut ) noexcept;

// Every string in a recipe that is an asset path, in document order: what
// the asset depends on. The view is valid only during the call.
using editor_asset_reference_fn = void ( * )( void *pContext, common::string_view_t path, editor_asset_kind_t kind );
CYPHER_NODISCARD editor_asset_status_t EditorAssets_References(
    common::string_view_t recipeText,
    const common::allocator_t *pAllocator,
    editor_asset_reference_fn pfnReference,
    void *pContext ) noexcept;

// The same walk over a tree already parsed - a map's root or chunk
// documents - for "Used in Map".
void EditorAssets_VisitReferences( const common::key_value_t *pValue, editor_asset_reference_fn pfnReference, void *pContext ) noexcept;

CYPHER_NODISCARD const char *EditorAssets_StatusName( editor_asset_status_t status ) noexcept;

} // namespace cypher::editor

#endif // CYPHER_EDITOR_CORE_ASSET_CATALOG_H
