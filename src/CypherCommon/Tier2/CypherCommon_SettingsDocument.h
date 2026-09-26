//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_SettingsDocument.h
//  Purpose: Declares the settings-family document store: an owned, editable
//           CYKV document plus typed setting descriptors, per-value reads,
//           scope resolution, and sparse writes.
//  Details: ADR 0009 requires settings-family files (.cysettings, the settings
//           blocks of .cyproject and .cyworkspace, .cytheme, .cykeymap) to
//           never reset: one invalid value falls back alone, members the
//           current build does not know are written back unchanged, and only
//           values that differ from their inherited value are stored. Keeping
//           the parsed document whole - rather than decoding into a struct -
//           is what preserves unknown members; descriptors give the known
//           values their types, limits, and defaults.
//
//           No file I/O happens here; callers own paths, backups, and the
//           unparseable-file policy.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER2_SETTINGSDOCUMENT_H
#define CYPHER_COMMON_TIER2_SETTINGSDOCUMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"
#include "CypherCommon/Tier1/CypherCommon_StringView.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

namespace cypher::common
{

inline constexpr usize CY_SETTINGS_PATH_MAX_SEGMENTS = 8u;   // Nesting bound for one setting path.
inline constexpr usize CY_SETTINGS_SEGMENT_MAX_LENGTH = 96u; // Bytes in one path segment.
inline constexpr usize CY_SETTINGS_TEXT_MAX_BYTES = 16u * CY_MIB; // Largest settings-family file.

/*
================
Setting Paths

A path names one value through nested objects. Dotted text such as
"editor.viewport.grid_size" splits into segments; a segment that itself
contains dots (theme token names like "ui.background") is added verbatim with
SettingsPath_Append.
================
*/
struct settings_path_t {
    string_view_t segments[CY_SETTINGS_PATH_MAX_SEGMENTS]{}; // Borrowed segment text.
    usize nSegments{ 0u };                                   // Active entries.
};

// Splits dotted text into segments. Rejects empty text, empty segments, more
// than CY_SETTINGS_PATH_MAX_SEGMENTS segments, and over-long segments.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t SettingsPath_Parse( string_view_t dottedPath, settings_path_t *pPathOut ) noexcept;

// Appends one verbatim segment. The text is borrowed, not copied.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t SettingsPath_Append( settings_path_t *pPath, string_view_t segment ) noexcept;

/*
================
Document Identity And Store
================
*/
struct settings_document_identity_t {
    string_view_t schemaId{};  // Exact schema, for example "cypher.settings".
    u32 nOldestVersion{ 1u };  // Oldest version still read.
    u32 nCurrentVersion{ 1u }; // Version written by this build.
};

enum class settings_document_status_t : u8 {
    OK = 0u,            // Operation completed.
    INVALID_ARGUMENT,   // Null pointer, uninitialized store, or bad path.
    OUT_OF_MEMORY,      // Allocation failed; the store is unchanged.
    TEXT_TOO_LARGE,     // Source or output exceeds CY_SETTINGS_TEXT_MAX_BYTES.
    PARSE_FAILED,       // Text is not CYKV; see parseStatus and location.
    LANGUAGE_MISMATCH,  // Unsupported @cykv language version.
    SCHEMA_MISMATCH,    // @schema names another document family.
    UNSUPPORTED_VERSION,// @schema version outside [oldest, current].
    ROOT_NOT_OBJECT,    // The document root is not an object.
    PATH_BLOCKED,       // A path segment crosses a non-object value.
    WRITE_FAILED        // CYKV serialization failed.
};

struct settings_document_t {
    settings_document_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( settings_document_t );
    ~settings_document_t() noexcept;

