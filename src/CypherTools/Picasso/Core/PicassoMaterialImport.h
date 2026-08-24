//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Core/PicassoMaterialImport.h
//  Purpose: Declares deterministic `.cymat` import into Picasso authoring state.
//  Details: CYKV parsing and renderer-format validation happen before a material
//           is published. Only documented semantic bindings are interpreted;
//           shader-specific bindings remain valid but are reported as skipped.
//
//  History:
//  - Created by Karlo Siric on 2026-08-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_PICASSO_MATERIALIMPORT_H
#define CYPHER_TOOLS_PICASSO_MATERIALIMPORT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "PicassoPaintMaterial.h"

#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_RenderAsset.h"

namespace cypher::tools::picasso
{

enum class picasso_material_import_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    OUT_OF_MEMORY,
    CYKV_PARSE_FAILED,
    SCHEMA_DECODE_FAILED,
    INVALID_MATERIAL_NAME,
    DUPLICATE_SEMANTIC_BINDING,
    MATERIAL_STATE_FAILED
};

// The result owns all diagnostic data needed after the temporary CYKV document
// has been destroyed. Counts distinguish imported standard slots from valid,
// shader-specific data that Picasso deliberately did not reinterpret.
struct picasso_material_import_result_t {
    picasso_material_import_status_t status{
        picasso_material_import_status_t::OK
    };
    key_value_parse_status_t parseStatus{ key_value_parse_status_t::OK };
    render_asset_decode_status_t decodeStatus{
        render_asset_decode_status_t::OK
    };
    text_location_t sourceLocation{};
    schema_diagnostic_t schemaDiagnostic{};
    usize nTexturesImported{ 0u };
    usize nTexturesSkipped{ 0u };
    usize nParametersSkipped{ 0u };
};

// Converts an already validated renderer material view. The output is unchanged
// on failure, which lets editors keep the previously selected material alive.
CYPHER_NODISCARD picasso_material_import_result_t
PicassoMaterialImport_FromDecoded(
    string_view_t materialName,
    const render_material_source_view_t &source,
    picasso_paint_material_t *pMaterialOut ) noexcept;

// Parses CYKV text, validates schema `cypher.material` version 1, then calls the
// decoded adapter above. Source text may be released immediately after return.
CYPHER_NODISCARD picasso_material_import_result_t
PicassoMaterialImport_FromText(
    string_view_t materialName,
    string_view_t sourceText,
    picasso_paint_material_t *pMaterialOut ) noexcept;

CYPHER_NODISCARD const char *PicassoMaterialImport_StatusName(
    picasso_material_import_status_t status ) noexcept;

} // namespace cypher::tools::picasso

#endif // CYPHER_TOOLS_PICASSO_MATERIALIMPORT_H
