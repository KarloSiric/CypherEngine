//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileEditorMainWindow.h
//  Purpose: Declares the standalone tile-map editor workspace.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_MAINWINDOW_H
#define CYPHER_TOOLS_TILEEDITOR_MAINWINDOW_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherTileCanvas.h"
#include "CypherTileDocumentBridge.h"
#include "CypherTileEditorSettingsDialog.h"
#include "CypherTileOrthoMaterials.h"

#include <QMainWindow>
#include <QMap>
#include <QStringList>

#include <functional>
#include <vector>

class QAction;
class QActionGroup;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QProcess;
class QSpinBox;
class QTabWidget;
class QTimer;
class QTreeWidget;

namespace cypher::tools::tile_editor
{

class CypherTileConsole;
class CypherTileShellRunner;
class CypherTileMaterialBrowser;
class CypherTileMapProperties;
class CypherTileRenderViewport;
class CypherTileOrthoView;
class CypherTileViewWorkspace;
enum class tile_editor_view_t;

class CypherTileEditorMainWindow final : public QMainWindow
{
public:
    explicit CypherTileEditorMainWindow( QWidget *pParent = nullptr );
    ~CypherTileEditorMainWindow() override;

    bool openFilePath( const QString &path, bool bConfirmDiscard = true );
    bool importConfigurationProfile( const QString &path, QString *pErrorOut = nullptr );
    bool exportConfigurationProfile( const QString &path, QString *pErrorOut = nullptr ) const;

protected:
    void closeEvent( QCloseEvent *pEvent ) override;

private:
    struct command_entry_t {
        QString help{};
        QString usage{};
        std::function<void( const QStringList & )> execute{};
    };

    void createActions();
    void buildMenus();
    void buildToolbar();
    void buildWorkspace();
    QWidget *createWorkspaceView( tile_editor_view_t view );
    void configureTopView( CypherTileCanvas *view );
    void configureOrthoView( CypherTileOrthoView *view );
    void synchronizeSelection();
    void synchronizeAuthoring();
    void buildToolsDock();
    void buildInspectorDock();
    void buildConsoleDock();
    void buildStatusBar();
    void buildOutlinerDock();
    void bindProjectMaterial( unsigned short slot, const QString &path );
    void applyMaterialToSelection( unsigned short slot );
    void refreshOutliner();
    void resetWorkspaceLayout();
    void translateSelection( int dx, int dy, bool copy = false );
    void deleteSelection();
    void raiseSelection( int delta );
    void adjustSelectionWallHeight( int delta );
    void rotateSelection();
    void syncOutlinerSelection();
    void registerCommands();

    QAction *createAction(
        const QString &id,
        const QString &text,
        bool bCheckable = false );
    void applyShortcuts();
    void setTool( tile_canvas_tool_t tool );
    void applyPreferences();
    void savePreferences();
    void showSettings( bool cameraPage = false );
    void applyEditorSettings( const tile_editor_preferences_t &preferences );
    void buildCameraMenu();
    void synchronizeCameraActions();

    bool maybeSave();
    void newMap();
    void showMapProperties();
    bool applyMapDescription( const tile_map_document_desc_t &description, QString &error );
    void openMap();
    bool saveMap();
    bool saveMapAs();
    bool saveMapTo( const QString &path );
    void addRecentFile( const QString &path );
    void rebuildRecentMenu();
    void restoreWorkspace();
    void saveWorkspace();

    void undo();
    void redo();
    void onDocumentChanged();
    void refreshMapViews();
    void refreshOrthoMaterials( bool force = false );
    void frameView( tile_editor_view_t view );
    void updateWindowState();
    void updateInspector( bool bHasSelection, tile_map_grid_coord_t coordinate );
    void applyInspectorCellEdit( flags32_t fields );
    void applyInspectorSpawnEdit();
    void updateValidationPanel();
    bool validateMap( bool bAnnounceSuccess = true );
    void buildMap();
    void previewMap();
    void stopPreview();
    void schedulePreviewSync();
    bool writePreviewSnapshot();

    void executeCommandLine( const QString &line );
    void appendInfo( const QString &message );
    void appendWarning( const QString &message );
    void appendError( const QString &message );
    void setStatus( const QString &message, bool bError = false );

    CypherTileDocumentBridge m_document{};
    tile_editor_preferences_t m_preferences{};
    tile_ortho_material_cache_t m_orthoMaterials{};
    CypherTileCanvas *m_pCanvas{ nullptr };
    CypherTileRenderViewport *m_pRenderViewport{ nullptr };
    CypherTileOrthoView *m_pFrontView{ nullptr };
    CypherTileOrthoView *m_pSideView{ nullptr };
    std::vector<CypherTileCanvas *> m_topViews{};
    std::vector<CypherTileOrthoView *> m_orthoViews{};
    std::function<void( const tile_map_paint_t & )> m_paintPicked{};
    bool m_syncingSelection{ false };
    CypherTileViewWorkspace *m_pViewWorkspace{ nullptr };
    QTimer *m_pDocumentPreviewTimer{ nullptr };
    QTimer *m_pCameraPreferenceTimer{ nullptr };
    CypherTileConsole *m_pConsole{ nullptr };
    CypherTileShellRunner *m_pShellRunner{ nullptr };
    QTabWidget *m_pConsoleTabs{ nullptr };
    QDockWidget *m_pConsoleDock{ nullptr };
    QDockWidget *m_pAssetsDock{ nullptr };
    CypherTileMaterialBrowser *m_pMaterialBrowser{ nullptr };
    CypherTileMapProperties *m_pMapProperties{ nullptr };
    QWidget *m_pMapPropertiesPage{ nullptr };
    QDockWidget *m_pInspectorDock{ nullptr };
    QDockWidget *m_pOutlinerDock{ nullptr };
    QWidget *m_pPaintPanel{ nullptr };
    QTreeWidget *m_pOutliner{ nullptr };
    QLineEdit *m_pObjectFilter{ nullptr };
    QLabel *m_pObjectCount{ nullptr };
    QSpinBox *m_pOffsetX{ nullptr };
    QSpinBox *m_pOffsetY{ nullptr };
    QSpinBox *m_pConstructionX{ nullptr };
    QSpinBox *m_pConstructionY{ nullptr };
    QMenu *m_pRecentMenu{ nullptr };
    QMenu *m_pWindowMenu{ nullptr };
    QProcess *m_pPreviewProcess{ nullptr };
    QTimer *m_pPreviewSyncTimer{ nullptr };
    QString m_previewMapPath{};

    QMap<QString, QAction *> m_actions{};
    QActionGroup *m_pToolActions{ nullptr };
    QMap<QString, command_entry_t> m_commands{};
    QStringList m_recentFiles{};

    QLabel *m_pDocumentSize{ nullptr };
    QLabel *m_pSelectionLabel{ nullptr };
    QCheckBox *m_pFloorEnabled{ nullptr };
    QSpinBox *m_pFloorLevel{ nullptr };
    QSpinBox *m_pWallHeight{ nullptr };
    QSpinBox *m_pMaterialSlot{ nullptr };
    QComboBox *m_pCellShape{ nullptr };
    QSpinBox *m_pStairSteps{ nullptr };
    QSpinBox *m_pPaintMaterialSlot{ nullptr };
    QComboBox *m_pDoorSide{ nullptr };
    QLineEdit *m_pMaterialFilter{ nullptr };
    QListWidget *m_pMaterialList{ nullptr };
    QDoubleSpinBox *m_pSpawnYaw{ nullptr };
    QListWidget *m_pValidationList{ nullptr };
    QLabel *m_pValidationSummary{ nullptr };
    QTabWidget *m_pInspectorTabs{ nullptr };
    bool m_bValidationCompleted{ false };
    int m_nValidationErrors{ 0 };
    int m_nValidationWarnings{ 0 };
    QLabel *m_pStatusMessage{ nullptr };
    QLabel *m_pStatusCursor{ nullptr };
    QLabel *m_pStatusDocument{ nullptr };
    QLabel *m_pStatusZoom{ nullptr };
    bool m_bUpdatingInspector{ false };
    flags32_t m_inspectorDirtyFields{ 0u };
    std::vector<tile_map_grid_coord_t> m_inspectorSelectionCells{};
};

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_MAINWINDOW_H
