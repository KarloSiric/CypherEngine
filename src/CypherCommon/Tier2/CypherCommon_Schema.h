//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_Schema.h
//  Purpose: Declares immutable CYKV schema descriptors and bounded validation.
//  Details: Tier2 schemas describe structural and scalar constraints without owning
//           runtime memory. Validation writes into caller-provided diagnostic storage
//           so engine, command-line, and Mason callers share one deterministic API.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER2_SCHEMA_H
#define CYPHER_COMMON_TIER2_SCHEMA_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_KeyValue.h"

namespace cypher::common
{

inline constexpr usize CY_SCHEMA_MAX_PATH = 512u; // Diagnostic path bytes including NUL.
inline constexpr usize CY_SCHEMA_MAX_DESCRIPTOR_DEPTH = 128u; // Rule-graph traversal bound.

enum schema_type_flags_t : flags32_t {
    SCHEMA_TYPE_NONE   = 0u,              // Rule accepts no value type.
    SCHEMA_TYPE_NULL   = CYPHER_BIT32( 0 ), // CYKV null.
    SCHEMA_TYPE_BOOL   = CYPHER_BIT32( 1 ), // CYKV Boolean.
    SCHEMA_TYPE_I64    = CYPHER_BIT32( 2 ), // Signed integer.
    SCHEMA_TYPE_U64    = CYPHER_BIT32( 3 ), // Unsigned integer.
    SCHEMA_TYPE_F64    = CYPHER_BIT32( 4 ), // Finite floating-point value.
    SCHEMA_TYPE_STRING = CYPHER_BIT32( 5 ), // UTF-8 string.
    SCHEMA_TYPE_BINARY = CYPHER_BIT32( 6 ), // Opaque byte block.
    SCHEMA_TYPE_OBJECT = CYPHER_BIT32( 7 ), // Named child members.
    SCHEMA_TYPE_ARRAY  = CYPHER_BIT32( 8 ), // Ordered unnamed elements.
    SCHEMA_TYPE_NUMBER = SCHEMA_TYPE_I64 | SCHEMA_TYPE_U64 | SCHEMA_TYPE_F64, // Any number.
    SCHEMA_TYPE_ANY    = SCHEMA_TYPE_NULL | SCHEMA_TYPE_BOOL |
                         SCHEMA_TYPE_I64 | SCHEMA_TYPE_U64 |
                         SCHEMA_TYPE_F64 | SCHEMA_TYPE_STRING |
                         SCHEMA_TYPE_BINARY | SCHEMA_TYPE_OBJECT |
                         SCHEMA_TYPE_ARRAY
};

enum schema_member_flags_t : flags32_t {
    SCHEMA_MEMBER_NONE       = 0u, // Optional current member.
    SCHEMA_MEMBER_REQUIRED   = CYPHER_BIT32( 0 ), // Absence is an error.
    SCHEMA_MEMBER_DEPRECATED = CYPHER_BIT32( 1 )  // Presence may emit a warning.
};

enum schema_object_flags_t : flags32_t {
    SCHEMA_OBJECT_NONE                   = 0u, // Unknown members may be ignored.
    SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS = CYPHER_BIT32( 0 ) // Closed object.
};

struct schema_rule_t;

struct schema_member_t {
    string_view_t name{};                 // Static member name.
    const schema_rule_t *pRule{ nullptr }; // Borrowed immutable value rule.
    flags32_t flags{ SCHEMA_MEMBER_NONE }; // schema_member_flags_t bits.
};

struct schema_object_rules_t {
    const schema_member_t *pMembers{ nullptr }; // Static named-member table.
    usize nMembers{ 0u };                       // Entries in pMembers.
    const schema_rule_t *pAdditionalMemberRule{ nullptr }; // Dynamic member rule.
    usize nMinMembers{ 0u };                    // Inclusive total-child floor.
    usize nMaxMembers{ CY_INVALID_SIZE };       // Inclusive total-child ceiling.
    flags32_t flags{ SCHEMA_OBJECT_NONE };      // schema_object_flags_t bits.
};

struct schema_array_rules_t {
    const schema_rule_t *pElementRule{ nullptr }; // Rule shared by every element.
    usize nMinElements{ 0u };                     // Inclusive element floor.
    usize nMaxElements{ CY_INVALID_SIZE };        // Inclusive element ceiling.
};

struct schema_string_rules_t {
    usize cbMinLength{ 0u };                 // Inclusive UTF-8 byte-length floor.
    usize cbMaxLength{ CY_INVALID_SIZE };    // Inclusive UTF-8 byte-length ceiling.
    const string_view_t *pAllowedValues{ nullptr }; // Optional exact-value set.
    usize nAllowedValues{ 0u };              // Entries in pAllowedValues.
};

struct schema_binary_rules_t {
    usize cbMinSize{ 0u };              // Inclusive binary-byte floor.
    usize cbMaxSize{ CY_INVALID_SIZE }; // Inclusive binary-byte ceiling.
};

struct schema_i64_rules_t {
    i64 nMin{ CY_I64_MIN }; // Inclusive signed minimum.
    i64 nMax{ CY_I64_MAX }; // Inclusive signed maximum.
};

struct schema_u64_rules_t {
    u64 nMin{ 0u };         // Inclusive unsigned minimum.
    u64 nMax{ CY_U64_MAX }; // Inclusive unsigned maximum.
};

struct schema_f64_rules_t {
    f64 flMin{ -CY_F64_MAX }; // Inclusive finite minimum.
    f64 flMax{ CY_F64_MAX };  // Inclusive finite maximum.
};

// Constraints are applied only when the corresponding value type is allowed.
struct schema_rule_t {
    flags32_t allowedTypes{ SCHEMA_TYPE_NONE }; // schema_type_flags_t mask.
    schema_object_rules_t object{};             // Active when OBJECT is allowed.
    schema_array_rules_t array{};               // Active when ARRAY is allowed.
    schema_string_rules_t string{};             // Active when STRING is allowed.
    schema_binary_rules_t binary{};             // Active when BINARY is allowed.
    schema_i64_rules_t signedInteger{};         // Active when I64 is allowed.
    schema_u64_rules_t unsignedInteger{};       // Active when U64 is allowed.
    schema_f64_rules_t floatingPoint{};         // Active when F64 is allowed.
};

struct schema_descriptor_t {
    string_view_t schemaId{};             // Stable dotted ID, for example cypher.map.
    u32 nVersion{ 0u };                   // Exact schema version; zero is invalid.
    const schema_rule_t *pRootRule{ nullptr }; // Borrowed immutable root graph.
};

enum class schema_descriptor_status_t : u8 {
    OK = 0u,              // Descriptor graph is structurally valid.
    INVALID_ARGUMENT,    // Descriptor or root rule is null.
    INVALID_SCHEMA_ID,   // Stable dotted schema identity is malformed.
    INVALID_VERSION,     // Schema version is zero.
    INVALID_TYPE_MASK,   // Rule accepts no type or unknown type bits.
    INVALID_RULE,        // Rule fields conflict with their allowed types.
    INVALID_MEMBER,      // Member name, rule, or flags are invalid.
    DUPLICATE_MEMBER,    // Object declares the same fixed member twice.
    INVALID_RANGE,       // Minimum exceeds maximum or float bound is non-finite.
    DESCRIPTOR_DEPTH_LIMIT // Rule graph exceeds bounded validation depth.
};

enum class schema_diagnostic_severity_t : u8 {
    WARNING = 0u, // Document remains valid but uses discouraged data.
    ERROR         // Document fails validation.
};

enum class schema_diagnostic_code_t : u8 {
    NONE = 0u,                // No diagnostic.
    LANGUAGE_VERSION_MISMATCH,// CYKV language generation differs.
    SCHEMA_ID_MISMATCH,      // Document declares another schema identity.
    SCHEMA_VERSION_MISMATCH, // Document declares another schema version.
    TYPE_MISMATCH,           // Node type is outside allowedTypes.
    MISSING_REQUIRED_MEMBER, // Required fixed object member is absent.
    UNKNOWN_MEMBER,          // Closed object contains an undeclared member.
    DEPRECATED_MEMBER,       // Deprecated member was present.
    I64_RANGE,               // Signed value lies outside inclusive bounds.
    U64_RANGE,               // Unsigned value lies outside inclusive bounds.
    F64_RANGE,               // Float is non-finite or outside inclusive bounds.
    STRING_LENGTH,           // UTF-8 byte length lies outside bounds.
    STRING_VALUE,            // String is outside the exact allowed-value set.
    BINARY_SIZE,             // Binary payload size lies outside bounds.
    ARRAY_LENGTH,            // Array element count lies outside bounds.
    OBJECT_LENGTH,           // Object member count lies outside bounds.
    PATH_LIMIT,              // Escaped diagnostic path exceeds storage.
    DEPTH_LIMIT,             // Document nesting exceeds configured depth.
    NODE_LIMIT,              // Validation exhausted its node budget.
    SCHEMA_NOT_FOUND         // Registry has no exact ID/version descriptor.
};

struct schema_diagnostic_t {
    schema_diagnostic_code_t code{ schema_diagnostic_code_t::NONE }; // Stable reason.
    schema_diagnostic_severity_t severity{
        schema_diagnostic_severity_t::ERROR
    };
    flags32_t expectedTypes{ SCHEMA_TYPE_NONE }; // Expected schema_type_flags_t mask.
    key_value_type_t actualType{ key_value_type_t::NULL_VALUE }; // Observed CYKV type.
    char path[CY_SCHEMA_MAX_PATH]{}; // NUL-terminated escaped path to the node.
};

struct schema_validation_options_t {
    usize nMaxDepth{ 128u };               // Maximum document nesting depth.
    usize nMaxNodes{ 1u * CY_MIB };        // Maximum values visited in one pass.
    bool_t bReportDeprecatedMembers{ CY_TRUE }; // Emit warnings for old members.
};

enum class schema_validation_status_t : u8 {
    OK = 0u,          // Document satisfies the selected schema.
    INVALID_ARGUMENT,// Document, diagnostics, or options are invalid.
    INVALID_SCHEMA,  // Descriptor graph itself is malformed.
    INVALID_DOCUMENT,// One or more validation errors were emitted.
    SCHEMA_NOT_FOUND // Registry could not resolve exact document metadata.
};

struct schema_validation_result_t {
    schema_validation_status_t status{ schema_validation_status_t::OK }; // Overall result.
    usize nDiagnosticsRequired{ 0u }; // Total diagnostics independent of capacity.
    usize nDiagnosticsWritten{ 0u };  // Entries committed to caller storage.
    usize nErrors{ 0u };              // Error-severity count.
    usize nWarnings{ 0u };            // Warning-severity count.
    usize nNodesVisited{ 0u };        // Values examined before any node limit.
    bool_t bDiagnosticsTruncated{ CY_FALSE }; // Required count exceeded capacity.
};

// Returns the schema mask corresponding to one exact CYKV value type.
CYPHER_NODISCARD CYPHER_COMMON_API
flags32_t Schema_TypeFlag( key_value_type_t type ) noexcept;

// Validates descriptor structure, ranges, members, and recursive references.
CYPHER_NODISCARD CYPHER_COMMON_API
schema_descriptor_status_t Schema_CheckDescriptor(
    const schema_descriptor_t *pSchema ) noexcept;

// Validates one parsed document without modifying it or allocating memory.
CYPHER_NODISCARD CYPHER_COMMON_API
schema_validation_result_t Schema_ValidateDocument(
    const schema_descriptor_t *pSchema,
    const key_value_document_t *pDocument,
    const schema_validation_options_t &options,
    schema_diagnostic_t *pDiagnostics,
    usize nDiagnosticCapacity ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Schema_ValidationSucceeded(
    const schema_validation_result_t &result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *Schema_DescriptorStatusName(
    schema_descriptor_status_t status ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *Schema_ValidationStatusName(
    schema_validation_status_t status ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *Schema_DiagnosticCodeName(
    schema_diagnostic_code_t code ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER2_SCHEMA_H
