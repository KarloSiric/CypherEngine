//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Actions_Tests.cpp
//  Purpose: Contract tests for command-driven actions, menus, toolbars, and
//           key-chord conversion.
//  Details: Menus and shortcuts must run exactly the registered command and
//           show exactly its state, or the console and the menus would
//           disagree about what the editor can do.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Actions.h"
#include "CypherEditorGui_Application.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QImage>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::gui;

namespace
{

struct counters_t {
    int nNew{ 0 };
    bool bGrid{ false };
};

command_result_t CountNew( void *pContext, const command_args_t & )
{
    ++static_cast<counters_t *>( pContext )->nNew;
    return command_result_t::OK;
}

command_result_t ToggleGrid( void *pContext, const command_args_t & )
{
    counters_t *pCounters = static_cast<counters_t *>( pContext );
    pCounters->bGrid = !pCounters->bGrid;
    return command_result_t::OK;
}

u32 GridState( void *pContext )
{
    return COMMAND_STATE_ENABLED | ( static_cast<counters_t *>( pContext )->bGrid ? COMMAND_STATE_CHECKED : 0u );
}

u32 Disabled( void * )
{
    return COMMAND_STATE_NONE;
}

// The framework defaults, resolved through a registry of framework tokens.
editor_style_t DefaultStyle()
{
    theme_registry_t tokens{};
    REQUIRE( EditorThemeRegistry_Init( &tokens, Allocator_GetSystem() ) == theme_status_t::OK );
    REQUIRE( EditorStyle_RegisterTokens( &tokens ) == theme_status_t::OK );
    editor_style_t style = EditorStyle_Resolve( &tokens, nullptr, 0u );
    EditorThemeRegistry_Shutdown( &tokens );
    return style;
}

struct fixture_t {
    counters_t counters{};
    command_registry_t registry{};
    editor_style_t style = DefaultStyle();
    QMainWindow window{};
    editor_actions_t actions{};

    fixture_t()
    {
        EditorGui_RegisterResources();
        REQUIRE( EditorCommands_Init( &registry, Allocator_GetSystem() ) == command_registry_status_t::OK );
        const command_desc_t commands[]{
            { "file.new", "New", "Creates a map.", "file-plus", nullptr, COMMAND_FLAG_NONE, CountNew, nullptr, &counters },
            { "map.grid.toggle", "Show Grid", nullptr, "grid-3x3", nullptr, COMMAND_FLAG_CHECKABLE, ToggleGrid, GridState, &counters },
            { "file.export", "Export", nullptr, nullptr, nullptr, COMMAND_FLAG_NONE, CountNew, Disabled, &counters },
            { "debug.dump", "Dump", nullptr, nullptr, nullptr, COMMAND_FLAG_CONSOLE_ONLY, CountNew, nullptr, &counters },
        };
        REQUIRE( EditorCommands_Register( &registry, commands, std::size( commands ) ) == command_registry_status_t::OK );
        EditorActions_Init( &actions, &registry, &style, &window );
    }
    ~fixture_t() { EditorCommands_Shutdown( &registry ); }
};

QKeySequence Parse( const char *pText )
{
    key_chord_t chord{};
    REQUIRE( EditorKeyChord_Parse( StringView_FromCString( pText ), &chord ) );
    return EditorKeyChord_ToKeySequence( chord );
}

} // namespace

