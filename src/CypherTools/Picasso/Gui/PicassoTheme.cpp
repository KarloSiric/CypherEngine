//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Gui/PicassoTheme.cpp
//  Purpose: Implements Picasso's shared Qt visual theme.
//  Details: Compact charcoal surfaces, beveled controls, colored tool glyphs,
//           and an orange active-state accent follow the visual language of
//           professional world-authoring applications on every desktop host.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoTheme.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace cypher::tools::picasso
{

void PicassoTheme_Apply( QApplication &application )
{
    // Fusion gives every desktop host the same base metrics and polish points;
    // native styles differ too much for a shared authoring-tool layout.
    application.setStyle( QStyleFactory::create( "Fusion" ) );

    QFont applicationFont = application.font();
    applicationFont.setPointSizeF( 9.5 );
    application.setFont( applicationFont );

    // Palette roles cover widgets that do not expose useful stylesheet hooks.
    QPalette palette;
    palette.setColor( QPalette::Window, QColor( 52, 52, 52 ) );
    palette.setColor( QPalette::WindowText, QColor( 211, 211, 211 ) );
    palette.setColor( QPalette::Base, QColor( 42, 42, 42 ) );
    palette.setColor( QPalette::AlternateBase, QColor( 49, 49, 49 ) );
    palette.setColor( QPalette::ToolTipBase, QColor( 47, 47, 47 ) );
    palette.setColor( QPalette::ToolTipText, QColor( 239, 239, 239 ) );
    palette.setColor( QPalette::Text, QColor( 211, 211, 211 ) );
    palette.setColor( QPalette::Button, QColor( 48, 48, 48 ) );
    palette.setColor( QPalette::ButtonText, QColor( 211, 211, 211 ) );
    palette.setColor( QPalette::BrightText, QColor( 255, 255, 255 ) );
    palette.setColor( QPalette::Highlight, QColor( 202, 119, 44 ) );
    palette.setColor( QPalette::HighlightedText, QColor( 255, 247, 237 ) );
    palette.setColor( QPalette::PlaceholderText, QColor( 130, 130, 130 ) );
    palette.setColor( QPalette::Link, QColor( 231, 145, 61 ) );
    palette.setColor( QPalette::Disabled, QPalette::Text, QColor( 89, 89, 89 ) );
    palette.setColor( QPalette::Disabled, QPalette::ButtonText, QColor( 89, 89, 89 ) );
    application.setPalette( palette );

    /*
    ========================================================================
    Picasso widget style

    Object names and dynamic properties are the interface between the Qt widget
    tree and this stylesheet. Keep selectors synchronized with PicassoMainWindow;
    an unmatched selector silently falls back to the base Fusion appearance.
    ========================================================================
    */
    application.setStyleSheet( R"QSS(
        * { color: #d3d3d3; selection-background-color: #995a27; selection-color: #ffffff; }
        QMainWindow, QDialog { background: #343434; }
        QMainWindow::separator { background: #191919; width: 2px; height: 2px; }
        QMainWindow::separator:hover { background: #c47730; }
        QToolTip { background: #2f2f2f; color: #f0f0f0; border: 1px solid #737373; padding: 5px 7px; }

        QMenuBar { background: #363636; border-top: 1px solid #484848; border-bottom: 1px solid #1b1b1b; padding: 0 3px; min-height: 24px; }
        QMenuBar::item { padding: 5px 10px; background: transparent; }
        QMenuBar::item:selected { background: #4a4a4a; color: #ffffff; }
        QMenu { background: #353535; border: 1px solid #181818; padding: 3px; }
        QMenu::item { padding: 6px 32px 6px 24px; border-left: 2px solid transparent; }
        QMenu::item:selected { background: #474747; color: #ffffff; border-left-color: #d18432; }
        QMenu::item:disabled { color: #717171; }
        QMenu::separator { height: 1px; background: #202020; margin: 4px 7px; }
        QMenu::right-arrow { image: url(:/picasso/icons/chevron-right.svg); width: 13px; height: 13px; }

        QToolBar { background: #313131; border: 0; border-top: 1px solid #444444; border-bottom: 1px solid #191919; spacing: 2px; padding: 3px; }
        QToolBar#PicassoMainToolbar { min-height: 40px; max-height: 40px; }
        QToolBar#PicassoOperationToolbar { min-height: 34px; max-height: 34px; background: #303030; }
        QToolBar#PicassoToolRail { min-width: 42px; max-width: 42px; background: #343434; border-right: 1px solid #181818; padding: 3px; }
        QToolBar::separator { background: #1d1d1d; width: 1px; height: 1px; margin: 5px 6px; }

        QToolButton { background: #303030; border: 1px solid #1b1b1b; border-top-color: #505050; border-left-color: #494949; padding: 3px; min-width: 30px; min-height: 30px; border-radius: 2px; }
        QToolButton:hover { background: #3b3f41; border-top-color: #66777e; border-left-color: #5d7078; border-bottom-color: #30464f; border-right-color: #30464f; }
        QToolButton:focus { border-color: #d18432; }
        QToolButton:pressed { background: #292929; border-color: #a96129; }
        QToolButton:checked { background: #44382c; border: 1px solid #db8734; border-top-color: #f0a252; border-left-color: #e09243; color: #ffc177; }
        QToolButton:disabled { background: #2b2b2b; border-color: #242424; color: #686868; }
        QToolButton::menu-indicator { image: url(:/picasso/icons/chevron-down.svg); width: 10px; height: 10px; subcontrol-origin: padding; subcontrol-position: bottom right; }
        QToolBar#PicassoToolRail QToolButton { min-width: 34px; max-width: 34px; min-height: 34px; max-height: 34px; padding: 3px; }
        QToolButton#PicassoContextTool { min-width: 116px; min-height: 25px; padding: 2px 8px 2px 5px; text-align: left; background: #292b2c; border: 1px solid #1d1d1d; border-left: 2px solid #c47730; color: #c8cacc; }
        QToolButton#PicassoContextTool:hover { background: #292b2c; border-color: #1d1d1d; border-left-color: #c47730; }
        QWidget#PicassoBrushContext { background: transparent; }
        QToolButton[commandButton="true"] { padding: 3px 9px; min-width: 66px; }
        QToolButton[segment="true"] { background: #303030; border: 1px solid #202020; padding: 3px 9px; min-height: 24px; }
        QToolButton[segment="true"]:hover { border-color: #7196aa; }
        QToolButton[segment="true"]:checked { background: #45372a; border-color: #dd8934; color: #ffc277; }
        QToolButton[channelButton="true"] { min-width: 34px; min-height: 22px; max-height: 22px; padding: 1px 7px; }
        QToolButton#PicassoPresetButton { background: #303030; border: 1px solid #202020; padding: 5px; min-height: 76px; }
        QToolButton#PicassoPresetButton:hover { border: 1px solid #d18432; background: #3c3c3c; }
        QToolButton#PicassoPresetButton:checked { background: #3a342f; border: 2px solid #d18432; color: #f1f1f1; }

        QDockWidget { background: #363636; color: #dedede; font-weight: 600; }
        QDockWidget::title { background: #2e2e2e; border-top: 1px solid #474747; border-bottom: 1px solid #191919; padding: 4px 7px; min-height: 14px; }
        QDockWidget::close-button, QDockWidget::float-button { background: #343434; border: 1px solid #202020; padding: 1px; width: 14px; height: 14px; }
        QDockWidget::close-button { image: url(:/picasso/icons/x.svg); }
        QDockWidget::float-button { image: url(:/picasso/icons/external-link.svg); }
        QDockWidget::close-button:hover, QDockWidget::float-button:hover { background: #4a4036; border-color: #cb782f; }
        QDockWidget > QWidget { border: 0; }

        QWidget#PicassoDocumentHost { background: #252728; }
        QWidget#PicassoDocumentTabs { background: #414141; border-top: 1px solid #505050; border-bottom: 1px solid #1d1d1d; min-height: 30px; max-height: 30px; }
        QWidget#PicassoDocumentInfoBar { background: #343434; border-bottom: 1px solid #1c1c1c; min-height: 29px; max-height: 29px; }
        QLabel#PicassoDocumentName { color: #f0f0f0; font-weight: 600; }
        QLabel#PicassoDocumentInfo { color: #a8aaab; }
        QWidget#PicassoLayersPanel { background: #363636; }

        QTreeWidget, QListWidget, QTableWidget, QPlainTextEdit { background: #343434; border: 1px solid #202020; alternate-background-color: #303030; outline: 0; }
        QTreeWidget:focus, QListWidget:focus, QTableWidget:focus, QPlainTextEdit:focus { border-color: #616c72; }
        QTreeWidget::item, QListWidget::item { min-height: 28px; padding: 3px 5px; border-left: 2px solid transparent; }
        QTreeWidget::item:hover, QListWidget::item:hover { background: #414141; }
        QTreeWidget::item:selected, QListWidget::item:selected { background: #453a30; color: #f7f7f7; border-left: 2px solid #df8934; }
        QTreeWidget::indicator, QListWidget::indicator { width: 13px; height: 13px; background: #292929; border: 1px solid #191919; border-top-color: #4e4e4e; border-left-color: #494949; }
        QTreeWidget::indicator:hover, QListWidget::indicator:hover { border-color: #7196aa; }
        QTreeWidget::indicator:checked, QListWidget::indicator:checked { image: url(:/picasso/icons/check.svg); background: #36583b; border-color: #68a56b; }
        QListWidget#PicassoGeneratorList::item { min-height: 40px; border-bottom: 1px solid #292929; }
        QListWidget#PicassoFilterChain::item { min-height: 42px; background: #313131; border: 1px solid #222222; margin-bottom: 2px; }
        QListWidget#PicassoFilterChain::item:hover { background: #404040; border-color: #68757b; }
        QListWidget#PicassoFilterChain::item:checked { color: #e0e0e0; }
        QListWidget#PicassoHistoryList::item { min-height: 28px; border-bottom: 1px solid #292929; }
        QListWidget#PicassoHistoryList::item:selected { background: #493a2d; border-left: 3px solid #df8934; color: #ffc078; }
        QHeaderView::section { background: #303030; border: 0; border-right: 1px solid #1b1b1b; border-bottom: 1px solid #191919; padding: 5px; }

        QGroupBox { background: #343434; border: 1px solid #202020; margin-top: 12px; padding: 10px 7px 7px 7px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 7px; padding: 0 4px; color: #bababa; }
        QWidget[propertyRow="true"] { background: #313131; border-top: 1px solid #414141; border-bottom: 1px solid #252525; min-height: 22px; }
        QLabel#PicassoSectionTitle { background: #2e2e2e; border-top: 1px solid #464646; border-bottom: 1px solid #1c1c1c; color: #dedede; font-weight: 700; padding: 5px 7px; }
        QLabel#PicassoMutedText { color: #969696; padding: 0 2px; }
        QLabel#ToolbarLabel { color: #a1a1a1; padding-left: 5px; }
        QLabel#ToolbarGroupLabel { color: #a6aaac; font-size: 9px; font-weight: 700; padding: 0 8px 0 5px; }

        QPushButton { background: #303030; border: 1px solid #202020; border-top-color: #505050; border-left-color: #494949; padding: 5px 10px; border-radius: 2px; }
        QPushButton:hover { border-color: #7196aa; background: #434748; }
        QPushButton:focus { border-color: #df8b36; }
        QPushButton:pressed { background: #292929; border-color: #bd6d2b; }
        QPushButton:default { border-color: #df8b36; }
        QPushButton:disabled { background: #2d2d2d; color: #6b6b6b; border-color: #252525; }

        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #292929; border: 1px solid #1c1c1c; border-top-color: #191919; border-bottom-color: #4a4a4a; padding: 4px 6px; border-radius: 1px; selection-background-color: #a5622b; }
        QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover { border-color: #687d88; }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #db8734; }
        QComboBox::drop-down { border: 0; width: 19px; }
        QComboBox::down-arrow { image: url(:/picasso/icons/chevron-down.svg); width: 12px; height: 12px; }
        QScrollArea { background: #363636; border: 0; }
        QScrollArea > QWidget > QWidget { background: #363636; }

        QWidget#PicassoConsoleChannels { background: #303030; border-bottom: 1px solid #1b1b1b; }
        QToolButton[consoleChannel="true"] { min-height: 20px; max-height: 20px; min-width: 44px; padding: 1px 9px; border: 1px solid #202020; }
        QToolButton[consoleChannel="true"]:hover { border-color: #7196aa; }
        QToolButton[consoleChannel="true"]:checked { color: #ffc078; background: #493a2d; border-color: #d17f31; }
        QPlainTextEdit#PicassoConsoleOutput { background: #232323; border: 0; padding: 6px; }
        QWidget#PicassoConsoleInputRow { background: #2c2c2c; border-top: 1px solid #494949; }
        QLineEdit#PicassoConsoleInput { background: transparent; border: 0; padding: 4px; }
        QLabel#ConsolePrompt { color: #e38a35; font-weight: 700; }

        QSlider::groove:horizontal { height: 4px; background: #242424; border-bottom: 1px solid #515151; border-radius: 0; }
        QSlider::sub-page:horizontal { background: #a86129; }
        QSlider::handle:horizontal { width: 11px; margin: -4px 0; background: #df8834; border: 1px solid #ffb15f; border-radius: 5px; }
        QStatusBar { background: #303030; border-top: 1px solid #171717; color: #b0b0b0; min-height: 22px; }
        QStatusBar QLabel { padding: 0 7px; border-left: 1px solid #484848; }
        QStatusBar QLabel#AccentLabel { border-left: 0; }
        QSplitter::handle { background: #1a1a1a; }
        QSplitter::handle:hover { background: #855329; }
        QScrollBar:vertical { background: #292929; width: 11px; margin: 0; }
        QScrollBar::handle:vertical { background: #5a5a5a; min-height: 30px; border: 1px solid #292929; }
        QScrollBar::handle:vertical:hover { background: #747474; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal { background: #292929; height: 11px; }
        QScrollBar::handle:horizontal { background: #5a5a5a; min-width: 30px; border: 1px solid #292929; }
        QLabel#SectionLabel { color: #a2a2a2; font-size: 10px; font-weight: 600; }
        QLabel#ValueLabel { color: #e0e0e0; }
        QLabel#AccentLabel { color: #79bc74; }
        QLabel#WarningLabel { color: #e18a35; }
        QWidget#PicassoColorSwatches { background: transparent; border: 0; }
    )QSS" );
}

} // namespace cypher::tools::picasso
