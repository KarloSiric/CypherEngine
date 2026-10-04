//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Style.cpp
//  Purpose: Implements the editor style: the framework token catalogue,
//           resolution, the generated stylesheet, the palette, and icons.
//  Details: The catalogue below is CYTHEME.md section 4 verbatim - same IDs,
//           defaults, formulas, and order - so the theme editor, saved
//           themes, and the documentation agree. The derived formulas are
//           the ones the TileEditor look was built from, so the charcoal
//           theme is unchanged by moving them from code into tokens.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - Full V2 catalogue, derived tokens, and token placeholders on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Style.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QIcon>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPixmapCache>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

// ---------------------------------------------------------------------------
// Token table builders
// ---------------------------------------------------------------------------

constexpr u32 kMissing = 0xFF00FFFFu; // Shown only if a formula cannot resolve.

constexpr theme_token_t Base( const char *pId, u32 rgba, const char *pLabel, const char *pGroup ) noexcept
{
    theme_token_t token{};
    token.pId = pId;
    token.kind = theme_token_kind_t::COLOR;
    token.rgbaDefault = rgba;
    token.pLabel = pLabel;
    token.pGroup = pGroup;
    return token;
}

constexpr theme_token_t Derived( const char *pId, theme_derive_t derive, const char *pLabel, const char *pGroup ) noexcept
{
    theme_token_t token = Base( pId, kMissing, pLabel, pGroup );
    token.derive = derive;
    return token;
}

constexpr theme_derive_t MixOf( const char *pA, const char *pB, f64 t ) noexcept { return { theme_derive_op_t::MIX, pA, pB, t, 0.0 }; }
constexpr theme_derive_t CopyOf( const char *pA ) noexcept { return { theme_derive_op_t::COPY, pA, nullptr, 0.0, 0.0 }; }
constexpr theme_derive_t LighterOf( const char *pA, f64 percent ) noexcept { return { theme_derive_op_t::LIGHTER, pA, nullptr, percent, 0.0 }; }
constexpr theme_derive_t DarkerOf( const char *pA, f64 percent ) noexcept { return { theme_derive_op_t::DARKER, pA, nullptr, percent, 0.0 }; }
constexpr theme_derive_t StatusOf( const char *pA, f64 t, f64 hue ) noexcept { return { theme_derive_op_t::MIX_STATUS, pA, nullptr, t, hue }; }

constexpr theme_token_t FontToken( const char *pId, const char *pFamily, f64 flSize, u32 nWeight, const char *pLabel ) noexcept
{
    theme_token_t token{};
    token.pId = pId;
    token.kind = theme_token_kind_t::FONT;
    token.pFontFamily = pFamily;
    token.flFontSize = flSize;
    token.nFontWeight = nWeight;
    token.pLabel = pLabel;
    token.pGroup = "Fonts";
    return token;
}

constexpr theme_token_t MetricToken( const char *pId, f64 flDefault, f64 flMin, f64 flMax, const char *pLabel ) noexcept
{
    theme_token_t token{};
    token.pId = pId;
    token.kind = theme_token_kind_t::METRIC;
    token.flDefault = flDefault;
    token.flMin = flMin;
    token.flMax = flMax;
    token.pLabel = pLabel;
    token.pGroup = "Metrics";
    return token;
}

template <usize nChoices>
constexpr theme_token_t ChoiceToken( const char *pId, const char *const ( &ppChoices )[nChoices], const char *pDefault, const char *pLabel ) noexcept
{
    theme_token_t token{};
    token.pId = pId;
    token.kind = theme_token_kind_t::CHOICE;
    token.ppChoices = ppChoices;
    token.nChoices = nChoices;
    token.pChoiceDefault = pDefault;
    token.pLabel = pLabel;
    token.pGroup = "Choices";
    return token;
}

constexpr const char *kBlack = "#000000";
// Hammer selects rows and menu items in a cool grey-blue; orange is kept
// for tools and modes that are switched on.
constexpr const char *kSelectionBlue = "#4f7bb3";
constexpr const char *kHoverBlue = "#98bdda";
constexpr const char *kDensities[]{ "compact", "normal", "comfortable" };
constexpr const char *kGridStyles[]{ "lines", "dots" };
constexpr const char *kSelectionStyles[]{ "solid", "dashed" };
constexpr const char *kBackgroundStyles[]{ "flat", "gradient" };

