//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileEditorSettingsDialog.cpp
//  Purpose: Implements tile editor preferences and shortcut editing.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileEditorSettingsDialog.h"
#include "CypherTileEditorConfig.h"
#include "CypherTileEditorPreferenceFields.h"
#include "CypherTileEditorUserThemes.h"

#include "Core/CypherTileMapDocument.h"

#include <QColorDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cypher::tools::tile_editor
{

namespace
{

constexpr int THEME_KIND_ROLE = Qt::UserRole + 1;
// Keep every real theme kind non-zero. Qt separators have no data for this
// role, and QVariant::toInt() maps that missing value to zero. Reserving zero
// prevents a separator from being mistaken for a built-in theme and selected
// as the current (blank) combo-box item.
constexpr int BUILT_IN_THEME = 1;
constexpr int USER_THEME = 2;
constexpr int MODIFIED_THEME = 3;

int GridSpacing( int value )
{
    value = std::clamp( value, 1, 16 );
    int spacing = 1;
    while ( spacing * 2 <= value ) spacing *= 2;
    return spacing;
}

double FiniteClampedValue( double value, double minimum, double maximum, double fallback )
{
    return std::isfinite( value ) ? std::clamp( value, minimum, maximum ) : fallback;
}


int ReadInteger( QSettings &settings, const char *pKey, int fallback )
{
    bool bOk = false;
    const int value = settings.value( QString::fromLatin1( pKey ), fallback ).toInt( &bOk );
    return bOk ? value : fallback;
}

double ReadDouble( QSettings &settings, const char *pKey, double fallback )
{
    bool bOk = false;
    const double value = settings.value( QString::fromLatin1( pKey ), fallback ).toDouble( &bOk );
    return bOk && std::isfinite( value ) ? value : fallback;
}

bool ReadBoolean( QSettings &settings, const char *pKey, bool fallback )
{
    const QString value = settings.value( QString::fromLatin1( pKey ), fallback )
        .toString().trimmed().toLower();
    if ( value == QStringLiteral( "true" ) || value == QStringLiteral( "1" ) ) return true;
    if ( value == QStringLiteral( "false" ) || value == QStringLiteral( "0" ) ) return false;
    return fallback;
}

} // namespace

QList<tile_editor_color_preset_definition_t> TileEditorColorPresetDefinitions()
{
    return {
        { QStringLiteral( "radiant-dark" ), QObject::tr( "Radiant Dark" ) },
        { QStringLiteral( "slate" ), QObject::tr( "Slate" ) },
        { QStringLiteral( "hammer-charcoal" ), QObject::tr( "Hammer Charcoal" ) },
        { QStringLiteral( "radiant-light" ), QObject::tr( "Radiant Light" ) },
        { QStringLiteral( "midnight" ), QObject::tr( "Midnight" ) },
        { QStringLiteral( "warm-workshop" ), QObject::tr( "Warm Workshop" ) },
        { QStringLiteral( "blueprint-blue" ), QObject::tr( "Blueprint Blue" ) },
        { QStringLiteral( "graphite" ), QObject::tr( "Graphite" ) },
        { QStringLiteral( "high-contrast-dark" ), QObject::tr( "High Contrast Dark" ) },
        { QStringLiteral( "coastal-dusk" ), QObject::tr( "Coastal Dusk" ) },
        { QStringLiteral( "sandstone-light" ), QObject::tr( "Sandstone Light" ) }
    };
}

tile_editor_preferences_t TileEditorPreferences_Normalize( tile_editor_preferences_t value )
{
    const tile_editor_preferences_t defaults{};
    for ( const auto &field : preference_fields::colors ) {
        if ( !( value.*field.member ).isValid() ) value.*field.member = defaults.*field.member;
    }
    for ( const auto &field : preference_fields::integers )
        value.*field.member = std::clamp( value.*field.member, field.minimum, field.maximum );
    for ( const auto &field : preference_fields::reals )
        value.*field.member = FiniteClampedValue(
            value.*field.member, field.minimum, field.maximum, defaults.*field.member );
    value.gridSpacingCells = GridSpacing( value.gridSpacingCells );
    value.defaultMapWidth = std::clamp( value.defaultMapWidth, 1, static_cast<int>( TILE_MAP_MAX_WIDTH ) );
    value.defaultMapHeight = std::clamp( value.defaultMapHeight, 1, static_cast<int>( TILE_MAP_MAX_HEIGHT ) );
    return value;
}

void TileEditorPreferences_ApplyColorPreset(
    tile_editor_preferences_t &preferences, const QString &presetId )
{
    const auto definitions = TileEditorColorPresetDefinitions();
    const auto definition = std::find_if(
        definitions.cbegin(), definitions.cend(), [&]( const auto &candidate ) {
            return candidate.id == presetId;
        } );
    if ( definition == definitions.cend() ) return;
    tile_editor_preferences_t preset;
    if ( presetId == QStringLiteral( "slate" ) ) {
        preset.canvasColor = QColor( 51, 75, 96 );
        preset.minorGridColor = QColor( 60, 86, 108 );
        preset.majorGridColor = QColor( 86, 112, 135 );
        preset.wireColor = QColor( 143, 189, 217 );
        preset.wallColor = QColor( 203, 211, 219 );
        preset.stairColor = QColor( 151, 216, 183 );
        preset.uiBackgroundColor = QColor( 54, 54, 54 );
        preset.panelColor = QColor( 48, 48, 48 );
    } else if ( presetId == QStringLiteral( "hammer-charcoal" ) ) {
        preset.uiBackgroundColor = QColor( "#333333" );
        preset.panelColor = QColor( "#242424" );
        preset.textColor = QColor( "#dedbd5" );
        preset.accentColor = QColor( "#d89434" );
        preset.canvasColor = QColor( "#171717" );
        preset.perspectiveColor = QColor( "#121212" );
        preset.minorGridColor = QColor( "#2c2c2c" );
        preset.majorGridColor = QColor( "#494949" );
        preset.wireColor = QColor( "#82a3b5" );
        preset.wallColor = QColor( "#b5bbbf" );
        preset.selectionColor = QColor( "#e7a33e" );
    } else if ( presetId == QStringLiteral( "radiant-light" ) ) {
        preset.uiBackgroundColor = QColor( "#dddfe3" );
        preset.panelColor = QColor( "#f4f5f7" );
        preset.textColor = QColor( "#252a31" );
        preset.accentColor = QColor( "#aa6600" );
        preset.activeViewColor = QColor( "#b42f30" );
        preset.canvasColor = QColor( "#edf0f3" );
        preset.perspectiveColor = QColor( "#d8e0e6" );
        preset.minorGridColor = QColor( "#d4dce3" );
        preset.majorGridColor = QColor( "#a6b4c2" );
        preset.floorColor = QColor( "#91a9b5" );
        preset.selectionColor = QColor( "#a85506" );
        preset.wireColor = QColor( "#39749d" );
        preset.wallColor = QColor( "#566775" );
        preset.stairColor = QColor( "#338059" );
        preset.doorColor = QColor( "#9b6f1c" );
        preset.axisXColor = QColor( "#bd4949" );
        preset.axisYColor = QColor( "#33825b" );
        preset.axisZColor = QColor( "#426fb4" );
    } else if ( presetId == QStringLiteral( "midnight" ) ) {
        preset.uiBackgroundColor = QColor( "#171b25" );
        preset.panelColor = QColor( "#10131c" );
        preset.textColor = QColor( "#c4cedd" );
        preset.accentColor = QColor( "#9f8fff" );
        preset.activeViewColor = QColor( "#7e96e6" );
        preset.canvasColor = QColor( "#101927" );
        preset.perspectiveColor = QColor( "#0c1119" );
        preset.minorGridColor = QColor( "#1c2c3b" );
        preset.majorGridColor = QColor( "#345169" );
        preset.selectionColor = QColor( "#c5acff" );
        preset.wireColor = QColor( "#5dabd9" );
        preset.wallColor = QColor( "#9baec8" );
        preset.stairColor = QColor( "#72b5a0" );
        preset.doorColor = QColor( "#c8a366" );
    } else if ( presetId == QStringLiteral( "warm-workshop" ) ) {
        preset.uiBackgroundColor = QColor( "#3b3530" );
        preset.panelColor = QColor( "#292522" );
        preset.textColor = QColor( "#e8e0d4" );
        preset.accentColor = QColor( "#e09a3e" );
        preset.activeViewColor = QColor( "#d05b48" );
        preset.perspectiveColor = QColor( "#151310" );
        preset.canvasColor = QColor( "#26211d" );
        preset.minorGridColor = QColor( "#3b332d" );
        preset.majorGridColor = QColor( "#655446" );
        preset.floorColor = QColor( "#a4937f" );
        preset.selectionColor = QColor( "#f0a34b" );
        preset.wireColor = QColor( "#c2a785" );
        preset.wallColor = QColor( "#cfc4b5" );
        preset.stairColor = QColor( "#8fb79b" );
        preset.doorColor = QColor( "#e3b15a" );
        preset.axisXColor = QColor( "#c96759" );
        preset.axisYColor = QColor( "#78a77d" );
        preset.axisZColor = QColor( "#7294c2" );
    } else if ( presetId == QStringLiteral( "blueprint-blue" ) ) {
        preset.uiBackgroundColor = QColor( "#263747" );
        preset.panelColor = QColor( "#1b2935" );
        preset.textColor = QColor( "#dceaf2" );
        preset.accentColor = QColor( "#e39a3b" );
        preset.activeViewColor = QColor( "#e05252" );
        preset.perspectiveColor = QColor( "#0f1820" );
        preset.canvasColor = QColor( "#163047" );
        preset.minorGridColor = QColor( "#21445f" );
        preset.majorGridColor = QColor( "#3b6985" );
        preset.floorColor = QColor( "#7e9cae" );
        preset.selectionColor = QColor( "#ffae48" );
        preset.wireColor = QColor( "#67c1ec" );
        preset.wallColor = QColor( "#b5d0de" );
        preset.stairColor = QColor( "#72c5a4" );
        preset.doorColor = QColor( "#edc269" );
        preset.axisXColor = QColor( "#dc6666" );
        preset.axisYColor = QColor( "#72bd83" );
        preset.axisZColor = QColor( "#69aee8" );
    } else if ( presetId == QStringLiteral( "graphite" ) ) {
        preset.uiBackgroundColor = QColor( "#333537" );
        preset.panelColor = QColor( "#27292b" );
        preset.textColor = QColor( "#e3e5e7" );
        preset.accentColor = QColor( "#d58a31" );
        preset.activeViewColor = QColor( "#cc4f48" );
        preset.perspectiveColor = QColor( "#121314" );
        preset.canvasColor = QColor( "#1d2022" );
        preset.minorGridColor = QColor( "#303438" );
        preset.majorGridColor = QColor( "#555b60" );
        preset.floorColor = QColor( "#899198" );
        preset.selectionColor = QColor( "#e29a42" );
        preset.wireColor = QColor( "#a5b3bc" );
        preset.wallColor = QColor( "#cfd3d6" );
        preset.stairColor = QColor( "#8fbea6" );
        preset.doorColor = QColor( "#cba45e" );
        preset.axisXColor = QColor( "#c86a65" );
        preset.axisYColor = QColor( "#76a77e" );
        preset.axisZColor = QColor( "#718fbd" );
    } else if ( presetId == QStringLiteral( "high-contrast-dark" ) ) {
        preset.uiBackgroundColor = QColor( "#171717" );
        preset.panelColor = QColor( "#0d0d0d" );
        preset.textColor = QColor( "#f3f3f3" );
        preset.accentColor = QColor( "#ffb000" );
        preset.activeViewColor = QColor( "#ff4949" );
        preset.perspectiveColor = QColor( "#050505" );
        preset.canvasColor = QColor( "#080f14" );
        preset.minorGridColor = QColor( "#1d2b34" );
        preset.majorGridColor = QColor( "#637582" );
        preset.floorColor = QColor( "#a7b8c1" );
        preset.selectionColor = QColor( "#ffc247" );
        preset.wireColor = QColor( "#70d0ff" );
        preset.wallColor = QColor( "#f2f2f2" );
        preset.stairColor = QColor( "#72e0a7" );
        preset.doorColor = QColor( "#ffd166" );
        preset.axisXColor = QColor( "#ff5a5a" );
        preset.axisYColor = QColor( "#62dc79" );
        preset.axisZColor = QColor( "#61a8ff" );
    } else if ( presetId == QStringLiteral( "coastal-dusk" ) ) {
        preset.uiBackgroundColor = QColor( "#123b43" );
        preset.panelColor = QColor( "#092e36" );
        preset.textColor = QColor( "#e9e4d1" );
        preset.accentColor = QColor( "#c18b27" );
        preset.activeViewColor = QColor( "#d9574f" );
        preset.perspectiveColor = QColor( "#061f25" );
        preset.canvasColor = QColor( "#10363e" );
        preset.minorGridColor = QColor( "#204a51" );
        preset.majorGridColor = QColor( "#487078" );
        preset.floorColor = QColor( "#879a9b" );
        preset.selectionColor = QColor( "#daa13a" );
        preset.wireColor = QColor( "#3eaaa4" );
        preset.wallColor = QColor( "#cbd1c6" );
        preset.stairColor = QColor( "#8ea34a" );
        preset.doorColor = QColor( "#daae45" );
        preset.axisXColor = QColor( "#d95a51" );
        preset.axisYColor = QColor( "#88a44e" );
        preset.axisZColor = QColor( "#3c91c8" );
    } else if ( presetId == QStringLiteral( "sandstone-light" ) ) {
        preset.uiBackgroundColor = QColor( "#d8d1c5" );
        preset.panelColor = QColor( "#f2eee7" );
        preset.textColor = QColor( "#2b2a27" );
        preset.accentColor = QColor( "#9c5b16" );
        preset.activeViewColor = QColor( "#a2342d" );
        preset.perspectiveColor = QColor( "#bfc5c3" );
        preset.canvasColor = QColor( "#e6e1d7" );
        preset.minorGridColor = QColor( "#cec6b8" );
        preset.majorGridColor = QColor( "#a69b88" );
        preset.floorColor = QColor( "#9b9485" );
        preset.selectionColor = QColor( "#a85213" );
        preset.wireColor = QColor( "#356e8a" );
        preset.wallColor = QColor( "#4c5354" );
        preset.stairColor = QColor( "#34745a" );
        preset.doorColor = QColor( "#8a621b" );
        preset.axisXColor = QColor( "#b33c38" );
        preset.axisYColor = QColor( "#35764d" );
        preset.axisZColor = QColor( "#356fa3" );
    }
    for ( const auto &field : preference_fields::colors ) preferences.*field.member = preset.*field.member;
}

void TileEditorPreferences_CopyAppearance(
    tile_editor_preferences_t &destination,
    const tile_editor_preferences_t &source )
{
    const auto normalized = TileEditorPreferences_Normalize( source );
    for ( const auto &field : preference_fields::colors )
        destination.*field.member = normalized.*field.member;
    destination.uiFontPointSize = normalized.uiFontPointSize;
    destination.uiIconSize = normalized.uiIconSize;
    destination.showActiveViewBorder = normalized.showActiveViewBorder;
    destination.highlightActiveView = normalized.highlightActiveView;
}

class CypherTileEditorSettingsDialog::ColorButton final : public QPushButton
{
public:
    explicit ColorButton( const QColor &color, QWidget *pParent = nullptr )
        : QPushButton( pParent ), m_color( color.toRgb() )
    {
        m_color.setAlpha( 255 );
        setMinimumWidth( 130 );
        updatePresentation();
        connect( this, &QPushButton::clicked, this, [this] {
            const QColor original = m_color;
            QColorDialog picker( m_color, this );
            picker.setWindowTitle( tr( "Choose Color" ) );
            connect( &picker, &QColorDialog::currentColorChanged, this,
                     [this]( const QColor &chosen ) {
                if ( !chosen.isValid() ) return;
                m_color = chosen;
                updatePresentation();
                if ( m_changed ) m_changed();
            } );
            if ( picker.exec() != QDialog::Accepted ) {
                m_color = original;
                updatePresentation();
                if ( m_changed ) m_changed();
            }
            if ( m_finished ) m_finished();
        } );
    }

    QColor color() const { return m_color; }

    void setColor( const QColor &color )
    {
        m_color = color.toRgb();
        m_color.setAlpha( 255 );
        updatePresentation();
    }

    void setChangedCallback( std::function<void()> callback )
    {
        m_changed = std::move( callback );
    }

    void setFinishedCallback( std::function<void()> callback )
    {
        m_finished = std::move( callback );
    }

private:
    void updatePresentation()
    {
        setText( m_color.name( QColor::HexRgb ) );
        const int luminance = ( 299 * m_color.red() +
                                587 * m_color.green() +
                                114 * m_color.blue() ) / 1000;
        setStyleSheet( QStringLiteral(
            "QPushButton { background: %1; color: %2; }" )
            .arg(
                m_color.name( QColor::HexRgb ),
                luminance > 145 ? QStringLiteral( "#111111" )
                                : QStringLiteral( "#ffffff" ) ) );
    }

    QColor m_color{};
    std::function<void()> m_changed{};
    std::function<void()> m_finished{};
};

QList<tile_editor_shortcut_definition_t> TileEditorShortcutDefinitions()
{
    return {
        { QStringLiteral( "file.new" ), QObject::tr( "New Map" ), QKeySequence::New },
        { QStringLiteral( "file.open" ), QObject::tr( "Open Map" ), QKeySequence::Open },
        { QStringLiteral( "file.save" ), QObject::tr( "Save Map" ), QKeySequence::Save },
        { QStringLiteral( "file.saveAs" ), QObject::tr( "Save Map As" ), QKeySequence::SaveAs },
        { QStringLiteral( "edit.undo" ), QObject::tr( "Undo" ), QKeySequence::Undo },
        { QStringLiteral( "edit.redo" ), QObject::tr( "Redo" ), QKeySequence::Redo },
        { QStringLiteral( "edit.moveLeft" ), QObject::tr( "Move Selection Left" ), QKeySequence( QStringLiteral( "Left" ) ) },
        { QStringLiteral( "edit.moveRight" ), QObject::tr( "Move Selection Right" ), QKeySequence( QStringLiteral( "Right" ) ) },
        { QStringLiteral( "edit.moveUp" ), QObject::tr( "Move Selection Up" ), QKeySequence( QStringLiteral( "Up" ) ) },
        { QStringLiteral( "edit.moveDown" ), QObject::tr( "Move Selection Down" ), QKeySequence( QStringLiteral( "Down" ) ) },
        { QStringLiteral( "edit.duplicate" ), QObject::tr( "Duplicate Selection" ), QKeySequence( QStringLiteral( "Ctrl+D" ) ) },
        { QStringLiteral( "edit.delete" ), QObject::tr( "Delete Selection" ), QKeySequence( QStringLiteral( "Delete" ) ) },
        { QStringLiteral( "edit.rotate" ), QObject::tr( "Rotate Selection Clockwise" ), QKeySequence( QStringLiteral( "Ctrl+R" ) ) },
        { QStringLiteral( "edit.raise" ), QObject::tr( "Raise Selection" ), QKeySequence( Qt::Key_PageUp ) },
        { QStringLiteral( "edit.lower" ), QObject::tr( "Lower Selection" ), QKeySequence( Qt::Key_PageDown ) },
        { QStringLiteral( "edit.wallRaise" ), QObject::tr( "Increase Selected Wall Height" ), QKeySequence( QStringLiteral( "Shift+PgUp" ) ) },
        { QStringLiteral( "edit.wallLower" ), QObject::tr( "Decrease Selected Wall Height" ), QKeySequence( QStringLiteral( "Shift+PgDown" ) ) },
        { QStringLiteral( "edit.selectAll" ), QObject::tr( "Select All Authored Tiles" ), QKeySequence( QStringLiteral( "Ctrl+A" ) ) },
        { QStringLiteral( "edit.selectNone" ), QObject::tr( "Clear Selection" ), QKeySequence( QStringLiteral( "Ctrl+Shift+A" ) ) },
        { QStringLiteral( "map.validate" ), QObject::tr( "Validate Map" ), QKeySequence( QStringLiteral( "F7" ) ) },
        { QStringLiteral( "map.build" ), QObject::tr( "Build Map" ), QKeySequence( QStringLiteral( "Ctrl+B" ) ) },
        { QStringLiteral( "map.preview" ), QObject::tr( "Launch Runtime Preview" ), QKeySequence( QStringLiteral( "F6" ) ) },
        { QStringLiteral( "map.properties" ), QObject::tr( "Map Properties" ), {} },
        { QStringLiteral( "tool.select" ), QObject::tr( "Select Tool" ), QKeySequence( QStringLiteral( "V" ) ) },
        { QStringLiteral( "tool.pan" ), QObject::tr( "Pan Tool" ), QKeySequence( QStringLiteral( "H" ) ) },
        { QStringLiteral( "tool.eyedropper" ), QObject::tr( "Eyedropper Tool" ), QKeySequence( QStringLiteral( "I" ) ) },
        { QStringLiteral( "tool.paint" ), QObject::tr( "Paint Tool" ), QKeySequence( QStringLiteral( "B" ) ) },
        { QStringLiteral( "tool.erase" ), QObject::tr( "Erase Tool" ), QKeySequence( QStringLiteral( "E" ) ) },
        { QStringLiteral( "tool.rectangle" ), QObject::tr( "Rectangle Tool" ), QKeySequence( QStringLiteral( "R" ) ) },
        { QStringLiteral( "tool.line" ), QObject::tr( "Line Tool" ), QKeySequence( QStringLiteral( "L" ) ) },
        { QStringLiteral( "tool.fill" ), QObject::tr( "Fill Tool" ), QKeySequence( QStringLiteral( "G" ) ) },
        { QStringLiteral( "tool.stamp" ), QObject::tr( "Place Piece Tool" ), QKeySequence( QStringLiteral( "T" ) ) },
        { QStringLiteral( "tool.spawn" ), QObject::tr( "Player Spawn Tool" ), QKeySequence( QStringLiteral( "P" ) ) },
        { QStringLiteral( "tool.door" ), QObject::tr( "Door Tool" ), QKeySequence( QStringLiteral( "D" ) ) },
        { QStringLiteral( "view.layout2d" ), QObject::tr( "Show 2D Layout" ), QKeySequence( QStringLiteral( "Ctrl+1" ) ) },
        { QStringLiteral( "view.live3d" ), QObject::tr( "Show 3D Map" ), QKeySequence( QStringLiteral( "Ctrl+2" ) ) },
        { QStringLiteral( "view.fit" ), QObject::tr( "Frame Active View" ), QKeySequence( QStringLiteral( "F" ) ) },
        { QStringLiteral( "view.fitAll" ), QObject::tr( "Frame All Views" ), QKeySequence( QStringLiteral( "Ctrl+Shift+F" ) ) },
        { QStringLiteral( "view.four" ), QObject::tr( "Four Views" ), QKeySequence( QStringLiteral( "Ctrl+4" ) ) },
        { QStringLiteral( "view.front" ), QObject::tr( "Focus Front View" ), QKeySequence( QStringLiteral( "Ctrl+3" ) ) },
        { QStringLiteral( "view.side" ), QObject::tr( "Focus Side View" ), QKeySequence( QStringLiteral( "Ctrl+5" ) ) },
        { QStringLiteral( "view.maximize" ), QObject::tr( "Maximize Active View / Restore" ), QKeySequence( QStringLiteral( "Shift+Space" ) ) },
        { QStringLiteral( "view.console" ), QObject::tr( "Toggle Console" ), QKeySequence( QStringLiteral( "Ctrl+`" ) ) },
        { QStringLiteral( "view.shell" ), QObject::tr( "Focus Local Shell in Console" ), QKeySequence( QStringLiteral( "Ctrl+Shift+`" ) ) },
        { QStringLiteral( "view.rulers" ), QObject::tr( "Toggle Coordinate Rulers" ), QKeySequence( QStringLiteral( "Ctrl+Shift+R" ) ) },
        { QStringLiteral( "view.axes" ), QObject::tr( "Toggle XYZ Coordinate Axes" ), QKeySequence( QStringLiteral( "Ctrl+Shift+X" ) ) },
        { QStringLiteral( "view.assets" ), QObject::tr( "Toggle Materials and Pieces" ), {} },
        { QStringLiteral( "view.outliner" ), QObject::tr( "Toggle Objects" ), {} },
        { QStringLiteral( "view.properties" ), QObject::tr( "Toggle Properties" ), {} },
        { QStringLiteral( "view.toolRail" ), QObject::tr( "Toggle Tool Rail" ), {} },
        { QStringLiteral( "view.activeBorder" ), QObject::tr( "Toggle Active Viewport Border" ), {} },
        { QStringLiteral( "view.cameraHints" ), QObject::tr( "Toggle Camera Hints" ), {} },
        { QStringLiteral( "view.orthoMaterials" ), QObject::tr( "Toggle Materials in 2D Views" ), {} },
        { QStringLiteral( "view.materialLabels" ), QObject::tr( "Toggle Material Identification" ), {} },
        { QStringLiteral( "camera.fly" ), QObject::tr( "Use Fly Camera" ), {} },
        { QStringLiteral( "camera.orbit" ), QObject::tr( "Use Orbit Camera" ), {} },
        { QStringLiteral( "camera.toggleMode" ), QObject::tr( "Toggle Fly / Orbit Camera" ), {} },
        { QStringLiteral( "camera.viewPerspective" ), QObject::tr( "Camera Perspective View" ), {} },
        { QStringLiteral( "camera.viewTop" ), QObject::tr( "Camera Top View" ), {} },
        { QStringLiteral( "camera.viewBottom" ), QObject::tr( "Camera Bottom View" ), {} },
        { QStringLiteral( "camera.viewFront" ), QObject::tr( "Camera Front View" ), {} },
        { QStringLiteral( "camera.viewBack" ), QObject::tr( "Camera Back View" ), {} },
        { QStringLiteral( "camera.viewLeft" ), QObject::tr( "Camera Left View" ), {} },
        { QStringLiteral( "camera.viewRight" ), QObject::tr( "Camera Right View" ), {} },
        { QStringLiteral( "camera.level" ), QObject::tr( "Level Camera" ), QKeySequence( Qt::Key_End ) },
        { QStringLiteral( "camera.levelUp" ), QObject::tr( "Move Camera Up One Level" ), QKeySequence( QStringLiteral( "Alt+PgUp" ) ) },
        { QStringLiteral( "camera.levelDown" ), QObject::tr( "Move Camera Down One Level" ), QKeySequence( QStringLiteral( "Alt+PgDown" ) ) },
        { QStringLiteral( "camera.store1" ), QObject::tr( "Store Camera Bookmark 1" ), {} },
        { QStringLiteral( "camera.store2" ), QObject::tr( "Store Camera Bookmark 2" ), {} },
        { QStringLiteral( "camera.store3" ), QObject::tr( "Store Camera Bookmark 3" ), {} },
        { QStringLiteral( "camera.store4" ), QObject::tr( "Store Camera Bookmark 4" ), {} },
        { QStringLiteral( "camera.recall1" ), QObject::tr( "Recall Camera Bookmark 1" ), {} },
        { QStringLiteral( "camera.recall2" ), QObject::tr( "Recall Camera Bookmark 2" ), {} },
        { QStringLiteral( "camera.recall3" ), QObject::tr( "Recall Camera Bookmark 3" ), {} },
        { QStringLiteral( "camera.recall4" ), QObject::tr( "Recall Camera Bookmark 4" ), {} },
        { QStringLiteral( "camera.autoOrbit" ), QObject::tr( "Toggle Camera Auto Orbit" ), {} },
        { QStringLiteral( "camera.frameMap" ), QObject::tr( "Frame Map in 3D" ), {} },
        { QStringLiteral( "camera.faster" ), QObject::tr( "Increase Camera Speed" ), {} },
        { QStringLiteral( "camera.slower" ), QObject::tr( "Decrease Camera Speed" ), {} },
        { QStringLiteral( "camera.settings" ), QObject::tr( "Camera Settings" ), {} },
        { QStringLiteral( "app.openConfig" ), QObject::tr( "Open Editor Configuration" ), {} },
        { QStringLiteral( "app.reloadConfig" ), QObject::tr( "Reload Editor Configuration" ), {} },
        { QStringLiteral( "app.importConfig" ), QObject::tr( "Import Editor Configuration" ), {} },
        { QStringLiteral( "app.exportConfig" ), QObject::tr( "Export Editor Configuration" ), {} },
        { QStringLiteral( "app.settings" ), QObject::tr( "Editor Settings" ), QKeySequence( QStringLiteral( "Ctrl+," ) ) }
    };
}

tile_editor_preferences_t TileEditorPreferences_Load( QSettings &settings )
{
    tile_editor_preferences_t result{};
    settings.beginGroup( QStringLiteral( "TileEditor/Preferences" ) );
    for ( const auto &field : preference_fields::colors ) {
        const QVariant stored = settings.value( QString::fromLatin1( field.key ), result.*field.member );
        result.*field.member = stored.value<QColor>();
    }
    for ( const auto &field : preference_fields::integers )
        result.*field.member = ReadInteger( settings, field.key, result.*field.member );
    for ( const auto &field : preference_fields::reals )
        result.*field.member = ReadDouble( settings, field.key, result.*field.member );
    for ( const auto &field : preference_fields::booleans )
        result.*field.member = ReadBoolean( settings, field.key, result.*field.member );
    settings.endGroup();

    settings.beginGroup( QStringLiteral( "TileEditor/Shortcuts" ) );
    for ( const tile_editor_shortcut_definition_t &definition :
          TileEditorShortcutDefinitions() ) {
        const QString portable = settings.value(
            definition.id,
            definition.defaultSequence.toString( QKeySequence::PortableText ) ).toString();
        result.shortcuts.insert(
            definition.id,
            QKeySequence::fromString( portable, QKeySequence::PortableText ) );
    }
    settings.endGroup();
    return TileEditorPreferences_Normalize( result );
}

void TileEditorPreferences_Save(
    QSettings &settings,
    const tile_editor_preferences_t &preferences )
{
    const tile_editor_preferences_t normalized = TileEditorPreferences_Normalize( preferences );
    settings.beginGroup( QStringLiteral( "TileEditor/Preferences" ) );
    for ( const auto &field : preference_fields::colors )
        settings.setValue( QString::fromLatin1( field.key ), normalized.*field.member );
    for ( const auto &field : preference_fields::integers )
        settings.setValue( QString::fromLatin1( field.key ), normalized.*field.member );
    for ( const auto &field : preference_fields::reals )
        settings.setValue( QString::fromLatin1( field.key ), normalized.*field.member );
    for ( const auto &field : preference_fields::booleans )
        settings.setValue( QString::fromLatin1( field.key ), normalized.*field.member );
    settings.endGroup();

    settings.beginGroup( QStringLiteral( "TileEditor/Shortcuts" ) );
    for ( auto iShortcut = preferences.shortcuts.cbegin();
          iShortcut != preferences.shortcuts.cend(); ++iShortcut ) {
        settings.setValue(
            iShortcut.key(),
            iShortcut.value().toString( QKeySequence::PortableText ) );
    }
    settings.endGroup();
}

CypherTileEditorSettingsDialog::CypherTileEditorSettingsDialog(
    const tile_editor_preferences_t &preferences,
    QWidget *pParent )
    : QDialog( pParent ), m_preferences( TileEditorPreferences_Normalize( preferences ) )
{
    setWindowTitle( tr( "Tile Editor Settings" ) );
    resize( 840, 780 );
    setMinimumSize( 700, 600 );

    auto *pRoot = new QVBoxLayout( this );
    auto *pTabs = new QTabWidget( this );

    auto *pAppearancePage = new QWidget( pTabs );
    auto *pAppearanceLayout = new QVBoxLayout( pAppearancePage );
    auto *pThemeGroup = new QGroupBox( tr( "Color themes" ), pAppearancePage );
    pThemeGroup->setObjectName( QStringLiteral( "TileSettingsThemeStudio" ) );
    auto *pThemeLayout = new QVBoxLayout( pThemeGroup );
    auto *pPresetRow = new QHBoxLayout();
    m_pThemeSelector = new QComboBox( pThemeGroup );
    m_pThemeSelector->setObjectName( QStringLiteral( "TileSettingsColorPreset" ) );
    m_pThemeSelector->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    m_pThemeSelector->setMinimumContentsLength( 22 );
    m_pThemeSelector->setToolTip( tr(
        "Selecting a palette previews it immediately. Camera, layout, shortcuts, "
        "grid behavior and map defaults stay unchanged." ) );
    pPresetRow->addWidget( m_pThemeSelector, 1 );
    pThemeLayout->addLayout( pPresetRow );

    auto *pThemeActions = new QHBoxLayout();
    auto *pSaveTheme = new QPushButton( tr( "Save As…" ), pThemeGroup );
    pSaveTheme->setObjectName( QStringLiteral( "TileSettingsSaveTheme" ) );
    auto *pImportTheme = new QPushButton( tr( "Import…" ), pThemeGroup );
    pImportTheme->setObjectName( QStringLiteral( "TileSettingsImportTheme" ) );
    m_pExportTheme = new QPushButton( tr( "Export Current…" ), pThemeGroup );
    m_pExportTheme->setObjectName( QStringLiteral( "TileSettingsExportTheme" ) );
    m_pDeleteTheme = new QPushButton( tr( "Delete" ), pThemeGroup );
    m_pDeleteTheme->setObjectName( QStringLiteral( "TileSettingsDeleteTheme" ) );
    pThemeActions->addWidget( pSaveTheme );
    pThemeActions->addWidget( pImportTheme );
    pThemeActions->addWidget( m_pExportTheme );
    pThemeActions->addStretch( 1 );
    pThemeActions->addWidget( m_pDeleteTheme );
    pThemeLayout->addLayout( pThemeActions );
    m_pThemeStatus = new QLabel( pThemeGroup );
    m_pThemeStatus->setObjectName( QStringLiteral( "TileSettingsThemeStatus" ) );
    m_pThemeStatus->setWordWrap( true );
    m_pThemeStatus->setTextInteractionFlags( Qt::TextSelectableByMouse );
    m_pThemeStatus->setProperty( "muted", true );
    pThemeLayout->addWidget( m_pThemeStatus );
    pAppearanceLayout->addWidget( pThemeGroup );

    auto *pAppearanceScroll = new QScrollArea( pAppearancePage );
    pAppearanceScroll->setWidgetResizable( true );
    pAppearanceScroll->setFrameShape( QFrame::NoFrame );
    auto *pAppearanceContent = new QWidget( pAppearanceScroll );
    auto *pAppearanceContentLayout = new QVBoxLayout( pAppearanceContent );
    auto *pInterfaceGroup = new QGroupBox( tr( "Interface scale and focus" ), pAppearanceContent );
    auto *pAppearanceForm = new QFormLayout( pInterfaceGroup );
    m_pUiFontPointSize = new QSpinBox( pInterfaceGroup );
    m_pUiFontPointSize->setObjectName( QStringLiteral( "TileSettingsUiFontPointSize" ) );
    m_pUiFontPointSize->setRange( 9, 18 );
    m_pUiFontPointSize->setSuffix( tr( " pt" ) );
    m_pUiFontPointSize->setValue( m_preferences.uiFontPointSize );
    m_pUiIconSize = new QSpinBox( pInterfaceGroup );
    m_pUiIconSize->setObjectName( QStringLiteral( "TileSettingsUiIconSize" ) );
    m_pUiIconSize->setRange( 16, 40 );
    m_pUiIconSize->setSingleStep( 2 );
    m_pUiIconSize->setSuffix( tr( " px" ) );
    m_pUiIconSize->setValue( m_preferences.uiIconSize );
    pAppearanceForm->addRow( tr( "Interface text and control size" ), m_pUiFontPointSize );
    pAppearanceForm->addRow( tr( "Toolbar icon size" ), m_pUiIconSize );
    m_pShowActiveViewBorder = new QCheckBox( tr( "Show active viewport border" ), pInterfaceGroup );
    m_pShowActiveViewBorder->setObjectName( QStringLiteral( "TileSettingsShowActiveViewBorder" ) );
    m_pShowActiveViewBorder->setChecked( m_preferences.showActiveViewBorder );
    m_pShowActiveViewBorder->setToolTip( tr(
        "Outline the active pane using the Active viewport border color below. "
        "Header highlighting is configured separately." ) );
    pAppearanceForm->addRow( m_pShowActiveViewBorder );
    m_pHighlightActiveView = new QCheckBox( tr( "Highlight active viewport header" ), pInterfaceGroup );
    m_pHighlightActiveView->setObjectName( QStringLiteral( "TileSettingsHighlightActiveView" ) );
    m_pHighlightActiveView->setChecked( m_preferences.highlightActiveView );
    m_pHighlightActiveView->setToolTip( tr(
        "Use a different title-bar background for the pane receiving input. "
        "This does not enable activation when hovering over a pane." ) );
    pAppearanceForm->addRow( m_pHighlightActiveView );
    pAppearanceContentLayout->addWidget( pInterfaceGroup );

    QMap<QString, QFormLayout *> colorGroups;
    for ( const QString &groupName : {
              tr( "Interface" ),
              tr( "Viewports and grid" ),
              tr( "Geometry and selection" ),
              tr( "Coordinate axes" ) } ) {
        auto *pGroup = new QGroupBox( groupName, pAppearanceContent );
        pGroup->setObjectName( QStringLiteral( "TileSettingsColorGroup_%1" )
                                  .arg( groupName.simplified().replace( QLatin1Char( ' ' ), QLatin1Char( '_' ) ) ) );
        auto *pForm = new QFormLayout( pGroup );
        pForm->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );
        colorGroups.insert( groupName, pForm );
        pAppearanceContentLayout->addWidget( pGroup );
    }
    for ( const auto &field : preference_fields::colors ) {
        auto *pColor = new ColorButton( m_preferences.*field.member, pAppearanceContent );
        pColor->setObjectName( QStringLiteral( "TileSettingsColor_%1" ).arg( QString::fromLatin1( field.key ) ) );
        m_appearanceColors.insert( QString::fromLatin1( field.key ), pColor );
        colorGroups.value( tr( field.group ) )->addRow( tr( field.label ), pColor );
    }
    auto *pResetColors = new QPushButton( tr( "Reset to Radiant Dark" ), pAppearanceContent );
    pResetColors->setObjectName( QStringLiteral( "TileSettingsResetColors" ) );
    pResetColors->setToolTip( tr( "Reset only theme colors. All other editor settings stay unchanged." ) );
    pAppearanceContentLayout->addWidget( pResetColors, 0, Qt::AlignLeft );
    pAppearanceContentLayout->addStretch( 1 );
    m_pCanvasColor = m_appearanceColors.value( QStringLiteral( "canvasColor" ) );
    m_pMinorGridColor = m_appearanceColors.value( QStringLiteral( "minorGridColor" ) );
    m_pMajorGridColor = m_appearanceColors.value( QStringLiteral( "majorGridColor" ) );
    m_pSelectionColor = m_appearanceColors.value( QStringLiteral( "selectionColor" ) );
    pAppearanceScroll->setWidget( pAppearanceContent );
    pAppearanceLayout->addWidget( pAppearanceScroll, 1 );
    auto *pConfigHint = new QLabel(
        tr( "Editable configuration: %1\nUse Edit > Reload Editor Configuration after editing this file. "
            "Saving Settings regenerates the file; dock and window placement are stored separately." )
            .arg( TileEditorConfig_DefaultPath() ), pAppearancePage );
    pConfigHint->setObjectName( QStringLiteral( "TileSettingsConfigPath" ) );
    pConfigHint->setWordWrap( true );
    pConfigHint->setTextInteractionFlags( Qt::TextSelectableByMouse );
    pConfigHint->setProperty( "muted", true );
    pAppearanceLayout->addWidget( pConfigHint );
    refreshThemeChoices();
    connect( m_pThemeSelector, qOverload<int>( &QComboBox::currentIndexChanged ),
             this, [this] {
        updateThemeActions();
        applySelectedTheme();
    } );
    connect( pSaveTheme, &QPushButton::clicked,
             this, [this] { saveCurrentTheme(); } );
    connect( pImportTheme, &QPushButton::clicked,
             this, [this] { importTheme(); } );
    connect( m_pExportTheme, &QPushButton::clicked,
             this, [this] { exportCurrentTheme(); } );
    connect( m_pDeleteTheme, &QPushButton::clicked,
             this, [this] { deleteSelectedTheme(); } );
    connect( pResetColors, &QPushButton::clicked, this, [this] {
        setAppearanceColors( tile_editor_preferences_t{} );
        refreshThemeChoices();
        m_pThemeStatus->setText( tr( "Default colors are being previewed. Press Apply or OK to save them." ) );
        previewAppearance();
    } );
    pTabs->addTab( pAppearancePage, tr( "Appearance" ) );

    auto *pGridPage = new QWidget( pTabs );
    auto *pGridForm = new QFormLayout( pGridPage );
    m_pMajorGridEvery = new QSpinBox( pGridPage );
    m_pMajorGridEvery->setRange( 2, 64 );
    m_pMajorGridEvery->setValue( preferences.majorGridEvery );
    pGridForm->addRow( tr( "Major line every" ), m_pMajorGridEvery );
    m_pShowGrid = new QCheckBox( tr( "Show grid in orthographic views" ), pGridPage );
    m_pShowGrid->setChecked( preferences.showGrid );
    m_pShowMarkers = new QCheckBox( tr( "Show spawn and door markers on the top map" ), pGridPage );
    m_pShowMarkers->setChecked( preferences.showMarkers );
    m_pWireframeOrtho = new QCheckBox( tr( "Draw orthographic geometry as wireframes" ), pGridPage );
    m_pWireframeOrtho->setObjectName( QStringLiteral( "TileSettingsWireframeOrtho" ) );
    m_pWireframeOrtho->setChecked( preferences.wireframeOrtho );
    m_pWireframeOrtho->setToolTip( tr(
        "Controls fallback presentation when Show materials in 2D views is off: "
        "enabled uses technical outlines and subtle fills; disabled uses blockout-colored surfaces." ) );
    m_pShowOrthoMaterials = new QCheckBox( tr( "Show materials in 2D views" ), pGridPage );
    m_pShowOrthoMaterials->setObjectName( QStringLiteral( "TileSettingsShowOrthoMaterials" ) );
    m_pShowOrthoMaterials->setChecked( m_preferences.showOrthoMaterials );
    m_pShowOrthoMaterials->setToolTip( tr(
        "Draw previews of bound project materials in Top, Front and Side. "
        "Unbound material slots use their blockout color." ) );
    m_pShowMaterialLabels = new QCheckBox( tr( "Show material identification" ), pGridPage );
    m_pShowMaterialLabels->setObjectName( QStringLiteral( "TileSettingsShowMaterialLabels" ) );
    m_pShowMaterialLabels->setChecked( m_preferences.showMaterialLabels );
    m_pShowMaterialLabels->setToolTip( tr(
        "Identify the material used by geometry in the 2D views, including bound "
        "project materials and unbound blockout slots." ) );
    m_pOrthoMaterialOpacity = new QDoubleSpinBox( pGridPage );
    m_pOrthoMaterialOpacity->setObjectName( QStringLiteral( "TileSettingsOrthoMaterialOpacity" ) );
    m_pOrthoMaterialOpacity->setDecimals( 1 );
    m_pOrthoMaterialOpacity->setRange( 20.0, 100.0 );
    m_pOrthoMaterialOpacity->setSingleStep( 5.0 );
    m_pOrthoMaterialOpacity->setSuffix( tr( " %" ) );
    m_pOrthoMaterialOpacity->setValue( m_preferences.orthoMaterialOpacity * 100.0 );
    m_pOrthoMaterialOpacity->setEnabled( m_preferences.showOrthoMaterials );
    m_pOrthoMaterialOpacity->setToolTip( tr(
        "Controls how strongly material previews and blockout colors fill 2D geometry. "
        "Lower opacity leaves more of the viewport background visible." ) );
    connect( m_pShowOrthoMaterials, &QCheckBox::toggled, m_pOrthoMaterialOpacity, &QWidget::setEnabled );
    m_pGridSpacingCells = new QComboBox( pGridPage );
    for ( int spacing : { 1, 2, 4, 8, 16 } ) m_pGridSpacingCells->addItem( tr( "%1 cells" ).arg( spacing ), spacing );
    m_pGridSpacingCells->setCurrentIndex( m_pGridSpacingCells->findData( preferences.gridSpacingCells ) );
    m_pGridSpacingCells->setToolTip( tr( "Controls displayed grid lines. Does not change map geometry or snap cell size." ) );
    m_pAdaptiveGrid = new QCheckBox( tr( "Automatically adjust grid density when zooming" ), pGridPage );
    m_pAdaptiveGrid->setObjectName( QStringLiteral( "TileSettingsAdaptiveGrid" ) );
    m_pAdaptiveGrid->setChecked( preferences.adaptiveGrid );
    m_pAdaptiveGrid->setToolTip( tr(
        "Spaces displayed grid lines farther apart as you zoom out. "
        "Map geometry and editing snap stay unchanged." ) );
    m_pGridMinimumPixels = new QSpinBox( pGridPage );
    m_pGridMinimumPixels->setObjectName( QStringLiteral( "TileSettingsGridMinimumPixels" ) );
    m_pGridMinimumPixels->setRange( 4, 64 );
    m_pGridMinimumPixels->setSuffix( tr( " px" ) );
    m_pGridMinimumPixels->setValue( preferences.gridMinimumPixels );
    m_pGridMinimumPixels->setEnabled( preferences.adaptiveGrid );
    m_pGridMinimumPixels->setToolTip( tr(
        "Minimum spacing between displayed grid lines when automatic grid density is enabled." ) );
    connect( m_pAdaptiveGrid, &QCheckBox::toggled,
             m_pGridMinimumPixels, &QWidget::setEnabled );
    pGridForm->addRow( m_pShowGrid );
    pGridForm->addRow( m_pShowMarkers );
    pGridForm->addRow( m_pWireframeOrtho );
    m_pInternalTileEdges = new QCheckBox( tr( "Show internal tile seams in Top wireframe" ), pGridPage );
    m_pInternalTileEdges->setObjectName( "TileSettingsInternalTileEdges" );
    m_pInternalTileEdges->setChecked( m_preferences.showInternalTileEdges );
    m_pInternalTileEdges->setToolTip( tr( "Disable for clean room outlines. Material, height and shape boundaries remain visible." ) );
    pGridForm->addRow( m_pInternalTileEdges );
    m_pShowFloorSurfaces = new QCheckBox(
        tr( "Show floor and stair surfaces in 2D views" ), pGridPage );
    m_pShowFloorSurfaces->setObjectName(
        QStringLiteral( "TileSettingsShowFloorSurfaces" ) );
    m_pShowFloorSurfaces->setChecked( m_preferences.showFloorSurfaces );
    m_pShowFloorSurfaces->setToolTip( tr(
        "Hide walkable floor and stair surfaces while preserving generated wall outlines, markers and editing." ) );
    pGridForm->addRow( m_pShowFloorSurfaces );
    m_pShowWallHeight = new QCheckBox(
        tr( "Show wall height in Front and Side views" ), pGridPage );
    m_pShowWallHeight->setObjectName(
        QStringLiteral( "TileSettingsShowWallHeight" ) );
    m_pShowWallHeight->setChecked( m_preferences.showWallHeight );
    m_pShowWallHeight->setToolTip( tr(
        "Draw the generated vertical wall bodies in elevation views. Top-view wall outlines remain available." ) );
    pGridForm->addRow( m_pShowWallHeight );
    m_pShowWallThickness = new QCheckBox(
        tr( "Show true wall thickness in 2D views" ), pGridPage );
    m_pShowWallThickness->setObjectName(
        QStringLiteral( "TileSettingsShowWallThickness" ) );
    m_pShowWallThickness->setChecked( m_preferences.showWallThickness );
    m_pShowWallThickness->setToolTip( tr(
        "Draw wall strips using the same physical thickness as generated map geometry. Disable for single-line technical outlines." ) );
    pGridForm->addRow( m_pShowWallThickness );
    pGridForm->addRow( m_pShowOrthoMaterials );
    pGridForm->addRow( m_pShowMaterialLabels );
    pGridForm->addRow( tr( "2D material opacity" ), m_pOrthoMaterialOpacity );
    pGridForm->addRow( tr( "Display grid spacing" ), m_pGridSpacingCells );
    pGridForm->addRow( m_pAdaptiveGrid );
    pGridForm->addRow( tr( "Minimum grid spacing" ), m_pGridMinimumPixels );
    m_pShowViewMetrics = new QCheckBox( tr( "Show viewport coordinates, scale and geometry metrics" ), pGridPage );
    m_pShowViewMetrics->setObjectName( QStringLiteral( "TileSettingsShowViewMetrics" ) );
    m_pShowViewMetrics->setChecked( m_preferences.showViewMetrics );
    m_pShowAuthoringFooter = new QCheckBox( tr( "Show authoring guidance in 2D view footers" ), pGridPage );
    m_pShowAuthoringFooter->setObjectName( QStringLiteral( "TileSettingsShowAuthoringFooter" ) );
    m_pShowAuthoringFooter->setChecked( m_preferences.showAuthoringFooter );
    m_pShowAuthoringFooter->setToolTip( tr(
        "Show the fixed construction slice and projected editing constraint at the bottom of Front and Side views." ) );
    m_pShowCoordinateRulers = new QCheckBox( tr( "Show coordinate rulers in 2D views" ), pGridPage );
    m_pShowCoordinateRulers->setObjectName( QStringLiteral( "TileSettingsShowCoordinateRulers" ) );
    m_pShowCoordinateRulers->setChecked( m_preferences.showCoordinateRulers );
    m_pShowCoordinateRulers->setToolTip( tr(
        "Draw world-coordinate numbers along the top and left edges of Top, Front and Side views." ) );
    m_pShowViewAxes = new QCheckBox( tr( "Show colored coordinate axes and 3D orientation triad" ), pGridPage );
    m_pShowViewAxes->setObjectName( QStringLiteral( "TileSettingsShowViewAxes" ) );
    m_pShowViewAxes->setChecked( m_preferences.showViewAxes );
    m_pShowViewAxes->setToolTip( tr(
        "Use the conventional X red, Y green and Z blue visual language in every viewport. "
        "Axis colors remain editable in Appearance and only affect display guides." ) );
    m_pCenterViewAxes = new QCheckBox( tr( "Center coordinate axes on the map" ), pGridPage );
    m_pCenterViewAxes->setObjectName( QStringLiteral( "TileSettingsCenterViewAxes" ) );
    m_pCenterViewAxes->setChecked( m_preferences.centerViewAxes );
    m_pCenterViewAxes->setEnabled( m_preferences.showViewAxes );
    m_pCenterViewAxes->setToolTip( tr(
        "Place the horizontal map axes at the document center. Disable to show the authored origin. "
        "Neither option moves geometry, changes cell coordinates, or changes editing snap." ) );
    connect( m_pShowViewAxes, &QCheckBox::toggled, m_pCenterViewAxes, &QWidget::setEnabled );
    m_pDepthCueWireframe = new QCheckBox( tr( "Distinguish front and back geometry with line depth" ), pGridPage );
    m_pDepthCueWireframe->setObjectName( QStringLiteral( "TileSettingsDepthCueWireframe" ) );
    m_pDepthCueWireframe->setChecked( m_preferences.depthCueWireframe );
    m_pDepthCueWireframe->setToolTip( tr( "Uses subtler outlines for geometry farther from each orthographic view." ) );
    m_pWireLineWidth = new QDoubleSpinBox( pGridPage );
    m_pWireLineWidth->setObjectName( QStringLiteral( "TileSettingsWireLineWidth" ) );
    m_pWireLineWidth->setDecimals( 1 );
    m_pWireLineWidth->setRange( 0.5, 3.0 );
    m_pWireLineWidth->setSingleStep( 0.1 );
    m_pWireLineWidth->setSuffix( tr( " px" ) );
    m_pWireLineWidth->setValue( m_preferences.wireLineWidth );
    m_pEmptyViewCellPixels = new QSpinBox( pGridPage );
    m_pEmptyViewCellPixels->setObjectName( QStringLiteral( "TileSettingsEmptyViewCellPixels" ) );
    m_pEmptyViewCellPixels->setRange( 12, 96 );
    m_pEmptyViewCellPixels->setSuffix( tr( " px/cell" ) );
    m_pEmptyViewCellPixels->setValue( m_preferences.emptyViewCellPixels );
    m_pEmptyViewCellPixels->setToolTip( tr( "Initial framing scale for maps without geometry. Larger values make nearby cells easier to see." ) );
    pGridForm->addRow( m_pShowViewMetrics );
    pGridForm->addRow( m_pShowAuthoringFooter );
    pGridForm->addRow( m_pShowCoordinateRulers );
    pGridForm->addRow( m_pShowViewAxes );
    pGridForm->addRow( m_pCenterViewAxes );
    pGridForm->addRow( m_pDepthCueWireframe );
    pGridForm->addRow( tr( "Wire outline thickness" ), m_pWireLineWidth );
    pGridForm->addRow( tr( "Empty map starting scale" ), m_pEmptyViewCellPixels );
    auto *pGridScroll = new QScrollArea( pTabs );
    pGridScroll->setWidgetResizable( true );
    pGridScroll->setFrameShape( QFrame::NoFrame );
    pGridScroll->setWidget( pGridPage );
    pTabs->addTab( pGridScroll, tr( "Canvas & Grid" ) );

    auto *pDocumentPage = new QWidget( pTabs );
    auto *pDocumentForm = new QFormLayout( pDocumentPage );
    m_pDefaultWidth = new QSpinBox( pDocumentPage );
    m_pDefaultHeight = new QSpinBox( pDocumentPage );
    m_pDefaultWidth->setRange( 1, static_cast<int>( TILE_MAP_MAX_WIDTH ) );
    m_pDefaultHeight->setRange( 1, static_cast<int>( TILE_MAP_MAX_HEIGHT ) );
    m_pDefaultWidth->setValue( preferences.defaultMapWidth );
    m_pDefaultHeight->setValue( preferences.defaultMapHeight );
    pDocumentForm->addRow( tr( "New map width" ), m_pDefaultWidth );
    pDocumentForm->addRow( tr( "New map height" ), m_pDefaultHeight );
    auto *pHint = new QLabel(
        tr( "These dimensions are used by File > New. Existing maps keep their authored size." ),
        pDocumentPage );
    pHint->setWordWrap( true );
    pHint->setProperty( "muted", true );
    pDocumentForm->addRow( pHint );
    m_pStartMaximized = new QCheckBox( tr( "Start the editor maximized" ), pDocumentPage );
    m_pStartMaximized->setChecked( preferences.startMaximized );
    m_pFrameMapOnOpen = new QCheckBox( tr( "Frame all views when opening or creating a map" ), pDocumentPage );
    m_pFrameMapOnOpen->setChecked( preferences.frameMapOnOpen );
    m_pLinkOrthographicCameras = new QCheckBox(
        tr( "Link pan and zoom across 2D views" ),
        pDocumentPage );
    m_pLinkOrthographicCameras->setObjectName(
        QStringLiteral( "TileSettingsLinkOrthographicCameras" ) );
    m_pLinkOrthographicCameras->setChecked(
        preferences.linkOrthographicCameras );
    m_pLinkOrthographicCameras->setToolTip( tr(
        "Optional. When enabled, Top, Front and Side share compatible pan axes "
        "and zoom scale. Leave this disabled when every pane should keep an "
        "independent camera." ) );
    m_pActivateViewOnHover = new QCheckBox(
        tr( "Route shortcuts and navigation to the viewport under the pointer" ),
        pDocumentPage );
    m_pActivateViewOnHover->setObjectName( QStringLiteral( "TileSettingsActivateViewOnHover" ) );
    m_pActivateViewOnHover->setChecked( preferences.activateViewOnHover );
    m_pActivateViewOnHover->setToolTip( tr(
        "Recommended. Moving into a viewport activates and focuses it immediately, so Space-pan, "
        "camera keys, tools and view shortcuts work without a preparatory click. Menus, dialogs "
        "and active mouse drags remain protected." ) );
    m_pViewSplitterWidth = new QSpinBox( pDocumentPage );
    m_pViewSplitterWidth->setObjectName( QStringLiteral( "TileSettingsViewSplitterWidth" ) );
    m_pViewSplitterWidth->setRange( 3, 16 );
    m_pViewSplitterWidth->setSuffix( tr( " px" ) );
    m_pViewSplitterWidth->setValue( preferences.viewSplitterWidth );
    m_pViewSplitterWidth->setToolTip( tr(
        "Thickness of the dark draggable gutters between viewport surfaces. "
        "Six pixels matches the default compact four-view layout." ) );
    pDocumentForm->addRow( m_pStartMaximized );
    pDocumentForm->addRow( m_pFrameMapOnOpen );
    pDocumentForm->addRow( m_pLinkOrthographicCameras );
    pDocumentForm->addRow( m_pActivateViewOnHover );
    pDocumentForm->addRow( tr( "Viewport separator thickness" ), m_pViewSplitterWidth );
    pTabs->addTab( pDocumentPage, tr( "Workspace" ) );

    auto *pCameraPage = new QWidget( pTabs );
    auto *pCameraForm = new QFormLayout( pCameraPage );
    m_pCameraFlyMode = new QCheckBox( tr( "Use fly navigation with right mouse button" ), pCameraPage );
    m_pCameraFlyMode->setObjectName( QStringLiteral( "TileSettingsCameraFlyMode" ) );
    m_pCameraFlyMode->setChecked( m_preferences.cameraFlyMode );
    m_pCameraFlyMode->setToolTip( tr(
        "When disabled, right mouse dragging orbits the map instead." ) );
    m_pCameraMoveSpeed = new QDoubleSpinBox( pCameraPage );
    m_pCameraMoveSpeed->setObjectName( QStringLiteral( "TileSettingsCameraSpeed" ) );
    m_pCameraMoveSpeed->setDecimals( 2 );
    m_pCameraMoveSpeed->setRange( 0.1, 1000.0 );
    m_pCameraMoveSpeed->setSingleStep( 1.0 );
    m_pCameraMoveSpeed->setSuffix( tr( " units/s" ) );
    m_pCameraMoveSpeed->setValue( m_preferences.cameraMoveSpeed );
    m_pCameraMoveSpeed->setToolTip( tr( "Base movement speed in map world units per second." ) );
    m_pCameraLookSensitivity = new QDoubleSpinBox( pCameraPage );
    m_pCameraLookSensitivity->setObjectName( QStringLiteral( "TileSettingsCameraSensitivity" ) );
    m_pCameraLookSensitivity->setDecimals( 2 );
    m_pCameraLookSensitivity->setRange( 0.01, 2.0 );
    m_pCameraLookSensitivity->setSingleStep( 0.01 );
    m_pCameraLookSensitivity->setSuffix( tr( " deg/px" ) );
    m_pCameraLookSensitivity->setValue( m_preferences.cameraLookSensitivity );
    m_pCameraLookSensitivity->setToolTip( tr( "Degrees of camera rotation per pixel of mouse movement." ) );
    m_pCameraFieldOfView = new QDoubleSpinBox( pCameraPage );
    m_pCameraFieldOfView->setObjectName( QStringLiteral( "TileSettingsCameraFov" ) );
    m_pCameraFieldOfView->setDecimals( 1 );
    m_pCameraFieldOfView->setRange( 30.0, 100.0 );
    m_pCameraFieldOfView->setSingleStep( 1.0 );
    m_pCameraFieldOfView->setSuffix( tr( " degrees" ) );
    m_pCameraFieldOfView->setValue( m_preferences.cameraFieldOfView );
    m_pCameraFieldOfView->setToolTip( tr( "Vertical field of view in the 3D map viewport." ) );
    m_pCameraInvertY = new QCheckBox( tr( "Invert vertical mouse look" ), pCameraPage );
    m_pCameraInvertY->setObjectName( QStringLiteral( "TileSettingsCameraInvertY" ) );
    m_pCameraInvertY->setChecked( m_preferences.cameraInvertY );
    auto makeMultiplier = [pCameraPage]( const char *pName, double minimum, double maximum,
                                        double value, double step, const QString &tooltip ) {
        auto *pControl = new QDoubleSpinBox( pCameraPage );
        pControl->setObjectName( QString::fromLatin1( pName ) );
        pControl->setDecimals( 2 );
        pControl->setRange( minimum, maximum );
        pControl->setSingleStep( step );
        pControl->setSuffix( QStringLiteral( " ×" ) );
        pControl->setValue( value );
        pControl->setToolTip( tooltip );
        return pControl;
    };
    m_pCameraPanSensitivity = makeMultiplier( "TileSettingsCameraPanSensitivity", 0.1, 5.0,
        m_preferences.cameraPanSensitivity, 0.1,
        tr( "Scales middle mouse panning in the 3D view. A value of 1 follows the normal screen-space distance." ) );
    m_pCameraZoomSensitivity = makeMultiplier( "TileSettingsCameraZoomSensitivity", 0.1, 5.0,
        m_preferences.cameraZoomSensitivity, 0.1,
        tr( "Scales Fly dolly, right-mouse fly-speed adjustment and Orbit distance in the 3D view. It does not change field of view." ) );
    m_pCameraFastMultiplier = makeMultiplier( "TileSettingsCameraFastMultiplier", 1.0, 20.0,
        m_preferences.cameraFastMultiplier, 0.5,
        tr( "Multiplies fly movement speed while Shift is held." ) );
    m_pCameraSlowMultiplier = makeMultiplier( "TileSettingsCameraSlowMultiplier", 0.01, 1.0,
        m_preferences.cameraSlowMultiplier, 0.05,
        tr( "Multiplies fly movement speed while Ctrl is held. Lower values give finer movement." ) );
    m_pCameraInvertWheel = new QCheckBox( tr( "Invert 3D mouse wheel" ), pCameraPage );
    m_pCameraInvertWheel->setObjectName( QStringLiteral( "TileSettingsCameraInvertWheel" ) );
    m_pCameraInvertWheel->setChecked( m_preferences.cameraInvertWheel );
    m_pShowCameraHints = new QCheckBox( tr( "Show 3D navigation hints" ), pCameraPage );
    m_pShowCameraHints->setObjectName( QStringLiteral( "TileSettingsShowCameraHints" ) );
    m_pShowCameraHints->setChecked( m_preferences.showCameraHints );
    m_pShowCameraHints->setToolTip( tr( "Show camera control help in the 3D pane. Rendering errors remain visible." ) );
    pCameraForm->addRow( m_pCameraFlyMode );
    pCameraForm->addRow( tr( "Movement speed" ), m_pCameraMoveSpeed );
    pCameraForm->addRow( tr( "Mouse sensitivity" ), m_pCameraLookSensitivity );
    pCameraForm->addRow( tr( "Pan sensitivity" ), m_pCameraPanSensitivity );
    pCameraForm->addRow( tr( "Wheel navigation sensitivity" ), m_pCameraZoomSensitivity );
    pCameraForm->addRow( tr( "Shift speed multiplier" ), m_pCameraFastMultiplier );
    pCameraForm->addRow( tr( "Ctrl speed multiplier" ), m_pCameraSlowMultiplier );
    pCameraForm->addRow( tr( "Field of view" ), m_pCameraFieldOfView );
    pCameraForm->addRow( m_pCameraInvertY );
    pCameraForm->addRow( m_pCameraInvertWheel );
    pCameraForm->addRow( m_pShowCameraHints );
    auto *pCameraHint = new QLabel( tr(
        "Hold the right mouse button in the 3D view to look around and use W/A/S/D to move. "
        "Q moves down and E moves up. After pressing right mouse, hold Shift to move faster or Ctrl to move slower.\n\n"
        "Alt + right mouse drag orbits the map. Middle mouse drag pans the camera. "
        "Shift accelerates right-mouse flight regardless of whether it is pressed before or after the mouse button. "
        "The mouse wheel dollies in Fly mode. While right-mouse look is held, the wheel changes fly speed. "
        "In Orbit mode it changes camera distance around the inspected point.\n\n"
        "Use Apply to save changes without closing Settings. "
        "Cancel discards only changes made since the last Apply." ), pCameraPage );
    pCameraHint->setObjectName( QStringLiteral( "TileSettingsCameraHelp" ) );
    pCameraHint->setWordWrap( true );
    pCameraHint->setProperty( "muted", true );
    pCameraForm->addRow( pCameraHint );
    auto *pCameraScroll = new QScrollArea( pTabs );
    pCameraScroll->setWidgetResizable( true );
    pCameraScroll->setFrameShape( QFrame::NoFrame );
    pCameraScroll->setWidget( pCameraPage );
    pTabs->addTab( pCameraScroll, tr( "Camera" ) );

    auto *pShortcutPage = new QWidget( pTabs );
    auto *pShortcutLayout = new QVBoxLayout( pShortcutPage );
    m_pShortcutTable = new QTableWidget( pShortcutPage );
    m_pShortcutTable->setColumnCount( 2 );
    m_pShortcutTable->setHorizontalHeaderLabels(
        { tr( "Command" ), tr( "Shortcut" ) } );
    const QList<tile_editor_shortcut_definition_t> definitions =
        TileEditorShortcutDefinitions();
    m_pShortcutTable->setRowCount( definitions.size() );
    for ( int i = 0; i < definitions.size(); ++i ) {
        const tile_editor_shortcut_definition_t &definition = definitions[i];
        auto *pName = new QTableWidgetItem( definition.label );
        pName->setData( Qt::UserRole, definition.id );
        pName->setFlags( pName->flags() & ~Qt::ItemIsEditable );
        m_pShortcutTable->setItem( i, 0, pName );
        auto *pEditor = new QKeySequenceEdit(
            preferences.shortcuts.value(
                definition.id,
                definition.defaultSequence ),
            m_pShortcutTable );
        pEditor->setClearButtonEnabled( true );
        m_pShortcutTable->setCellWidget( i, 1, pEditor );
        connect( pEditor, &QKeySequenceEdit::keySequenceChanged, this,
                 [this] { updateShortcutConflicts(); } );
    }
    m_pShortcutTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch );
    m_pShortcutTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents );
    m_pShortcutTable->verticalHeader()->setVisible( false );
    pShortcutLayout->addWidget( m_pShortcutTable );
    m_pShortcutConflict = new QLabel( pShortcutPage );
    m_pShortcutConflict->setWordWrap( true );
    pShortcutLayout->addWidget( m_pShortcutConflict );
    pTabs->addTab( pShortcutPage, tr( "Shortcuts" ) );

    pRoot->addWidget( pTabs );
    auto *pButtons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel |
            QDialogButtonBox::RestoreDefaults,
        this );
    m_pButtons = pButtons;
    pButtons->button( QDialogButtonBox::RestoreDefaults )->setText( tr( "Reset All Settings" ) );
    pButtons->button( QDialogButtonBox::RestoreDefaults )->setToolTip( tr(
        "Reset appearance, viewport, camera, workspace, map defaults and every shortcut." ) );
    connect( pButtons->button( QDialogButtonBox::RestoreDefaults ), &QPushButton::clicked, this, [this] {
        const tile_editor_preferences_t defaults{};
        for ( const auto &field : preference_fields::colors )
            m_appearanceColors.value( QString::fromLatin1( field.key ) )->setColor( defaults.*field.member );
        m_pUiFontPointSize->setValue( defaults.uiFontPointSize );
        m_pUiIconSize->setValue( defaults.uiIconSize );
        m_pShowActiveViewBorder->setChecked( defaults.showActiveViewBorder );
        m_pHighlightActiveView->setChecked( defaults.highlightActiveView );
        m_pShowViewMetrics->setChecked( defaults.showViewMetrics );
        m_pShowAuthoringFooter->setChecked( defaults.showAuthoringFooter );
        m_pShowCoordinateRulers->setChecked( defaults.showCoordinateRulers );
        m_pShowViewAxes->setChecked( defaults.showViewAxes );
        m_pCenterViewAxes->setChecked( defaults.centerViewAxes );
        m_pDepthCueWireframe->setChecked( defaults.depthCueWireframe );
        m_pWireLineWidth->setValue( defaults.wireLineWidth );
        m_pEmptyViewCellPixels->setValue( defaults.emptyViewCellPixels );
        m_pViewSplitterWidth->setValue( defaults.viewSplitterWidth );
        m_pCanvasColor->setColor( defaults.canvasColor );
        m_pMinorGridColor->setColor( defaults.minorGridColor );
        m_pMajorGridColor->setColor( defaults.majorGridColor );
        m_pSelectionColor->setColor( defaults.selectionColor );
        m_pMajorGridEvery->setValue( defaults.majorGridEvery );
        m_pGridSpacingCells->setCurrentIndex( 0 );
        m_pShowGrid->setChecked( true );
        m_pAdaptiveGrid->setChecked( defaults.adaptiveGrid );
        m_pGridMinimumPixels->setValue( defaults.gridMinimumPixels );
        m_pShowMarkers->setChecked( true );
        m_pWireframeOrtho->setChecked( defaults.wireframeOrtho );
        m_pInternalTileEdges->setChecked( defaults.showInternalTileEdges );
        m_pShowFloorSurfaces->setChecked( defaults.showFloorSurfaces );
        m_pShowWallHeight->setChecked( defaults.showWallHeight );
        m_pShowWallThickness->setChecked( defaults.showWallThickness );
        m_pShowOrthoMaterials->setChecked( defaults.showOrthoMaterials );
        m_pShowMaterialLabels->setChecked( defaults.showMaterialLabels );
        m_pOrthoMaterialOpacity->setValue( defaults.orthoMaterialOpacity * 100.0 );
        m_pStartMaximized->setChecked( true );
        m_pFrameMapOnOpen->setChecked( true );
        m_pLinkOrthographicCameras->setChecked( defaults.linkOrthographicCameras );
        m_pActivateViewOnHover->setChecked( defaults.activateViewOnHover );
        m_pCameraMoveSpeed->setValue( defaults.cameraMoveSpeed );
        m_pCameraLookSensitivity->setValue( defaults.cameraLookSensitivity );
        m_pCameraPanSensitivity->setValue( defaults.cameraPanSensitivity );
        m_pCameraZoomSensitivity->setValue( defaults.cameraZoomSensitivity );
        m_pCameraFastMultiplier->setValue( defaults.cameraFastMultiplier );
        m_pCameraSlowMultiplier->setValue( defaults.cameraSlowMultiplier );
        m_pCameraFieldOfView->setValue( defaults.cameraFieldOfView );
        m_pCameraInvertY->setChecked( defaults.cameraInvertY );
        m_pCameraInvertWheel->setChecked( defaults.cameraInvertWheel );
        m_pCameraFlyMode->setChecked( defaults.cameraFlyMode );
        m_pShowCameraHints->setChecked( defaults.showCameraHints );
        m_pDefaultWidth->setValue( defaults.defaultMapWidth );
        m_pDefaultHeight->setValue( defaults.defaultMapHeight );
        const auto definitions = TileEditorShortcutDefinitions();
        for ( int i = 0; i < definitions.size(); ++i ) {
            static_cast<QKeySequenceEdit *>( m_pShortcutTable->cellWidget( i, 1 ) )
                ->setKeySequence( definitions[i].defaultSequence );
        }
        refreshThemeChoices();
        m_pThemeStatus->setText( tr(
            "All editor defaults are staged. Press Apply or OK to save them." ) );
        previewAppearance();
    } );
    connect( pButtons->button( QDialogButtonBox::Apply ), &QPushButton::clicked, this, [this] {
        updateShortcutConflicts();
        if ( !m_pShortcutConflict->text().isEmpty() ) return;
        const auto candidate = this->preferences();
        if ( !m_applyCallback || m_applyCallback( candidate ) )
            m_preferences = candidate;
    } );
    connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
    connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
    pRoot->addWidget( pButtons );
    for ( ColorButton *pColor : std::as_const( m_appearanceColors ) )
    {
        pColor->setChangedCallback( [this] {
            markThemeModified();
            previewAppearance();
        } );
        pColor->setFinishedCallback( [this] { refreshThemeChoices(); } );
    }
    for ( QSpinBox *pSpin : { m_pUiFontPointSize, m_pUiIconSize } ) {
        connect( pSpin, qOverload<int>( &QSpinBox::valueChanged ),
                 this, [this] { previewAppearance(); } );
    }
    connect( m_pShowActiveViewBorder, &QCheckBox::toggled,
             this, [this] { previewAppearance(); } );
    connect( m_pHighlightActiveView, &QCheckBox::toggled,
             this, [this] { previewAppearance(); } );
    updateShortcutConflicts();
}

