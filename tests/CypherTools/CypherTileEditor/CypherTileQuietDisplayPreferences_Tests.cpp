//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Quiet viewport defaults, optional display axes and additional palettes.
//////////////////////////////////////////////////////////////////////////
#include "CypherTileCanvas.h"
#include "CypherTileEditorConfig.h"
#include "CypherTileEditorTheme.h"

#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QImage>
#include <QMap>
#include <QPalette>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

using namespace cypher::tools::tile_editor;

namespace
{
QApplication &QuietDisplayApplication()
{
    if ( auto *application = qobject_cast<QApplication *>( QApplication::instance() ) ) return *application;
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    static int argc = 1;
    static char name[] = "TileQuietDisplayTests";
    static char *argv[]{ name, nullptr };
    static QApplication application( argc, argv );
    return application;
}

QString HeaderBackground( const QString &style, bool active )
{
    const QRegularExpression expression( active
        ? QStringLiteral( R"(QWidget#TileViewHeader\[active="true"\] \{ background: ([^;]+);)" )
        : QStringLiteral( R"(QWidget#TileViewHeader \{ background: ([^;]+);)" ) );
    return expression.match( style ).captured( 1 );
}

double RelativeLuminance( const QColor &color )
{
    const auto linear = []( int channel ) {
        const double value = static_cast<double>( channel ) / 255.0;
        return value <= 0.04045 ? value / 12.92
                               : std::pow( ( value + 0.055 ) / 1.055, 2.4 );
    };
    return 0.2126 * linear( color.red() ) +
           0.7152 * linear( color.green() ) +
           0.0722 * linear( color.blue() );
}

double ContrastRatio( const QColor &first, const QColor &second )
{
    const double lighter = std::max( RelativeLuminance( first ), RelativeLuminance( second ) );
    const double darker = std::min( RelativeLuminance( first ), RelativeLuminance( second ) );
    return ( lighter + 0.05 ) / ( darker + 0.05 );
}
} // namespace

TEST_CASE( "Viewport defaults are quiet while explicit display preferences persist",
    "[TileEditor][Config][Display]" )
{
    QuietDisplayApplication();
    tile_editor_preferences_t preferences;
    CHECK_FALSE( preferences.showViewMetrics );
    CHECK_FALSE( preferences.showAuthoringFooter );
    CHECK( preferences.showViewAxes );
    CHECK( preferences.axisXColor.red() > preferences.axisXColor.green() );
    CHECK( preferences.axisYColor.green() > preferences.axisYColor.red() );
    CHECK( preferences.axisZColor.blue() > preferences.axisZColor.red() );
    CHECK_FALSE( preferences.showCameraHints );
    CHECK_FALSE( preferences.showMaterialLabels );
    CHECK_FALSE( preferences.showInternalTileEdges );
    CHECK_FALSE( preferences.highlightActiveView );
    CHECK_FALSE( preferences.showActiveViewBorder );
    CHECK( preferences.activateViewOnHover );
    CHECK( preferences.centerViewAxes );
    preferences.showViewMetrics = true;
    preferences.showAuthoringFooter = true;
    preferences.showViewAxes = true;
    preferences.centerViewAxes = false;
    preferences.showCameraHints = true;
    preferences.showMaterialLabels = true;
    preferences.showInternalTileEdges = true;
    preferences.highlightActiveView = true;
    preferences.activateViewOnHover = true;
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QSettings native( directory.filePath( "native.ini" ), QSettings::IniFormat );
    TileEditorPreferences_Save( native, preferences );
    const auto loaded = TileEditorPreferences_Load( native );
    QString error;
    const QString path = directory.filePath( "editor.ini" );
    REQUIRE( TileEditorConfig_Save( path, loaded, error ) );
    tile_editor_preferences_t reloaded;
    REQUIRE( TileEditorConfig_Load( path, reloaded, error ) );
    CHECK( reloaded.showViewMetrics );
    CHECK( reloaded.showAuthoringFooter );
    CHECK( reloaded.showViewAxes );
    CHECK_FALSE( reloaded.centerViewAxes );
    CHECK( reloaded.showCameraHints );
    CHECK( reloaded.showMaterialLabels );
    CHECK( reloaded.showInternalTileEdges );
    CHECK( reloaded.highlightActiveView );
    CHECK( reloaded.activateViewOnHover );
    CypherTileEditorSettingsDialog dialog( reloaded );
    auto *axes = dialog.findChild<QCheckBox *>( "TileSettingsShowViewAxes" );
    auto *authoringFooter = dialog.findChild<QCheckBox *>( "TileSettingsShowAuthoringFooter" );
    auto *center = dialog.findChild<QCheckBox *>( "TileSettingsCenterViewAxes" );
    auto *highlight = dialog.findChild<QCheckBox *>( "TileSettingsHighlightActiveView" );
    REQUIRE( axes ); REQUIRE( authoringFooter ); REQUIRE( center ); REQUIRE( highlight );
    CHECK( authoringFooter->isChecked() );
    authoringFooter->setChecked( false );
    CHECK_FALSE( dialog.preferences().showAuthoringFooter );
    CHECK( center->isEnabled() );
    axes->setChecked( false );
    CHECK_FALSE( center->isEnabled() );
    CHECK_FALSE( dialog.preferences().showViewAxes );
    CHECK_FALSE( dialog.preferences().centerViewAxes );
    CHECK( highlight->isChecked() );
}

