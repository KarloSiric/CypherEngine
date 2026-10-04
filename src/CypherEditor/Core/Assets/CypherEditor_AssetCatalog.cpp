//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_AssetCatalog.cpp
//  Purpose: Implements the asset catalogue.
//
//  History:
//  - Created by Karlo Siric on 2026-10-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_AssetCatalog.h"

#include "CypherEditor_FuzzyMatch.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"

#include <algorithm>
#include <iterator>

namespace cypher::editor
{

using namespace cypher::common;

namespace
{

struct extension_t {
    const char *pExtension;
    editor_asset_kind_t kind;
    bool bSource;
};

// FORMAT_CATALOG.md: recipes first, then the source files they cook from.
constexpr extension_t kExtensions[]{
    { "cymat", editor_asset_kind_t::MATERIAL, false },
    { "cytex", editor_asset_kind_t::TEXTURE, false },
    { "png", editor_asset_kind_t::TEXTURE, true },
    { "tga", editor_asset_kind_t::TEXTURE, true },
    { "jpg", editor_asset_kind_t::TEXTURE, true },
    { "jpeg", editor_asset_kind_t::TEXTURE, true },
    { "bmp", editor_asset_kind_t::TEXTURE, true },
    { "exr", editor_asset_kind_t::TEXTURE, true },
    { "hdr", editor_asset_kind_t::TEXTURE, true },
    { "cymesh", editor_asset_kind_t::MODEL, false },
    { "fbx", editor_asset_kind_t::MODEL, true },
    { "gltf", editor_asset_kind_t::MODEL, true },
    { "glb", editor_asset_kind_t::MODEL, true },
    { "obj", editor_asset_kind_t::MODEL, true },
    { "cyparticle", editor_asset_kind_t::PARTICLE, false },
    { "cyprefab", editor_asset_kind_t::PREFAB, false },
    { "cysnd", editor_asset_kind_t::SOUND, false },
    { "wav", editor_asset_kind_t::SOUND, true },
    { "ogg", editor_asset_kind_t::SOUND, true },
    { "flac", editor_asset_kind_t::SOUND, true },
    { "mp3", editor_asset_kind_t::SOUND, true },
    { "cyshader", editor_asset_kind_t::SHADER, false },
    { "vert", editor_asset_kind_t::SHADER, true },
    { "frag", editor_asset_kind_t::SHADER, true },
    { "glsl", editor_asset_kind_t::SHADER, true },
    { "hlsl", editor_asset_kind_t::SHADER, true },
    { "comp", editor_asset_kind_t::SHADER, true },
    { "cymap", editor_asset_kind_t::MAP, false },
    { "cytilemap", editor_asset_kind_t::MAP, false },
    { "cyfont", editor_asset_kind_t::FONT, false },
    { "ttf", editor_asset_kind_t::FONT, true },
    { "otf", editor_asset_kind_t::FONT, true },
    { "cyanim", editor_asset_kind_t::ANIMATION, false },
    { "cyskel", editor_asset_kind_t::ANIMATION, false },
    { "cyanimgraph", editor_asset_kind_t::ANIMATION, false },
};

constexpr const char *kKindNames[]{ "Materials", "Textures", "Models", "Particles", "Prefabs", "Sounds", "Shaders", "Maps", "Fonts", "Animations" };
constexpr const char *kKindLabels[]{ "Material", "Texture", "Model", "Particle", "Prefab", "Sound", "Shader", "Map", "Font", "Animation" };
static_assert( std::size( kKindNames ) == static_cast<usize>( editor_asset_kind_t::COUNT ) );
static_assert( std::size( kKindLabels ) == static_cast<usize>( editor_asset_kind_t::COUNT ) );

char Lower( char c ) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>( c - 'A' + 'a' ) : c;
}

bool EqualsNoCase( string_view_t a, const char *pB ) noexcept
{
    usize i = 0u;
    for ( ; i < a.cchLength; ++i ) {
        if ( pB[i] == '\0' || Lower( a.pData[i] ) != pB[i] ) { return false; }
    }
    return pB[i] == '\0';
}

// Recipes and maps name assets with these paths, so the catalogue keeps
// only paths they could write: relative, forward slashes, no empty, "." or
// ".." parts.
bool IsVirtualPath( string_view_t path ) noexcept
{
    if ( path.cchLength == 0u || path.cchLength > EDITOR_ASSET_MAX_PATH || path.pData[0] == '/' ) { return false; }
    usize iPart = 0u;
    for ( usize i = 0u; i <= path.cchLength; ++i ) {
        if ( i < path.cchLength && path.pData[i] == '\\' ) { return false; }
        if ( i < path.cchLength && path.pData[i] != '/' ) { continue; }
        const usize cchPart = i - iPart;
        if ( cchPart == 0u ) { return false; }
        if ( path.pData[iPart] == '.' && ( cchPart == 1u || ( cchPart == 2u && path.pData[iPart + 1u] == '.' ) ) ) { return false; }
        iPart = i + 1u;
    }
    return true;
}

usize NameStart( string_view_t path ) noexcept
{
    for ( usize i = path.cchLength; i-- > 0u; ) {
        if ( path.pData[i] == '/' ) { return i + 1u; }
    }
    return 0u;
}

// Offset of the extension's dot within the path; CY_INVALID_SIZE for none.
// A leading dot is a hidden file, not an extension.
usize DotOf( string_view_t path ) noexcept
{
    const usize iName = NameStart( path );
    for ( usize i = path.cchLength; i-- > iName + 1u; ) {
        if ( path.pData[i] == '.' ) { return i; }
    }
    return CY_INVALID_SIZE;
}

struct document_t {
    key_value_document_t *p{ nullptr };
    ~document_t()
    {
        if ( p != nullptr ) { KeyValue_DestroyDocument( p ); }
    }
};

// Parses a recipe and checks its schema; the root object on success.
editor_asset_status_t ParseRecipe( string_view_t text, const allocator_t *pAllocator, const char *pSchema, document_t *pDocument,
                                   const key_value_t **ppRootOut ) noexcept
{
    key_value_document_desc_t desc{};
    desc.pAllocator = pAllocator;
    pDocument->p = KeyValue_CreateDocument( desc );
    if ( pDocument->p == nullptr ) { return editor_asset_status_t::OUT_OF_MEMORY; }
    key_value_parse_options_t options{};
    options.cbMaxInput = 4u * CY_MIB; // Recipes are small; a huge file is not one.
    options.nMaxDepth = 32u;
    if ( KeyValue_ParseText( text, options, pDocument->p ).status != key_value_parse_status_t::OK ) { return editor_asset_status_t::INVALID_DATA; }
    if ( pSchema != nullptr && !StringView_Equals( KeyValue_DocumentHeader( pDocument->p ).schemaId, StringView_FromCString( pSchema ) ) ) {
        return editor_asset_status_t::INVALID_DATA;
    }
    const key_value_t *pRoot = KeyValue_Root( pDocument->p );
    if ( pRoot == nullptr || KeyValue_Type( pRoot ) != key_value_type_t::OBJECT ) { return editor_asset_status_t::INVALID_DATA; }
    *ppRootOut = pRoot;
    return editor_asset_status_t::OK;
}

editor_asset_status_t WritePath( const key_value_t *pValue, text_buffer_t *pPathOut ) noexcept
{
    string_view_t path{};
    if ( pValue == nullptr || !KeyValue_GetString( pValue, &path ) || !IsVirtualPath( path ) ) { return editor_asset_status_t::NOT_FOUND; }
    TextBuffer_Clear( pPathOut );
    return TextBuffer_Append( pPathOut, path ) ? editor_asset_status_t::OK : editor_asset_status_t::OUT_OF_MEMORY;
}

void VisitReferences( const key_value_t *pValue, editor_asset_reference_fn pfnReference, void *pContext ) noexcept
{
    string_view_t text{};
    if ( KeyValue_GetString( pValue, &text ) ) {
        bool_t bSource = CY_FALSE;
        const editor_asset_kind_t kind = EditorAssets_KindOf( text, &bSource );
        if ( kind != editor_asset_kind_t::COUNT && IsVirtualPath( text ) ) { pfnReference( pContext, text, kind ); }
        return;
    }
    for ( usize i = 0u; i < KeyValue_ChildCount( pValue ); ++i ) { VisitReferences( KeyValue_ChildAt( pValue, i ), pfnReference, pContext ); }
}

} // namespace

