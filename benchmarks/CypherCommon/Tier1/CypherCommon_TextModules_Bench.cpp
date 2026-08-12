//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_TextModules_Bench.cpp
//  Purpose: Benchmarks Tier1 text, path, Unicode, and address modules.
//  Details: Measures representative transforms, parsing, matching, and lookup
//           workloads for modules not covered by the focused string benchmarks.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Localization.h"
#include "CypherCommon_NameServiceAddress.h"
#include "CypherCommon_NetAddress.h"
#include "CypherCommon_PathMatch.h"
#include "CypherCommon_StringConvert.h"
#include "CypherCommon_StringEscape.h"
#include "CypherCommon_StringFormat.h"
#include "CypherCommon_StringHtml.h"
#include "CypherCommon_StringMatch.h"
#include "CypherCommon_StringPath.h"
#include "CypherCommon_StringUrl.h"
#include "CypherCommon_Unicode.h"
#include "CypherCommon_WideChar.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

void BM_StringConvert_I64( benchmark::State &state )
{
    char output[32]{};
    i64 value = -9223372036854775000ll;
    for ( auto _ : state ) {
        string_convert_result_t result = StringConvert_I64(
            value,
            {},
            output,
            sizeof( output ) );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( output );
        value += 7919;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_StringEscape_Encode( benchmark::State &state )
{
    const string_view_t source = StringView_FromCString(
        "materials/\"facility\"/wall\\panel\n"
        "unicode:\xC3\xA9; control:\t; shader=retro" );
    constexpr flags32_t flags =
        STRING_ESCAPE_FLAG_QUOTES |
        STRING_ESCAPE_FLAG_BACKSLASH |
        STRING_ESCAPE_FLAG_CONTROL_CHARS |
        STRING_ESCAPE_FLAG_NON_ASCII |
        STRING_ESCAPE_FLAG_PATH_SLASHES;
    char output[512]{};
    for ( auto _ : state ) {
        string_escape_result_t result = StringEscape_Encode(
            source,
            string_escape_style_t::CYPHER,
            flags,
            output,
            sizeof( output ) );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( output );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( source.cchLength ) );
}

void BM_StringHtml_StripTags( benchmark::State &state )
{
    const string_view_t source = StringView_FromCString(
        "<section><h1>Facility</h1><p class=\"warning\">"
        "Gas pressure &amp; containment status</p><br>Ready</section>" );
    char output[512]{};
    for ( auto _ : state ) {
        html_text_result_t result = StringHtml_StripTags(
            source,
            HTML_TEXT_FLAG_PRESERVE_LINE_BREAKS |
                HTML_TEXT_FLAG_COLLAPSE_WHITESPACE,
            output,
            sizeof( output ) );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( output );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( source.cchLength ) );
}

void BM_StringMatch_Wildcard( benchmark::State &state )
{
    const string_view_t text = StringView_FromCString(
        "materials/facility/sector_07/wall_retro.cymat" );
    const string_view_t pattern = StringView_FromCString(
        "materials/**/sector_[0-9][0-9]/*_retro.cymat" );
    constexpr flags32_t flags =
        STRING_MATCH_FLAG_ALLOW_CHARACTER_CLASS |
        STRING_MATCH_FLAG_STAR_MATCHES_SEPARATOR;
    for ( auto _ : state ) {
        bool_t matches = StringMatch_Wildcard( text, pattern, flags );
        benchmark::DoNotOptimize( matches );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_StringPath_Normalize( benchmark::State &state )
{
    const string_view_t source = StringView_FromCString(
        "Materials\\Facility//Sector_07/./Props/../WALL_RETro.CYMAT" );
    constexpr flags32_t flags =
        PATH_NORMALIZE_FLAG_COLLAPSE_SEPARATORS |
        PATH_NORMALIZE_FLAG_RESOLVE_DOT |
        PATH_NORMALIZE_FLAG_RESOLVE_DOT_DOT |
        PATH_NORMALIZE_FLAG_LOWERCASE_ASCII |
        PATH_NORMALIZE_FLAG_REJECT_ABSOLUTE |
        PATH_NORMALIZE_FLAG_REJECT_ABOVE_ROOT;
    char output[256]{};
    for ( auto _ : state ) {
        path_write_result_t result = StringPath_Normalize(
            source,
            path_style_t::VIRTUAL,
            flags,
            output,
            sizeof( output ) );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( output );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( source.cchLength ) );
}

void BM_StringUrl_Parse( benchmark::State &state )
{
    const string_view_t url = StringView_FromCString(
        "cypher://user@assets.example.test:27015/"
        "packages/base.cypkg?revision=42#manifest" );
    for ( auto _ : state ) {
        url_parts_t parts{};
        url_result_t result = StringUrl_Parse( url, &parts );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( parts );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( url.cchLength ) );
}

void BM_PathMatch_Filter( benchmark::State &state )
{
    const string_view_t includes[]{
        StringView_FromCString( "*.cymat" ),
        StringView_FromCString( "*.cytex" ),
        StringView_FromCString( "*.cymesh" )
    };
    const string_view_t excludes[]{
        StringView_FromCString( "debug_*" ),
        StringView_FromCString( "*_temp.*" )
    };
    const path_filter_t filter{
        includes,
        3u,
        excludes,
        2u,
        PATH_MATCH_FLAG_CASE_INSENSITIVE_ASCII |
            PATH_MATCH_FLAG_BASENAME_ONLY
    };
    const string_view_t path = StringView_FromCString(
        "materials/facility/wall_retro.cymat" );
    for ( auto _ : state ) {
        bool_t matches = PathMatch_Filter( path, filter );
        benchmark::DoNotOptimize( matches );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_Unicode_ValidateUtf8( benchmark::State &state )
{
    std::array<char, 4096> text{};
    for ( usize iByte = 0u; iByte < text.size(); ++iByte ) {
        text[iByte] = static_cast<char>( 'a' + ( iByte % 26u ) );
    }
    const string_view_t view{ text.data(), text.size() };
    for ( auto _ : state ) {
        unicode_result_t result = Unicode_ValidateUtf8( view );
        benchmark::DoNotOptimize( result );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( text.size() ) );
}

void BM_Unicode_Utf8ToUtf16( benchmark::State &state )
{
    std::array<char, 2048> text{};
    for ( usize iByte = 0u; iByte < text.size(); ++iByte ) {
        text[iByte] = static_cast<char>( 'a' + ( iByte % 26u ) );
    }
    std::array<utf16_unit_t, 2048> output{};
    const string_view_t view{ text.data(), text.size() };
    for ( auto _ : state ) {
        unicode_result_t result = Unicode_Utf8ToUtf16(
            view,
            UNICODE_FLAG_NONE,
            { output.data(), output.size() } );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( output );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( text.size() ) );
}

void BM_WideChar_Compare( benchmark::State &state )
{
    constexpr wchar_engine_t first[] =
        L"materials/facility/sector_07/wall_retro.cymat";
    constexpr wchar_engine_t second[] =
        L"materials/facility/sector_07/wall_retro.cytex";
    for ( auto _ : state ) {
        i32 order = WChar_Compare( first, second );
        benchmark::DoNotOptimize( order );
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_NetAddress_ParseAndFormat( benchmark::State &state )
{
    const string_view_t text = StringView_FromCString( "[2001:db8::7%3]:27015" );
    char output[64]{};
    for ( auto _ : state ) {
        net_address_t address{};
        bool_t parsed = NetAddress_Parse( text, 0u, &address );
        usize cchRequired = NetAddress_Format(
            address,
            CY_TRUE,
            output,
            sizeof( output ) );
        benchmark::DoNotOptimize( parsed );
        benchmark::DoNotOptimize( cchRequired );
        benchmark::DoNotOptimize( output );
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_NameServiceAddress_ParseAndFormat( benchmark::State &state )
{
    const string_view_t text = StringView_FromCString(
        "matchmaking.eu.cypher.test:game-udp" );
    char output[128]{};
    for ( auto _ : state ) {
        name_service_address_t address{};
        bool_t parsed = NameServiceAddress_Parse( text, 27015u, &address );
        usize cchRequired = NameServiceAddress_Format(
            address,
            output,
            sizeof( output ) );
        benchmark::DoNotOptimize( parsed );
        benchmark::DoNotOptimize( cchRequired );
        benchmark::DoNotOptimize( output );
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

void BM_Localization_FindByKey( benchmark::State &state )
{
    localization_catalog_t *pCatalog = Localization_CreateCatalog(
        { Allocator_GetSystem(), StringView_FromCString( "en-US" ), 256u } );
    if ( pCatalog == nullptr ) {
        state.SkipWithError( "Localization catalog creation failed." );
        return;
    }

    constexpr usize nEntryCount = 256u;
    std::array<std::array<char, 32>, nEntryCount> keys{};
    for ( usize iEntry = 0u; iEntry < nEntryCount; ++iEntry ) {
        const string_format_result_t formatResult = StringFormat_Printf(
            keys[iEntry].data(),
            keys[iEntry].size(),
            "hud.message.%03zu",
            iEntry );
        if ( formatResult.status != string_format_status_t::OK ||
             !Localization_Add(
                 pCatalog,
                 StringView_FromCString( keys[iEntry].data() ),
                 StringView_FromCString( "Facility status message" ) ) ) {
            Localization_DestroyCatalog( pCatalog );
            state.SkipWithError( "Localization catalog population failed." );
            return;
        }
    }

    usize iEntry = 0u;
    for ( auto _ : state ) {
        string_view_t value = Localization_FindByKey(
            pCatalog,
            StringView_FromCString(
                keys[iEntry & ( nEntryCount - 1u )].data() ) );
        benchmark::DoNotOptimize( value );
        iEntry += 17u;
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
    Localization_DestroyCatalog( pCatalog );
}

} // namespace

BENCHMARK( BM_StringConvert_I64 );
BENCHMARK( BM_StringEscape_Encode );
BENCHMARK( BM_StringHtml_StripTags );
BENCHMARK( BM_StringMatch_Wildcard );
BENCHMARK( BM_StringPath_Normalize );
BENCHMARK( BM_StringUrl_Parse );
BENCHMARK( BM_PathMatch_Filter );
BENCHMARK( BM_Unicode_ValidateUtf8 );
BENCHMARK( BM_Unicode_Utf8ToUtf16 );
BENCHMARK( BM_WideChar_Compare );
BENCHMARK( BM_NetAddress_ParseAndFormat );
BENCHMARK( BM_NameServiceAddress_ParseAndFormat );
BENCHMARK( BM_Localization_FindByKey );
