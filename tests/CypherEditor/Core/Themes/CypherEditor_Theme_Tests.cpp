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

namespace
{

theme_token_t Derived( const char *pId, theme_derive_op_t op, const char *pA, const char *pB, f64 amount, f64 hue = 0.0 )
{
    theme_token_t token = ColorToken( pId, 0x000000FFu );
    token.derive = theme_derive_t{ op, pA, pB, amount, hue };
    return token;
}

constexpr const char *kGridStyles[]{ "lines", "dots" };

theme_token_t ChoiceToken()
{
    theme_token_t token{};
    token.pId = "viewport.grid.style";
    token.kind = theme_token_kind_t::CHOICE;
    token.ppChoices = kGridStyles;
    token.nChoices = 2u;
    token.pChoiceDefault = "lines";
    return token;
}

const theme_token_t kV2Tokens[]{
    ColorToken( "ui.background", 0x303032FFu ),
    ColorToken( "ui.panel", 0x27282BFFu ),
    ColorToken( "ui.text", 0xDCE0E5FFu ),
    ColorToken( "ui.accent", 0xE1A03EFFu ),
    Derived( "ui.border", theme_derive_op_t::MIX, "ui.background", "ui.text", 0.20 ),
    Derived( "ui.border.highlight", theme_derive_op_t::LIGHTER, "ui.border", nullptr, 110.0 ),
    Derived( "ui.edge", theme_derive_op_t::MIX, "ui.panel", "#000000", 0.32 ),
    Derived( "ui.deepest", theme_derive_op_t::DARKER, "ui.edge", nullptr, 115.0 ),
    Derived( "ui.focus", theme_derive_op_t::COPY, "ui.accent", nullptr, 0.0 ),
    Derived( "ui.status.error.text", theme_derive_op_t::MIX_STATUS, "ui.text", nullptr, 0.58, 0.010 ),
    Derived( "loop.a", theme_derive_op_t::COPY, "loop.b", nullptr, 0.0 ),
    Derived( "loop.b", theme_derive_op_t::COPY, "loop.a", nullptr, 0.0 ),
    ChoiceToken(),
};

u32 Resolve( const theme_registry_t &registry, const theme_doc_t &doc, const char *pId, theme_resolution_t *pResolution = nullptr )
{
    const theme_token_t *pToken = EditorThemeRegistry_Find( &registry, StringView_FromCString( pId ) );
    REQUIRE( pToken != nullptr );
    const key_value_t *chain[]{ doc.Root() };
    return EditorTheme_ResolveColorIn( &registry, chain, 1u, *pToken, pResolution );
}

} // namespace

TEST_CASE( "Derived colours follow their formula until a theme sets them", "[CypherEditor][Core][Theme]" )
{
    theme_registry_t registry{};
    REQUIRE( EditorThemeRegistry_Init( &registry, Allocator_GetSystem() ) == theme_status_t::OK );
    REQUIRE( EditorThemeRegistry_Register( &registry, kV2Tokens, std::size( kV2Tokens ) ) == theme_status_t::OK );

    theme_doc_t plain( "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"plain\" name = \"Plain\" }\n" );
    // The formulas reproduce the charcoal look the stylesheet used to blend.
    CHECK( Resolve( registry, plain, "ui.border" ) == 0x525356FFu );
    CHECK( Resolve( registry, plain, "ui.edge" ) == 0x1A1B1DFFu );
    CHECK( Resolve( registry, plain, "ui.focus" ) == 0xE1A03EFFu );
    const u32 highlight = Resolve( registry, plain, "ui.border.highlight" );
    CHECK( ( ( highlight >> 24u ) & 0xFFu ) > 0x53u ); // Lighter than the border.
    const u32 deepest = Resolve( registry, plain, "ui.deepest" );
    CHECK( ( ( deepest >> 24u ) & 0xFFu ) < 0x1Au ); // Darker than the edge.
    // Error text leans red on a dark theme.
    const u32 error = Resolve( registry, plain, "ui.status.error.text" );
    CHECK( ( ( error >> 24u ) & 0xFFu ) > ( ( error >> 8u ) & 0xFFu ) );
    // A cycle falls back to the default instead of recursing forever.
    CHECK( Resolve( registry, plain, "loop.a" ) == 0x000000FFu );

    // Changing a base colour moves every derived colour with it; "auto"
    // keeps a derived token on its formula; a set value wins.
    theme_doc_t edited( "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"edited\" name = \"Edited\"\n"
                        "  colors = { \"ui.background\" = \"#000000\" \"ui.text\" = \"#ffffff\" \"ui.border\" = \"auto\"\n"
                        "             \"ui.focus\" = \"#123456\" } }\n" );
    CHECK( Resolve( registry, edited, "ui.border" ) == 0x333333FFu );
    theme_resolution_t resolution{};
    CHECK( Resolve( registry, edited, "ui.focus", &resolution ) == 0x123456FFu );
    CHECK( resolution.iSource == 0u );
    EditorThemeRegistry_Shutdown( &registry );
}