void CypherTileEditorSettingsDialog::setAppearanceColors(
    const tile_editor_preferences_t &preferences )
{
    const auto normalized = TileEditorPreferences_Normalize( preferences );
    for ( const auto &field : preference_fields::colors ) {
        if ( auto *pColor = m_appearanceColors.value( QString::fromLatin1( field.key ) ) )
            pColor->setColor( normalized.*field.member );
    }
}

void CypherTileEditorSettingsDialog::previewAppearance()
{
    if ( !m_previewCallback ) return;
    auto preview = m_preferences;
    TileEditorPreferences_CopyAppearance( preview, preferences() );
    m_previewCallback( preview );
}

void CypherTileEditorSettingsDialog::refreshThemeChoices( const QString &selectedPath )
{
    if ( m_pThemeSelector == nullptr ) return;
    const QSignalBlocker blocker( m_pThemeSelector );
    m_pThemeSelector->clear();
    for ( const auto &preset : TileEditorColorPresetDefinitions() ) {
        m_pThemeSelector->addItem( preset.label, preset.id );
        m_pThemeSelector->setItemData(
            m_pThemeSelector->count() - 1, BUILT_IN_THEME, THEME_KIND_ROLE );
    }

    QStringList errors;
    const auto themes = TileEditorUserThemes_Discover(
        TileEditorUserThemes_DefaultDirectory(), &errors );
    if ( !themes.isEmpty() ) m_pThemeSelector->insertSeparator( m_pThemeSelector->count() );
    for ( const auto &theme : themes ) {
        m_pThemeSelector->addItem( theme.name, theme.path );
        const int index = m_pThemeSelector->count() - 1;
        m_pThemeSelector->setItemData( index, USER_THEME, THEME_KIND_ROLE );
        m_pThemeSelector->setItemData( index, theme.path, Qt::ToolTipRole );
    }

    int selection = -1;
    if ( !selectedPath.isEmpty() ) {
        for ( int i = 0; i < m_pThemeSelector->count(); ++i ) {
            if ( m_pThemeSelector->itemData( i, THEME_KIND_ROLE ).toInt() == USER_THEME &&
                 QFileInfo( m_pThemeSelector->itemData( i ).toString() ) == QFileInfo( selectedPath ) ) {
                selection = i;
                break;
            }
        }
    }
    auto current = m_preferences;
    for ( const auto &field : preference_fields::colors ) {
        if ( const auto *pColor = m_appearanceColors.value( QString::fromLatin1( field.key ) ) )
            current.*field.member = pColor->color();
    }
    if ( selection < 0 ) {
        for ( int i = 0; i < m_pThemeSelector->count(); ++i ) {
            tile_editor_preferences_t candidate = current;
            if ( m_pThemeSelector->itemData( i, THEME_KIND_ROLE ).toInt() == BUILT_IN_THEME ) {
                TileEditorPreferences_ApplyColorPreset(
                    candidate, m_pThemeSelector->itemData( i ).toString() );
            } else if ( m_pThemeSelector->itemData( i, THEME_KIND_ROLE ).toInt() == USER_THEME ) {
                const auto match = std::find_if( themes.cbegin(), themes.cend(), [&]( const auto &theme ) {
                    return QFileInfo( theme.path ) == QFileInfo( m_pThemeSelector->itemData( i ).toString() );
                } );
                if ( match == themes.cend() ) continue;
                TileEditorTheme_CopyColors( candidate, match->colors );
            } else continue;
            bool equal = true;
            for ( const auto &field : preference_fields::colors )
                equal = equal && candidate.*field.member == current.*field.member;
            if ( equal ) {
                selection = i;
                break;
            }
        }
    }
    if ( selection < 0 ) {
        if ( m_pThemeSelector->count() > 0 )
            m_pThemeSelector->insertSeparator( m_pThemeSelector->count() );
        m_pThemeSelector->addItem( tr( "Custom (modified)" ), QString() );
        selection = m_pThemeSelector->count() - 1;
        m_pThemeSelector->setItemData( selection, MODIFIED_THEME, THEME_KIND_ROLE );
    }
    m_pThemeSelector->setCurrentIndex( selection );
    updateThemeActions();
    if ( !errors.isEmpty() ) {
        m_pThemeStatus->setText( tr( "%1 invalid theme file(s) were skipped. %2" )
                                     .arg( errors.size() )
                                     .arg( errors.first() ) );
        m_pThemeStatus->setProperty( "error", true );
        m_pThemeStatus->style()->unpolish( m_pThemeStatus );
        m_pThemeStatus->style()->polish( m_pThemeStatus );
    } else {
        m_pThemeStatus->setProperty( "error", false );
    }
}