constexpr theme_token_t kTokens[]{
    // 4.1 Interface: base colours.
    // Hammer 5's neutral greys: mid-grey panels, darker chrome strips,
    // raised lighter buttons, a blue hover surface, and an orange checked rim.
    Base( "ui.background", 0x3C3C3CFFu, "Window, dialogs, menus", "Interface/Base" ),
    Base( "ui.panel", 0x333333FFu, "Lists, trees, text areas", "Interface/Base" ),
    Base( "ui.text", 0xDCDCDCFFu, "All text", "Interface/Base" ),
    Base( "ui.accent", 0xE59A2FFFu, "Active tool, focus, current tab", "Interface/Base" ),
    // 4.2 Interface: chrome (derived).
    Derived( "ui.chrome", MixOf( "ui.background", kBlack, 0.28 ), "Menu bar, toolbars, tool strip", "Interface/Chrome" ),
    Derived( "ui.header", MixOf( "ui.background", kBlack, 0.22 ), "Dock title bars", "Interface/Chrome" ),
    Derived( "ui.border", MixOf( "ui.background", "ui.text", 0.20 ), "Control borders, scrollbar handles", "Interface/Chrome" ),
    Derived( "ui.border.highlight", LighterOf( "ui.border", 110.0 ), "Hovered borders, title top line", "Interface/Chrome" ),
    Derived( "ui.edge", MixOf( "ui.panel", kBlack, 0.32 ), "Separators, splitters, frame edges", "Interface/Chrome" ),
    Derived( "ui.inset", MixOf( "ui.panel", kBlack, 0.16 ), "Text inputs, selected rows, tooltips", "Interface/Chrome" ),
    Derived( "ui.deepest", DarkerOf( "ui.edge", 115.0 ), "Console, previews, badges", "Interface/Chrome" ),
    Derived( "ui.button", MixOf( "ui.chrome", "ui.text", 0.05 ), "Buttons: a step above the toolbar (Hammer)", "Interface/Chrome" ),
    Derived( "ui.hover", MixOf( "ui.button", kHoverBlue, 0.30 ), "Hovered controls: restrained light blue", "Interface/Chrome" ),
    Derived( "ui.pressed", CopyOf( "ui.inset" ), "Pressed buttons", "Interface/Chrome" ),
    Derived( "ui.checked", MixOf( "ui.button", "ui.text", 0.18 ), "Checked tools and modes: lighter grey, accent rim", "Interface/Chrome" ),
    Derived( "ui.checked.hover", MixOf( "ui.checked", kHoverBlue, 0.22 ), "Checked tool hover surface: blue with an accent rim", "Interface/Chrome" ),
    Derived( "ui.accent.light", LighterOf( "ui.accent", 115.0 ), "Checked tool text and icons", "Interface/Chrome" ),
    Derived( "ui.text.muted", MixOf( "ui.background", "ui.text", 0.68 ), "Secondary text, placeholders", "Interface/Chrome" ),
    Derived( "ui.text.disabled", MixOf( "ui.background", "ui.text", 0.42 ), "Disabled text and icons", "Interface/Chrome" ),
    Derived( "ui.text.selected", CopyOf( "ui.text" ), "Text in selected rows and tabs", "Interface/Chrome" ),
    Derived( "ui.selection", MixOf( "ui.panel", kSelectionBlue, 0.42 ), "Selected text and row background", "Interface/Chrome" ),
    Derived( "ui.row.alternate", MixOf( "ui.panel", "ui.text", 0.035 ), "Alternating list rows", "Interface/Chrome" ),
    Derived( "ui.focus", CopyOf( "ui.border.highlight" ), "Focused input border", "Interface/Chrome" ),
    Derived( "ui.link", CopyOf( "ui.status.info.text" ), "Links in help and diagnostics", "Interface/Chrome" ),
    // 4.3 Interface: status colours (derived).
    Derived( "ui.status.error.text", StatusOf( "ui.text", 0.58, 0.010 ), "Error text", "Interface/Status" ),
    Derived( "ui.status.error.background", StatusOf( "ui.panel", 0.13, 0.010 ), "Error background", "Interface/Status" ),
    Derived( "ui.status.error.border", StatusOf( "ui.border", 0.52, 0.010 ), "Error border", "Interface/Status" ),
    Derived( "ui.status.warning.text", CopyOf( "ui.accent.light" ), "Warning text", "Interface/Status" ),
    Derived( "ui.status.warning.background", MixOf( "ui.panel", "ui.accent", 0.13 ), "Warning background", "Interface/Status" ),
    Derived( "ui.status.warning.border", CopyOf( "ui.accent" ), "Warning border", "Interface/Status" ),
    Derived( "ui.status.success.text", StatusOf( "ui.text", 0.48, 0.365 ), "Success text", "Interface/Status" ),
    Derived( "ui.status.success.border", StatusOf( "ui.border", 0.48, 0.365 ), "Success border", "Interface/Status" ),
    Derived( "ui.status.info.text", StatusOf( "ui.text", 0.48, 0.565 ), "Information text", "Interface/Status" ),
    Derived( "ui.status.info.border", StatusOf( "ui.border", 0.48, 0.565 ), "Information border", "Interface/Status" ),
    // 4.4 Viewport: frame and overlays.
    Derived( "viewport.gutter", MixOf( "ui.background", kBlack, 0.48 ), "Space between viewport panes", "Viewport/Frame" ),
    Derived( "viewport.gutter.light", MixOf( "viewport.gutter", "ui.text", 0.13 ), "Gutter bevel highlight", "Viewport/Frame" ),
    Derived( "viewport.gutter.shadow", MixOf( "viewport.gutter", kBlack, 0.55 ), "Gutter bevel shadow", "Viewport/Frame" ),
    Derived( "viewport.gutter.hover", MixOf( "viewport.gutter", "ui.text", 0.08 ), "Hovered pane splitter", "Viewport/Frame" ),
    Derived( "viewport.header", MixOf( "ui.panel", "viewport.background.2d", 0.24 ), "Pane title strip", "Viewport/Frame" ),
    Derived( "viewport.header.active", MixOf( "ui.button", "viewport.background.2d", 0.15 ), "Active pane title strip", "Viewport/Frame" ),
    Base( "viewport.active_border", 0xD93E36FFu, "Active pane outline", "Viewport/Frame" ),
    Derived( "viewport.overlay.background", MixOf( "viewport.background.3d", "ui.text", 0.055 ), "On-view labels and readouts",
             "Viewport/Frame" ),
    Derived( "viewport.overlay.text", CopyOf( "ui.text.muted" ), "On-view text", "Viewport/Frame" ),
    Derived( "viewport.overlay.border", CopyOf( "ui.border" ), "On-view label frames", "Viewport/Frame" ),
    // 4.5 Viewport: backgrounds and grid.
    Base( "viewport.background.2d", 0x000000FFu, "Top, front, and side views", "Viewport/Grid" ),
    Base( "viewport.background.3d", 0x000000FFu, "3D view", "Viewport/Grid" ),
    Derived( "viewport.background.3d.bottom", CopyOf( "viewport.background.3d" ), "3D gradient bottom", "Viewport/Grid" ),
    Base( "viewport.grid.minor", 0x1F1F1FFFu, "Every grid line", "Viewport/Grid" ),
    Base( "viewport.grid.major", 0x3A3A3AFFu, "Major grid lines", "Viewport/Grid" ),
    Derived( "viewport.grid.band", CopyOf( "viewport.grid.major" ), "Legacy construction grid bands", "Viewport/Grid" ),
    Derived( "viewport.grid.highlight", MixOf( "viewport.grid.major", "ui.text", 0.15 ), "Highlight grid lines", "Viewport/Grid" ),
    Derived( "viewport.grid.origin", MixOf( "viewport.grid.major", "ui.text", 0.35 ), "Lines through 0", "Viewport/Grid" ),
    Derived( "viewport.grid.3d", MixOf( "viewport.background.3d", "viewport.grid.major", 0.6 ), "3D floor grid", "Viewport/Grid" ),
    Base( "viewport.axis.x", 0xDE524CFFu, "X axis (keep red)", "Viewport/Grid" ),
    Base( "viewport.axis.y", 0x52BE69FFu, "Y axis (keep green)", "Viewport/Grid" ),
    Base( "viewport.axis.z", 0x4B8BE8FFu, "Z axis (keep blue)", "Viewport/Grid" ),
    // 4.6 Viewport: objects.
    Base( "viewport.wire.world", 0xC8CCD2FFu, "World brush edges", "Viewport/Objects" ),
    Base( "viewport.wire.entity", 0x9DB4C2FFu, "Brush-entity edges", "Viewport/Objects" ),
    Base( "viewport.wire.mesh", 0xC8CCD2FFu, "Mesh edges", "Viewport/Objects" ),
    Base( "viewport.wire.patch", 0xBCC6C1FFu, "Patch control cages", "Viewport/Objects" ),
    Base( "viewport.wire.terrain", 0xC6C0B4FFu, "Terrain edges", "Viewport/Objects" ),
    Base( "viewport.face.world", 0x8A8F96FFu, "Untextured faces", "Viewport/Objects" ),
    Base( "viewport.selection", 0xD0BA62FFu, "Selected edges and outline", "Viewport/Objects" ),
    Base( "viewport.selection.fill", 0xD0BA6240u, "Selected face tint", "Viewport/Objects" ),
    Derived( "viewport.selection.bounds", CopyOf( "viewport.selection" ), "Selection box and handles", "Viewport/Objects" ),
    Base( "viewport.hover", 0x8CBA87FFu, "Object under the cursor (restrained green)", "Viewport/Objects" ),
    Base( "viewport.entity.default", 0xC0A2D6FFu, "Point entities without a class colour", "Viewport/Objects" ),
    Base( "viewport.entity.light", 0xD2C493FFu, "Light entity markers and labels", "Viewport/Objects" ),
    Base( "viewport.entity.info", 0x9CBDB0FFu, "Spawn, target and info entity markers and labels", "Viewport/Objects" ),
    Base( "viewport.entity.trigger", 0xC2A0CCFFu, "Trigger entity markers and labels", "Viewport/Objects" ),
    Base( "viewport.entity.prop", 0x93B8CEFFu, "Prop entity markers and labels", "Viewport/Objects" ),
    Base( "viewport.entity.unknown", 0xFF00FFFFu, "Entities with an unknown class", "Viewport/Objects" ),
    Base( "viewport.entity.helper", 0xE6D36AFFu, "Radius spheres, cones, arrows", "Viewport/Objects" ),
    Derived( "viewport.entity.name", CopyOf( "ui.text" ), "Entity name labels", "Viewport/Objects" ),
    Base( "viewport.io.line", 0x4FD1C5FFu, "Output connections", "Viewport/Objects" ),
    Derived( "viewport.io.broken", CopyOf( "ui.status.error.text" ), "Outputs with no target", "Viewport/Objects" ),
    Base( "viewport.path", 0xF6AD55FFu, "Path links", "Viewport/Objects" ),
    Base( "viewport.light.radius", 0xFFF3B0FFu, "Light falloff radius", "Viewport/Objects" ),
    Base( "viewport.model.bounds", 0x7F9CF5FFu, "Model bounding boxes", "Viewport/Objects" ),
    Base( "viewport.decal", 0xF687B3FFu, "Decal and overlay outlines", "Viewport/Objects" ),
    Base( "viewport.invalid", 0xFF3B30FFu, "Invalid geometry, leaks, errors", "Viewport/Objects" ),
    Derived( "viewport.hidden", MixOf( "viewport.background.2d", "ui.text", 0.25 ), "Ghosted hidden objects", "Viewport/Objects" ),
    // 4.7 Viewport: tools and gizmos.
    Base( "viewport.tool.preview", 0xFFFFFFFFu, "Creation previews", "Viewport/Tools" ),
    Base( "viewport.tool.preview.fill", 0xFFFFFF20u, "Creation preview fill", "Viewport/Tools" ),
    Base( "viewport.transform.source", 0xED756FFFu, "Transform starting outline and travel guide", "Viewport/Tools" ),
    Base( "viewport.clip.plane", 0xFF6B6BFFu, "Clip plane and points", "Viewport/Tools" ),
    Base( "viewport.clip.kept", 0x68D391FFu, "Part kept by the clip", "Viewport/Tools" ),
    Base( "viewport.clip.removed", 0xFC8181FFu, "Part removed by the clip", "Viewport/Tools" ),
    Base( "viewport.vertex", 0xE2E8F0FFu, "Vertex handles", "Viewport/Tools" ),
    Derived( "viewport.vertex.selected", CopyOf( "viewport.selection" ), "Selected vertices", "Viewport/Tools" ),
    Derived( "viewport.vertex.hover", CopyOf( "viewport.hover" ), "Vertex under the cursor", "Viewport/Tools" ),
    Derived( "viewport.edge.selected", CopyOf( "viewport.selection" ), "Selected edges", "Viewport/Tools" ),
    Base( "viewport.face.selected", 0xFF4D4D60u, "Faces selected by the texture tool", "Viewport/Tools" ),
    Derived( "viewport.handle", CopyOf( "viewport.selection" ), "Resize and rotate handles", "Viewport/Tools" ),
    Derived( "viewport.handle.hover", LighterOf( "viewport.selection", 130.0 ), "Hovered handle", "Viewport/Tools" ),
    Derived( "viewport.gizmo.x", CopyOf( "viewport.axis.x" ), "Gizmo X", "Viewport/Tools" ),
    Derived( "viewport.gizmo.y", CopyOf( "viewport.axis.y" ), "Gizmo Y", "Viewport/Tools" ),
    Derived( "viewport.gizmo.z", CopyOf( "viewport.axis.z" ), "Gizmo Z", "Viewport/Tools" ),
    Base( "viewport.gizmo.view", 0xD0D0D0FFu, "Screen ring and free-move centre", "Viewport/Tools" ),
    Base( "viewport.gizmo.hover", 0xFFE066FFu, "Hovered gizmo part", "Viewport/Tools" ),
    Derived( "viewport.gizmo.active", CopyOf( "ui.accent" ), "Dragged gizmo part", "Viewport/Tools" ),
    Base( "viewport.measure", 0x90CDF4FFu, "Measuring lines and readouts", "Viewport/Tools" ),
    Base( "viewport.cordon", 0xFFCC00FFu, "Cordon bounds", "Viewport/Tools" ),
    Base( "viewport.cordon.outside", 0x00000080u, "Dimming outside a cordon", "Viewport/Tools" ),
    Base( "viewport.camera", 0xA0AEC0FFu, "Camera icons and frustums in 2D", "Viewport/Tools" ),
    Derived( "viewport.camera.active", CopyOf( "ui.accent" ), "The 3D view's camera in 2D", "Viewport/Tools" ),
    // 4.8 Console.
    Derived( "console.background", CopyOf( "ui.deepest" ), "Output area", "Console" ),
    Derived( "console.text", CopyOf( "ui.text" ), "Info records", "Console" ),
    Derived( "console.muted", MixOf( "ui.background", "ui.text", 0.62 ), "Trace and debug records", "Console" ),
    Derived( "console.warning", CopyOf( "ui.accent" ), "Warnings", "Console" ),
    Derived( "console.error", CopyOf( "ui.status.error.text" ), "Errors and fatals", "Console" ),
    Derived( "console.success", CopyOf( "ui.status.success.text" ), "Completed builds, passed checks", "Console" ),
    Derived( "console.command", CopyOf( "ui.text.selected" ), "Echoed command lines", "Console" ),
    Derived( "console.prompt", CopyOf( "ui.accent" ), "The > prompt", "Console" ),
    // 4.8b Code: shader and recipe source in the Database View.
    Derived( "code.background", CopyOf( "ui.deepest" ), "Code editor background", "Code" ),
    Derived( "code.text", CopyOf( "ui.text" ), "Plain source text", "Code" ),
    Derived( "code.current_line", MixOf( "code.background", "ui.text", 0.06 ), "The cursor's line", "Code" ),
    Derived( "code.line_number", CopyOf( "ui.text.disabled" ), "Line numbers", "Code" ),
    Base( "code.keyword", 0xCC8A4EFFu, "Keywords: if, return, uniform, layout", "Code" ),
    Base( "code.type", 0x7FA9C4FFu, "Types (vec3, sampler2D) and recipe keys", "Code" ),
    Base( "code.builtin", 0xB39DCFFFu, "Built-in functions and variables", "Code" ),
    Base( "code.number", 0x79A6C9FFu, "Numbers", "Code" ),
    Base( "code.string", 0x93B077FFu, "Strings and asset paths", "Code" ),
    Base( "code.comment", 0x7D8389FFu, "Comments", "Code" ),
    Base( "code.preprocessor", 0xC2B260FFu, "#version, #define, @schema", "Code" ),
    // 4.9 Fonts.
    FontToken( "ui", EDITOR_STYLE_FONT_SYSTEM, 11.0, 400u, "Interface text" ),
    FontToken( "ui.heading", EDITOR_STYLE_FONT_SYSTEM, 11.0, 600u, "Dock titles, group boxes" ),
    FontToken( "ui.small", EDITOR_STYLE_FONT_SYSTEM, 10.0, 400u, "Status bar, pane headers, badges" ),
    FontToken( "console", EDITOR_STYLE_FONT_SYSTEM_MONO, 11.0, 400u, "Console and log views" ),
    FontToken( "code", EDITOR_STYLE_FONT_SYSTEM_MONO, 12.0, 400u, "Script and text editors" ),
    FontToken( "viewport.labels", EDITOR_STYLE_FONT_SYSTEM, 9.0, 500u, "Entity names" ),
    FontToken( "viewport.dimensions", EDITOR_STYLE_FONT_SYSTEM, 12.0, 500u, "Dimensions and transform measurements" ),
    // 4.10 Metrics.
    MetricToken( "ui.icon_size", 28.0, 12.0, 64.0, "Toolbar icons (px)" ),
    MetricToken( "ui.tool_strip.icon_size", 32.0, 12.0, 64.0, "Tool strip icons (px)" ),
    MetricToken( "ui.tool_strip.spacing", 1.0, 0.0, 8.0, "Space between tool strip buttons (px)" ),
    MetricToken( "ui.tool_strip.separator_height", 3.0, 1.0, 12.0, "Tool strip group divider height (px)" ),
    // Hammer's icons are full colour; 0 renders tool art in neutral greys,
    // with hover and checked bringing colour back by the two strengths below.
    MetricToken( "ui.icon.color_strength", 1.0, 0.0, 1.0, "Original illustration colour at rest; 0 keeps neutral icons" ),
    MetricToken( "ui.icon.hover_color_strength", 0.35, 0.0, 1.0, "Original illustration colour on hover; 0 keeps neutral icons" ),
    MetricToken( "ui.icon.checked_color_strength", 0.65, 0.0, 1.0, "Original illustration colour when checked; 0 keeps neutral icons" ),
    MetricToken( "ui.menu.icon_size", 16.0, 12.0, 32.0, "Menu icons (px)" ),
    MetricToken( "ui.spacing", 4.0, 0.0, 24.0, "Space between controls" ),
    MetricToken( "ui.padding", 4.0, 0.0, 24.0, "Inner padding of controls" ),
    MetricToken( "ui.row_height", 20.0, 14.0, 48.0, "List and tree rows" ),
    MetricToken( "ui.splitter_width", 3.0, 1.0, 12.0, "Dock splitters" ),
    MetricToken( "ui.scrollbar_width", 9.0, 4.0, 24.0, "Scrollbars" ),
    MetricToken( "ui.border_radius", 2.0, 0.0, 12.0, "Button and input corners" ),
    MetricToken( "viewport.splitter_width", 6.0, 1.0, 16.0, "Gutters between viewport panes" ),
    MetricToken( "viewport.grid.line_width", 1.0, 0.5, 4.0, "Minor grid lines" ),
    MetricToken( "viewport.grid.major.line_width", 1.0, 0.5, 4.0, "Major and highlight lines" ),
    MetricToken( "viewport.grid.origin.line_width", 1.5, 0.5, 6.0, "Origin lines" ),
    MetricToken( "viewport.grid.dot_size", 2.0, 1.0, 8.0, "Dots in dot style" ),
    MetricToken( "viewport.wire.line_width", 1.2, 0.5, 4.0, "Object edges" ),
    MetricToken( "viewport.selection.line_width", 1.5, 0.5, 6.0, "Selected edges and outline" ),
    MetricToken( "viewport.vertex.size", 6.0, 2.0, 24.0, "Vertex handles (px)" ),
    MetricToken( "viewport.handle.size", 7.0, 3.0, 24.0, "Resize handles (px)" ),
    MetricToken( "viewport.gizmo.size", 96.0, 32.0, 256.0, "Gizmo size on screen (px)" ),
    MetricToken( "viewport.gizmo.line_width", 2.0, 1.0, 8.0, "Gizmo strokes" ),
    MetricToken( "viewport.entity.icon_size", 16.0, 8.0, 64.0, "Entity sprites and icons" ),
    // 4.11 Choices.
    ChoiceToken( "ui.density", kDensities, "normal", "Padding, spacing, and row height" ),
    ChoiceToken( "viewport.grid.style", kGridStyles, "lines", "2D grid drawing" ),
    ChoiceToken( "viewport.selection.style", kSelectionStyles, "solid", "Selected edges in 2D" ),
    ChoiceToken( "viewport.background.3d.style", kBackgroundStyles, "flat", "3D background" ),
};

