//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Core/PicassoMaterialImport.cpp
//  Purpose: Implements deterministic `.cymat` import for Picasso.
//  Details: Runtime materials may expose arbitrary shader bindings. This adapter
//           recognizes only the stable Picasso semantic vocabulary and never
//           guesses semantics from resource filenames or binding substrings.
//
//  History:
//  - Created by Karlo Siric on 2026-08-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoMaterialImport.h"

#include "CypherCommon_Allocator.h"

namespace cypher::tools::picasso
{

namespace
{

struct picasso_material_binding_t {
    const char *pName;
    picasso_channel_semantic_t semantic;
};

// These names are the portable authoring vocabulary. Additional shader inputs
// remain in `.cymat`, but require an explicit future shader-reflection mapping.
constexpr picasso_material_binding_t PICASSO_MATERIAL_BINDINGS[]{
    { "base_color",        picasso_channel_semantic_t::BASE_COLOR },
    { "normal_map",        picasso_channel_semantic_t::NORMAL },
    { "roughness",         picasso_channel_semantic_t::ROUGHNESS },
    { "metalness",         picasso_channel_semantic_t::METALNESS },
    { "ambient_occlusion", picasso_channel_semantic_t::AMBIENT_OCCLUSION },
    { "emissive",          picasso_channel_semantic_t::EMISSIVE },
    { "height",            picasso_channel_semantic_t::HEIGHT },
    { "opacity",           picasso_channel_semantic_t::OPACITY }
};

bool_t PicassoMaterialImport_FindSemantic(
    string_view_t binding,
    picasso_channel_semantic_t *pSemanticOut ) noexcept
{
    if ( pSemanticOut == nullptr ) {
        return CY_FALSE;
    }
    for ( const picasso_material_binding_t &candidate :
          PICASSO_MATERIAL_BINDINGS ) {
        if ( StringView_Equals(
                 binding,
                 StringView_FromCString( candidate.pName ) ) ) {
            *pSemanticOut = candidate.semantic;
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

picasso_material_import_status_t PicassoMaterialImport_MapMaterialStatus(
    picasso_paint_material_status_t status ) noexcept
{
    return status == picasso_paint_material_status_t::INVALID_NAME
        ? picasso_material_import_status_t::INVALID_MATERIAL_NAME
        : picasso_material_import_status_t::MATERIAL_STATE_FAILED;
}

} // namespace

picasso_material_import_result_t PicassoMaterialImport_FromDecoded(
    string_view_t materialName,
    const render_material_source_view_t &source,
    picasso_paint_material_t *pMaterialOut ) noexcept
{
    picasso_material_import_result_t result{};
    if ( pMaterialOut == nullptr || !StringView_IsValid( materialName ) ||
         !StringView_IsValid( source.shader ) ||
         source.nTextures > CY_RENDER_MATERIAL_MAX_TEXTURES ||
         source.nParameters > CY_RENDER_MATERIAL_MAX_PARAMETERS ) {
        result.status = picasso_material_import_status_t::INVALID_ARGUMENT;
        return result;
    }

    picasso_paint_material_t pending{};
    picasso_paint_material_status_t materialStatus =
        PicassoPaintMaterial_Init( &pending, materialName );
    if ( materialStatus != picasso_paint_material_status_t::OK ) {
        result.status = PicassoMaterialImport_MapMaterialStatus(
            materialStatus );
        return result;
    }
    materialStatus = PicassoPaintMaterial_SetShader(
        &pending,
        source.shader );
    if ( materialStatus != picasso_paint_material_status_t::OK ) {
        result.status = PicassoMaterialImport_MapMaterialStatus(
            materialStatus );
        return result;
    }

    picasso_channel_mask_t importedChannels = 0u;
    for ( usize iTexture = 0u;
          iTexture < source.nTextures;
          ++iTexture ) {
        const render_material_texture_view_t &texture =
            source.textures[iTexture];
        picasso_channel_semantic_t semantic{};
        if ( !PicassoMaterialImport_FindSemantic(
                 texture.binding,
                 &semantic ) ) {
            ++result.nTexturesSkipped;
            continue;
        }

        const picasso_channel_mask_t channelBit = PicassoChannel_Bit( semantic );
        if ( ( importedChannels & channelBit ) != 0u ) {
            result.status =
                picasso_material_import_status_t::DUPLICATE_SEMANTIC_BINDING;
            return result;
        }
        materialStatus = PicassoPaintMaterial_SetTexture(
            &pending,
            semantic,
            texture.texture );
        if ( materialStatus != picasso_paint_material_status_t::OK ) {
            result.status = PicassoMaterialImport_MapMaterialStatus(
                materialStatus );
            return result;
        }
        importedChannels |= channelBit;
        ++result.nTexturesImported;
    }

    // Shader parameters are intentionally retained by `.cymat`, not guessed
    // into paint-channel constants. Their meaning becomes reliable only after
    // shader reflection defines a typed binding contract.
    result.nParametersSkipped = source.nParameters;
    if ( !PicassoPaintMaterial_IsValid( &pending ) ) {
        result.status = picasso_material_import_status_t::MATERIAL_STATE_FAILED;
        return result;
    }

    *pMaterialOut = pending;
    return result;
}

picasso_material_import_result_t PicassoMaterialImport_FromText(
    string_view_t materialName,
    string_view_t sourceText,
    picasso_paint_material_t *pMaterialOut ) noexcept
{
    picasso_material_import_result_t result{};
    if ( pMaterialOut == nullptr || !StringView_IsValid( materialName ) ||
         !StringView_IsValid( sourceText ) || sourceText.pData == nullptr ) {
        result.status = picasso_material_import_status_t::INVALID_ARGUMENT;
        return result;
    }

    key_value_document_desc_t documentDesc{};
    documentDesc.pAllocator = Allocator_GetSystem();
    key_value_document_t *pDocument = KeyValue_CreateDocument( documentDesc );
    if ( pDocument == nullptr ) {
        result.status = picasso_material_import_status_t::OUT_OF_MEMORY;
        return result;
    }

    const key_value_parse_result_t parsed = KeyValue_ParseText(
        sourceText,
        {},
        pDocument );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        result.status = picasso_material_import_status_t::CYKV_PARSE_FAILED;
        result.parseStatus = parsed.status;
        result.sourceLocation = parsed.errorLocation;
        KeyValue_DestroyDocument( pDocument );
        return result;
    }

    render_material_source_view_t decodedMaterial{};
    schema_diagnostic_t diagnostic{};
    const render_asset_decode_result_t decoded = RenderMaterialSource_Decode(
        pDocument,
        {},
        &diagnostic,
        1u,
        &decodedMaterial );
    if ( !RenderAsset_DecodeSucceeded( decoded ) ) {
        result.status =
            picasso_material_import_status_t::SCHEMA_DECODE_FAILED;
        result.decodeStatus = decoded.status;
        result.schemaDiagnostic = diagnostic;
        if ( diagnostic.code ==
             schema_diagnostic_code_t::LANGUAGE_VERSION_MISMATCH ) {
            result.sourceLocation = parsed.languageVersionLocation;
        } else if ( diagnostic.code ==
                    schema_diagnostic_code_t::SCHEMA_ID_MISMATCH ) {
            result.sourceLocation = parsed.schemaIdLocation;
        } else if ( diagnostic.code ==
                    schema_diagnostic_code_t::SCHEMA_VERSION_MISMATCH ) {
            result.sourceLocation = parsed.schemaVersionLocation;
        }
        KeyValue_DestroyDocument( pDocument );
        return result;
    }

    result = PicassoMaterialImport_FromDecoded(
        materialName,
        decodedMaterial,
        pMaterialOut );
    KeyValue_DestroyDocument( pDocument );
    return result;
}

const char *PicassoMaterialImport_StatusName(
    picasso_material_import_status_t status ) noexcept
{
    switch ( status ) {
        case picasso_material_import_status_t::OK: return "OK";
        case picasso_material_import_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case picasso_material_import_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case picasso_material_import_status_t::CYKV_PARSE_FAILED: return "CYKV_PARSE_FAILED";
        case picasso_material_import_status_t::SCHEMA_DECODE_FAILED: return "SCHEMA_DECODE_FAILED";
        case picasso_material_import_status_t::INVALID_MATERIAL_NAME: return "INVALID_MATERIAL_NAME";
        case picasso_material_import_status_t::DUPLICATE_SEMANTIC_BINDING: return "DUPLICATE_SEMANTIC_BINDING";
        case picasso_material_import_status_t::MATERIAL_STATE_FAILED: return "MATERIAL_STATE_FAILED";
    }
    return "UNKNOWN";
}

} // namespace cypher::tools::picasso