void CypherTileEditorSettingsDialog::updateThemeActions()
{
    if ( m_pThemeSelector == nullptr ) return;
    const int kind = m_pThemeSelector->currentData( THEME_KIND_ROLE ).toInt();
    const bool userTheme = kind == USER_THEME;
    if ( m_pDeleteTheme != nullptr ) m_pDeleteTheme->setEnabled( userTheme );
    if ( m_pExportTheme != nullptr ) m_pExportTheme->setEnabled( true );
    if ( m_pThemeStatus == nullptr ) return;
    m_pThemeStatus->setProperty( "error", false );
    if ( kind == MODIFIED_THEME ) {
        m_pThemeStatus->setText( tr(
            "The current colors do not match a saved theme. Save As keeps this palette for reuse." ) );
    } else if ( userTheme ) {
        m_pThemeStatus->setText( tr( "User theme: %1" )
                                     .arg( m_pThemeSelector->currentData().toString() ) );
    } else {
        m_pThemeStatus->setText( tr(
            "Built-in palette. Selection previews immediately; Save As creates an editable user theme." ) );
    }
    m_pThemeStatus->style()->unpolish( m_pThemeStatus );
    m_pThemeStatus->style()->polish( m_pThemeStatus );
}

void CypherTileEditorSettingsDialog::markThemeModified()
{
    if ( m_pThemeSelector == nullptr ) return;
    int index = m_pThemeSelector->findData( MODIFIED_THEME, THEME_KIND_ROLE );
    if ( index < 0 ) {
        if ( m_pThemeSelector->count() > 0 )
            m_pThemeSelector->insertSeparator( m_pThemeSelector->count() );
        m_pThemeSelector->addItem( tr( "Custom (modified)" ), QString() );
        index = m_pThemeSelector->count() - 1;
        m_pThemeSelector->setItemData( index, MODIFIED_THEME, THEME_KIND_ROLE );
    }
    m_pThemeSelector->setCurrentIndex( index );
}