// The fast slots, in editor_style_color_t order.
constexpr const char *kSlotIds[STYLE_COLOR_COUNT]{
    "ui.background", "ui.panel", "ui.text", "ui.accent", "viewport.active_border", "viewport.background.3d",
    "viewport.background.2d", "viewport.grid.minor", "viewport.grid.major", "viewport.selection", "viewport.wire.world",
    "viewport.axis.x", "viewport.axis.y", "viewport.axis.z",
};

QColor ToQColor( u32 rgba ) noexcept
{
    return QColor( static_cast<int>( ( rgba >> 24u ) & 0xFFu ), static_cast<int>( ( rgba >> 16u ) & 0xFFu ),
                   static_cast<int>( ( rgba >> 8u ) & 0xFFu ), static_cast<int>( rgba & 0xFFu ) );
}

QFont ResolveQFont( const theme_font_t &font )
{
    const QString family = QString::fromUtf8( font.family.pData, static_cast<qsizetype>( font.family.cchLength ) );
    QFont result;
    if ( family == QLatin1String( EDITOR_STYLE_FONT_SYSTEM ) ) {
        result = QFontDatabase::systemFont( QFontDatabase::GeneralFont );
    } else if ( family == QLatin1String( EDITOR_STYLE_FONT_SYSTEM_MONO ) ) {
        result = QFontDatabase::systemFont( QFontDatabase::FixedFont );
        // Some platforms (offscreen, minimal X11) answer with a proportional
        // font; the hint makes Qt substitute a monospace one.
        result.setStyleHint( QFont::Monospace );
        result.setFixedPitch( true );
    } else {
        result = QFont( family );
    }
    result.setPointSizeF( font.flSize );
    // theme_font_t weights are CSS-style 1..1000, which is Qt 6's scale too.
    result.setWeight( static_cast<QFont::Weight>( std::clamp<u32>( font.nWeight, 1u, 1000u ) ) );
    return result;
}

