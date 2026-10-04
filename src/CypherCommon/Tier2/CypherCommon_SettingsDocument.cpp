//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_SettingsDocument.cpp
//  Purpose: Implements the settings-family document store.
//  Details: Loading parses into a fresh document and swaps it in only after
//           the header is accepted, so a failed load never disturbs the
//           settings already in use. Writes mutate the kept tree in place,
//           which preserves member order and every member the build does not
//           know.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SettingsDocument.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueWriter.h"

#include <cmath>

namespace cypher::common
{

namespace
{

struct document_owner_t {
    key_value_document_t *p{ nullptr };
    ~document_owner_t() noexcept { KeyValue_DestroyDocument( p ); }
};

CYPHER_NODISCARD key_value_document_t *CreateDocument( const allocator_t *pAllocator ) noexcept
{
    key_value_document_desc_t desc{};
    desc.pAllocator = pAllocator;
    return KeyValue_CreateDocument( desc );
}

CYPHER_NODISCARD bool_t SetHeader(
    key_value_document_t *pDocument,
    const settings_document_identity_t &identity ) noexcept
{
    return KeyValue_SetDocumentHeader(
        pDocument,
        { CYKV_LANGUAGE_VERSION, identity.schemaId, identity.nCurrentVersion } );
}

CYPHER_NODISCARD bool_t IsHexDigit( char c, u32 &valueOut ) noexcept
{
    if ( c >= '0' && c <= '9' ) { valueOut = static_cast<u32>( c - '0' ); return CY_TRUE; }
    if ( c >= 'a' && c <= 'f' ) { valueOut = static_cast<u32>( c - 'a' + 10 ); return CY_TRUE; }
    if ( c >= 'A' && c <= 'F' ) { valueOut = static_cast<u32>( c - 'A' + 10 ); return CY_TRUE; }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ParseDescriptorPath(
    const setting_descriptor_t &descriptor,
    settings_path_t &pathOut ) noexcept
{
    return descriptor.pPath != nullptr &&
           SettingsPath_Parse( StringView_FromCString( descriptor.pPath ), &pathOut );
}

// Walks every segment but the last; nullptr when absent or blocked.
CYPHER_NODISCARD const key_value_t *FindParent(
    const key_value_t *pBase,
    const settings_path_t &path,
    bool_t &bBlockedOut ) noexcept
{
    bBlockedOut = CY_FALSE;
    const key_value_t *pNode = pBase;
    for ( usize iSegment = 0u; iSegment + 1u < path.nSegments; ++iSegment ) {
        if ( KeyValue_Type( pNode ) != key_value_type_t::OBJECT ) {
            bBlockedOut = CY_TRUE;
            return nullptr;
        }
        pNode = KeyValue_Find( pNode, path.segments[iSegment] );
        if ( pNode == nullptr ) {
            return nullptr;
        }
    }
    if ( KeyValue_Type( pNode ) != key_value_type_t::OBJECT ) {
        bBlockedOut = CY_TRUE;
        return nullptr;
    }
    return pNode;
}

CYPHER_NODISCARD bool_t TextIsEnumValue(
    const setting_descriptor_t &descriptor,
    string_view_t text,
    string_view_t &canonicalOut ) noexcept
{
    for ( usize iValue = 0u; iValue < descriptor.nEnumValues; ++iValue ) {
        const string_view_t allowed = StringView_FromCString( descriptor.ppEnumValues[iValue] );
        if ( StringView_Equals( allowed, text ) ) {
            // Return the descriptor's static text so resolved values never
            // borrow a document that may be reloaded.
            canonicalOut = allowed;
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

} // namespace

bool_t SettingsPath_Parse( string_view_t dottedPath, settings_path_t *pPathOut ) noexcept
{
    if ( pPathOut == nullptr || dottedPath.pData == nullptr || dottedPath.cchLength == 0u ) {
        return CY_FALSE;
    }
    settings_path_t path{};
    usize iStart = 0u;
    for ( usize iChar = 0u; iChar <= dottedPath.cchLength; ++iChar ) {
        if ( iChar < dottedPath.cchLength && dottedPath.pData[iChar] != '.' ) {
            continue;
        }
        const usize cch = iChar - iStart;
        if ( cch == 0u || cch > CY_SETTINGS_SEGMENT_MAX_LENGTH ||
             path.nSegments == CY_SETTINGS_PATH_MAX_SEGMENTS ) {
            return CY_FALSE;
        }
        path.segments[path.nSegments++] = { dottedPath.pData + iStart, cch };
        iStart = iChar + 1u;
    }
    *pPathOut = path;
    return CY_TRUE;
}

bool_t SettingsPath_Append( settings_path_t *pPath, string_view_t segment ) noexcept
{
    if ( pPath == nullptr || segment.pData == nullptr || segment.cchLength == 0u ||
         segment.cchLength > CY_SETTINGS_SEGMENT_MAX_LENGTH ||
         pPath->nSegments == CY_SETTINGS_PATH_MAX_SEGMENTS ) {
        return CY_FALSE;
    }
    pPath->segments[pPath->nSegments++] = segment;
    return CY_TRUE;
}

settings_document_t::~settings_document_t() noexcept
{
    SettingsDocument_Shutdown( this );
}

settings_document_status_t SettingsDocument_Init(
    settings_document_t *pStore,
    const allocator_t *pAllocator,
    const settings_document_identity_t &identity ) noexcept
{
    if ( pStore == nullptr || pAllocator == nullptr || identity.schemaId.cchLength == 0u ||
         identity.nOldestVersion == 0u || identity.nOldestVersion > identity.nCurrentVersion ) {
        return settings_document_status_t::INVALID_ARGUMENT;
    }
    document_owner_t owner{ CreateDocument( pAllocator ) };
    if ( owner.p == nullptr || !SetHeader( owner.p, identity ) ||
         !KeyValue_SetRootType( owner.p, key_value_type_t::OBJECT ) ) {
        return settings_document_status_t::OUT_OF_MEMORY;
    }
    SettingsDocument_Shutdown( pStore );
    pStore->pDocument = owner.p;
    owner.p = nullptr;
    pStore->pAllocator = pAllocator;
    pStore->identity = identity;
    pStore->nLoadedVersion = identity.nCurrentVersion;
    return settings_document_status_t::OK;
}

void SettingsDocument_Shutdown( settings_document_t *pStore ) noexcept
{
    if ( pStore == nullptr ) {
        return;
    }
    KeyValue_DestroyDocument( pStore->pDocument );
    pStore->pDocument = nullptr;
    pStore->nLoadedVersion = 0u;
}

bool_t SettingsDocument_IsInitialized( const settings_document_t *pStore ) noexcept
{
    return pStore != nullptr && pStore->pDocument != nullptr;
}

settings_document_load_result_t SettingsDocument_Load(
    settings_document_t *pStore,
    string_view_t text ) noexcept
{
    settings_document_load_result_t result{};
    if ( !SettingsDocument_IsInitialized( pStore ) || text.pData == nullptr ) {
        result.status = settings_document_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( text.cchLength > CY_SETTINGS_TEXT_MAX_BYTES ) {
        result.status = settings_document_status_t::TEXT_TOO_LARGE;
        return result;
    }
    document_owner_t owner{ CreateDocument( pStore->pAllocator ) };
    if ( owner.p == nullptr ) {
        result.status = settings_document_status_t::OUT_OF_MEMORY;
        return result;
    }
    key_value_parse_options_t options{};
    options.cbMaxInput = CY_SETTINGS_TEXT_MAX_BYTES;
    options.cbMaxStringData = CY_SETTINGS_TEXT_MAX_BYTES;
    const key_value_parse_result_t parsed = KeyValue_ParseText( text, options, owner.p );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        result.status = parsed.status == key_value_parse_status_t::OUT_OF_MEMORY
            ? settings_document_status_t::OUT_OF_MEMORY
            : settings_document_status_t::PARSE_FAILED;
        result.parseStatus = parsed.status;
        result.location = parsed.errorLocation;
        return result;
    }

    const key_value_document_header_t header = KeyValue_DocumentHeader( owner.p );
    result.nDeclaredVersion = header.nSchemaVersion;
    if ( header.nLanguageVersion != CYKV_LANGUAGE_VERSION ) {
        result.status = settings_document_status_t::LANGUAGE_MISMATCH;
        result.location = parsed.languageVersionLocation;
        return result;
    }
    if ( !StringView_Equals( header.schemaId, pStore->identity.schemaId ) ) {
        result.status = settings_document_status_t::SCHEMA_MISMATCH;
        result.location = parsed.schemaIdLocation;
        return result;
    }
    if ( header.nSchemaVersion < pStore->identity.nOldestVersion ||
         header.nSchemaVersion > pStore->identity.nCurrentVersion ) {
        result.status = settings_document_status_t::UNSUPPORTED_VERSION;
        result.location = parsed.schemaVersionLocation;
        return result;
    }
    if ( KeyValue_Type( KeyValue_Root( owner.p ) ) != key_value_type_t::OBJECT ) {
        result.status = settings_document_status_t::ROOT_NOT_OBJECT;
        return result;
    }

    // Accepted: swap the new tree in. The header keeps the declared version
    // until Write upgrades it, so migrations can see where the data came from.
    KeyValue_DestroyDocument( pStore->pDocument );
    pStore->pDocument = owner.p;
    owner.p = nullptr;
    pStore->nLoadedVersion = header.nSchemaVersion;
    return result;
}

settings_document_status_t SettingsDocument_Write(
    const settings_document_t *pStore,
    text_buffer_t *pTextOut ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pStore ) || pTextOut == nullptr ) {
        return settings_document_status_t::INVALID_ARGUMENT;
    }
    // Always write the current version: loading an older file and saving it
    // is the upgrade. The header is set on the owned tree, which is logically
    // const - its content is unchanged.
    if ( !SetHeader( pStore->pDocument, pStore->identity ) ) {
        return settings_document_status_t::OUT_OF_MEMORY;
    }
    const key_value_t *pRoot = KeyValue_Root( pStore->pDocument );
    // Settings files are edited by hand as often as by tools, so they are
    // written the way the format docs write them: bare keys where CYKV
    // allows, dotted IDs ("ui.background", "file.save") quoted, and reals as
    // typed (0.3, not 0.29999999999999999).
    key_value_write_options_t options{};
    options.flags = KEY_VALUE_WRITE_FLAG_PRETTY | KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE | KEY_VALUE_WRITE_FLAG_BARE_KEYS |
                    KEY_VALUE_WRITE_FLAG_QUOTE_DOTTED_KEYS | KEY_VALUE_WRITE_FLAG_SHORTEST_REALS;
    options.nIndentSpaces = 4u;
    const key_value_write_result_t measured = KeyValue_WriteText( pRoot, options, nullptr, 0u );
    if ( measured.status != key_value_write_status_t::OUTPUT_TRUNCATED &&
         measured.status != key_value_write_status_t::OK ) {
        return measured.status == key_value_write_status_t::OUT_OF_MEMORY
            ? settings_document_status_t::OUT_OF_MEMORY
            : settings_document_status_t::WRITE_FAILED;
    }
    if ( measured.cchRequired > CY_SETTINGS_TEXT_MAX_BYTES ) {
        return settings_document_status_t::TEXT_TOO_LARGE;
    }
    text_buffer_t pending{};
    if ( !TextBuffer_Init( &pending, pStore->pAllocator, measured.cchRequired ) ||
         !TextBuffer_Resize( &pending, measured.cchRequired ) ) {
        return settings_document_status_t::OUT_OF_MEMORY;
    }
    const key_value_write_result_t written = KeyValue_WriteText(
        pRoot, options, TextBuffer_Data( &pending ), TextBuffer_Capacity( &pending ) + 1u );
    if ( written.status != key_value_write_status_t::OK ||
         written.cchWritten != measured.cchRequired ) {
        return written.status == key_value_write_status_t::OUT_OF_MEMORY
            ? settings_document_status_t::OUT_OF_MEMORY
            : settings_document_status_t::WRITE_FAILED;
    }
    if ( !TextBuffer_Assign( pTextOut, TextBuffer_View( &pending ) ) ) {
        return settings_document_status_t::OUT_OF_MEMORY;
    }
    return settings_document_status_t::OK;
}

const key_value_t *SettingsDocument_Root( const settings_document_t *pStore ) noexcept
{
    return SettingsDocument_IsInitialized( pStore ) ? KeyValue_Root( pStore->pDocument ) : nullptr;
}

const key_value_t *SettingsNode_Find(
    const key_value_t *pBase,
    const settings_path_t &path ) noexcept
{
    if ( pBase == nullptr || path.nSegments == 0u ) {
        return nullptr;
    }
    bool_t bBlocked = CY_FALSE;
    const key_value_t *pParent = FindParent( pBase, path, bBlocked );
    return pParent != nullptr ? KeyValue_Find( pParent, path.segments[path.nSegments - 1u] ) : nullptr;
}

settings_document_status_t SettingsDocument_Ensure(
    settings_document_t *pStore,
    const settings_path_t &path,
    key_value_t **ppNodeOut ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pStore ) || path.nSegments == 0u || ppNodeOut == nullptr ) {
        return settings_document_status_t::INVALID_ARGUMENT;
    }
    key_value_document_t *pDocument = pStore->pDocument;