void CypherTileEditorSettingsDialog::applySelectedTheme()
{
    if ( m_pThemeSelector == nullptr || m_pThemeSelector->currentIndex() < 0 ) return;
    auto candidate = preferences();
    const int kind = m_pThemeSelector->currentData( THEME_KIND_ROLE ).toInt();
    if ( kind == BUILT_IN_THEME ) {
        TileEditorPreferences_ApplyColorPreset(
            candidate, m_pThemeSelector->currentData().toString() );
        m_pThemeStatus->setText( tr( "%1 is being previewed. Press Apply or OK to save it." )
                                     .arg( m_pThemeSelector->currentText() ) );
    } else if ( kind == USER_THEME ) {
        tile_editor_user_theme_t theme;
        QString error;
        if ( !TileEditorUserTheme_Load(
                 m_pThemeSelector->currentData().toString(), theme, error ) ) {
            QMessageBox::warning( this, tr( "Load Theme" ), error );
            refreshThemeChoices();
            return;
        }
        TileEditorTheme_CopyColors( candidate, theme.colors );
        m_pThemeStatus->setText( tr( "%1 is being previewed. Press Apply or OK to save it." )
                                     .arg( theme.name ) );
    } else return;
    setAppearanceColors( candidate );
    previewAppearance();
}

