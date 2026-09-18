//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileEditorSettingsDialog.h
//  Purpose: Declares persistent editor preferences and their settings dialog.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_SETTINGSDIALOG_H
#define CYPHER_TOOLS_TILEEDITOR_SETTINGSDIALOG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <QColor>
#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QString>

#include <functional>

class QSettings;
class QSpinBox;
class QDoubleSpinBox;
class QTableWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QDialogButtonBox;
class QPushButton;

namespace cypher::tools::tile_editor
{

struct tile_editor_shortcut_definition_t {
    QString id{};
    QString label{};
    QKeySequence defaultSequence{};
};

struct tile_editor_color_preset_definition_t {
    QString id{};
    QString label{};
};

struct tile_editor_preferences_t {
    QColor uiBackgroundColor{ 48, 48, 50 };
    QColor panelColor{ 39, 40, 43 };
    QColor textColor{ 220, 224, 229 };
    QColor accentColor{ 225, 160, 62 };
    QColor activeViewColor{ 217, 62, 54 };
    QColor perspectiveColor{ 17, 19, 21 };
    QColor canvasColor{ 25, 36, 47 };
    QColor minorGridColor{ 35, 49, 62 };
    QColor majorGridColor{ 52, 74, 91 };
    QColor floorColor{ 111, 139, 153 };
    QColor selectionColor{ 225, 154, 68 };
    QColor wireColor{ 111, 170, 205 };
    QColor wallColor{ 159, 181, 199 };
    QColor stairColor{ 134, 196, 167 };
    QColor doorColor{ 221, 173, 89 };
    // Preserve the conventional editor axis language in every palette:
    // X is red, Y is green and Z is blue. Presets may tune luminance for
    // their background, but should not change these semantic hues.
    QColor axisXColor{ 222, 82, 76 };
    QColor axisYColor{ 82, 190, 105 };
    QColor axisZColor{ 75, 139, 232 };
    int uiFontPointSize{ 11 };
    int uiIconSize{ 24 };
    bool showActiveViewBorder{ false };
    bool highlightActiveView{ false };
    bool showViewMetrics{ false };
    // Projected-authoring instructions are useful while learning the tool,
    // but should not permanently consume the bottom of every 2D viewport.
    bool showAuthoringFooter{ false };
    bool showCoordinateRulers{ true };
    bool showViewAxes{ true };
    bool centerViewAxes{ true };
    bool depthCueWireframe{ true };
    double wireLineWidth{ 1.2 };
    int emptyViewCellPixels{ 32 };
    int viewSplitterWidth{ 6 };
    bool showGrid{ true };
    bool adaptiveGrid{ true };
    bool showMarkers{ true };
    bool wireframeOrtho{ true };
    bool showInternalTileEdges{ false };
    bool showFloorSurfaces{ true };
    bool showWallHeight{ true };
    bool showWallThickness{ true };
    bool showOrthoMaterials{ true };
    bool showMaterialLabels{ false };
    double orthoMaterialOpacity{ 0.8 };
    bool startMaximized{ true };
    bool frameMapOnOpen{ true };
    // Each pane owns its pan and zoom by default. Linked navigation remains an
    // explicit opt-in for workflows that need matching 2D camera transforms.
    bool linkOrthographicCameras{ false };
    // Professional multi-view editors route viewport input to the pane under
    // the pointer. Keep this enabled by default so navigation and authoring
    // shortcuts do not require a preparatory click.
    bool activateViewOnHover{ true };
    bool cameraInvertY{ false };
    bool cameraInvertWheel{ false };
    bool cameraFlyMode{ true };
    bool showCameraHints{ false };
    double cameraMoveSpeed{ 8.0 };
    double cameraLookSensitivity{ 0.20 };
    double cameraPanSensitivity{ 1.0 };
    double cameraZoomSensitivity{ 1.0 };
    double cameraFastMultiplier{ 4.0 };
    double cameraSlowMultiplier{ 0.25 };
    double cameraFieldOfView{ 60.0 };
    int gridSpacingCells{ 1 };
    int gridMinimumPixels{ 12 };
    int majorGridEvery{ 8 };
    int defaultMapWidth{ 64 };
    int defaultMapHeight{ 64 };
    QMap<QString, QKeySequence> shortcuts{};
};

QList<tile_editor_shortcut_definition_t> TileEditorShortcutDefinitions();
QList<tile_editor_color_preset_definition_t> TileEditorColorPresetDefinitions();
tile_editor_preferences_t TileEditorPreferences_Normalize( tile_editor_preferences_t preferences );
// Presets replace colors only; navigation, key bindings, and map defaults remain authored.
void TileEditorPreferences_ApplyColorPreset(
    tile_editor_preferences_t &preferences, const QString &presetId );
// Copies only controls that the Appearance page previews live.
void TileEditorPreferences_CopyAppearance(
    tile_editor_preferences_t &destination,
    const tile_editor_preferences_t &source );
tile_editor_preferences_t TileEditorPreferences_Load( QSettings &settings );
void TileEditorPreferences_Save(
    QSettings &settings,
    const tile_editor_preferences_t &preferences );

class CypherTileEditorSettingsDialog final : public QDialog
{
public:
    explicit CypherTileEditorSettingsDialog(
        const tile_editor_preferences_t &preferences,
        QWidget *pParent = nullptr );

