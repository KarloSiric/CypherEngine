//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Gui/PicassoMainWindow.h
//  Purpose: Declares the standalone Picasso authoring workspace.
//  Details: The main window composes Qt presentation around PicassoCore and keeps
//           file dialogs, docks, actions, and display conversion out of the
//           reusable texture document backend.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_PICASSO_MAINWINDOW_H
#define CYPHER_TOOLS_PICASSO_MAINWINDOW_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "PicassoMaterialImport.h"
#include "PicassoTextureDocument.h"
#include "CypherCommon_CommandSystem.h"

#include <QColor>
#include <QImage>
#include <QMainWindow>
#include <QString>
#include <QStringList>

class QAction;
class QActionGroup;
class QCloseEvent;
class QColor;
class QComboBox;
class QDockWidget;
class QLabel;
class QListWidget;
class QRect;
class QSlider;
class QSpinBox;
class QToolBar;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace cypher::tools::picasso
{

class PicassoCanvas;
class PicassoConsole;
class PicassoSwatchPreview;
enum class picasso_canvas_interaction_t;
enum class picasso_canvas_tool_t;
enum class picasso_console_record_t : unsigned char;
enum class picasso_console_channel_t : unsigned char;

class PicassoMainWindow final : public QMainWindow
{
public:
    explicit PicassoMainWindow( QWidget *pParent = nullptr );
    ~PicassoMainWindow() override;

protected:
    void closeEvent( QCloseEvent *pEvent ) override;

private:
    void buildActions();
    void buildMenus();
    void buildToolbar();
    void buildWorkspace();
    void buildToolRail();
    void restoreWorkspace();
    void initializeCommandSystem();
    void registerCommands();

    void newTexture();
    void openTexture();
    bool createTexture( const picasso_canvas_desc_t &desc );
    bool openTextureFromPath( const QString &path );
    bool saveTexture();
    bool saveTextureAs();
    bool saveTextureTo( const QString &path );
    bool maybeSave();

    void newMaterial();
    void openMaterial();
    bool openMaterialFromPath( const QString &path );
    void activateMaterialPreset( const QString &name, int iPreset );

    void applyOperation( picasso_texture_operation_t operation );
    void compileTexture();
    void undo();
    void redo();
    void jumpToHistory( usize iHistoryCursor );
    void handleCanvasInteraction(
        picasso_canvas_tool_t tool,
        picasso_canvas_interaction_t interaction,
        qreal x,
        qreal y );
    bool continuePaintStroke(
        picasso_canvas_tool_t tool,
        qreal x,
        qreal y,
        QRect *pDirtyRegionOut );
    bool applyBrushDab(
        picasso_canvas_tool_t tool,
        qreal x,
        qreal y,
        QRect *pDirtyRegionOut );
    void refreshCanvasRegion( const QRect &imageRegion );
    void refreshDocument( bool bResetCanvasView = true );
    void refreshActions();
    void refreshProperties();
    void refreshHistory();
    void refreshMaterial();
    void refreshToolContext();
    void updateWindowTitle();
    void appendOutput(
        const QString &message,
        picasso_console_record_t type,
        picasso_console_channel_t channel );
    void appendInfo( const QString &message );
    void appendWarning( const QString &message );
    void appendError( const QString &message );
    void setStatusMessage( const QString &message );
    void executeCommandLine( const QString &line );
    [[nodiscard]] QStringList completeCommandLine( const QString &partial );
    [[nodiscard]] error_code_t dispatchCommand(
        const command_args_t &args ) noexcept;
    void printCommandHelp( string_view_t commandName );

    static error_code_t ExecuteCommandCallback(
        const command_context_t &context,
        const command_args_t &args,
        void *pCommandUserData ) noexcept;
    static void CommandOutputCallback(
        string_view_t text,
        void *pUserData ) noexcept;
    static bool_t CommandHelpVisitor(
        command_handle_t handle,
        const concommand_desc_t &desc,
        void *pUserData ) noexcept;
    static usize CompleteChannelCallback(
        string_view_t partial,
        string_view_t *pSuggestions,
        usize nSuggestionCapacity,
        void *pUserData ) noexcept;
    static usize CompleteToolCallback(
        string_view_t partial,
        string_view_t *pSuggestions,
        usize nSuggestionCapacity,
        void *pUserData ) noexcept;