// Scales "Npx" lengths so padding and minimum sizes follow the font size.
QString ScalePixels( const QString &style, double scale )
{
    if ( std::abs( scale - 1.0 ) < 0.01 ) { return style; }
    static const QRegularExpression pixels( QStringLiteral( "(\\d+)px" ) );
    QString result;
    result.reserve( style.size() );
    qsizetype iLast = 0;
    for ( QRegularExpressionMatchIterator it = pixels.globalMatch( style ); it.hasNext(); ) {
        const QRegularExpressionMatch match = it.next();
        result += QStringView( style ).mid( iLast, match.capturedStart() - iLast );
        const int value = match.captured( 1 ).toInt();
        // Hairlines stay one pixel; everything else keeps at least that.
        const int scaled = value <= 1 ? value : std::max( 1, static_cast<int>( std::lround( value * scale ) ) );
        result += QString::number( scaled ) + QStringLiteral( "px" );
        iLast = match.capturedEnd();
    }
    result += QStringView( style ).mid( iLast );
    return result;
}

// QSS reads "#rrggbb" and rgba(); translucent tokens need the latter.
QString CssColor( const QColor &color )
{
    if ( color.alpha() == 255 ) { return color.name( QColor::HexRgb ); }
    return QStringLiteral( "rgba(%1, %2, %3, %4)" ).arg( color.red() ).arg( color.green() ).arg( color.blue() ).arg( color.alpha() );
}

} // namespace

