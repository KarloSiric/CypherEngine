//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Theme_Tests.cpp
//  Purpose: Contract tests for editor theme tokens, chains, and editing.
//  Details: Themes resolve token by token through their bases, fonts field
//           by field, store only differences, and flag what the theme
//           editor should show as unknown or invalid.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_Theme.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace cypher::common;
using namespace cypher::editor;

namespace
{

theme_token_t ColorToken( const char *pId, u32 rgba )
{
    theme_token_t token{};
    token.pId = pId;
    token.kind = theme_token_kind_t::COLOR;
    token.rgbaDefault = rgba;
    return token;
}

const theme_token_t kTokens[]{
    ColorToken( "ui.background", 0x303032FFu ),
    ColorToken( "viewport.grid.minor", 0x23313EFFu ),
    ColorToken( "viewport.axis.x", 0xDE524CFFu ),
    { "console", theme_token_kind_t::FONT, 0u, "Menlo", 10.0, 400u, 0.0, 0.0, 0.0, "Console", "Fonts" },
    { "ui.icon_size", theme_token_kind_t::METRIC, 0u, "", 0.0, 0u, 24.0, 12.0, 64.0, "Icon size", "Interface" },
};

struct theme_doc_t {
    settings_document_t store{};
    explicit theme_doc_t( const char *pText )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorTheme_Identity() ) == settings_document_status_t::OK );
        if ( pText != nullptr ) {
            const settings_document_load_result_t loaded = SettingsDocument_Load( &store, StringView_FromCString( pText ) );
            INFO( SettingsDocument_StatusName( loaded.status ) );
            REQUIRE( loaded.status == settings_document_status_t::OK );
        }
    }
    const key_value_t *Root() const { return SettingsDocument_Root( &store ); }
};

bool_t Equals( string_view_t view, const char *pText )
{
    return StringView_Equals( view, StringView_FromCString( pText ) );
}

} // namespace

TEST_CASE( "Theme tokens register all-or-nothing and resolve by ID",
           "[CypherEditor][Core][Theme]" )
{
    theme_registry_t registry{};
    REQUIRE( EditorThemeRegistry_Init( &registry, Allocator_GetSystem() ) == theme_status_t::OK );
    REQUIRE( EditorThemeRegistry_Register( &registry, kTokens, 5u ) == theme_status_t::OK );
    REQUIRE( EditorThemeRegistry_Find( &registry, StringView_FromCString( "viewport.axis.x" ) ) == &kTokens[2] );
    REQUIRE( EditorThemeRegistry_Find( &registry, StringView_FromCString( "viewport.axis" ) ) == nullptr );

    const theme_token_t duplicate[]{ ColorToken( "ui.accent", 0xFFu ), ColorToken( "ui.background", 0xFFu ) };
    REQUIRE( EditorThemeRegistry_Register( &registry, duplicate, 2u ) == theme_status_t::DUPLICATE_TOKEN );
    REQUIRE( EditorThemeRegistry_Find( &registry, StringView_FromCString( "ui.accent" ) ) == nullptr );

    for ( const char *pBad : { "UI.background", "ui..background", ".ui", "ui.", "9ui" } ) {
        const theme_token_t bad = ColorToken( pBad, 0xFFu );
        CAPTURE( pBad );
        REQUIRE( EditorThemeRegistry_Register( &registry, &bad, 1u ) == theme_status_t::INVALID_ARGUMENT );
    }
    EditorThemeRegistry_Shutdown( &registry );
}

