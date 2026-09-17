//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileEditorMainWindow.cpp
//  Purpose: Implements the tile-map editor's complete standalone Qt shell.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileEditorMainWindow.h"

#include "CypherTileConsole.h"
#include "CypherTileShellRunner.h"
#include "CypherTileMaterialBrowser.h"
#include "CypherTilePiecePalette.h"
#include "CypherTileEditorConfig.h"
#include "CypherTileMapProperties.h"
#include "CypherTileEditorTheme.h"
#include "CypherTileEditorIcons.h"
#include "CypherTileRenderViewport.h"
#include "CypherTileOrthoView.h"
#include "CypherTileViewWorkspace.h"

#include "Core/CypherTileMapGeometry.h"
#include "Core/CypherTileMapMaterials.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_StringView.h"
#include "CypherCommon/Tier1/CypherCommon_Vector.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace cypher::tools::tile_editor
{

namespace
{

constexpr int TILE_EDITOR_STATE_VERSION = 3;
constexpr int TILE_EDITOR_MAX_RECENT_FILES = 8;

QString FromView( string_view_t view )
{
    return QString::fromUtf8( view.pData, static_cast<qsizetype>( view.cchLength ) );
}

void PopulateCellShapes( QComboBox *combo )
{
    combo->addItem( QObject::tr( "Flat floor" ), static_cast<int>( tile_map_cell_shape_t::FLAT ) );
    combo->addItem( QObject::tr( "Stairs · North ↑" ), static_cast<int>( tile_map_cell_shape_t::STAIRS_NORTH ) );
    combo->addItem( QObject::tr( "Stairs · East →" ), static_cast<int>( tile_map_cell_shape_t::STAIRS_EAST ) );
    combo->addItem( QObject::tr( "Stairs · South ↓" ), static_cast<int>( tile_map_cell_shape_t::STAIRS_SOUTH ) );
    combo->addItem( QObject::tr( "Stairs · West ←" ), static_cast<int>( tile_map_cell_shape_t::STAIRS_WEST ) );
    combo->setToolTip( QObject::tr( "Stairs rise one map level toward the arrow. Stair edges are open; connect flat landings at the bottom and top levels." ) );
}

QString ValidationMessage( const tile_map_validation_diagnostic_t &diagnostic )
{
    switch ( diagnostic.code ) {
        case tile_map_validation_code_t::INVALID_MATERIAL_BINDING:
            return QObject::tr( "A material binding has an invalid project-relative .cymat path." );
        case tile_map_validation_code_t::DUPLICATE_MATERIAL_SLOT:
            return QObject::tr( "More than one material is bound to the same map slot." );
        case tile_map_validation_code_t::MATERIAL_BINDING_LIMIT_EXCEEDED:
            return QObject::tr( "The map exceeds the %1 material binding limit." ).arg( TILE_MAP_MAX_MATERIAL_BINDINGS );
        case tile_map_validation_code_t::INVALID_CELL_PROPERTIES:
            return QObject::tr( "Cell (%1, %2) has invalid wall, flag, or floor properties." ).arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::INVALID_CELL_SHAPE:
            return QObject::tr( "Cell (%1, %2) has an unknown shape." ).arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::INVALID_STAIR_STEPS:
            return QObject::tr( "Stairs at (%1, %2) need between 2 and 32 steps." ).arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::PLAYER_SPAWN_ON_STAIRS:
            return QObject::tr( "Move the player spawn at (%1, %2) onto a flat landing." ).arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::DOOR_ON_STAIRS:
            return QObject::tr( "Move the door at (%1, %2) onto an exposed flat-floor edge." ).arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::INVALID_DIMENSIONS:
            return QObject::tr( "Document dimensions are invalid." );
        case tile_map_validation_code_t::INVALID_METRICS:
            return QObject::tr( "Cell size or level height is invalid." );
        case tile_map_validation_code_t::CELL_STORAGE_MISMATCH:
            return QObject::tr( "Dense cell storage does not match the map dimensions." );
        case tile_map_validation_code_t::ACTIVE_CELL_LIMIT_EXCEEDED:
            return QObject::tr( "The map exceeds the %1 active-cell authoring limit." )
                .arg( TILE_MAP_MAX_ACTIVE_CELLS );
        case tile_map_validation_code_t::NONCANONICAL_EMPTY_CELL:
            return QObject::tr(
                "Empty cell (%1, %2) contains hidden noncanonical data." )
                .arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::INVALID_MARKER_ID:
            return QObject::tr( "A marker at (%1, %2) has an invalid ID." )
                .arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::DUPLICATE_MARKER_ID:
            return QObject::tr( "A marker at (%1, %2) reuses another marker ID." )
                .arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::INVALID_MARKER_KIND:
            return QObject::tr( "A marker at (%1, %2) has an unknown kind." )
                .arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::MISSING_PLAYER_SPAWN:
            return QObject::tr( "The map has no player spawn." );
        case tile_map_validation_code_t::DUPLICATE_PLAYER_SPAWN:
            return QObject::tr( "The map contains more than one player spawn." );
        case tile_map_validation_code_t::PLAYER_SPAWN_OUT_OF_BOUNDS:
            return QObject::tr( "Player spawn (%1, %2) is outside the map." )
                .arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::PLAYER_SPAWN_OUTSIDE_FLOOR:
            return QObject::tr( "Player spawn (%1, %2) is not on a floor cell." )
                .arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::DOOR_OUT_OF_BOUNDS:
            return QObject::tr( "Door (%1, %2) is outside the map." )
                .arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::DOOR_INVALID_SIDE:
            return QObject::tr(
                "Door (%1, %2) must use a north, east, south, or west edge." )
                .arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::DOOR_OUTSIDE_FLOOR:
            return QObject::tr( "Door (%1, %2) is not attached to a floor cell." )
                .arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::DOOR_NOT_ON_BOUNDARY:
            return QObject::tr(
                "Door (%1, %2) is on an interior edge; choose an exposed map boundary." )
                .arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
        case tile_map_validation_code_t::DUPLICATE_DOOR_EDGE:
            return QObject::tr( "More than one door occupies the same edge at (%1, %2)." )
                .arg( diagnostic.cell.x ).arg( diagnostic.cell.y );
    }
    return QObject::tr( "Unknown validation problem." );
}

bool GeometryDiagnosticIsBlocking( tile_map_validation_code_t code )
{
    switch ( code ) {
        case tile_map_validation_code_t::MISSING_PLAYER_SPAWN:
        case tile_map_validation_code_t::DUPLICATE_PLAYER_SPAWN:
        case tile_map_validation_code_t::PLAYER_SPAWN_OUT_OF_BOUNDS:
        case tile_map_validation_code_t::PLAYER_SPAWN_OUTSIDE_FLOOR:
        case tile_map_validation_code_t::PLAYER_SPAWN_ON_STAIRS:
            return false;
        default:
            return true;
    }
}

bool ValidationDiagnosticHasCell( tile_map_validation_code_t code )
{
    switch ( code ) {
        case tile_map_validation_code_t::INVALID_DIMENSIONS:
        case tile_map_validation_code_t::INVALID_METRICS:
        case tile_map_validation_code_t::CELL_STORAGE_MISMATCH:
        case tile_map_validation_code_t::MISSING_PLAYER_SPAWN:
            return false;
        default:
            return true;
    }
}

QIcon MaterialSwatchIcon( const tile_map_material_definition_t &material )
{
    QPixmap swatch( 152, 100 );
    swatch.setDevicePixelRatio( 2 );
    swatch.fill( QColor( 25, 29, 34 ) );
    const QColor base = QColor::fromRgbF( material.colorR, material.colorG, material.colorB );
    QPainter painter( &swatch );
    painter.setRenderHint( QPainter::Antialiasing );
    painter.setPen( QPen( QColor( 18, 21, 25 ), 1.0 ) );
    painter.setBrush( base.lighter( 130 ) );
    painter.drawPolygon( QPolygonF{ QPointF( 38, 4 ), QPointF( 65, 15 ), QPointF( 38, 27 ), QPointF( 11, 15 ) } );
    painter.setBrush( base );
    painter.drawPolygon( QPolygonF{ QPointF( 11, 15 ), QPointF( 38, 27 ), QPointF( 38, 46 ), QPointF( 11, 34 ) } );
    painter.setBrush( base.darker( 145 ) );
    painter.drawPolygon( QPolygonF{ QPointF( 38, 27 ), QPointF( 65, 15 ), QPointF( 65, 34 ), QPointF( 38, 46 ) } );
    painter.setPen( QColor( 182, 193, 202 ) );
    painter.drawText( QRect( 3, 33, 18, 15 ), Qt::AlignLeft, QString::number( material.nSlot ) );
    return QIcon( swatch );
}

class NewMapDialog final : public QDialog
{
public:
    NewMapDialog(
        const tile_editor_preferences_t &preferences,
        QWidget *pParent )
        : QDialog( pParent )
    {
        setWindowTitle( tr( "New Tile Map" ) );
        auto *pRoot = new QVBoxLayout( this );
        auto *pForm = new QFormLayout();
        m_pWidth = new QSpinBox( this );
        m_pHeight = new QSpinBox( this );
        m_pCellSize = new QDoubleSpinBox( this );
        m_pLevelHeight = new QDoubleSpinBox( this );
        m_pWidth->setRange( 1, static_cast<int>( TILE_MAP_MAX_WIDTH ) );
        m_pHeight->setRange( 1, static_cast<int>( TILE_MAP_MAX_HEIGHT ) );
        m_pWidth->setValue( preferences.defaultMapWidth );
        m_pHeight->setValue( preferences.defaultMapHeight );
        m_pCellSize->setRange(
            TILE_MAP_DEFAULT_WALL_THICKNESS, 1000.0 );
        m_pLevelHeight->setRange(
            TILE_MAP_DEFAULT_FLOOR_THICKNESS, 1000.0 );
        m_pCellSize->setDecimals( 2 );
        m_pLevelHeight->setDecimals( 2 );
        m_pCellSize->setValue( TILE_MAP_DEFAULT_CELL_SIZE );
        m_pLevelHeight->setValue( TILE_MAP_DEFAULT_LEVEL_HEIGHT );
        pForm->addRow( tr( "Width (cells)" ), m_pWidth );
        pForm->addRow( tr( "Height (cells)" ), m_pHeight );
        pForm->addRow( tr( "Cell size" ), m_pCellSize );
        pForm->addRow( tr( "Level height" ), m_pLevelHeight );
        pRoot->addLayout( pForm );
        auto *pButtons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
        connect( pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept );
        connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
        pRoot->addWidget( pButtons );
    }

    tile_map_document_desc_t description() const
    {
        return {
            static_cast<u32>( m_pWidth->value() ),
            static_cast<u32>( m_pHeight->value() ),
            static_cast<f32>( m_pCellSize->value() ),
            static_cast<f32>( m_pLevelHeight->value() )
        };
    }

private:
    QSpinBox *m_pWidth{ nullptr };
    QSpinBox *m_pHeight{ nullptr };
    QDoubleSpinBox *m_pCellSize{ nullptr };
    QDoubleSpinBox *m_pLevelHeight{ nullptr };
};

} // namespace

CypherTileEditorMainWindow::CypherTileEditorMainWindow( QWidget *pParent )
    : QMainWindow( pParent )
{
    setObjectName( QStringLiteral( "CypherTileEditorMainWindow" ) );
    setDockNestingEnabled( true );
    setCorner( Qt::BottomLeftCorner, Qt::BottomDockWidgetArea );
    setCorner( Qt::BottomRightCorner, Qt::RightDockWidgetArea );

    QSettings settings;
    m_preferences = TileEditorPreferences_Load( settings );
    QString configurationError;
    const QString configurationPath = TileEditorConfig_DefaultPath();
    const bool hadConfiguration = QFileInfo::exists( configurationPath );
    if ( TileEditorConfig_Load( configurationPath, m_preferences, configurationError ) && !hadConfiguration )
        TileEditorConfig_Save( configurationPath, m_preferences, configurationError );
    // One-time presentation update for the user's existing desktop profile.
    // Later user choices and imported profiles retain full control of these options.
    if ( QCoreApplication::organizationName() == QStringLiteral( "CypherEngine" ) &&
         QCoreApplication::applicationName() == QStringLiteral( "CypherTileEditor" ) &&
         !settings.value( "TileEditor/quietWorkspaceV1", false ).toBool() ) {
        m_preferences.showViewMetrics = false;
        m_preferences.showMaterialLabels = false;
        m_preferences.showCameraHints = false;
        m_preferences.highlightActiveView = false;
        m_preferences.showActiveViewBorder = false;
        m_preferences.showViewAxes = false;
        m_preferences.centerViewAxes = true;
        TileEditorPreferences_Save( settings, m_preferences );
        if ( TileEditorConfig_Save( configurationPath, m_preferences, configurationError ) )
            settings.setValue( "TileEditor/quietWorkspaceV1", true );
    }
    // Existing profiles created before pointer-routed viewport input shipped
    // stored this option as false. Upgrade those profiles once so the pane
    // beneath the pointer receives navigation and authoring shortcuts without
    // first requiring a click. Users can still opt out afterward in Settings.
    if ( QCoreApplication::organizationName() == QStringLiteral( "CypherEngine" ) &&
         QCoreApplication::applicationName() == QStringLiteral( "CypherTileEditor" ) &&
         !settings.value( "TileEditor/pointerRoutedViewportsV1", false ).toBool() ) {
        m_preferences.activateViewOnHover = true;
        m_preferences.showViewAxes = true;
        m_preferences.centerViewAxes = true;
        TileEditorPreferences_Save( settings, m_preferences );
        if ( TileEditorConfig_Save( configurationPath, m_preferences, configurationError ) )
            settings.setValue( "TileEditor/pointerRoutedViewportsV1", true );
    }
    m_recentFiles = settings.value(
        QStringLiteral( "TileEditor/recentFiles" ) ).toStringList();

    createActions();
    buildWorkspace();
    buildMenus();
    buildCameraMenu();
    buildToolbar();
    buildStatusBar();
    registerCommands();
    applyPreferences();
    if ( !configurationError.isEmpty() ) appendError( configurationError );

    m_pPreviewSyncTimer = new QTimer( this );
    m_pPreviewSyncTimer->setSingleShot( true );
    m_pPreviewSyncTimer->setInterval( 140 );
    connect( m_pPreviewSyncTimer, &QTimer::timeout, this, [this] {
        if ( m_pPreviewProcess != nullptr &&
             m_pPreviewProcess->state() != QProcess::NotRunning ) {
            (void)writePreviewSnapshot();
        }
    } );

    tile_map_document_desc_t description{};
    description.nWidth = static_cast<u32>( m_preferences.defaultMapWidth );
    description.nHeight = static_cast<u32>( m_preferences.defaultMapHeight );
    QString error;
    if ( !m_document.newDocument( description, &error ) ) {
        appendError( error );
    } else {
        // The constructor creates a placeholder rather than a user-authored
        // map. Keep only this placeholder clean; File > New remains unsaved.
        m_document.markSaved();
    }
    m_pCanvas->setDocumentBridge( &m_document );
    m_pRenderViewport->setDocumentBridge( &m_document );
    if ( m_pFrontView != nullptr ) m_pFrontView->setDocumentBridge( &m_document );
    if ( m_pSideView != nullptr ) m_pSideView->setDocumentBridge( &m_document );
    restoreWorkspace();
    refreshOrthoMaterials();
    rebuildRecentMenu();
    updateWindowState();
    updateValidationPanel();
    appendInfo( tr( "Cypher Tile Editor ready. Type 'help' for console commands." ) );
}

CypherTileEditorMainWindow::~CypherTileEditorMainWindow()
{
    if ( m_pCameraPreferenceTimer && m_pCameraPreferenceTimer->isActive() ) savePreferences();
    stopPreview();
    if ( !m_previewMapPath.isEmpty() ) QFile::remove( m_previewMapPath );
}

bool CypherTileEditorMainWindow::openFilePath(
    const QString &path,
    bool bConfirmDiscard )
{
    if ( path.isEmpty() || ( bConfirmDiscard && !maybeSave() ) ) return false;
    QString error;
    if ( !m_document.loadFromFile( path, &error ) ) {
        appendError( error );
        QMessageBox::critical( this, tr( "Open Tile Map" ), error );
        return false;
    }
    addRecentFile( m_document.filePath() );
    m_pMaterialBrowser->cancelPendingAssignment();
    m_pRenderViewport->clearCameraBookmarks();
    synchronizeCameraActions();
    refreshOrthoMaterials();
    for ( auto *view : m_topViews ) view->setDocumentBridge( &m_document );
    for ( auto *view : m_orthoViews ) view->setDocumentBridge( &m_document );
    if ( m_preferences.frameMapOnOpen ) m_pRenderViewport->setDocumentBridge( &m_document );
    else m_pRenderViewport->refreshDocument();
    m_pConstructionX->setMaximum( static_cast<int>( m_document.document()->nWidth ) - 1 );
    m_pConstructionY->setMaximum( static_cast<int>( m_document.document()->nHeight ) - 1 );
    synchronizeSelection();
    updateInspector( false, {} );
    updateWindowState();
    updateValidationPanel();
    schedulePreviewSync();
    appendInfo( tr( "Opened %1" ).arg(
        QDir::toNativeSeparators( m_document.filePath() ) ) );
    return true;
}

void CypherTileEditorMainWindow::closeEvent( QCloseEvent *pEvent )
{
    if ( !maybeSave() ) {
        pEvent->ignore();
        return;
    }
    saveWorkspace();
    pEvent->accept();
}

QAction *CypherTileEditorMainWindow::createAction(
    const QString &id,
    const QString &text,
    bool bCheckable )
{
    auto *pAction = new QAction( text, this );
    pAction->setCheckable( bCheckable );
    pAction->setObjectName( id );
    m_actions.insert( id, pAction );
    return pAction;
}

void CypherTileEditorMainWindow::createActions()
{
    connect( createAction( QStringLiteral( "file.new" ), tr( "&New Map" ) ),
             &QAction::triggered, this, [this] { newMap(); } );
    connect( createAction( QStringLiteral( "file.open" ), tr( "&Open Map..." ) ),
             &QAction::triggered, this, [this] { openMap(); } );
    connect( createAction( QStringLiteral( "file.save" ), tr( "&Save" ) ),
             &QAction::triggered, this, [this] { saveMap(); } );
    connect( createAction( QStringLiteral( "file.saveAs" ), tr( "Save &As..." ) ),
             &QAction::triggered, this, [this] { saveMapAs(); } );
    connect( createAction( QStringLiteral( "map.properties" ), tr( "Map Properties…" ) ),
             &QAction::triggered, this, [this] { showMapProperties(); } );
    m_actions.value( "map.properties" )->setIcon( CypherTileEditorIcon_Create( u"adjustments-horizontal" ) );

    connect( createAction( QStringLiteral( "edit.undo" ), tr( "&Undo" ) ),
             &QAction::triggered, this, [this] { undo(); } );
    connect( createAction( QStringLiteral( "edit.redo" ), tr( "&Redo" ) ),
             &QAction::triggered, this, [this] { redo(); } );

    struct region_action_t { const char *id; const char *label; const char *icon; };
    for ( const region_action_t &spec : {
            region_action_t{ "edit.move", "Move by Offset", "arrows-move" },
            { "edit.moveLeft", "Move Left", "arrows-move" },
            { "edit.moveRight", "Move Right", "arrows-move" },
            { "edit.moveUp", "Move Up", "arrows-move" },
            { "edit.moveDown", "Move Down", "arrows-move" },
            { "edit.duplicate", "Duplicate to the Right", "copy" },
            { "edit.copyOffset", "Duplicate by Offset", "copy" },
            { "edit.delete", "Delete Selection", "trash" },
            { "edit.rotate", "Rotate Selection Clockwise", "rotate-clockwise" },
            { "edit.raise", "Raise Floor One Level", "arrow-up" },
            { "edit.lower", "Lower Floor One Level", "arrow-down" },
            { "edit.wallRaise", "Increase Wall Height", "arrow-up" },
            { "edit.wallLower", "Decrease Wall Height", "arrow-down" } } ) {
        auto *action = createAction( QString::fromLatin1( spec.id ), tr( spec.label ) );
        action->setIcon( CypherTileEditorIcon_Create( QString::fromLatin1( spec.icon ) ) );
        action->setEnabled( false );
    }
    connect( m_actions.value( "edit.move" ), &QAction::triggered, this,
        [this] { translateSelection( m_pOffsetX->value(), m_pOffsetY->value() ); } );
    connect( m_actions.value( "edit.copyOffset" ), &QAction::triggered, this,
        [this] { translateSelection( m_pOffsetX->value(), m_pOffsetY->value(), true ); } );
    connect( m_actions.value( "edit.moveLeft" ), &QAction::triggered, this, [this] { translateSelection( -1, 0 ); } );
    connect( m_actions.value( "edit.moveRight" ), &QAction::triggered, this, [this] { translateSelection( 1, 0 ); } );
    connect( m_actions.value( "edit.moveUp" ), &QAction::triggered, this, [this] { translateSelection( 0, -1 ); } );
    connect( m_actions.value( "edit.moveDown" ), &QAction::triggered, this, [this] { translateSelection( 0, 1 ); } );
    connect( m_actions.value( "edit.duplicate" ), &QAction::triggered, this,
        [this] { translateSelection( static_cast<int>( m_pCanvas->selectionRect().nWidth ), 0, true ); } );
    connect( m_actions.value( "edit.delete" ), &QAction::triggered, this, [this] { deleteSelection(); } );
    connect( m_actions.value( "edit.rotate" ), &QAction::triggered, this, [this] { rotateSelection(); } );
    connect( m_actions.value( "edit.raise" ), &QAction::triggered, this, [this] { raiseSelection( 1 ); } );
    connect( m_actions.value( "edit.lower" ), &QAction::triggered, this, [this] { raiseSelection( -1 ); } );
    connect( m_actions.value( "edit.wallRaise" ), &QAction::triggered, this, [this] { adjustSelectionWallHeight( 1 ); } );
    connect( m_actions.value( "edit.wallLower" ), &QAction::triggered, this, [this] { adjustSelectionWallHeight( -1 ); } );
    connect( createAction( "edit.selectAll", tr( "Select All Authored Tiles" ) ), &QAction::triggered, this, [this] {
        if ( !m_document.isInitialized() || m_pRenderViewport->isNavigating() ) return;
        setTool( tile_canvas_tool_t::SELECT );
        m_pCanvas->selectAllOccupied();
    } );
    auto *clearSelection = createAction( "edit.selectNone", tr( "Clear Selection" ) );
    connect( clearSelection, &QAction::triggered, this, [this] { m_pCanvas->clearSelection(); } );
    connect( createAction( "view.resetLayout", tr( "Reset Workspace Layout" ) ), &QAction::triggered,
        this, [this] { resetWorkspaceLayout(); } );
    const auto addPreset = [this]( const char *id, const QString &label, const QString &preset ) {
        connect( createAction( id, label ), &QAction::triggered, this, [this, preset] {
            TileEditorPreferences_ApplyColorPreset( m_preferences, preset );
            applyPreferences();
            QSettings settings;
            TileEditorPreferences_Save( settings, m_preferences );
            QString error;
            if ( !TileEditorConfig_Save( TileEditorConfig_DefaultPath(), m_preferences, error ) ) appendError( error );
        } );
    };
    addPreset( "view.slatePalette", tr( "Use Slate Grid Palette" ), "slate" );
    addPreset( "view.radiantPalette", tr( "Use Radiant Dark Palette" ), "radiant-dark" );
    connect( createAction( "app.openConfig", tr( "Open Editor Configuration…" ) ), &QAction::triggered, this, [this] {
        const auto path = TileEditorConfig_DefaultPath();
        if ( !QFileInfo::exists( path ) ) {
            QString error;
            if ( !TileEditorConfig_Save( path, m_preferences, error ) ) { appendError( error ); return; }
        }
        if ( !QDesktopServices::openUrl( QUrl::fromLocalFile( path ) ) ) appendWarning( tr( "Open this configuration file in a text editor: %1" ).arg( path ) );
    } );
    connect( createAction( "app.reloadConfig", tr( "Reload Editor Configuration" ) ), &QAction::triggered, this, [this] {
        QString error;
        if ( !TileEditorConfig_Load( TileEditorConfig_DefaultPath(), m_preferences, error ) ) { appendError( error ); return; }
        applyPreferences();
        QSettings settings;
        TileEditorPreferences_Save( settings, m_preferences );
        setStatus( tr( "Reloaded editor configuration" ) );
    } );
    connect( createAction( "app.importConfig", tr( "Import Configuration Profile…" ) ), &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName( this, tr( "Import Editor Configuration" ),
            QFileInfo( TileEditorConfig_DefaultPath() ).absolutePath(), tr( "Editor configurations (*.ini)" ) );
        if ( path.isEmpty() ) return;
        QString error;
        if ( !importConfigurationProfile( path, &error ) ) appendError( error );
    } );
    connect( createAction( "app.exportConfig", tr( "Export Configuration Profile…" ) ), &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName( this, tr( "Export Editor Configuration" ),
            QDir( QStandardPaths::writableLocation( QStandardPaths::DocumentsLocation ) )
                .filePath( QStringLiteral( "Cypher-editor-profile.ini" ) ), tr( "Editor configurations (*.ini)" ) );
        if ( path.isEmpty() ) return;
        QString error;
        if ( !exportConfigurationProfile( path, &error ) ) appendError( error );
        else setStatus( tr( "Exported configuration profile: %1" ).arg( QFileInfo( path ).fileName() ) );
    } );

    connect( createAction( QStringLiteral( "map.validate" ), tr( "&Validate Map" ) ),
             &QAction::triggered, this, [this] { validateMap(); } );
    connect( createAction( QStringLiteral( "map.build" ), tr( "&Build Geometry" ) ),
             &QAction::triggered, this, [this] { buildMap(); } );
    connect( createAction( QStringLiteral( "map.preview" ), tr( "Launch Runtime Preview" ) ),
             &QAction::triggered, this, [this] { previewMap(); } );
    connect( createAction( QStringLiteral( "map.previewStop" ),
                           tr( "Stop Runtime Preview" ) ),
             &QAction::triggered, this, [this] {
        stopPreview();
        setStatus( tr( "Runtime preview stopped" ) );
    } );
    m_actions.value( QStringLiteral( "map.previewStop" ) )->setEnabled( false );
    QAction *pGrid = createAction( QStringLiteral( "view.grid" ), tr( "Show Grid" ), true );
    QAction *pMarkers = createAction( QStringLiteral( "view.markers" ), tr( "Show Spawn and Door Markers" ), true );
    QAction *pAdaptiveGrid = createAction( QStringLiteral( "view.adaptiveGrid" ), tr( "Adaptive Grid Density" ), true );
    QAction *pRulers = createAction( QStringLiteral( "view.rulers" ), tr( "Show Coordinate Rulers" ), true );
    QAction *pAxes = createAction( QStringLiteral( "view.axes" ), tr( "Show XYZ Coordinate Axes" ), true );
    auto *materials = createAction( "view.orthoMaterials", tr( "Show Materials in 2D Views" ), true );
    materials->setIcon( CypherTileEditorIcon_Create( tile_editor_icon_t::MATERIAL ) );
    materials->setToolTip( tr(
        "2D material preview · checked shows material textures in Top, Front and Side; "
        "unchecked uses blockout shading" ) );
    connect( materials, &QAction::toggled, this, [this]( bool enabled ) {
        m_preferences.showOrthoMaterials = enabled;
        applyPreferences();
        savePreferences();
    } );
    auto *materialLabels = createAction( "view.materialLabels", tr( "Show Material Identification" ), true );
    connect( materialLabels, &QAction::toggled, this, [this]( bool enabled ) {
        m_preferences.showMaterialLabels = enabled;
        applyPreferences();
        savePreferences();
    } );
    connect( pGrid, &QAction::toggled, this, [this]( bool enabled ) {
        m_preferences.showGrid = enabled;
        applyPreferences();
        savePreferences();
    } );
    connect( pMarkers, &QAction::toggled, this, [this]( bool enabled ) {
        m_preferences.showMarkers = enabled;
        applyPreferences();
        savePreferences();
    } );
    connect( pAdaptiveGrid, &QAction::toggled, this, [this]( bool enabled ) {
        m_preferences.adaptiveGrid = enabled;
        applyPreferences();
        savePreferences();
    } );
    connect( pRulers, &QAction::toggled, this, [this]( bool enabled ) {
        m_preferences.showCoordinateRulers = enabled;
        applyPreferences();
        savePreferences();
    } );
    connect( pAxes, &QAction::toggled, this, [this]( bool enabled ) {
        m_preferences.showViewAxes = enabled;
        applyPreferences();
        savePreferences();
    } );
    connect( createAction( QStringLiteral( "app.settings" ), tr( "Editor &Settings..." ) ),
             &QAction::triggered, this, [this] { showSettings(); } );
    connect( createAction( QStringLiteral( "view.fit" ), tr( "Frame Active View" ) ),
             &QAction::triggered, this, [this] {
        if ( m_pViewWorkspace->activeView() == tile_editor_view_t::PERSPECTIVE )
            m_pRenderViewport->frameSelection();
        else frameView( m_pViewWorkspace->activeView() );
    } );
    connect( createAction( QStringLiteral( "view.frameSelection" ), tr( "Frame Selection in 3D" ) ),
             &QAction::triggered, this, [this] {
        m_pViewWorkspace->focusView( tile_editor_view_t::PERSPECTIVE );
        m_pRenderViewport->frameSelection();
    } );
    connect( createAction( QStringLiteral( "view.spawnCamera" ), tr( "Camera at Player Spawn" ) ),
        &QAction::triggered, this, [this] {
        m_pViewWorkspace->focusView( tile_editor_view_t::PERSPECTIVE );
        if ( !m_pRenderViewport->goToPlayerSpawn() )
            appendWarning( tr( "Could not move the camera to the player spawn; verify the spawn and map metrics." ) );
    } );
    connect( createAction( QStringLiteral( "view.fitAll" ), tr( "Frame All Views" ) ),
             &QAction::triggered, this, [this] {
        for ( auto *view : m_topViews ) view->fitToView();
        for ( auto *view : m_orthoViews ) view->fitToView();
        m_pRenderViewport->fitCamera();
    } );
    connect( createAction( QStringLiteral( "view.four" ), tr( "&Four Views" ) ),
             &QAction::triggered, this, [this] { m_pViewWorkspace->showFourViews(); } );
    connect( createAction( QStringLiteral( "view.maximize" ), tr( "Maximize Active View / Restore" ) ),
             &QAction::triggered, this, [this] { m_pViewWorkspace->toggleMaximize(); } );
    connect( createAction( QStringLiteral( "view.front" ), tr( "Focus &Front View" ) ),
             &QAction::triggered, this, [this] {
        m_pViewWorkspace->focusView( tile_editor_view_t::FRONT );
    } );
    connect( createAction( QStringLiteral( "view.side" ), tr( "Focus &Side View" ) ),
             &QAction::triggered, this, [this] {
        m_pViewWorkspace->focusView( tile_editor_view_t::SIDE );
    } );
    connect( createAction( QStringLiteral( "view.layout2d" ), tr( "Show &2D Layout" ) ),
             &QAction::triggered, this, [this] {
        m_pViewWorkspace->focusView( tile_editor_view_t::TOP );
    } );
    connect( createAction( QStringLiteral( "view.live3d" ), tr( "Show &3D Map" ) ),
             &QAction::triggered, this, [this] {
        m_pViewWorkspace->focusView( tile_editor_view_t::PERSPECTIVE );
    } );
    QAction *pConsole = createAction(
        QStringLiteral( "view.console" ), tr( "Command Console" ), true );
    pConsole->setChecked( true );
    connect( pConsole, &QAction::toggled, this, [this]( bool bVisible ) {
        if ( m_pConsoleDock != nullptr ) {
            m_pConsoleDock->setVisible( bVisible );
            if ( bVisible ) m_pConsoleDock->raise();
        }
    } );
    QAction *pShell = createAction(
        QStringLiteral( "view.shell" ), tr( "Open Local Shell Runner" ) );
    pShell->setToolTip( tr(
        "Open the asynchronous zsh/bash command runner in the Console dock" ) );
    connect( pShell, &QAction::triggered, this, [this] {
        if ( m_pConsoleDock == nullptr || m_pConsoleTabs == nullptr ||
             m_pShellRunner == nullptr ) return;
        m_pConsoleDock->show();
        m_pConsoleDock->raise();
        m_pConsoleTabs->setCurrentWidget( m_pShellRunner );
        m_pShellRunner->focusInput();
    } );

    m_pToolActions = new QActionGroup( this );
    m_pToolActions->setExclusive( true );
    struct tool_spec_t {
        const char *pId;
        const char *pText;
        tile_canvas_tool_t tool;
    };
    static constexpr tool_spec_t tools[]{
        { "tool.select", "Select", tile_canvas_tool_t::SELECT },
        { "tool.paint", "Paint Floor", tile_canvas_tool_t::PAINT },
        { "tool.erase", "Erase", tile_canvas_tool_t::ERASE },
        { "tool.rectangle", "Rectangle", tile_canvas_tool_t::RECTANGLE },
        { "tool.spawn", "Player Spawn", tile_canvas_tool_t::PLAYER_SPAWN },
        { "tool.door", "Door", tile_canvas_tool_t::DOOR },
        { "tool.pan", "Pan", tile_canvas_tool_t::PAN },
        { "tool.eyedropper", "Pick Material and Dimensions", tile_canvas_tool_t::EYEDROPPER },
        { "tool.line", "Paint Line", tile_canvas_tool_t::LINE },
        { "tool.fill", "Fill Region", tile_canvas_tool_t::FILL }
    };
    for ( const tool_spec_t &tool : tools ) {
        QAction *pAction = createAction(
            QString::fromLatin1( tool.pId ),
            tr( tool.pText ),
            true );
        pAction->setProperty( "tileTool", static_cast<int>( tool.tool ) );
        m_pToolActions->addAction( pAction );
        connect( pAction, &QAction::triggered, this, [this, tool] {
            setTool( tool.tool );
        } );
    }
    m_actions.value( QStringLiteral( "tool.paint" ) )->setChecked( true );

    QAction *pQuit = createAction( QStringLiteral( "file.quit" ), tr( "&Quit" ) );
    pQuit->setShortcut( QKeySequence::Quit );
    connect( pQuit, &QAction::triggered, this, &QWidget::close );

    const auto setIcon = [this](
        const char *pActionId,
        tile_editor_icon_t icon ) {
        if ( QAction *pAction = m_actions.value(
                QString::fromLatin1( pActionId ), nullptr ) ) {
            pAction->setIcon( CypherTileEditorIcon_Create( icon ) );
        }
    };
    setIcon( "file.new", tile_editor_icon_t::NEW_MAP );
    setIcon( "file.open", tile_editor_icon_t::OPEN );
    setIcon( "file.save", tile_editor_icon_t::SAVE );
    setIcon( "file.saveAs", tile_editor_icon_t::SAVE );
    setIcon( "edit.undo", tile_editor_icon_t::UNDO );
    setIcon( "edit.redo", tile_editor_icon_t::REDO );
    setIcon( "map.validate", tile_editor_icon_t::VALIDATE );
    setIcon( "map.build", tile_editor_icon_t::BUILD );
    setIcon( "map.preview", tile_editor_icon_t::PLAY );
    setIcon( "map.previewStop", tile_editor_icon_t::STOP );
    setIcon( "app.settings", tile_editor_icon_t::SETTINGS );
    setIcon( "view.fit", tile_editor_icon_t::FIT_VIEW );
    setIcon( "view.layout2d", tile_editor_icon_t::GRID );
    setIcon( "view.live3d", tile_editor_icon_t::PREVIEW );
    setIcon( "view.four", tile_editor_icon_t::FOUR_VIEWS );
    setIcon( "tool.pan", tile_editor_icon_t::PAN );
    setIcon( "tool.eyedropper", tile_editor_icon_t::EYEDROPPER );
    setIcon( "tool.line", tile_editor_icon_t::LINE );
    setIcon( "tool.fill", tile_editor_icon_t::FILL );
    setIcon( "view.console", tile_editor_icon_t::CONSOLE );
    setIcon( "view.shell", tile_editor_icon_t::CONSOLE );
    setIcon( "tool.select", tile_editor_icon_t::SELECT );
    setIcon( "tool.paint", tile_editor_icon_t::PAINT );
    setIcon( "tool.erase", tile_editor_icon_t::ERASE );
    setIcon( "tool.rectangle", tile_editor_icon_t::RECTANGLE );
    setIcon( "tool.spawn", tile_editor_icon_t::SPAWN );
    setIcon( "tool.door", tile_editor_icon_t::DOOR );
    m_actions.value( QStringLiteral( "map.validate" ) )->setToolTip(
        tr( "Validate map structure, player spawn, and door placement" ) );
    m_actions.value( QStringLiteral( "map.build" ) )->setToolTip(
        tr( "Validate and rebuild generated floor and wall geometry" ) );
    m_actions.value( QStringLiteral( "map.preview" ) )->setToolTip(
        tr( "Launch or synchronize the CypherRender runtime preview" ) );
    m_actions.value( QStringLiteral( "map.previewStop" ) )->setToolTip(
        tr( "Stop the running CypherRender runtime preview" ) );
}