    key_value_document_t *pDocument{ nullptr };   // Owned CYKV tree, header included.
    const allocator_t *pAllocator{ nullptr };     // Allocator for the tree.
    settings_document_identity_t identity{};      // Accepted identity.
    u32 nLoadedVersion{ 0u };                     // Version the source text declared.
};

struct settings_document_load_result_t {
    settings_document_status_t status{ settings_document_status_t::OK };
    key_value_parse_status_t parseStatus{ key_value_parse_status_t::OK };
    text_location_t location{};       // Failure location, when known.
    u32 nDeclaredVersion{ 0u };       // Schema version the text declared.
};

// Creates an empty document (root object) with the current header.
CYPHER_NODISCARD CYPHER_COMMON_API
settings_document_status_t SettingsDocument_Init(
    settings_document_t *pStore,
    const allocator_t *pAllocator,
    const settings_document_identity_t &identity ) noexcept;

CYPHER_COMMON_API void SettingsDocument_Shutdown( settings_document_t *pStore ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t SettingsDocument_IsInitialized( const settings_document_t *pStore ) noexcept;

// Parses text into the store. On any failure the store keeps its previous
// contents, so a caller never replaces good settings with a broken file.
CYPHER_NODISCARD CYPHER_COMMON_API
settings_document_load_result_t SettingsDocument_Load(
    settings_document_t *pStore,
    string_view_t text ) noexcept;

// Writes the whole document, unknown members included, under the current
// schema version. pTextOut is replaced only on success.
CYPHER_NODISCARD CYPHER_COMMON_API
settings_document_status_t SettingsDocument_Write(
    const settings_document_t *pStore,
    text_buffer_t *pTextOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const key_value_t *SettingsDocument_Root( const settings_document_t *pStore ) noexcept;

// Finds the value at a path below pBase (an object); nullptr when absent.
CYPHER_NODISCARD CYPHER_COMMON_API
const key_value_t *SettingsNode_Find(
    const key_value_t *pBase,
    const settings_path_t &path ) noexcept;

// Returns the node at a path, creating missing parent objects and a null
// leaf. The caller then sets the leaf through pStore->pDocument. A scalar in
// the way is PATH_BLOCKED and nothing is created.
CYPHER_NODISCARD CYPHER_COMMON_API
settings_document_status_t SettingsDocument_Ensure(
    settings_document_t *pStore,
    const settings_path_t &path,
    key_value_t **ppNodeOut ) noexcept;

// Removes the value at a path and then any objects the removal left empty.
// Absent values succeed.
CYPHER_NODISCARD CYPHER_COMMON_API
settings_document_status_t SettingsDocument_Remove(
    settings_document_t *pStore,
    const settings_path_t &path ) noexcept;

/*
================
Typed Settings

A descriptor states what one known value is. Reads never fail a document:
an absent value reports ABSENT, a present value of the wrong type or outside
its limits reports INVALID with a problem code, and the caller moves on to the
next scope or the default.
================
*/
enum class setting_type_t : u8 {
    BOOL = 0u, // CYKV Boolean.
    INTEGER,   // Signed integer within [nMin, nMax].
    REAL,      // Finite number within [flMin, flMax]; integers are accepted.
    STRING,    // UTF-8 text of at most cbMaxText bytes.
    ENUM,      // One exact string from ppEnumValues.
    COLOR      // "#rrggbb" or "#rrggbbaa" text, stored as 0xRRGGBBAA.
};

struct setting_descriptor_t {
    const char *pPath{ nullptr };        // Dotted path, for example "display.width".
    setting_type_t type{ setting_type_t::BOOL };
    bool_t bDefault{ CY_FALSE };         // BOOL default.
    i64 nDefault{ 0 };                   // INTEGER default.
    f64 flDefault{ 0.0 };                // REAL default.
    const char *pDefaultText{ "" };      // STRING and ENUM default.
    u32 rgbaDefault{ 0x000000FFu };      // COLOR default.
    i64 nMin{ CY_I64_MIN };              // INTEGER inclusive minimum.
    i64 nMax{ CY_I64_MAX };              // INTEGER inclusive maximum.
    f64 flMin{ -CY_F64_MAX };            // REAL inclusive minimum.
    f64 flMax{ CY_F64_MAX };             // REAL inclusive maximum.
    const char *const *ppEnumValues{ nullptr }; // ENUM allowed values.
    usize nEnumValues{ 0u };
    usize cbMaxText{ 256u };             // STRING byte limit.
    const char *pLabel{ nullptr };       // UI label.
    const char *pDescription{ nullptr }; // UI help text.
    const char *pPage{ nullptr };        // Settings page, for example "Editor/Viewport".
};

struct setting_value_t {
    setting_type_t type{ setting_type_t::BOOL };
    bool_t bValue{ CY_FALSE };
    i64 nValue{ 0 };
    f64 flValue{ 0.0 };
    string_view_t text{}; // STRING/ENUM; borrows the document or descriptor.
    u32 rgba{ 0u };       // COLOR as 0xRRGGBBAA.
};

enum class setting_read_status_t : u8 {
    VALUE = 0u, // A valid value was read.
    ABSENT,     // The scope does not hold the value.
    INVALID     // The scope holds an unusable value; see the problem code.
};

enum class setting_problem_code_t : u8 {
    NONE = 0u,
    WRONG_TYPE,    // CYKV type does not match the descriptor.
    OUT_OF_RANGE,  // Number outside the descriptor limits or non-finite.
    UNKNOWN_ENUM,  // String is not one of the allowed values.
    BAD_COLOR,     // Text is not "#rrggbb" or "#rrggbbaa".
    TEXT_TOO_LONG, // String exceeds cbMaxText.
    BAD_PATH       // Descriptor path is malformed or crosses a non-object.
};

struct setting_problem_t {
    setting_problem_code_t code{ setting_problem_code_t::NONE };
    const setting_descriptor_t *pDescriptor{ nullptr }; // Which setting.
    usize iScope{ CY_INVALID_SIZE };                     // Which scope held it.
};

// The descriptor's default as a value.
CYPHER_NODISCARD CYPHER_COMMON_API
setting_value_t Setting_Default( const setting_descriptor_t &descriptor ) noexcept;

// Reads one value below pBase (an object, usually a document root or a
// settings block). pProblemOut may be null.
CYPHER_NODISCARD CYPHER_COMMON_API
setting_read_status_t Setting_Read(
    const key_value_t *pBase,
    const setting_descriptor_t &descriptor,
    setting_value_t *pValueOut,
    setting_problem_code_t *pProblemOut ) noexcept;

struct setting_resolution_t {
    setting_value_t value{};             // Resolved value.
    usize iScope{ CY_INVALID_SIZE };     // Scope that supplied it; CY_INVALID_SIZE = default.
    usize nProblemsRequired{ 0u };       // Invalid values met, independent of capacity.
    usize nProblemsWritten{ 0u };        // Problems stored in the caller's array.
};

// Resolves a value through scopes ordered most specific first. Null scope
// entries are skipped. Invalid values are recorded and passed over.
CYPHER_NODISCARD CYPHER_COMMON_API
setting_resolution_t Setting_Resolve(
    const key_value_t *const *ppScopes,
    usize nScopes,
    const setting_descriptor_t &descriptor,
    setting_problem_t *pProblems,
    usize nProblemCapacity ) noexcept;

// Validates a value against its descriptor without touching any document.
CYPHER_NODISCARD CYPHER_COMMON_API
setting_problem_code_t Setting_Check(
    const setting_descriptor_t &descriptor,
    const setting_value_t &value ) noexcept;

// Stores one value at the descriptor path, creating parent objects. An
// invalid value is refused (INVALID_ARGUMENT) and the document is unchanged.
CYPHER_NODISCARD CYPHER_COMMON_API
settings_document_status_t Setting_Write(
    settings_document_t *pStore,
    const setting_descriptor_t &descriptor,
    const setting_value_t &value ) noexcept;

// Keeps a scope sparse: stores the value when it differs from the inherited
// value, otherwise removes the scope's own entry so it inherits again.
CYPHER_NODISCARD CYPHER_COMMON_API
settings_document_status_t Setting_WriteOverride(
    settings_document_t *pStore,
    const setting_descriptor_t &descriptor,
    const setting_value_t &value,
    const setting_value_t &inheritedValue ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Setting_ValuesEqual( const setting_value_t &a, const setting_value_t &b ) noexcept;

/*
================
Colours
================
*/
// Parses "#rrggbb" (alpha 0xFF) or "#rrggbbaa", hex digits in either case.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t SettingColor_Parse( string_view_t text, u32 *pRgbaOut ) noexcept;

// Formats as lower-case "#rrggbb" when opaque, otherwise "#rrggbbaa".
// Returns the character count; pBuffer receives a NUL terminator.
CYPHER_COMMON_API
usize SettingColor_Format( u32 rgba, char ( &buffer )[10] ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *SettingsDocument_StatusName( settings_document_status_t status ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *Setting_ProblemName( setting_problem_code_t code ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER2_SETTINGSDOCUMENT_H