const theme_token_t *EditorStyle_Tokens( usize *pnTokensOut ) noexcept
{
    if ( pnTokensOut != nullptr ) { *pnTokensOut = std::size( kTokens ); }
    return kTokens;
}

theme_status_t EditorStyle_RegisterTokens( theme_registry_t *pRegistry ) noexcept
{
    return EditorThemeRegistry_Register( pRegistry, kTokens, std::size( kTokens ) );
}

editor_style_t EditorStyle_Resolve( const theme_registry_t *pRegistry, const key_value_t *const *ppChain, usize nChain )
{
    CY_ASSERT( pRegistry != nullptr );
    editor_style_t style{};
    for ( usize i = 0u; pRegistry != nullptr && i < pRegistry->tokens.nCount; ++i ) {
        const theme_token_t &token = *pRegistry->tokens.pData[i];
        const QString id = QString::fromUtf8( token.pId );
        theme_resolution_t resolution{};
        switch ( token.kind ) {
            case theme_token_kind_t::COLOR:
                style.tokenColors.insert( id, ToQColor( EditorTheme_ResolveColorIn( pRegistry, ppChain, nChain, token, &resolution ) ) );
                break;
            case theme_token_kind_t::FONT:
                style.fonts.insert( id, ResolveQFont( EditorTheme_ResolveFont( ppChain, nChain, token, &resolution ) ) );
                break;
            case theme_token_kind_t::METRIC:
                style.metrics.insert( id, EditorTheme_ResolveMetric( ppChain, nChain, token, &resolution ) );
                break;
            case theme_token_kind_t::CHOICE: {
                const string_view_t value = EditorTheme_ResolveChoice( ppChain, nChain, token, &resolution );
                style.choices.insert( id, QString::fromUtf8( value.pData, static_cast<qsizetype>( value.cchLength ) ) );
                break;
            }
        }
        style.nInvalidSkipped += resolution.nInvalidSkipped;
    }
    for ( u32 i = 0u; i < STYLE_COLOR_COUNT; ++i ) {
        const QColor color = EditorStyle_TokenColor( style, kSlotIds[i] );
        style.colors[i] = ( static_cast<u32>( color.red() ) << 24u ) | ( static_cast<u32>( color.green() ) << 16u ) |
                          ( static_cast<u32>( color.blue() ) << 8u ) | static_cast<u32>( color.alpha() );
    }
    style.uiFont = EditorStyle_Font( style, "ui" );
    style.consoleFont = EditorStyle_Font( style, "console" );
    style.iconSize = EditorStyle_Metric( style, "ui.icon_size", 28.0 );
    const QString density = EditorStyle_Choice( style, "ui.density" );
    style.density = density == QLatin1String( "compact" ) ? 0.8 : ( density == QLatin1String( "comfortable" ) ? 1.25 : 1.0 );
    style.bLight = EditorStyle_Color( style, STYLE_COLOR_BACKGROUND ).lightnessF() >= 0.5;
    return style;
}

