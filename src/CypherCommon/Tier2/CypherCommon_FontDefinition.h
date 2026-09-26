//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_FontDefinition.h
//  Purpose: Declares the `.cyfont` family definition (cypher.font V1): the
//           face files of one font family, their weights and styles, and an
//           ordered fallback chain.
//  Details: One definition serves the editor (which loads the faces for its
//           UI, console, and code views) and, later, the runtime font compiler
//           (which will add atlas and glyph-range fields in a later version).
//           Decoding is tolerant per face and per fallback: an unusable entry
//           is skipped and reported, and the definition fails only when no
//           usable face remains.
//
//             @cykv 1
//             @schema "cypher.font" 1
//             {
//                 name = "Inter"
//                 faces = [
//                     { file = "fonts/inter/inter-regular.ttf" weight = 400 },
//                     { file = "fonts/inter/inter-italic.ttf" weight = 400 italic = true }
//                 ]
//                 fallbacks = [ "fonts/noto/noto_sans.cyfont", "system:Helvetica" ]
//                 monospace = false
//             }
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER2_FONTDEFINITION_H
#define CYPHER_COMMON_TIER2_FONTDEFINITION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_SettingsDocument.h"

namespace cypher::common
{

inline constexpr u32 CY_FONT_SCHEMA_VERSION = 1u;          // cypher.font generation.
inline constexpr usize CY_FONT_NAME_MAX_LENGTH = 64u;       // Family display-name bytes.
inline constexpr usize CY_FONT_MAX_FACES = 32u;             // Faces in one family.
inline constexpr usize CY_FONT_MAX_FALLBACKS = 8u;          // Fallback chain length.
inline constexpr usize CY_FONT_PATH_MAX_LENGTH = 259u;      // Virtual path bytes.
inline constexpr u32 CY_FONT_WEIGHT_DEFAULT = 400u;         // Regular.

struct font_face_view_t {
    string_view_t file{};                   // Canonical virtual path to .ttf or .otf.
    u32 nWeight{ CY_FONT_WEIGHT_DEFAULT };  // 1..1000; 400 regular, 700 bold.
    bool_t bItalic{ CY_FALSE };
};

// Zero-copy: every string borrows the parsed document.
struct font_definition_view_t {
    string_view_t name{};
    font_face_view_t faces[CY_FONT_MAX_FACES]{};
    usize nFaces{ 0u };
    // A fallback is a .cyfont virtual path or "system:<family>" for an
    // installed OS font.
    string_view_t fallbacks[CY_FONT_MAX_FALLBACKS]{};
    usize nFallbacks{ 0u };
    bool_t bMonospace{ CY_FALSE };
};

enum class font_definition_status_t : u8 {
    OK = 0u,          // Decoded; skipped entries are listed as problems.
    INVALID_ARGUMENT, // Null document or output, or bad problem storage.
    INVALID_HEADER,   // Not cypher.font V1 in CYKV 1.
    INVALID_NAME,     // name missing, not a string, or out of bounds.
    NO_USABLE_FACE    // faces missing, empty, or every face was skipped.
};

enum class font_problem_code_t : u8 {
    NONE = 0u,
    BAD_FACE,         // Face is not an object or lacks a string file.
    BAD_FACE_FILE,    // File is not a canonical .ttf/.otf virtual path.
    BAD_FACE_WEIGHT,  // Weight is not an integer in 1..1000.
    BAD_FACE_ITALIC,  // italic is not a Boolean.
    TOO_MANY_FACES,   // Faces beyond CY_FONT_MAX_FACES were ignored.
    BAD_FALLBACK,     // Fallback is neither a .cyfont path nor "system:<family>".
    TOO_MANY_FALLBACKS,
    BAD_MONOSPACE     // monospace is not a Boolean.
};

struct font_problem_t {
    font_problem_code_t code{ font_problem_code_t::NONE };
    usize iElement{ CY_INVALID_SIZE }; // Face or fallback index, when applicable.
};

struct font_definition_decode_result_t {
    font_definition_status_t status{ font_definition_status_t::OK };
    usize nProblemsRequired{ 0u };
    usize nProblemsWritten{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
settings_document_identity_t FontDefinition_Identity() noexcept;

// pDefinitionOut is written only when the status is OK.
CYPHER_NODISCARD CYPHER_COMMON_API
font_definition_decode_result_t FontDefinition_Decode(
    const key_value_document_t *pDocument,
    font_problem_t *pProblems,
    usize nProblemCapacity,
    font_definition_view_t *pDefinitionOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *FontDefinition_StatusName( font_definition_status_t status ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *FontDefinition_ProblemName( font_problem_code_t code ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER2_FONTDEFINITION_H
