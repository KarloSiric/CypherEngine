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

inline constexpr usize CY_SCHEMA_MAX_PATH = 512u;
inline constexpr usize CY_SCHEMA_MAX_DESCRIPTOR_DEPTH = 128u;

enum schema_type_flags_t : flags32_t {
    SCHEMA_TYPE_NONE   = 0u,
    SCHEMA_TYPE_NULL   = CYPHER_BIT32( 0 ),
    SCHEMA_TYPE_BOOL   = CYPHER_BIT32( 1 ),
    SCHEMA_TYPE_I64    = CYPHER_BIT32( 2 ),
    SCHEMA_TYPE_U64    = CYPHER_BIT32( 3 ),
    SCHEMA_TYPE_F64    = CYPHER_BIT32( 4 ),
    SCHEMA_TYPE_STRING = CYPHER_BIT32( 5 ),
    SCHEMA_TYPE_BINARY = CYPHER_BIT32( 6 ),
    SCHEMA_TYPE_OBJECT = CYPHER_BIT32( 7 ),
    SCHEMA_TYPE_ARRAY  = CYPHER_BIT32( 8 ),
    SCHEMA_TYPE_NUMBER = SCHEMA_TYPE_I64 | SCHEMA_TYPE_U64 | SCHEMA_TYPE_F64,
    SCHEMA_TYPE_ANY    = SCHEMA_TYPE_NULL | SCHEMA_TYPE_BOOL |
                         SCHEMA_TYPE_I64 | SCHEMA_TYPE_U64 |
                         SCHEMA_TYPE_F64 | SCHEMA_TYPE_STRING |
                         SCHEMA_TYPE_BINARY | SCHEMA_TYPE_OBJECT |
                         SCHEMA_TYPE_ARRAY
};

enum schema_member_flags_t : flags32_t {
    SCHEMA_MEMBER_NONE       = 0u,
    SCHEMA_MEMBER_REQUIRED   = CYPHER_BIT32( 0 ),
    SCHEMA_MEMBER_DEPRECATED = CYPHER_BIT32( 1 )
};

enum schema_object_flags_t : flags32_t {
    SCHEMA_OBJECT_NONE                   = 0u,
    SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS = CYPHER_BIT32( 0 )
};

struct schema_rule_t;

struct schema_member_t {
    string_view_t name{};
    const schema_rule_t *pRule{ nullptr };
    flags32_t flags{ SCHEMA_MEMBER_NONE };
};

struct schema_object_rules_t {
    const schema_member_t *pMembers{ nullptr };
    usize nMembers{ 0u };
    flags32_t flags{ SCHEMA_OBJECT_NONE };
};

struct schema_array_rules_t {
    const schema_rule_t *pElementRule{ nullptr };
    usize nMinElements{ 0u };
    usize nMaxElements{ CY_INVALID_SIZE };
};

struct schema_string_rules_t {
    usize cbMinLength{ 0u };
    usize cbMaxLength{ CY_INVALID_SIZE };
    const string_view_t *pAllowedValues{ nullptr };
    usize nAllowedValues{ 0u };
};

struct schema_binary_rules_t {
    usize cbMinSize{ 0u };
    usize cbMaxSize{ CY_INVALID_SIZE };
};

struct schema_i64_rules_t {
    i64 nMin{ CY_I64_MIN };
    i64 nMax{ CY_I64_MAX };
};

struct schema_u64_rules_t {
    u64 nMin{ 0u };
    u64 nMax{ CY_U64_MAX };
};

struct schema_f64_rules_t {
    f64 flMin{ -CY_F64_MAX };
    f64 flMax{ CY_F64_MAX };
};

// Constraints are applied only when the corresponding value type is allowed.
struct schema_rule_t {
    flags32_t allowedTypes{ SCHEMA_TYPE_NONE };
    schema_object_rules_t object{};
    schema_array_rules_t array{};
    schema_string_rules_t string{};
    schema_binary_rules_t binary{};
    schema_i64_rules_t signedInteger{};
    schema_u64_rules_t unsignedInteger{};
    schema_f64_rules_t floatingPoint{};
};

struct schema_descriptor_t {
    string_view_t schemaId{};
    u32 nVersion{ 0u };
    const schema_rule_t *pRootRule{ nullptr };
};

enum class schema_descriptor_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_SCHEMA_ID,
    INVALID_VERSION,
    INVALID_TYPE_MASK,
    INVALID_RULE,
    INVALID_MEMBER,
    DUPLICATE_MEMBER,
    INVALID_RANGE,
    DESCRIPTOR_DEPTH_LIMIT
};

enum class schema_diagnostic_severity_t : u8 {
    WARNING = 0u,
    ERROR
};

enum class schema_diagnostic_code_t : u8 {
    NONE = 0u,
    LANGUAGE_VERSION_MISMATCH,
    SCHEMA_ID_MISMATCH,
    SCHEMA_VERSION_MISMATCH,
    TYPE_MISMATCH,
    MISSING_REQUIRED_MEMBER,
    UNKNOWN_MEMBER,
    DEPRECATED_MEMBER,
    I64_RANGE,
    U64_RANGE,
    F64_RANGE,
    STRING_LENGTH,
    STRING_VALUE,
    BINARY_SIZE,
    ARRAY_LENGTH,
    PATH_LIMIT,
    DEPTH_LIMIT,
    NODE_LIMIT,
    SCHEMA_NOT_FOUND
};

struct schema_diagnostic_t {
    schema_diagnostic_code_t code{ schema_diagnostic_code_t::NONE };
    schema_diagnostic_severity_t severity{
        schema_diagnostic_severity_t::ERROR
    };
    flags32_t expectedTypes{ SCHEMA_TYPE_NONE };
    key_value_type_t actualType{ key_value_type_t::NULL_VALUE };
    char path[CY_SCHEMA_MAX_PATH]{};
};

struct schema_validation_options_t {
    usize nMaxDepth{ 128u };
    usize nMaxNodes{ 1u * CY_MIB };
    bool_t bReportDeprecatedMembers{ CY_TRUE };
};

enum class schema_validation_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_SCHEMA,
    INVALID_DOCUMENT,
    SCHEMA_NOT_FOUND
};

struct schema_validation_result_t {
    schema_validation_status_t status{ schema_validation_status_t::OK };
    usize nDiagnosticsRequired{ 0u };
    usize nDiagnosticsWritten{ 0u };
    usize nErrors{ 0u };
    usize nWarnings{ 0u };
    usize nNodesVisited{ 0u };
    bool_t bDiagnosticsTruncated{ CY_FALSE };
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
