//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_Schema.cpp
//  Purpose: Implements immutable CYKV schema checks and bounded validation.
//  Details: Descriptor validation catches authoring mistakes before use. Document
//           validation walks the semantic tree without allocation and emits stable,
//           path-addressed diagnostics into storage supplied by the caller.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Schema.h"

#include "CypherCommon_Char.h"
#include "CypherCommon_StringView.h"

#include <cmath>

namespace cypher::common
{

namespace
{

inline constexpr flags32_t CY_SCHEMA_MEMBER_FLAGS =
    SCHEMA_MEMBER_REQUIRED | SCHEMA_MEMBER_DEPRECATED;
inline constexpr flags32_t CY_SCHEMA_OBJECT_FLAGS =
    SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS;

struct descriptor_context_t {
    const schema_rule_t *pAncestors[CY_SCHEMA_MAX_DESCRIPTOR_DEPTH]{};
    usize nDepth{ 0u };
};

struct validation_context_t {
    const schema_validation_options_t *pOptions{ nullptr };
    schema_diagnostic_t *pDiagnostics{ nullptr };
    usize nDiagnosticCapacity{ 0u };
    schema_validation_result_t result{};
    char path[CY_SCHEMA_MAX_PATH]{};
    usize cchPath{ 0u };
    bool_t bNodeLimitReported{ CY_FALSE };
};

CYPHER_NODISCARD bool_t ViewHasNoNullByte( string_view_t view ) noexcept
{
    if ( view.pData == nullptr && view.cchLength != 0u ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < view.cchLength; ++iByte ) {
        if ( view.pData[iByte] == '\0' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t SchemaIdIsValid( string_view_t schemaId ) noexcept
{
    if ( !ViewHasNoNullByte( schemaId ) || schemaId.cchLength == 0u ) {
        return CY_FALSE;
    }

    bool_t bAtComponentStart = CY_TRUE;
    bool_t bSawDot = CY_FALSE;
    for ( usize iByte = 0u; iByte < schemaId.cchLength; ++iByte ) {
        const char ch = schemaId.pData[iByte];
        if ( ch == '.' ) {
            if ( bAtComponentStart ) {
                return CY_FALSE;
            }
            bAtComponentStart = CY_TRUE;
            bSawDot = CY_TRUE;
        } else if ( bAtComponentStart ) {
            if ( ch < 'a' || ch > 'z' ) {
                return CY_FALSE;
            }
            bAtComponentStart = CY_FALSE;
        } else if ( ( ch < 'a' || ch > 'z' ) &&
                    !Char_IsDigitAscii( ch ) && ch != '_' && ch != '-' ) {
            return CY_FALSE;
        }
    }
    return bSawDot && !bAtComponentStart;
}

CYPHER_NODISCARD bool_t MemberNameIsValid( string_view_t name ) noexcept
{
    return name.cchLength != 0u && ViewHasNoNullByte( name );
}

CYPHER_NODISCARD bool_t RuleIsAncestor(
    const descriptor_context_t &context,
    const schema_rule_t *pRule ) noexcept
{
    for ( usize iDepth = 0u; iDepth < context.nDepth; ++iDepth ) {
        if ( context.pAncestors[iDepth] == pRule ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

CYPHER_NODISCARD schema_descriptor_status_t CheckRule(
    const schema_rule_t *pRule,
    descriptor_context_t &context ) noexcept
{
    if ( pRule == nullptr ) {
        return schema_descriptor_status_t::INVALID_RULE;
    }
    if ( RuleIsAncestor( context, pRule ) ) {
        return schema_descriptor_status_t::OK;
    }
    if ( context.nDepth >= CY_SCHEMA_MAX_DESCRIPTOR_DEPTH ) {
        return schema_descriptor_status_t::DESCRIPTOR_DEPTH_LIMIT;
    }
    if ( pRule->allowedTypes == SCHEMA_TYPE_NONE ||
         ( pRule->allowedTypes & ~static_cast<flags32_t>( SCHEMA_TYPE_ANY ) ) != 0u ) {
        return schema_descriptor_status_t::INVALID_TYPE_MASK;
    }
    if ( pRule->signedInteger.nMin > pRule->signedInteger.nMax ||
         pRule->unsignedInteger.nMin > pRule->unsignedInteger.nMax ||
         !std::isfinite( pRule->floatingPoint.flMin ) ||
         !std::isfinite( pRule->floatingPoint.flMax ) ||
         pRule->floatingPoint.flMin > pRule->floatingPoint.flMax ||
         pRule->string.cbMinLength > pRule->string.cbMaxLength ||
         pRule->binary.cbMinSize > pRule->binary.cbMaxSize ||
         pRule->array.nMinElements > pRule->array.nMaxElements ) {
        return schema_descriptor_status_t::INVALID_RANGE;
    }
    if ( ( pRule->object.flags & ~CY_SCHEMA_OBJECT_FLAGS ) != 0u ) {
        return schema_descriptor_status_t::INVALID_RULE;
    }
    if ( ( pRule->object.pMembers == nullptr ) !=
         ( pRule->object.nMembers == 0u ) ) {
        return schema_descriptor_status_t::INVALID_RULE;
    }
    if ( ( pRule->string.pAllowedValues == nullptr ) !=
         ( pRule->string.nAllowedValues == 0u ) ) {
        return schema_descriptor_status_t::INVALID_RULE;
    }
    if ( pRule->object.nMembers != 0u &&
         ( pRule->allowedTypes & SCHEMA_TYPE_OBJECT ) == 0u ) {
        return schema_descriptor_status_t::INVALID_RULE;
    }
    if ( pRule->array.pElementRule != nullptr &&
         ( pRule->allowedTypes & SCHEMA_TYPE_ARRAY ) == 0u ) {
        return schema_descriptor_status_t::INVALID_RULE;
    }
    if ( ( pRule->allowedTypes & SCHEMA_TYPE_ARRAY ) != 0u &&
         pRule->array.pElementRule == nullptr ) {
        return schema_descriptor_status_t::INVALID_RULE;
    }
    if ( pRule->string.nAllowedValues != 0u &&
         ( pRule->allowedTypes & SCHEMA_TYPE_STRING ) == 0u ) {
        return schema_descriptor_status_t::INVALID_RULE;
    }

    context.pAncestors[context.nDepth++] = pRule;

    for ( usize iAllowed = 0u;
          iAllowed < pRule->string.nAllowedValues;
          ++iAllowed ) {
        const string_view_t value = pRule->string.pAllowedValues[iAllowed];
        if ( !ViewHasNoNullByte( value ) ) {
            --context.nDepth;
            return schema_descriptor_status_t::INVALID_RULE;
        }
        for ( usize iPrevious = 0u; iPrevious < iAllowed; ++iPrevious ) {
            if ( StringView_Equals(
                     value,
                     pRule->string.pAllowedValues[iPrevious] ) ) {
                --context.nDepth;
                return schema_descriptor_status_t::INVALID_RULE;
            }
        }
    }

    for ( usize iMember = 0u; iMember < pRule->object.nMembers; ++iMember ) {
        const schema_member_t &member = pRule->object.pMembers[iMember];
        if ( !MemberNameIsValid( member.name ) || member.pRule == nullptr ||
             ( member.flags & ~CY_SCHEMA_MEMBER_FLAGS ) != 0u ||
             ( ( member.flags & SCHEMA_MEMBER_REQUIRED ) != 0u &&
               ( member.flags & SCHEMA_MEMBER_DEPRECATED ) != 0u ) ) {
            --context.nDepth;
            return schema_descriptor_status_t::INVALID_MEMBER;
        }
        for ( usize iPrevious = 0u; iPrevious < iMember; ++iPrevious ) {
            if ( StringView_Equals(
                     member.name,
                     pRule->object.pMembers[iPrevious].name ) ) {
                --context.nDepth;
                return schema_descriptor_status_t::DUPLICATE_MEMBER;
            }
        }

        const schema_descriptor_status_t status = CheckRule(
            member.pRule,
            context );
        if ( status != schema_descriptor_status_t::OK ) {
            --context.nDepth;
            return status;
        }
    }

    if ( pRule->array.pElementRule != nullptr ) {
        const schema_descriptor_status_t status = CheckRule(
            pRule->array.pElementRule,
            context );
        if ( status != schema_descriptor_status_t::OK ) {
            --context.nDepth;
            return status;
        }
    }

    --context.nDepth;
    return schema_descriptor_status_t::OK;
}

void CopyPath(
    const validation_context_t &context,
    char *pDest ) noexcept
{
    if ( context.cchPath == 0u ) {
        pDest[0] = '/';
        pDest[1] = '\0';
        return;
    }
    for ( usize iByte = 0u; iByte < context.cchPath; ++iByte ) {
        pDest[iByte] = context.path[iByte];
    }
    pDest[context.cchPath] = '\0';
}

void EmitDiagnostic(
    validation_context_t &context,
    schema_diagnostic_code_t code,
    schema_diagnostic_severity_t severity,
    flags32_t expectedTypes,
    key_value_type_t actualType ) noexcept
{
    ++context.result.nDiagnosticsRequired;
    if ( severity == schema_diagnostic_severity_t::ERROR ) {
        ++context.result.nErrors;
    } else {
        ++context.result.nWarnings;
    }

    if ( context.result.nDiagnosticsWritten < context.nDiagnosticCapacity ) {
        schema_diagnostic_t &diagnostic =
            context.pDiagnostics[context.result.nDiagnosticsWritten++];
        diagnostic = {};
        diagnostic.code = code;
        diagnostic.severity = severity;
        diagnostic.expectedTypes = expectedTypes;
        diagnostic.actualType = actualType;
        CopyPath( context, diagnostic.path );
    } else {
        context.result.bDiagnosticsTruncated = CY_TRUE;
    }
}

CYPHER_NODISCARD bool_t PushPathBytes(
    validation_context_t &context,
    const char *pBytes,
    usize cBytes ) noexcept
{
    if ( pBytes == nullptr || cBytes > CY_SCHEMA_MAX_PATH - 1u - context.cchPath ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < cBytes; ++iByte ) {
        context.path[context.cchPath++] = pBytes[iByte];
    }
    context.path[context.cchPath] = '\0';
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t PushMemberPath(
    validation_context_t &context,
    string_view_t name ) noexcept
{
    if ( !PushPathBytes( context, "/", 1u ) ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < name.cchLength; ++iByte ) {
        const u8 value = static_cast<u8>( name.pData[iByte] );
        if ( value == static_cast<u8>( '~' ) ) {
            if ( !PushPathBytes( context, "~0", 2u ) ) {
                return CY_FALSE;
            }
        } else if ( value == static_cast<u8>( '/' ) ) {
            if ( !PushPathBytes( context, "~1", 2u ) ) {
                return CY_FALSE;
            }
        } else if ( value < 0x20u || value == 0x7fu ) {
            constexpr char digits[]{ "0123456789abcdef" };
            const char encoded[]{
                '~', 'x', digits[( value >> 4u ) & 0x0fu], digits[value & 0x0fu]
            };
            if ( !PushPathBytes( context, encoded, sizeof( encoded ) ) ) {
                return CY_FALSE;
            }
        } else if ( !PushPathBytes( context, name.pData + iByte, 1u ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t PushIndexPath(
    validation_context_t &context,
    usize iValue ) noexcept
{
    char reversed[32]{};
    usize nDigits = 0u;
    do {
        reversed[nDigits++] = static_cast<char>( '0' + ( iValue % 10u ) );
        iValue /= 10u;
    } while ( iValue != 0u );

    if ( !PushPathBytes( context, "/", 1u ) ||
         nDigits > CY_SCHEMA_MAX_PATH - 1u - context.cchPath ) {
        return CY_FALSE;
    }
    while ( nDigits != 0u ) {
        context.path[context.cchPath++] = reversed[--nDigits];
    }
    context.path[context.cchPath] = '\0';
    return CY_TRUE;
}

void RestorePath( validation_context_t &context, usize cchPath ) noexcept
{
    context.cchPath = cchPath;
    context.path[cchPath] = '\0';
}

CYPHER_NODISCARD const schema_member_t *FindMemberRule(
    const schema_rule_t &rule,
    string_view_t name ) noexcept
{
    for ( usize iMember = 0u; iMember < rule.object.nMembers; ++iMember ) {
        if ( StringView_Equals( rule.object.pMembers[iMember].name, name ) ) {
            return &rule.object.pMembers[iMember];
        }
    }
    return nullptr;
}

CYPHER_NODISCARD bool_t StringIsAllowed(
    const schema_string_rules_t &rules,
    string_view_t value ) noexcept
{
    if ( rules.nAllowedValues == 0u ) {
        return CY_TRUE;
    }
    for ( usize iAllowed = 0u; iAllowed < rules.nAllowedValues; ++iAllowed ) {
        if ( StringView_Equals( value, rules.pAllowedValues[iAllowed] ) ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

void ValidateRule(
    validation_context_t &context,
    const schema_rule_t &rule,
    const key_value_t *pValue,
    usize nDepth ) noexcept;

void ValidateObject(
    validation_context_t &context,
    const schema_rule_t &rule,
    const key_value_t *pValue,
    usize nDepth ) noexcept
{
    for ( usize iMember = 0u; iMember < rule.object.nMembers; ++iMember ) {
        const schema_member_t &member = rule.object.pMembers[iMember];
        if ( ( member.flags & SCHEMA_MEMBER_REQUIRED ) == 0u ||
             KeyValue_Find( pValue, member.name ) != nullptr ) {
            continue;
        }

        const usize cchSaved = context.cchPath;
        if ( !PushMemberPath( context, member.name ) ) {
            RestorePath( context, cchSaved );
            EmitDiagnostic(
                context,
                schema_diagnostic_code_t::PATH_LIMIT,
                schema_diagnostic_severity_t::ERROR,
                SCHEMA_TYPE_NONE,
                key_value_type_t::NULL_VALUE );
            continue;
        }
        EmitDiagnostic(
            context,
            schema_diagnostic_code_t::MISSING_REQUIRED_MEMBER,
            schema_diagnostic_severity_t::ERROR,
            member.pRule->allowedTypes,
            key_value_type_t::NULL_VALUE );
        RestorePath( context, cchSaved );
    }

    const usize nChildren = KeyValue_ChildCount( pValue );
    for ( usize iChild = 0u; iChild < nChildren; ++iChild ) {
        const key_value_t *pChild = KeyValue_ChildAt( pValue, iChild );
        const string_view_t name = KeyValue_Name( pChild );
        const schema_member_t *pMember = FindMemberRule( rule, name );
        const usize cchSaved = context.cchPath;
        if ( !PushMemberPath( context, name ) ) {
            RestorePath( context, cchSaved );
            EmitDiagnostic(
                context,
                schema_diagnostic_code_t::PATH_LIMIT,
                schema_diagnostic_severity_t::ERROR,
                SCHEMA_TYPE_NONE,
                KeyValue_Type( pChild ) );
            continue;
        }

        if ( pMember == nullptr ) {
            if ( ( rule.object.flags &
                   SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS ) != 0u ) {
                EmitDiagnostic(
                    context,
                    schema_diagnostic_code_t::UNKNOWN_MEMBER,
                    schema_diagnostic_severity_t::ERROR,
                    SCHEMA_TYPE_NONE,
                    KeyValue_Type( pChild ) );
            }
        } else {
            if ( context.pOptions->bReportDeprecatedMembers &&
                 ( pMember->flags & SCHEMA_MEMBER_DEPRECATED ) != 0u ) {
                EmitDiagnostic(
                    context,
                    schema_diagnostic_code_t::DEPRECATED_MEMBER,
                    schema_diagnostic_severity_t::WARNING,
                    pMember->pRule->allowedTypes,
                    KeyValue_Type( pChild ) );
            }
            ValidateRule(
                context,
                *pMember->pRule,
                pChild,
                nDepth + 1u );
        }
        RestorePath( context, cchSaved );
    }
}

void ValidateArray(
    validation_context_t &context,
    const schema_rule_t &rule,
    const key_value_t *pValue,
    usize nDepth ) noexcept
{
    const usize nChildren = KeyValue_ChildCount( pValue );
    if ( nChildren < rule.array.nMinElements ||
         nChildren > rule.array.nMaxElements ) {
        EmitDiagnostic(
            context,
            schema_diagnostic_code_t::ARRAY_LENGTH,
            schema_diagnostic_severity_t::ERROR,
            SCHEMA_TYPE_ARRAY,
            key_value_type_t::ARRAY );
    }

    for ( usize iChild = 0u; iChild < nChildren; ++iChild ) {
        const usize cchSaved = context.cchPath;
        if ( !PushIndexPath( context, iChild ) ) {
            RestorePath( context, cchSaved );
            EmitDiagnostic(
                context,
                schema_diagnostic_code_t::PATH_LIMIT,
                schema_diagnostic_severity_t::ERROR,
                SCHEMA_TYPE_NONE,
                KeyValue_Type( KeyValue_ChildAt( pValue, iChild ) ) );
            continue;
        }
        ValidateRule(
            context,
            *rule.array.pElementRule,
            KeyValue_ChildAt( pValue, iChild ),
            nDepth + 1u );
        RestorePath( context, cchSaved );
    }
}

void ValidateRule(
    validation_context_t &context,
    const schema_rule_t &rule,
    const key_value_t *pValue,
    usize nDepth ) noexcept
{
    if ( context.result.nNodesVisited >= context.pOptions->nMaxNodes ) {
        if ( !context.bNodeLimitReported ) {
            EmitDiagnostic(
                context,
                schema_diagnostic_code_t::NODE_LIMIT,
                schema_diagnostic_severity_t::ERROR,
                rule.allowedTypes,
                KeyValue_Type( pValue ) );
            context.bNodeLimitReported = CY_TRUE;
        }
        return;
    }
    ++context.result.nNodesVisited;

    if ( nDepth > context.pOptions->nMaxDepth ) {
        EmitDiagnostic(
            context,
            schema_diagnostic_code_t::DEPTH_LIMIT,
            schema_diagnostic_severity_t::ERROR,
            rule.allowedTypes,
            KeyValue_Type( pValue ) );
        return;
    }

    const key_value_type_t type = KeyValue_Type( pValue );
    const flags32_t typeFlag = Schema_TypeFlag( type );
    if ( ( rule.allowedTypes & typeFlag ) == 0u ) {
        EmitDiagnostic(
            context,
            schema_diagnostic_code_t::TYPE_MISMATCH,
            schema_diagnostic_severity_t::ERROR,
            rule.allowedTypes,
            type );
        return;
    }

    switch ( type ) {
        case key_value_type_t::I64: {
            i64 value = 0;
            if ( !KeyValue_GetI64( pValue, &value ) ||
                 value < rule.signedInteger.nMin ||
                 value > rule.signedInteger.nMax ) {
                EmitDiagnostic(
                    context,
                    schema_diagnostic_code_t::I64_RANGE,
                    schema_diagnostic_severity_t::ERROR,
                    SCHEMA_TYPE_I64,
                    type );
            }
            break;
        }
        case key_value_type_t::U64: {
            u64 value = 0u;
            if ( !KeyValue_GetU64( pValue, &value ) ||
                 value < rule.unsignedInteger.nMin ||
                 value > rule.unsignedInteger.nMax ) {
                EmitDiagnostic(
                    context,
                    schema_diagnostic_code_t::U64_RANGE,
                    schema_diagnostic_severity_t::ERROR,
                    SCHEMA_TYPE_U64,
                    type );
            }
            break;
        }
        case key_value_type_t::F64: {
            f64 value = 0.0;
            if ( !KeyValue_GetF64( pValue, &value ) || !std::isfinite( value ) ||
                 value < rule.floatingPoint.flMin ||
                 value > rule.floatingPoint.flMax ) {
                EmitDiagnostic(
                    context,
                    schema_diagnostic_code_t::F64_RANGE,
                    schema_diagnostic_severity_t::ERROR,
                    SCHEMA_TYPE_F64,
                    type );
            }
            break;
        }
        case key_value_type_t::STRING: {
            string_view_t value{};
            if ( !KeyValue_GetString( pValue, &value ) ||
                 value.cchLength < rule.string.cbMinLength ||
                 value.cchLength > rule.string.cbMaxLength ) {
                EmitDiagnostic(
                    context,
                    schema_diagnostic_code_t::STRING_LENGTH,
                    schema_diagnostic_severity_t::ERROR,
                    SCHEMA_TYPE_STRING,
                    type );
            } else if ( !StringIsAllowed( rule.string, value ) ) {
                EmitDiagnostic(
                    context,
                    schema_diagnostic_code_t::STRING_VALUE,
                    schema_diagnostic_severity_t::ERROR,
                    SCHEMA_TYPE_STRING,
                    type );
            }
            break;
        }
        case key_value_type_t::BINARY: {
            binary_block_t value{};
            if ( !KeyValue_GetBinary( pValue, &value ) ||
                 value.cbSize < rule.binary.cbMinSize ||
                 value.cbSize > rule.binary.cbMaxSize ) {
                EmitDiagnostic(
                    context,
                    schema_diagnostic_code_t::BINARY_SIZE,
                    schema_diagnostic_severity_t::ERROR,
                    SCHEMA_TYPE_BINARY,
                    type );
            }
            break;
        }
        case key_value_type_t::OBJECT:
            ValidateObject( context, rule, pValue, nDepth );
            break;
        case key_value_type_t::ARRAY:
            ValidateArray( context, rule, pValue, nDepth );
            break;
        case key_value_type_t::NULL_VALUE:
        case key_value_type_t::BOOL:
            break;
    }
}

} // namespace

flags32_t Schema_TypeFlag( key_value_type_t type ) noexcept
{
    switch ( type ) {
        case key_value_type_t::NULL_VALUE: return SCHEMA_TYPE_NULL;
        case key_value_type_t::BOOL:       return SCHEMA_TYPE_BOOL;
        case key_value_type_t::I64:        return SCHEMA_TYPE_I64;
        case key_value_type_t::U64:        return SCHEMA_TYPE_U64;
        case key_value_type_t::F64:        return SCHEMA_TYPE_F64;
        case key_value_type_t::STRING:     return SCHEMA_TYPE_STRING;
        case key_value_type_t::BINARY:     return SCHEMA_TYPE_BINARY;
        case key_value_type_t::OBJECT:     return SCHEMA_TYPE_OBJECT;
        case key_value_type_t::ARRAY:      return SCHEMA_TYPE_ARRAY;
    }
    return SCHEMA_TYPE_NONE;
}

schema_descriptor_status_t Schema_CheckDescriptor(
    const schema_descriptor_t *pSchema ) noexcept
{
    if ( pSchema == nullptr || pSchema->pRootRule == nullptr ) {
        return schema_descriptor_status_t::INVALID_ARGUMENT;
    }
    if ( !SchemaIdIsValid( pSchema->schemaId ) ) {
        return schema_descriptor_status_t::INVALID_SCHEMA_ID;
    }
    if ( pSchema->nVersion == 0u ) {
        return schema_descriptor_status_t::INVALID_VERSION;
    }

    descriptor_context_t context{};
    return CheckRule( pSchema->pRootRule, context );
}

schema_validation_result_t Schema_ValidateDocument(
    const schema_descriptor_t *pSchema,
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity ) noexcept
{
    validation_context_t context{};
    context.pOptions = &options;
    context.pDiagnostics = pDiagnostics;
    context.nDiagnosticCapacity = nDiagnosticCapacity;

    if ( pSchema == nullptr || pDocument == nullptr ||
         ( pDiagnostics == nullptr && nDiagnosticCapacity != 0u ) ||
         options.nMaxDepth == 0u || options.nMaxNodes == 0u ) {
        context.result.status = schema_validation_status_t::INVALID_ARGUMENT;
        return context.result;
    }
    if ( Schema_CheckDescriptor( pSchema ) !=
         schema_descriptor_status_t::OK ) {
        context.result.status = schema_validation_status_t::INVALID_SCHEMA;
        return context.result;
    }

    const key_value_document_header_t header =
        KeyValue_DocumentHeader( pDocument );
    if ( header.nLanguageVersion != CYKV_LANGUAGE_VERSION ) {
        EmitDiagnostic(
            context,
            schema_diagnostic_code_t::LANGUAGE_VERSION_MISMATCH,
            schema_diagnostic_severity_t::ERROR,
            SCHEMA_TYPE_NONE,
            KeyValue_Type( KeyValue_Root( pDocument ) ) );
    }
    if ( !StringView_Equals( header.schemaId, pSchema->schemaId ) ) {
        EmitDiagnostic(
            context,
            schema_diagnostic_code_t::SCHEMA_ID_MISMATCH,
            schema_diagnostic_severity_t::ERROR,
            SCHEMA_TYPE_NONE,
            KeyValue_Type( KeyValue_Root( pDocument ) ) );
    }
    if ( header.nSchemaVersion != pSchema->nVersion ) {
        EmitDiagnostic(
            context,
            schema_diagnostic_code_t::SCHEMA_VERSION_MISMATCH,
            schema_diagnostic_severity_t::ERROR,
            SCHEMA_TYPE_NONE,
            KeyValue_Type( KeyValue_Root( pDocument ) ) );
    }
    if ( context.result.nErrors == 0u ) {
        ValidateRule(
            context,
            *pSchema->pRootRule,
            KeyValue_Root( pDocument ),
            0u );
    }

    context.result.status = context.result.nErrors == 0u
        ? schema_validation_status_t::OK
        : schema_validation_status_t::INVALID_DOCUMENT;
    return context.result;
}

bool_t Schema_ValidationSucceeded(
    const schema_validation_result_t &result ) noexcept
{
    return result.status == schema_validation_status_t::OK &&
           result.nErrors == 0u;
}

const char *Schema_DescriptorStatusName(
    schema_descriptor_status_t status ) noexcept
{
    switch ( status ) {
        case schema_descriptor_status_t::OK: return "OK";
        case schema_descriptor_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case schema_descriptor_status_t::INVALID_SCHEMA_ID: return "INVALID_SCHEMA_ID";
        case schema_descriptor_status_t::INVALID_VERSION: return "INVALID_VERSION";
        case schema_descriptor_status_t::INVALID_TYPE_MASK: return "INVALID_TYPE_MASK";
        case schema_descriptor_status_t::INVALID_RULE: return "INVALID_RULE";
        case schema_descriptor_status_t::INVALID_MEMBER: return "INVALID_MEMBER";
        case schema_descriptor_status_t::DUPLICATE_MEMBER: return "DUPLICATE_MEMBER";
        case schema_descriptor_status_t::INVALID_RANGE: return "INVALID_RANGE";
        case schema_descriptor_status_t::DESCRIPTOR_DEPTH_LIMIT:
            return "DESCRIPTOR_DEPTH_LIMIT";
    }
    return "UNKNOWN";
}

const char *Schema_ValidationStatusName(
    schema_validation_status_t status ) noexcept
{
    switch ( status ) {
        case schema_validation_status_t::OK: return "OK";
        case schema_validation_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case schema_validation_status_t::INVALID_SCHEMA: return "INVALID_SCHEMA";
        case schema_validation_status_t::INVALID_DOCUMENT: return "INVALID_DOCUMENT";
        case schema_validation_status_t::SCHEMA_NOT_FOUND: return "SCHEMA_NOT_FOUND";
    }
    return "UNKNOWN";
}

const char *Schema_DiagnosticCodeName(
    schema_diagnostic_code_t code ) noexcept
{
    switch ( code ) {
        case schema_diagnostic_code_t::NONE: return "NONE";
        case schema_diagnostic_code_t::LANGUAGE_VERSION_MISMATCH: return "LANGUAGE_VERSION_MISMATCH";
        case schema_diagnostic_code_t::SCHEMA_ID_MISMATCH: return "SCHEMA_ID_MISMATCH";
        case schema_diagnostic_code_t::SCHEMA_VERSION_MISMATCH: return "SCHEMA_VERSION_MISMATCH";
        case schema_diagnostic_code_t::TYPE_MISMATCH: return "TYPE_MISMATCH";
        case schema_diagnostic_code_t::MISSING_REQUIRED_MEMBER: return "MISSING_REQUIRED_MEMBER";
        case schema_diagnostic_code_t::UNKNOWN_MEMBER: return "UNKNOWN_MEMBER";
        case schema_diagnostic_code_t::DEPRECATED_MEMBER: return "DEPRECATED_MEMBER";
        case schema_diagnostic_code_t::I64_RANGE: return "I64_RANGE";
        case schema_diagnostic_code_t::U64_RANGE: return "U64_RANGE";
        case schema_diagnostic_code_t::F64_RANGE: return "F64_RANGE";
        case schema_diagnostic_code_t::STRING_LENGTH: return "STRING_LENGTH";
        case schema_diagnostic_code_t::STRING_VALUE: return "STRING_VALUE";
        case schema_diagnostic_code_t::BINARY_SIZE: return "BINARY_SIZE";
        case schema_diagnostic_code_t::ARRAY_LENGTH: return "ARRAY_LENGTH";
        case schema_diagnostic_code_t::PATH_LIMIT: return "PATH_LIMIT";
        case schema_diagnostic_code_t::DEPTH_LIMIT: return "DEPTH_LIMIT";
        case schema_diagnostic_code_t::NODE_LIMIT: return "NODE_LIMIT";
        case schema_diagnostic_code_t::SCHEMA_NOT_FOUND: return "SCHEMA_NOT_FOUND";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