TEST_CASE( "Additional color presets keep behavior intact and active header emphasis is optional",
    "[TileEditor][Theme][Display]" )
{
    auto &application = QuietDisplayApplication();
    tile_editor_preferences_t preferences;
    preferences.cameraMoveSpeed = 35.0;
    preferences.uiFontPointSize = 13;
    preferences.showViewAxes = true;
    preferences.centerViewAxes = false;
    const QMap<QString, QColor> expectedCanvases{
        { QStringLiteral( "radiant-dark" ), QColor( "#19242f" ) },
        { QStringLiteral( "slate" ), QColor( "#334b60" ) },
        { QStringLiteral( "hammer-charcoal" ), QColor( "#171717" ) },
        { QStringLiteral( "radiant-light" ), QColor( "#edf0f3" ) },
        { QStringLiteral( "midnight" ), QColor( "#101927" ) },
        { QStringLiteral( "warm-workshop" ), QColor( "#26211d" ) },
        { QStringLiteral( "blueprint-blue" ), QColor( "#163047" ) },
        { QStringLiteral( "graphite" ), QColor( "#1d2022" ) },
        { QStringLiteral( "high-contrast-dark" ), QColor( "#080f14" ) },
        { QStringLiteral( "coastal-dusk" ), QColor( "#10363e" ) },
        { QStringLiteral( "sandstone-light" ), QColor( "#e6e1d7" ) }
    };
    const auto definitions = TileEditorColorPresetDefinitions();
    CHECK( definitions.size() == expectedCanvases.size() );
    QSet<QString> ids;
    QSet<QString> canvasColors;
    for ( const auto &definition : definitions ) {
        INFO( definition.id.toStdString() );
        CHECK_FALSE( definition.id.isEmpty() );
        CHECK_FALSE( definition.label.isEmpty() );
        CHECK_FALSE( ids.contains( definition.id ) );
        ids.insert( definition.id );
        REQUIRE( expectedCanvases.contains( definition.id ) );
        TileEditorPreferences_ApplyColorPreset( preferences, definition.id );
        CHECK( preferences.canvasColor == expectedCanvases.value( definition.id ) );
        canvasColors.insert( preferences.canvasColor.name( QColor::HexRgb ) );
        CHECK( preferences.cameraMoveSpeed == 35.0 );
        CHECK( preferences.uiFontPointSize == 13 );
        CHECK( preferences.showViewAxes );
        CHECK_FALSE( preferences.centerViewAxes );
        CHECK( ContrastRatio( preferences.textColor, preferences.panelColor ) >= 4.5 );
        CHECK( ContrastRatio( preferences.textColor, preferences.uiBackgroundColor ) >= 4.5 );
        CypherTileEditorTheme_Apply( application, preferences );
        CHECK_FALSE( application.styleSheet().contains( '@' ) );
        CHECK( application.palette().color( QPalette::Text ) == preferences.textColor );
        CHECK( application.palette().color( QPalette::Base ) == preferences.panelColor );
        const auto inactive = HeaderBackground( application.styleSheet(), false );
        REQUIRE_FALSE( inactive.isEmpty() );
        CHECK( HeaderBackground( application.styleSheet(), true ) == inactive );
        if ( definition.id == "radiant-light" || definition.id == "sandstone-light" ) {
            CHECK( preferences.textColor.lightness() < preferences.panelColor.lightness() );
            CHECK( preferences.panelColor.lightness() > 200 );
        }
    }
    CHECK( canvasColors.size() == definitions.size() );
    const QColor previousCanvas = preferences.canvasColor;
    const QColor previousAccent = preferences.accentColor;
    TileEditorPreferences_ApplyColorPreset( preferences, QStringLiteral( "not-a-preset" ) );
    CHECK( preferences.canvasColor == previousCanvas );
    CHECK( preferences.accentColor == previousAccent );
    preferences.highlightActiveView = true;
    CypherTileEditorTheme_Apply( application, preferences );
    CHECK( HeaderBackground( application.styleSheet(), false ) != HeaderBackground( application.styleSheet(), true ) );
    CypherTileEditorSettingsDialog dialog( preferences );
    const auto *presets = dialog.findChild<QComboBox *>( "TileSettingsColorPreset" );
    REQUIRE( presets );
    CHECK( presets->count() == definitions.size() );
    for ( const auto &definition : definitions ) {
        const int index = presets->findData( definition.id );
        REQUIRE( index >= 0 );
        CHECK( presets->itemText( index ) == definition.label );
    }
    CypherTileEditorTheme_Apply( application );
}

