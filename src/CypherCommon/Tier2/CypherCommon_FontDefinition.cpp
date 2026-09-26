//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_FontDefinition.cpp
//  Purpose: Implements tolerant decoding of `.cyfont` family definitions.
//  Details: Each face and fallback is judged on its own so one bad entry in a
//           family - a renamed file, a typo in a weight - costs only that
//           entry. The output view is built locally and committed once.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_FontDefinition.h"

#include "CypherCommon_DataValidation.h"

namespace cypher::common
{

namespace
{

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t FontText( const char ( &text )[nExtent] ) noexcept
{
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

struct problem_sink_t {
    font_definition_decode_result_t &result;
    font_problem_t *pProblems;
    usize nCapacity;

    void Add( font_problem_code_t code, usize iElement ) noexcept
    {
        if ( pProblems != nullptr && result.nProblemsWritten < nCapacity ) {
            pProblems[result.nProblemsWritten++] = { code, iElement };
        }
        ++result.nProblemsRequired;
    }
};

CYPHER_NODISCARD bool_t EndsWith( string_view_t text, string_view_t suffix ) noexcept
{
    return text.cchLength >= suffix.cchLength &&
           StringView_Equals( { text.pData + text.cchLength - suffix.cchLength, suffix.cchLength }, suffix );
}

CYPHER_NODISCARD bool_t IsFaceFile( string_view_t file ) noexcept
{
    return DataValidation_Succeeded( DataValidation_CheckCanonicalVirtualPath( file, CY_FONT_PATH_MAX_LENGTH ) ) &&
           ( EndsWith( file, FontText( ".ttf" ) ) || EndsWith( file, FontText( ".otf" ) ) );
}

CYPHER_NODISCARD bool_t IsFallback( string_view_t fallback ) noexcept
{
    constexpr string_view_t kSystem = FontText( "system:" );
    if ( fallback.cchLength > kSystem.cchLength &&
         StringView_Equals( { fallback.pData, kSystem.cchLength }, kSystem ) ) {
        return fallback.cchLength - kSystem.cchLength <= CY_FONT_NAME_MAX_LENGTH;
    }
    return DataValidation_Succeeded(
        DataValidation_CheckResourcePath( fallback, FontText( ".cyfont" ), CY_FONT_PATH_MAX_LENGTH ) );
}

} // namespace

settings_document_identity_t FontDefinition_Identity() noexcept
{
    return { FontText( "cypher.font" ), CY_FONT_SCHEMA_VERSION, CY_FONT_SCHEMA_VERSION };
}

font_definition_decode_result_t FontDefinition_Decode(
    const key_value_document_t *pDocument,
    font_problem_t *pProblems,
    usize nProblemCapacity,
    font_definition_view_t *pDefinitionOut ) noexcept
{
    font_definition_decode_result_t result{};
    if ( pDocument == nullptr || pDefinitionOut == nullptr ||
         ( pProblems == nullptr && nProblemCapacity != 0u ) ) {
        result.status = font_definition_status_t::INVALID_ARGUMENT;
        return result;
    }
    const key_value_document_header_t header = KeyValue_DocumentHeader( pDocument );
    const key_value_t *pRoot = KeyValue_Root( pDocument );
    if ( header.nLanguageVersion != CYKV_LANGUAGE_VERSION ||
         !StringView_Equals( header.schemaId, FontText( "cypher.font" ) ) ||
         header.nSchemaVersion != CY_FONT_SCHEMA_VERSION ||
         KeyValue_Type( pRoot ) != key_value_type_t::OBJECT ) {
        result.status = font_definition_status_t::INVALID_HEADER;
        return result;
    }

    problem_sink_t sink{ result, pProblems, nProblemCapacity };
    font_definition_view_t definition{};
    if ( !KeyValue_GetString( KeyValue_Find( pRoot, FontText( "name" ) ), &definition.name ) ||
         definition.name.cchLength == 0u || definition.name.cchLength > CY_FONT_NAME_MAX_LENGTH ) {
        result.status = font_definition_status_t::INVALID_NAME;
        return result;
    }

    const key_value_t *pFaces = KeyValue_Find( pRoot, FontText( "faces" ) );
    const usize nFaces = KeyValue_Type( pFaces ) == key_value_type_t::ARRAY ? KeyValue_ChildCount( pFaces ) : 0u;
    for ( usize iFace = 0u; iFace < nFaces; ++iFace ) {
        if ( definition.nFaces == CY_FONT_MAX_FACES ) {
            sink.Add( font_problem_code_t::TOO_MANY_FACES, iFace );
            break;
        }
        const key_value_t *pFace = KeyValue_ChildAt( pFaces, iFace );
        font_face_view_t face{};
        if ( KeyValue_Type( pFace ) != key_value_type_t::OBJECT ||
             !KeyValue_GetString( KeyValue_Find( pFace, FontText( "file" ) ), &face.file ) ) {
            sink.Add( font_problem_code_t::BAD_FACE, iFace );
            continue;
        }
        if ( !IsFaceFile( face.file ) ) {
            sink.Add( font_problem_code_t::BAD_FACE_FILE, iFace );
            continue;
        }
        const key_value_t *pWeight = KeyValue_Find( pFace, FontText( "weight" ) );
        if ( pWeight != nullptr ) {
            // "400" and "400u" both appear in hand-edited files.
            i64 nWeight = 0;
            u64 nUnsigned = 0u;
            bool_t bRead = KeyValue_GetI64( pWeight, &nWeight );
            if ( !bRead && KeyValue_GetU64( pWeight, &nUnsigned ) && nUnsigned <= 1000u ) {
                nWeight = static_cast<i64>( nUnsigned );
                bRead = CY_TRUE;
            }
            if ( !bRead || nWeight < 1 || nWeight > 1000 ) {
                sink.Add( font_problem_code_t::BAD_FACE_WEIGHT, iFace );
                continue;
            }
            face.nWeight = static_cast<u32>( nWeight );
        }
        const key_value_t *pItalic = KeyValue_Find( pFace, FontText( "italic" ) );
        if ( pItalic != nullptr && !KeyValue_GetBool( pItalic, &face.bItalic ) ) {
            sink.Add( font_problem_code_t::BAD_FACE_ITALIC, iFace );
            continue;
        }
        definition.faces[definition.nFaces++] = face;
    }
    if ( definition.nFaces == 0u ) {
        result.status = font_definition_status_t::NO_USABLE_FACE;
        return result;
    }

    const key_value_t *pFallbacks = KeyValue_Find( pRoot, FontText( "fallbacks" ) );
    const usize nFallbacks = KeyValue_Type( pFallbacks ) == key_value_type_t::ARRAY ? KeyValue_ChildCount( pFallbacks ) : 0u;
    for ( usize iFallback = 0u; iFallback < nFallbacks; ++iFallback ) {
        if ( definition.nFallbacks == CY_FONT_MAX_FALLBACKS ) {
            sink.Add( font_problem_code_t::TOO_MANY_FALLBACKS, iFallback );
            break;
        }
        string_view_t fallback{};
        if ( !KeyValue_GetString( KeyValue_ChildAt( pFallbacks, iFallback ), &fallback ) || !IsFallback( fallback ) ) {
            sink.Add( font_problem_code_t::BAD_FALLBACK, iFallback );
            continue;
        }
        definition.fallbacks[definition.nFallbacks++] = fallback;
    }

    const key_value_t *pMonospace = KeyValue_Find( pRoot, FontText( "monospace" ) );
    if ( pMonospace != nullptr && !KeyValue_GetBool( pMonospace, &definition.bMonospace ) ) {
        sink.Add( font_problem_code_t::BAD_MONOSPACE, CY_INVALID_SIZE );
        definition.bMonospace = CY_FALSE;
    }

    *pDefinitionOut = definition;
    return result;
}

const char *FontDefinition_StatusName( font_definition_status_t status ) noexcept
{
    switch ( status ) {
        case font_definition_status_t::OK: return "OK";
        case font_definition_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case font_definition_status_t::INVALID_HEADER: return "INVALID_HEADER";
        case font_definition_status_t::INVALID_NAME: return "INVALID_NAME";
        case font_definition_status_t::NO_USABLE_FACE: return "NO_USABLE_FACE";
    }
    return "UNKNOWN";
}

const char *FontDefinition_ProblemName( font_problem_code_t code ) noexcept
{
    switch ( code ) {
        case font_problem_code_t::NONE: return "NONE";
        case font_problem_code_t::BAD_FACE: return "BAD_FACE";
        case font_problem_code_t::BAD_FACE_FILE: return "BAD_FACE_FILE";
        case font_problem_code_t::BAD_FACE_WEIGHT: return "BAD_FACE_WEIGHT";
        case font_problem_code_t::BAD_FACE_ITALIC: return "BAD_FACE_ITALIC";
        case font_problem_code_t::TOO_MANY_FACES: return "TOO_MANY_FACES";
        case font_problem_code_t::BAD_FALLBACK: return "BAD_FALLBACK";
        case font_problem_code_t::TOO_MANY_FALLBACKS: return "TOO_MANY_FALLBACKS";
        case font_problem_code_t::BAD_MONOSPACE: return "BAD_MONOSPACE";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