TEST_CASE( "Key chords map to Qt key sequences and back", "[editor][gui][actions]" )
{
    CHECK( Parse( "Ctrl+Shift+S" ) == QKeySequence( Qt::CTRL | Qt::SHIFT | Qt::Key_S ) );
    CHECK( Parse( "F9" ) == QKeySequence( Qt::Key_F9 ) );
    CHECK( Parse( "BracketLeft" ) == QKeySequence( Qt::Key_BracketLeft ) );
    CHECK( Parse( "Delete" ) == QKeySequence( Qt::Key_Delete ) );
    CHECK( Parse( "Num5" ) == QKeySequence( Qt::KeypadModifier | Qt::Key_5 ) );
    CHECK( Parse( "Ctrl+K, Ctrl+C" ) == QKeySequence( Qt::CTRL | Qt::Key_K, Qt::CTRL | Qt::Key_C ) );
    for ( const char *pText : { "Ctrl+Alt+Shift+Meta+X", "Backquote", "Space", "PageDown", "NumEnter", "Alt+F12", "Ctrl+K, Ctrl+C" } ) {
        key_chord_t original{};
        REQUIRE( EditorKeyChord_Parse( StringView_FromCString( pText ), &original ) );
        key_chord_t back{};
        INFO( pText );
        REQUIRE( EditorKeyChord_FromKeySequence( EditorKeyChord_ToKeySequence( original ), &back ) );
        CHECK( EditorKeyChord_Equals( original, back ) );
    }
    key_chord_t unused{};
    CHECK_FALSE( EditorKeyChord_FromKeySequence( QKeySequence(), &unused ) );
}