    // Check the whole route before creating anything, so a blocked path
    // leaves the document untouched.
    const key_value_t *pProbe = KeyValue_Root( pDocument );
    for ( usize iSegment = 0u; iSegment + 1u < path.nSegments && pProbe != nullptr; ++iSegment ) {
        pProbe = KeyValue_Find( pProbe, path.segments[iSegment] );
        if ( pProbe != nullptr && KeyValue_Type( pProbe ) != key_value_type_t::OBJECT ) {
            return settings_document_status_t::PATH_BLOCKED;
        }
    }

    key_value_t *pNode = KeyValue_Root( pDocument );
    for ( usize iSegment = 0u; iSegment + 1u < path.nSegments; ++iSegment ) {
        key_value_t *pNext = KeyValue_Find( pNode, path.segments[iSegment] );
        if ( pNext == nullptr ) {
            pNext = KeyValue_ObjectInsert( pDocument, pNode, path.segments[iSegment], key_value_type_t::OBJECT );
            if ( pNext == nullptr ) {
                return settings_document_status_t::OUT_OF_MEMORY;
            }
        }
        pNode = pNext;
    }
    const string_view_t leaf = path.segments[path.nSegments - 1u];
    key_value_t *pLeaf = KeyValue_Find( pNode, leaf );
    if ( pLeaf == nullptr ) {
        pLeaf = KeyValue_ObjectInsert( pDocument, pNode, leaf, key_value_type_t::NULL_VALUE );
        if ( pLeaf == nullptr ) {
            return settings_document_status_t::OUT_OF_MEMORY;
        }
    }
    CY_ASSERT( KeyValue_Find( pNode, leaf ) == pLeaf );
    *ppNodeOut = pLeaf;
    return settings_document_status_t::OK;
}

settings_document_status_t SettingsDocument_Remove(
    settings_document_t *pStore,
    const settings_path_t &path ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pStore ) || path.nSegments == 0u ) {
        return settings_document_status_t::INVALID_ARGUMENT;
    }
    // Record the chain so emptied parents can be pruned bottom-up.
    CY_ASSERT( path.nSegments <= CY_SETTINGS_PATH_MAX_SEGMENTS );
    key_value_t *chain[CY_SETTINGS_PATH_MAX_SEGMENTS + 1u]{};
    chain[0] = KeyValue_Root( pStore->pDocument );
    for ( usize iSegment = 0u; iSegment < path.nSegments; ++iSegment ) {
        if ( KeyValue_Type( chain[iSegment] ) != key_value_type_t::OBJECT ) {
            return settings_document_status_t::OK; // Nothing stored below a scalar.
        }
        chain[iSegment + 1u] = KeyValue_Find( chain[iSegment], path.segments[iSegment] );
        if ( chain[iSegment + 1u] == nullptr ) {
            return settings_document_status_t::OK;
        }
    }
    for ( usize iLevel = path.nSegments; iLevel > 0u; --iLevel ) {
        key_value_t *pChild = chain[iLevel];
        const bool_t bRemove = iLevel == path.nSegments ||
            ( KeyValue_Type( pChild ) == key_value_type_t::OBJECT && KeyValue_ChildCount( pChild ) == 0u );
        if ( !bRemove ) {
            break;
        }
        if ( !KeyValue_Remove( pStore->pDocument, chain[iLevel - 1u], pChild ) ) {
            return settings_document_status_t::INVALID_ARGUMENT;
        }
    }
    return settings_document_status_t::OK;
}