QColor EditorStyle_Color( const editor_style_t &style, editor_style_color_t color ) noexcept
{
    CY_ASSERT( color < STYLE_COLOR_COUNT );
    return ToQColor( style.colors[color] );
}

QColor EditorStyle_TokenColor( const editor_style_t &style, const char *pId )
{
    CY_ASSERT( pId != nullptr );
    return style.tokenColors.value( QString::fromUtf8( pId ), ToQColor( kMissing ) );
}

double EditorStyle_Metric( const editor_style_t &style, const char *pId, double fallback )
{
    CY_ASSERT( pId != nullptr );
    return style.metrics.value( QString::fromUtf8( pId ), fallback );
}

QString EditorStyle_Choice( const editor_style_t &style, const char *pId )
{
    CY_ASSERT( pId != nullptr );
    return style.choices.value( QString::fromUtf8( pId ) );
}

QFont EditorStyle_Font( const editor_style_t &style, const char *pId )
{
    CY_ASSERT( pId != nullptr );
    return style.fonts.value( QString::fromUtf8( pId ), QApplication::font() );
}

QString EditorStyle_BuildStyleSheet( const editor_style_t &style, const QString &templateText )
{
    static const QRegularExpression tokenPlaceholder( QStringLiteral( "@([a-z][a-z0-9_.]*)@" ) );
    QString sheet;
    sheet.reserve( templateText.size() );
    qsizetype iLast = 0;
    for ( QRegularExpressionMatchIterator it = tokenPlaceholder.globalMatch( templateText ); it.hasNext(); ) {
        const QRegularExpressionMatch match = it.next();
        const auto found = style.tokenColors.constFind( match.captured( 1 ) );
        if ( found == style.tokenColors.constEnd() ) {
            CY_LOG_WRITE( Error, Gui, "Stylesheet template names a colour token nobody registered" );
            return {};
        }
        sheet += QStringView( templateText ).mid( iLast, match.capturedStart() - iLast );
        sheet += CssColor( found.value() );
        iLast = match.capturedEnd();
    }
    sheet += QStringView( templateText ).mid( iLast );

    // The docking library ships light and dark glyph variants of its title
    // bar buttons; a dark background takes the light glyphs ("_dark").
    sheet.replace( QLatin1String( "@ADS_ICON@" ), style.bLight ? QString() : QStringLiteral( "_dark" ) );
    const double pointSize = style.uiFont.pointSizeF() > 0.0 ? style.uiFont.pointSizeF() : 11.0;
    sheet.replace( QLatin1String( "@HEADER_FONT@" ), QString::number( std::max( 9.0, pointSize - 1.0 ) ) );
    sheet.replace( QLatin1String( "@BODY_FONT@" ), QString::number( pointSize ) );
    const double controlPadding = EditorStyle_Metric( style, "ui.padding", 4.0 );
    sheet.replace( QLatin1String( "@TOOLBAR_PADDING@" ), QString::number( qRound( controlPadding * 0.75 ) ) );
    sheet.replace( QLatin1String( "@TOOL_PALETTE_PADDING@" ), QString::number( qRound( controlPadding * 0.5 ) ) );

    static const QRegularExpression leftover( QStringLiteral( "@[A-Za-z0-9_.]+@" ) );
    if ( leftover.match( sheet ).hasMatch() ) {
        CY_LOG_WRITE( Error, Gui, "Stylesheet template has a placeholder the style does not fill" );
        return {};
    }
    // The template's pixel sizes are designed for a 9 pt interface font at
    // normal density.
    return ScalePixels( sheet, pointSize / 9.0 * style.density );
}

