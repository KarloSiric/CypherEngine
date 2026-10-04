//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Theme.cpp
//  Purpose: Implements editor theme tokens, base chains, resolution, sparse
//           editing, and auditing.
//  Details: Font tokens resolve field by field, so a theme that only makes
//           the console larger keeps the family its base chose. The registry
//           is kept sorted so lookups stay logarithmic as plugins add tokens.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_Theme.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier2/CypherCommon_DataValidation.h"

#include <cmath>
#include <cstring>

namespace cypher::editor
{

using namespace cypher::common;

namespace
{

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t ThemeText( const char ( &text )[nExtent] ) noexcept
{
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

constexpr f64 kFontSizeMin = 1.0;
constexpr f64 kFontSizeMax = 200.0;
constexpr usize kFontFamilyMax = 259u;

CYPHER_NODISCARD string_view_t SectionName( theme_token_kind_t kind ) noexcept
{
    switch ( kind ) {
        case theme_token_kind_t::COLOR: return ThemeText( "colors" );
        case theme_token_kind_t::FONT: return ThemeText( "fonts" );
        case theme_token_kind_t::METRIC: return ThemeText( "metrics" );
        case theme_token_kind_t::CHOICE: return ThemeText( "choices" );
    }
    return ThemeText( "colors" );
}

CYPHER_NODISCARD i32 CompareIds( string_view_t a, string_view_t b ) noexcept
{
    const usize cch = a.cchLength < b.cchLength ? a.cchLength : b.cchLength;
    const i32 order = cch != 0u ? std::memcmp( a.pData, b.pData, cch ) : 0;
    if ( order != 0 ) { return order; }
    return a.cchLength < b.cchLength ? -1 : ( a.cchLength > b.cchLength ? 1 : 0 );
}

// Token IDs: lower-case ASCII letters, digits, '_', and '.', starting with a
// letter, with no empty dotted part.
CYPHER_NODISCARD bool_t IsTokenId( string_view_t id ) noexcept
{
    if ( id.cchLength == 0u || id.cchLength > EDITOR_THEME_TOKEN_MAX_LENGTH ||
         id.pData[0] < 'a' || id.pData[0] > 'z' || id.pData[id.cchLength - 1u] == '.' ) {
        return CY_FALSE;
    }
    for ( usize iChar = 0u; iChar < id.cchLength; ++iChar ) {
        const char c = id.pData[iChar];
        const bool_t bOk = ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' ||
                           ( c == '.' && id.pData[iChar - 1u] != '.' );
        if ( !bOk ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD const key_value_t *FindEntry(
    const key_value_t *pThemeRoot,
    theme_token_kind_t kind,
    string_view_t id ) noexcept
{
    const key_value_t *pSection = KeyValue_Find( pThemeRoot, SectionName( kind ) );
    return KeyValue_Type( pSection ) == key_value_type_t::OBJECT ? KeyValue_Find( pSection, id ) : nullptr;
}

CYPHER_NODISCARD bool_t ReadNumber( const key_value_t *pValue, f64 &valueOut ) noexcept
{
    i64 nSigned = 0;
    u64 nUnsigned = 0u;
    if ( KeyValue_GetF64( pValue, &valueOut ) ) { return std::isfinite( valueOut ); }
    if ( KeyValue_GetI64( pValue, &nSigned ) ) { valueOut = static_cast<f64>( nSigned ); return CY_TRUE; }
    if ( KeyValue_GetU64( pValue, &nUnsigned ) ) { valueOut = static_cast<f64>( nUnsigned ); return CY_TRUE; }
    return CY_FALSE;
}

enum class read_t : u8 { VALUE, ABSENT, INVALID };

// "auto" reads as absent: the token passes on to the base theme and, at the
// end of the chain, to its formula.
CYPHER_NODISCARD read_t ReadColor( const key_value_t *pEntry, u32 &rgbaOut ) noexcept
{
    if ( pEntry == nullptr ) { return read_t::ABSENT; }
    string_view_t text{};
    if ( !KeyValue_GetString( pEntry, &text ) ) { return read_t::INVALID; }
    if ( StringView_Equals( text, ThemeText( "auto" ) ) ) { return read_t::ABSENT; }
    return SettingColor_Parse( text, &rgbaOut ) ? read_t::VALUE : read_t::INVALID;
}

CYPHER_NODISCARD read_t ReadChoice( const key_value_t *pEntry, const theme_token_t &token, string_view_t &valueOut ) noexcept
{
    if ( pEntry == nullptr ) { return read_t::ABSENT; }
    string_view_t text{};
    if ( !KeyValue_GetString( pEntry, &text ) ) { return read_t::INVALID; }
    for ( usize i = 0u; i < token.nChoices; ++i ) {
        if ( StringView_Equals( text, StringView_FromCString( token.ppChoices[i] ) ) ) {
            valueOut = text;
            return read_t::VALUE;
        }
    }
    return read_t::INVALID;
}

CYPHER_NODISCARD read_t ReadMetric( const key_value_t *pEntry, const theme_token_t &token, f64 &valueOut ) noexcept
{
    if ( pEntry == nullptr ) { return read_t::ABSENT; }
    return ReadNumber( pEntry, valueOut ) && valueOut >= token.flMin && valueOut <= token.flMax
        ? read_t::VALUE : read_t::INVALID;
}

enum font_field_t : usize { FONT_FAMILY = 0u, FONT_SIZE, FONT_WEIGHT, FONT_FIELD_COUNT };

// Reads one font field. A font entry that is not an object is invalid for
// every field; an object simply lacks the fields it does not override.
CYPHER_NODISCARD read_t ReadFontField( const key_value_t *pEntry, font_field_t field, theme_font_t &fontOut ) noexcept
{
    if ( pEntry == nullptr ) { return read_t::ABSENT; }
    if ( KeyValue_Type( pEntry ) != key_value_type_t::OBJECT ) { return read_t::INVALID; }
    switch ( field ) {
        case FONT_FAMILY: {
            const key_value_t *pFamily = KeyValue_Find( pEntry, ThemeText( "family" ) );
            if ( pFamily == nullptr ) { return read_t::ABSENT; }
            string_view_t family{};
            if ( !KeyValue_GetString( pFamily, &family ) || family.cchLength == 0u || family.cchLength > kFontFamilyMax ) {
                return read_t::INVALID;
            }
            fontOut.family = family;
            return read_t::VALUE;
        }
        case FONT_SIZE: {
            const key_value_t *pSize = KeyValue_Find( pEntry, ThemeText( "size" ) );
            if ( pSize == nullptr ) { return read_t::ABSENT; }
            f64 flSize = 0.0;
            if ( !ReadNumber( pSize, flSize ) || flSize < kFontSizeMin || flSize > kFontSizeMax ) { return read_t::INVALID; }
            fontOut.flSize = flSize;
            return read_t::VALUE;
        }
        case FONT_WEIGHT: {
            const key_value_t *pWeight = KeyValue_Find( pEntry, ThemeText( "weight" ) );
            if ( pWeight == nullptr ) { return read_t::ABSENT; }
            f64 flWeight = 0.0;
            if ( !ReadNumber( pWeight, flWeight ) || flWeight < 1.0 || flWeight > 1000.0 ||
                 flWeight != std::floor( flWeight ) ) {
                return read_t::INVALID;
            }
            fontOut.nWeight = static_cast<u32>( flWeight );
            return read_t::VALUE;
        }
        case FONT_FIELD_COUNT: break;
    }
    return read_t::INVALID;
}

CYPHER_NODISCARD bool_t EntryPath( theme_token_kind_t kind, const char *pId, settings_path_t &pathOut ) noexcept
{
    pathOut = {};
    return pId != nullptr &&
           SettingsPath_Append( &pathOut, SectionName( kind ) ) &&
           SettingsPath_Append( &pathOut, StringView_FromCString( pId ) );
}

CYPHER_NODISCARD theme_status_t FromStore( settings_document_status_t status ) noexcept
{
    switch ( status ) {
        case settings_document_status_t::OK: return theme_status_t::OK;
        case settings_document_status_t::OUT_OF_MEMORY: return theme_status_t::OUT_OF_MEMORY;
        case settings_document_status_t::INVALID_ARGUMENT: return theme_status_t::INVALID_ARGUMENT;
        default: return theme_status_t::STORE_FAILED;
    }
}

CYPHER_NODISCARD bool_t TokenDefaultValid( const theme_token_t &token ) noexcept
{
    switch ( token.kind ) {
        case theme_token_kind_t::COLOR: return CY_TRUE;
        case theme_token_kind_t::FONT:
            return token.pFontFamily != nullptr && token.flFontSize >= kFontSizeMin &&
                   token.flFontSize <= kFontSizeMax && token.nFontWeight >= 1u && token.nFontWeight <= 1000u;
        case theme_token_kind_t::METRIC:
            return std::isfinite( token.flMin ) && std::isfinite( token.flMax ) && token.flMin <= token.flMax &&
                   token.flDefault >= token.flMin && token.flDefault <= token.flMax;
        case theme_token_kind_t::CHOICE: {
            if ( token.ppChoices == nullptr || token.nChoices == 0u || token.nChoices > EDITOR_THEME_MAX_CHOICES ||
                 token.pChoiceDefault == nullptr ) {
                return CY_FALSE;
            }
            for ( usize i = 0u; i < token.nChoices; ++i ) {
                if ( token.ppChoices[i] != nullptr && std::strcmp( token.ppChoices[i], token.pChoiceDefault ) == 0 ) { return CY_TRUE; }
            }
            return CY_FALSE;
        }
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t DeriveValid( const theme_derive_t &derive ) noexcept
{
    switch ( derive.op ) {
        case theme_derive_op_t::NONE: return CY_TRUE;
        case theme_derive_op_t::COPY: return derive.pA != nullptr;
        case theme_derive_op_t::MIX: return derive.pA != nullptr && derive.pB != nullptr && derive.flAmount >= 0.0 && derive.flAmount <= 1.0;
        case theme_derive_op_t::LIGHTER:
        case theme_derive_op_t::DARKER: return derive.pA != nullptr && derive.flAmount > 0.0;
        case theme_derive_op_t::MIX_STATUS:
            return derive.pA != nullptr && derive.flAmount >= 0.0 && derive.flAmount <= 1.0 && derive.flHue >= 0.0 && derive.flHue < 1.0;
    }
    return CY_FALSE;
}

// ---------------------------------------------------------------------------
// Colour arithmetic for derived tokens. Mixing matches the Qt editor's
// QColor::fromRgbF rounding and lighter/darker follow QColor's HSV rule, so
// a derived token resolves to the same value the GUI used to compute.
// ---------------------------------------------------------------------------

struct rgb_t {
    f64 r{ 0.0 };
    f64 g{ 0.0 };
    f64 b{ 0.0 };
    f64 a{ 1.0 };
};

CYPHER_NODISCARD rgb_t ToRgb( u32 rgba ) noexcept
{
    return { ( ( rgba >> 24u ) & 0xFFu ) / 255.0, ( ( rgba >> 16u ) & 0xFFu ) / 255.0, ( ( rgba >> 8u ) & 0xFFu ) / 255.0,
             ( rgba & 0xFFu ) / 255.0 };
}

// QColor stores 16-bit channels (rounded from float) and reports 8-bit ones
// as the high byte.
CYPHER_NODISCARD u32 Channel8( f64 value ) noexcept
{
    const f32 clamped = static_cast<f32>( value < 0.0 ? 0.0 : ( value > 1.0 ? 1.0 : value ) );
    const u32 wide = static_cast<u32>( clamped * 65535.0f + 0.5f );
    return wide >> 8u;
}

CYPHER_NODISCARD u32 FromRgb( const rgb_t &c ) noexcept
{
    return ( Channel8( c.r ) << 24u ) | ( Channel8( c.g ) << 16u ) | ( Channel8( c.b ) << 8u ) | Channel8( c.a );
}

CYPHER_NODISCARD u32 Mix( u32 a, u32 b, f64 t ) noexcept
{
    const rgb_t ca = ToRgb( a );
    const rgb_t cb = ToRgb( b );
    // Derived colours are opaque, as the Qt blend was.
    return FromRgb( { ca.r + ( cb.r - ca.r ) * t, ca.g + ( cb.g - ca.g ) * t, ca.b + ( cb.b - ca.b ) * t, 1.0 } );
}

struct hsv_t {
    f64 h{ 0.0 }; // 0..1
    f64 s{ 0.0 };
    f64 v{ 0.0 };
};

CYPHER_NODISCARD hsv_t ToHsv( const rgb_t &c ) noexcept
{
    const f64 maxC = std::fmax( c.r, std::fmax( c.g, c.b ) );
    const f64 minC = std::fmin( c.r, std::fmin( c.g, c.b ) );
    const f64 delta = maxC - minC;
    hsv_t hsv{ 0.0, maxC > 0.0 ? delta / maxC : 0.0, maxC };
    if ( delta > 0.0 ) {
        if ( maxC == c.r ) { hsv.h = std::fmod( ( c.g - c.b ) / delta, 6.0 ); }
        else if ( maxC == c.g ) { hsv.h = ( c.b - c.r ) / delta + 2.0; }
        else { hsv.h = ( c.r - c.g ) / delta + 4.0; }
        hsv.h /= 6.0;
        if ( hsv.h < 0.0 ) { hsv.h += 1.0; }
    }
    return hsv;
}

CYPHER_NODISCARD rgb_t FromHsv( const hsv_t &hsv, f64 alpha ) noexcept
{
    const f64 h = hsv.h * 6.0;
    const f64 c = hsv.v * hsv.s;
    const f64 x = c * ( 1.0 - std::fabs( std::fmod( h, 2.0 ) - 1.0 ) );
    const f64 m = hsv.v - c;
    f64 r = 0.0;
    f64 g = 0.0;
    f64 b = 0.0;
    if ( h < 1.0 ) { r = c; g = x; }
    else if ( h < 2.0 ) { r = x; g = c; }
    else if ( h < 3.0 ) { g = c; b = x; }
    else if ( h < 4.0 ) { g = x; b = c; }
    else if ( h < 5.0 ) { r = x; b = c; }
    else { r = c; b = x; }
    return { r + m, g + m, b + m, alpha };
}

// QColor::lighter: value scaled by the factor; overflow drains saturation.
CYPHER_NODISCARD u32 Lighter( u32 rgba, f64 percent ) noexcept
{
    const rgb_t c = ToRgb( rgba );
    hsv_t hsv = ToHsv( c );
    f64 v = hsv.v * percent / 100.0;
    if ( v > 1.0 ) {
        hsv.s = std::fmax( 0.0, hsv.s - ( v - 1.0 ) );
        v = 1.0;
    }
    hsv.v = v;
    return FromRgb( FromHsv( hsv, c.a ) );
}

CYPHER_NODISCARD u32 Darker( u32 rgba, f64 percent ) noexcept
{
    const rgb_t c = ToRgb( rgba );
    hsv_t hsv = ToHsv( c );
    hsv.v = hsv.v * 100.0 / percent;
    return FromRgb( FromHsv( hsv, c.a ) );
}

CYPHER_NODISCARD f64 HslLightness( const rgb_t &c ) noexcept
{
    return ( std::fmax( c.r, std::fmax( c.g, c.b ) ) + std::fmin( c.r, std::fmin( c.g, c.b ) ) ) * 0.5;
}

CYPHER_NODISCARD f64 HslSaturation( const rgb_t &c ) noexcept
{
    const f64 maxC = std::fmax( c.r, std::fmax( c.g, c.b ) );
    const f64 minC = std::fmin( c.r, std::fmin( c.g, c.b ) );
    const f64 l = ( maxC + minC ) * 0.5;
    const f64 delta = maxC - minC;
    if ( delta <= 0.0 ) { return 0.0; }
    return delta / ( 1.0 - std::fabs( 2.0 * l - 1.0 ) );
}

CYPHER_NODISCARD u32 FromHsl( f64 h, f64 s, f64 l ) noexcept
{
    const f64 c = ( 1.0 - std::fabs( 2.0 * l - 1.0 ) ) * s;
    const f64 v = l + c * 0.5;
    const f64 sv = v > 0.0 ? 2.0 * ( 1.0 - l / v ) : 0.0;
    return FromRgb( FromHsv( { h, sv, v }, 1.0 ) );
}

CYPHER_NODISCARD bool_t IsThemeId( string_view_t id ) noexcept
{
    return DataValidation_Succeeded( DataValidation_CheckStableIdentifier( id, EDITOR_THEME_ID_MAX_LENGTH ) );
}

} // namespace

theme_status_t EditorThemeRegistry_Init( theme_registry_t *pRegistry, const allocator_t *pAllocator ) noexcept
{
    if ( pRegistry == nullptr || pAllocator == nullptr ) { return theme_status_t::INVALID_ARGUMENT; }
    return Vector_Init( &pRegistry->tokens, pAllocator ) ? theme_status_t::OK : theme_status_t::OUT_OF_MEMORY;
}

void EditorThemeRegistry_Shutdown( theme_registry_t *pRegistry ) noexcept
{
    if ( pRegistry != nullptr ) { Vector_Shutdown( &pRegistry->tokens ); }
}

theme_status_t EditorThemeRegistry_Register(
    theme_registry_t *pRegistry,
    const theme_token_t *pTokens,
    usize nTokens ) noexcept
{
    if ( pRegistry == nullptr || ( pTokens == nullptr && nTokens != 0u ) ) {
        return theme_status_t::INVALID_ARGUMENT;
    }
    // Validate the whole table first so registration is all or nothing.
    for ( usize iToken = 0u; iToken < nTokens; ++iToken ) {
        const string_view_t id = StringView_FromCString( pTokens[iToken].pId != nullptr ? pTokens[iToken].pId : "" );
        if ( !IsTokenId( id ) || !TokenDefaultValid( pTokens[iToken] ) || !DeriveValid( pTokens[iToken].derive ) ) {
            return theme_status_t::INVALID_ARGUMENT;
        }
        if ( EditorThemeRegistry_Find( pRegistry, id ) != nullptr ) {
            return theme_status_t::DUPLICATE_TOKEN;
        }
        for ( usize iEarlier = 0u; iEarlier < iToken; ++iEarlier ) {
            if ( CompareIds( id, StringView_FromCString( pTokens[iEarlier].pId ) ) == 0 ) {
                return theme_status_t::DUPLICATE_TOKEN;
            }
        }
    }
    const usize nBefore = Vector_Count( &pRegistry->tokens );
    if ( !Vector_Reserve( &pRegistry->tokens, nBefore + nTokens ) ) {
        return theme_status_t::OUT_OF_MEMORY;
    }
    for ( usize iToken = 0u; iToken < nTokens; ++iToken ) {
        // Insertion keeps the vector sorted; token tables are small and
        // registered once at start-up.
        const string_view_t id = StringView_FromCString( pTokens[iToken].pId );
        usize iInsert = Vector_Count( &pRegistry->tokens );
        while ( iInsert > 0u &&
                CompareIds( StringView_FromCString( pRegistry->tokens.pData[iInsert - 1u]->pId ), id ) > 0 ) {
            --iInsert;
        }
        CY_ASSERT( iInsert == Vector_Count( &pRegistry->tokens ) ||
                   CompareIds( StringView_FromCString( pRegistry->tokens.pData[iInsert]->pId ), id ) > 0 );
        if ( !Vector_Insert( &pRegistry->tokens, iInsert, &pTokens[iToken] ) ) {
            return theme_status_t::OUT_OF_MEMORY; // Unreachable after the reserve.
        }
    }
    return theme_status_t::OK;
}

const theme_token_t *EditorThemeRegistry_Find( const theme_registry_t *pRegistry, string_view_t id ) noexcept
{
    if ( pRegistry == nullptr ) { return nullptr; }
    usize iLow = 0u;
    usize iHigh = Vector_Count( &pRegistry->tokens );
    while ( iLow < iHigh ) {
        const usize iMid = iLow + ( iHigh - iLow ) / 2u;
        const i32 order = CompareIds( StringView_FromCString( pRegistry->tokens.pData[iMid]->pId ), id );
        if ( order == 0 ) { return pRegistry->tokens.pData[iMid]; }
        if ( order < 0 ) { iLow = iMid + 1u; } else { iHigh = iMid; }
    }
    return nullptr;
}

settings_document_identity_t EditorTheme_Identity() noexcept
{
    return { ThemeText( "cypher.theme" ), EDITOR_THEME_OLDEST_SCHEMA_VERSION, EDITOR_THEME_SCHEMA_VERSION };
}

theme_header_t EditorTheme_Header( const key_value_t *pThemeRoot ) noexcept
{
    theme_header_t header{};
    if ( KeyValue_Type( pThemeRoot ) != key_value_type_t::OBJECT ) { return header; }
    string_view_t text{};
    if ( KeyValue_GetString( KeyValue_Find( pThemeRoot, ThemeText( "id" ) ), &text ) && IsThemeId( text ) ) {
        header.id = text;
    }
    if ( KeyValue_GetString( KeyValue_Find( pThemeRoot, ThemeText( "name" ) ), &text ) && text.cchLength <= 128u ) {
        header.name = text;
    }
    if ( KeyValue_GetString( KeyValue_Find( pThemeRoot, ThemeText( "base" ) ), &text ) && IsThemeId( text ) ) {
        header.base = text;
    }
    if ( KeyValue_GetString( KeyValue_Find( pThemeRoot, ThemeText( "author" ) ), &text ) && text.cchLength <= 128u ) {
        header.author = text;
    }
    if ( KeyValue_GetString( KeyValue_Find( pThemeRoot, ThemeText( "description" ) ), &text ) && text.cchLength <= 1024u ) {
        header.description = text;
    }
    header.bLight = KeyValue_GetString( KeyValue_Find( pThemeRoot, ThemeText( "appearance" ) ), &text ) &&
                    StringView_Equals( text, ThemeText( "light" ) );
    return header;
}

theme_status_t EditorThemeLibrary_Init( theme_library_t *pLibrary, const allocator_t *pAllocator ) noexcept
{
    if ( pLibrary == nullptr || pAllocator == nullptr ) { return theme_status_t::INVALID_ARGUMENT; }
    return Vector_Init( &pLibrary->roots, pAllocator ) ? theme_status_t::OK : theme_status_t::OUT_OF_MEMORY;
}

void EditorThemeLibrary_Shutdown( theme_library_t *pLibrary ) noexcept
{
    if ( pLibrary != nullptr ) { Vector_Shutdown( &pLibrary->roots ); }
}

theme_status_t EditorThemeLibrary_Add( theme_library_t *pLibrary, const key_value_t *pThemeRoot ) noexcept
{
    if ( pLibrary == nullptr || EditorTheme_Header( pThemeRoot ).id.cchLength == 0u ) {
        return theme_status_t::INVALID_ARGUMENT;
    }
    return Vector_PushBack( &pLibrary->roots, pThemeRoot ) ? theme_status_t::OK : theme_status_t::OUT_OF_MEMORY;
}

usize EditorTheme_BuildChain(
    const theme_library_t *pLibrary,
    const key_value_t *pThemeRoot,
    const key_value_t **ppChainOut,
    usize nChainCapacity,
    bool_t *pbCompleteOut ) noexcept
{
    bool_t bComplete = CY_TRUE;
    usize nChain = 0u;
    const key_value_t *pCurrent = pThemeRoot;
    while ( pCurrent != nullptr && ppChainOut != nullptr ) {
        // A theme already in the chain means the bases form a cycle.
        for ( usize iSeen = 0u; iSeen < nChain; ++iSeen ) {
            if ( ppChainOut[iSeen] == pCurrent ) { bComplete = CY_FALSE; pCurrent = nullptr; break; }
        }
        if ( pCurrent == nullptr ) { break; }
        if ( nChain == nChainCapacity || nChain == EDITOR_THEME_MAX_BASE_DEPTH ) { bComplete = CY_FALSE; break; }
        ppChainOut[nChain++] = pCurrent;

        const string_view_t base = EditorTheme_Header( pCurrent ).base;
        pCurrent = nullptr;
        if ( base.cchLength == 0u ) { break; }
        for ( usize iTheme = 0u; pLibrary != nullptr && iTheme < Vector_Count( &pLibrary->roots ); ++iTheme ) {
            if ( StringView_Equals( EditorTheme_Header( pLibrary->roots.pData[iTheme] ).id, base ) ) {
                pCurrent = pLibrary->roots.pData[iTheme];
                break;
            }
        }
        if ( pCurrent == nullptr ) { bComplete = CY_FALSE; }
    }
    if ( pbCompleteOut != nullptr ) { *pbCompleteOut = bComplete; }
    return nChain;
}

u32 EditorTheme_ResolveColor(
    const key_value_t *const *ppChain,
    usize nChain,
    const theme_token_t &token,
    theme_resolution_t *pResolutionOut ) noexcept
{
    theme_resolution_t resolution{};
    u32 rgba = token.rgbaDefault;
    const string_view_t id = StringView_FromCString( token.pId );
    for ( usize iTheme = 0u; ppChain != nullptr && iTheme < nChain; ++iTheme ) {
        u32 value = 0u;
        const read_t read = ReadColor( FindEntry( ppChain[iTheme], theme_token_kind_t::COLOR, id ), value );
        if ( read == read_t::VALUE ) { rgba = value; resolution.iSource = iTheme; break; }
        if ( read == read_t::INVALID ) { ++resolution.nInvalidSkipped; }
    }
    if ( pResolutionOut != nullptr ) { *pResolutionOut = resolution; }
    return rgba;
}

theme_font_t EditorTheme_ResolveFont(
    const key_value_t *const *ppChain,
    usize nChain,
    const theme_token_t &token,
    theme_resolution_t *pResolutionOut ) noexcept
{
    theme_resolution_t resolution{};
    theme_font_t font{ StringView_FromCString( token.pFontFamily ), token.flFontSize, token.nFontWeight };
    const string_view_t id = StringView_FromCString( token.pId );
    for ( usize iField = 0u; iField < FONT_FIELD_COUNT; ++iField ) {
        for ( usize iTheme = 0u; ppChain != nullptr && iTheme < nChain; ++iTheme ) {
            const read_t read = ReadFontField(
                FindEntry( ppChain[iTheme], theme_token_kind_t::FONT, id ), static_cast<font_field_t>( iField ), font );
            if ( read == read_t::VALUE ) {
                // Report the most specific theme that supplied any field.
                if ( resolution.iSource == CY_INVALID_SIZE || iTheme < resolution.iSource ) { resolution.iSource = iTheme; }
                break;
            }
            if ( read == read_t::INVALID ) { ++resolution.nInvalidSkipped; }
        }
    }
    if ( pResolutionOut != nullptr ) { *pResolutionOut = resolution; }
    return font;
}

f64 EditorTheme_ResolveMetric(
    const key_value_t *const *ppChain,
    usize nChain,
    const theme_token_t &token,
    theme_resolution_t *pResolutionOut ) noexcept
{
    theme_resolution_t resolution{};
    f64 flValue = token.flDefault;
    const string_view_t id = StringView_FromCString( token.pId );
    for ( usize iTheme = 0u; ppChain != nullptr && iTheme < nChain; ++iTheme ) {
        f64 value = 0.0;
        const read_t read = ReadMetric( FindEntry( ppChain[iTheme], theme_token_kind_t::METRIC, id ), token, value );
        if ( read == read_t::VALUE ) { flValue = value; resolution.iSource = iTheme; break; }
        if ( read == read_t::INVALID ) { ++resolution.nInvalidSkipped; }
    }
    if ( pResolutionOut != nullptr ) { *pResolutionOut = resolution; }
    return flValue;
}

namespace
{

CYPHER_NODISCARD u32 ResolveDerived(
    const theme_registry_t *pRegistry,
    const key_value_t *const *ppChain,
    usize nChain,
    const theme_token_t &token,
    usize depth,
    theme_resolution_t &resolution ) noexcept;

// A formula operand: a colour literal, or another token resolved fully.
CYPHER_NODISCARD bool_t ResolveOperand(
    const theme_registry_t *pRegistry,
    const key_value_t *const *ppChain,
    usize nChain,
    const char *pOperand,
    usize depth,
    theme_resolution_t &resolution,
    u32 &rgbaOut ) noexcept
{
    const string_view_t operand = StringView_FromCString( pOperand );
    if ( operand.cchLength != 0u && operand.pData[0] == '#' ) { return SettingColor_Parse( operand, &rgbaOut ); }
    const theme_token_t *pToken = EditorThemeRegistry_Find( pRegistry, operand );
    if ( pToken == nullptr || pToken->kind != theme_token_kind_t::COLOR ) { return CY_FALSE; }
    // An invalid value in an operand is that token's problem and is counted
    // when that token resolves; counting it again for every formula that
    // uses it would inflate the theme editor's count.
    theme_resolution_t inner{};
    rgbaOut = ResolveDerived( pRegistry, ppChain, nChain, *pToken, depth + 1u, inner );
    ( void )resolution;
    return depth + 1u < EDITOR_THEME_MAX_DERIVE_DEPTH;
}

u32 ResolveDerived(
    const theme_registry_t *pRegistry,
    const key_value_t *const *ppChain,
    usize nChain,
    const theme_token_t &token,
    usize depth,
    theme_resolution_t &resolution ) noexcept
{
    const u32 own = EditorTheme_ResolveColor( ppChain, nChain, token, &resolution );
    if ( resolution.iSource != CY_INVALID_SIZE || token.derive.op == theme_derive_op_t::NONE ) { return own; }
    if ( depth >= EDITOR_THEME_MAX_DERIVE_DEPTH ) { return token.rgbaDefault; } // A cycle in the formulas.
    const theme_derive_t &derive = token.derive;
    u32 a = 0u;
    u32 b = 0u;
    if ( !ResolveOperand( pRegistry, ppChain, nChain, derive.pA, depth, resolution, a ) ) { return token.rgbaDefault; }
    switch ( derive.op ) {
        case theme_derive_op_t::NONE: break;
        case theme_derive_op_t::COPY: return a;
        case theme_derive_op_t::MIX:
            if ( !ResolveOperand( pRegistry, ppChain, nChain, derive.pB, depth, resolution, b ) ) { return token.rgbaDefault; }
            return Mix( a, b, derive.flAmount );
        case theme_derive_op_t::LIGHTER: return Lighter( a, derive.flAmount );
        case theme_derive_op_t::DARKER: return Darker( a, derive.flAmount );
        case theme_derive_op_t::MIX_STATUS: {
            // Status hues keep their meaning in every theme; saturation
            // follows the accent and lightness the background.
            u32 accent = 0u;
            u32 background = 0u;
            if ( !ResolveOperand( pRegistry, ppChain, nChain, "ui.accent", depth, resolution, accent ) ||
                 !ResolveOperand( pRegistry, ppChain, nChain, "ui.background", depth, resolution, background ) ) {
                return token.rgbaDefault;
            }
            const f64 accentSaturation = HslSaturation( ToRgb( accent ) );
            const f64 saturation = accentSaturation < 0.05 ? 0.70 : std::fmin( 0.90, std::fmax( 0.58, accentSaturation ) );
            const f64 lightness = HslLightness( ToRgb( background ) ) < 0.5 ? 0.68 : 0.36;
            return Mix( a, FromHsl( derive.flHue, saturation, lightness ), derive.flAmount );
        }
    }
    return token.rgbaDefault;
}

} // namespace

u32 EditorTheme_ResolveColorIn(
    const theme_registry_t *pRegistry,
    const key_value_t *const *ppChain,
    usize nChain,
    const theme_token_t &token,
    theme_resolution_t *pResolutionOut ) noexcept
{
    theme_resolution_t resolution{};
    const u32 rgba = ResolveDerived( pRegistry, ppChain, nChain, token, 0u, resolution );
    if ( pResolutionOut != nullptr ) { *pResolutionOut = resolution; }
    return rgba;
}

string_view_t EditorTheme_ResolveChoice(
    const key_value_t *const *ppChain,
    usize nChain,
    const theme_token_t &token,
    theme_resolution_t *pResolutionOut ) noexcept
{
    theme_resolution_t resolution{};
    string_view_t value = StringView_FromCString( token.pChoiceDefault != nullptr ? token.pChoiceDefault : "" );
    const string_view_t id = StringView_FromCString( token.pId );
    for ( usize iTheme = 0u; token.kind == theme_token_kind_t::CHOICE && ppChain != nullptr && iTheme < nChain; ++iTheme ) {
        string_view_t text{};
        const read_t read = ReadChoice( FindEntry( ppChain[iTheme], theme_token_kind_t::CHOICE, id ), token, text );
        if ( read == read_t::VALUE ) { value = text; resolution.iSource = iTheme; break; }
        if ( read == read_t::INVALID ) { ++resolution.nInvalidSkipped; }
    }
    if ( pResolutionOut != nullptr ) { *pResolutionOut = resolution; }
    return value;
}

theme_status_t EditorTheme_SetDetails(
    settings_document_t *pTheme,
    string_view_t author,
    string_view_t description,
    bool_t bLight ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pTheme ) || author.cchLength > 128u || description.cchLength > 1024u ) {
        return theme_status_t::INVALID_ARGUMENT;
    }
    const struct { const char *pKey; string_view_t value; } fields[]{
        { "author", author },
        { "description", description },
        { "appearance", bLight ? ThemeText( "light" ) : ThemeText( "dark" ) },
    };
    for ( const auto &field : fields ) {
        settings_path_t path{};
        if ( !SettingsPath_Append( &path, StringView_FromCString( field.pKey ) ) ) { return theme_status_t::INVALID_ARGUMENT; }
        if ( field.value.cchLength == 0u ) {
            const theme_status_t removed = FromStore( SettingsDocument_Remove( pTheme, path ) );
            if ( removed != theme_status_t::OK ) { return removed; }
            continue;
        }
        key_value_t *pNode = nullptr;
        const theme_status_t ensured = FromStore( SettingsDocument_Ensure( pTheme, path, &pNode ) );
        if ( ensured != theme_status_t::OK ) { return ensured; }
        if ( !KeyValue_SetString( pTheme->pDocument, pNode, field.value ) ) { return theme_status_t::OUT_OF_MEMORY; }
    }
    return theme_status_t::OK;
}

theme_status_t EditorTheme_SetColorAuto( settings_document_t *pTheme, const theme_token_t &token ) noexcept
{
    settings_path_t path{};
    if ( !SettingsDocument_IsInitialized( pTheme ) || token.kind != theme_token_kind_t::COLOR || !EntryPath( token.kind, token.pId, path ) ) {
        return theme_status_t::INVALID_ARGUMENT;
    }
    key_value_t *pNode = nullptr;
    const theme_status_t ensured = FromStore( SettingsDocument_Ensure( pTheme, path, &pNode ) );
    if ( ensured != theme_status_t::OK ) { return ensured; }
    return KeyValue_SetString( pTheme->pDocument, pNode, ThemeText( "auto" ) ) ? theme_status_t::OK : theme_status_t::OUT_OF_MEMORY;
}

theme_status_t EditorTheme_SetChoice(
    settings_document_t *pTheme,
    const theme_token_t &token,
    string_view_t value,
    string_view_t inherited ) noexcept
{
    settings_path_t path{};
    if ( !SettingsDocument_IsInitialized( pTheme ) || token.kind != theme_token_kind_t::CHOICE || !EntryPath( token.kind, token.pId, path ) ) {
        return theme_status_t::INVALID_ARGUMENT;
    }
    bool_t bListed = CY_FALSE;
    for ( usize i = 0u; i < token.nChoices && !bListed; ++i ) { bListed = StringView_Equals( value, StringView_FromCString( token.ppChoices[i] ) ); }
    if ( !bListed ) { return theme_status_t::INVALID_ARGUMENT; }
    if ( StringView_Equals( value, inherited ) ) { return FromStore( SettingsDocument_Remove( pTheme, path ) ); }
    key_value_t *pNode = nullptr;
    const theme_status_t ensured = FromStore( SettingsDocument_Ensure( pTheme, path, &pNode ) );
    if ( ensured != theme_status_t::OK ) { return ensured; }
    return KeyValue_SetString( pTheme->pDocument, pNode, value ) ? theme_status_t::OK : theme_status_t::OUT_OF_MEMORY;
}

theme_status_t EditorTheme_SetHeader(
    settings_document_t *pTheme,
    string_view_t id,
    string_view_t name,
    string_view_t base ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pTheme ) || !IsThemeId( id ) || name.cchLength == 0u ||
         name.cchLength > 128u || ( base.cchLength != 0u && !IsThemeId( base ) ) ) {
        return theme_status_t::INVALID_ARGUMENT;
    }
    const struct { const char *pKey; string_view_t value; } fields[]{
        { "id", id }, { "name", name }, { "base", base }
    };
    for ( const auto &field : fields ) {
        settings_path_t path{};
        if ( !SettingsPath_Append( &path, StringView_FromCString( field.pKey ) ) ) { return theme_status_t::INVALID_ARGUMENT; }
        if ( field.value.cchLength == 0u ) {
            const theme_status_t removed = FromStore( SettingsDocument_Remove( pTheme, path ) );
            if ( removed != theme_status_t::OK ) { return removed; }
            continue;
        }
        key_value_t *pNode = nullptr;
        const theme_status_t ensured = FromStore( SettingsDocument_Ensure( pTheme, path, &pNode ) );
        if ( ensured != theme_status_t::OK ) { return ensured; }
        if ( !KeyValue_SetString( pTheme->pDocument, pNode, field.value ) ) { return theme_status_t::OUT_OF_MEMORY; }
    }
    return theme_status_t::OK;
}

theme_status_t EditorTheme_SetColor(
    settings_document_t *pTheme,
    const theme_token_t &token,
    u32 rgba,
    u32 inheritedRgba ) noexcept
{
    settings_path_t path{};
    if ( !SettingsDocument_IsInitialized( pTheme ) || token.kind != theme_token_kind_t::COLOR ||
         !EntryPath( token.kind, token.pId, path ) ) {
        return theme_status_t::INVALID_ARGUMENT;
    }
    if ( rgba == inheritedRgba ) {
        return FromStore( SettingsDocument_Remove( pTheme, path ) );
    }
    key_value_t *pNode = nullptr;
    const theme_status_t ensured = FromStore( SettingsDocument_Ensure( pTheme, path, &pNode ) );
    if ( ensured != theme_status_t::OK ) { return ensured; }
    char text[10]{};
    const usize cch = SettingColor_Format( rgba, text );
    return KeyValue_SetString( pTheme->pDocument, pNode, { text, cch } ) ? theme_status_t::OK : theme_status_t::OUT_OF_MEMORY;
}

theme_status_t EditorTheme_SetFont(
    settings_document_t *pTheme,
    const theme_token_t &token,
    const theme_font_t &font,
    const theme_font_t &inherited ) noexcept
{
    settings_path_t path{};
    if ( !SettingsDocument_IsInitialized( pTheme ) || token.kind != theme_token_kind_t::FONT ||
         !EntryPath( token.kind, token.pId, path ) || font.family.cchLength == 0u ||
         font.family.cchLength > kFontFamilyMax || !( font.flSize >= kFontSizeMin && font.flSize <= kFontSizeMax ) ||
         font.nWeight < 1u || font.nWeight > 1000u ) {
        return theme_status_t::INVALID_ARGUMENT;
    }
    // Only fields that differ from the inherited font are stored.
    const bool_t bFamily = !StringView_Equals( font.family, inherited.family );
    const bool_t bSize = font.flSize != inherited.flSize;
    const bool_t bWeight = font.nWeight != inherited.nWeight;
    const theme_status_t removed = FromStore( SettingsDocument_Remove( pTheme, path ) );
    if ( removed != theme_status_t::OK || !( bFamily || bSize || bWeight ) ) { return removed; }

    key_value_t *pNode = nullptr;
    const theme_status_t ensured = FromStore( SettingsDocument_Ensure( pTheme, path, &pNode ) );
    if ( ensured != theme_status_t::OK ) { return ensured; }
    key_value_document_t *pDocument = pTheme->pDocument;
    if ( !KeyValue_SetContainerType( pDocument, pNode, key_value_type_t::OBJECT ) ) { return theme_status_t::OUT_OF_MEMORY; }
    key_value_t *pField = nullptr;
    if ( bFamily && ( ( pField = KeyValue_ObjectInsert( pDocument, pNode, ThemeText( "family" ), key_value_type_t::NULL_VALUE ) ) == nullptr ||
                      !KeyValue_SetString( pDocument, pField, font.family ) ) ) {
        return theme_status_t::OUT_OF_MEMORY;
    }
    if ( bSize && ( ( pField = KeyValue_ObjectInsert( pDocument, pNode, ThemeText( "size" ), key_value_type_t::NULL_VALUE ) ) == nullptr ||
                    !KeyValue_SetF64( pDocument, pField, font.flSize ) ) ) {
        return theme_status_t::OUT_OF_MEMORY;
    }
    if ( bWeight && ( ( pField = KeyValue_ObjectInsert( pDocument, pNode, ThemeText( "weight" ), key_value_type_t::NULL_VALUE ) ) == nullptr ||
                      !KeyValue_SetI64( pDocument, pField, static_cast<i64>( font.nWeight ) ) ) ) {
        return theme_status_t::OUT_OF_MEMORY;
    }
    return theme_status_t::OK;
}

theme_status_t EditorTheme_SetMetric(
    settings_document_t *pTheme,
    const theme_token_t &token,
    f64 flValue,
    f64 flInherited ) noexcept
{
    settings_path_t path{};
    if ( !SettingsDocument_IsInitialized( pTheme ) || token.kind != theme_token_kind_t::METRIC ||
         !EntryPath( token.kind, token.pId, path ) || !std::isfinite( flValue ) ||
         flValue < token.flMin || flValue > token.flMax ) {
        return theme_status_t::INVALID_ARGUMENT;
    }
    if ( flValue == flInherited ) {
        return FromStore( SettingsDocument_Remove( pTheme, path ) );
    }
    key_value_t *pNode = nullptr;
    const theme_status_t ensured = FromStore( SettingsDocument_Ensure( pTheme, path, &pNode ) );
    if ( ensured != theme_status_t::OK ) { return ensured; }
    return KeyValue_SetF64( pTheme->pDocument, pNode, flValue ) ? theme_status_t::OK : theme_status_t::OUT_OF_MEMORY;
}

theme_status_t EditorTheme_WriteComplete(
    settings_document_t *pTheme,
    const theme_registry_t *pRegistry,
    const theme_token_t *const *ppTokens,
    usize nTokens,
    const key_value_t *const *ppChain,
    usize nChain ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pTheme ) || ( ppTokens == nullptr && nTokens != 0u ) ) { return theme_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0u; i < nTokens; ++i ) {
        const theme_token_t &token = *ppTokens[i];
        theme_status_t status = theme_status_t::OK;
        switch ( token.kind ) {
            case theme_token_kind_t::COLOR: {
                theme_resolution_t own{};
                ( void )EditorTheme_ResolveColor( ppChain, nChain, token, &own );
                if ( own.iSource == CY_INVALID_SIZE && token.derive.op != theme_derive_op_t::NONE ) {
                    status = EditorTheme_SetColorAuto( pTheme, token );
                } else {
                    const u32 rgba = EditorTheme_ResolveColorIn( pRegistry, ppChain, nChain, token, nullptr );
                    status = EditorTheme_SetColor( pTheme, token, rgba, ~rgba ); // A different "inherited" value forces the write.
                }
                break;
            }
            case theme_token_kind_t::FONT: {
                const theme_font_t font = EditorTheme_ResolveFont( ppChain, nChain, token, nullptr );
                status = EditorTheme_SetFont( pTheme, token, font, theme_font_t{ {}, 0.0, 0u } );
                break;
            }
            case theme_token_kind_t::METRIC: {
                const f64 value = EditorTheme_ResolveMetric( ppChain, nChain, token, nullptr );
                // Nothing compares equal to NaN, so the value is always stored.
                status = EditorTheme_SetMetric( pTheme, token, value, std::nan( "" ) );
                break;
            }
            case theme_token_kind_t::CHOICE:
                status = EditorTheme_SetChoice( pTheme, token, EditorTheme_ResolveChoice( ppChain, nChain, token, nullptr ), string_view_t{} );
                break;
        }
        if ( status != theme_status_t::OK ) { return status; }
    }
    return theme_status_t::OK;
}

theme_status_t EditorTheme_KeepUnknownTokens(
    settings_document_t *pTheme,
    const theme_registry_t *pRegistry,
    const key_value_t *const *ppChain,
    usize nChain ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pTheme ) || pRegistry == nullptr || ( ppChain == nullptr && nChain != 0u ) ) {
        return theme_status_t::INVALID_ARGUMENT;
    }
    key_value_t *pRoot = KeyValue_Root( pTheme->pDocument );
    constexpr theme_token_kind_t kKinds[]{ theme_token_kind_t::COLOR, theme_token_kind_t::FONT, theme_token_kind_t::METRIC,
                                           theme_token_kind_t::CHOICE };
    for ( const theme_token_kind_t kind : kKinds ) {
        for ( usize iTheme = 0u; iTheme < nChain; ++iTheme ) {
            const key_value_t *pSection = KeyValue_Find( ppChain[iTheme], SectionName( kind ) );
            if ( KeyValue_Type( pSection ) != key_value_type_t::OBJECT ) { continue; }
            for ( usize iEntry = 0u; iEntry < KeyValue_ChildCount( pSection ); ++iEntry ) {
                const key_value_t *pEntry = KeyValue_ChildAt( pSection, iEntry );
                const string_view_t id = KeyValue_Name( pEntry );
                // Registered tokens are WriteComplete's; a registered token in
                // the wrong section never resolves, so it is not carried.
                if ( EditorThemeRegistry_Find( pRegistry, id ) != nullptr ) { continue; }
                key_value_t *pDest = KeyValue_Find( pRoot, SectionName( kind ) );
                if ( pDest == nullptr ) {
                    pDest = KeyValue_ObjectInsert( pTheme->pDocument, pRoot, SectionName( kind ), key_value_type_t::OBJECT );
                    if ( pDest == nullptr ) { return theme_status_t::OUT_OF_MEMORY; }
                }
                if ( KeyValue_Type( pDest ) != key_value_type_t::OBJECT ) { return theme_status_t::STORE_FAILED; }
                if ( KeyValue_Find( pDest, id ) != nullptr ) { continue; } // A more specific theme gave it already.
                if ( KeyValue_CloneInto( pTheme->pDocument, pDest, pEntry ) == nullptr ) { return theme_status_t::OUT_OF_MEMORY; }
            }
        }
    }
    return theme_status_t::OK;
}