void CypherTileEditorMainWindow::buildMenus()
{
    menuBar()->setNativeMenuBar( false );
    QMenu *pFile = menuBar()->addMenu( tr( "&File" ) );
    pFile->addAction( m_actions.value( QStringLiteral( "file.new" ) ) );
    pFile->addAction( m_actions.value( QStringLiteral( "file.open" ) ) );
    m_pRecentMenu = pFile->addMenu( tr( "Open &Recent" ) );
    pFile->addSeparator();
    pFile->addAction( m_actions.value( QStringLiteral( "file.save" ) ) );
    pFile->addAction( m_actions.value( QStringLiteral( "file.saveAs" ) ) );
    pFile->addSeparator();
    pFile->addAction( m_actions.value( QStringLiteral( "file.quit" ) ) );

    QMenu *pEdit = menuBar()->addMenu( tr( "&Edit" ) );
    pEdit->addAction( m_actions.value( QStringLiteral( "edit.undo" ) ) );
    pEdit->addAction( m_actions.value( QStringLiteral( "edit.redo" ) ) );
    pEdit->addSeparator();
    for ( const char *id : { "edit.selectAll", "edit.selectNone", "edit.move", "edit.duplicate", "edit.copyOffset",
                            "edit.rotate", "edit.raise", "edit.lower", "edit.wallRaise", "edit.wallLower", "edit.delete" } )
        pEdit->addAction( m_actions.value( QString::fromLatin1( id ) ) );
    pEdit->addSeparator();
    pEdit->addAction( m_actions.value( QStringLiteral( "app.settings" ) ) );
    pEdit->addAction( m_actions.value( "app.openConfig" ) );
    pEdit->addAction( m_actions.value( "app.reloadConfig" ) );
    auto *profiles = pEdit->addMenu( tr( "Configuration Profiles" ) );
    profiles->addAction( m_actions.value( "app.importConfig" ) );
    profiles->addAction( m_actions.value( "app.exportConfig" ) );

    QMenu *pTools = menuBar()->addMenu( tr( "&Tools" ) );
    for ( QAction *pTool : m_pToolActions->actions() ) pTools->addAction( pTool );
    pTools->addSeparator();
    pTools->addAction( m_actions.value( QStringLiteral( "map.properties" ) ) );
    pTools->addAction( m_actions.value( QStringLiteral( "map.validate" ) ) );
    pTools->addAction( m_actions.value( QStringLiteral( "map.build" ) ) );
    pTools->addAction( m_actions.value( QStringLiteral( "map.preview" ) ) );
    pTools->addAction( m_actions.value( QStringLiteral( "map.previewStop" ) ) );

    QMenu *pView = menuBar()->addMenu( tr( "&View" ) );
    pView->setObjectName( "TileViewMenu" );
    pView->addAction( m_actions.value( QStringLiteral( "view.four" ) ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.maximize" ) ) );
    pView->addSeparator();
    pView->addAction( m_actions.value( QStringLiteral( "view.layout2d" ) ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.live3d" ) ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.front" ) ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.side" ) ) );
    pView->addSeparator();
    pView->addAction( m_actions.value( QStringLiteral( "view.fit" ) ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.fitAll" ) ) );
    pView->addSeparator();
    pView->addAction( m_actions.value( QStringLiteral( "view.grid" ) ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.adaptiveGrid" ) ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.rulers" ) ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.axes" ) ) );
    pView->addAction( m_actions.value( "view.orthoMaterials" ) );
    pView->addAction( m_actions.value( "view.materialLabels" ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.frameSelection" ) ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.spawnCamera" ) ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.markers" ) ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.console" ) ) );
    pView->addAction( m_actions.value( QStringLiteral( "view.shell" ) ) );
    pView->addAction( m_actions.value( "view.resetLayout" ) );
    pView->addAction( m_actions.value( "view.slatePalette" ) );
    pView->addAction( m_actions.value( "view.radiantPalette" ) );
    m_pWindowMenu = menuBar()->addMenu( tr( "&Window" ) );
    for ( QDockWidget *pDock : findChildren<QDockWidget *>() ) {
        m_pWindowMenu->addAction( pDock->toggleViewAction() );
    }

    QMenu *pHelp = menuBar()->addMenu( tr( "&Help" ) );
    QAction *pAbout = pHelp->addAction( tr( "About Cypher Tile Editor" ) );
    connect( pAbout, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this,
            tr( "About Cypher Tile Editor" ),
            tr( "Cypher Tile Editor\nGrid blockout authoring for CypherEngine." ) );
    } );
}

void CypherTileEditorMainWindow::buildCameraMenu()
{
    auto *menu = new QMenu( tr( "Camera" ), this );
    menu->setObjectName( QStringLiteral( "TileCameraMenu" ) );
    auto *modes = new QActionGroup( menu );
    modes->setExclusive( true );
    for ( const auto &entry : { std::pair{ "camera.fly", "Fly navigation" },
                               std::pair{ "camera.orbit", "Orbit navigation" } } ) {
        auto *action = createAction( QString::fromLatin1( entry.first ), tr( entry.second ), true );
        modes->addAction( action );
        menu->addAction( action );
        const bool fly = QString::fromLatin1( entry.first ) == QStringLiteral( "camera.fly" );
        connect( action, &QAction::triggered, this, [this, fly] {
            m_pRenderViewport->setCameraMode( fly ? tile_camera_mode_t::FLY : tile_camera_mode_t::ORBIT );
        } );
    }
    auto *toggleMode = createAction( "camera.toggleMode", tr( "Toggle Fly / Orbit" ) );
    toggleMode->setAutoRepeat( false );
    connect( toggleMode, &QAction::triggered, this, [this] {
        m_pRenderViewport->setCameraMode(
            m_pRenderViewport->camera().mode == tile_camera_mode_t::FLY
            ? tile_camera_mode_t::ORBIT : tile_camera_mode_t::FLY );
    } );
    menu->addAction( toggleMode );
    auto *autoOrbit = createAction( "camera.autoOrbit", tr( "Auto Orbit" ), true );
    autoOrbit->setAutoRepeat( false );
    connect( autoOrbit, &QAction::toggled, this,
        [this]( bool enabled ) { m_pRenderViewport->setAutoOrbitEnabled( enabled ); } );
    menu->addAction( autoOrbit );
    menu->addSeparator();

    auto *viewPresets = menu->addMenu( tr( "View Direction" ) );
    viewPresets->setObjectName( QStringLiteral( "TileCameraViewPresetsMenu" ) );
    struct view_preset_action_t {
        const char *id;
        const char *label;
        tile_camera_view_preset_t preset;
    };
    for ( const auto &entry : {
            view_preset_action_t{ "camera.viewPerspective", "Perspective", tile_camera_view_preset_t::PERSPECTIVE },
            view_preset_action_t{ "camera.viewTop", "Top", tile_camera_view_preset_t::TOP },
            view_preset_action_t{ "camera.viewBottom", "Bottom", tile_camera_view_preset_t::BOTTOM },
            view_preset_action_t{ "camera.viewFront", "Front", tile_camera_view_preset_t::FRONT },
            view_preset_action_t{ "camera.viewBack", "Back", tile_camera_view_preset_t::BACK },
            view_preset_action_t{ "camera.viewLeft", "Left", tile_camera_view_preset_t::LEFT },
            view_preset_action_t{ "camera.viewRight", "Right", tile_camera_view_preset_t::RIGHT } } ) {
        auto *action = createAction( QString::fromLatin1( entry.id ), tr( entry.label ) );
        connect( action, &QAction::triggered, this, [this, preset = entry.preset] {
            m_pViewWorkspace->focusView( tile_editor_view_t::PERSPECTIVE );
            m_pRenderViewport->setCameraViewPreset( preset );
        } );
        viewPresets->addAction( action );
    }
    viewPresets->addSeparator();
    auto *level = createAction( "camera.level", tr( "Level Camera Pitch" ) );
    connect( level, &QAction::triggered, this, [this] {
        m_pViewWorkspace->focusView( tile_editor_view_t::PERSPECTIVE );
        m_pRenderViewport->levelCamera();
    } );
    viewPresets->addAction( level );
    for ( const auto &entry : { std::pair{ "camera.levelUp", 1 },
                               std::pair{ "camera.levelDown", -1 } } ) {
        auto *action = createAction( QString::fromLatin1( entry.first ),
            entry.second > 0 ? tr( "Camera Up One Map Level" ) : tr( "Camera Down One Map Level" ) );
        connect( action, &QAction::triggered, this, [this, delta = entry.second] {
            m_pViewWorkspace->focusView( tile_editor_view_t::PERSPECTIVE );
            if ( !m_pRenderViewport->moveCameraLevel( delta ) )
                appendWarning( tr( "Could not move the camera by one map level." ) );
        } );
        viewPresets->addAction( action );
    }

    auto *savedViews = menu->addMenu( tr( "Saved Views" ) );
    savedViews->setObjectName( QStringLiteral( "TileCameraBookmarksMenu" ) );
    auto *storeViews = savedViews->addMenu( tr( "Store Current View" ) );
    auto *recallViews = savedViews->addMenu( tr( "Recall View" ) );
    for ( int slot = 0; slot < 4; ++slot ) {
        const QString storeId = QStringLiteral( "camera.store%1" ).arg( slot + 1 );
        const QString recallId = QStringLiteral( "camera.recall%1" ).arg( slot + 1 );
        auto *store = createAction( storeId, tr( "View %1" ).arg( slot + 1 ) );
        auto *recall = createAction( recallId, tr( "View %1" ).arg( slot + 1 ) );
        recall->setEnabled( false );
        connect( store, &QAction::triggered, this, [this, slot] {
            if ( m_pRenderViewport->storeCameraBookmark( slot ) ) {
                synchronizeCameraActions();
                setStatus( tr( "Stored camera view %1 for this map session." ).arg( slot + 1 ) );
            }
        } );
        connect( recall, &QAction::triggered, this, [this, slot] {
            m_pViewWorkspace->focusView( tile_editor_view_t::PERSPECTIVE );
            if ( m_pRenderViewport->recallCameraBookmark( slot ) )
                setStatus( tr( "Recalled camera view %1." ).arg( slot + 1 ) );
        } );
        storeViews->addAction( store );
        recallViews->addAction( recall );
    }
    menu->addSeparator();
    auto *frameMap = createAction( "camera.frameMap", tr( "Frame Map" ) );
    frameMap->setIcon( CypherTileEditorIcon_Create( tile_editor_icon_t::FIT_VIEW ) );
    connect( frameMap, &QAction::triggered, this, [this] { m_pRenderViewport->fitCamera(); } );
    menu->addAction( frameMap );
    menu->addAction( m_actions.value( "view.frameSelection" ) );
    menu->addAction( m_actions.value( "view.spawnCamera" ) );
    menu->addSeparator();

    auto *speedAction = new QWidgetAction( menu );
    auto *row = new QWidget( menu );
    auto *layout = new QHBoxLayout( row );
    layout->setContentsMargins( 12, 5, 12, 5 );
    layout->addWidget( new QLabel( tr( "Fly speed" ), row ) );
    auto *speed = new QDoubleSpinBox( row );
    speed->setObjectName( QStringLiteral( "TileCameraSpeed" ) );
    speed->setRange( 0.1, 1000.0 );
    speed->setDecimals( 2 );
    speed->setKeyboardTracking( false );
    speed->setSuffix( tr( " units/s" ) );
    speed->setToolTip( tr( "Base fly speed. Shift uses the fast multiplier; Ctrl uses the slow multiplier." ) );
    connect( speed, qOverload<double>( &QDoubleSpinBox::valueChanged ), this,
        [this]( double value ) { m_pRenderViewport->setMoveSpeed( static_cast<float>( value ) ); } );
    layout->addWidget( speed );
    speedAction->setDefaultWidget( row );
    menu->addAction( speedAction );
    for ( const auto &entry : { std::pair{ "camera.faster", "Increase Fly Speed" },
                               std::pair{ "camera.slower", "Decrease Fly Speed" } } ) {
        auto *action = createAction( QString::fromLatin1( entry.first ), tr( entry.second ) );
        const bool faster = QString::fromLatin1( entry.first ) == QStringLiteral( "camera.faster" );
        connect( action, &QAction::triggered, this, [this, faster] {
            m_pRenderViewport->setMoveSpeed( m_pRenderViewport->camera().settings.moveSpeed * ( faster ? 1.5f : 1.0f / 1.5f ) );
        } );
        menu->addAction( action );
    }
    menu->addSeparator();
    auto *hints = createAction( "view.cameraHints", tr( "Show Camera Controls" ), true );
    connect( hints, &QAction::toggled, this, [this]( bool visible ) {
        m_preferences.showCameraHints = visible;
        m_pRenderViewport->setCameraHintsVisible( visible );
        savePreferences();
    } );
    menu->addAction( hints );
    auto *settings = createAction( "camera.settings", tr( "Camera Settings…" ) );
    settings->setIcon( CypherTileEditorIcon_Create( tile_editor_icon_t::SETTINGS ) );
    connect( settings, &QAction::triggered, this, [this] { showSettings( true ); } );
    menu->addAction( settings );
    m_pViewWorkspace->setCameraMenu( menu );
    findChild<QMenu *>( "TileViewMenu" )->addMenu( menu );

    auto *border = createAction( "view.activeBorder", tr( "Show Active View Border" ), true );
    connect( border, &QAction::toggled, this, [this]( bool visible ) {
        m_preferences.showActiveViewBorder = visible;
        CypherTileEditorTheme_Apply( *qApp, m_preferences );
        savePreferences();
    } );
    findChild<QMenu *>( "TileViewMenu" )->addAction( border );
    m_pViewWorkspace->setContextMenuContributor(
        [this, menu]( QMenu &context, tile_editor_view_t view ) {
        if ( view == tile_editor_view_t::PERSPECTIVE ) {
            context.addMenu( menu );
            context.addAction( m_actions.value( QStringLiteral( "view.axes" ) ) );
        } else {
            context.addAction( m_actions.value( QStringLiteral( "view.grid" ) ) );
            context.addAction( m_actions.value( QStringLiteral( "view.adaptiveGrid" ) ) );
            context.addAction( m_actions.value( QStringLiteral( "view.rulers" ) ) );
            context.addAction( m_actions.value( QStringLiteral( "view.axes" ) ) );
            context.addAction( m_actions.value( QStringLiteral( "view.orthoMaterials" ) ) );
            context.addAction( m_actions.value( QStringLiteral( "view.materialLabels" ) ) );
            context.addAction( m_actions.value( QStringLiteral( "view.markers" ) ) );
        }
        context.addSeparator();
        context.addAction( m_actions.value( QStringLiteral( "view.activeBorder" ) ) );
        context.addAction( m_actions.value( QStringLiteral( "app.settings" ) ) );
    } );
}

void CypherTileEditorMainWindow::synchronizeCameraActions()
{
    for ( const auto &entry : { std::pair{ "camera.fly", m_preferences.cameraFlyMode },
                               std::pair{ "camera.orbit", !m_preferences.cameraFlyMode },
                               std::pair{ "camera.autoOrbit", m_pRenderViewport->isAutoOrbiting() },
                               std::pair{ "view.cameraHints", m_preferences.showCameraHints },
                               std::pair{ "view.activeBorder", m_preferences.showActiveViewBorder } } ) {
        if ( auto *action = m_actions.value( QString::fromLatin1( entry.first ), nullptr ) ) {
            const QSignalBlocker blocker( action );
            action->setChecked( entry.second );
        }
    }
    if ( auto *speed = findChild<QDoubleSpinBox *>( QStringLiteral( "TileCameraSpeed" ) ) ) {
        const QSignalBlocker blocker( speed );
        speed->setValue( m_preferences.cameraMoveSpeed );
    }
    for ( int slot = 0; slot < 4; ++slot ) {
        if ( auto *recall = m_actions.value( QStringLiteral( "camera.recall%1" ).arg( slot + 1 ), nullptr ) )
            recall->setEnabled( m_pRenderViewport->hasCameraBookmark( slot ) );
    }
}

void CypherTileEditorMainWindow::buildToolbar()
{
    QToolBar *pToolbar = addToolBar( tr( "Main" ) );
    pToolbar->setObjectName( QStringLiteral( "TileEditorMainToolbar" ) );
    pToolbar->setMovable( false );
    pToolbar->setToolButtonStyle( Qt::ToolButtonIconOnly );
    pToolbar->setIconSize( QSize( 18, 18 ) );
    pToolbar->addAction( m_actions.value( QStringLiteral( "file.new" ) ) );
    pToolbar->addAction( m_actions.value( QStringLiteral( "file.open" ) ) );
    pToolbar->addAction( m_actions.value( QStringLiteral( "file.save" ) ) );
    pToolbar->addSeparator();
    pToolbar->addAction( m_actions.value( QStringLiteral( "edit.undo" ) ) );
    pToolbar->addAction( m_actions.value( QStringLiteral( "edit.redo" ) ) );
    pToolbar->addSeparator();
    pToolbar->addAction( m_actions.value( QStringLiteral( "map.validate" ) ) );
    pToolbar->addAction( m_actions.value( QStringLiteral( "map.build" ) ) );
    pToolbar->addAction( m_actions.value( QStringLiteral( "map.preview" ) ) );
    pToolbar->addAction( m_actions.value( QStringLiteral( "view.live3d" ) ) );
    pToolbar->addAction( m_actions.value( QStringLiteral( "view.four" ) ) );
    pToolbar->addSeparator();
    pToolbar->addAction( m_actions.value( QStringLiteral( "view.fit" ) ) );
    pToolbar->addSeparator();
    auto *pGridLabel = new QLabel( tr( "Grid " ), pToolbar );
    pToolbar->addWidget( pGridLabel );
    auto *pGridStep = new QComboBox( pToolbar );
    pGridStep->setObjectName( QStringLiteral( "TileGridSpacing" ) );
    pGridStep->setToolTip( tr( "Visible grid spacing in cells; tile geometry stays unchanged" ) );
    for ( int step : { 1, 2, 4, 8, 16 } ) pGridStep->addItem( tr( "%1 cells" ).arg( step ), step );
    pGridStep->setCurrentIndex( pGridStep->findData( m_preferences.gridSpacingCells ) );
    connect( pGridStep, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this, pGridStep]( int ) {
        m_preferences.gridSpacingCells = pGridStep->currentData().toInt();
        applyPreferences();
        savePreferences();
    } );
    pToolbar->addWidget( pGridStep );
    auto *pAutoGrid = new QCheckBox( tr( "Auto" ), pToolbar );
    pAutoGrid->setObjectName( QStringLiteral( "TileAdaptiveGrid" ) );
    pAutoGrid->setToolTip( tr( "Adapt displayed grid spacing to zoom, using the selected cell spacing as its minimum" ) );
    connect( pAutoGrid, &QCheckBox::toggled, this, [this]( bool enabled ) {
        m_actions.value( QStringLiteral( "view.adaptiveGrid" ) )->setChecked( enabled );
    } );
    pToolbar->addWidget( pAutoGrid );
    pToolbar->addSeparator();
    pToolbar->addAction( m_actions.value( "view.orthoMaterials" ) );
    pToolbar->addAction( m_actions.value( QStringLiteral( "app.settings" ) ) );

    auto *pToolRail = new QToolBar( tr( "Map Tools" ), this );
    pToolRail->setObjectName( QStringLiteral( "TileEditorToolRail" ) );
    pToolRail->setMovable( false );
    pToolRail->setFloatable( false );
    pToolRail->setToolButtonStyle( Qt::ToolButtonIconOnly );
    pToolRail->setIconSize( QSize( 18, 18 ) );
    addToolBar( Qt::LeftToolBarArea, pToolRail );
    for ( QAction *pTool : m_pToolActions->actions() ) pToolRail->addAction( pTool );
    pToolRail->addSeparator();
    for ( const char *id : { "edit.move", "edit.copyOffset", "edit.rotate", "edit.raise", "edit.lower", "edit.delete" } )
        pToolRail->addAction( m_actions.value( QString::fromLatin1( id ) ) );
    pToolRail->addSeparator();
    pToolRail->addAction( m_actions.value( QStringLiteral( "view.four" ) ) );
    pToolRail->addAction( m_actions.value( QStringLiteral( "view.fit" ) ) );

    pToolbar->addSeparator();
    pToolbar->addAction( m_actions.value( "view.console" ) );
    const auto addDockToggle = [this, pToolbar]( QDockWidget *dock, const char *id,
        const QString &label, QStringView icon ) {
        auto *action = createAction( id, label, true );
        action->setIcon( CypherTileEditorIcon_Create( icon ) );
        action->setToolTip( label );
        action->setChecked( !dock->isHidden() );
        connect( action, &QAction::toggled, this, [dock]( bool visible ) {
            dock->setVisible( visible );
        } );
        connect( dock, &QDockWidget::visibilityChanged, action, [action]( bool visible ) {
            const QSignalBlocker blocker( action ); action->setChecked( visible );
        } );
        pToolbar->addAction( action );
        findChild<QMenu *>( "TileViewMenu" )->addAction( action );
    };
    addDockToggle( m_pAssetsDock, "view.assets", tr( "Show / Hide Materials and Pieces" ), u"material-library" );
    m_actions.value( QStringLiteral( "view.assets" ) )->setIcon(
        CypherTileEditorIcon_Create( tile_editor_icon_t::MATERIAL_LIBRARY ) );
    m_actions.value( QStringLiteral( "view.assets" ) )->setToolTip( tr(
        "Materials and Pieces panel · checked means the panel is visible" ) );
    addDockToggle( m_pOutlinerDock, "view.outliner", tr( "Show / Hide Objects" ), u"object-tree" );
    addDockToggle( m_pInspectorDock, "view.properties", tr( "Show / Hide Properties" ), u"panel-right" );
    auto *railToggle = pToolRail->toggleViewAction();
    railToggle->setObjectName( "view.toolRail" );
    m_actions.insert( QStringLiteral( "view.toolRail" ), railToggle );
    railToggle->setIcon( CypherTileEditorIcon_Create( u"panel-left" ) );
    railToggle->setToolTip( tr( "Show / Hide Tool Rail" ) );
    pToolbar->addAction( railToggle );
    findChild<QMenu *>( "TileViewMenu" )->addAction( railToggle );
}

void CypherTileEditorMainWindow::buildWorkspace()
{
    m_pCanvas = new CypherTileCanvas( this );
    m_pCanvas->setDocumentBridge( &m_document );
    m_pRenderViewport = new CypherTileRenderViewport( this );
    m_pRenderViewport->setDocumentBridge( &m_document );
    if ( m_pFrontView != nullptr ) m_pFrontView->setDocumentBridge( &m_document );
    if ( m_pSideView != nullptr ) m_pSideView->setDocumentBridge( &m_document );
    m_pFrontView = new CypherTileOrthoView( tile_editor_ortho_plane_t::FRONT, this );
    m_pSideView = new CypherTileOrthoView( tile_editor_ortho_plane_t::SIDE, this );
    m_pFrontView->setDocumentBridge( &m_document );
    m_pSideView->setDocumentBridge( &m_document );
    m_topViews = { m_pCanvas };
    m_orthoViews = { m_pFrontView, m_pSideView };
    m_pCanvas->setMaterialCache( &m_orthoMaterials );
    m_pFrontView->setMaterialCache( &m_orthoMaterials );
    m_pSideView->setMaterialCache( &m_orthoMaterials );
    m_pViewWorkspace = new CypherTileViewWorkspace(
        { m_pCanvas, m_pRenderViewport, m_pFrontView, m_pSideView }, this );
    m_pViewWorkspace->setFrameCallback( [this]( tile_editor_view_t view ) { frameView( view ); } );
    m_pRenderViewport->setContextMenuCallback( [this]( const QPoint &globalPosition ) {
        if ( m_pViewWorkspace != nullptr )
            m_pViewWorkspace->showPaneContextMenu( m_pRenderViewport, globalPosition );
    } );
    m_pCameraPreferenceTimer = new QTimer( this );
    m_pCameraPreferenceTimer->setSingleShot( true );
    m_pCameraPreferenceTimer->setInterval( 250 );
    connect( m_pCameraPreferenceTimer, &QTimer::timeout, this, [this] { savePreferences(); } );
    m_pRenderViewport->setCameraChangeCallback( [this]( const tile_camera_settings_t &settings, bool flyMode ) {
        m_preferences.cameraMoveSpeed = settings.moveSpeed;
        m_preferences.cameraFlyMode = flyMode;
        synchronizeCameraActions();
        // Wheel gestures can emit many changes. Persist their final value once
        // the gesture settles, without reapplying settings during navigation.
        m_pCameraPreferenceTimer->start();
    } );
    m_pRenderViewport->setAutoOrbitChangeCallback(
        [this]( bool ) { synchronizeCameraActions(); } );
    setCentralWidget( m_pViewWorkspace );
    m_pDocumentPreviewTimer = new QTimer( this );
    m_pDocumentPreviewTimer->setInterval( 40 );
    m_pDocumentPreviewTimer->setSingleShot( true );
    connect( m_pDocumentPreviewTimer, &QTimer::timeout, this, [this] {
        updateValidationPanel();
        refreshMapViews();
        updateInspector( m_pCanvas->hasSelection(), m_pCanvas->selectedCell() );
    } );
    m_pCanvas->setPreviewChangedCallback( [this] {
        // Throttle rather than debounce: a continuous stroke still updates all
        // views. History and external runtime snapshots remain commit-based.
        if ( !m_pDocumentPreviewTimer->isActive() ) m_pDocumentPreviewTimer->start();
    } );
    configureTopView( m_pCanvas );
    configureOrthoView( m_pFrontView );
    configureOrthoView( m_pSideView );
    m_pRenderViewport->setMultiSelectionCallback( [this]( bool selected, tile_map_grid_coord_t cell, Qt::KeyboardModifiers modifiers ) {
        m_pCanvas->modifySelectedCells( selected ? std::span<const tile_map_grid_coord_t>( &cell, 1 )
            : std::span<const tile_map_grid_coord_t>(), modifiers );
    } );
    m_pRenderViewport->setChangedCallback( [this] { onDocumentChanged(); } );
    m_pRenderViewport->setPaintPickedCallback( [this]( const tile_map_paint_t &paint ) {
        m_pCanvas->setPaint( paint ); if ( m_paintPicked ) m_paintPicked( paint ); synchronizeAuthoring();
    } );
    m_pRenderViewport->setStatusCallback(
        [this]( const QString &message, bool bError ) {
            setStatus( message, bError );
            if ( bError ) appendError( message );
        } );

    buildToolsDock();
    buildInspectorDock();
    buildOutlinerDock();
    connect( m_pValidationList, &QListWidget::itemActivated, this, [this]( QListWidgetItem *pItem ) {
        if ( !pItem->data( Qt::UserRole ).toBool() ) return;
        const tile_map_grid_coord_t cell{
            pItem->data( Qt::UserRole + 1 ).toInt(), pItem->data( Qt::UserRole + 2 ).toInt() };
        if ( !CypherTileMapDocument_ContainsCell( m_document.document(), cell ) ) {
            setStatus( tr( "Diagnostic cell (%1, %2) is outside the map bounds" ).arg( cell.x ).arg( cell.y ), true );
            return;
        }
        m_pViewWorkspace->focusView( tile_editor_view_t::TOP );
        auto *active = m_pViewWorkspace->activeWidget();
        auto *top = m_pCanvas;
        for ( auto *candidate : m_topViews ) if ( candidate == active ) { top = candidate; break; }
        top->selectCell( cell, true );
        setStatus( pItem->text() );
    } );
    buildConsoleDock();
    m_pViewWorkspace->setViewFactory( [this]( tile_editor_view_t view ) { return createWorkspaceView( view ); } );
    synchronizeAuthoring();
}

void CypherTileEditorMainWindow::configureTopView( CypherTileCanvas *view )
{
    view->setDocumentBridge( &m_document );
    view->setMaterialCache( &m_orthoMaterials );
    view->setPreferences( m_preferences );
    view->setChangedCallback( [this] { onDocumentChanged(); } );
    view->setPreviewChangedCallback( [this] {
        if ( !m_pDocumentPreviewTimer->isActive() ) m_pDocumentPreviewTimer->start();
    } );
    view->setSelectionCallback( [this, view]( bool, tile_map_grid_coord_t ) {
        if ( m_syncingSelection ) return;
        if ( view != m_pCanvas ) m_pCanvas->setSelectedCells( view->selectedCells() );
        else synchronizeSelection();
    } );
    view->setMoveSelectionCallback( [this]( int dx, int dy, bool copy ) { translateSelection( dx, dy, copy ); } );
    view->setPaintPickedCallback( [this]( const tile_map_paint_t &paint ) {
        m_pCanvas->setPaint( paint );
        if ( m_paintPicked ) m_paintPicked( paint );
        synchronizeAuthoring();
    } );
    view->setCursorCallback( [this]( bool inside, tile_map_grid_coord_t coordinate ) {
        if ( m_pStatusCursor ) m_pStatusCursor->setText( inside
            ? tr( "Cell %1, %2" ).arg( coordinate.x ).arg( coordinate.y ) : tr( "Cell —" ) );
    } );
    view->setZoomCallback( [this]( qreal zoom ) {
        if ( m_pStatusZoom ) m_pStatusZoom->setText( tr( "%1 px/cell" ).arg( zoom, 0, 'f', 1 ) );
    } );
    view->setStatusCallback( [this]( const QString &message, bool error ) {
        setStatus( message, error ); if ( error ) appendError( message );
    } );
    view->setContextMenuCallback( [this, view]( const QPoint &globalPosition ) {
        if ( m_pViewWorkspace != nullptr )
            m_pViewWorkspace->showPaneContextMenu( view, globalPosition );
    } );
}

void CypherTileEditorMainWindow::configureOrthoView( CypherTileOrthoView *view )
{
    view->setDocumentBridge( &m_document );
    view->setMaterialCache( &m_orthoMaterials );
    view->setPreferences( m_preferences );
    view->setMultiSelectionCallback( [this]( bool hit, tile_map_grid_coord_t cell, Qt::KeyboardModifiers modifiers ) {
        m_pCanvas->modifySelectedCells( hit ? std::span<const tile_map_grid_coord_t>( &cell, 1 )
            : std::span<const tile_map_grid_coord_t>(), modifiers );
    } );
    view->setSelectionCellsCallback( [this]( const std::vector<tile_map_grid_coord_t> &cells, Qt::KeyboardModifiers modifiers ) {
        m_pCanvas->modifySelectedCells( cells, modifiers );
    } );
    view->setChangedCallback( [this] { onDocumentChanged(); } );
    view->setPreviewChangedCallback( [this] {
        if ( !m_pDocumentPreviewTimer->isActive() ) m_pDocumentPreviewTimer->start();
    } );
    view->setStatusCallback( [this]( const QString &message, bool error ) {
        setStatus( message, error ); if ( error ) appendError( message );
    } );
    view->setContextMenuCallback( [this, view]( const QPoint &globalPosition ) {
        if ( m_pViewWorkspace != nullptr )
            m_pViewWorkspace->showPaneContextMenu( view, globalPosition );
    } );
    view->setPaintPickedCallback( [this]( const tile_map_paint_t &paint ) {
        m_pCanvas->setPaint( paint );
        if ( m_paintPicked ) m_paintPicked( paint );
        synchronizeAuthoring();
    } );
}

QWidget *CypherTileEditorMainWindow::createWorkspaceView( tile_editor_view_t type )
{
    QWidget *created = nullptr;
    if ( type == tile_editor_view_t::PERSPECTIVE ) return m_pRenderViewport;
    if ( type == tile_editor_view_t::TOP ) {
        auto *view = new CypherTileCanvas( this );
        view->setObjectName( QStringLiteral( "CypherTileCanvasExtra%1" ).arg( m_topViews.size() ) );
        m_topViews.push_back( view );
        configureTopView( view );
        created = view;
    } else {
        auto *view = new CypherTileOrthoView( type == tile_editor_view_t::FRONT
            ? tile_editor_ortho_plane_t::FRONT : tile_editor_ortho_plane_t::SIDE, this );
        view->setObjectName( QStringLiteral( "CypherTileOrthoExtra%1" ).arg( m_orthoViews.size() ) );
        m_orthoViews.push_back( view );
        configureOrthoView( view );
        created = view;
    }
    applyShortcuts();
    synchronizeSelection();
    synchronizeAuthoring();
    return created;
}

void CypherTileEditorMainWindow::synchronizeSelection()
{
    if ( m_syncingSelection ) return;
    m_syncingSelection = true;
    const auto &cells = m_pCanvas->selectedCells();
    for ( auto *view : m_topViews ) if ( view != m_pCanvas ) view->setSelectedCells( cells );
    for ( auto *view : m_orthoViews ) view->setSelectedCells( cells );
    m_pRenderViewport->setSelectedCells( cells );
    updateInspector( m_pCanvas->hasSelection(), m_pCanvas->selectedCell() );
    syncOutlinerSelection();
    if ( !cells.empty() && m_pConstructionX && m_pConstructionY ) {
        const QSignalBlocker x( m_pConstructionX ), y( m_pConstructionY );
        m_pConstructionX->setValue( cells.front().x );
        m_pConstructionY->setValue( cells.front().y );
    }
    synchronizeAuthoring();
    m_syncingSelection = false;
}

void CypherTileEditorMainWindow::synchronizeAuthoring()
{
    if ( m_pCanvas == nullptr ) return;
    const auto tool = m_pCanvas->tool();
    const auto paint = m_pCanvas->paint();
    const tile_map_grid_coord_t construction{ m_pConstructionX ? m_pConstructionX->value() : 0,
        m_pConstructionY ? m_pConstructionY->value() : 0 };
    for ( auto *view : m_topViews ) {
        view->setTool( tool ); view->setPaint( paint ); view->setDoorSide( m_pCanvas->doorSide() );
    }
    for ( auto *view : m_orthoViews ) {
        view->setTool( tool ); view->setPaint( paint ); view->setConstructionCell( construction );
        view->setDoorSide( m_pCanvas->doorSide() );
    }
    if ( m_pRenderViewport ) { m_pRenderViewport->setTool( tool ); m_pRenderViewport->setPaint( paint ); m_pRenderViewport->setDoorSide( m_pCanvas->doorSide() ); }
}

void CypherTileEditorMainWindow::frameView( tile_editor_view_t view )
{
    QWidget *active = m_pViewWorkspace ? m_pViewWorkspace->activeWidget() : nullptr;
    for ( auto *top : m_topViews ) if ( top == active && view == tile_editor_view_t::TOP ) {
        top->fitSelection(); return;
    }
    for ( auto *ortho : m_orthoViews ) if ( ortho == active &&
        ( view == tile_editor_view_t::FRONT || view == tile_editor_view_t::SIDE ) ) {
        ortho->fitToView(); return;
    }
    switch ( view ) {
        case tile_editor_view_t::TOP: m_pCanvas->fitToView(); break;
        case tile_editor_view_t::PERSPECTIVE: m_pRenderViewport->frameSelection(); break;
        case tile_editor_view_t::FRONT: m_pFrontView->fitToView(); break;
        case tile_editor_view_t::SIDE: m_pSideView->fitToView(); break;
    }
}

void CypherTileEditorMainWindow::refreshMapViews()
{
    refreshOrthoMaterials();
    for ( auto *view : m_topViews ) view->refreshDocument();
    for ( auto *view : m_orthoViews ) view->refreshDocument();
    m_pRenderViewport->refreshDocument();
    if ( m_pConstructionX && m_pConstructionY && m_document.isInitialized() ) {
        m_pConstructionX->setMaximum( static_cast<int>( m_document.document()->nWidth ) - 1 );
        m_pConstructionY->setMaximum( static_cast<int>( m_document.document()->nHeight ) - 1 );
    }
}

void CypherTileEditorMainWindow::refreshOrthoMaterials( bool force )
{
    if ( !m_document.isInitialized() ) return;
    const QString root = m_pMaterialBrowser ? m_pMaterialBrowser->cookedRoot() : QString{};
    if ( !TileOrthoMaterials_Refresh( m_orthoMaterials, *m_document.document(), root, force ) ) return;
    for ( auto *view : m_topViews ) view->update();
    for ( auto *view : m_orthoViews ) view->update();
}

void CypherTileEditorMainWindow::buildToolsDock()
{
    auto *pDock = new QDockWidget( tr( "MATERIALS & PIECES" ), this );
    pDock->setObjectName( QStringLiteral( "TileEditorToolsDock" ) );
    m_pAssetsDock = pDock;
    pDock->setMinimumWidth( 220 );
    auto *pScroll = new QScrollArea( pDock );
    pScroll->setWidgetResizable( true );
    pScroll->setFrameShape( QFrame::NoFrame );
    auto *pPanel = new QWidget( pScroll );
    auto *pLayout = new QVBoxLayout( pPanel );
    pLayout->setContentsMargins( 7, 7, 7, 7 );
    pLayout->setSpacing( 7 );

    auto *pPaint = new QGroupBox( tr( "Paint Properties" ), pPanel );
    auto *pPaintForm = new QFormLayout( pPaint );
    auto *pFloor = new QSpinBox( pPaint );
    auto *pWall = new QSpinBox( pPaint );
    m_pPaintMaterialSlot = new QSpinBox( pPaint );
    pFloor->setRange(
        std::numeric_limits<i16>::min(),
        std::numeric_limits<i16>::max() );
    pWall->setRange( 1, std::numeric_limits<u16>::max() );
    m_pPaintMaterialSlot->setRange( 0, 65535 );
    pFloor->setValue( m_pCanvas->paint().nFloorLevel );
    pWall->setValue( m_pCanvas->paint().nWallHeightLevels );
    m_pPaintMaterialSlot->setValue( m_pCanvas->paint().nMaterialSlot );
    auto *pShape = new QComboBox( pPaint );
    pShape->setObjectName( QStringLiteral( "TilePaintShape" ) );
    PopulateCellShapes( pShape );
    auto *pSteps = new QSpinBox( pPaint );
    pSteps->setObjectName( QStringLiteral( "TilePaintStairSteps" ) );
    pSteps->setRange( 2, 32 );
    pSteps->setValue( 8 );
    pSteps->setEnabled( false );
    m_paintPicked = [this, pFloor, pWall, pShape, pSteps]( const tile_map_paint_t &paint ) {
        const QSignalBlocker floorSignals( pFloor ), wallSignals( pWall ), shapeSignals( pShape ),
            stepSignals( pSteps ), materialSignals( m_pPaintMaterialSlot );
        pFloor->setValue( paint.nFloorLevel );
        pWall->setValue( paint.nWallHeightLevels );
        m_pPaintMaterialSlot->setValue( paint.nMaterialSlot );
        if ( m_pMaterialList != nullptr ) {
            const QSignalBlocker paletteSignals( m_pMaterialList );
            m_pMaterialList->setCurrentRow( -1 );
            for ( int i = 0; i < m_pMaterialList->count(); ++i ) {
                if ( m_pMaterialList->item( i )->data( Qt::UserRole ).toUInt() == paint.nMaterialSlot ) {
                    m_pMaterialList->setCurrentRow( i );
                    break;
                }
            }
        }
        pShape->setCurrentIndex( pShape->findData( static_cast<int>( paint.shape ) ) );
        pSteps->setValue( paint.nStairSteps );
        pSteps->setEnabled( paint.shape != tile_map_cell_shape_t::FLAT );
    };
    auto updatePaint = [this, pFloor, pWall, pShape, pSteps] {
        tile_map_paint_t paint{};
        paint.nFloorLevel = static_cast<i16>( pFloor->value() );
        paint.nWallHeightLevels = static_cast<u16>( pWall->value() );
        paint.nMaterialSlot = static_cast<u16>( m_pPaintMaterialSlot->value() );
        paint.shape = static_cast<tile_map_cell_shape_t>( pShape->currentData().toInt() );
        paint.nStairSteps = static_cast<u16>( pSteps->value() );
        pSteps->setEnabled( paint.shape != tile_map_cell_shape_t::FLAT );
        m_pCanvas->setPaint( paint );
        synchronizeAuthoring();
    };
    for ( QSpinBox *spin : { pFloor, pWall, m_pPaintMaterialSlot, pSteps } )
        connect( spin, qOverload<int>( &QSpinBox::valueChanged ), this, [updatePaint]( int ) { updatePaint(); } );
    connect( pShape, qOverload<int>( &QComboBox::currentIndexChanged ), this, [updatePaint]( int ) { updatePaint(); } );
    pPaintForm->addRow( tr( "Shape" ), pShape );
    pPaintForm->addRow( tr( "Stair steps" ), pSteps );
    pPaintForm->addRow( tr( "Floor level" ), pFloor );
    pPaintForm->addRow( tr( "Wall levels" ), pWall );
    pPaintForm->addRow( tr( "Material slot" ), m_pPaintMaterialSlot );
    m_pConstructionX = new QSpinBox( pPaint );
    m_pConstructionY = new QSpinBox( pPaint );
    m_pConstructionX->setObjectName( "TileConstructionColumn" );
    m_pConstructionY->setObjectName( "TileConstructionRow" );
    m_pConstructionX->setRange( 0, 1023 );
    m_pConstructionY->setRange( 0, 1023 );
    pPaintForm->addRow( tr( "Front view row Y" ), m_pConstructionY );
    pPaintForm->addRow( tr( "Side view column X" ), m_pConstructionX );
    m_pConstructionY->setToolTip( tr( "Front-view placement edits this map row. Clicking a selected tile updates this row." ) );
    m_pConstructionX->setToolTip( tr( "Side-view placement edits this map column. Clicking a selected tile updates this column." ) );
    for ( auto *spin : { m_pConstructionX, m_pConstructionY } )
        connect( spin, qOverload<int>( &QSpinBox::valueChanged ), this, [this] { synchronizeAuthoring(); } );
    pLayout->addWidget( pPaint );

    auto *pObjects = new QGroupBox( tr( "Special Objects" ), pPanel );
    auto *pObjectsForm = new QFormLayout( pObjects );
    m_pDoorSide = new QComboBox( pObjects );
    m_pDoorSide->addItem(
        tr( "North edge" ), static_cast<int>( tile_map_marker_side_t::NORTH ) );
    m_pDoorSide->addItem(
        tr( "East edge" ), static_cast<int>( tile_map_marker_side_t::EAST ) );
    m_pDoorSide->addItem(
        tr( "South edge" ), static_cast<int>( tile_map_marker_side_t::SOUTH ) );
    m_pDoorSide->addItem(
        tr( "West edge" ), static_cast<int>( tile_map_marker_side_t::WEST ) );
    pObjectsForm->addRow( tr( "Door side" ), m_pDoorSide );
    auto *pDoorHint = new QLabel(
        tr( "A door replaces one exposed wall segment. Click the same edge again to remove it." ),
        pObjects );
    pDoorHint->setWordWrap( true );
    pDoorHint->setProperty( "muted", true );
    pObjectsForm->addRow( pDoorHint );
    connect( m_pDoorSide, qOverload<int>( &QComboBox::currentIndexChanged ),
             this, [this]( int iIndex ) {
        const auto side = static_cast<tile_map_marker_side_t>(
            m_pDoorSide->itemData( iIndex ).toInt() );
        m_pCanvas->setDoorSide( side );
        setTool( tile_canvas_tool_t::DOOR );
    } );
    pLayout->addWidget( pObjects );

    auto *pMaterials = new QGroupBox( tr( "Blockout Materials" ), pPanel );
    auto *pMaterialsLayout = new QVBoxLayout( pMaterials );
    pMaterialsLayout->setContentsMargins( 5, 8, 5, 5 );
    pMaterialsLayout->setSpacing( 5 );
    m_pMaterialFilter = new QLineEdit( pMaterials );
    m_pMaterialFilter->setPlaceholderText( tr( "Filter materials..." ) );
    m_pMaterialFilter->setClearButtonEnabled( true );
    m_pMaterialFilter->addAction(
        CypherTileEditorIcon_Create( tile_editor_icon_t::SEARCH ),
        QLineEdit::LeadingPosition );
    m_pMaterialList = new QListWidget( pMaterials );
    m_pMaterialList->setObjectName( QStringLiteral( "TileMaterialPalette" ) );
    m_pMaterialList->setViewMode( QListView::IconMode );
    m_pMaterialList->setMovement( QListView::Static );
    m_pMaterialList->setResizeMode( QListView::Adjust );
    m_pMaterialList->setFlow( QListView::LeftToRight );
    m_pMaterialList->setWrapping( true );
    m_pMaterialList->setIconSize( QSize( 76, 50 ) );
    m_pMaterialList->setGridSize( QSize( 102, 88 ) );
    m_pMaterialList->setSpacing( 2 );
    m_pMaterialList->setMinimumHeight( 80 );
    m_pMaterialList->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    for ( usize iMaterial = 0u;
          iMaterial < CypherTileMapMaterial_Count();
          ++iMaterial ) {
        const tile_map_material_definition_t *pDefinition =
            CypherTileMapMaterial_At( iMaterial );
        if ( pDefinition == nullptr ) continue;
        auto *pItem = new QListWidgetItem(
            MaterialSwatchIcon( *pDefinition ),
            QString::fromUtf8( pDefinition->pDisplayName ),
            m_pMaterialList );
        pItem->setData( Qt::UserRole, pDefinition->nSlot );
        pItem->setData(
            Qt::UserRole + 1,
            QString::fromUtf8( pDefinition->pStableId ) );
        pItem->setToolTip( tr( "%1\n%2\nMaterial slot %3" )
            .arg(
                QString::fromUtf8( pDefinition->pDisplayName ),
                QString::fromUtf8( pDefinition->pStableId ) )
            .arg( pDefinition->nSlot ) );
        if ( pDefinition->nSlot == m_pCanvas->paint().nMaterialSlot ) {
            m_pMaterialList->setCurrentItem( pItem );
        }
    }
    connect( m_pMaterialList, &QListWidget::currentItemChanged, this,
             [this]( QListWidgetItem *pCurrent ) {
        if ( pCurrent == nullptr ) return;
        m_pPaintMaterialSlot->setValue(
            pCurrent->data( Qt::UserRole ).toInt() );
        setTool( tile_canvas_tool_t::PAINT );
        setStatus( tr( "Paint material: %1" ).arg( pCurrent->text() ) );
    } );
    connect( m_pPaintMaterialSlot, qOverload<int>( &QSpinBox::valueChanged ),
             this, [this]( int nSlot ) {
        for ( int iItem = 0; iItem < m_pMaterialList->count(); ++iItem ) {
            QListWidgetItem *pItem = m_pMaterialList->item( iItem );
            if ( pItem->data( Qt::UserRole ).toInt() == nSlot ) {
                m_pMaterialList->setCurrentItem( pItem );
                return;
            }
        }
        m_pMaterialList->clearSelection();
    } );
    connect( m_pMaterialFilter, &QLineEdit::textChanged, this,
             [this]( const QString &text ) {
        const QString needle = text.trimmed();
        for ( int iItem = 0; iItem < m_pMaterialList->count(); ++iItem ) {
            QListWidgetItem *pItem = m_pMaterialList->item( iItem );
            const QString haystack = pItem->text() + QLatin1Char( ' ' ) +
                pItem->data( Qt::UserRole + 1 ).toString() + QLatin1Char( ' ' ) +
                pItem->data( Qt::UserRole ).toString();
            pItem->setHidden(
                !needle.isEmpty() &&
                !haystack.contains( needle, Qt::CaseInsensitive ) );
        }
    } );
    pMaterialsLayout->addWidget( m_pMaterialFilter );
    pMaterialsLayout->addWidget( m_pMaterialList );
    pLayout->insertWidget( 0, pMaterials );

    auto *pStamps = new CypherTilePiecePalette( pPanel );
    pStamps->setActivateCallback( [this, pShape, pWall]( const tile_piece_t &piece ) {
        if ( piece.kind == tile_piece_kind_t::STAIRS ) {
            static constexpr tile_map_cell_shape_t shapes[]{ tile_map_cell_shape_t::STAIRS_NORTH,
                tile_map_cell_shape_t::STAIRS_EAST, tile_map_cell_shape_t::STAIRS_SOUTH, tile_map_cell_shape_t::STAIRS_WEST };
            pShape->setCurrentIndex( pShape->findData( static_cast<int>( shapes[piece.orientation] ) ) );
            setTool( tile_canvas_tool_t::PAINT );
        } else if ( piece.kind == tile_piece_kind_t::DOOR ) {
            static constexpr tile_map_marker_side_t sides[]{ tile_map_marker_side_t::NORTH,
                tile_map_marker_side_t::EAST, tile_map_marker_side_t::SOUTH, tile_map_marker_side_t::WEST };
            m_pDoorSide->setCurrentIndex( m_pDoorSide->findData( static_cast<int>( sides[piece.orientation] ) ) );
            m_pCanvas->setDoorSide( sides[piece.orientation] );
            setTool( tile_canvas_tool_t::DOOR );
        } else if ( piece.kind == tile_piece_kind_t::BOUNDARY ) {
            pShape->setCurrentIndex( pShape->findData( static_cast<int>( tile_map_cell_shape_t::FLAT ) ) );
            pWall->setValue( piece.wallLevels );
            setTool( tile_canvas_tool_t::RECTANGLE );
        } else {
            if ( !m_pCanvas->hasSelection() ) { appendWarning( tr( "Select a cell in Top to anchor the footprint." ) ); return; }
            QString error;
            if ( !TileEditorPiece_Stamp( m_document, piece, m_pCanvas->selectedCell(), m_pCanvas->paint(), error ) ) {
                appendWarning( error ); return;
            }
            m_pCanvas->refreshDocument();
            onDocumentChanged();
        }
        setStatus( tr( "%1 selected" ).arg( piece.label ) );
    } );
    pLayout->addWidget( pStamps );
    auto *pHint = new QLabel(
        tr( "Middle-drag or Space-drag pans. The mouse wheel zooms around the cursor. Escape cancels the active drag." ),
        pPanel );
    pHint->setWordWrap( true );
    pHint->setProperty( "muted", true );
    pLayout->addWidget( pHint );
    pLayout->addStretch( 1 );

    pLayout->removeWidget( pMaterials );
    pLayout->removeWidget( pStamps );
    auto *tabs = new QTabWidget( pDock );
    tabs->setObjectName( QStringLiteral( "TileAssetTabs" ) );
    pMaterials->setTitle( QString() );
    tabs->addTab( pMaterials, tr( "Blockout Palette" ) );
    tabs->addTab( pStamps, tr( "Room Pieces" ) );
    m_pMaterialBrowser = new CypherTileMaterialBrowser( tabs );
    tabs->insertTab( 0, m_pMaterialBrowser, tr( "Project Materials" ) );
    tabs->setCurrentWidget( m_pMaterialBrowser );
    m_pMaterialBrowser->setAssignCallback( [this]( unsigned short slot, const QString &path ) { bindProjectMaterial( slot, path ); } );
    m_pMaterialBrowser->setApplyCallback( [this]( unsigned short slot ) { applyMaterialToSelection( slot ); } );
    m_pMaterialBrowser->setStatusCallback( [this]( const QString &message, bool error ) { if ( error ) appendError( message ); else appendInfo( message ); } );
    m_pMaterialBrowser->setReloadCallback( [this]( const QString &root ) {
        refreshOrthoMaterials( true );
        m_pRenderViewport->setMaterialRoot( root );
        m_pRenderViewport->reloadMaterials();
        if ( m_pPreviewProcess != nullptr && m_pPreviewProcess->state() != QProcess::NotRunning ) {
            const auto arguments = m_pPreviewProcess->arguments();
            const auto index = arguments.indexOf( "--asset-root" );
            if ( index < 0 || arguments.value( index + 1 ) != root )
                appendInfo( tr( "Asset root changed. Relaunch Runtime Preview to switch projects." ) );
        }
    } );
    m_pRenderViewport->setMaterialRoot( m_pMaterialBrowser->cookedRoot() );
    pScroll->setWidget( pPanel );
    m_pPaintPanel = pScroll;
    pDock->setWidget( tabs );
    addDockWidget( Qt::RightDockWidgetArea, pDock );
}

void CypherTileEditorMainWindow::buildInspectorDock()
{
    auto *pDock = new QDockWidget( tr( "PROPERTIES" ), this );
    m_pInspectorDock = pDock;
    pDock->setObjectName( QStringLiteral( "TileEditorInspectorDock" ) );
    pDock->setMinimumWidth( 265 );
    auto *pContainer = new QWidget( pDock );
    auto *pContainerLayout = new QVBoxLayout( pContainer );
    pContainerLayout->setContentsMargins( 5, 5, 5, 5 );
    pContainerLayout->setSpacing( 5 );
    m_pValidationSummary = new QLabel( tr( "Live checks pending" ), pContainer );
    m_pValidationSummary->setObjectName( QStringLiteral( "TileValidationSummary" ) );
    m_pValidationSummary->setWordWrap( true );
    m_pValidationSummary->setToolTip( tr(
        "Checks update while editing and after undo/redo. Checks cover document "
        "structure, player spawn, and door placement. Spawn warnings do not block "
        "geometry rendering. Connectivity and gameplay collision are not checked." ) );
    pContainerLayout->addWidget( m_pValidationSummary );
    auto *pTabs = new QTabWidget( pContainer );
    m_pInspectorTabs = pTabs;
    pTabs->setObjectName( QStringLiteral( "TileInspectorTabs" ) );
    pContainerLayout->addWidget( pTabs, 1 );

    auto *pInspector = new QWidget( pTabs );
    auto *pInspectorLayout = new QVBoxLayout( pInspector );
    pInspectorLayout->setContentsMargins( 7, 7, 7, 7 );
    auto *pDocumentGroup = new QGroupBox( tr( "Document" ), pInspector );
    auto *pDocumentForm = new QFormLayout( pDocumentGroup );
    m_pDocumentSize = new QLabel( tr( "—" ), pDocumentGroup );
    pDocumentForm->addRow( tr( "Grid" ), m_pDocumentSize );
    auto *pEditMap = new QPushButton( tr( "Edit Map Properties…" ), pDocumentGroup );
    pEditMap->setObjectName( QStringLiteral( "TileEditMapProperties" ) );
    connect( pEditMap, &QPushButton::clicked, this, [this] { showMapProperties(); } );
    pDocumentForm->addRow( pEditMap );
    pInspectorLayout->addWidget( pDocumentGroup );

    auto *pCellGroup = new QGroupBox( tr( "Selected Tiles" ), pInspector );
    auto *pCellForm = new QFormLayout( pCellGroup );
    m_pSelectionLabel = new QLabel( tr( "No selection" ), pCellGroup );
    m_pFloorEnabled = new QCheckBox( tr( "Playable floor" ), pCellGroup );
    m_pFloorLevel = new QSpinBox( pCellGroup );
    m_pWallHeight = new QSpinBox( pCellGroup );
    m_pMaterialSlot = new QSpinBox( pCellGroup );
    m_pSpawnYaw = new QDoubleSpinBox( pCellGroup );
    m_pSelectionLabel->setObjectName( "TileSelectionSummary" );
    m_pSelectionLabel->setWordWrap( true );
    m_pFloorEnabled->setObjectName( "TileSelectionFloorEnabled" );
    m_pFloorLevel->setObjectName( "TileSelectionFloorLevel" );
    m_pWallHeight->setObjectName( "TileSelectionWallHeight" );
    m_pMaterialSlot->setObjectName( "TileSelectionMaterialSlot" );
    m_pCellShape = new QComboBox( pCellGroup );
    m_pCellShape->setObjectName( QStringLiteral( "TileSelectedShape" ) );
    PopulateCellShapes( m_pCellShape );
    m_pCellShape->setPlaceholderText( tr( "Mixed" ) );
    m_pStairSteps = new QSpinBox( pCellGroup );
    m_pStairSteps->setObjectName( QStringLiteral( "TileSelectedStairSteps" ) );
    m_pStairSteps->setRange( 2, 32 );
    m_pFloorLevel->setRange(
        std::numeric_limits<i16>::min(),
        std::numeric_limits<i16>::max() );
    m_pWallHeight->setRange( 1, std::numeric_limits<u16>::max() );
    m_pMaterialSlot->setRange( 0, 65535 );
    m_pSpawnYaw->setRange(
        -static_cast<double>( std::numeric_limits<f32>::max() ),
        static_cast<double>( std::numeric_limits<f32>::max() ) );
    m_pSpawnYaw->setDecimals( 6 );
    m_pSpawnYaw->setSuffix( tr( "°" ) );
    pCellForm->addRow( tr( "Coordinate" ), m_pSelectionLabel );
    pCellForm->addRow( QString(), m_pFloorEnabled );
    pCellForm->addRow( tr( "Shape" ), m_pCellShape );
    pCellForm->addRow( tr( "Stair steps" ), m_pStairSteps );
    pCellForm->addRow( tr( "Floor level" ), m_pFloorLevel );
    pCellForm->addRow( tr( "Wall levels" ), m_pWallHeight );
    pCellForm->addRow( tr( "Material slot" ), m_pMaterialSlot );
    pCellForm->addRow( tr( "Spawn yaw" ), m_pSpawnYaw );
    auto *propertyHint = new QLabel( tr( "Mixed fields keep their individual values until edited. Each changed field applies to every selected floor. Playable floor creates or removes floors in the selected cells." ), pCellGroup );
    propertyHint->setWordWrap( true );
    propertyHint->setProperty( "muted", true );
    pCellForm->addRow( propertyHint );
    pInspectorLayout->addWidget( pCellGroup );
    auto *pTransform = new QGroupBox( tr( "Selection Transform" ), pInspector );
    auto *pTransformLayout = new QFormLayout( pTransform );
    m_pOffsetX = new QSpinBox( pTransform );
    m_pOffsetY = new QSpinBox( pTransform );
    m_pOffsetX->setObjectName( "TileSelectionOffsetX" );
    m_pOffsetY->setObjectName( "TileSelectionOffsetY" );
    m_pOffsetX->setRange( -1024, 1024 );
    m_pOffsetY->setRange( -1024, 1024 );
    m_pOffsetX->setValue( 1 );
    pTransformLayout->addRow( tr( "Offset X" ), m_pOffsetX );
    pTransformLayout->addRow( tr( "Offset Y" ), m_pOffsetY );
    auto *pTransformButtons = new QHBoxLayout;
    for ( const char *id : { "edit.move", "edit.copyOffset", "edit.rotate", "edit.raise", "edit.lower", "edit.delete" } ) {
        auto *button = new QToolButton( pTransform );
        button->setDefaultAction( m_actions.value( QString::fromLatin1( id ) ) );
        button->setToolButtonStyle( Qt::ToolButtonIconOnly );
        button->setIconSize( QSize( 18, 18 ) );
        pTransformButtons->addWidget( button );
    }
    pTransformLayout->addRow( pTransformButtons );
    auto *wallButtons = new QHBoxLayout;
    for ( const char *id : { "edit.wallRaise", "edit.wallLower" } ) {
        auto *button = new QToolButton( pTransform );
        button->setDefaultAction( m_actions.value( QString::fromLatin1( id ) ) );
        button->setToolButtonStyle( Qt::ToolButtonIconOnly );
        wallButtons->addWidget( button );
    }
    wallButtons->addStretch();
    pTransformLayout->addRow( tr( "Wall height" ), wallButtons );
    auto *pTransformHint = new QLabel( tr( "V: select. Drag a box in Top/Front/Side. Shift adds, Ctrl/Cmd toggles, Alt removes. Drag selected tiles in Top to move. Arrows: move. Page Up/Down: floor elevation. Shift+Page Up/Down: wall height. Walls move with their owning tiles." ), pTransform );
    pTransformHint->setWordWrap( true );
    pTransformHint->setProperty( "muted", true );
    pTransformLayout->addRow( pTransformHint );
    pInspectorLayout->addWidget( pTransform );
    pInspectorLayout->addStretch( 1 );
    auto *pSelectionScroll = new QScrollArea( pTabs );
    pSelectionScroll->setWidgetResizable( true );
    pSelectionScroll->setFrameShape( QFrame::NoFrame );
    pSelectionScroll->setWidget( pInspector );
    pTabs->addTab( pSelectionScroll, tr( "Selection" ) );
    pTabs->addTab( m_pPaintPanel, tr( "Paint" ) );

    auto *pMapScroll = new QScrollArea( pTabs );
    pMapScroll->setWidgetResizable( true );
    pMapScroll->setFrameShape( QFrame::NoFrame );
    m_pMapProperties = new CypherTileMapProperties( pMapScroll );
    m_pMapProperties->setApplyCallback( [this]( const tile_map_document_desc_t &description, QString &error ) {
        return applyMapDescription( description, error );
    } );
    pMapScroll->setWidget( m_pMapProperties );
    m_pMapPropertiesPage = pMapScroll;
    pTabs->addTab( pMapScroll, tr( "Map" ) );

    auto connectInspector = [this]( QSpinBox *spin, flags32_t field ) {
        spin->setKeyboardTracking( false );
        connect( spin, qOverload<int>( &QSpinBox::valueChanged ), this, [this, field]( int ) {
            if ( !m_bUpdatingInspector ) m_inspectorDirtyFields |= field;
        } );
        // A mixed field can be explicitly set to its representative value,
        // which does not emit valueChanged. Track actual text edits as well.
        connect( spin->findChild<QLineEdit *>(), &QLineEdit::textEdited, this, [this, field] {
            if ( !m_bUpdatingInspector ) m_inspectorDirtyFields |= field;
        } );
        connect( spin, &QSpinBox::editingFinished, this, [this, field] {
            if ( m_inspectorDirtyFields & field ) applyInspectorCellEdit( field );
        } );
    };
    connectInspector( m_pFloorLevel, TILE_MAP_SELECTION_PROPERTY_FLOOR_LEVEL );
    connectInspector( m_pWallHeight, TILE_MAP_SELECTION_PROPERTY_WALL_HEIGHT );
    connectInspector( m_pMaterialSlot, TILE_MAP_SELECTION_PROPERTY_MATERIAL );
    connectInspector( m_pStairSteps, TILE_MAP_SELECTION_PROPERTY_STAIR_STEPS );
    connect( m_pCellShape, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]( int index ) {
        if ( index >= 0 ) applyInspectorCellEdit( TILE_MAP_SELECTION_PROPERTY_SHAPE );
    } );
    connect( m_pFloorEnabled, &QCheckBox::clicked, this, [this] {
        if ( m_pFloorEnabled->checkState() != Qt::PartiallyChecked )
            applyInspectorCellEdit( TILE_MAP_SELECTION_PROPERTY_FLOOR_ENABLED );
    } );
    connect( m_pSpawnYaw, &QDoubleSpinBox::editingFinished,
             this, [this] { applyInspectorSpawnEdit(); } );

    auto *pValidation = new QWidget( pTabs );
    auto *pValidationLayout = new QVBoxLayout( pValidation );
    pValidationLayout->setContentsMargins( 7, 7, 7, 7 );
    m_pValidationList = new QListWidget( pValidation );
    m_pValidationList->setObjectName( QStringLiteral( "TileValidationList" ) );
    m_pValidationList->setAlternatingRowColors( true );
    m_pValidationList->setWordWrap( true );
    m_pValidationList->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    pValidationLayout->addWidget( m_pValidationList, 1 );
    auto *pValidationHelp = new QLabel( tr(
        "Double-click an issue to locate its cell. Spawn warnings still allow "
        "geometry inspection." ), pValidation );
    pValidationHelp->setWordWrap( true );
    pValidationLayout->addWidget( pValidationHelp );
    auto *pValidationCommands = new QHBoxLayout();
    auto *pValidate = new QPushButton( tr( "Validate" ), pValidation );
    auto *pBuild = new QPushButton( tr( "Build Geometry" ), pValidation );
    pValidate->setIcon(
        CypherTileEditorIcon_Create( tile_editor_icon_t::VALIDATE ) );
    pBuild->setIcon(
        CypherTileEditorIcon_Create( tile_editor_icon_t::BUILD ) );
    connect( pValidate, &QPushButton::clicked, this,
             [this] { validateMap(); } );
    connect( pBuild, &QPushButton::clicked, this,
             [this] { buildMap(); } );
    pValidationCommands->addWidget( pValidate );
    pValidationCommands->addWidget( pBuild );
    pValidationLayout->addLayout( pValidationCommands );
    pTabs->addTab( pValidation, tr( "Validation" ) );

    pDock->setWidget( pContainer );
    addDockWidget( Qt::RightDockWidgetArea, pDock );
    updateInspector( false, {} );
}