bool EditorStyle_Apply( QApplication *pApplication, const editor_style_t &style )
{
    CY_ASSERT( pApplication != nullptr );
    QFile templateFile( QString::fromLatin1( EDITOR_STYLE_TEMPLATE_RESOURCE ) );
    if ( !templateFile.open( QIODevice::ReadOnly ) ) {
        CY_LOG_WRITE( Error, Gui, "Editor stylesheet template resource is missing" );
        return false;
    }
    const QString sheet = EditorStyle_BuildStyleSheet( style, QString::fromUtf8( templateFile.readAll() ) );
    if ( sheet.isEmpty() ) { return false; }

    // Fusion draws every control from the palette, so the theme reaches the
    // parts the stylesheet does not describe.
    if ( QStyle *pFusion = QStyleFactory::create( QStringLiteral( "Fusion" ) ) ) { QApplication::setStyle( pFusion ); }
    const auto token = [&style]( const char *pId ) { return EditorStyle_TokenColor( style, pId ); };
    const QColor disabled = token( "ui.text.disabled" );
    QPalette palette;
    palette.setColor( QPalette::Window, token( "ui.background" ) );
    palette.setColor( QPalette::WindowText, token( "ui.text" ) );
    palette.setColor( QPalette::Base, token( "ui.panel" ) );
    palette.setColor( QPalette::AlternateBase, token( "ui.row.alternate" ) );
    palette.setColor( QPalette::Text, token( "ui.text" ) );
    palette.setColor( QPalette::Button, token( "ui.button" ) );
    palette.setColor( QPalette::ButtonText, token( "ui.text" ) );
    palette.setColor( QPalette::Light, token( "ui.border.highlight" ) );
    palette.setColor( QPalette::Midlight, token( "ui.border" ) );
    palette.setColor( QPalette::Mid, token( "ui.panel" ) );
    palette.setColor( QPalette::Dark, token( "ui.edge" ) );
    palette.setColor( QPalette::Shadow, token( "ui.deepest" ) );
    palette.setColor( QPalette::Highlight, token( "ui.selection" ) );
    palette.setColor( QPalette::HighlightedText, token( "ui.text.selected" ) );
    palette.setColor( QPalette::ToolTipBase, token( "ui.inset" ) );
    palette.setColor( QPalette::ToolTipText, token( "ui.text" ) );
    palette.setColor( QPalette::PlaceholderText, token( "ui.text.muted" ) );
    palette.setColor( QPalette::Link, token( "ui.link" ) );
    palette.setColor( QPalette::Disabled, QPalette::Text, disabled );
    palette.setColor( QPalette::Disabled, QPalette::ButtonText, disabled );
    palette.setColor( QPalette::Disabled, QPalette::WindowText, disabled );
    QApplication::setPalette( palette );
    QApplication::setFont( style.uiFont );
    pApplication->setStyleSheet( sheet );
    CY_LOG_WRITE( Info, Gui, "Editor style applied" );
    return true;
}

namespace
{

bool IconKeepsSemanticColor( const QString &name )
{
    // Most tool art is deliberately neutral. Colour is reserved for run/build
    // and diagnostics, where losing it loses useful meaning. Grid/visibility
    // switches use the same off/on colours as every other toolbar toggle.
    return name == QStringLiteral( "map-run" ) || name == QStringLiteral( "map-stop" )
        || name == QStringLiteral( "map-compile" ) || name == QStringLiteral( "map-check" )
        || name == QStringLiteral( "map-leak" ) || name == QStringLiteral( "log-error" )
        || name == QStringLiteral( "log-warning" ) || name == QStringLiteral( "log-info" );
}

// Qt owns this engine through QIcon. Render the vector at the requested pixel
// size instead of eagerly storing every size/state for hundreds of commands.
// QPixmapCache bounds the shared raster cache; the key includes the resolved
// palette, so applying a theme cannot reuse an icon from the previous theme.
class editor_icon_engine_t final : public QIconEngine
{
public:
    editor_icon_engine_t( const editor_style_t &style, const QString &name, QByteArray svg, bool shaded, bool bColorChecked )
        : m_name( name ), m_svg( std::move( svg ) ), m_shaded( shaded ),
          m_semantic( shaded && IconKeepsSemanticColor( name ) ), m_colorChecked( bColorChecked ),
          m_text( EditorStyle_TokenColor( style, "ui.text.muted" ) ),
          m_disabled( EditorStyle_TokenColor( style, "ui.text.disabled" ) ),
          m_selected( EditorStyle_TokenColor( style, "ui.text" ) ),
          m_background( EditorStyle_TokenColor( style, "ui.panel" ) ),
          m_idleColorStrength( EditorStyle_Metric( style, "ui.icon.color_strength", 1.0 ) ),
          m_hoverColorStrength( EditorStyle_Metric( style, "ui.icon.hover_color_strength", 0.35 ) ),
          m_checkedColorStrength( EditorStyle_Metric( style, "ui.icon.checked_color_strength", 0.65 ) )
    {
        // Outline SVGs use currentColor. White gives their raster a neutral
        // mask; their alpha and stroke coverage are preserved during tinting.
        m_svg.replace( "currentColor", "#ffffff" );
        QByteArray cacheIdentity = m_svg + name.toUtf8();
        for ( const QColor &color : { m_text, m_disabled, m_selected, m_background } ) {
            cacheIdentity += color.name( QColor::HexArgb ).toUtf8();
        }
        cacheIdentity += shaded ? '1' : '0';
        cacheIdentity += bColorChecked ? '1' : '0';
        cacheIdentity += QByteArray::number( m_idleColorStrength, 'g', 16 ) + '/';
        cacheIdentity += QByteArray::number( m_hoverColorStrength, 'g', 16 ) + '/';
        cacheIdentity += QByteArray::number( m_checkedColorStrength, 'g', 16 );
        m_cachePrefix = QStringLiteral( "cypher-editor-icon-v5/" )
            + QString::fromLatin1( QCryptographicHash::hash( cacheIdentity, QCryptographicHash::Sha256 ).toHex() );
    }

    QIconEngine *clone() const override { return new editor_icon_engine_t( *this ); }
    QString key() const override { return QStringLiteral( "CypherEditorThemedSvg" ); }
    QString iconName() override { return m_name; }
    bool isNull() override { return false; } // The resource is validated before construction.