TEST_CASE( "Themes resolve through their base chain down to token defaults",
           "[CypherEditor][Core][Theme]" )
{
    theme_doc_t charcoal( R"cykv(@cykv 1
@schema "cypher.theme" 1
{
    id = "charcoal"
    name = "Charcoal"
    colors = { "ui.background" = "#303032" "viewport.grid.minor" = "#23313e" }
    fonts = { console = { family = "JetBrains Mono" size = 10.0 } }
}
)cykv" );
    theme_doc_t midnight( R"cykv(@cykv 1
@schema "cypher.theme" 1
{
    id = "midnight"
    name = "Midnight"
    base = "charcoal"
    colors = { "ui.background" = "#101418" "viewport.grid.minor" = "blue" }
    fonts = { console = { size = 12 } }
    metrics = { "ui.icon_size" = 20 }
}
)cykv" );

    theme_library_t library{};
    REQUIRE( EditorThemeLibrary_Init( &library, Allocator_GetSystem() ) == theme_status_t::OK );
    REQUIRE( EditorThemeLibrary_Add( &library, charcoal.Root() ) == theme_status_t::OK );
    REQUIRE( EditorThemeLibrary_Add( &library, midnight.Root() ) == theme_status_t::OK );

    const key_value_t *chain[EDITOR_THEME_MAX_BASE_DEPTH]{};
    bool_t bComplete = CY_FALSE;
    const usize nChain = EditorTheme_BuildChain( &library, midnight.Root(), chain, EDITOR_THEME_MAX_BASE_DEPTH, &bComplete );
    REQUIRE( nChain == 2u );
    REQUIRE( bComplete == CY_TRUE );
    REQUIRE( chain[1] == charcoal.Root() );

    theme_resolution_t resolution{};
    REQUIRE( EditorTheme_ResolveColor( chain, nChain, kTokens[0], &resolution ) == 0x101418FFu );
    REQUIRE( resolution.iSource == 0u );
    // Midnight's grid colour is invalid, so charcoal's applies.
    REQUIRE( EditorTheme_ResolveColor( chain, nChain, kTokens[1], &resolution ) == 0x23313EFFu );
    REQUIRE( resolution.iSource == 1u );
    REQUIRE( resolution.nInvalidSkipped == 1u );
    REQUIRE( EditorTheme_ResolveColor( chain, nChain, kTokens[2], &resolution ) == 0xDE524CFFu );
    REQUIRE( resolution.iSource == CY_INVALID_SIZE );

    // Font fields resolve independently: size from midnight, family from charcoal.
    const theme_font_t font = EditorTheme_ResolveFont( chain, nChain, kTokens[3], &resolution );
    REQUIRE( Equals( font.family, "JetBrains Mono" ) );
    REQUIRE( font.flSize == 12.0 );
    REQUIRE( font.nWeight == 400u );
    REQUIRE( EditorTheme_ResolveMetric( chain, nChain, kTokens[4], nullptr ) == 20.0 );
    EditorThemeLibrary_Shutdown( &library );
}

TEST_CASE( "Theme chains stop at missing bases and cycles",
           "[CypherEditor][Core][Theme]" )
{
    theme_doc_t a( "@cykv 1\n@schema \"cypher.theme\" 1\n{ id = \"a\" base = \"b\" }" );
    theme_doc_t b( "@cykv 1\n@schema \"cypher.theme\" 1\n{ id = \"b\" base = \"a\" }" );
    theme_doc_t orphan( "@cykv 1\n@schema \"cypher.theme\" 1\n{ id = \"orphan\" base = \"gone\" }" );
    theme_library_t library{};
    REQUIRE( EditorThemeLibrary_Init( &library, Allocator_GetSystem() ) == theme_status_t::OK );
    REQUIRE( EditorThemeLibrary_Add( &library, a.Root() ) == theme_status_t::OK );
    REQUIRE( EditorThemeLibrary_Add( &library, b.Root() ) == theme_status_t::OK );

    const key_value_t *chain[EDITOR_THEME_MAX_BASE_DEPTH]{};
    bool_t bComplete = CY_TRUE;
    REQUIRE( EditorTheme_BuildChain( &library, a.Root(), chain, EDITOR_THEME_MAX_BASE_DEPTH, &bComplete ) == 2u );
    REQUIRE( bComplete == CY_FALSE );
    REQUIRE( EditorTheme_BuildChain( &library, orphan.Root(), chain, EDITOR_THEME_MAX_BASE_DEPTH, &bComplete ) == 1u );
    REQUIRE( bComplete == CY_FALSE );
    EditorThemeLibrary_Shutdown( &library );
}