void CypherTileEditorMainWindow::bindProjectMaterial( unsigned short slot, const QString &path )
{
    const QByteArray utf8 = path.toUtf8();
    const auto result = CypherTileMapDocument_SetMaterialBinding( m_document.document(), slot,
        { utf8.constData(), static_cast<usize>( utf8.size() ) } );
    if ( result != tile_map_document_status_t::OK ) {
        appendError( tr( "Could not bind material: %1" ).arg( QString::fromLatin1( CypherTileMapDocument_StatusName( result ) ) ) );
        return;
    }
    onDocumentChanged();
    if ( path.isEmpty() ) {
        setStatus( tr( "Cleared material binding for slot %1" ).arg( slot ) );
    } else {
        m_pPaintMaterialSlot->setValue( slot );
        setTool( tile_canvas_tool_t::PAINT );
        setStatus( tr( "%1 bound to map slot %2; ready to paint" ).arg( path ).arg( slot ) );
    }
}

void CypherTileEditorMainWindow::applyMaterialToSelection( unsigned short slot )
{
    if ( !m_pCanvas->hasSelection() ) { appendWarning( tr( "Select cells in Top before applying a material." ) ); return; }
    tile_map_selection_patch_t patch{};
    patch.fields = TILE_MAP_SELECTION_PROPERTY_MATERIAL;
    patch.nMaterialSlot = slot;
    QString error;
    if ( !m_document.applySelectionProperties( m_pCanvas->selectedCells(), patch, &error ) ) {
        appendError( error ); return;
    }
    m_pCanvas->refreshDocument();
    onDocumentChanged();
    setStatus( tr( "Applied material slot %1 to selected floors" ).arg( slot ) );
}