    void paint( QPainter *pPainter, const QRect &rect, QIcon::Mode mode, QIcon::State state ) override
    {
        const qreal scale = pPainter->device()->devicePixelRatioF();
        pPainter->drawPixmap( rect, Rasterize( rect.size() * scale, mode, state, scale ) );
    }

    QPixmap pixmap( const QSize &size, QIcon::Mode mode, QIcon::State state ) override
    {
        return Rasterize( size, mode, state, 1.0 );
    }

    QPixmap scaledPixmap( const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale ) override
    {
        if ( size.isEmpty() || !std::isfinite( scale ) || scale <= 0.0 ) { return {}; }
        // Qt 6.5 is supported by Mason. Before 6.8, QIcon passed physical
        // pixels here; from 6.8 onward it passes device-independent pixels.
#if QT_VERSION < QT_VERSION_CHECK( 6, 8, 0 )
        return Rasterize( size, mode, state, scale );
#else
        return Rasterize( size * scale, mode, state, scale );
#endif
    }

private:
    QPixmap Rasterize( const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale )
    {
        if ( size.isEmpty() || !std::isfinite( scale ) || scale <= 0.0 ) { return {}; }
        const QString cacheKey = m_cachePrefix + QStringLiteral( "/%1/%2/%3/%4/%5" )
            .arg( size.width() ).arg( size.height() ).arg( static_cast<int>( mode ) )
            .arg( static_cast<int>( state ) ).arg( scale, 0, 'g', 16 );
        QPixmap pixmap;
        if ( QPixmapCache::find( cacheKey, &pixmap ) ) { return pixmap; }

        QImage image( size, QImage::Format_ARGB32_Premultiplied );
        if ( image.isNull() ) { return {}; }
        image.fill( Qt::transparent );
        QSvgRenderer renderer( m_svg );
        {
            QPainter painter( &image );
            renderer.render( &painter );
        }
        const bool colorOn = m_colorChecked && state == QIcon::On;
        if ( !m_semantic || mode == QIcon::Disabled || colorOn ) {
            const QColor tint = mode == QIcon::Disabled ? m_disabled
                : mode == QIcon::Selected ? m_selected : m_text;
            const bool lightSurface = qGray( tint.rgb() ) < qGray( m_background.rgb() );
            for ( int y = 0; y < image.height(); ++y ) {
                auto *pLine = reinterpret_cast<QRgb *>( image.scanLine( y ) );
                for ( int x = 0; x < image.width(); ++x ) {
                    const QColor pixel = QColor::fromRgba( qUnpremultiply( pLine[x] ) );
                    if ( pixel.alpha() == 0 ) { continue; }
                    QColor color = tint;
                    if ( m_shaded ) {
                        // Preserve the light/dark faces that explain a shape.
                        // A flat SourceIn mask would turn a cube into a blob.
                        const double luminance = qGray( pixel.rgb() ) / 255.0;
                        const double weight = lightSurface ? luminance * 0.42 : 0.22 + 0.78 * std::sqrt( luminance );
                        const auto channel = [&]( int foreground, int background ) {
                            return qRound( lightSurface ? foreground + ( background - foreground ) * weight : foreground * weight );
                        };
                        color.setRgb( channel( color.red(), m_background.red() ),
                            channel( color.green(), m_background.green() ), channel( color.blue(), m_background.blue() ) );
                        if ( mode != QIcon::Disabled ) {
                            // Preserve the illustration's own colour relationships.
                            // Selection belongs to the button border, never a global
                            // orange tint over blue/steel/tan construction details.
                            const double strength = colorOn ? std::max( m_checkedColorStrength, m_idleColorStrength )
                                : mode == QIcon::Active ? std::max( m_hoverColorStrength, m_idleColorStrength ) : m_idleColorStrength;
                            const auto sourceChannel = [strength]( int neutral, int original ) {
                                return qRound( neutral + ( original - neutral ) * strength );
                            };
                            color.setRgb( sourceChannel( color.red(), pixel.red() ),
                                sourceChannel( color.green(), pixel.green() ), sourceChannel( color.blue(), pixel.blue() ) );
                        }
                    }
                    color.setAlpha( pixel.alpha() * tint.alpha() / 255 );
                    pLine[x] = qPremultiply( color.rgba() );
                }
            }
        }
        pixmap = QPixmap::fromImage( image );
        pixmap.setDevicePixelRatio( scale );
        QPixmapCache::insert( cacheKey, pixmap );
        return pixmap;
    }

    QString m_name;
    QByteArray m_svg;
    bool m_shaded;
    bool m_semantic;
    bool m_colorChecked;
    QColor m_text;
    QColor m_disabled;
    QColor m_selected;
    QColor m_background;
    double m_idleColorStrength;
    double m_hoverColorStrength;
    double m_checkedColorStrength;
    QString m_cachePrefix;
};

} // namespace

QIcon EditorStyle_Icon( const editor_style_t &style, const char *pName, bool bColorChecked )
{
    CY_ASSERT( pName != nullptr );
    const QString name = QString::fromLatin1( pName );
    QString path = QStringLiteral( ":/cypher/editor/icons/color/%1.svg" ).arg( name );
    const bool shaded = QFile::exists( path );
    if ( !shaded ) { path = QStringLiteral( ":/cypher/editor/icons/%1.svg" ).arg( name ); }
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) { return {}; }
    QByteArray svg = file.readAll();
    // QtSvg treats this CSS fallback list in our generated text icons as one
    // literal family. Resolve it to the editor's installed UI font before the
    // validation parse as well as painting, avoiding repeated font lookup warnings.
    if ( svg.contains( "font-family=\"Helvetica, Arial, sans-serif\"" ) ) {
        const QByteArray family = QFontInfo( style.uiFont ).family().toHtmlEscaped().toUtf8();
        svg.replace( "font-family=\"Helvetica, Arial, sans-serif\"", QByteArray( "font-family=\"" ) + family + '"' );
    }
    if ( !QSvgRenderer( svg ).isValid() ) { return {}; }
    return QIcon( new editor_icon_engine_t( style, name, std::move( svg ), shaded, bColorChecked ) );
}

} // namespace cypher::editor::gui