editor_asset_status_t EditorAssets_Init( editor_asset_catalog_t *pCatalog, const allocator_t *pAllocator ) noexcept
{
    CY_ASSERT( pCatalog != nullptr && !pCatalog->bInitialized );
    if ( pCatalog == nullptr || pAllocator == nullptr ) { return editor_asset_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Init( &pCatalog->assets, pAllocator ) || !Vector_Init( &pCatalog->text, pAllocator ) ) {
        Vector_Shutdown( &pCatalog->assets );
        return editor_asset_status_t::OUT_OF_MEMORY;
    }
    pCatalog->bInitialized = CY_TRUE;
    EditorAssets_Clear( pCatalog );
    return editor_asset_status_t::OK;
}

void EditorAssets_Shutdown( editor_asset_catalog_t *pCatalog ) noexcept
{
    if ( pCatalog == nullptr || !pCatalog->bInitialized ) { return; }
    Vector_Shutdown( &pCatalog->assets );
    Vector_Shutdown( &pCatalog->text );
    pCatalog->bInitialized = CY_FALSE;
}

void EditorAssets_Clear( editor_asset_catalog_t *pCatalog ) noexcept
{
    if ( pCatalog == nullptr || !pCatalog->bInitialized ) { return; }
    Vector_Clear( &pCatalog->assets );
    Vector_Clear( &pCatalog->text );
    for ( u32 &count : pCatalog->counts ) { count = 0u; }
    pCatalog->nSources = 0u;
    pCatalog->bFinished = CY_TRUE; // Nothing to sort.
}