void CypherTileEditorMainWindow::buildOutlinerDock()
{
    m_pOutlinerDock = new QDockWidget( tr( "ALL OBJECTS" ), this );
    m_pOutlinerDock->setObjectName( "TileEditorOutlinerDock" );
    m_pOutlinerDock->setMinimumWidth( 265 );
    auto *panel = new QWidget( m_pOutlinerDock );
    auto *layout = new QVBoxLayout( panel );
    layout->setContentsMargins( 4, 4, 4, 4 );
    layout->setSpacing( 3 );
    m_pObjectFilter = new QLineEdit( panel );
    m_pObjectFilter->setObjectName( "TileObjectFilter" );
    m_pObjectFilter->setPlaceholderText( tr( "Filter objects, coordinates, material..." ) );
    m_pObjectFilter->setClearButtonEnabled( true );
    m_pOutliner = new QTreeWidget( panel );
    m_pOutliner->setObjectName( "TileObjectTree" );
    m_pOutliner->setHeaderHidden( true );
    m_pOutliner->setUniformRowHeights( true );
    m_pOutliner->setIndentation( 12 );
    m_pOutliner->setSelectionMode( QAbstractItemView::ExtendedSelection );
    m_pObjectCount = new QLabel( panel );
    m_pObjectCount->setProperty( "muted", true );
    layout->addWidget( m_pObjectFilter );
    layout->addWidget( m_pOutliner, 1 );
    layout->addWidget( m_pObjectCount );
    connect( m_pObjectFilter, &QLineEdit::textChanged, this, [this] { refreshOutliner(); } );
    connect( m_pOutliner, &QTreeWidget::itemSelectionChanged, this, [this] {
        std::vector<tile_map_grid_coord_t> cells;
        for ( auto *item : m_pOutliner->selectedItems() ) {
            if ( !item->data( 0, Qt::UserRole ).isValid() ) continue;
            const QPoint cell = item->data( 0, Qt::UserRole ).toPoint();
            cells.push_back( { cell.x(), cell.y() } );
        }
        m_pCanvas->setSelectedCells( cells );
    } );
    connect( m_pOutliner, &QTreeWidget::itemDoubleClicked, this, [this]( QTreeWidgetItem *item ) {
        if ( item != nullptr && item->data( 0, Qt::UserRole ).isValid() ) m_pRenderViewport->frameSelection();
    } );
    m_pOutlinerDock->setWidget( panel );
    addDockWidget( Qt::RightDockWidgetArea, m_pOutlinerDock );
}