TEST_CASE( "Theme edits store only differences from the inherited value",
           "[CypherEditor][Core][Theme]" )
{
    theme_doc_t theme( nullptr );
    REQUIRE( EditorTheme_SetHeader( &theme.store, StringView_FromCString( "mine" ), StringView_FromCString( "Mine" ),
                                    StringView_FromCString( "charcoal" ) ) == theme_status_t::OK );
    REQUIRE( EditorTheme_SetColor( &theme.store, kTokens[0], 0x112233FFu, 0x303032FFu ) == theme_status_t::OK );
    const key_value_t *const self[]{ theme.Root() };
    REQUIRE( EditorTheme_ResolveColor( self, 1u, kTokens[0], nullptr ) == 0x112233FFu );

    // Choosing the inherited colour again removes the entry and the empty section.
    REQUIRE( EditorTheme_SetColor( &theme.store, kTokens[0], 0x303032FFu, 0x303032FFu ) == theme_status_t::OK );
    REQUIRE( KeyValue_Find( theme.Root(), StringView_FromCString( "colors" ) ) == nullptr );

    const theme_font_t inherited{ StringView_FromCString( "Menlo" ), 10.0, 400u };
    const theme_font_t larger{ StringView_FromCString( "Menlo" ), 13.0, 400u };
    REQUIRE( EditorTheme_SetFont( &theme.store, kTokens[3], larger, inherited ) == theme_status_t::OK );
    const key_value_t *pConsole = KeyValue_Find( KeyValue_Find( theme.Root(), StringView_FromCString( "fonts" ) ),
                                                 StringView_FromCString( "console" ) );
    REQUIRE( KeyValue_ChildCount( pConsole ) == 1u ); // Only the size differs.
    REQUIRE( KeyValue_Find( pConsole, StringView_FromCString( "size" ) ) != nullptr );

    REQUIRE( EditorTheme_SetMetric( &theme.store, kTokens[4], 200.0, 24.0 ) == theme_status_t::INVALID_ARGUMENT );
    REQUIRE( EditorTheme_SetMetric( &theme.store, kTokens[4], 32.0, 24.0 ) == theme_status_t::OK );
    REQUIRE( EditorTheme_Reset( &theme.store, kTokens[4] ) == theme_status_t::OK );
    REQUIRE( KeyValue_Find( theme.Root(), StringView_FromCString( "metrics" ) ) == nullptr );

    const theme_header_t header = EditorTheme_Header( theme.Root() );
    REQUIRE( Equals( header.id, "mine" ) );
    REQUIRE( Equals( header.base, "charcoal" ) );
}

TEST_CASE( "Theme audits flag unknown tokens, wrong sections, and invalid values",
           "[CypherEditor][Core][Theme]" )
{
    theme_registry_t registry{};
    REQUIRE( EditorThemeRegistry_Init( &registry, Allocator_GetSystem() ) == theme_status_t::OK );
    REQUIRE( EditorThemeRegistry_Register( &registry, kTokens, 5u ) == theme_status_t::OK );
    theme_doc_t theme( R"cykv(@cykv 1
@schema "cypher.theme" 1
{
    id = "audit"
    colors = { "ui.background" = "#000000" "plugin.glow" = "#ff00ff" "console" = "#ffffff" "viewport.axis.x" = "#12" }
    fonts = { console = { size = 900 } }
    metrics = { "ui.icon_size" = 32 }
}
)cykv" );
    theme_problem_t problems[8]{};
    const usize nProblems = EditorTheme_Audit( &registry, theme.Root(), problems, 8u );
    REQUIRE( nProblems == 4u );
    REQUIRE( problems[0].code == theme_problem_code_t::UNKNOWN_TOKEN );
    REQUIRE( Equals( problems[0].token, "plugin.glow" ) );
    REQUIRE( problems[1].code == theme_problem_code_t::WRONG_KIND );
    REQUIRE( problems[2].code == theme_problem_code_t::INVALID_VALUE );
    REQUIRE( Equals( problems[2].token, "viewport.axis.x" ) );
    REQUIRE( problems[3].code == theme_problem_code_t::INVALID_VALUE );
    REQUIRE( Equals( problems[3].token, "console" ) );

    // Unknown tokens survive a save: a plugin theme entry is never lost.
    text_buffer_t text{};
    REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( SettingsDocument_Write( &theme.store, &text ) == settings_document_status_t::OK );
    REQUIRE( std::string( TextBuffer_Data( &text ), TextBuffer_Length( &text ) ).find( "plugin.glow" ) != std::string::npos );
    EditorThemeRegistry_Shutdown( &registry );
}