void CypherTileEditorSettingsDialog::saveCurrentTheme()
{
    QString initial = m_pThemeSelector != nullptr
        ? m_pThemeSelector->currentText() : tr( "Custom Theme" );
    if ( m_pThemeSelector != nullptr &&
         m_pThemeSelector->currentData( THEME_KIND_ROLE ).toInt() == BUILT_IN_THEME )
        initial += tr( " Custom" );
    else if ( m_pThemeSelector != nullptr &&
              m_pThemeSelector->currentData( THEME_KIND_ROLE ).toInt() == MODIFIED_THEME )
        initial = tr( "Custom Theme" );
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, tr( "Save Color Theme" ), tr( "Theme name" ),
        QLineEdit::Normal, initial, &accepted ).simplified();
    if ( !accepted || name.isEmpty() ) return;
    const QString path = TileEditorUserThemes_PathForName(
        TileEditorUserThemes_DefaultDirectory(), name );
    if ( QFileInfo::exists( path ) &&
         QMessageBox::question(
             this, tr( "Replace Theme" ),
             tr( "A theme named %1 already exists. Replace it?" ).arg( name ),
             QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) != QMessageBox::Yes ) return;
    QString error;
    if ( !TileEditorUserTheme_Save( path, name, preferences(), error ) ) {
        QMessageBox::warning( this, tr( "Save Theme" ), error );
        return;
    }
    refreshThemeChoices( path );
    m_pThemeStatus->setText( tr( "Saved user theme: %1" ).arg( path ) );
}