    [[nodiscard]] QImage buildDisplayImage() const;

    picasso_texture_document_t m_document{};
    picasso_paint_material_t m_material{};
    command_system_t *m_pCommandSystem{ nullptr };
    QString m_currentPath{};
    QString m_currentMaterialPath{};
    QColor m_paintColor{ 24, 28, 31, 255 };
    qreal m_lastStrokeX{ 0.0 };
    qreal m_lastStrokeY{ 0.0 };
    bool m_bHasLastStrokePoint{ false };

    PicassoCanvas *m_pCanvas{ nullptr };
    QWidget *m_pColorSwatches{ nullptr };
    PicassoConsole *m_pConsole{ nullptr };
    PicassoSwatchPreview *m_pPreview{ nullptr };
    QTreeWidget *m_pLayerTree{ nullptr };
    QTreeWidgetItem *m_pBaseLayerItem{ nullptr };
    QDockWidget *m_pConsoleDock{ nullptr };
    QListWidget *m_pHistoryList{ nullptr };
    QLabel *m_pDocumentName{ nullptr };
    QLabel *m_pDocumentInfo{ nullptr };
    QLabel *m_pStatusMessage{ nullptr };
    QLabel *m_pStatusDimensions{ nullptr };
    QLabel *m_pStatusPixel{ nullptr };
    QLabel *m_pStatusBrush{ nullptr };
    QLabel *m_pStatusMemory{ nullptr };
    QLabel *m_pStatusZoom{ nullptr };
    QLabel *m_pDimensionsValue{ nullptr };
    QLabel *m_pFormatValue{ nullptr };
    QLabel *m_pColorSpaceValue{ nullptr };
    QLabel *m_pSourceValue{ nullptr };
    QLabel *m_pMaterialName{ nullptr };
    QLabel *m_pMaterialShader{ nullptr };
    QTreeWidget *m_pMaterialChannels{ nullptr };
    QSlider *m_pOpacitySlider{ nullptr };
    QSpinBox *m_pBrushSize{ nullptr };
    QSpinBox *m_pBrushOpacity{ nullptr };
    QSpinBox *m_pBrushHardness{ nullptr };
    QComboBox *m_pBlendMode{ nullptr };
    QToolBar *m_pToolRail{ nullptr };
    QToolButton *m_pContextToolButton{ nullptr };
    QWidget *m_pBrushContextPanel{ nullptr };
    QAction *m_pBrushContextAction{ nullptr };

    QAction *m_pNewAction{ nullptr };
    QAction *m_pOpenAction{ nullptr };
    QAction *m_pNewMaterialAction{ nullptr };
    QAction *m_pOpenMaterialAction{ nullptr };
    QAction *m_pSaveAction{ nullptr };
    QAction *m_pSaveAsAction{ nullptr };
    QAction *m_pCompileAction{ nullptr };
    QAction *m_pUndoAction{ nullptr };
    QAction *m_pRedoAction{ nullptr };
    QAction *m_pFlipHorizontalAction{ nullptr };
    QAction *m_pFlipVerticalAction{ nullptr };
    QAction *m_pRotateClockwiseAction{ nullptr };
    QAction *m_pRotateCounterClockwiseAction{ nullptr };
    QAction *m_pRotate180Action{ nullptr };
    QAction *m_pFitAction{ nullptr };
    QAction *m_pActualSizeAction{ nullptr };
    QAction *m_pZoomInAction{ nullptr };
    QAction *m_pZoomOutAction{ nullptr };
    QAction *m_pToggleConsoleAction{ nullptr };
    QActionGroup *m_pChannelActions{ nullptr };
    QAction *m_pChannelRgbaAction{ nullptr };
    QAction *m_pChannelRedAction{ nullptr };
    QAction *m_pChannelGreenAction{ nullptr };
    QAction *m_pChannelBlueAction{ nullptr };
    QAction *m_pChannelAlphaAction{ nullptr };
    QActionGroup *m_pToolActions{ nullptr };
    QAction *m_pSelectToolAction{ nullptr };
    QAction *m_pInspectToolAction{ nullptr };
    QAction *m_pPanToolAction{ nullptr };
    QAction *m_pZoomToolAction{ nullptr };
};

} // namespace cypher::tools::picasso

#endif // CYPHER_TOOLS_PICASSO_MAINWINDOW_H