editor_asset_status_t EditorAssets_Add( editor_asset_catalog_t *pCatalog, u16 iRoot, string_view_t path, u64 cbSize, i64 modifiedMs ) noexcept
{
    CY_ASSERT( pCatalog != nullptr && pCatalog->bInitialized );
    if ( pCatalog == nullptr || !pCatalog->bInitialized ) { return editor_asset_status_t::INVALID_ARGUMENT; }
    if ( !IsVirtualPath( path ) ) { return editor_asset_status_t::INVALID_ARGUMENT; }
    bool_t bSource = CY_FALSE;
    const editor_asset_kind_t kind = EditorAssets_KindOf( path, &bSource );
    if ( kind == editor_asset_kind_t::COUNT ) { return editor_asset_status_t::SKIPPED; }
    const usize iText = Vector_Count( &pCatalog->text );
    if ( iText + path.cchLength > 0xFFFFFFFFu ) { return editor_asset_status_t::OUT_OF_MEMORY; }
    if ( !Vector_Append( &pCatalog->text, span_t<const char>{ path.pData, path.cchLength } ) ) { return editor_asset_status_t::OUT_OF_MEMORY; }
    const usize iName = NameStart( path );
    const usize iDot = DotOf( path );
    editor_asset_t asset{};
    asset.iText = static_cast<u32>( iText );
    asset.cchPath = static_cast<u32>( path.cchLength );
    asset.iName = static_cast<u32>( iName );
    asset.cchName = static_cast<u32>( iDot - iName );
    asset.cbSize = cbSize;
    asset.modifiedMs = modifiedMs;
    asset.iRoot = iRoot;
    asset.kind = kind;
    asset.bSource = bSource;
    if ( !Vector_PushBack( &pCatalog->assets, asset ) ) {
        ( void )Vector_Resize( &pCatalog->text, iText ); // Shrinking never allocates.
        return editor_asset_status_t::OUT_OF_MEMORY;
    }
    ++pCatalog->counts[static_cast<usize>( kind )];
    pCatalog->nSources += bSource ? 1u : 0u;
    pCatalog->bFinished = CY_FALSE;
    return editor_asset_status_t::OK;
}