TEST_CASE( "Choices resolve to listed values and V2 header details read back", "[CypherEditor][Core][Theme]" )
{
    theme_registry_t registry{};
    REQUIRE( EditorThemeRegistry_Init( &registry, Allocator_GetSystem() ) == theme_status_t::OK );
    REQUIRE( EditorThemeRegistry_Register( &registry, kV2Tokens, std::size( kV2Tokens ) ) == theme_status_t::OK );
    const theme_token_t &choice = kV2Tokens[std::size( kV2Tokens ) - 1u];

    theme_doc_t doc( "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"night\" name = \"Night\" author = \"Karlo\"\n"
                     "  description = \"Dots.\" appearance = \"light\" choices = { \"viewport.grid.style\" = \"dots\" } }\n" );
    const key_value_t *chain[]{ doc.Root() };
    CHECK( Equals( EditorTheme_ResolveChoice( chain, 1u, choice, nullptr ), "dots" ) );
    const theme_header_t header = EditorTheme_Header( doc.Root() );
    CHECK( Equals( header.author, "Karlo" ) );
    CHECK( Equals( header.description, "Dots." ) );
    CHECK( header.bLight );

    // Unlisted values are skipped, counted, and flagged by the audit.
    theme_doc_t bad( "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"bad\" name = \"Bad\"\n"
                     "  choices = { \"viewport.grid.style\" = \"stripes\" } colors = { \"ui.border\" = \"auto\" } }\n" );
    const key_value_t *badChain[]{ bad.Root() };
    theme_resolution_t resolution{};
    CHECK( Equals( EditorTheme_ResolveChoice( badChain, 1u, choice, &resolution ), "lines" ) );
    CHECK( resolution.nInvalidSkipped == 1u );
    theme_problem_t problems[4]{};
    REQUIRE( EditorTheme_Audit( &registry, bad.Root(), problems, 4u ) == 1u ); // "auto" is not a problem.
    CHECK( problems[0].code == theme_problem_code_t::INVALID_VALUE );

    // Setters: a choice equal to what is inherited is not stored; "auto" is.
    REQUIRE( EditorTheme_SetChoice( &doc.store, choice, StringView_FromCString( "lines" ), StringView_FromCString( "lines" ) ) ==
             theme_status_t::OK );
    CHECK( Equals( EditorTheme_ResolveChoice( chain, 1u, choice, nullptr ), "lines" ) );
    CHECK( EditorTheme_SetChoice( &doc.store, choice, StringView_FromCString( "stripes" ), StringView_FromCString( "lines" ) ) ==
           theme_status_t::INVALID_ARGUMENT );
    REQUIRE( EditorTheme_SetColorAuto( &doc.store, kV2Tokens[4] ) == theme_status_t::OK );
    REQUIRE( EditorTheme_SetDetails( &doc.store, StringView_FromCString( "Someone" ), string_view_t{}, CY_FALSE ) == theme_status_t::OK );
    const theme_header_t edited = EditorTheme_Header( doc.Root() );
    CHECK( Equals( edited.author, "Someone" ) );
    CHECK( edited.description.cchLength == 0u );
    CHECK_FALSE( edited.bLight );
    EditorThemeRegistry_Shutdown( &registry );
}

TEST_CASE( "V1 themes still load; saving writes V2", "[CypherEditor][Core][Theme]" )
{
    theme_doc_t v1( "@cykv 1\n@schema \"cypher.theme\" 1\n{ id = \"old\" name = \"Old\" colors = { \"ui.background\" = \"#101010\" } }\n" );
    text_buffer_t text{};
    REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( SettingsDocument_Write( &v1.store, &text ) == settings_document_status_t::OK );
    CHECK( std::string( TextBuffer_CStr( &text ) ).find( "\"cypher.theme\" 2" ) != std::string::npos );
}