void CypherTileEditorSettingsDialog::importTheme()
{
    const QString sourcePath = QFileDialog::getOpenFileName(
        this, tr( "Import Color Theme" ), QString(),
        tr( "Cypher Color Themes (*.cytheme);;All Files (*)" ) );
    if ( sourcePath.isEmpty() ) return;
    tile_editor_user_theme_t imported;
    QString error;
    if ( !TileEditorUserTheme_Load( sourcePath, imported, error ) ) {
        QMessageBox::warning( this, tr( "Import Theme" ), error );
        return;
    }
    const QString destination = TileEditorUserThemes_PathForName(
        TileEditorUserThemes_DefaultDirectory(), imported.name );
    if ( QFileInfo::exists( destination ) &&
         QFileInfo( destination ) != QFileInfo( sourcePath ) &&
         QMessageBox::question(
             this, tr( "Replace Theme" ),
             tr( "The user theme %1 already exists. Replace it?" ).arg( imported.name ),
             QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) != QMessageBox::Yes ) return;
    if ( !TileEditorUserTheme_Save(
             destination, imported.name, imported.colors, error ) ) {
        QMessageBox::warning( this, tr( "Import Theme" ), error );
        return;
    }
    refreshThemeChoices( destination );
    applySelectedTheme();
}