    tile_editor_preferences_t preferences() const;
    void setApplyCallback( std::function<bool( const tile_editor_preferences_t & )> callback );
    void setPreviewCallback( std::function<void( const tile_editor_preferences_t & )> callback );

private:
    class ColorButton;
    void applySelectedTheme();
    void deleteSelectedTheme();
    void exportCurrentTheme();
    void importTheme();
    void markThemeModified();
    void previewAppearance();
    void refreshThemeChoices( const QString &selectedPath = {} );
    void saveCurrentTheme();
    void setAppearanceColors( const tile_editor_preferences_t &preferences );
    void updateThemeActions();
    void updateShortcutConflicts();

    tile_editor_preferences_t m_preferences{};
    std::function<bool( const tile_editor_preferences_t & )> m_applyCallback{};
    std::function<void( const tile_editor_preferences_t & )> m_previewCallback{};
    QMap<QString, ColorButton *> m_appearanceColors{};
    QComboBox *m_pThemeSelector{ nullptr };
    QPushButton *m_pDeleteTheme{ nullptr };
    QPushButton *m_pExportTheme{ nullptr };
    QLabel *m_pThemeStatus{ nullptr };
    QSpinBox *m_pUiFontPointSize{ nullptr };
    QSpinBox *m_pUiIconSize{ nullptr };
    QCheckBox *m_pShowActiveViewBorder{ nullptr };
    QCheckBox *m_pHighlightActiveView{ nullptr };
    QSpinBox *m_pEmptyViewCellPixels{ nullptr };
    QSpinBox *m_pViewSplitterWidth{ nullptr };
    QCheckBox *m_pShowViewMetrics{ nullptr };
    QCheckBox *m_pShowAuthoringFooter{ nullptr };
    QCheckBox *m_pShowCoordinateRulers{ nullptr };
    QCheckBox *m_pShowViewAxes{ nullptr };
    QCheckBox *m_pCenterViewAxes{ nullptr };
    QCheckBox *m_pDepthCueWireframe{ nullptr };
    QDoubleSpinBox *m_pWireLineWidth{ nullptr };
    ColorButton *m_pCanvasColor{ nullptr };
    ColorButton *m_pMinorGridColor{ nullptr };
    ColorButton *m_pMajorGridColor{ nullptr };
    ColorButton *m_pSelectionColor{ nullptr };
    QCheckBox *m_pShowGrid{ nullptr };
    QCheckBox *m_pAdaptiveGrid{ nullptr };
    QCheckBox *m_pShowMarkers{ nullptr };
    QCheckBox *m_pWireframeOrtho{ nullptr };
    QCheckBox *m_pInternalTileEdges{ nullptr };
    QCheckBox *m_pShowFloorSurfaces{ nullptr };
    QCheckBox *m_pShowWallHeight{ nullptr };
    QCheckBox *m_pShowWallThickness{ nullptr };
    QCheckBox *m_pShowOrthoMaterials{ nullptr };
    QCheckBox *m_pShowMaterialLabels{ nullptr };
    QDoubleSpinBox *m_pOrthoMaterialOpacity{ nullptr };
    QCheckBox *m_pStartMaximized{ nullptr };
    QCheckBox *m_pFrameMapOnOpen{ nullptr };
    QCheckBox *m_pLinkOrthographicCameras{ nullptr };
    QCheckBox *m_pActivateViewOnHover{ nullptr };
    QCheckBox *m_pCameraInvertY{ nullptr };
    QCheckBox *m_pCameraInvertWheel{ nullptr };
    QCheckBox *m_pCameraFlyMode{ nullptr };
    QCheckBox *m_pShowCameraHints{ nullptr };
    QDoubleSpinBox *m_pCameraMoveSpeed{ nullptr };
    QDoubleSpinBox *m_pCameraLookSensitivity{ nullptr };
    QDoubleSpinBox *m_pCameraPanSensitivity{ nullptr };
    QDoubleSpinBox *m_pCameraZoomSensitivity{ nullptr };
    QDoubleSpinBox *m_pCameraFastMultiplier{ nullptr };
    QDoubleSpinBox *m_pCameraSlowMultiplier{ nullptr };
    QDoubleSpinBox *m_pCameraFieldOfView{ nullptr };
    QComboBox *m_pGridSpacingCells{ nullptr };
    QSpinBox *m_pGridMinimumPixels{ nullptr };
    QSpinBox *m_pMajorGridEvery{ nullptr };
    QSpinBox *m_pDefaultWidth{ nullptr };
    QSpinBox *m_pDefaultHeight{ nullptr };
    QTableWidget *m_pShortcutTable{ nullptr };
    QLabel *m_pShortcutConflict{ nullptr };
    QDialogButtonBox *m_pButtons{ nullptr };
};

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_SETTINGSDIALOG_H