setting_value_t Setting_Default( const setting_descriptor_t &descriptor ) noexcept
{
    setting_value_t value{};
    value.type = descriptor.type;
    value.bValue = descriptor.bDefault;
    value.nValue = descriptor.nDefault;
    value.flValue = descriptor.flDefault;
    value.text = StringView_FromCString( descriptor.pDefaultText != nullptr ? descriptor.pDefaultText : "" );
    value.rgba = descriptor.rgbaDefault;
    return value;
}

setting_problem_code_t Setting_Check(
    const setting_descriptor_t &descriptor,
    const setting_value_t &value ) noexcept
{
    if ( value.type != descriptor.type ) {
        return setting_problem_code_t::WRONG_TYPE;
    }
    switch ( descriptor.type ) {
        case setting_type_t::BOOL:
        case setting_type_t::COLOR:
            return setting_problem_code_t::NONE;
        case setting_type_t::INTEGER:
            return value.nValue < descriptor.nMin || value.nValue > descriptor.nMax
                ? setting_problem_code_t::OUT_OF_RANGE : setting_problem_code_t::NONE;
        case setting_type_t::REAL:
            return !std::isfinite( value.flValue ) || value.flValue < descriptor.flMin ||
                           value.flValue > descriptor.flMax
                ? setting_problem_code_t::OUT_OF_RANGE : setting_problem_code_t::NONE;
        case setting_type_t::STRING:
            return value.text.cchLength > descriptor.cbMaxText
                ? setting_problem_code_t::TEXT_TOO_LONG : setting_problem_code_t::NONE;
        case setting_type_t::ENUM: {
            string_view_t canonical{};
            return TextIsEnumValue( descriptor, value.text, canonical )
                ? setting_problem_code_t::NONE : setting_problem_code_t::UNKNOWN_ENUM;
        }
    }
    return setting_problem_code_t::WRONG_TYPE;
}