void CypherTileEditorMainWindow::refreshOutliner()
{
    if ( m_pOutliner == nullptr ) return;
    const QSignalBlocker outlinerSignals( m_pOutliner );
    // Reuse the current filter; cap widget creation while still searching every
    // authored cell. Large maps must not create hundreds of thousands of rows.
    m_pOutliner->clear();
    if ( !m_document.isInitialized() ) return;
    const auto *doc = m_document.document();
    const QString filter = m_pObjectFilter->text().trimmed();
    auto *markers = new QTreeWidgetItem( m_pOutliner, { tr( "Entities" ) } );
    auto *stairs = new QTreeWidgetItem( m_pOutliner, { tr( "Stairs" ) } );
    auto *floors = new QTreeWidgetItem( m_pOutliner, { tr( "Floors" ) } );
    constexpr int rowLimit = 1024;
    int matched = 0;
    int shown = 0;
    const auto add = [&]( QTreeWidgetItem *parent, const QString &label, tile_map_grid_coord_t cell ) {
        if ( !filter.isEmpty() && !label.contains( filter, Qt::CaseInsensitive ) ) return;
        ++matched;
        if ( shown >= rowLimit ) return;
        auto *row = new QTreeWidgetItem( parent, { label } );
        row->setData( 0, Qt::UserRole, QPoint( cell.x, cell.y ) );
        ++shown;
    };
    for ( usize i = 0; i < doc->markers.nCount; ++i ) {
        const auto *marker = Vector_At( &doc->markers, i );
        add( markers, tr( "%1 · (%2, %3)" ).arg(
            marker->kind == tile_map_marker_kind_t::PLAYER_SPAWN ? tr( "Player spawn" ) : tr( "Door" ) )
                .arg( marker->cell.x ).arg( marker->cell.y ), marker->cell );
    }
    for ( u32 y = 0; y < doc->nHeight; ++y ) {
        for ( u32 x = 0; x < doc->nWidth; ++x ) {
            const tile_map_grid_coord_t coord{ static_cast<i32>( x ), static_cast<i32>( y ) };
            const auto *cell = CypherTileMapDocument_CellAt( doc, coord );
            if ( cell == nullptr || ( cell->flags & TILE_MAP_CELL_FLAG_FLOOR ) == 0 ) continue;
            const bool stair = cell->shape != tile_map_cell_shape_t::FLAT;
            add( stair ? stairs : floors, tr( "%1 (%2, %3) · level %4 · mat %5" )
                .arg( stair ? tr( "Stair" ) : tr( "Floor" ) ).arg( x ).arg( y )
                .arg( cell->nFloorLevel ).arg( cell->nMaterialSlot ), coord );
        }
    }
    for ( auto *group : { markers, stairs, floors } ) {
        group->setFlags( group->flags() & ~Qt::ItemIsSelectable );
        group->setHidden( group->childCount() == 0 );
        group->setExpanded( true );
    }
    m_pObjectCount->setText( matched > rowLimit
        ? tr( "%1 of %2 matches · refine filter" ).arg( shown ).arg( matched )
        : tr( "%1 objects" ).arg( shown ) );
    syncOutlinerSelection();
}

void CypherTileEditorMainWindow::resetWorkspaceLayout()
{
    if ( auto *rail = findChild<QToolBar *>( "TileEditorToolRail" ) ) {
        addToolBar( Qt::LeftToolBarArea, rail );
        rail->show();
    }
    // Re-dock these panels without replacing the OpenGL widget or its context.
    for ( auto *dock : { m_pAssetsDock, m_pConsoleDock, m_pInspectorDock, m_pOutlinerDock } ) {
        dock->setFloating( false );
        removeDockWidget( dock );
    }
    addDockWidget( Qt::RightDockWidgetArea, m_pAssetsDock );
    splitDockWidget( m_pAssetsDock, m_pOutlinerDock, Qt::Vertical );
    splitDockWidget( m_pOutlinerDock, m_pInspectorDock, Qt::Vertical );
    addDockWidget( Qt::BottomDockWidgetArea, m_pConsoleDock );
    for ( auto *dock : { m_pAssetsDock, m_pInspectorDock, m_pOutlinerDock } ) dock->show();
    m_pConsoleDock->hide();
    resizeDocks( { m_pAssetsDock, m_pOutlinerDock, m_pInspectorDock },
                 { 360, 360, 360 }, Qt::Horizontal );
    resizeDocks( { m_pAssetsDock, m_pOutlinerDock, m_pInspectorDock },
                 { 480, 220, 300 }, Qt::Vertical );
    m_pViewWorkspace->showFourViews();
    m_pViewWorkspace->resetViewOrder();
}

void CypherTileEditorMainWindow::syncOutlinerSelection()
{
    if ( m_pOutliner == nullptr ) return;
    const QSignalBlocker blocker( m_pOutliner );
    const auto &cells = m_pCanvas->selectedCells();
    const auto less = []( tile_map_grid_coord_t a, tile_map_grid_coord_t b ) {
        return a.y < b.y || ( a.y == b.y && a.x < b.x );
    };
    for ( int group = 0; group < m_pOutliner->topLevelItemCount(); ++group ) {
        auto *parent = m_pOutliner->topLevelItem( group );
        for ( int row = 0; row < parent->childCount(); ++row ) {
            auto *item = parent->child( row );
            const QPoint coordinate = item->data( 0, Qt::UserRole ).toPoint();
            item->setSelected( std::binary_search( cells.begin(), cells.end(),
                tile_map_grid_coord_t{ coordinate.x(), coordinate.y() }, less ) );
        }
    }
}

void CypherTileEditorMainWindow::translateSelection( int dx, int dy, bool copy )
{
    if ( !m_pCanvas->hasSelection() || m_pRenderViewport->isNavigating() ) return;
    auto cells = m_pCanvas->selectedCells();
    QString error;
    if ( !m_document.moveSelection( cells, dx, dy, copy, &error ) ) {
        appendWarning( error ); setStatus( error, true ); return;
    }
    for ( auto &cell : cells ) { cell.x += dx; cell.y += dy; }
    m_pCanvas->refreshDocument();
    m_pCanvas->setSelectedCells( cells );
    onDocumentChanged();
    setStatus( copy ? tr( "Duplicated selected tiles and doors" ) : tr( "Moved selected tiles and their walls" ) );
}

void CypherTileEditorMainWindow::deleteSelection()
{
    if ( !m_pCanvas->hasSelection() || m_pRenderViewport->isNavigating() ) return;
    QString error;
    if ( !m_document.deleteSelection( m_pCanvas->selectedCells(), &error ) ) {
        appendWarning( error ); setStatus( error, true ); return;
    }
    m_pCanvas->refreshDocument();
    m_pCanvas->clearSelection();
    onDocumentChanged();
    setStatus( tr( "Deleted selected tiles and their markers" ) );
}

void CypherTileEditorMainWindow::raiseSelection( int delta )
{
    if ( !m_pCanvas->hasSelection() || m_pRenderViewport->isNavigating() ) return;
    QString error;
    if ( !m_document.raiseSelection( m_pCanvas->selectedCells(), delta, &error ) ) {
        appendWarning( error ); setStatus( error, true ); return;
    }
    m_pCanvas->refreshDocument();
    onDocumentChanged();
    setStatus( delta > 0 ? tr( "Raised selected floors one level" ) : tr( "Lowered selected floors one level" ) );
}

void CypherTileEditorMainWindow::adjustSelectionWallHeight( int delta )
{
    if ( !m_pCanvas->hasSelection() || m_pRenderViewport->isNavigating() ) return;
    QString error;
    if ( !m_document.adjustSelectionWallHeight( m_pCanvas->selectedCells(), delta, &error ) ) {
        appendWarning( error ); setStatus( error, true ); return;
    }
    m_pCanvas->refreshDocument();
    onDocumentChanged();
    setStatus( delta > 0 ? tr( "Increased selected wall heights one level" ) : tr( "Decreased selected wall heights one level" ) );
}

void CypherTileEditorMainWindow::rotateSelection()
{
    if ( !m_pCanvas->hasSelection() || m_pRenderViewport->isNavigating() ) return;
    auto cells = m_pCanvas->selectedCells();
    const auto rect = m_pCanvas->selectionRect();
    QString error;
    if ( !m_document.rotateSelection( cells, &error ) ) {
        appendWarning( error ); setStatus( error, true ); return;
    }
    for ( auto &cell : cells ) {
        const auto old = cell;
        cell.x = rect.x + static_cast<i32>( rect.nHeight ) - 1 - ( old.y - rect.y );
        cell.y = rect.y + old.x - rect.x;
    }
    m_pCanvas->refreshDocument();
    m_pCanvas->setSelectedCells( cells );
    onDocumentChanged();
    setStatus( tr( "Rotated selected tiles clockwise" ) );
}

void CypherTileEditorMainWindow::buildConsoleDock()
{
    m_pConsoleDock = new QDockWidget( tr( "CONSOLE" ), this );
    m_pConsoleDock->setObjectName( QStringLiteral( "TileEditorConsoleDock" ) );
    m_pConsoleTabs = new QTabWidget( m_pConsoleDock );
    m_pConsoleTabs->setObjectName( QStringLiteral( "TileConsoleTabs" ) );
    m_pConsoleTabs->setDocumentMode( true );
    m_pConsoleTabs->setMinimumHeight( 150 );
    m_pConsole = new CypherTileConsole( m_pConsoleTabs );
    m_pConsole->setExecuteCallback(
        [this]( const QString &line ) { executeCommandLine( line ); } );
    m_pShellRunner = new CypherTileShellRunner( m_pConsoleTabs );
    QString shellDirectory = QDir::currentPath();
#ifdef CYPHER_TILE_EDITOR_ASSET_SOURCE_DIR
    QDir sourceRoot( QString::fromUtf8( CYPHER_TILE_EDITOR_ASSET_SOURCE_DIR ) );
    if ( sourceRoot.cdUp() ) shellDirectory = sourceRoot.absolutePath();
#endif
    m_pShellRunner->setWorkingDirectory( shellDirectory );
    const int editorTab = m_pConsoleTabs->addTab( m_pConsole, tr( "EDITOR COMMANDS" ) );
    const int shellTab = m_pConsoleTabs->addTab( m_pShellRunner, tr( "LOCAL SHELL" ) );
    m_pConsoleTabs->setTabToolTip( editorTab,
        tr( "Commands that operate on the current tile-map editor session" ) );
    m_pConsoleTabs->setTabToolTip( shellTab,
        tr( "Run one asynchronous zsh/bash command at a time in a selected directory" ) );
    m_pConsoleDock->setWidget( m_pConsoleTabs );
    addDockWidget( Qt::BottomDockWidgetArea, m_pConsoleDock );
    connect( m_pConsoleDock, &QDockWidget::visibilityChanged,
             this, [this]( bool bVisible ) {
        if ( QAction *pAction = m_actions.value(
                QStringLiteral( "view.console" ), nullptr ) ) {
            const QSignalBlocker blocker( pAction );
            pAction->setChecked( bVisible );
        }
    } );
}

void CypherTileEditorMainWindow::buildStatusBar()
{
    m_pStatusMessage = new QLabel( tr( "Ready" ), this );
    m_pStatusCursor = new QLabel( tr( "Cell —" ), this );
    m_pStatusDocument = new QLabel( this );
    m_pStatusZoom = new QLabel( tr( "24 px/cell" ), this );
    m_pStatusMessage->setProperty( "accent", true );
    statusBar()->addWidget( m_pStatusMessage, 1 );
    statusBar()->addPermanentWidget( m_pStatusCursor );
    statusBar()->addPermanentWidget( m_pStatusDocument );
    statusBar()->addPermanentWidget( m_pStatusZoom );
}

