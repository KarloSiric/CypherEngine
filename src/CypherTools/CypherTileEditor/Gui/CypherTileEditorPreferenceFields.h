//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Purpose: Shared preference keys for native persistence, editable INI and UI.
//////////////////////////////////////////////////////////////////////////
#pragma once

#include "CypherTileEditorSettingsDialog.h"
#include "Core/CypherTileMapDocument.h"

namespace cypher::tools::tile_editor::preference_fields
{
struct color_t {
    const char *section;
    const char *key;
    const char *label;
    const char *group;
    QColor tile_editor_preferences_t::*member;
};
inline constexpr color_t colors[] = {
    { "Appearance", "uiBackgroundColor", "Editor background", "Interface", &tile_editor_preferences_t::uiBackgroundColor },
    { "Appearance", "panelColor", "Inset panels", "Interface", &tile_editor_preferences_t::panelColor },
    { "Appearance", "textColor", "Text", "Interface", &tile_editor_preferences_t::textColor },
    { "Appearance", "accentColor", "Active tools", "Interface", &tile_editor_preferences_t::accentColor },
    { "Appearance", "activeViewColor", "Active viewport border", "Interface", &tile_editor_preferences_t::activeViewColor },
    { "Viewport", "perspectiveColor", "3D background", "Viewports and grid", &tile_editor_preferences_t::perspectiveColor },
    { "Viewport", "canvasColor", "Orthographic background", "Viewports and grid", &tile_editor_preferences_t::canvasColor },
    { "Grid", "minorGridColor", "Minor grid", "Viewports and grid", &tile_editor_preferences_t::minorGridColor },
    { "Grid", "majorGridColor", "Major grid", "Viewports and grid", &tile_editor_preferences_t::majorGridColor },
    { "Viewport", "floorColor", "Untextured floor", "Geometry and selection", &tile_editor_preferences_t::floorColor },
    { "Viewport", "selectionColor", "Selected geometry", "Geometry and selection", &tile_editor_preferences_t::selectionColor },
    { "Viewport", "wireColor", "Floor outlines", "Geometry and selection", &tile_editor_preferences_t::wireColor },
    { "Viewport", "wallColor", "Wall outlines", "Geometry and selection", &tile_editor_preferences_t::wallColor },
    { "Viewport", "stairColor", "Stair outlines", "Geometry and selection", &tile_editor_preferences_t::stairColor },
    { "Viewport", "doorColor", "Door markers", "Geometry and selection", &tile_editor_preferences_t::doorColor },
    { "Grid", "axisXColor", "X axis", "Coordinate axes", &tile_editor_preferences_t::axisXColor },
    { "Grid", "axisYColor", "Y axis", "Coordinate axes", &tile_editor_preferences_t::axisYColor },
    { "Grid", "axisZColor", "Z axis", "Coordinate axes", &tile_editor_preferences_t::axisZColor }
};
struct integer_t {
    const char *section;
    const char *key;
    int tile_editor_preferences_t::*member;
    int minimum;
    int maximum;
};
inline constexpr integer_t integers[] = {
    { "Appearance", "uiFontPointSize", &tile_editor_preferences_t::uiFontPointSize, 9, 18 },
    { "Appearance", "uiIconSize", &tile_editor_preferences_t::uiIconSize, 16, 40 },
    { "Grid", "gridSpacingCells", &tile_editor_preferences_t::gridSpacingCells, 1, 16 },
    { "Grid", "gridMinimumPixels", &tile_editor_preferences_t::gridMinimumPixels, 4, 64 },
    { "Grid", "majorGridEvery", &tile_editor_preferences_t::majorGridEvery, 2, 64 },
    { "Viewport", "emptyViewCellPixels", &tile_editor_preferences_t::emptyViewCellPixels, 12, 96 },
    { "Workspace", "viewSplitterWidth", &tile_editor_preferences_t::viewSplitterWidth, 3, 16 },
    { "Map", "defaultMapWidth", &tile_editor_preferences_t::defaultMapWidth, 1, static_cast<int>( TILE_MAP_MAX_WIDTH ) },
    { "Map", "defaultMapHeight", &tile_editor_preferences_t::defaultMapHeight, 1, static_cast<int>( TILE_MAP_MAX_HEIGHT ) }
};
struct real_t {
    const char *section;
    const char *key;
    double tile_editor_preferences_t::*member;
    double minimum;
    double maximum;
};
inline constexpr real_t reals[] = {
    { "Viewport", "wireLineWidth", &tile_editor_preferences_t::wireLineWidth, 0.5, 3.0 },
    { "Viewport", "orthoMaterialOpacity", &tile_editor_preferences_t::orthoMaterialOpacity, 0.2, 1.0 },
    { "Camera", "cameraMoveSpeed", &tile_editor_preferences_t::cameraMoveSpeed, 0.1, 1000.0 },
    { "Camera", "cameraLookSensitivity", &tile_editor_preferences_t::cameraLookSensitivity, 0.01, 2.0 },
    { "Camera", "cameraPanSensitivity", &tile_editor_preferences_t::cameraPanSensitivity, 0.1, 5.0 },
    { "Camera", "cameraZoomSensitivity", &tile_editor_preferences_t::cameraZoomSensitivity, 0.1, 5.0 },
    { "Camera", "cameraFastMultiplier", &tile_editor_preferences_t::cameraFastMultiplier, 1.0, 20.0 },
    { "Camera", "cameraSlowMultiplier", &tile_editor_preferences_t::cameraSlowMultiplier, 0.01, 1.0 },
    { "Camera", "cameraFieldOfView", &tile_editor_preferences_t::cameraFieldOfView, 30.0, 100.0 }
};
struct boolean_t {
    const char *section;
    const char *key;
    bool tile_editor_preferences_t::*member;
};
inline constexpr boolean_t booleans[] = {
    { "Appearance", "showActiveViewBorder", &tile_editor_preferences_t::showActiveViewBorder },
    { "Appearance", "highlightActiveView", &tile_editor_preferences_t::highlightActiveView },
    { "Grid", "showGrid", &tile_editor_preferences_t::showGrid },
    { "Grid", "adaptiveGrid", &tile_editor_preferences_t::adaptiveGrid },
    { "Viewport", "showMarkers", &tile_editor_preferences_t::showMarkers },
    { "Viewport", "wireframeOrtho", &tile_editor_preferences_t::wireframeOrtho },
    { "Viewport", "showInternalTileEdges", &tile_editor_preferences_t::showInternalTileEdges },
    { "Viewport", "showFloorSurfaces", &tile_editor_preferences_t::showFloorSurfaces },
    { "Viewport", "showWallHeight", &tile_editor_preferences_t::showWallHeight },
    { "Viewport", "showWallThickness", &tile_editor_preferences_t::showWallThickness },
    { "Viewport", "showOrthoMaterials", &tile_editor_preferences_t::showOrthoMaterials },
    { "Viewport", "showMaterialLabels", &tile_editor_preferences_t::showMaterialLabels },
    { "Viewport", "showViewMetrics", &tile_editor_preferences_t::showViewMetrics },
    { "Viewport", "showAuthoringFooter", &tile_editor_preferences_t::showAuthoringFooter },
    { "Viewport", "showCoordinateRulers", &tile_editor_preferences_t::showCoordinateRulers },
    { "Viewport", "showViewAxes", &tile_editor_preferences_t::showViewAxes },
    { "Viewport", "centerViewAxes", &tile_editor_preferences_t::centerViewAxes },
    { "Viewport", "depthCueWireframe", &tile_editor_preferences_t::depthCueWireframe },
    { "Workspace", "startMaximized", &tile_editor_preferences_t::startMaximized },
    { "Workspace", "frameMapOnOpen", &tile_editor_preferences_t::frameMapOnOpen },
    { "Workspace", "linkOrthographicCameras", &tile_editor_preferences_t::linkOrthographicCameras },
    { "Workspace", "activateViewOnHover", &tile_editor_preferences_t::activateViewOnHover },
    { "Camera", "cameraInvertY", &tile_editor_preferences_t::cameraInvertY },
    { "Camera", "cameraInvertWheel", &tile_editor_preferences_t::cameraInvertWheel },
    { "Camera", "showCameraHints", &tile_editor_preferences_t::showCameraHints },
    { "Camera", "cameraFlyMode", &tile_editor_preferences_t::cameraFlyMode }
};
} // namespace cypher::tools::tile_editor::preference_fields