theme_status_t EditorTheme_Reset( settings_document_t *pTheme, const theme_token_t &token ) noexcept
{
    settings_path_t path{};
    if ( !SettingsDocument_IsInitialized( pTheme ) || !EntryPath( token.kind, token.pId, path ) ) {
        return theme_status_t::INVALID_ARGUMENT;
    }
    return FromStore( SettingsDocument_Remove( pTheme, path ) );
}

usize EditorTheme_Audit(
    const theme_registry_t *pRegistry,
    const key_value_t *pThemeRoot,
    theme_problem_t *pProblems,
    usize nCapacity ) noexcept
{
    usize nProblems = 0u;
    const auto add = [&]( theme_problem_code_t code, string_view_t id ) noexcept {
        if ( pProblems != nullptr && nProblems < nCapacity ) { pProblems[nProblems] = { code, id }; }
        ++nProblems;
    };
    constexpr theme_token_kind_t kKinds[]{ theme_token_kind_t::COLOR, theme_token_kind_t::FONT, theme_token_kind_t::METRIC,
                                           theme_token_kind_t::CHOICE };
    for ( const theme_token_kind_t kind : kKinds ) {
        const key_value_t *pSection = KeyValue_Find( pThemeRoot, SectionName( kind ) );
        if ( KeyValue_Type( pSection ) != key_value_type_t::OBJECT ) { continue; }
        for ( usize iEntry = 0u; iEntry < KeyValue_ChildCount( pSection ); ++iEntry ) {
            const key_value_t *pEntry = KeyValue_ChildAt( pSection, iEntry );
            const string_view_t id = KeyValue_Name( pEntry );
            const theme_token_t *pToken = EditorThemeRegistry_Find( pRegistry, id );
            if ( pToken == nullptr ) { add( theme_problem_code_t::UNKNOWN_TOKEN, id ); continue; }
            if ( pToken->kind != kind ) { add( theme_problem_code_t::WRONG_KIND, id ); continue; }
            bool_t bValid = CY_TRUE;
            if ( kind == theme_token_kind_t::COLOR ) {
                u32 rgba = 0u;
                bValid = ReadColor( pEntry, rgba ) != read_t::INVALID; // "auto" is valid.
            } else if ( kind == theme_token_kind_t::CHOICE ) {
                string_view_t text{};
                bValid = ReadChoice( pEntry, *pToken, text ) == read_t::VALUE;
            } else if ( kind == theme_token_kind_t::METRIC ) {
                f64 flValue = 0.0;
                bValid = ReadMetric( pEntry, *pToken, flValue ) == read_t::VALUE;
            } else {
                theme_font_t font{};
                for ( usize iField = 0u; iField < FONT_FIELD_COUNT && bValid; ++iField ) {
                    bValid = ReadFontField( pEntry, static_cast<font_field_t>( iField ), font ) != read_t::INVALID;
                }
            }
            if ( !bValid ) { add( theme_problem_code_t::INVALID_VALUE, id ); }
        }
    }
    return nProblems;
}

const char *EditorTheme_StatusName( theme_status_t status ) noexcept
{
    switch ( status ) {
        case theme_status_t::OK: return "OK";
        case theme_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case theme_status_t::DUPLICATE_TOKEN: return "DUPLICATE_TOKEN";
        case theme_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case theme_status_t::STORE_FAILED: return "STORE_FAILED";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor
