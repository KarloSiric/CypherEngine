//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_AssetCatalog_Tests.cpp
//  Purpose: Contract tests for the asset catalogue: classification by
//           extension, path rules, root priority, lookup, filtering, and
//           reading what material and texture recipes name.
//
//  History:
//  - Created by Karlo Siric on 2026-10-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_AssetCatalog.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace cypher::common;
using namespace cypher::editor;

namespace
{

std::string Str( string_view_t view )
{
    return std::string( view.pData != nullptr ? view.pData : "", view.cchLength );
}

struct catalog_t {
    editor_asset_catalog_t catalog{};
    catalog_t() { REQUIRE( EditorAssets_Init( &catalog, Allocator_GetSystem() ) == editor_asset_status_t::OK ); }
    ~catalog_t() { EditorAssets_Shutdown( &catalog ); }
    editor_asset_status_t Add( const char *pPath, u16 iRoot = 0u ) { return EditorAssets_Add( &catalog, iRoot, StringView_FromCString( pPath ), 100u, 0 ); }
};

} // namespace

TEST_CASE( "Assets are classified by extension, recipes apart from sources", "[editor][assets]" )
{
    bool_t bSource = CY_TRUE;
    CHECK( EditorAssets_KindOf( StringView_FromCString( "materials/dev/grid.cymat" ), &bSource ) == editor_asset_kind_t::MATERIAL );
    CHECK_FALSE( bSource );
    CHECK( EditorAssets_KindOf( StringView_FromCString( "textures/dev/grid.PNG" ), &bSource ) == editor_asset_kind_t::TEXTURE );
    CHECK( bSource );
    CHECK( EditorAssets_KindOf( StringView_FromCString( "models/crate.fbx" ), nullptr ) == editor_asset_kind_t::MODEL );
    CHECK( EditorAssets_KindOf( StringView_FromCString( "sound/door.wav" ), nullptr ) == editor_asset_kind_t::SOUND );
    CHECK( EditorAssets_KindOf( StringView_FromCString( "prefabs/door.cyprefab" ), nullptr ) == editor_asset_kind_t::PREFAB );
    CHECK( EditorAssets_KindOf( StringView_FromCString( "maps/a.cytilemap" ), nullptr ) == editor_asset_kind_t::MAP );
    CHECK( EditorAssets_KindOf( StringView_FromCString( "notes/readme.md" ), nullptr ) == editor_asset_kind_t::COUNT );
    CHECK( EditorAssets_KindOf( StringView_FromCString( "textures/.png" ), nullptr ) == editor_asset_kind_t::COUNT ); // A hidden file.
    CHECK( std::string( EditorAssets_KindName( editor_asset_kind_t::MATERIAL ) ) == "Materials" );
    CHECK( std::string( EditorAssets_KindLabel( editor_asset_kind_t::PARTICLE ) ) == "Particle" );
}

TEST_CASE( "The catalogue keeps virtual paths and lets the first root win", "[editor][assets]" )
{
    catalog_t c;
    CHECK( c.Add( "materials/blockout/floor_tile.cymat" ) == editor_asset_status_t::OK );
    CHECK( c.Add( "textures/blockout/floor_tile.png" ) == editor_asset_status_t::OK );
    CHECK( c.Add( "materials/dev/grid.cymat" ) == editor_asset_status_t::OK );
    CHECK( c.Add( "materials/dev/grid.cymat", 1u ) == editor_asset_status_t::OK ); // A lower-priority root.
    CHECK( c.Add( "docs/readme.md" ) == editor_asset_status_t::SKIPPED );
    CHECK( c.Add( "/abs/path.cymat" ) == editor_asset_status_t::INVALID_ARGUMENT );
    CHECK( c.Add( "materials/../x.cymat" ) == editor_asset_status_t::INVALID_ARGUMENT );
    CHECK( c.Add( "materials\\x.cymat" ) == editor_asset_status_t::INVALID_ARGUMENT );
    CHECK( c.Add( "materials//x.cymat" ) == editor_asset_status_t::INVALID_ARGUMENT );
    EditorAssets_Finish( &c.catalog );

    REQUIRE( EditorAssets_Count( &c.catalog ) == 3u );
    CHECK( EditorAssets_KindCount( &c.catalog, editor_asset_kind_t::MATERIAL ) == 2u );
    CHECK( c.catalog.nSources == 1u );
    // Sorted by path.
    CHECK( Str( EditorAssets_Path( &c.catalog, *EditorAssets_At( &c.catalog, 0u ) ) ) == "materials/blockout/floor_tile.cymat" );
    const editor_asset_t *pGrid = EditorAssets_Find( &c.catalog, StringView_FromCString( "materials/dev/grid.cymat" ) );
    REQUIRE( pGrid != nullptr );
    CHECK( pGrid->iRoot == 0u );
    CHECK( Str( EditorAssets_Name( &c.catalog, *pGrid ) ) == "grid" );
    CHECK( Str( EditorAssets_Folder( &c.catalog, *pGrid ) ) == "materials/dev" );
    CHECK( Str( EditorAssets_Extension( &c.catalog, *pGrid ) ) == "cymat" );
    CHECK( EditorAssets_Find( &c.catalog, StringView_FromCString( "materials/dev/nope.cymat" ) ) == nullptr );

    // An asset at the root has no folder.
    CHECK( c.Add( "sky.cymat" ) == editor_asset_status_t::OK );
    EditorAssets_Finish( &c.catalog );
    const editor_asset_t *pSky = EditorAssets_Find( &c.catalog, StringView_FromCString( "sky.cymat" ) );
    REQUIRE( pSky != nullptr );
    CHECK( EditorAssets_Folder( &c.catalog, *pSky ).cchLength == 0u );

    EditorAssets_Clear( &c.catalog );
    CHECK( EditorAssets_Count( &c.catalog ) == 0u );
    CHECK( EditorAssets_KindCount( &c.catalog, editor_asset_kind_t::MATERIAL ) == 0u );
}