void EditorAssets_Finish( editor_asset_catalog_t *pCatalog ) noexcept
{
    if ( pCatalog == nullptr || !pCatalog->bInitialized || pCatalog->bFinished ) { return; }
    editor_asset_t *pBegin = Vector_Data( &pCatalog->assets );
    editor_asset_t *pEnd = pBegin + Vector_Count( &pCatalog->assets );
    const auto path = [pCatalog]( const editor_asset_t &asset ) { return EditorAssets_Path( pCatalog, asset ); };
    // Stable: among equal paths the earlier root stays first.
    std::stable_sort( pBegin, pEnd, [&path]( const editor_asset_t &a, const editor_asset_t &b ) {
        const string_view_t pa = path( a );
        const string_view_t pb = path( b );
        return StringView_Compare( pa, pb ) < 0;
    } );
    usize nKept = 0u;
    for ( editor_asset_t *pAsset = pBegin; pAsset != pEnd; ++pAsset ) {
        if ( nKept != 0u && StringView_Equals( path( pBegin[nKept - 1u] ), path( *pAsset ) ) ) {
            // A lower-priority root's copy is hidden, as mounting would.
            --pCatalog->counts[static_cast<usize>( pAsset->kind )];
            pCatalog->nSources -= pAsset->bSource ? 1u : 0u;
            continue;
        }
        pBegin[nKept++] = *pAsset;
    }
    ( void )Vector_Resize( &pCatalog->assets, nKept );
    pCatalog->bFinished = CY_TRUE;
}

usize EditorAssets_Count( const editor_asset_catalog_t *pCatalog ) noexcept
{
    return pCatalog != nullptr && pCatalog->bInitialized ? Vector_Count( &pCatalog->assets ) : 0u;
}

const editor_asset_t *EditorAssets_At( const editor_asset_catalog_t *pCatalog, usize iAsset ) noexcept
{
    return iAsset < EditorAssets_Count( pCatalog ) ? Vector_Data( &pCatalog->assets ) + iAsset : nullptr;
}

const editor_asset_t *EditorAssets_Find( const editor_asset_catalog_t *pCatalog, string_view_t path ) noexcept
{
    CY_ASSERT_MSG( pCatalog == nullptr || pCatalog->bFinished, "Finish the catalogue before searching it by path" );
    if ( pCatalog == nullptr || !pCatalog->bInitialized || !pCatalog->bFinished ) { return nullptr; }
    const editor_asset_t *pBegin = Vector_Data( &pCatalog->assets );
    const editor_asset_t *pEnd = pBegin + Vector_Count( &pCatalog->assets );
    const editor_asset_t *pFound = std::lower_bound( pBegin, pEnd, path, [pCatalog]( const editor_asset_t &asset, string_view_t wanted ) {
        return StringView_Compare( EditorAssets_Path( pCatalog, asset ), wanted ) < 0;
    } );
    return pFound != pEnd && StringView_Equals( EditorAssets_Path( pCatalog, *pFound ), path ) ? pFound : nullptr;
}

u32 EditorAssets_KindCount( const editor_asset_catalog_t *pCatalog, editor_asset_kind_t kind ) noexcept
{
    return pCatalog != nullptr && kind < editor_asset_kind_t::COUNT ? pCatalog->counts[static_cast<usize>( kind )] : 0u;
}

string_view_t EditorAssets_Path( const editor_asset_catalog_t *pCatalog, const editor_asset_t &asset ) noexcept
{
    return string_view_t{ Vector_Data( &pCatalog->text ) + asset.iText, asset.cchPath };
}