void CypherTileEditorSettingsDialog::exportCurrentTheme()
{
    QString suggested = m_pThemeSelector != nullptr
        ? m_pThemeSelector->currentText() : tr( "Cypher Theme" );
    if ( m_pThemeSelector != nullptr &&
         m_pThemeSelector->currentData( THEME_KIND_ROLE ).toInt() == MODIFIED_THEME )
        suggested = tr( "Custom Theme" );
    suggested = QFileInfo( TileEditorUserThemes_PathForName( QString(), suggested ) ).fileName();
    QString path = QFileDialog::getSaveFileName(
        this, tr( "Export Current Color Theme" ), suggested,
        tr( "Cypher Color Themes (*.cytheme)" ) );
    if ( path.isEmpty() ) return;
    if ( !path.endsWith( QStringLiteral( ".cytheme" ), Qt::CaseInsensitive ) )
        path += QStringLiteral( ".cytheme" );
    QString name = QFileInfo( path ).completeBaseName();
    if ( m_pThemeSelector != nullptr && !m_pThemeSelector->currentText().isEmpty() &&
         m_pThemeSelector->currentData( THEME_KIND_ROLE ).toInt() != MODIFIED_THEME )
        name = m_pThemeSelector->currentText();
    QString error;
    if ( !TileEditorUserTheme_Save( path, name, preferences(), error ) )
        QMessageBox::warning( this, tr( "Export Theme" ), error );
    else
        m_pThemeStatus->setText( tr( "Exported current colors to %1" ).arg( path ) );
}