TEST_CASE( "Filtering takes kinds, sources, and a fuzzy query", "[editor][assets]" )
{
    catalog_t c;
    for ( const char *pPath : { "materials/blockout/floor_tile.cymat", "materials/blockout/metal_panel.cymat", "materials/dev/grid.cymat",
                                "textures/blockout/floor_tile.cytex", "textures/blockout/floor_tile.png", "sound/door.wav" } ) {
        REQUIRE( c.Add( pPath ) == editor_asset_status_t::OK );
    }
    EditorAssets_Finish( &c.catalog );
    vector_t<u32> indices{};
    REQUIRE( Vector_Init( &indices, Allocator_GetSystem() ) );
    const auto paths = [&]() {
        std::vector<std::string> out;
        for ( usize i = 0u; i < Vector_Count( &indices ); ++i ) {
            out.push_back( Str( EditorAssets_Path( &c.catalog, *EditorAssets_At( &c.catalog, Vector_Data( &indices )[i] ) ) ) );
        }
        return out;
    };

    REQUIRE( EditorAssets_Filter( &c.catalog, EditorAssets_KindBit( editor_asset_kind_t::MATERIAL ), {}, CY_FALSE, &indices ) == editor_asset_status_t::OK );
    CHECK( paths() == std::vector<std::string>{ "materials/blockout/floor_tile.cymat", "materials/blockout/metal_panel.cymat", "materials/dev/grid.cymat" } );

    REQUIRE( EditorAssets_Filter( &c.catalog, EDITOR_ASSET_KIND_ALL, StringView_FromCString( "floor" ), CY_FALSE, &indices ) == editor_asset_status_t::OK );
    CHECK( paths() == std::vector<std::string>{ "materials/blockout/floor_tile.cymat", "textures/blockout/floor_tile.cytex" } );

    // Sources join only when asked.
    REQUIRE( EditorAssets_Filter( &c.catalog, EditorAssets_KindBit( editor_asset_kind_t::TEXTURE ), {}, CY_TRUE, &indices ) == editor_asset_status_t::OK );
    CHECK( Vector_Count( &indices ) == 2u );
    Vector_Shutdown( &indices );
}

TEST_CASE( "Recipes name their textures, sources, and dependencies", "[editor][assets]" )
{
    const char *pMaterial = "@cykv 1\n@schema \"cypher.material\" 1\n{\n    shader = \"shaders/tile_surface.cyshader\"\n"
                            "    textures = { normal = \"textures/a_n.cytex\" base_color = \"textures/blockout/floor_tile.cytex\" }\n"
                            "    parameters = { tint = [1.0, 1.0, 1.0, 1.0] }\n}\n";
    text_buffer_t path{};
    REQUIRE( TextBuffer_Init( &path, Allocator_GetSystem() ) );
    REQUIRE( EditorAssets_MaterialTexture( StringView_FromCString( pMaterial ), Allocator_GetSystem(), &path ) == editor_asset_status_t::OK );
    CHECK( Str( TextBuffer_View( &path ) ) == "textures/blockout/floor_tile.cytex" );

    // No base colour: the first texture stands in.
    const char *pDecal = "@cykv 1\n@schema \"cypher.material\" 1\n{ shader = \"s.cyshader\" textures = { normal = \"textures/a_n.cytex\" } }\n";
    REQUIRE( EditorAssets_MaterialTexture( StringView_FromCString( pDecal ), Allocator_GetSystem(), &path ) == editor_asset_status_t::OK );
    CHECK( Str( TextBuffer_View( &path ) ) == "textures/a_n.cytex" );

    const char *pTexture = "@cykv 1\n@schema \"cypher.texture\" 1\n{ source = \"textures/blockout/floor_tile.png\" usage = \"color\" }\n";
    REQUIRE( EditorAssets_TextureSource( StringView_FromCString( pTexture ), Allocator_GetSystem(), &path ) == editor_asset_status_t::OK );
    CHECK( Str( TextBuffer_View( &path ) ) == "textures/blockout/floor_tile.png" );

    // Wrong schema, broken text, and a missing member say so.
    CHECK( EditorAssets_TextureSource( StringView_FromCString( pMaterial ), Allocator_GetSystem(), &path ) == editor_asset_status_t::INVALID_DATA );
    CHECK( EditorAssets_TextureSource( StringView_FromCString( "{ nope" ), Allocator_GetSystem(), &path ) == editor_asset_status_t::INVALID_DATA );
    CHECK( EditorAssets_TextureSource( StringView_FromCString( "@cykv 1\n@schema \"cypher.texture\" 1\n{ usage = \"color\" }\n" ), Allocator_GetSystem(),
                                       &path ) == editor_asset_status_t::NOT_FOUND );

    std::vector<std::string> references;
    REQUIRE( EditorAssets_References(
                 StringView_FromCString( pMaterial ), Allocator_GetSystem(),
                 []( void *pContext, string_view_t reference, editor_asset_kind_t ) {
                     static_cast<std::vector<std::string> *>( pContext )->push_back( Str( reference ) );
                 },
                 &references ) == editor_asset_status_t::OK );
    CHECK( references == std::vector<std::string>{ "shaders/tile_surface.cyshader", "textures/a_n.cytex", "textures/blockout/floor_tile.cytex" } );
    TextBuffer_Shutdown( &path );
}