setting_read_status_t Setting_Read(
    const key_value_t *pBase,
    const setting_descriptor_t &descriptor,
    setting_value_t *pValueOut,
    setting_problem_code_t *pProblemOut ) noexcept
{
    setting_problem_code_t problem = setting_problem_code_t::NONE;
    const auto invalid = [&]( setting_problem_code_t code ) noexcept {
        problem = code;
        if ( pProblemOut != nullptr ) { *pProblemOut = problem; }
        return setting_read_status_t::INVALID;
    };
    if ( pProblemOut != nullptr ) { *pProblemOut = problem; }

    settings_path_t path{};
    if ( !ParseDescriptorPath( descriptor, path ) ) {
        return invalid( setting_problem_code_t::BAD_PATH );
    }
    if ( pBase == nullptr ) {
        return setting_read_status_t::ABSENT;
    }
    bool_t bBlocked = CY_FALSE;
    const key_value_t *pParent = FindParent( pBase, path, bBlocked );
    if ( bBlocked ) {
        return invalid( setting_problem_code_t::BAD_PATH );
    }
    const key_value_t *pNode = pParent != nullptr
        ? KeyValue_Find( pParent, path.segments[path.nSegments - 1u] ) : nullptr;
    if ( pNode == nullptr ) {
        return setting_read_status_t::ABSENT;
    }

    setting_value_t value{};
    value.type = descriptor.type;
    switch ( descriptor.type ) {
        case setting_type_t::BOOL:
            if ( !KeyValue_GetBool( pNode, &value.bValue ) ) { return invalid( setting_problem_code_t::WRONG_TYPE ); }
            break;
        case setting_type_t::INTEGER: {
            if ( KeyValue_Type( pNode ) == key_value_type_t::U64 ) {
                u64 nUnsigned = 0u;
                if ( !KeyValue_GetU64( pNode, &nUnsigned ) || nUnsigned > static_cast<u64>( CY_I64_MAX ) ) {
                    return invalid( setting_problem_code_t::OUT_OF_RANGE );
                }
                value.nValue = static_cast<i64>( nUnsigned );
            } else if ( !KeyValue_GetI64( pNode, &value.nValue ) ) {
                return invalid( setting_problem_code_t::WRONG_TYPE );
            }
            break;
        }
        case setting_type_t::REAL: {
            // Integers are accepted so "1" is as valid as "1.0" in a hand-edited file.
            i64 nSigned = 0;
            u64 nUnsigned = 0u;
            if ( KeyValue_GetF64( pNode, &value.flValue ) ) {
            } else if ( KeyValue_GetI64( pNode, &nSigned ) ) {
                value.flValue = static_cast<f64>( nSigned );
            } else if ( KeyValue_GetU64( pNode, &nUnsigned ) ) {
                value.flValue = static_cast<f64>( nUnsigned );
            } else {
                return invalid( setting_problem_code_t::WRONG_TYPE );
            }
            break;
        }
        case setting_type_t::STRING:
            if ( !KeyValue_GetString( pNode, &value.text ) ) { return invalid( setting_problem_code_t::WRONG_TYPE ); }
            break;
        case setting_type_t::ENUM: {
            string_view_t text{};
            if ( !KeyValue_GetString( pNode, &text ) ) { return invalid( setting_problem_code_t::WRONG_TYPE ); }
            if ( !TextIsEnumValue( descriptor, text, value.text ) ) { return invalid( setting_problem_code_t::UNKNOWN_ENUM ); }
            break;
        }
        case setting_type_t::COLOR: {
            string_view_t text{};
            if ( !KeyValue_GetString( pNode, &text ) ) { return invalid( setting_problem_code_t::WRONG_TYPE ); }
            if ( !SettingColor_Parse( text, &value.rgba ) ) { return invalid( setting_problem_code_t::BAD_COLOR ); }
            break;
        }
    }
    const setting_problem_code_t check = Setting_Check( descriptor, value );
    if ( check != setting_problem_code_t::NONE ) {
        return invalid( check );
    }
    if ( pValueOut != nullptr ) {
        *pValueOut = value;
    }
    return setting_read_status_t::VALUE;
}