void CypherTileEditorMainWindow::applyShortcuts()
{
    for ( const tile_editor_shortcut_definition_t &definition :
          TileEditorShortcutDefinitions() ) {
        QAction *pAction = m_actions.value( definition.id, nullptr );
        if ( pAction == nullptr ) continue;
        pAction->setShortcut( m_preferences.shortcuts.value(
            definition.id,
            definition.defaultSequence ) );
        const bool bToolShortcut = definition.id.startsWith(
            QStringLiteral( "tool." ) );
        const bool bRegionShortcut = definition.id.startsWith( QStringLiteral( "edit." ) ) &&
            definition.id != QStringLiteral( "edit.undo" ) && definition.id != QStringLiteral( "edit.redo" );
        const bool bViewportShortcut =
            definition.id == QStringLiteral( "view.fit" ) ||
            definition.id == QStringLiteral( "view.maximize" );
        const bool bCameraShortcut = definition.id.startsWith(
            QStringLiteral( "camera." ) );
        pAction->setShortcutContext(
            bCameraShortcut ? Qt::WidgetWithChildrenShortcut :
            ( bToolShortcut || bViewportShortcut || bRegionShortcut )
                ? Qt::WidgetShortcut
                : Qt::WindowShortcut );
        if ( ( bToolShortcut || bViewportShortcut || bRegionShortcut ) &&
             m_pCanvas != nullptr ) {
            // Single-key authoring shortcuts must never consume typing in the
            // console, material search, inspector, or settings dialog.
            for ( auto *view : m_topViews ) view->addAction( pAction );
        }
        if ( ( bToolShortcut || bViewportShortcut || bCameraShortcut || bRegionShortcut ) && m_pRenderViewport != nullptr &&
             !m_pRenderViewport->actions().contains( pAction ) ) {
            m_pRenderViewport->addAction( pAction );
        }
        if ( bViewportShortcut || bRegionShortcut || bToolShortcut ) {
            for ( QWidget *pView : m_orthoViews ) {
                if ( pView != nullptr && !pView->actions().contains( pAction ) ) pView->addAction( pAction );
            }
        }
    }
}

void CypherTileEditorMainWindow::setTool( tile_canvas_tool_t tool )
{
    m_pCanvas->setTool( tool );
    synchronizeAuthoring();
    for ( QAction *pAction : m_pToolActions->actions() ) {
        const bool bMatch = pAction->property( "tileTool" ).toInt() ==
            static_cast<int>( tool );
        if ( bMatch ) pAction->setChecked( true );
    }
    static constexpr const char *names[]{
        "Select", "Paint Floor", "Erase", "Rectangle", "Player Spawn", "Door",
        "Pan", "Pick Material and Dimensions", "Paint Line", "Fill Region"
    };
    const int iTool = static_cast<int>( tool );
    if ( iTool >= 0 && iTool < static_cast<int>( std::size( names ) ) ) {
        setStatus( tr( "%1 tool" ).arg( tr( names[iTool] ) ) );
    }
}

void CypherTileEditorMainWindow::applyPreferences()
{
    CypherTileEditorTheme_Apply( *qApp, m_preferences );
    CypherTileEditorIcons_Refresh( this );
    for ( auto *toolbar : findChildren<QToolBar *>() )
        toolbar->setIconSize( QSize( m_preferences.uiIconSize, m_preferences.uiIconSize ) );
    if ( m_pRenderViewport != nullptr ) {
        m_pRenderViewport->setViewAppearance(
            m_preferences.perspectiveColor, m_preferences.showViewMetrics );
        m_pRenderViewport->setAxisAppearance(
            m_preferences.showViewAxes,
            m_preferences.axisXColor,
            m_preferences.axisYColor,
            m_preferences.axisZColor );
    }
    applyShortcuts();
    for ( auto *view : m_topViews ) view->setPreferences( m_preferences );
    for ( auto *view : m_orthoViews ) view->setPreferences( m_preferences );
    if ( m_pRenderViewport != nullptr ) {
        tile_camera_settings_t settings{};
        settings.moveSpeed = static_cast<float>( m_preferences.cameraMoveSpeed );
        settings.lookSensitivity = static_cast<float>( m_preferences.cameraLookSensitivity * 0.01745329252 );
        settings.verticalFovDegrees = static_cast<float>( m_preferences.cameraFieldOfView );
        settings.invertMouseY = m_preferences.cameraInvertY;
        settings.panSensitivity = static_cast<float>( m_preferences.cameraPanSensitivity );
        settings.zoomSensitivity = static_cast<float>( m_preferences.cameraZoomSensitivity );
        settings.fastMultiplier = static_cast<float>( m_preferences.cameraFastMultiplier );
        settings.slowMultiplier = static_cast<float>( m_preferences.cameraSlowMultiplier );
        settings.invertWheel = m_preferences.cameraInvertWheel;
        m_pRenderViewport->setCameraSettings( settings, m_preferences.cameraFlyMode );
        m_pRenderViewport->setCameraHintsVisible( m_preferences.showCameraHints );
        synchronizeCameraActions();
    }
    if ( m_pViewWorkspace != nullptr ) {
        m_pViewWorkspace->setActivateOnHover( m_preferences.activateViewOnHover );
        m_pViewWorkspace->setSplitterWidth( m_preferences.viewSplitterWidth );
    }
    for ( const auto &entry : { std::pair{ "view.orthoMaterials", m_preferences.showOrthoMaterials },
                               std::pair{ "view.materialLabels", m_preferences.showMaterialLabels },
                               std::pair{ "view.rulers", m_preferences.showCoordinateRulers },
                               std::pair{ "view.axes", m_preferences.showViewAxes } } ) {
        auto *action = m_actions.value( QString::fromLatin1( entry.first ) );
        const QSignalBlocker blocker( action );
        action->setChecked( entry.second );
    }
    for ( const QString &id : { QStringLiteral( "view.grid" ), QStringLiteral( "view.markers" ) } ) {
        QAction *pAction = m_actions.value( id );
        const QSignalBlocker blocker( pAction );
        pAction->setChecked( id == QStringLiteral( "view.grid" ) ? m_preferences.showGrid : m_preferences.showMarkers );
    }
    if ( auto *pGridStep = findChild<QComboBox *>( QStringLiteral( "TileGridSpacing" ) ) ) {
        const QSignalBlocker blocker( pGridStep );
        pGridStep->setCurrentIndex( pGridStep->findData( m_preferences.gridSpacingCells ) );
    }
    {
        QAction *pAction = m_actions.value( QStringLiteral( "view.adaptiveGrid" ) );
        const QSignalBlocker blocker( pAction );
        pAction->setChecked( m_preferences.adaptiveGrid );
    }
    if ( auto *pAutoGrid = findChild<QCheckBox *>( QStringLiteral( "TileAdaptiveGrid" ) ) ) {
        const QSignalBlocker blocker( pAutoGrid );
        pAutoGrid->setChecked( m_preferences.adaptiveGrid );
    }
}

void CypherTileEditorMainWindow::savePreferences()
{
    QSettings settings;
    TileEditorPreferences_Save( settings, m_preferences );
    QString error;
    if ( !TileEditorConfig_Save( TileEditorConfig_DefaultPath(), m_preferences, error ) ) appendError( error );
}

void CypherTileEditorMainWindow::showSettings( bool cameraPage )
{
    CypherTileEditorSettingsDialog dialog( m_preferences, this );
    if ( cameraPage ) {
        auto *tabs = dialog.findChild<QTabWidget *>();
        for ( int i = 0; tabs && i < tabs->count(); ++i )
            if ( tabs->tabText( i ) == tr( "Camera" ) ) tabs->setCurrentIndex( i );
    }
    dialog.setApplyCallback( [this]( const tile_editor_preferences_t &preferences ) { applyEditorSettings( preferences ); } );
    if ( dialog.exec() != QDialog::Accepted ) return;
    applyEditorSettings( dialog.preferences() );
}

void CypherTileEditorMainWindow::applyEditorSettings( const tile_editor_preferences_t &preferences )
{
    m_preferences = TileEditorPreferences_Normalize( preferences );
    QSettings settings;
    QString error;
    const bool saved = TileEditorConfig_Save( TileEditorConfig_DefaultPath(), m_preferences, error );
    if ( !saved ) appendError( error );
    TileEditorPreferences_Save( settings, m_preferences );
    applyPreferences();
    setStatus( saved ? tr( "Editor settings saved" ) : tr( "Settings applied, but editor.ini could not be saved" ), !saved );
}

bool CypherTileEditorMainWindow::importConfigurationProfile( const QString &path, QString *pErrorOut )
{
    QString error;
    auto candidate = m_preferences;
    if ( !QFileInfo( path ).isFile() ) error = tr( "Configuration profile does not exist: %1" ).arg( path );
    else if ( TileEditorConfig_Load( path, candidate, error ) ) {
        // Publish only after the selected profile has been validated and its
        // active copy saved. A failed import leaves the current UI untouched.
        if ( TileEditorConfig_Save( TileEditorConfig_DefaultPath(), candidate, error ) ) {
            m_preferences = candidate;
            QSettings settings;
            TileEditorPreferences_Save( settings, m_preferences );
            applyPreferences();
            setStatus( tr( "Imported configuration profile: %1" ).arg( QFileInfo( path ).fileName() ) );
            if ( pErrorOut ) pErrorOut->clear();
            return true;
        }
    }
    if ( pErrorOut ) *pErrorOut = error;
    setStatus( tr( "Configuration profile was not applied" ), true );
    return false;
}

bool CypherTileEditorMainWindow::exportConfigurationProfile( const QString &path, QString *pErrorOut ) const
{
    QString error;
    const bool saved = TileEditorConfig_Save( path, m_preferences, error );
    if ( pErrorOut ) *pErrorOut = error;
    return saved;
}

bool CypherTileEditorMainWindow::maybeSave()
{
    if ( !m_document.isDirty() ) return true;
    const QMessageBox::StandardButton decision = QMessageBox::warning(
        this,
        tr( "Unsaved Tile Map" ),
        tr( "The current map has unsaved changes." ),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save );
    if ( decision == QMessageBox::Cancel ) return false;
    if ( decision == QMessageBox::Save ) return saveMap();
    return true;
}

void CypherTileEditorMainWindow::showMapProperties()
{
    m_pInspectorDock->show();
    m_pInspectorDock->raise();
    m_pInspectorTabs->setCurrentWidget( m_pMapPropertiesPage );
    m_pMapProperties->setFocus();
}

bool CypherTileEditorMainWindow::applyMapDescription(
    const tile_map_document_desc_t &description, QString &error )
{
    const auto previousRevision = m_document.document()->nCurrentRevision;
    if ( !m_document.setDescription( description, &error ) ) {
        appendError( error );
        setStatus( tr( "Map properties were not changed" ), true );
        return false;
    }
    if ( m_document.document()->nCurrentRevision == previousRevision ) {
        setStatus( tr( "Map properties are unchanged" ) );
        return true;
    }
    // Refresh the existing views: rebinding the bridge would discard navigation.
    // Canvas first clamps the selection and notifies the other viewports.
    m_pCanvas->refreshDocument();
    onDocumentChanged();
    const QString message = tr( "Map updated: %1 × %2 cells, %3 units/cell, %4 units/level" )
        .arg( description.nWidth ).arg( description.nHeight )
        .arg( description.nCellSize ).arg( description.nLevelHeight );
    appendInfo( message );
    setStatus( message );
    return true;
}

void CypherTileEditorMainWindow::newMap()
{
    if ( !maybeSave() ) return;
    NewMapDialog dialog( m_preferences, this );
    if ( dialog.exec() != QDialog::Accepted ) return;
    QString error;
    if ( !m_document.newDocument( dialog.description(), &error ) ) {
        appendError( error );
        QMessageBox::critical( this, tr( "New Tile Map" ), error );
        return;
    }
    m_pRenderViewport->clearCameraBookmarks();
    synchronizeCameraActions();
    m_pMaterialBrowser->cancelPendingAssignment();
    refreshOrthoMaterials();
    for ( auto *view : m_topViews ) view->setDocumentBridge( &m_document );
    for ( auto *view : m_orthoViews ) view->setDocumentBridge( &m_document );
    if ( m_preferences.frameMapOnOpen ) m_pRenderViewport->setDocumentBridge( &m_document );
    else m_pRenderViewport->refreshDocument();
    m_pConstructionX->setMaximum( static_cast<int>( m_document.document()->nWidth ) - 1 );
    m_pConstructionY->setMaximum( static_cast<int>( m_document.document()->nHeight ) - 1 );
    synchronizeSelection();
    updateInspector( false, {} );
    updateWindowState();
    updateValidationPanel();
    schedulePreviewSync();
    appendInfo( tr( "Created a new %1 x %2 tile map." )
        .arg( m_document.document()->nWidth )
        .arg( m_document.document()->nHeight ) );
}

void CypherTileEditorMainWindow::openMap()
{
    const QString start = m_document.filePath().isEmpty()
        ? QString() : QFileInfo( m_document.filePath() ).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr( "Open Tile Map" ),
        start,
        tr( "Cypher Maps (*.cymap);;All Files (*)" ) );
    if ( !path.isEmpty() ) openFilePath( path );
}

bool CypherTileEditorMainWindow::saveMap()
{
    return m_document.filePath().isEmpty()
        ? saveMapAs()
        : saveMapTo( m_document.filePath() );
}

bool CypherTileEditorMainWindow::saveMapAs()
{
    QString path = QFileDialog::getSaveFileName(
        this,
        tr( "Save Tile Map" ),
        m_document.filePath().isEmpty()
            ? QStringLiteral( "untitled.cymap" )
            : m_document.filePath(),
        tr( "Cypher Maps (*.cymap);;All Files (*)" ) );
    if ( path.isEmpty() ) return false;
    if ( QFileInfo( path ).suffix().isEmpty() ) path += QStringLiteral( ".cymap" );
    return saveMapTo( path );
}

bool CypherTileEditorMainWindow::saveMapTo( const QString &path )
{
    QString error;
    if ( !m_document.saveToFile( path, &error ) ) {
        appendError( error );
        QMessageBox::critical( this, tr( "Save Tile Map" ), error );
        return false;
    }
    addRecentFile( m_document.filePath() );
    updateWindowState();
    appendInfo( tr( "Saved %1" ).arg(
        QDir::toNativeSeparators( m_document.filePath() ) ) );
    setStatus( tr( "Map saved" ) );
    return true;
}

void CypherTileEditorMainWindow::addRecentFile( const QString &path )
{
    const QString canonical = QFileInfo( path ).absoluteFilePath();
    m_recentFiles.removeAll( canonical );
    m_recentFiles.prepend( canonical );
    while ( m_recentFiles.size() > TILE_EDITOR_MAX_RECENT_FILES ) {
        m_recentFiles.removeLast();
    }
    QSettings settings;
    settings.setValue( QStringLiteral( "TileEditor/recentFiles" ), m_recentFiles );
    rebuildRecentMenu();
}

void CypherTileEditorMainWindow::rebuildRecentMenu()
{
    if ( m_pRecentMenu == nullptr ) return;
    m_pRecentMenu->clear();
    QStringList existing;
    for ( const QString &path : std::as_const( m_recentFiles ) ) {
        if ( !QFileInfo::exists( path ) ) continue;
        existing.push_back( path );
        QAction *pOpen = m_pRecentMenu->addAction(
            QFileInfo( path ).fileName() );
        pOpen->setToolTip( QDir::toNativeSeparators( path ) );
        connect( pOpen, &QAction::triggered, this,
                 [this, path] { openFilePath( path ); } );
    }
    m_recentFiles = existing;
    if ( existing.isEmpty() ) {
        QAction *pEmpty = m_pRecentMenu->addAction( tr( "No Recent Maps" ) );
        pEmpty->setEnabled( false );
    } else {
        m_pRecentMenu->addSeparator();
        QAction *pClear = m_pRecentMenu->addAction( tr( "Clear Recent Maps" ) );
        connect( pClear, &QAction::triggered, this, [this] {
            m_recentFiles.clear();
            QSettings settings;
            settings.remove( QStringLiteral( "TileEditor/recentFiles" ) );
            rebuildRecentMenu();
        } );
    }
}

void CypherTileEditorMainWindow::restoreWorkspace()
{
    QSettings settings;
    const QByteArray geometry = settings.value(
        QStringLiteral( "TileEditor/windowGeometryV1" ) ).toByteArray();
    const QByteArray state = settings.value(
        QStringLiteral( "TileEditor/windowStateV1" ) ).toByteArray();
    if ( geometry.isEmpty() ) {
        resize( 1440, 900 );
    } else {
        restoreGeometry( geometry );
    }
    const bool restored = !state.isEmpty() &&
        restoreState( state, TILE_EDITOR_STATE_VERSION );
    if ( !restored ) resetWorkspaceLayout();

    // Older saved workspaces could tabify any combination of Materials,
    // Objects and Properties. Qt necessarily activates a sibling tab when the
    // current one is hidden, which made a Materials visibility toggle appear
    // to redirect to another panel. Rebuild a legacy tab group once as three
    // independent right-side regions while retaining explicit visibility.
    // V2 intentionally supersedes independentMaterialsDockV1 because real
    // profiles were found with all three docks in the same tab group.
    constexpr auto migrationKey = "TileEditor/independentMaterialsDocksV2";
    const bool rightDocksTabbed =
        !tabifiedDockWidgets( m_pAssetsDock ).isEmpty() ||
        !tabifiedDockWidgets( m_pOutlinerDock ).isEmpty() ||
        !tabifiedDockWidgets( m_pInspectorDock ).isEmpty();
    if ( restored && !settings.value( migrationKey, false ).toBool() &&
         rightDocksTabbed ) {
        const bool assetsHidden = m_pAssetsDock->isHidden();
        const bool outlinerHidden = m_pOutlinerDock->isHidden();
        const bool inspectorHidden = m_pInspectorDock->isHidden();
        for ( QDockWidget *dock :
              { m_pAssetsDock, m_pOutlinerDock, m_pInspectorDock } ) {
            dock->setFloating( false );
            removeDockWidget( dock );
        }
        addDockWidget( Qt::RightDockWidgetArea, m_pAssetsDock );
        splitDockWidget( m_pAssetsDock, m_pOutlinerDock, Qt::Vertical );
        splitDockWidget( m_pOutlinerDock, m_pInspectorDock, Qt::Vertical );

        // Establish useful proportions before restoring hidden panels. A
        // legacy tab stack gives each member the same geometry, so its saved
        // heights cannot provide meaningful independent splitter sizes.
        for ( QDockWidget *dock :
              { m_pAssetsDock, m_pOutlinerDock, m_pInspectorDock } ) {
            dock->show();
        }
        resizeDocks( { m_pAssetsDock, m_pOutlinerDock, m_pInspectorDock },
                     { 360, 360, 360 }, Qt::Horizontal );
        resizeDocks( { m_pAssetsDock, m_pOutlinerDock, m_pInspectorDock },
                     { 480, 220, 300 }, Qt::Vertical );
        m_pAssetsDock->setVisible( !assetsHidden );
        m_pOutlinerDock->setVisible( !outlinerHidden );
        m_pInspectorDock->setVisible( !inspectorHidden );
    }
    settings.setValue( migrationKey, true );
    setWindowState( m_preferences.startMaximized
        ? windowState() | Qt::WindowMaximized
        : windowState() & ~Qt::WindowMaximized );
    m_pViewWorkspace->restoreLayout( settings );
}

void CypherTileEditorMainWindow::saveWorkspace()
{
    QSettings settings;
    m_pViewWorkspace->saveLayout( settings );
    settings.setValue(
        QStringLiteral( "TileEditor/windowGeometryV1" ), saveGeometry() );
    settings.setValue(
        QStringLiteral( "TileEditor/windowStateV1" ),
        saveState( TILE_EDITOR_STATE_VERSION ) );
    settings.setValue( QStringLiteral( "TileEditor/recentFiles" ), m_recentFiles );
    TileEditorPreferences_Save( settings, m_preferences );
}

void CypherTileEditorMainWindow::undo()
{
    QString error;
    if ( !m_document.undo( &error ) ) {
        appendWarning( error );
        return;
    }
    m_pCanvas->refreshDocument();
    onDocumentChanged();
    setStatus( tr( "Undo" ) );
}

void CypherTileEditorMainWindow::redo()
{
    QString error;
    if ( !m_document.redo( &error ) ) {
        appendWarning( error );
        return;
    }
    m_pCanvas->refreshDocument();
    onDocumentChanged();
    setStatus( tr( "Redo" ) );
}

void CypherTileEditorMainWindow::onDocumentChanged()
{
    m_pDocumentPreviewTimer->stop();
    updateWindowState();
    updateValidationPanel();
    updateInspector( m_pCanvas->hasSelection(), m_pCanvas->selectedCell() );
    refreshMapViews();
    schedulePreviewSync();
}

void CypherTileEditorMainWindow::updateWindowState()
{
    refreshOutliner();
    const tile_map_document_t *pDocument = m_document.document();
    m_pMapProperties->setDocument( pDocument );
    const QString name = m_document.filePath().isEmpty()
        ? tr( "Untitled.cymap" )
        : QFileInfo( m_document.filePath() ).fileName();
    setWindowTitle( tr( "%1%2 — Cypher Tile Editor" )
        .arg( m_document.isDirty() ? QStringLiteral( "*" ) : QString(), name ) );
    if ( pDocument != nullptr && m_document.isInitialized() ) {
        const QString dimensions = tr( "%1 x %2" )
            .arg( pDocument->nWidth ).arg( pDocument->nHeight );
        m_pDocumentSize->setText( tr( "%1 cells · %2 units/cell" )
            .arg( dimensions )
            .arg( pDocument->nCellSize, 0, 'f', 2 ) );
        m_pStatusDocument->setText( dimensions );
    }

    QAction *pUndo = m_actions.value( QStringLiteral( "edit.undo" ) );
    QAction *pRedo = m_actions.value( QStringLiteral( "edit.redo" ) );
    pUndo->setEnabled( m_document.canUndo() );
    pRedo->setEnabled( m_document.canRedo() );
    const QString undoLabel = m_document.canUndo()
        ? FromView( CypherTileMapDocument_UndoLabel( pDocument ) ) : QString();
    const QString redoLabel = m_document.canRedo()
        ? FromView( CypherTileMapDocument_RedoLabel( pDocument ) ) : QString();
    pUndo->setText( undoLabel.isEmpty() ? tr( "&Undo" )
                                        : tr( "&Undo %1" ).arg( undoLabel ) );
    pRedo->setText( redoLabel.isEmpty() ? tr( "&Redo" )
                                        : tr( "&Redo %1" ).arg( redoLabel ) );
}

void CypherTileEditorMainWindow::updateInspector(
    bool bHasSelection, tile_map_grid_coord_t coordinate )
{
    m_bUpdatingInspector = true;
    const auto &selected = m_pCanvas->selectedCells();
    const bool sameSelection = selected.size() == m_inspectorSelectionCells.size() &&
        std::equal( selected.begin(), selected.end(), m_inspectorSelectionCells.begin(),
            []( auto a, auto b ) { return a.x == b.x && a.y == b.y; } );
    if ( !sameSelection ) m_inspectorDirtyFields = 0u;
    m_inspectorSelectionCells = selected;
    const bool valid = bHasSelection && !selected.empty();
    const bool single = valid && selected.size() == 1;
    const auto rectangle = m_pCanvas->selectionRect();
    const tile_map_cell_t *firstFloor = nullptr;
    usize floors = 0;
    flags32_t mixed = 0u;
    bool hasStairs = false;
    for ( const auto cell : selected ) {
        const auto *value = CypherTileMapDocument_CellAt( m_document.document(), cell );
        if ( value == nullptr || !( value->flags & TILE_MAP_CELL_FLAG_FLOOR ) ) continue;
        ++floors;
        hasStairs |= value->shape != tile_map_cell_shape_t::FLAT;
        if ( firstFloor == nullptr ) { firstFloor = value; continue; }
        if ( firstFloor->nFloorLevel != value->nFloorLevel ) mixed |= TILE_MAP_SELECTION_PROPERTY_FLOOR_LEVEL;
        if ( firstFloor->nWallHeightLevels != value->nWallHeightLevels ) mixed |= TILE_MAP_SELECTION_PROPERTY_WALL_HEIGHT;
        if ( firstFloor->nMaterialSlot != value->nMaterialSlot ) mixed |= TILE_MAP_SELECTION_PROPERTY_MATERIAL;
        if ( firstFloor->shape != value->shape ) mixed |= TILE_MAP_SELECTION_PROPERTY_SHAPE;
        if ( firstFloor->nStairSteps != value->nStairSteps ) mixed |= TILE_MAP_SELECTION_PROPERTY_STAIR_STEPS;
    }
    m_pSelectionLabel->setText( valid
        ? tr( "%1 cells · %2 floors\nBounds %3, %4 · %5 × %6" )
            .arg( selected.size() ).arg( floors ).arg( rectangle.x ).arg( rectangle.y )
            .arg( rectangle.nWidth ).arg( rectangle.nHeight )
        : tr( "No selection" ) );
    for ( const char *id : { "edit.move", "edit.moveLeft", "edit.moveRight", "edit.moveUp", "edit.moveDown",
            "edit.duplicate", "edit.copyOffset", "edit.delete", "edit.rotate", "edit.raise", "edit.lower",
            "edit.wallRaise", "edit.wallLower" } )
        m_actions.value( QString::fromLatin1( id ) )->setEnabled( valid );
    m_pFloorEnabled->setEnabled( valid );
    m_pFloorEnabled->setTristate( floors > 0 && floors < selected.size() );
    m_pFloorEnabled->setCheckState( floors == 0 ? Qt::Unchecked
        : floors == selected.size() ? Qt::Checked : Qt::PartiallyChecked );
    const tile_map_cell_t fallback{};
    const auto &representative = firstFloor != nullptr ? *firstFloor : fallback;
    const auto showValue = [&]( QSpinBox *spin, int value, flags32_t field ) {
        spin->setEnabled( floors > 0 );
        // Material preparation and live refreshes may finish while a property
        // is being typed. Preserve that pending field for the same selection;
        // editingFinished will commit it through the normal single-field edit.
        if ( floors > 0 && ( m_inspectorDirtyFields & field ) &&
             ( spin->hasFocus() || spin->isAncestorOf( QApplication::focusWidget() ) ) ) return;
        m_inspectorDirtyFields &= ~field;
        spin->setValue( value );
        const bool isMixed = ( mixed & field ) != 0u;
        spin->setProperty( "mixed", isMixed );
        if ( auto *edit = spin->findChild<QLineEdit *>() ) {
            edit->setPlaceholderText( isMixed ? tr( "Mixed" ) : QString() );
            if ( isMixed ) spin->clear();
        }
        spin->setToolTip( isMixed ? tr( "Mixed values. Enter a value to apply this property to all selected floors." )
            : tr( "Changing this field updates only this property on the selected floors." ) );
    };
    showValue( m_pFloorLevel, representative.nFloorLevel, TILE_MAP_SELECTION_PROPERTY_FLOOR_LEVEL );
    showValue( m_pWallHeight, representative.nWallHeightLevels, TILE_MAP_SELECTION_PROPERTY_WALL_HEIGHT );
    showValue( m_pMaterialSlot, representative.nMaterialSlot, TILE_MAP_SELECTION_PROPERTY_MATERIAL );
    showValue( m_pStairSteps, representative.nStairSteps, TILE_MAP_SELECTION_PROPERTY_STAIR_STEPS );
    m_pCellShape->setEnabled( floors > 0 );
    m_pCellShape->setCurrentIndex( mixed & TILE_MAP_SELECTION_PROPERTY_SHAPE ? -1
        : m_pCellShape->findData( static_cast<int>( representative.shape ) ) );
    m_pStairSteps->setEnabled( hasStairs );
    const auto *spawn = m_document.isInitialized()
        ? CypherTileMapDocument_PlayerSpawn( m_document.document() ) : nullptr;
    const bool spawnSelected = single && spawn != nullptr &&
        spawn->cell.x == coordinate.x && spawn->cell.y == coordinate.y;
    m_pSpawnYaw->setEnabled( spawnSelected );
    m_pSpawnYaw->setValue( spawnSelected ? spawn->yawDegrees : 0.0 );
    m_bUpdatingInspector = false;
}

void CypherTileEditorMainWindow::applyInspectorCellEdit( flags32_t fields )
{
    if ( m_bUpdatingInspector || !m_pCanvas->hasSelection() ) return;
    m_inspectorDirtyFields &= ~fields;
    tile_map_selection_patch_t patch{};
    patch.fields = fields;
    patch.bFloorEnabled = m_pFloorEnabled->checkState() == Qt::Checked;
    patch.nFloorLevel = static_cast<i16>( m_pFloorLevel->value() );
    patch.nWallHeightLevels = static_cast<u16>( m_pWallHeight->value() );
    patch.nMaterialSlot = static_cast<u16>( m_pMaterialSlot->value() );
    patch.shape = static_cast<tile_map_cell_shape_t>( m_pCellShape->currentData().toInt() );
    patch.nStairSteps = static_cast<u16>( m_pStairSteps->value() );
    QString error;
    if ( !m_document.applySelectionProperties( m_pCanvas->selectedCells(), patch, &error ) ) {
        appendError( error );
        updateInspector( m_pCanvas->hasSelection(), m_pCanvas->selectedCell() );
        return;
    }
    m_pCanvas->refreshDocument();
    onDocumentChanged();
}

void CypherTileEditorMainWindow::applyInspectorSpawnEdit()
{
    if ( m_bUpdatingInspector || !m_pCanvas->hasSelection() ||
         m_pCanvas->selectionRect().nWidth != 1 || m_pCanvas->selectionRect().nHeight != 1 ) return;
    const tile_map_grid_coord_t coordinate = m_pCanvas->selectedCell();
    const tile_map_marker_t *pSpawn = CypherTileMapDocument_PlayerSpawn(
        m_document.document() );
    if ( pSpawn == nullptr || pSpawn->cell.x != coordinate.x ||
         pSpawn->cell.y != coordinate.y ) return;

    QString error;
    if ( !m_document.beginEdit( tr( "Edit player spawn" ), &error ) ) {
        appendError( error );
        return;
    }
    bool bSucceeded = m_document.placePlayerSpawn(
        coordinate,
        static_cast<float>( m_pSpawnYaw->value() ),
        &error );
    if ( bSucceeded ) bSucceeded = m_document.commitEdit( &error );
    if ( !bSucceeded ) {
        m_document.cancelEdit();
        appendError( error );
        updateInspector( true, coordinate );
        return;
    }
    m_pCanvas->refreshDocument();
    onDocumentChanged();
}

void CypherTileEditorMainWindow::updateValidationPanel()
{
    if ( m_pValidationList == nullptr ) return;
    // Keep a located diagnostic selected when an unrelated edit refreshes the
    // report. The data roles are also the navigation contract with the views.
    QString selectedCode;
    int selectedX = 0;
    int selectedY = 0;
    if ( const QListWidgetItem *pSelected = m_pValidationList->currentItem() ) {
        selectedCode = pSelected->data( Qt::UserRole + 4 ).toString();
        selectedX = pSelected->data( Qt::UserRole + 1 ).toInt();
        selectedY = pSelected->data( Qt::UserRole + 2 ).toInt();
    }
    m_pValidationList->clear();
    m_bValidationCompleted = false;
    m_nValidationErrors = 0;
    m_nValidationWarnings = 0;

    const QColor errorColor( 239, 112, 105 );
    const QColor warningColor( 229, 176, 87 );
    const QColor successColor( 107, 205, 124 );
    auto showSummary = [this]( const QString &message, const QColor &color ) {
        m_pValidationSummary->setText( message );
        m_pValidationSummary->setStyleSheet(
            QStringLiteral( "color: %1;" ).arg( color.name() ) );
        const int nIssues = m_nValidationErrors + m_nValidationWarnings;
        m_pInspectorTabs->setTabText( m_pInspectorTabs->indexOf( m_pValidationList->parentWidget() ), nIssues == 0
            ? tr( "Validation" )
            : tr( "Validation (%1)" ).arg( nIssues ) );
    };
    auto showFailure = [this, &showSummary, &errorColor]( const QString &message ) {
        auto *pItem = new QListWidgetItem( message, m_pValidationList );
        pItem->setForeground( errorColor );
        pItem->setFlags( Qt::ItemIsEnabled );
        showSummary( tr( "Live checks unavailable" ), errorColor );
    };
    if ( !m_document.isInitialized() ) {
        showFailure( tr( "No initialized document to check." ) );
        return;
    }

    tile_map_validation_report_t report{};
    const tile_map_document_status_t initialized =
        CypherTileMapValidationReport_Init( &report, Allocator_GetSystem() );
    if ( initialized != tile_map_document_status_t::OK ) {
        showFailure( tr( "Validation report allocation failed: %1" ).arg(
            QString::fromLatin1( CypherTileMapDocument_StatusName( initialized ) ) ) );
        return;
    }
    const tile_map_document_status_t validated =
        CypherTileMapDocument_Validate( m_document.document(), &report );
    if ( validated != tile_map_document_status_t::OK ) {
        showFailure( tr( "Validation could not run: %1" ).arg(
            QString::fromLatin1(
                CypherTileMapDocument_StatusName( validated ) ) ) );
    } else {
        m_bValidationCompleted = true;
        for ( usize i = 0u; i < Vector_Count( &report.diagnostics ); ++i ) {
            const tile_map_validation_diagnostic_t *pDiagnostic =
                Vector_At( &report.diagnostics, i );
            if ( pDiagnostic == nullptr ) continue;
            const bool bError = GeometryDiagnosticIsBlocking( pDiagnostic->code );
            const bool bHasCell = ValidationDiagnosticHasCell( pDiagnostic->code );
            if ( bError ) ++m_nValidationErrors;
            else ++m_nValidationWarnings;
            const QString code = QString::fromLatin1(
                CypherTileMapValidation_CodeName( pDiagnostic->code ) );
            const QString message = ValidationMessage( *pDiagnostic );
            auto *pItem = new QListWidgetItem(
                tr( "%1: %2" ).arg( bError ? tr( "Error" ) : tr( "Warning" ), message ),
                m_pValidationList );
            pItem->setForeground( bError ? errorColor : warningColor );
            pItem->setData( Qt::UserRole, bHasCell );
            pItem->setData( Qt::UserRole + 1, pDiagnostic->cell.x );
            pItem->setData( Qt::UserRole + 2, pDiagnostic->cell.y );
            pItem->setData( Qt::UserRole + 3, bError ? 2 : 1 );
            pItem->setData( Qt::UserRole + 4, code );
            pItem->setToolTip( tr( "%1\n%2%3" ).arg(
                message,
                bError ? tr( "Blocks geometry generation." )
                       : tr( "Fix before gameplay; geometry can still render." ),
                bHasCell ? tr( "\nDouble-click to locate cell (%1, %2)." )
                    .arg( pDiagnostic->cell.x ).arg( pDiagnostic->cell.y )
                         : QString() ) );
            if ( code == selectedCode && pDiagnostic->cell.x == selectedX &&
                 pDiagnostic->cell.y == selectedY ) {
                m_pValidationList->setCurrentItem( pItem );
            }
        }
        if ( m_nValidationErrors == 0 && m_nValidationWarnings == 0 ) {
            auto *pItem = new QListWidgetItem(
                tr( "Document structure, spawn, and door checks passed." ),
                m_pValidationList );
            pItem->setForeground( successColor );
            pItem->setFlags( Qt::ItemIsEnabled );
            showSummary( tr( "Live checks: no issues" ), successColor );
        } else {
            showSummary( tr( "Live checks: %1 error(s), %2 warning(s)" )
                .arg( m_nValidationErrors ).arg( m_nValidationWarnings ),
                m_nValidationErrors != 0 ? errorColor : warningColor );
        }
    }
    CypherTileMapValidationReport_Shutdown( &report );
}

bool CypherTileEditorMainWindow::validateMap( bool bAnnounceSuccess )
{
    // Console and live panel must describe one result. A second independent
    // validation could disagree with the panel if report allocation failed.
    updateValidationPanel();
    m_pInspectorTabs->setCurrentWidget( m_pValidationList->parentWidget() );
    if ( auto *pDock = findChild<QDockWidget *>(
             QStringLiteral( "TileEditorInspectorDock" ) ) ) {
        pDock->show();
        pDock->raise();
    }
    if ( !m_bValidationCompleted ) {
        appendError( m_pValidationList->count() > 0
            ? m_pValidationList->item( 0 )->text()
            : tr( "Validation could not run." ) );
        setStatus( tr( "Map checks unavailable" ), true );
        return false;
    }
    const bool bValid = m_nValidationErrors == 0 && m_nValidationWarnings == 0;
    if ( bValid ) {
        if ( bAnnounceSuccess ) {
            appendInfo( tr( "Document structure, spawn, and door checks passed." ) );
        }
        setStatus( tr( "Map checks passed" ) );
    } else {
        appendInfo( tr( "Map checks: %1 error(s), %2 warning(s)." )
            .arg( m_nValidationErrors ).arg( m_nValidationWarnings ) );
        for ( int i = 0; i < m_pValidationList->count(); ++i ) {
            const QListWidgetItem *pItem = m_pValidationList->item( i );
            if ( pItem->data( Qt::UserRole + 3 ).toInt() == 2 ) {
                appendError( pItem->text() );
            } else {
                appendWarning( pItem->text() );
            }
        }
        setStatus( m_pValidationSummary->text(), m_nValidationErrors != 0 );
    }
    return bValid;
}

void CypherTileEditorMainWindow::buildMap()
{
    if ( !m_document.isInitialized() ) {
        appendError( tr( "No initialized map to build." ) );
        return;
    }

    updateValidationPanel();
    tile_map_validation_report_t validation{};
    const tile_map_document_status_t validationInitialized =
        CypherTileMapValidationReport_Init(
            &validation,
            Allocator_GetSystem() );
    if ( validationInitialized != tile_map_document_status_t::OK ) {
        appendError( tr( "Could not allocate a geometry validation report: %1" )
            .arg( QString::fromLatin1(
                CypherTileMapDocument_StatusName( validationInitialized ) ) ) );
        return;
    }
    const tile_map_document_status_t validated =
        CypherTileMapDocument_Validate(
            m_document.document(),
            &validation );
    bool bGeometryBlocked = validated != tile_map_document_status_t::OK;
    if ( validated != tile_map_document_status_t::OK ) {
        appendError( tr( "Geometry validation could not run: %1" ).arg(
            QString::fromLatin1(
                CypherTileMapDocument_StatusName( validated ) ) ) );
    } else {
        for ( usize i = 0u; i < Vector_Count( &validation.diagnostics ); ++i ) {
            const tile_map_validation_diagnostic_t *pDiagnostic =
                Vector_At( &validation.diagnostics, i );
            if ( pDiagnostic == nullptr ) continue;
            appendWarning( ValidationMessage( *pDiagnostic ) );
            bGeometryBlocked = bGeometryBlocked ||
                GeometryDiagnosticIsBlocking( pDiagnostic->code );
        }
    }
    CypherTileMapValidationReport_Shutdown( &validation );
    if ( bGeometryBlocked ) {
        appendError( tr(
            "Geometry build stopped because the map has structural errors." ) );
        setStatus( tr( "Geometry build blocked by structural validation" ), true );
        return;
    }

    tile_map_geometry_t geometry{};
    const tile_map_document_status_t initialized = CypherTileMapGeometry_Init(
        &geometry,
        Allocator_GetSystem() );
    if ( initialized != tile_map_document_status_t::OK ) {
        appendError( tr( "Geometry initialization failed: %1" ).arg(
            QString::fromLatin1(
                CypherTileMapDocument_StatusName( initialized ) ) ) );
        return;
    }

    const tile_map_geometry_desc_t description{};
    const tile_map_document_status_t built = CypherTileMapGeometry_Build(
        m_document.document(), description, &geometry );
    if ( built != tile_map_document_status_t::OK ) {
        const QString error = tr( "Geometry build failed: %1" ).arg(
            QString::fromLatin1( CypherTileMapDocument_StatusName( built ) ) );
        appendError( error );
        setStatus( error, true );
        CypherTileMapGeometry_Shutdown( &geometry );
        return;
    }

    const usize nFloorBoxes = CypherTileMapGeometry_CountKind(
        &geometry, tile_map_geometry_box_kind_t::FLOOR );
    const usize nWallBoxes = CypherTileMapGeometry_CountKind(
        &geometry, tile_map_geometry_box_kind_t::WALL );
    const usize nDoorBoxes = CypherTileMapGeometry_CountKind(
        &geometry, tile_map_geometry_box_kind_t::DOOR );
    const usize nStairBoxes = CypherTileMapGeometry_CountKind( &geometry, tile_map_geometry_box_kind_t::STAIR );
    const usize nTotalBoxes = Vector_Count( &geometry.boxes );
    QString summary = tr(
        "Geometry build succeeded: %1 floor boxes, %2 wall boxes, %3 doors, %5 stair steps (%4 total)." )
        .arg( nFloorBoxes ).arg( nWallBoxes ).arg( nDoorBoxes ).arg( nTotalBoxes ).arg( nStairBoxes );
    if ( geometry.bHasBounds ) {
        summary += tr( " Bounds: [%1, %2, %3] to [%4, %5, %6]." )
            .arg( geometry.boundsMinX, 0, 'f', 2 )
            .arg( geometry.boundsMinY, 0, 'f', 2 )
            .arg( geometry.boundsMinZ, 0, 'f', 2 )
            .arg( geometry.boundsMaxX, 0, 'f', 2 )
            .arg( geometry.boundsMaxY, 0, 'f', 2 )
            .arg( geometry.boundsMaxZ, 0, 'f', 2 );
    }
    appendInfo( summary );
    setStatus( tr( "Built %1 floor + %2 wall + %3 door boxes" )
        .arg( nFloorBoxes ).arg( nWallBoxes ).arg( nDoorBoxes ) );
    CypherTileMapGeometry_Shutdown( &geometry );
}

void CypherTileEditorMainWindow::previewMap()
{
    if ( !m_document.isInitialized() ) {
        appendError( tr( "No initialized map to preview." ) );
        return;
    }
    if ( m_pPreviewProcess != nullptr &&
         m_pPreviewProcess->state() != QProcess::NotRunning ) {
        if ( writePreviewSnapshot() ) {
            setStatus( tr( "Runtime preview synchronized" ) );
        }
        return;
    }
    if ( !writePreviewSnapshot() ) return;

#if defined( Q_OS_WIN )
    const QString executableName = QStringLiteral( "cypher_tile_map_preview.exe" );
#else
    const QString executableName = QStringLiteral( "cypher_tile_map_preview" );
#endif
    const QDir applicationDirectory( QCoreApplication::applicationDirPath() );
    const QStringList candidates{
        applicationDirectory.filePath( executableName ),
        // A macOS application executable lives in
        // bin/CypherTileEditor.app/Contents/MacOS; the preview lives in bin.
        applicationDirectory.absoluteFilePath(
            QStringLiteral( "../../../" ) + executableName )
    };
    QString executable;
    for ( const QString &candidate : candidates ) {
        const QFileInfo file( candidate );
        if ( file.exists() && file.isExecutable() ) {
            executable = file.absoluteFilePath();
            break;
        }
    }
    if ( executable.isEmpty() ) {
        appendError( tr(
            "Could not find cypher_tile_map_preview beside the editor. "
            "Build the cypher_tile_map_preview target first." ) );
        setStatus( tr( "Runtime preview executable not found" ), true );
        return;
    }

    QStringList arguments{
        QStringLiteral( "--map" ),
        m_previewMapPath,
        QStringLiteral( "--camera-speed" ), QString::number( m_preferences.cameraMoveSpeed ),
        QStringLiteral( "--camera-sensitivity" ), QString::number( m_preferences.cameraLookSensitivity ),
        QStringLiteral( "--camera-fov" ), QString::number( m_preferences.cameraFieldOfView )
    };
    if ( m_preferences.cameraInvertY ) arguments.append( QStringLiteral( "--invert-y" ) );
    if ( !m_preferences.cameraFlyMode ) arguments.append( QStringLiteral( "--orbit-camera" ) );
    arguments.append( { QStringLiteral( "--camera-pan-sensitivity" ), QString::number( m_preferences.cameraPanSensitivity ),
        QStringLiteral( "--camera-zoom-sensitivity" ), QString::number( m_preferences.cameraZoomSensitivity ),
        QStringLiteral( "--camera-fast-multiplier" ), QString::number( m_preferences.cameraFastMultiplier ),
        QStringLiteral( "--camera-slow-multiplier" ), QString::number( m_preferences.cameraSlowMultiplier ) } );
    if ( m_preferences.cameraInvertWheel ) arguments.append( QStringLiteral( "--camera-invert-wheel" ) );
    const QString relativeShader =
        QStringLiteral( "resources/render_smoke/cube.cyshader_c" );
    const QStringList shaderCandidates{
        applicationDirectory.filePath( relativeShader ),
        applicationDirectory.absoluteFilePath(
            QStringLiteral( "../../../" ) + relativeShader ),
        applicationDirectory.absoluteFilePath(
            QStringLiteral( "../Resources/render_smoke/cube.cyshader_c" ) )
    };
    QString shaderPath;
    for ( const QString &candidate : shaderCandidates ) {
        const QFileInfo shaderFile( candidate );
        if ( shaderFile.isFile() && shaderFile.isReadable() ) {
            shaderPath = shaderFile.absoluteFilePath();
            break;
        }
    }
    if ( shaderPath.isEmpty() ) {
        appendError( tr(
            "Could not find the cooked preview shader beside the editor." ) );
        setStatus( tr( "Runtime preview shader not found" ), true );
        return;
    }
    arguments.append( QStringLiteral( "--asset-root" ) );
    arguments.append( m_pMaterialBrowser->cookedRoot() );
    arguments.append( QStringLiteral( "--shader" ) );
    arguments.append( shaderPath );

    auto *pProcess = new QProcess( this );
    m_pPreviewProcess = pProcess;
    pProcess->setProgram( executable );
    pProcess->setArguments( arguments );
    pProcess->setWorkingDirectory( QFileInfo( m_previewMapPath ).absolutePath() );
    pProcess->setProcessChannelMode( QProcess::MergedChannels );
    connect( pProcess, &QProcess::readyReadStandardOutput, this,
             [this, pProcess] {
        const QString output = QString::fromUtf8(
            pProcess->readAllStandardOutput() ).trimmed();
        if ( !output.isEmpty() ) appendInfo( output );
    } );
    connect( pProcess, &QProcess::errorOccurred, this,
             [this, pProcess]( QProcess::ProcessError ) {
        appendError( tr( "Live preview process: %1" )
            .arg( pProcess->errorString() ) );
    } );
    connect( pProcess,
             qOverload<int, QProcess::ExitStatus>( &QProcess::finished ),
             this,
             [this, pProcess]( int nExitCode, QProcess::ExitStatus status ) {
        const QString remaining = QString::fromUtf8(
            pProcess->readAllStandardOutput() ).trimmed();
        if ( !remaining.isEmpty() ) appendInfo( remaining );
        if ( m_pPreviewProcess == pProcess ) m_pPreviewProcess = nullptr;
        m_actions.value( QStringLiteral( "map.previewStop" ) )->setEnabled( false );
        pProcess->deleteLater();
        if ( status == QProcess::CrashExit || nExitCode != 0 ) {
            appendWarning( tr( "Runtime preview exited with code %1." )
                .arg( nExitCode ) );
            setStatus( tr( "Runtime preview stopped unexpectedly" ), true );
        } else {
            setStatus( tr( "Runtime preview stopped" ) );
        }
    } );
    pProcess->start();
    if ( !pProcess->waitForStarted( 3000 ) ) {
        appendError( tr( "Could not start the CypherRender map preview: %1" )
            .arg( pProcess->errorString() ) );
        setStatus( tr( "Runtime preview failed to start" ), true );
        m_pPreviewProcess = nullptr;
        pProcess->deleteLater();
        return;
    }
    appendInfo( tr(
        "Started CypherRender runtime preview. Committed edits now synchronize "
        "without saving the authored map." ) );
    m_actions.value( QStringLiteral( "map.previewStop" ) )->setEnabled( true );
    setStatus( tr( "Runtime preview running · edits synchronize automatically" ) );
}