void CypherTileEditorSettingsDialog::deleteSelectedTheme()
{
    if ( m_pThemeSelector == nullptr ||
         m_pThemeSelector->currentData( THEME_KIND_ROLE ).toInt() != USER_THEME ) return;
    const QString path = m_pThemeSelector->currentData().toString();
    if ( QMessageBox::question(
             this, tr( "Delete User Theme" ),
             tr( "Delete the user theme %1?" ).arg( m_pThemeSelector->currentText() ),
             QMessageBox::Yes | QMessageBox::No, QMessageBox::No ) != QMessageBox::Yes ) return;
    if ( !QFile::remove( path ) ) {
        QMessageBox::warning(
            this, tr( "Delete Theme" ), tr( "Could not delete theme file: %1" ).arg( path ) );
        return;
    }
    refreshThemeChoices();
    m_pThemeStatus->setText( tr( "User theme deleted. Current colors are unchanged." ) );
}

void CypherTileEditorSettingsDialog::updateShortcutConflicts()
{
    if ( m_pButtons == nullptr || m_pShortcutConflict == nullptr ) return;
    QMap<QString, QString> assigned;
    QString conflict;
    for ( int i = 0; i < m_pShortcutTable->rowCount(); ++i ) {
        const auto *pEditor = static_cast<QKeySequenceEdit *>( m_pShortcutTable->cellWidget( i, 1 ) );
        if ( pEditor == nullptr || pEditor->keySequence().isEmpty() ) continue;
        const QString key = pEditor->keySequence().toString( QKeySequence::PortableText );
        const QString command = m_pShortcutTable->item( i, 0 )->text();
        if ( assigned.contains( key ) ) {
            conflict = tr( "%1 is assigned to both %2 and %3. Choose a unique shortcut." )
                .arg( key, assigned.value( key ), command );
            break;
        }
        assigned.insert( key, command );
    }
    m_pShortcutConflict->setText( conflict );
    m_pButtons->button( QDialogButtonBox::Ok )->setEnabled( conflict.isEmpty() );
    m_pButtons->button( QDialogButtonBox::Apply )->setEnabled( conflict.isEmpty() );
}

void CypherTileEditorSettingsDialog::setApplyCallback(
    std::function<bool( const tile_editor_preferences_t & )> callback )
{
    m_applyCallback = std::move( callback );
}

void CypherTileEditorSettingsDialog::setPreviewCallback(
    std::function<void( const tile_editor_preferences_t & )> callback )
{
    m_previewCallback = std::move( callback );
}

tile_editor_preferences_t CypherTileEditorSettingsDialog::preferences() const
{
    tile_editor_preferences_t result = m_preferences;
    for ( const auto &field : preference_fields::colors )
        result.*field.member = m_appearanceColors.value( QString::fromLatin1( field.key ) )->color();
    result.uiFontPointSize = m_pUiFontPointSize->value();
    result.uiIconSize = m_pUiIconSize->value();
    result.showActiveViewBorder = m_pShowActiveViewBorder->isChecked();
    result.highlightActiveView = m_pHighlightActiveView->isChecked();
    result.showViewMetrics = m_pShowViewMetrics->isChecked();
    result.showAuthoringFooter = m_pShowAuthoringFooter->isChecked();
    result.showCoordinateRulers = m_pShowCoordinateRulers->isChecked();
    result.showViewAxes = m_pShowViewAxes->isChecked();
    result.centerViewAxes = m_pCenterViewAxes->isChecked();
    result.depthCueWireframe = m_pDepthCueWireframe->isChecked();
    result.wireLineWidth = m_pWireLineWidth->value();
    result.emptyViewCellPixels = m_pEmptyViewCellPixels->value();
    result.viewSplitterWidth = m_pViewSplitterWidth->value();
    result.canvasColor = m_pCanvasColor->color();
    result.minorGridColor = m_pMinorGridColor->color();
    result.majorGridColor = m_pMajorGridColor->color();
    result.selectionColor = m_pSelectionColor->color();
    result.majorGridEvery = m_pMajorGridEvery->value();
    result.gridSpacingCells = m_pGridSpacingCells->currentData().toInt();
    result.gridMinimumPixels = m_pGridMinimumPixels->value();
    result.showGrid = m_pShowGrid->isChecked();
    result.adaptiveGrid = m_pAdaptiveGrid->isChecked();
    result.showMarkers = m_pShowMarkers->isChecked();
    result.wireframeOrtho = m_pWireframeOrtho->isChecked();
    result.showInternalTileEdges = m_pInternalTileEdges->isChecked();
    result.showFloorSurfaces = m_pShowFloorSurfaces->isChecked();
    result.showWallHeight = m_pShowWallHeight->isChecked();
    result.showWallThickness = m_pShowWallThickness->isChecked();
    result.showOrthoMaterials = m_pShowOrthoMaterials->isChecked();
    result.showMaterialLabels = m_pShowMaterialLabels->isChecked();
    result.orthoMaterialOpacity = m_pOrthoMaterialOpacity->value() / 100.0;
    result.startMaximized = m_pStartMaximized->isChecked();
    result.frameMapOnOpen = m_pFrameMapOnOpen->isChecked();
    result.linkOrthographicCameras = m_pLinkOrthographicCameras->isChecked();
    result.activateViewOnHover = m_pActivateViewOnHover->isChecked();
    result.cameraMoveSpeed = m_pCameraMoveSpeed->value();
    result.cameraLookSensitivity = m_pCameraLookSensitivity->value();
    result.cameraPanSensitivity = m_pCameraPanSensitivity->value();
    result.cameraZoomSensitivity = m_pCameraZoomSensitivity->value();
    result.cameraFastMultiplier = m_pCameraFastMultiplier->value();
    result.cameraSlowMultiplier = m_pCameraSlowMultiplier->value();
    result.cameraFieldOfView = m_pCameraFieldOfView->value();
    result.cameraInvertY = m_pCameraInvertY->isChecked();
    result.cameraInvertWheel = m_pCameraInvertWheel->isChecked();
    result.cameraFlyMode = m_pCameraFlyMode->isChecked();
    result.showCameraHints = m_pShowCameraHints->isChecked();
    result.defaultMapWidth = m_pDefaultWidth->value();
    result.defaultMapHeight = m_pDefaultHeight->value();
    result.shortcuts.clear();
    for ( int i = 0; i < m_pShortcutTable->rowCount(); ++i ) {
        const QString id = m_pShortcutTable->item( i, 0 )
            ->data( Qt::UserRole ).toString();
        const auto *pEditor = qobject_cast<QKeySequenceEdit *>(
            m_pShortcutTable->cellWidget( i, 1 ) );
        if ( pEditor != nullptr ) {
            result.shortcuts.insert( id, pEditor->keySequence() );
        }
    }
    return TileEditorPreferences_Normalize( result );
}

} // namespace cypher::tools::tile_editor