string_view_t EditorAssets_Name( const editor_asset_catalog_t *pCatalog, const editor_asset_t &asset ) noexcept
{
    return string_view_t{ Vector_Data( &pCatalog->text ) + asset.iText + asset.iName, asset.cchName };
}

string_view_t EditorAssets_Folder( const editor_asset_catalog_t *pCatalog, const editor_asset_t &asset ) noexcept
{
    return string_view_t{ Vector_Data( &pCatalog->text ) + asset.iText, asset.iName != 0u ? asset.iName - 1u : 0u };
}

string_view_t EditorAssets_Extension( const editor_asset_catalog_t *pCatalog, const editor_asset_t &asset ) noexcept
{
    const u32 iExtension = asset.iName + asset.cchName + 1u; // Past the dot.
    return string_view_t{ Vector_Data( &pCatalog->text ) + asset.iText + iExtension, asset.cchPath - iExtension };
}

editor_asset_kind_t EditorAssets_KindOf( string_view_t path, bool_t *pbSourceOut ) noexcept
{
    if ( pbSourceOut != nullptr ) { *pbSourceOut = CY_FALSE; }
    const usize iDot = DotOf( path );
    if ( iDot == CY_INVALID_SIZE ) { return editor_asset_kind_t::COUNT; }
    const string_view_t extension{ path.pData + iDot + 1u, path.cchLength - iDot - 1u };
    for ( const extension_t &entry : kExtensions ) {
        if ( !EqualsNoCase( extension, entry.pExtension ) ) { continue; }
        if ( pbSourceOut != nullptr ) { *pbSourceOut = entry.bSource ? CY_TRUE : CY_FALSE; }
        return entry.kind;
    }
    return editor_asset_kind_t::COUNT;
}

const char *EditorAssets_KindName( editor_asset_kind_t kind ) noexcept
{
    return kind < editor_asset_kind_t::COUNT ? kKindNames[static_cast<usize>( kind )] : "Assets";
}

const char *EditorAssets_KindLabel( editor_asset_kind_t kind ) noexcept
{
    return kind < editor_asset_kind_t::COUNT ? kKindLabels[static_cast<usize>( kind )] : "Asset";
}

editor_asset_status_t EditorAssets_Filter( const editor_asset_catalog_t *pCatalog, u32 kindMask, string_view_t query, bool_t bSources,
                                           vector_t<u32> *pIndicesOut ) noexcept
{
    CY_ASSERT( pCatalog != nullptr && pIndicesOut != nullptr );
    if ( pCatalog == nullptr || pIndicesOut == nullptr || !pCatalog->bInitialized ) { return editor_asset_status_t::INVALID_ARGUMENT; }
    Vector_Clear( pIndicesOut );
    struct scored_t {
        u32 iAsset;
        i32 score;
    };
    vector_t<scored_t> scored{};
    if ( !Vector_Init( &scored, pIndicesOut->pAllocator ) ) { return editor_asset_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0u; i < Vector_Count( &pCatalog->assets ); ++i ) {
        const editor_asset_t &asset = Vector_Data( &pCatalog->assets )[i];
        if ( ( kindMask & EditorAssets_KindBit( asset.kind ) ) == 0u || ( asset.bSource && !bSources ) ) { continue; }
        const i32 score = query.cchLength == 0u ? 0 : EditorFuzzy_Score( EditorAssets_Path( pCatalog, asset ), query );
        if ( score == EDITOR_FUZZY_NO_MATCH ) { continue; }
        if ( !Vector_PushBack( &scored, scored_t{ static_cast<u32>( i ), score } ) ) { return editor_asset_status_t::OUT_OF_MEMORY; }
    }
    // The catalogue is in path order already; stable keeps it among ties.
    std::stable_sort( Vector_Data( &scored ), Vector_Data( &scored ) + Vector_Count( &scored ),
                      []( const scored_t &a, const scored_t &b ) { return a.score > b.score; } );
    if ( !Vector_Reserve( pIndicesOut, Vector_Count( &scored ) ) ) { return editor_asset_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0u; i < Vector_Count( &scored ); ++i ) { ( void )Vector_PushBack( pIndicesOut, Vector_Data( &scored )[i].iAsset ); }
    return editor_asset_status_t::OK;
}

