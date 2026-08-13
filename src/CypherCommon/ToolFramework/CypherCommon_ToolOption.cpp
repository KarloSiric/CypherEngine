//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolOption.cpp
//  Purpose: Implements typed tool option descriptor and value validation.
//  Details: Parsing checks complete scalar consumption so malformed overrides fail
//           before compiler behavior or cache identity can become ambiguous.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolOption.h"

#include "CypherCommon_StringParse.h"

namespace cypher::common
{
namespace
{

bool IsOptionName( string_view_t name ) noexcept
{
    if ( !StringView_IsValid( name ) || name.cchLength == 0u ) {
        return false;
    }
    for ( usize i = 0u; i < name.cchLength; ++i ) {
        const char value = name.pData[i];
        const bool_t bLetter = value >= 'a' && value <= 'z';
        const bool_t bDigit = value >= '0' && value <= '9';
        if ( !bLetter && !bDigit && value != '-' ) {
            return false;
        }
    }
    return name.pData[0] != '-' && name.pData[name.cchLength - 1u] != '-';
}

bool IsShortName( char value ) noexcept
{
    return value == '\0' ||
           ( value >= 'a' && value <= 'z' ) ||
           ( value >= 'A' && value <= 'Z' );
}

bool IsBoolean( string_view_t value ) noexcept
{
    return StringView_Equals( value, StringView_FromCString( "true" ) ) ||
           StringView_Equals( value, StringView_FromCString( "false" ) ) ||
           StringView_Equals( value, StringView_FromCString( "1" ) ) ||
           StringView_Equals( value, StringView_FromCString( "0" ) );
}

} // namespace

tool_status_t ToolOption_CheckDescriptor(
    const tool_option_desc_t &desc ) noexcept
{
    constexpr flags32_t knownFlags =
        TOOL_OPTION_FLAG_REQUIRED |
        TOOL_OPTION_FLAG_REPEATABLE |
        TOOL_OPTION_FLAG_HIDDEN |
        TOOL_OPTION_FLAG_SEMANTIC |
        TOOL_OPTION_FLAG_ALLOW_EMPTY;

    if ( !IsOptionName( desc.name ) ||
         !IsShortName( desc.shortName ) ||
         desc.type > tool_option_type_t::ENUM ||
         !StringView_IsValid( desc.valueName ) ||
         !StringView_IsValid( desc.summary ) ||
         desc.summary.cchLength == 0u ||
         !StringView_IsValid( desc.defaultValue ) ||
         ( desc.flags & ~knownFlags ) != 0u ||
         ( desc.nEnumValues != 0u && desc.pEnumValues == nullptr ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    if ( desc.type == tool_option_type_t::BOOLEAN ) {
        if ( desc.valueName.cchLength != 0u ||
             ( desc.flags & TOOL_OPTION_FLAG_REPEATABLE ) != 0u ) {
            return tool_status_t::INVALID_CONFIGURATION;
        }
    } else if ( desc.valueName.cchLength == 0u ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }

    if ( desc.type == tool_option_type_t::ENUM ) {
        if ( desc.nEnumValues == 0u ) {
            return tool_status_t::INVALID_CONFIGURATION;
        }
        for ( usize i = 0u; i < desc.nEnumValues; ++i ) {
            if ( !StringView_IsValid( desc.pEnumValues[i] ) ||
                 desc.pEnumValues[i].cchLength == 0u ) {
                return tool_status_t::INVALID_CONFIGURATION;
            }
            for ( usize j = 0u; j < i; ++j ) {
                if ( StringView_Equals(
                         desc.pEnumValues[i],
                         desc.pEnumValues[j] ) ) {
                    return tool_status_t::INVALID_CONFIGURATION;
                }
            }
        }
    } else if ( desc.nEnumValues != 0u ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }

    if ( desc.defaultValue.cchLength != 0u ) {
        return ToolOption_ValidateValue( desc, desc.defaultValue );
    }
    if ( ( desc.flags & TOOL_OPTION_FLAG_REQUIRED ) != 0u &&
         ( desc.flags & TOOL_OPTION_FLAG_ALLOW_EMPTY ) == 0u ) {
        return tool_status_t::OK;
    }
    return tool_status_t::OK;
}

tool_status_t ToolOption_ValidateValue(
    const tool_option_desc_t &desc,
    string_view_t value ) noexcept
{
    if ( !StringView_IsValid( value ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( value.cchLength == 0u &&
         ( desc.flags & TOOL_OPTION_FLAG_ALLOW_EMPTY ) == 0u ) {
        return tool_status_t::INVALID_OPTION;
    }

    switch ( desc.type ) {
        case tool_option_type_t::BOOLEAN:
            return IsBoolean( value )
                ? tool_status_t::OK
                : tool_status_t::INVALID_OPTION;
        case tool_option_type_t::I64: {
            i64 parsed = 0;
            const string_parse_result_t result = StringParse_I64(
                value,
                { 10u, STRING_PARSE_FLAG_NONE },
                &parsed );
            return result.status == string_parse_status_t::OK &&
                   result.cchConsumed == value.cchLength
                ? tool_status_t::OK
                : tool_status_t::INVALID_OPTION;
        }
        case tool_option_type_t::U64: {
            u64 parsed = 0u;
            const string_parse_result_t result = StringParse_U64(
                value,
                { 10u, STRING_PARSE_FLAG_NONE },
                &parsed );
            return result.status == string_parse_status_t::OK &&
                   result.cchConsumed == value.cchLength
                ? tool_status_t::OK
                : tool_status_t::INVALID_OPTION;
        }
        case tool_option_type_t::F64: {
            f64 parsed = 0.0;
            const string_parse_result_t result = StringParse_F64(
                value,
                STRING_PARSE_FLAG_NONE,
                &parsed );
            return result.status == string_parse_status_t::OK &&
                   result.cchConsumed == value.cchLength
                ? tool_status_t::OK
                : tool_status_t::INVALID_OPTION;
        }
        case tool_option_type_t::ENUM:
            for ( usize i = 0u; i < desc.nEnumValues; ++i ) {
                if ( StringView_Equals( value, desc.pEnumValues[i] ) ) {
                    return tool_status_t::OK;
                }
            }
            return tool_status_t::INVALID_OPTION;
        case tool_option_type_t::STRING:
        case tool_option_type_t::PATH:
            return tool_status_t::OK;
    }
    return tool_status_t::INVALID_OPTION;
}

bool_t ToolOption_SourceOverrides(
    tool_option_source_t incoming,
    tool_option_source_t current ) noexcept
{
    return incoming >= tool_option_source_t::DEFAULT_VALUE &&
           incoming <= tool_option_source_t::COMMAND_LINE &&
           current >= tool_option_source_t::DEFAULT_VALUE &&
           current <= tool_option_source_t::COMMAND_LINE &&
           incoming >= current;
}

const char *ToolOption_TypeName( tool_option_type_t type ) noexcept
{
    switch ( type ) {
        case tool_option_type_t::BOOLEAN: return "boolean";
        case tool_option_type_t::I64: return "i64";
        case tool_option_type_t::U64: return "u64";
        case tool_option_type_t::F64: return "f64";
        case tool_option_type_t::STRING: return "string";
        case tool_option_type_t::PATH: return "path";
        case tool_option_type_t::ENUM: return "enum";
    }
    return "unknown";
}

const char *ToolOption_SourceName( tool_option_source_t source ) noexcept
{
    switch ( source ) {
        case tool_option_source_t::DEFAULT_VALUE: return "default";
        case tool_option_source_t::PROJECT: return "project";
        case tool_option_source_t::PROFILE: return "profile";
        case tool_option_source_t::MACHINE: return "machine";
        case tool_option_source_t::COMMAND_LINE: return "command-line";
    }
    return "unknown";
}

} // namespace cypher::common
