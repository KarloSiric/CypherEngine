//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_SettingsSchema.cpp
//  Purpose: Implements version 1 of the Cypher user-settings schema.
//  Details: Every setting is optional so a valid document may override only the
//           values it needs. Unknown fields are rejected to catch misspellings and
//           unsupported configuration early.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SettingsSchema.h"

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

CYPHER_NODISCARD constexpr schema_rule_t I64Rule(
    i64 nMin,
    i64 nMax ) noexcept
{
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_I64;
    rule.signedInteger.nMin = nMin;
    rule.signedInteger.nMax = nMax;
    return rule;
}

CYPHER_NODISCARD constexpr schema_rule_t BoolRule() noexcept
{
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_BOOL;
    return rule;
}

CYPHER_NODISCARD constexpr schema_rule_t StringValuesRule(
    const string_view_t *pAllowedValues,
    usize nAllowedValues ) noexcept
{
    schema_rule_t rule{};
    rule.allowedTypes = SCHEMA_TYPE_STRING;
    rule.string.cbMinLength = 1u;
    rule.string.cbMaxLength = 32u;
    rule.string.pAllowedValues = pAllowedValues;
    rule.string.nAllowedValues = nAllowedValues;
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

inline constexpr string_view_t g_displayModes[]{
    SchemaText( "windowed" ),
    SchemaText( "borderless" ),
    SchemaText( "fullscreen" )
};

inline constexpr schema_rule_t g_displayWidthRule = I64Rule(
    CY_SETTINGS_DISPLAY_WIDTH_MIN,
    CY_SETTINGS_DISPLAY_WIDTH_MAX );
inline constexpr schema_rule_t g_displayHeightRule = I64Rule(
    CY_SETTINGS_DISPLAY_HEIGHT_MIN,
    CY_SETTINGS_DISPLAY_HEIGHT_MAX );
inline constexpr schema_rule_t g_displayModeRule = StringValuesRule(
    g_displayModes,
    sizeof( g_displayModes ) / sizeof( g_displayModes[0] ) );
inline constexpr schema_rule_t g_boolRule = BoolRule();

inline constexpr schema_member_t g_displayMembers[]{
    { SchemaText( "width" ), &g_displayWidthRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "height" ), &g_displayHeightRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "mode" ), &g_displayModeRule, SCHEMA_MEMBER_NONE },
    { SchemaText( "vsync" ), &g_boolRule, SCHEMA_MEMBER_NONE }
};
inline constexpr schema_rule_t g_displayRule = ObjectRule(
    g_displayMembers,
    sizeof( g_displayMembers ) / sizeof( g_displayMembers[0] ) );

inline constexpr schema_member_t g_settingsMembers[]{
    { SchemaText( "display" ), &g_displayRule, SCHEMA_MEMBER_NONE }
};

// Absence means "use the compiled default". Unknown names remain errors so
// misspelled or unsupported preferences do not fail silently.
inline constexpr schema_rule_t g_settingsRootRule = ObjectRule(
    g_settingsMembers,
    sizeof( g_settingsMembers ) / sizeof( g_settingsMembers[0] ) );

inline constexpr schema_descriptor_t g_settingsSchema{
    SchemaText( "cypher.settings" ),
    CY_SETTINGS_SCHEMA_VERSION,
    &g_settingsRootRule
};

} // namespace

const schema_descriptor_t *SettingsSchema_V1() noexcept
{
    return &g_settingsSchema;
}

} // namespace cypher::common