editor_asset_status_t EditorAssets_MaterialTexture( string_view_t materialText, const allocator_t *pAllocator, text_buffer_t *pPathOut ) noexcept
{
    if ( pAllocator == nullptr || pPathOut == nullptr ) { return editor_asset_status_t::INVALID_ARGUMENT; }
    document_t document{};
    const key_value_t *pRoot = nullptr;
    const editor_asset_status_t status = ParseRecipe( materialText, pAllocator, "cypher.material", &document, &pRoot );
    if ( status != editor_asset_status_t::OK ) { return status; }
    const key_value_t *pTextures = KeyValue_Find( pRoot, StringView_FromCString( "textures" ) );
    if ( pTextures == nullptr || KeyValue_Type( pTextures ) != key_value_type_t::OBJECT ) { return editor_asset_status_t::NOT_FOUND; }
    // The base colour is what a thumbnail should show; a material without
    // one (a normal-map-only decal) still shows its first texture.
    if ( WritePath( KeyValue_Find( pTextures, StringView_FromCString( "base_color" ) ), pPathOut ) == editor_asset_status_t::OK ) {
        return editor_asset_status_t::OK;
    }
    for ( usize i = 0u; i < KeyValue_ChildCount( pTextures ); ++i ) {
        if ( WritePath( KeyValue_ChildAt( pTextures, i ), pPathOut ) == editor_asset_status_t::OK ) { return editor_asset_status_t::OK; }
    }
    return editor_asset_status_t::NOT_FOUND;
}

editor_asset_status_t EditorAssets_TextureSource( string_view_t textureText, const allocator_t *pAllocator, text_buffer_t *pPathOut ) noexcept
{
    if ( pAllocator == nullptr || pPathOut == nullptr ) { return editor_asset_status_t::INVALID_ARGUMENT; }
    document_t document{};
    const key_value_t *pRoot = nullptr;
    const editor_asset_status_t status = ParseRecipe( textureText, pAllocator, "cypher.texture", &document, &pRoot );
    if ( status != editor_asset_status_t::OK ) { return status; }
    return WritePath( KeyValue_Find( pRoot, StringView_FromCString( "source" ) ), pPathOut );
}

editor_asset_status_t EditorAssets_References( string_view_t recipeText, const allocator_t *pAllocator, editor_asset_reference_fn pfnReference,
                                               void *pContext ) noexcept
{
    if ( pAllocator == nullptr || pfnReference == nullptr ) { return editor_asset_status_t::INVALID_ARGUMENT; }
    document_t document{};
    const key_value_t *pRoot = nullptr;
    const editor_asset_status_t status = ParseRecipe( recipeText, pAllocator, nullptr, &document, &pRoot );
    if ( status != editor_asset_status_t::OK ) { return status; }
    VisitReferences( pRoot, pfnReference, pContext );
    return editor_asset_status_t::OK;
}

void EditorAssets_VisitReferences( const key_value_t *pValue, editor_asset_reference_fn pfnReference, void *pContext ) noexcept
{
    if ( pValue != nullptr && pfnReference != nullptr ) { VisitReferences( pValue, pfnReference, pContext ); }
}

const char *EditorAssets_StatusName( editor_asset_status_t status ) noexcept
{
    switch ( status ) {
        case editor_asset_status_t::OK: return "OK";
        case editor_asset_status_t::SKIPPED: return "SKIPPED";
        case editor_asset_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case editor_asset_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case editor_asset_status_t::NOT_FOUND: return "NOT_FOUND";
        case editor_asset_status_t::INVALID_DATA: return "INVALID_DATA";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor
