//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_ProjectSchema.cpp
//  Purpose: Implements version 1 of the Cypher project document schema.
//  Details: The project contract contains durable, source-controlled project identity
//           and resource-location data. User and machine preferences intentionally
//           belong to the separate cypher.settings contract.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ProjectSchema.h"

namespace cypher::common
{

namespace
{

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t SchemaText(
    const char ( &text )[nExtent] ) noexcept
{
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

CYPHER_NODISCARD constexpr schema_rule_t StringRule(
    usize cbMinLength,
    usize cbMaxLength ) noexcept
{
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_STRING;
    rule.string.cbMinLength = cbMinLength;
    rule.string.cbMaxLength = cbMaxLength;
    return rule;
}

CYPHER_NODISCARD constexpr schema_rule_t ArrayRule(
    const schema_rule_t *pElementRule,
    usize nMinElements,
    usize nMaxElements ) noexcept
{
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_ARRAY;
    rule.array.pElementRule = pElementRule;
    rule.array.nMinElements = nMinElements;
    rule.array.nMaxElements = nMaxElements;
    return rule;
}

CYPHER_NODISCARD constexpr schema_rule_t ObjectRule(
    const schema_member_t *pMembers,
    usize nMembers ) noexcept
{
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_OBJECT;
    rule.object.pMembers = pMembers;
    rule.object.nMembers = nMembers;
    rule.object.flags = SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS;
    return rule;
}

inline constexpr schema_rule_t g_projectIdRule = StringRule(
    1u,
    CY_PROJECT_ID_MAX_LENGTH );
inline constexpr schema_rule_t g_projectNameRule = StringRule(
    1u,
    CY_PROJECT_NAME_MAX_LENGTH );
inline constexpr schema_rule_t g_virtualPathRule = StringRule(
    1u,
    CY_PROJECT_PATH_MAX_LENGTH );

inline constexpr schema_rule_t g_searchPathsRule = ArrayRule(
    &g_virtualPathRule,
    1u,
    CY_PROJECT_MAX_SEARCH_PATHS );

inline constexpr schema_member_t g_projectMembers[]{
    { SchemaText( "id" ), &g_projectIdRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "name" ), &g_projectNameRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "start_map" ), &g_virtualPathRule, SCHEMA_MEMBER_REQUIRED },
    { SchemaText( "search_paths" ), &g_searchPathsRule, SCHEMA_MEMBER_NONE }
};

// This descriptor handles shape and byte bounds only. ProjectManifest_Decode adds
// identifier grammar, canonical VFS paths, map extension, and uniqueness policy.
inline constexpr schema_rule_t g_projectRootRule = ObjectRule(
    g_projectMembers,
    sizeof( g_projectMembers ) / sizeof( g_projectMembers[0] ) );

inline constexpr schema_descriptor_t g_projectSchema{
    SchemaText( "cypher.project" ),
    CY_PROJECT_SCHEMA_VERSION,
    &g_projectRootRule
};

} // namespace

const schema_descriptor_t *ProjectSchema_V1() noexcept
{
    return &g_projectSchema;
}

} // namespace cypher::common