setting_resolution_t Setting_Resolve(
    const key_value_t *const *ppScopes,
    usize nScopes,
    const setting_descriptor_t &descriptor,
    setting_problem_t *pProblems,
    usize nProblemCapacity ) noexcept
{
    setting_resolution_t resolution{};
    for ( usize iScope = 0u; ppScopes != nullptr && iScope < nScopes; ++iScope ) {
        if ( ppScopes[iScope] == nullptr ) {
            continue;
        }
        setting_value_t value{};
        setting_problem_code_t problem = setting_problem_code_t::NONE;
        const setting_read_status_t status = Setting_Read( ppScopes[iScope], descriptor, &value, &problem );
        if ( status == setting_read_status_t::VALUE ) {
            resolution.value = value;
            resolution.iScope = iScope;
            return resolution;
        }
        if ( status == setting_read_status_t::INVALID ) {
            if ( pProblems != nullptr && resolution.nProblemsWritten < nProblemCapacity ) {
                pProblems[resolution.nProblemsWritten++] = { problem, &descriptor, iScope };
            }
            ++resolution.nProblemsRequired;
        }
    }
    resolution.value = Setting_Default( descriptor );
    return resolution;
}

settings_document_status_t Setting_Write(
    settings_document_t *pStore,
    const setting_descriptor_t &descriptor,
    const setting_value_t &value ) noexcept
{
    settings_path_t path{};
    if ( !SettingsDocument_IsInitialized( pStore ) || !ParseDescriptorPath( descriptor, path ) ||
         Setting_Check( descriptor, value ) != setting_problem_code_t::NONE ) {
        return settings_document_status_t::INVALID_ARGUMENT;
    }
    key_value_document_t *pDocument = pStore->pDocument;
    key_value_t *pLeaf = nullptr;
    const settings_document_status_t ensured = SettingsDocument_Ensure( pStore, path, &pLeaf );
    if ( ensured != settings_document_status_t::OK ) {
        return ensured;
    }

    bool_t bStored = CY_FALSE;
    switch ( descriptor.type ) {
        case setting_type_t::BOOL: bStored = KeyValue_SetBool( pDocument, pLeaf, value.bValue ); break;
        case setting_type_t::INTEGER: bStored = KeyValue_SetI64( pDocument, pLeaf, value.nValue ); break;
        case setting_type_t::REAL: bStored = KeyValue_SetF64( pDocument, pLeaf, value.flValue ); break;
        case setting_type_t::STRING:
        case setting_type_t::ENUM: bStored = KeyValue_SetString( pDocument, pLeaf, value.text ); break;
        case setting_type_t::COLOR: {
            char text[10]{};
            const usize cch = SettingColor_Format( value.rgba, text );
            bStored = KeyValue_SetString( pDocument, pLeaf, { text, cch } );
            break;
        }
    }
    return bStored ? settings_document_status_t::OK : settings_document_status_t::OUT_OF_MEMORY;
}