void CypherTileEditorMainWindow::stopPreview()
{
    if ( m_pPreviewSyncTimer != nullptr ) m_pPreviewSyncTimer->stop();
    QProcess *pProcess = m_pPreviewProcess;
    if ( pProcess == nullptr ) return;
    m_pPreviewProcess = nullptr;
    disconnect( pProcess, nullptr, this, nullptr );
    if ( pProcess->state() != QProcess::NotRunning ) {
        pProcess->terminate();
        if ( !pProcess->waitForFinished( 1200 ) ) {
            pProcess->kill();
            (void)pProcess->waitForFinished( 1200 );
        }
    }
    pProcess->deleteLater();
    if ( QAction *pStop = m_actions.value(
            QStringLiteral( "map.previewStop" ), nullptr ) ) {
        pStop->setEnabled( false );
    }
}

void CypherTileEditorMainWindow::schedulePreviewSync()
{
    if ( m_pPreviewSyncTimer == nullptr || m_pPreviewProcess == nullptr ||
         m_pPreviewProcess->state() == QProcess::NotRunning ) return;
    m_pPreviewSyncTimer->start();
}

bool CypherTileEditorMainWindow::writePreviewSnapshot()
{
    if ( m_previewMapPath.isEmpty() ) {
        QString cacheRoot = QStandardPaths::writableLocation(
            QStandardPaths::CacheLocation );
        if ( cacheRoot.isEmpty() ) {
            cacheRoot = QDir::tempPath() + QStringLiteral( "/CypherTileEditor" );
        }
        QDir directory( cacheRoot );
        if ( !directory.mkpath( QStringLiteral( "." ) ) ) {
            appendError( tr( "Could not create the live-preview cache directory." ) );
            return false;
        }
        m_previewMapPath = directory.filePath(
            QStringLiteral( "live-preview-%1.cymap" )
                .arg( QCoreApplication::applicationPid() ) );
    }

    QString error;
    if ( !m_document.writeSnapshotToFile( m_previewMapPath, &error ) ) {
        appendError( tr( "Could not synchronize the runtime preview: %1" )
            .arg( error ) );
        setStatus( tr( "Live preview synchronization failed" ), true );
        return false;
    }
    return true;
}

void CypherTileEditorMainWindow::registerCommands()
{
    auto add = [this](
        const QString &name,
        const QString &help,
        const QString &usage,
        std::function<void( const QStringList & )> execute ) {
        m_commands.insert( name, { help, usage, std::move( execute ) } );
    };

    add( QStringLiteral( "help" ),
         tr( "List commands or describe one command." ),
         QStringLiteral( "help [command]" ),
         [this]( const QStringList &arguments ) {
        if ( arguments.size() > 1 ) {
            const QString name = arguments[1].toLower();
            const auto iCommand = m_commands.constFind( name );
            if ( iCommand == m_commands.cend() ) {
                appendError( tr( "Unknown command: %1" ).arg( name ) );
                return;
            }
            appendInfo( QStringLiteral( "%1 — %2\nusage: %3" )
                .arg( name, iCommand->help, iCommand->usage ) );
            return;
        }
        appendInfo( tr( "Available commands:" ) );
        for ( auto iCommand = m_commands.cbegin();
              iCommand != m_commands.cend(); ++iCommand ) {
            appendInfo( QStringLiteral( "  %1 — %2" )
                .arg( iCommand.key(), iCommand->help ) );
        }
    } );
    add( QStringLiteral( "clear" ),
         tr( "Clear console output." ),
         QStringLiteral( "clear" ),
         [this]( const QStringList & ) { m_pConsole->clear(); } );
    add( QStringLiteral( "shell" ),
         tr( "Open the asynchronous local shell runner." ),
         QStringLiteral( "shell" ),
         [this]( const QStringList &arguments ) {
        if ( arguments.size() != 1 ) {
            appendError( tr( "usage: shell" ) );
            return;
        }
        m_actions.value( QStringLiteral( "view.shell" ) )->trigger();
    } );
    for ( const bool bImport : { true, false } ) {
        const QString command = bImport ? QStringLiteral( "config_import" ) : QStringLiteral( "config_export" );
        add( command, bImport ? tr( "Load and activate a validated editor configuration profile." )
                             : tr( "Save current editor preferences as a reusable profile." ),
             command + QStringLiteral( " <path.ini>" ), [this, bImport, command]( const QStringList &arguments ) {
            if ( arguments.size() != 2 ) { appendError( tr( "usage: %1 <path.ini>" ).arg( command ) ); return; }
            QString error;
            const bool ok = bImport ? importConfigurationProfile( arguments[1], &error )
                                   : exportConfigurationProfile( arguments[1], &error );
            if ( !ok ) appendError( error );
            else appendInfo( tr( "%1: %2" ).arg( command, QDir::toNativeSeparators( arguments[1] ) ) );
        } );
    }
    add( QStringLiteral( "new" ),
         tr( "Create a new tile map." ),
         QStringLiteral( "new" ),
         [this]( const QStringList & ) { newMap(); } );
    add( QStringLiteral( "map_properties" ),
         tr( "Inspect or change the live map dimensions and world metrics as one undo step." ),
         QStringLiteral( "map_properties [width height cell_size level_height]" ),
         [this]( const QStringList &arguments ) {
        if ( arguments.size() == 1 ) {
            showMapProperties();
            const auto *doc = m_document.document();
            appendInfo( tr( "Map: %1 × %2 cells; cell_size %3; level_height %4" )
                .arg( doc->nWidth ).arg( doc->nHeight ).arg( doc->nCellSize ).arg( doc->nLevelHeight ) );
            return;
        }
        if ( arguments.size() != 5 ) {
            appendError( tr( "usage: map_properties [width height cell_size level_height]" ) );
            return;
        }
        bool widthOk = false, heightOk = false, cellOk = false, levelOk = false;
        tile_map_document_desc_t description{};
        description.nWidth = arguments[1].toUInt( &widthOk );
        description.nHeight = arguments[2].toUInt( &heightOk );
        description.nCellSize = arguments[3].toFloat( &cellOk );
        description.nLevelHeight = arguments[4].toFloat( &levelOk );
        if ( !widthOk || !heightOk || !cellOk || !levelOk ||
             !std::isfinite( description.nCellSize ) || !std::isfinite( description.nLevelHeight ) ) {
            appendError( tr( "Map dimensions must be positive integers and world metrics must be finite numbers." ) );
            return;
        }
        QString error;
        (void)applyMapDescription( description, error );
    } );
    add( QStringLiteral( "save" ),
         tr( "Save to the current path or an optional path." ),
         QStringLiteral( "save [path]" ),
         [this]( const QStringList &arguments ) {
        if ( arguments.size() > 2 ) {
            appendError( tr( "usage: save [path]" ) );
        } else if ( arguments.size() == 2 ) {
            saveMapTo( arguments[1] );
        } else {
            saveMap();
        }
    } );
    add( QStringLiteral( "validate" ),
         tr( "Run structural and gameplay validation." ),
         QStringLiteral( "validate" ),
         [this]( const QStringList & ) { validateMap(); } );
    add( QStringLiteral( "build" ),
         tr( "Validate and build floor/wall box geometry." ),
         QStringLiteral( "build" ),
         [this]( const QStringList & ) { buildMap(); } );
    add( QStringLiteral( "preview" ),
         tr( "Launch or synchronize the standalone runtime preview." ),
         QStringLiteral( "preview" ),
         [this]( const QStringList & ) { previewMap(); } );
    add( QStringLiteral( "preview_stop" ),
         tr( "Stop the standalone CypherRender runtime preview." ),
         QStringLiteral( "preview_stop" ),
         [this]( const QStringList & ) {
        stopPreview();
        setStatus( tr( "Runtime preview stopped" ) );
    } );
    add( QStringLiteral( "layout2d" ),
         tr( "Show the authored 2D tile layout." ),
         QStringLiteral( "layout2d" ),
         [this]( const QStringList & ) {
        m_pViewWorkspace->focusView( tile_editor_view_t::TOP );
    } );
    add( QStringLiteral( "map3d" ),
         tr( "Show the current map in the embedded CypherRender 3D view." ),
         QStringLiteral( "map3d" ),
         [this]( const QStringList & ) {
        m_pViewWorkspace->focusView( tile_editor_view_t::PERSPECTIVE );
    } );
    add( QStringLiteral( "camera_spawn" ), tr( "Move the 3D camera to the player spawn." ), QStringLiteral( "camera_spawn" ),
         [this]( const QStringList & ) { m_actions.value( QStringLiteral( "view.spawnCamera" ) )->trigger(); } );
    add( QStringLiteral( "frame_selection" ), tr( "Frame the selected region in 3D." ), QStringLiteral( "frame_selection" ),
         [this]( const QStringList & ) { m_actions.value( QStringLiteral( "view.frameSelection" ) )->trigger(); } );
    add( QStringLiteral( "camera_view" ),
         tr( "Align the 3D camera to a deterministic inspection direction." ),
         QStringLiteral( "camera_view <perspective|top|bottom|front|back|left|right|level>" ),
         [this]( const QStringList &arguments ) {
        if ( arguments.size() != 2 ) {
            appendError( tr( "usage: camera_view <perspective|top|bottom|front|back|left|right|level>" ) );
            return;
        }
        const QString value = arguments[1].toLower();
        QString actionId;
        if ( value == QStringLiteral( "perspective" ) ) actionId = QStringLiteral( "camera.viewPerspective" );
        else if ( value == QStringLiteral( "top" ) ) actionId = QStringLiteral( "camera.viewTop" );
        else if ( value == QStringLiteral( "bottom" ) ) actionId = QStringLiteral( "camera.viewBottom" );
        else if ( value == QStringLiteral( "front" ) ) actionId = QStringLiteral( "camera.viewFront" );
        else if ( value == QStringLiteral( "back" ) ) actionId = QStringLiteral( "camera.viewBack" );
        else if ( value == QStringLiteral( "left" ) ) actionId = QStringLiteral( "camera.viewLeft" );
        else if ( value == QStringLiteral( "right" ) ) actionId = QStringLiteral( "camera.viewRight" );
        else if ( value == QStringLiteral( "level" ) ) actionId = QStringLiteral( "camera.level" );
        if ( actionId.isEmpty() ) {
            appendError( tr( "Unknown camera view '%1'." ).arg( arguments[1] ) );
            return;
        }
        m_actions.value( actionId )->trigger();
        appendInfo( tr( "Camera view: %1" ).arg( value ) );
    } );
    add( QStringLiteral( "camera_level" ),
         tr( "Move the 3D camera by one authored map level." ),
         QStringLiteral( "camera_level <up|down>" ),
         [this]( const QStringList &arguments ) {
        if ( arguments.size() != 2 ||
             ( arguments[1].compare( QStringLiteral( "up" ), Qt::CaseInsensitive ) != 0 &&
               arguments[1].compare( QStringLiteral( "down" ), Qt::CaseInsensitive ) != 0 ) ) {
            appendError( tr( "usage: camera_level <up|down>" ) );
            return;
        }
        m_actions.value( arguments[1].compare( QStringLiteral( "up" ), Qt::CaseInsensitive ) == 0
            ? QStringLiteral( "camera.levelUp" )
            : QStringLiteral( "camera.levelDown" ) )->trigger();
    } );
    add( QStringLiteral( "camera_bookmark" ),
         tr( "Store, recall, or clear session camera views." ),
         QStringLiteral( "camera_bookmark <store|recall> <1..4> | camera_bookmark clear" ),
         [this]( const QStringList &arguments ) {
        const QString operation = arguments.size() > 1
            ? arguments[1].toLower() : QString{};
        if ( operation == QStringLiteral( "clear" ) && arguments.size() == 2 ) {
            m_pRenderViewport->clearCameraBookmarks();
            synchronizeCameraActions();
            appendInfo( tr( "Cleared camera bookmarks for this map session." ) );
            return;
        }
        bool slotValid = false;
        const int slot = arguments.size() == 3
            ? arguments[2].toInt( &slotValid ) : 0;
        if ( !slotValid || slot < 1 || slot > 4 ||
             ( operation != QStringLiteral( "store" ) &&
               operation != QStringLiteral( "recall" ) ) ) {
            appendError( tr( "usage: camera_bookmark <store|recall> <1..4> | camera_bookmark clear" ) );
            return;
        }
        QAction *action = m_actions.value(
            QStringLiteral( "camera.%1%2" ).arg( operation ).arg( slot ), nullptr );
        if ( action == nullptr || !action->isEnabled() ) {
            appendError( tr( "Camera bookmark %1 has not been stored." ).arg( slot ) );
            return;
        }
        action->trigger();
    } );
    add( QStringLiteral( "camera_auto_orbit" ),
         tr( "Inspect or change automatic camera orbiting." ),
         QStringLiteral( "camera_auto_orbit [on|off|toggle]" ),
         [this]( const QStringList &arguments ) {
        if ( arguments.size() == 1 ) {
            appendInfo( m_pRenderViewport->isAutoOrbiting()
                ? tr( "Camera auto orbit is on." )
                : tr( "Camera auto orbit is off." ) );
            return;
        }
        if ( arguments.size() != 2 ) {
            appendError( tr( "usage: camera_auto_orbit [on|off|toggle]" ) );
            return;
        }
        const QString value = arguments[1].toLower();
        bool enabled = m_pRenderViewport->isAutoOrbiting();
        if ( value == QStringLiteral( "on" ) ) enabled = true;
        else if ( value == QStringLiteral( "off" ) ) enabled = false;
        else if ( value == QStringLiteral( "toggle" ) ) enabled = !enabled;
        else {
            appendError( tr( "usage: camera_auto_orbit [on|off|toggle]" ) );
            return;
        }
        m_pRenderViewport->setAutoOrbitEnabled( enabled );
    } );
    add( QStringLiteral( "shape" ), tr( "Choose flat or oriented stairs for painting." ),
         QStringLiteral( "shape [flat|stairs_north|stairs_east|stairs_south|stairs_west] [steps]" ),
         [this]( const QStringList &arguments ) {
        if ( arguments.size() == 1 ) {
            appendInfo( tr( "Paint shape: %1, %2 steps" ).arg( QString::fromLatin1( CypherTileMapCellShape_Name( m_pCanvas->paint().shape ) ) ).arg( m_pCanvas->paint().nStairSteps ) );
            return;
        }
        int shape = -1;
        for ( int i = 0; i < 5; ++i ) {
            if ( arguments[1] == QString::fromLatin1( CypherTileMapCellShape_Name( static_cast<tile_map_cell_shape_t>( i ) ) ) ) shape = i;
        }
        bool ok = true;
        const int steps = arguments.size() == 3 ? arguments[2].toInt( &ok ) : m_pCanvas->paint().nStairSteps;
        if ( shape < 0 || arguments.size() > 3 || !ok || steps < 2 || steps > 32 ) {
            appendError( tr( "Use shape flat|stairs_north|stairs_east|stairs_south|stairs_west [2..32 steps]" ) );
            return;
        }
        findChild<QComboBox *>( QStringLiteral( "TilePaintShape" ) )->setCurrentIndex( shape );
        findChild<QSpinBox *>( QStringLiteral( "TilePaintStairSteps" ) )->setValue( steps );
        setTool( tile_canvas_tool_t::PAINT );
    } );
    add( QStringLiteral( "material" ),
         tr( "List, inspect, or select a blockout paint material." ),
         QStringLiteral( "material [list|slot]" ),
         [this]( const QStringList &arguments ) {
        if ( arguments.size() == 1 ) {
            const u16 nSlot = m_pCanvas->paint().nMaterialSlot;
            const tile_map_material_definition_t material =
                CypherTileMapMaterial_Resolve( nSlot );
            appendInfo( tr( "Paint material %1: %2 (%3)" )
                .arg( nSlot )
                .arg( QString::fromUtf8( material.pDisplayName ) )
                .arg( QString::fromUtf8( material.pStableId ) ) );
            return;
        }
        if ( arguments.size() == 2 &&
             arguments[1].compare( QStringLiteral( "list" ),
                                   Qt::CaseInsensitive ) == 0 ) {
            for ( usize i = 0u; i < CypherTileMapMaterial_Count(); ++i ) {
                const tile_map_material_definition_t *pMaterial =
                    CypherTileMapMaterial_At( i );
                if ( pMaterial != nullptr ) appendInfo(
                    tr( "  %1  %2  [%3]" )
                        .arg( pMaterial->nSlot )
                        .arg( QString::fromUtf8( pMaterial->pDisplayName ) )
                        .arg( QString::fromUtf8( pMaterial->pStableId ) ) );
            }
            return;
        }
        bool bOk = false;
        const uint nSlot = arguments.size() == 2
            ? arguments[1].toUInt( &bOk ) : 0u;
        if ( !bOk || nSlot > std::numeric_limits<u16>::max() ) {
            appendError( tr( "usage: material [list|slot]" ) );
            return;
        }
        m_pPaintMaterialSlot->setValue( static_cast<int>( nSlot ) );
        setTool( tile_canvas_tool_t::PAINT );
        const tile_map_material_definition_t material =
            CypherTileMapMaterial_Resolve( static_cast<u16>( nSlot ) );
        appendInfo( tr( "Paint material set to %1 (%2)." )
            .arg( nSlot )
            .arg( QString::fromUtf8( material.pDisplayName ) ) );
    } );
    add( QStringLiteral( "door" ),
         tr( "Select the door tool and choose its cell-edge direction." ),
         QStringLiteral( "door [north|east|south|west]" ),
         [this]( const QStringList &arguments ) {
        if ( arguments.size() > 2 ) {
            appendError( tr( "usage: door [north|east|south|west]" ) );
            return;
        }
        if ( arguments.size() == 2 ) {
            const QString side = arguments[1].toLower();
            tile_map_marker_side_t value = tile_map_marker_side_t::NONE;
            if ( side == QStringLiteral( "north" ) ) {
                value = tile_map_marker_side_t::NORTH;
            } else if ( side == QStringLiteral( "east" ) ) {
                value = tile_map_marker_side_t::EAST;
            } else if ( side == QStringLiteral( "south" ) ) {
                value = tile_map_marker_side_t::SOUTH;
            } else if ( side == QStringLiteral( "west" ) ) {
                value = tile_map_marker_side_t::WEST;
            } else {
                appendError( tr( "usage: door [north|east|south|west]" ) );
                return;
            }
            const int iSide = m_pDoorSide->findData( static_cast<int>( value ) );
            if ( iSide >= 0 ) m_pDoorSide->setCurrentIndex( iSide );
            m_pCanvas->setDoorSide( value );
        }
        setTool( tile_canvas_tool_t::DOOR );
        appendInfo( tr( "Door tool active. Click a cell's %1 edge." )
            .arg( m_pDoorSide->currentText() ) );
    } );
    add( QStringLiteral( "zoom" ),
         tr( "Inspect or change canvas zoom." ),
         QStringLiteral( "zoom [fit|in|out|pixels-per-cell]" ),
         [this]( const QStringList &arguments ) {
        if ( arguments.size() == 1 ) {
            appendInfo( tr( "Zoom is %1 pixels per cell." ).arg(
                m_pCanvas->zoomFactor(), 0, 'f', 1 ) );
            return;
        }
        if ( arguments.size() != 2 ) {
            appendError( tr( "usage: zoom [fit|in|out|pixels-per-cell]" ) );
            return;
        }
        const QString value = arguments[1].toLower();
        if ( value == QStringLiteral( "fit" ) ) {
            m_pCanvas->fitToView();
        } else if ( value == QStringLiteral( "in" ) ) {
            m_pCanvas->setZoomFactor( m_pCanvas->zoomFactor() * 1.25 );
        } else if ( value == QStringLiteral( "out" ) ) {
            m_pCanvas->setZoomFactor( m_pCanvas->zoomFactor() / 1.25 );
        } else {
            bool bOk = false;
            const qreal zoom = value.toDouble( &bOk );
            if ( !bOk || !std::isfinite( zoom ) || zoom <= 0.0 ) {
                appendError( tr( "Zoom must be fit, in, out, or a positive number." ) );
                return;
            }
            m_pCanvas->setZoomFactor( zoom );
        }
    } );
    QStringList completions = m_commands.keys();
    for ( auto iCommand = m_commands.cbegin();
          iCommand != m_commands.cend(); ++iCommand ) {
        completions.push_back(
            QStringLiteral( "help %1" ).arg( iCommand.key() ) );
    }
    completions.append( {
        QStringLiteral( "door north" ),
        QStringLiteral( "door east" ),
        QStringLiteral( "door south" ),
        QStringLiteral( "door west" ),
        QStringLiteral( "material list" ),
        QStringLiteral( "camera_view perspective" ),
        QStringLiteral( "camera_view top" ),
        QStringLiteral( "camera_view bottom" ),
        QStringLiteral( "camera_view front" ),
        QStringLiteral( "camera_view back" ),
        QStringLiteral( "camera_view left" ),
        QStringLiteral( "camera_view right" ),
        QStringLiteral( "camera_view level" ),
        QStringLiteral( "camera_level up" ),
        QStringLiteral( "camera_level down" ),
        QStringLiteral( "camera_bookmark clear" ),
        QStringLiteral( "camera_auto_orbit on" ),
        QStringLiteral( "camera_auto_orbit off" ),
        QStringLiteral( "camera_auto_orbit toggle" ),
        QStringLiteral( "zoom fit" ),
        QStringLiteral( "zoom in" ),
        QStringLiteral( "zoom out" )
    } );
    for ( int slot = 1; slot <= 4; ++slot ) {
        completions.push_back( QStringLiteral( "camera_bookmark store %1" ).arg( slot ) );
        completions.push_back( QStringLiteral( "camera_bookmark recall %1" ).arg( slot ) );
    }
    for ( usize i = 0u; i < CypherTileMapMaterial_Count(); ++i ) {
        const tile_map_material_definition_t *pMaterial =
            CypherTileMapMaterial_At( i );
        if ( pMaterial != nullptr ) completions.push_back(
            QStringLiteral( "material %1" ).arg( pMaterial->nSlot ) );
    }
    m_pConsole->setCompletions( completions );
}

void CypherTileEditorMainWindow::executeCommandLine( const QString &line )
{
    const QStringList arguments = QProcess::splitCommand( line );
    if ( arguments.isEmpty() ) return;
    const QString name = arguments.front().toLower();
    const auto iCommand = m_commands.constFind( name );
    if ( iCommand == m_commands.cend() ) {
        appendError( tr( "Unknown command '%1'. Type 'help' for a list." ).arg(
            name ) );
        return;
    }
    iCommand->execute( arguments );
}

void CypherTileEditorMainWindow::appendInfo( const QString &message )
{
    if ( m_pConsole != nullptr ) m_pConsole->appendInfo( message );
}

void CypherTileEditorMainWindow::appendWarning( const QString &message )
{
    if ( m_pConsole != nullptr ) m_pConsole->appendWarning( message );
}

void CypherTileEditorMainWindow::appendError( const QString &message )
{
    if ( m_pConsole != nullptr ) m_pConsole->appendError( message );
}

void CypherTileEditorMainWindow::setStatus(
    const QString &message,
    bool bError )
{
    if ( m_pStatusMessage == nullptr ) return;
    m_pStatusMessage->setText( message );
    m_pStatusMessage->setStyleSheet( bError
        ? QStringLiteral( "color: #ef7777;" )
        : QStringLiteral( "color: #76c6e8;" ) );
}

} // namespace cypher::tools::tile_editor
