//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ConVar.cpp
//  Purpose: Implements typed console-variable parsing and validation.
//  Details: Values are parsed without allocation or locale state. Descriptors are
//           rejected unless defaults and optional numeric bounds form one coherent
//           typed contract.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ConVar.h"

#include "CypherCommon_StringView.h"

#include <charconv>
#include <limits>

namespace cypher::common
{

namespace
{

CYPHER_NODISCARD bool_t IsValidConVarType( convar_type_t type ) noexcept
{
    return static_cast<u8>( type ) <= static_cast<u8>( convar_type_t::STRING );
}

CYPHER_NODISCARD bool_t HasEmbeddedNull( string_view_t text ) noexcept
{
    if ( !StringView_IsValid( text ) ) {
        return CY_TRUE;
    }
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        if ( text.pData[iByte] == '\0' ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

CYPHER_NODISCARD string_parse_result_t ScalarSuccess(
    usize cchConsumed ) noexcept
{
    return {
        string_parse_status_t::OK,
        cchConsumed,
        CY_STRING_VIEW_NPOS
    };
}

CYPHER_NODISCARD convar_parse_result_t ConVarResult(
    convar_parse_status_t status,
    string_parse_result_t scalarResult = {} ) noexcept
{
    return { status, scalarResult };
}

CYPHER_NODISCARD i32 CompareValues(
    convar_type_t type,
    const convar_value_t &left,
    const convar_value_t &right ) noexcept
{
    switch ( type ) {
        case convar_type_t::I64:
            if ( left.value.data.iValue < right.value.data.iValue ) return -1;
            if ( left.value.data.iValue > right.value.data.iValue ) return 1;
            return 0;
        case convar_type_t::U64:
            if ( left.value.data.uValue < right.value.data.uValue ) return -1;
            if ( left.value.data.uValue > right.value.data.uValue ) return 1;
            return 0;
        case convar_type_t::F64:
            if ( left.value.data.flValue < right.value.data.flValue ) return -1;
            if ( left.value.data.flValue > right.value.data.flValue ) return 1;
            return 0;
        case convar_type_t::BOOL:
        case convar_type_t::STRING:
            return 0;
    }
    return 0;
}

CYPHER_NODISCARD bool_t ParseDescriptorBound(
    convar_type_t type,
    string_view_t text,
    convar_value_t *pValueOut ) noexcept
{
    return ConVar_ParseSucceeded( ConVar_ParseValue( type, text, pValueOut ) );
}

template <typename integer_t>
CYPHER_NODISCARD usize FormatInteger(
    integer_t value,
    char *pDest,
    usize cchDest ) noexcept
{
    char scratch[32]{};
    // max_digits10 guarantees that parsing the formatted value reproduces the
    // same finite binary64 value on every supported host.
    const std::to_chars_result format = std::to_chars(
        scratch,
        scratch + sizeof( scratch ),
        value );
    if ( format.ec != std::errc{} ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return 0u;
    }

    return StringView_CopyToCString(
        StringView_FromRange(
            scratch,
            static_cast<usize>( format.ptr - scratch ) ),
        pDest,
        cchDest );
}

CYPHER_NODISCARD usize FormatFloat(
    f64 value,
    char *pDest,
    usize cchDest ) noexcept
{
    char scratch[128]{};
    const std::to_chars_result format = std::to_chars(
        scratch,
        scratch + sizeof( scratch ),
        value,
        std::chars_format::general,
        std::numeric_limits<f64>::max_digits10 );
    if ( format.ec != std::errc{} ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return 0u;
    }

    return StringView_CopyToCString(
        StringView_FromRange(
            scratch,
            static_cast<usize>( format.ptr - scratch ) ),
        pDest,
        cchDest );
}

} // namespace

bool_t ConVar_IsValidName( string_view_t name ) noexcept
{
    return ConCommand_IsValidName( name );
}

bool_t ConVar_ValidateDesc( const convar_desc_t &desc ) noexcept
{
    if ( !ConVar_IsValidName( desc.name ) ||
         !IsValidConVarType( desc.type ) ||
         !StringView_IsValid( desc.help ) ||
         !StringView_IsValid( desc.defaultValue ) ||
         !StringView_IsValid( desc.minValue ) ||
         !StringView_IsValid( desc.maxValue ) ||
         HasEmbeddedNull( desc.help ) ||
         HasEmbeddedNull( desc.defaultValue ) ||
         HasEmbeddedNull( desc.minValue ) ||
         HasEmbeddedNull( desc.maxValue ) ||
         ( desc.flags & ~CONVAR_VALID_FLAGS ) != 0u ||
         ( ( desc.flags & CONVAR_FLAG_READ_ONLY ) != 0u &&
           ( desc.flags & CONVAR_FLAG_REMOTE_WRITE_ALLOWED ) != 0u ) ) {
        return CY_FALSE;
    }

    const bool_t bHasMinimum = !StringView_IsEmpty( desc.minValue );
    const bool_t bHasMaximum = !StringView_IsEmpty( desc.maxValue );
    if ( ( desc.type == convar_type_t::BOOL ||
           desc.type == convar_type_t::STRING ) &&
         ( bHasMinimum || bHasMaximum ) ) {
        return CY_FALSE;
    }

    // Parse defaults and bounds through the same runtime parser. A descriptor
    // cannot declare values that its own setter would later reject.
    convar_value_t defaultValue{};
    if ( !ConVar_ParseSucceeded(
             ConVar_ParseValue( desc.type, desc.defaultValue, &defaultValue ) ) ) {
        return CY_FALSE;
    }

    convar_value_t minimum{};
    convar_value_t maximum{};
    if ( bHasMinimum &&
         !ParseDescriptorBound( desc.type, desc.minValue, &minimum ) ) {
        return CY_FALSE;
    }
    if ( bHasMaximum &&
         !ParseDescriptorBound( desc.type, desc.maxValue, &maximum ) ) {
        return CY_FALSE;
    }
    if ( bHasMinimum && bHasMaximum &&
         CompareValues( desc.type, minimum, maximum ) > 0 ) {
        return CY_FALSE;
    }
    if ( bHasMinimum && CompareValues( desc.type, defaultValue, minimum ) < 0 ) {
        return CY_FALSE;
    }
    if ( bHasMaximum && CompareValues( desc.type, defaultValue, maximum ) > 0 ) {
        return CY_FALSE;
    }
    return CY_TRUE;
}

bool_t ConVar_ParseSucceeded( convar_parse_result_t result ) noexcept
{
    return result.status == convar_parse_status_t::OK;
}

const char *ConVar_ParseStatusName( convar_parse_status_t status ) noexcept
{
    switch ( status ) {
        case convar_parse_status_t::OK:               return "OK";
        case convar_parse_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case convar_parse_status_t::INVALID_TYPE:     return "INVALID_TYPE";
        case convar_parse_status_t::EMBEDDED_NULL:    return "EMBEDDED_NULL";
        case convar_parse_status_t::INVALID_VALUE:    return "INVALID_VALUE";
        case convar_parse_status_t::BELOW_MINIMUM:    return "BELOW_MINIMUM";
        case convar_parse_status_t::ABOVE_MAXIMUM:    return "ABOVE_MAXIMUM";
    }
    return "UNKNOWN_CONVAR_PARSE_STATUS";
}

convar_parse_result_t ConVar_ParseValue(
    convar_type_t type,
    string_view_t text,
    convar_value_t *pValueOut ) noexcept
{
    if ( pValueOut == nullptr || !StringView_IsValid( text ) ) {
        return ConVarResult( convar_parse_status_t::INVALID_ARGUMENT );
    }
    if ( !IsValidConVarType( type ) ) {
        return ConVarResult( convar_parse_status_t::INVALID_TYPE );
    }
    if ( HasEmbeddedNull( text ) ) {
        return ConVarResult( convar_parse_status_t::EMBEDDED_NULL );
    }

    // Keep parsing transactional: pValueOut changes only after type-specific
    // syntax and conversion have both succeeded.
    convar_value_t parsed{};
    string_parse_result_t scalarResult{};
    switch ( type ) {
        case convar_type_t::BOOL: {
            bool_t value = CY_FALSE;
            scalarResult = StringParse_Bool(
                text,
                STRING_PARSE_FLAG_TRIM_WHITESPACE |
                    STRING_PARSE_FLAG_CASE_INSENSITIVE_BOOL |
                    STRING_PARSE_FLAG_ALLOW_NUMERIC_BOOL,
                &value );
            if ( !StringParse_Succeeded( scalarResult ) ) {
                return ConVarResult(
                    convar_parse_status_t::INVALID_VALUE,
                    scalarResult );
            }
            parsed.value = Variant_FromBool( value );
            break;
        }
        case convar_type_t::I64: {
            i64 value = 0;
            const string_parse_options_t options{
                0u,
                STRING_PARSE_FLAG_TRIM_WHITESPACE |
                    STRING_PARSE_FLAG_ALLOW_PLUS_SIGN |
                    STRING_PARSE_FLAG_ALLOW_BASE_PREFIX |
                    STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR
            };
            scalarResult = StringParse_I64( text, options, &value );
            if ( !StringParse_Succeeded( scalarResult ) ) {
                return ConVarResult(
                    convar_parse_status_t::INVALID_VALUE,
                    scalarResult );
            }
            parsed.value = Variant_FromI64( value );
            break;
        }
        case convar_type_t::U64: {
            u64 value = 0u;
            const string_parse_options_t options{
                0u,
                STRING_PARSE_FLAG_TRIM_WHITESPACE |
                    STRING_PARSE_FLAG_ALLOW_PLUS_SIGN |
                    STRING_PARSE_FLAG_ALLOW_BASE_PREFIX |
                    STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR
            };
            scalarResult = StringParse_U64( text, options, &value );
            if ( !StringParse_Succeeded( scalarResult ) ) {
                return ConVarResult(
                    convar_parse_status_t::INVALID_VALUE,
                    scalarResult );
            }
            parsed.value = Variant_FromU64( value );
            break;
        }
        case convar_type_t::F64: {
            f64 value = 0.0;
            scalarResult = StringParse_F64(
                text,
                STRING_PARSE_FLAG_TRIM_WHITESPACE |
                    STRING_PARSE_FLAG_ALLOW_PLUS_SIGN,
                &value );
            if ( !StringParse_Succeeded( scalarResult ) ) {
                return ConVarResult(
                    convar_parse_status_t::INVALID_VALUE,
                    scalarResult );
            }
            parsed.value = Variant_FromF64( value );
            break;
        }
        case convar_type_t::STRING:
            scalarResult = ScalarSuccess( text.cchLength );
            // String convars borrow the supplied bytes; the registry layer must
            // copy them when it requires persistent ownership.
            parsed.value = Variant_FromString( text );
            break;
    }

    *pValueOut = parsed;
    return ConVarResult( convar_parse_status_t::OK, scalarResult );
}

convar_parse_result_t ConVar_ParseValueForDesc(
    const convar_desc_t &desc,
    string_view_t text,
    convar_value_t *pValueOut ) noexcept
{
    if ( pValueOut == nullptr || !ConVar_ValidateDesc( desc ) ) {
        return ConVarResult( convar_parse_status_t::INVALID_ARGUMENT );
    }

    convar_value_t parsed{};
    const convar_parse_result_t parseResult =
        ConVar_ParseValue( desc.type, text, &parsed );
    if ( !ConVar_ParseSucceeded( parseResult ) ) {
        return parseResult;
    }

    // Bounds are inclusive and apply only after a value has parsed to the
    // descriptor's declared scalar type.
    if ( !StringView_IsEmpty( desc.minValue ) ) {
        convar_value_t minimum{};
        if ( !ParseDescriptorBound( desc.type, desc.minValue, &minimum ) ) {
            return ConVarResult( convar_parse_status_t::INVALID_ARGUMENT );
        }
        if ( CompareValues( desc.type, parsed, minimum ) < 0 ) {
            return ConVarResult(
                convar_parse_status_t::BELOW_MINIMUM,
                parseResult.scalarResult );
        }
    }

    if ( !StringView_IsEmpty( desc.maxValue ) ) {
        convar_value_t maximum{};
        if ( !ParseDescriptorBound( desc.type, desc.maxValue, &maximum ) ) {
            return ConVarResult( convar_parse_status_t::INVALID_ARGUMENT );
        }
        if ( CompareValues( desc.type, parsed, maximum ) > 0 ) {
            return ConVarResult(
                convar_parse_status_t::ABOVE_MAXIMUM,
                parseResult.scalarResult );
        }
    }

    *pValueOut = parsed;
    return parseResult;
}

bool_t ConVar_ValueMatchesType(
    convar_type_t type,
    const convar_value_t &value ) noexcept
{
    if ( !IsValidConVarType( type ) || !Variant_IsValid( value.value ) ) {
        return CY_FALSE;
    }

    switch ( type ) {
        case convar_type_t::BOOL:
            return value.value.type == variant_type_t::BOOL;
        case convar_type_t::I64:
            return value.value.type == variant_type_t::I64;
        case convar_type_t::U64:
            return value.value.type == variant_type_t::U64;
        case convar_type_t::F64:
            return value.value.type == variant_type_t::F64;
        case convar_type_t::STRING:
            return value.value.type == variant_type_t::STRING_VIEW;
    }
    return CY_FALSE;
}

usize ConVar_FormatValue(
    const convar_value_t &value,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( pDest == nullptr && cchDest > 0u ) {
        return 0u;
    }
    if ( pDest != nullptr && cchDest > 0u ) {
        pDest[0] = '\0';
    }
    if ( !Variant_IsValid( value.value ) ) {
        return 0u;
    }

    switch ( value.value.type ) {
        case variant_type_t::BOOL:
            return StringView_CopyToCString(
                StringView_FromCString(
                    value.value.data.bValue ? "true" : "false" ),
                pDest,
                cchDest );
        case variant_type_t::I64:
            return FormatInteger( value.value.data.iValue, pDest, cchDest );
        case variant_type_t::U64:
            return FormatInteger( value.value.data.uValue, pDest, cchDest );
        case variant_type_t::F64:
            return FormatFloat( value.value.data.flValue, pDest, cchDest );
        case variant_type_t::STRING_VIEW:
            return StringView_CopyToCString(
                {
                    value.value.data.stringValue.pData,
                    value.value.data.stringValue.cchLength
                },
                pDest,
                cchDest );
        case variant_type_t::EMPTY:
        case variant_type_t::BYTE_VIEW:
        case variant_type_t::POINTER:
            return 0u;
    }
    return 0u;
}

} // namespace cypher::common