TEST_CASE( "Camera view and bookmark shortcut definitions are complete and collision-free",
    "[TileEditor][Preferences][Camera]" )
{
    const auto definitions = TileEditorShortcutDefinitions();
    QMap<QString, QKeySequence> shortcuts;
    QMap<QString, int> assignedSequenceCounts;
    for ( const auto &definition : definitions ) {
        INFO( definition.id.toStdString() );
        CHECK_FALSE( shortcuts.contains( definition.id ) );
        shortcuts.insert( definition.id, definition.defaultSequence );
        if ( !definition.defaultSequence.isEmpty() ) {
            const QString portable = definition.defaultSequence.toString( QKeySequence::PortableText );
            ++assignedSequenceCounts[portable];
        }
    }

    const QStringList unbound{
        QStringLiteral( "camera.viewPerspective" ), QStringLiteral( "camera.viewTop" ),
        QStringLiteral( "camera.viewBottom" ),
        QStringLiteral( "camera.viewFront" ), QStringLiteral( "camera.viewBack" ),
        QStringLiteral( "camera.viewLeft" ), QStringLiteral( "camera.viewRight" ),
        QStringLiteral( "camera.store1" ), QStringLiteral( "camera.store2" ),
        QStringLiteral( "camera.store3" ), QStringLiteral( "camera.store4" ),
        QStringLiteral( "camera.recall1" ), QStringLiteral( "camera.recall2" ),
        QStringLiteral( "camera.recall3" ), QStringLiteral( "camera.recall4" ),
        QStringLiteral( "camera.toggleMode" ), QStringLiteral( "camera.autoOrbit" )
    };
    for ( const QString &id : unbound ) {
        REQUIRE( shortcuts.contains( id ) );
        CHECK( shortcuts.value( id ).isEmpty() );
    }

    const QMap<QString, QKeySequence> assigned{
        { QStringLiteral( "camera.level" ), QKeySequence( Qt::Key_End ) },
        { QStringLiteral( "camera.levelUp" ), QKeySequence( QStringLiteral( "Alt+PgUp" ) ) },
        { QStringLiteral( "camera.levelDown" ), QKeySequence( QStringLiteral( "Alt+PgDown" ) ) }
    };
    for ( auto i = assigned.cbegin(); i != assigned.cend(); ++i ) {
        REQUIRE( shortcuts.contains( i.key() ) );
        CHECK( shortcuts.value( i.key() ) == i.value() );
        CHECK( assignedSequenceCounts.value(
            i.value().toString( QKeySequence::PortableText ) ) == 1 );
    }
}

TEST_CASE( "Top-view axes are optional centered guides that preserve authored coordinates",
    "[TileEditor][Display][Axes]" )
{
    QuietDisplayApplication();
    CypherTileDocumentBridge bridge;
    QString error;
    REQUIRE( bridge.newDocument( { 10u, 8u, 2.0f, 2.0f }, &error ) );
    bridge.markSaved();
    CypherTileCanvas canvas;
    canvas.resize( 600, 480 );
    canvas.setDocumentBridge( &bridge );
    canvas.setTool( tile_canvas_tool_t::SELECT );
    tile_editor_preferences_t preferences;
    preferences.showGrid = false;
    preferences.showViewAxes = false;
    preferences.axisXColor = QColor( "#f045ae" );
    preferences.axisYColor = QColor( "#30e693" );
    canvas.setPreferences( preferences );
    canvas.show();
    QApplication::processEvents();
    const QPointF origin = canvas.viewOrigin();
    const auto cellSize = bridge.document()->nCellSize;
    const auto revision = bridge.document()->nCurrentRevision;
    const QPointF center = origin + QPointF( 5 * canvas.zoomFactor(), 4 * canvas.zoomFactor() );
    const QPoint sample = ( center + QPointF( 15.0, 0.0 ) ).toPoint();
    const auto off = canvas.grab().toImage();
    preferences.showViewAxes = true;
    canvas.setPreferences( preferences );
    const auto centered = canvas.grab().toImage();
    CHECK( centered.pixelColor( sample ) != off.pixelColor( sample ) );
    preferences.centerViewAxes = false;
    canvas.setPreferences( preferences );
    const auto atOrigin = canvas.grab().toImage();
    CHECK( atOrigin.pixelColor( sample ) == off.pixelColor( sample ) );
    const QPoint originSample = ( origin + QPointF( 15.0, 0.0 ) ).toPoint();
    CHECK( atOrigin.pixelColor( originSample ) != off.pixelColor( originSample ) );
    CHECK( canvas.viewOrigin() == origin );
    CHECK( bridge.document()->nCellSize == cellSize );
    CHECK( bridge.document()->nCurrentRevision == revision );
    CHECK_FALSE( bridge.isDirty() );
}