TEST_CASE( "Qt Backtab normalizes to canonical Shift Tab with its other modifiers intact", "[editor][gui][actions][backtab]" )
{
    const struct {
        Qt::KeyboardModifiers modifiers;
        const char *text;
    } variants[]{
        { Qt::NoModifier, "Shift+Tab" },
        { Qt::ShiftModifier, "Shift+Tab" },
        { Qt::ControlModifier, "Ctrl+Shift+Tab" },
        { Qt::MetaModifier, "Shift+Meta+Tab" },
        { Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier, "Ctrl+Alt+Shift+Meta+Tab" }
    };
    for ( const auto &variant : variants ) {
        const auto modifiers = variant.modifiers;
        const QKeySequence physical( QKeyCombination( modifiers, Qt::Key_Backtab ) );
        const QKeySequence logical( QKeyCombination( modifiers | Qt::ShiftModifier, Qt::Key_Tab ) );
        key_chord_t backtab{}, tab{}, roundTrip{};
        REQUIRE( EditorKeyChord_FromKeySequence( physical, &backtab ) );
        REQUIRE( EditorKeyChord_FromKeySequence( logical, &tab ) );
        CHECK( EditorKeyChord_Equals( backtab, tab ) );
        REQUIRE( backtab.nStrokes == 1u );
        CHECK( backtab.strokes[0].key == KEY_TAB );
        CHECK( ( backtab.strokes[0].modifiers & KEY_MODIFIER_SHIFT ) != 0u );
        CHECK( EditorKeyChord_ToKeySequence( backtab ) == logical );
        REQUIRE( EditorKeyChord_FromKeySequence( EditorKeyChord_ToKeySequence( backtab ), &roundTrip ) );
        CHECK( EditorKeyChord_Equals( backtab, roundTrip ) );
        char text[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
        const usize length = EditorKeyChord_Format( backtab, text );
        REQUIRE( length > 0u );
        CHECK( QString::fromUtf8( text, static_cast<qsizetype>( length ) ) == QString::fromLatin1( variant.text ) );
    }

    key_chord_t plain{};
    REQUIRE( EditorKeyChord_FromKeySequence( QKeySequence( QKeyCombination( Qt::NoModifier, Qt::Key_Backtab ) ), &plain ) );
    char text[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
    const usize length = EditorKeyChord_Format( plain, text );
    CHECK( QString::fromUtf8( text, static_cast<qsizetype>( length ) ) == QStringLiteral( "Shift+Tab" ) );
    key_chord_t declared{};
    REQUIRE( EditorKeyChord_Parse( StringView_FromCString( "Shift+Tab" ), &declared ) );
    CHECK( EditorKeyChord_Equals( plain, declared ) );
}

TEST_CASE( "Menus are built from command tables", "[editor][gui][actions]" )
{
    fixture_t f;
    const editor_menu_item_t menu[]{
        { "File", "file.new" }, { "File", nullptr }, { "File", "file.export" }, { "File/Recent", "file.missing" },
        { "View", "map.grid.toggle" }, { "View", "debug.dump" },
    };
    CHECK( EditorActions_BuildMenus( &f.actions, f.window.menuBar(), menu, std::size( menu ) ) == 2u );
    QMenu *pFile = f.window.menuBar()->findChild<QMenu *>( QStringLiteral( "menu:File" ) );
    REQUIRE( pFile != nullptr );
    CHECK( pFile->actions().size() == 4 ); // New, separator, Export, Recent submenu.
    CHECK( pFile->toolTipsVisible() );
    CHECK( f.window.menuBar()->findChild<QMenu *>( QStringLiteral( "menu:File/Recent" ) ) != nullptr );
    QAction *pNew = EditorActions_Get( &f.actions, "file.new" );
    REQUIRE( pNew != nullptr );
    CHECK( pNew->text() == QStringLiteral( "New" ) );
    CHECK_FALSE( pNew->icon().isNull() );
    CHECK( pNew->isIconVisibleInMenu() );
    CHECK( pNew->isShortcutVisibleInContextMenu() );
    CHECK( EditorActions_Get( &f.actions, "debug.dump" ) == nullptr ); // Console-only.
    CHECK( EditorActions_Get( &f.actions, "no.such" ) == nullptr );
    CHECK_FALSE( EditorActions_Get( &f.actions, "file.export" )->isEnabled() );
}

TEST_CASE( "Command tooltips preserve descriptions and distinguish temporary unavailability", "[editor][gui][actions]" )
{
    fixture_t f;
    const command_desc_t commands[]{
        { "test.context", "Scale <Selection>", "Requires a selection & keeps its pivot.", nullptr, nullptr, COMMAND_FLAG_NONE,
          CountNew, []( void *pContext ) -> u32 {
              return static_cast<counters_t *>( pContext )->bGrid ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
          }, &f.counters },
        { "test.planned", "Planned", "Not connected yet: renderer preview.", nullptr, nullptr, COMMAND_FLAG_NONE, CountNew, Disabled, &f.counters },
    };
    REQUIRE( EditorCommands_Register( &f.registry, commands, std::size( commands ) ) == command_registry_status_t::OK );
    QAction *pContext = EditorActions_Get( &f.actions, "test.context" );
    REQUIRE( pContext != nullptr );
    CHECK( pContext->toolTip().contains( QStringLiteral( "Scale &lt;Selection&gt;" ) ) );
    CHECK( pContext->toolTip().contains( QStringLiteral( "Requires a selection &amp; keeps its pivot." ) ) );
    CHECK( pContext->toolTip().contains( QStringLiteral( "Currently unavailable." ) ) );
    CHECK_FALSE( pContext->toolTip().contains( QStringLiteral( "Not connected" ) ) );
    CHECK( pContext->statusTip() == QStringLiteral( "Requires a selection & keeps its pivot." ) );
    f.counters.bGrid = true;
    EditorActions_RefreshStates( &f.actions );
    CHECK( pContext->isEnabled() );
    CHECK_FALSE( pContext->toolTip().contains( QStringLiteral( "Currently unavailable." ) ) );

    QMenu contextMenu;
    const char *ids[]{ "test.context", "test.planned" };
    CHECK( EditorActions_FillMenu( &f.actions, &contextMenu, ids, std::size( ids ) ) == 0u );
    CHECK( contextMenu.toolTipsVisible() );
    CHECK( EditorActions_Get( &f.actions, "test.planned" )->toolTip().contains( QStringLiteral( "Not connected yet: renderer preview." ) ) );
}

TEST_CASE( "Triggering an action runs its command and reflects state", "[editor][gui][actions]" )
{
    fixture_t f;
    const char *toolbar[]{ "file.new", nullptr, "map.grid.toggle" };
    QToolBar *pToolBar = f.window.addToolBar( QStringLiteral( "Main" ) );
    CHECK( EditorActions_BuildToolBar( &f.actions, pToolBar, toolbar, std::size( toolbar ) ) == 0u );
    CHECK( pToolBar->actions().size() == 3 );
    EditorActions_Get( &f.actions, "file.new" )->trigger();
    CHECK( f.counters.nNew == 1 );
    QAction *pGrid = EditorActions_Get( &f.actions, "map.grid.toggle" );
    REQUIRE( pGrid->isCheckable() );
    CHECK_FALSE( pGrid->isChecked() );
    pGrid->trigger();
    CHECK( f.counters.bGrid );
    CHECK( pGrid->isChecked() );
    // State set elsewhere (console, script) shows after a refresh.
    f.counters.bGrid = false;
    EditorActions_RefreshStates( &f.actions );
    CHECK_FALSE( pGrid->isChecked() );
}

TEST_CASE( "Two-column tool palettes scroll without increasing the minimum window height", "[editor][gui][actions][palette]" )
{
    fixture_t f;
    auto *pToolbar = new QToolBar( &f.window );
    pToolbar->setObjectName( QStringLiteral( "EditorToolStrip" ) );
    pToolbar->setIconSize( QSize( 32, 32 ) );
    f.window.addToolBar( Qt::LeftToolBarArea, pToolbar );
    const char *commands[36];
    std::fill( std::begin( commands ), std::end( commands ), "file.new" );
    REQUIRE( EditorActions_BuildToolPalette( &f.actions, pToolbar, commands, std::size( commands ), 2 ) == 0u );
    f.window.resize( 500, 240 );
    f.window.show();
    QApplication::processEvents();
    auto *pScroll = pToolbar->findChild<QScrollArea *>( QStringLiteral( "EditorToolPaletteScroll" ) );
    REQUIRE( pScroll != nullptr );
    CHECK( pScroll->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff );
    CHECK( pScroll->minimumSizeHint().height() == 0 );
    CHECK( f.window.height() == 240 );
    CHECK( pScroll->width() < 120 );
    CHECK( pScroll->verticalScrollBar()->maximum() > 0 );
    QWidget *pPalette = pScroll->widget();
    REQUIRE( pPalette != nullptr );
    CHECK( pPalette->objectName() == QStringLiteral( "EditorToolPalette" ) );
    const QList<QToolButton *> buttons = pPalette->findChildren<QToolButton *>();
    REQUIRE( buttons.size() == 36 );
    for ( const QToolButton *pButton : buttons ) { CHECK( pButton->iconSize() == QSize( 32, 32 ) ); }
    pScroll->ensureWidgetVisible( buttons.last(), 0, 0 );
    QApplication::processEvents();
    const QRect last( buttons.last()->mapTo( pScroll->viewport(), QPoint() ), buttons.last()->size() );
    CHECK( pScroll->viewport()->rect().contains( last ) );
    pToolbar->setIconSize( QSize( 40, 40 ) );
    QApplication::processEvents();
    CHECK( pScroll->sizeHint().width() > pScroll->viewport()->width() );
    CHECK( buttons.last()->iconSize() == QSize( 40, 40 ) );
}

TEST_CASE( "Tool palette groups stay compact and refresh their theme spacing", "[editor][gui][actions][palette]" )
{
    fixture_t f;
    f.style.uiFont.setPointSizeF( 9.0 );
    f.style.density = 1.0;
    auto *pToolbar = new QToolBar( &f.window );
    pToolbar->setIconSize( QSize( 32, 32 ) );
    f.window.addToolBar( Qt::LeftToolBarArea, pToolbar );
    const char *commands[]{ nullptr, "file.new", "file.new", nullptr, nullptr,
                            "missing.command", nullptr, "map.grid.toggle", nullptr };
    REQUIRE( EditorActions_BuildToolPalette( &f.actions, pToolbar, commands, std::size( commands ), 2 ) == 1u );
    f.window.resize( 500, 800 );
    f.window.show();
    QApplication::processEvents();
    auto *pScroll = pToolbar->findChild<QScrollArea *>( QStringLiteral( "EditorToolPaletteScroll" ) );
    REQUIRE( pScroll != nullptr );
    QWidget *pPalette = pScroll->widget();
    auto *pGrid = qobject_cast<QGridLayout *>( pPalette->layout() );
    REQUIRE( pGrid != nullptr );
    const auto dividers = pPalette->findChildren<QFrame *>( QStringLiteral( "EditorToolPaletteDivider" ) );
    const auto buttons = pPalette->findChildren<QToolButton *>();
    REQUIRE( dividers.size() == 1 );
    REQUIRE( buttons.size() == 3 );
    CHECK( pGrid->spacing() == 1 );
    CHECK( pGrid->rowCount() == 3 ); // No unused rows from leading/trailing separators.
    CHECK( dividers.first()->height() == 3 );
    CHECK( buttons.first()->y() == pGrid->contentsMargins().top() );
    CHECK( buttons.last()->y() - buttons.first()->geometry().bottom() == 3 + 2 + 1 );
    CHECK( pScroll->verticalScrollBar()->maximum() == 0 );
    CHECK( pScroll->height() >= pToolbar->height() - 20 ); // The scroll surface uses the available rail height.
    f.style.metrics.insert( QStringLiteral( "ui.tool_strip.spacing" ), 4.0 );
    f.style.metrics.insert( QStringLiteral( "ui.tool_strip.separator_height" ), 7.0 );
    EditorActions_RefreshIcons( &f.actions );
    QApplication::processEvents();
    CHECK( pGrid->spacing() == 4 );
    CHECK( dividers.first()->height() == 7 );
    CHECK( buttons.last()->y() - buttons.first()->geometry().bottom() == 7 + 8 + 1 );
    CHECK( buttons.last()->iconSize() == QSize( 32, 32 ) );
}

TEST_CASE( "Checkable command glyphs distinguish off on and disabled after theme refresh", "[editor][gui][actions][icons]" )
{
    fixture_t f;
    const command_desc_t commands[]{
        { "view.console", "Console", nullptr, "view-console", nullptr, COMMAND_FLAG_CHECKABLE, ToggleGrid, GridState, &f.counters },
        { "map.grid.snap", "Snap", nullptr, "snap-grid", nullptr, COMMAND_FLAG_CHECKABLE, ToggleGrid, GridState, &f.counters },
        { "map.view.grid", "Grid", nullptr, "grid-show", nullptr, COMMAND_FLAG_CHECKABLE, ToggleGrid, GridState, &f.counters },
    };
    REQUIRE( EditorCommands_Register( &f.registry, commands, std::size( commands ) ) == command_registry_status_t::OK );
    // The neutral icon look (full colour is the default): off is grey,
    // checked brings the illustration's colour back.
    f.style.metrics.insert( QStringLiteral( "ui.icon.color_strength" ), 0.0 );
    EditorActions_RefreshIcons( &f.actions );
    const auto coloredPixels = []( const QImage &image ) {
        int count = 0;
        for ( int y = 0; y < image.height(); ++y ) {
            for ( int x = 0; x < image.width(); ++x ) {
                const QColor pixel = image.pixelColor( x, y );
                const int range = std::max( { pixel.red(), pixel.green(), pixel.blue() } ) - std::min( { pixel.red(), pixel.green(), pixel.blue() } );
                count += pixel.alpha() > 128 && range > 20 ? 1 : 0; // Hammer-toned icons carry muted hues.
            }
        }
        return count;
    };
    for ( const char *id : { "map.grid.toggle", "view.console", "map.grid.snap", "map.view.grid" } ) {
        INFO( id );
        QAction *pAction = EditorActions_Get( &f.actions, id );
        REQUIRE( pAction != nullptr );
        REQUIRE( pAction->isCheckable() );
        const auto raster = [pAction]( QIcon::Mode mode, QIcon::State state ) { return pAction->icon().pixmap( 32, mode, state ).toImage(); };
        const QImage off = raster( QIcon::Normal, QIcon::Off );
        const QImage on = raster( QIcon::Normal, QIcon::On );
        CHECK( coloredPixels( off ) == 0 );
        // Outline-only commands stay neutral; original illustrated assets show
        // their own colours when checked, never a recolouring by the UI accent.
        const bool illustrated = QString::fromLatin1( id ) != QStringLiteral( "map.grid.toggle" );
        if ( illustrated ) { CHECK( coloredPixels( on ) > 10 ); }
        else { CHECK( on == off ); }
        CHECK( coloredPixels( raster( QIcon::Disabled, QIcon::On ) ) == 0 );
        CHECK( raster( QIcon::Disabled, QIcon::Off ) != off );
        const QColor oldAccent = f.style.tokenColors.value( QStringLiteral( "ui.accent" ) );
        f.style.tokenColors.insert( QStringLiteral( "ui.accent" ), QColor( "#4bacff" ) );
        EditorActions_RefreshIcons( &f.actions );
        CHECK( raster( QIcon::Normal, QIcon::On ) == on );
        CHECK( raster( QIcon::Normal, QIcon::Off ) == off );
        f.style.tokenColors.insert( QStringLiteral( "ui.accent" ), oldAccent );
        f.style.metrics.insert( QStringLiteral( "ui.icon.checked_color_strength" ), 0.0 );
        EditorActions_RefreshIcons( &f.actions );
        CHECK( raster( QIcon::Normal, QIcon::On ) == off );
        f.style.metrics.insert( QStringLiteral( "ui.icon.checked_color_strength" ), 0.65 );
        EditorActions_RefreshIcons( &f.actions );
        CHECK( raster( QIcon::Normal, QIcon::On ) == on );
    }
}

TEST_CASE( "The keymap sets shortcuts, even for commands in no menu", "[editor][gui][actions]" )
{
    fixture_t f;
    settings_document_t keymap{};
    REQUIRE( SettingsDocument_Init( &keymap, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &keymap, StringView_FromCString( "@cykv 1\n@schema \"cypher.editor_keymap\" 1\n"
                                                                     "{ id = \"k\" bindings = { global = { \"file.new\" = [ \"Ctrl+N\" ] "
                                                                     "\"map.grid.toggle\" = [ \"Shift+R\", \"G\" ] \"debug.dump\" = [ \"F1\" ] } } }" ) )
                 .status == settings_document_status_t::OK );
    const key_value_t *chain[]{ SettingsDocument_Root( &keymap ) };
    EditorActions_ApplyKeymap( &f.actions, chain, 1u, StringView_FromCString( "global" ) );
    QAction *pGrid = f.actions.actions.value( QStringLiteral( "map.grid.toggle" ), nullptr );
    REQUIRE( pGrid != nullptr ); // Created because it is bound.
    CHECK( pGrid->shortcuts().size() == 2 );
    for ( const QKeySequence &binding : pGrid->shortcuts() ) {
        CHECK( pGrid->toolTip().contains( binding.toString( QKeySequence::NativeText ) ) );
    }
    CHECK( pGrid->shortcut() == QKeySequence( Qt::SHIFT | Qt::Key_R ) );
    CHECK( f.window.actions().contains( pGrid ) );
    QAction *pNew = EditorActions_Get( &f.actions, "file.new" );
    CHECK( pNew->shortcut() == QKeySequence( Qt::CTRL | Qt::Key_N ) );
    CHECK( pNew->toolTip().contains( pNew->shortcut().toString( QKeySequence::NativeText ) ) );
    CHECK( f.actions.actions.value( QStringLiteral( "debug.dump" ), nullptr ) == nullptr );

    // An empty chain clears shortcuts again.
    EditorActions_ApplyKeymap( &f.actions, nullptr, 0u, StringView_FromCString( "global" ) );
    CHECK( pNew->shortcut().isEmpty() );
    CHECK_FALSE( pGrid->toolTip().contains( QStringLiteral( " / " ) ) );
}