settings_document_status_t Setting_WriteOverride(
    settings_document_t *pStore,
    const setting_descriptor_t &descriptor,
    const setting_value_t &value,
    const setting_value_t &inheritedValue ) noexcept
{
    if ( Setting_ValuesEqual( value, inheritedValue ) ) {
        settings_path_t path{};
        if ( !SettingsDocument_IsInitialized( pStore ) || !ParseDescriptorPath( descriptor, path ) ) {
            return settings_document_status_t::INVALID_ARGUMENT;
        }
        return SettingsDocument_Remove( pStore, path );
    }
    return Setting_Write( pStore, descriptor, value );
}

bool_t Setting_ValuesEqual( const setting_value_t &a, const setting_value_t &b ) noexcept
{
    if ( a.type != b.type ) {
        return CY_FALSE;
    }
    switch ( a.type ) {
        case setting_type_t::BOOL: return a.bValue == b.bValue;
        case setting_type_t::INTEGER: return a.nValue == b.nValue;
        case setting_type_t::REAL: return a.flValue == b.flValue;
        case setting_type_t::STRING:
        case setting_type_t::ENUM: return StringView_Equals( a.text, b.text );
        case setting_type_t::COLOR: return a.rgba == b.rgba;
    }
    return CY_FALSE;
}