TEST_CASE( "A complete theme lists every token and keeps derived ones on auto", "[CypherEditor][Core][Theme]" )
{
    theme_registry_t registry{};
    REQUIRE( EditorThemeRegistry_Init( &registry, Allocator_GetSystem() ) == theme_status_t::OK );
    REQUIRE( EditorThemeRegistry_Register( &registry, kV2Tokens, std::size( kV2Tokens ) ) == theme_status_t::OK );
    // A partial theme that sets one base colour and one derived colour.
    theme_doc_t partial( "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"p\" name = \"P\"\n"
                         "  colors = { \"ui.accent\" = \"#12ab34\" \"ui.edge\" = \"#010203\" } choices = { \"viewport.grid.style\" = \"dots\" } }\n" );
    const key_value_t *chain[]{ partial.Root() };
    theme_doc_t complete( "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"c\" name = \"C\" }\n" );
    const theme_token_t *order[std::size( kV2Tokens )]{};
    for ( usize i = 0u; i < std::size( kV2Tokens ); ++i ) { order[i] = &kV2Tokens[i]; }
    REQUIRE( EditorTheme_WriteComplete( &complete.store, &registry, order, std::size( order ), chain, 1u ) == theme_status_t::OK );

    const key_value_t *pColors = KeyValue_Find( complete.Root(), StringView_FromCString( "colors" ) );
    string_view_t text{};
    REQUIRE( KeyValue_GetString( KeyValue_Find( pColors, StringView_FromCString( "ui.accent" ) ), &text ) );
    CHECK( Equals( text, "#12ab34" ) );
    REQUIRE( KeyValue_GetString( KeyValue_Find( pColors, StringView_FromCString( "ui.border" ) ), &text ) );
    CHECK( Equals( text, "auto" ) ); // Derived and unset: follows its formula.
    REQUIRE( KeyValue_GetString( KeyValue_Find( pColors, StringView_FromCString( "ui.edge" ) ), &text ) );
    CHECK( Equals( text, "#010203" ) ); // Derived but set: kept explicit.
    REQUIRE( KeyValue_GetString( KeyValue_Find( pColors, StringView_FromCString( "ui.background" ) ), &text ) );
    CHECK( Equals( text, "#303032" ) ); // Base default written out.
    REQUIRE( KeyValue_GetString( KeyValue_Find( KeyValue_Find( complete.Root(), StringView_FromCString( "choices" ) ),
                                                StringView_FromCString( "viewport.grid.style" ) ), &text ) );
    CHECK( Equals( text, "dots" ) );
    // The complete theme resolves exactly like the partial one.
    const key_value_t *completeChain[]{ complete.Root() };
    for ( const theme_token_t &token : kV2Tokens ) {
        if ( token.kind != theme_token_kind_t::COLOR ) { continue; }
        INFO( token.pId );
        CHECK( EditorTheme_ResolveColorIn( &registry, completeChain, 1u, token, nullptr ) == EditorTheme_ResolveColorIn( &registry, chain, 1u, token, nullptr ) );
    }
    EditorThemeRegistry_Shutdown( &registry );
}

TEST_CASE( "Tokens nobody registers survive a save, in their own sections", "[CypherEditor][Core][Theme]" )
{
    theme_registry_t registry{};
    REQUIRE( EditorThemeRegistry_Init( &registry, Allocator_GetSystem() ) == theme_status_t::OK );
    REQUIRE( EditorThemeRegistry_Register( &registry, kTokens, 5u ) == theme_status_t::OK );
    theme_doc_t base( R"cykv(@cykv 1
@schema "cypher.theme" 2
{
    id = "base"
    name = "Base"
    colors = { "plugin.glow" = "#00ff00" "plugin.other" = "#0000ff" }
    fonts = { "ui.background" = { family = "Wrong" } }
}
)cykv" );
    theme_doc_t child( R"cykv(@cykv 1
@schema "cypher.theme" 2
{
    id = "child"
    name = "Child"
    base = "base"
    colors = { "plugin.glow" = "#ff0000" "ui.background" = "#101010" }
    fonts = { "plugin.font" = { family = "Plugin Sans" size = 12.0 } }
}
)cykv" );
    theme_doc_t saved( "@cykv 1\n@schema \"cypher.theme\" 2\n{ id = \"saved\" name = \"Saved\" colors = { \"plugin.other\" = \"#123456\" } }\n" );
    const key_value_t *chain[]{ child.Root(), base.Root() };
    REQUIRE( EditorTheme_KeepUnknownTokens( &saved.store, &registry, chain, 2u ) == theme_status_t::OK );

    const key_value_t *pColors = KeyValue_Find( saved.Root(), StringView_FromCString( "colors" ) );
    string_view_t text{};
    REQUIRE( KeyValue_GetString( KeyValue_Find( pColors, StringView_FromCString( "plugin.glow" ) ), &text ) );
    CHECK( Equals( text, "#ff0000" ) ); // The more specific theme wins.
    REQUIRE( KeyValue_GetString( KeyValue_Find( pColors, StringView_FromCString( "plugin.other" ) ), &text ) );
    CHECK( Equals( text, "#123456" ) ); // What the saved theme already had stays.
    CHECK( KeyValue_Find( pColors, StringView_FromCString( "ui.background" ) ) == nullptr ); // Registered: not this function's job.
    const key_value_t *pFonts = KeyValue_Find( saved.Root(), StringView_FromCString( "fonts" ) );
    REQUIRE( KeyValue_GetString( KeyValue_Find( KeyValue_Find( pFonts, StringView_FromCString( "plugin.font" ) ), StringView_FromCString( "family" ) ), &text ) );
    CHECK( Equals( text, "Plugin Sans" ) ); // Whole objects are copied.
    CHECK( KeyValue_Find( pFonts, StringView_FromCString( "ui.background" ) ) == nullptr ); // Registered, wrong section: dropped.
    theme_problem_t problems[4]{};
    CHECK( EditorTheme_Audit( &registry, saved.Root(), problems, 4u ) == 3u ); // plugin.other, plugin.glow, plugin.font: all unknown.
    CHECK( EditorTheme_KeepUnknownTokens( &saved.store, nullptr, chain, 2u ) == theme_status_t::INVALID_ARGUMENT );
    EditorThemeRegistry_Shutdown( &registry );
}