bool_t SettingColor_Parse( string_view_t text, u32 *pRgbaOut ) noexcept
{
    if ( pRgbaOut == nullptr || text.pData == nullptr ||
         ( text.cchLength != 7u && text.cchLength != 9u ) || text.pData[0] != '#' ) {
        return CY_FALSE;
    }
    u32 rgba = 0u;
    for ( usize iChar = 1u; iChar < text.cchLength; ++iChar ) {
        u32 digit = 0u;
        if ( !IsHexDigit( text.pData[iChar], digit ) ) {
            return CY_FALSE;
        }
        rgba = ( rgba << 4u ) | digit;
    }
    *pRgbaOut = text.cchLength == 7u ? ( rgba << 8u ) | 0xFFu : rgba;
    return CY_TRUE;
}

usize SettingColor_Format( u32 rgba, char ( &buffer )[10] ) noexcept
{
    constexpr char kDigits[] = "0123456789abcdef";
    const usize nDigits = ( rgba & 0xFFu ) == 0xFFu ? 6u : 8u;
    const u32 bits = nDigits == 6u ? rgba >> 8u : rgba;
    buffer[0] = '#';
    for ( usize iDigit = 0u; iDigit < nDigits; ++iDigit ) {
        const u32 shift = static_cast<u32>( ( nDigits - 1u - iDigit ) * 4u );
        buffer[1u + iDigit] = kDigits[( bits >> shift ) & 0xFu];
    }
    buffer[1u + nDigits] = '\0';
    return 1u + nDigits;
}

const char *SettingsDocument_StatusName( settings_document_status_t status ) noexcept
{
    switch ( status ) {
        case settings_document_status_t::OK: return "OK";
        case settings_document_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case settings_document_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case settings_document_status_t::TEXT_TOO_LARGE: return "TEXT_TOO_LARGE";
        case settings_document_status_t::PARSE_FAILED: return "PARSE_FAILED";
        case settings_document_status_t::LANGUAGE_MISMATCH: return "LANGUAGE_MISMATCH";
        case settings_document_status_t::SCHEMA_MISMATCH: return "SCHEMA_MISMATCH";
        case settings_document_status_t::UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
        case settings_document_status_t::ROOT_NOT_OBJECT: return "ROOT_NOT_OBJECT";
        case settings_document_status_t::PATH_BLOCKED: return "PATH_BLOCKED";
        case settings_document_status_t::WRITE_FAILED: return "WRITE_FAILED";
    }
    return "UNKNOWN";
}

const char *Setting_ProblemName( setting_problem_code_t code ) noexcept
{
    switch ( code ) {
        case setting_problem_code_t::NONE: return "NONE";
        case setting_problem_code_t::WRONG_TYPE: return "WRONG_TYPE";
        case setting_problem_code_t::OUT_OF_RANGE: return "OUT_OF_RANGE";
        case setting_problem_code_t::UNKNOWN_ENUM: return "UNKNOWN_ENUM";
        case setting_problem_code_t::BAD_COLOR: return "BAD_COLOR";
        case setting_problem_code_t::TEXT_TOO_LONG: return "TEXT_TOO_LONG";
        case setting_problem_code_t::BAD_PATH: return "BAD_PATH";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
