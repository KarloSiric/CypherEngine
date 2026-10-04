//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Actions.cpp
//  Purpose: Implements command-driven actions, menus, toolbars, and the
//           key-chord to QKeySequence mapping.
//  Details: Actions execute through EditorCommands_Execute, so the registry
//           observer (console echo, logging, automation audit) sees menu
//           and shortcut use exactly like console use.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Actions.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QAction>
#include <QFrame>
#include <QGridLayout>
#include <QMenu>
#include <QMenuBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

// The toolbar must request only the palette's width, while allowing its
// height to shrink on a laptop. QScrollArea's generic size hint otherwise
// makes a narrow two-column tool strip hundreds of pixels wide.
class tool_palette_scroll_t final : public QScrollArea
{
public:
    explicit tool_palette_scroll_t( QWidget *pParent ) : QScrollArea( pParent ) {}
    QSize minimumSizeHint() const override { return QSize( PaletteWidth(), 0 ); }
    QSize sizeHint() const override { return QSize( PaletteWidth(), widget() != nullptr ? widget()->sizeHint().height() : 0 ); }

private:
    // Exactly the palette: no scroll bar is reserved (the wheel still
    // scrolls a short window), so the margins either side are equal.
    int PaletteWidth() const { return ( widget() != nullptr ? widget()->sizeHint().width() : 0 ) + frameWidth() * 2; }
};

void ApplyToolPaletteMetrics( QWidget *pPalette, const editor_style_t &style )
{
    auto *pGrid = qobject_cast<QGridLayout *>( pPalette->layout() );
    if ( pGrid == nullptr ) { return; }
    const double pointSize = style.uiFont.pointSizeF() > 0.0 ? style.uiFont.pointSizeF() : 11.0;
    const double scale = pointSize / 9.0 * style.density;
    const int spacing = qRound( EditorStyle_Metric( style, "ui.tool_strip.spacing", 1.0 ) * scale );
    pGrid->setSpacing( spacing );
    // The same gap outside the buttons as between them: a symmetric rail.
    pGrid->setContentsMargins( spacing + 1, spacing + 1, spacing + 1, spacing + 1 );
    const int dividerHeight = qMax( 1, qRound( EditorStyle_Metric( style, "ui.tool_strip.separator_height", 3.0 ) * scale ) );
    for ( QFrame *pDivider : pPalette->findChildren<QFrame *>( QStringLiteral( "EditorToolPaletteDivider" ) ) ) {
        pDivider->setFixedHeight( dividerHeight );
    }
    pPalette->updateGeometry();
    if ( QWidget *pViewport = pPalette->parentWidget() ) {
        if ( auto *pScroll = qobject_cast<QScrollArea *>( pViewport->parentWidget() ) ) { pScroll->updateGeometry(); }
    }
}

struct named_key_t {
    u16 key;
    int qtKey;
};

// Named keymap keys and their Qt keys. Printable keys share codes: the
// keymap stores upper-case ASCII, which is what Qt::Key uses for them.
constexpr named_key_t kNamedKeys[]{
    { KEY_ESCAPE, Qt::Key_Escape }, { KEY_TAB, Qt::Key_Tab },         { KEY_BACKSPACE, Qt::Key_Backspace },
    { KEY_ENTER, Qt::Key_Return },  { KEY_INSERT, Qt::Key_Insert },   { KEY_DELETE, Qt::Key_Delete },
    { KEY_HOME, Qt::Key_Home },     { KEY_END, Qt::Key_End },         { KEY_PAGE_UP, Qt::Key_PageUp },
    { KEY_PAGE_DOWN, Qt::Key_PageDown }, { KEY_LEFT, Qt::Key_Left },  { KEY_RIGHT, Qt::Key_Right },
    { KEY_UP, Qt::Key_Up },         { KEY_DOWN, Qt::Key_Down },
};

// Keypad keys carry Qt::KeypadModifier; the keymap names them separately.
constexpr named_key_t kKeypadKeys[]{
    { KEY_NUMPAD_ADD, Qt::Key_Plus },       { KEY_NUMPAD_SUBTRACT, Qt::Key_Minus }, { KEY_NUMPAD_MULTIPLY, Qt::Key_Asterisk },
    { KEY_NUMPAD_DIVIDE, Qt::Key_Slash },   { KEY_NUMPAD_DECIMAL, Qt::Key_Period }, { KEY_NUMPAD_ENTER, Qt::Key_Enter },
};

QKeyCombination StrokeToCombination( const key_stroke_t &stroke ) noexcept
{
    Qt::KeyboardModifiers modifiers{};
    if ( ( stroke.modifiers & KEY_MODIFIER_CTRL ) != 0u ) { modifiers |= Qt::ControlModifier; }
    if ( ( stroke.modifiers & KEY_MODIFIER_ALT ) != 0u ) { modifiers |= Qt::AltModifier; }
    if ( ( stroke.modifiers & KEY_MODIFIER_SHIFT ) != 0u ) { modifiers |= Qt::ShiftModifier; }
    if ( ( stroke.modifiers & KEY_MODIFIER_META ) != 0u ) { modifiers |= Qt::MetaModifier; }
    int qtKey = 0;
    if ( stroke.key < KEY_NAMED_BASE ) {
        qtKey = stroke.key;
    } else if ( stroke.key >= KEY_F1 && stroke.key < KEY_F1 + 24u ) {
        qtKey = Qt::Key_F1 + ( stroke.key - KEY_F1 );
    } else if ( stroke.key >= KEY_NUMPAD_0 && stroke.key <= KEY_NUMPAD_0 + 9u ) {
        qtKey = Qt::Key_0 + ( stroke.key - KEY_NUMPAD_0 );
        modifiers |= Qt::KeypadModifier;
    } else {
        for ( const named_key_t &named : kNamedKeys ) {
            if ( named.key == stroke.key ) { qtKey = named.qtKey; }
        }
        for ( const named_key_t &keypad : kKeypadKeys ) {
            if ( keypad.key == stroke.key ) {
                qtKey = keypad.qtKey;
                modifiers |= Qt::KeypadModifier;
            }
        }
    }
    return QKeyCombination( modifiers, static_cast<Qt::Key>( qtKey ) );
}

bool CombinationToStroke( QKeyCombination combination, key_stroke_t &strokeOut ) noexcept
{
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    const int qtKey = combination.key();
    strokeOut = key_stroke_t{};
    if ( modifiers.testFlag( Qt::ControlModifier ) ) { strokeOut.modifiers |= KEY_MODIFIER_CTRL; }
    if ( modifiers.testFlag( Qt::AltModifier ) ) { strokeOut.modifiers |= KEY_MODIFIER_ALT; }
    if ( modifiers.testFlag( Qt::ShiftModifier ) ) { strokeOut.modifiers |= KEY_MODIFIER_SHIFT; }
    if ( modifiers.testFlag( Qt::MetaModifier ) ) { strokeOut.modifiers |= KEY_MODIFIER_META; }
    // Qt can deliver Shift+Tab as Backtab rather than Tab with Shift. The
    // keymap has one Tab identity; preserve every other modifier and make
    // Backtab's implicit Shift explicit for lookup and canonical round trips.
    if ( qtKey == Qt::Key_Backtab ) {
        strokeOut.key = KEY_TAB;
        strokeOut.modifiers |= KEY_MODIFIER_SHIFT;
        return true;
    }
    if ( modifiers.testFlag( Qt::KeypadModifier ) ) {
        if ( qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9 ) {
            strokeOut.key = static_cast<u16>( KEY_NUMPAD_0 + ( qtKey - Qt::Key_0 ) );
            return true;
        }
        for ( const named_key_t &keypad : kKeypadKeys ) {
            if ( keypad.qtKey == qtKey ) {
                strokeOut.key = keypad.key;
                return true;
            }
        }
    }
    if ( qtKey > 0x20 && qtKey < 0x7F ) {
        // Letters are stored upper-case; Qt already reports them that way.
        strokeOut.key = static_cast<u16>( qtKey );
        return true;
    }
    if ( qtKey == Qt::Key_Space ) {
        strokeOut.key = KEY_SPACE;
        return true;
    }
    if ( qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24 ) {
        strokeOut.key = static_cast<u16>( KEY_F1 + ( qtKey - Qt::Key_F1 ) );
        return true;
    }
    for ( const named_key_t &named : kNamedKeys ) {
        if ( named.qtKey == qtKey ) {
            strokeOut.key = named.key;
            return true;
        }
    }
    if ( qtKey == Qt::Key_Enter ) { // Main Enter and Return are one key to the keymap.
        strokeOut.key = KEY_ENTER;
        return true;
    }
    return false;
}

QString CommandId( const command_desc_t &command )
{
    return QString::fromUtf8( command.pId );
}

void UpdateToolTip( QAction *pAction, const command_desc_t &command )
{
    QString tip = QStringLiteral( "<b>%1</b>" ).arg( QString::fromUtf8( command.pLabel ).toHtmlEscaped() );
    QStringList bindings;
    for ( const QKeySequence &shortcut : pAction->shortcuts() ) {
        if ( !shortcut.isEmpty() ) { bindings.append( shortcut.toString( QKeySequence::NativeText ).toHtmlEscaped() ); }
    }
    if ( !bindings.isEmpty() ) { tip += QStringLiteral( "  (%1)" ).arg( bindings.join( QStringLiteral( " / " ) ) ); }
    if ( command.pDescription != nullptr && command.pDescription[0] != '\0' ) {
        tip += QStringLiteral( "<br/>" ) + QString::fromUtf8( command.pDescription ).toHtmlEscaped().replace( QLatin1Char( '\n' ), QStringLiteral( "<br/>" ) );
    }
    // A disabled command may merely need a selection or an open document.
    // Only its owning module can explain missing implementation in its description.
    if ( !pAction->isEnabled() ) { tip += QStringLiteral( "<br/><i>Currently unavailable.</i>" ); }
    pAction->setToolTip( tip );
}

QMenu *FindOrAddMenu( QMenuBar *pMenuBar, const QString &path )
{
    const QStringList segments = path.split( QLatin1Char( '/' ), Qt::SkipEmptyParts );
    CY_ASSERT( !segments.isEmpty() );
    QMenu *pMenu = nullptr;
    QString prefix;
    for ( const QString &segment : segments ) {
        prefix += ( prefix.isEmpty() ? QString() : QStringLiteral( "/" ) ) + segment;
        const QString name = QStringLiteral( "menu:" ) + prefix;
        QMenu *pFound = pMenuBar->findChild<QMenu *>( name );
        if ( pFound == nullptr ) {
            pFound = pMenu == nullptr ? pMenuBar->addMenu( segment ) : pMenu->addMenu( segment );
            pFound->setObjectName( name );
            pFound->setToolTipsVisible( true );
        }
        pMenu = pFound;
    }
    return pMenu;
}

} // namespace

void EditorActions_Init( editor_actions_t *pActions, const command_registry_t *pRegistry, const editor_style_t *pStyle, QWidget *pOwner ) noexcept
{
    CY_ASSERT( pActions != nullptr && pRegistry != nullptr && pStyle != nullptr && pOwner != nullptr );
    pActions->pRegistry = pRegistry;
    pActions->pStyle = pStyle;
    pActions->pOwner = pOwner;
    pActions->actions.clear();
}

QAction *EditorActions_Get( editor_actions_t *pActions, const char *pCommand )
{
    CY_ASSERT( pActions != nullptr && pActions->pRegistry != nullptr );
    const QString id = QString::fromUtf8( pCommand );
    if ( QAction *pExisting = pActions->actions.value( id, nullptr ) ) { return pExisting; }
    const command_desc_t *pDesc = EditorCommands_Find( pActions->pRegistry, StringView_FromCString( pCommand ) );
    if ( pDesc == nullptr || ( pDesc->flags & COMMAND_FLAG_CONSOLE_ONLY ) != 0u ) { return nullptr; }

    QAction *pAction = new QAction( QString::fromUtf8( pDesc->pLabel ), pActions->pOwner );
    pAction->setObjectName( id );
    pAction->setCheckable( ( pDesc->flags & COMMAND_FLAG_CHECKABLE ) != 0u );
    pAction->setIconVisibleInMenu( true );
    pAction->setShortcutVisibleInContextMenu( true );
    if ( pDesc->pDescription != nullptr ) { pAction->setStatusTip( QString::fromUtf8( pDesc->pDescription ) ); }
    if ( pDesc->pIcon != nullptr ) {
        const bool accentChecked = pAction->isCheckable();
        pAction->setIcon( EditorStyle_Icon( *pActions->pStyle, pDesc->pIcon, accentChecked ) );
    }
    // Added to the owner so its shortcut is live even outside menus.
    pActions->pOwner->addAction( pAction );
    const command_registry_t *pRegistry = pActions->pRegistry;
    // The ID's storage is the command table's, which outlives the action.
    const char *pId = pDesc->pId;
    QObject::connect( pAction, &QAction::triggered, pAction, [pActions, pRegistry, pId]() {
        const command_result_t result = EditorCommands_Execute( pRegistry, StringView_FromCString( pId ), command_args_t{} );
        if ( result != command_result_t::OK && result != command_result_t::DISABLED ) {
            CY_LOG_WRITE( Warning, Editor, "A menu or shortcut command did not complete" );
        }
        EditorActions_RefreshStates( pActions );
    } );
    pActions->actions.insert( id, pAction );
    // Born showing the command's state, not Qt's enabled default.
    const u32 state = EditorCommands_State( pRegistry, StringView_FromCString( pId ) );
    pAction->setEnabled( ( state & COMMAND_STATE_ENABLED ) != 0u );
    if ( pAction->isCheckable() ) {
        const QSignalBlocker blocker( pAction );
        pAction->setChecked( ( state & COMMAND_STATE_CHECKED ) != 0u );
    }
    UpdateToolTip( pAction, *pDesc );
    return pAction;
}

void EditorActions_ApplyKeymap( editor_actions_t *pActions, const key_value_t *const *ppChain, usize nChain, string_view_t context )
{
    EditorActions_ApplyKeymapStack( pActions, ppChain, nChain, keymap_platform_t::NONE, &context, 1u );
}

void EditorActions_ApplyKeymapStack(
    editor_actions_t *pActions,
    const key_value_t *const *ppChain,
    usize nChain,
    keymap_platform_t platform,
    const string_view_t *pContexts,
    usize nContexts )
{
    CY_ASSERT( pActions != nullptr && pActions->pRegistry != nullptr && ( pContexts != nullptr || nContexts == 0u ) );
    const usize nCommands = EditorCommands_Count( pActions->pRegistry );
    for ( usize i = 0u; i < nCommands; ++i ) {
        const command_desc_t *pDesc = EditorCommands_At( pActions->pRegistry, i );
        // The most specific context that mentions the command decides, so a
        // workspace context can rebind or unbind a global shortcut.
        keymap_binding_t binding{};
        keymap_lookup_t lookup = keymap_lookup_t::NOT_DEFINED;
        usize nInvalid = 0u;
        for ( usize iContext = 0u; iContext < nContexts && lookup == keymap_lookup_t::NOT_DEFINED; ++iContext ) {
            lookup = EditorKeymap_FindBindingOn( ppChain, nChain, platform, pContexts[iContext], StringView_FromCString( pDesc->pId ), &binding );
            nInvalid += binding.nInvalidChords;
        }
        QAction *pAction = pActions->actions.value( CommandId( *pDesc ), nullptr );
        if ( lookup == keymap_lookup_t::BOUND && pAction == nullptr ) { pAction = EditorActions_Get( pActions, pDesc->pId ); }
        if ( pAction == nullptr ) { continue; }
        QList<QKeySequence> shortcuts;
        if ( lookup == keymap_lookup_t::BOUND ) {
            for ( usize iChord = 0u; iChord < binding.nChords; ++iChord ) {
                shortcuts.append( EditorKeyChord_ToKeySequence( binding.chords[iChord] ) );
            }
        }
        if ( nInvalid != 0u ) {
            CY_LOG_WRITE( Warning, Editor, "Keymap has chords that could not be read; they were skipped" );
        }
        pAction->setShortcuts( shortcuts );
        UpdateToolTip( pAction, *pDesc );
    }
}

usize EditorActions_BuildMenus( editor_actions_t *pActions, QMenuBar *pMenuBar, const editor_menu_item_t *pItems, usize nItems )
{
    CY_ASSERT( pActions != nullptr && pMenuBar != nullptr && ( pItems != nullptr || nItems == 0u ) );
    usize nSkipped = 0u;
    for ( usize i = 0u; i < nItems; ++i ) {
        QMenu *pMenu = FindOrAddMenu( pMenuBar, QString::fromUtf8( pItems[i].pMenu ) );
        if ( pItems[i].pCommand == nullptr ) {
            pMenu->addSeparator();
            continue;
        }
        QAction *pAction = EditorActions_Get( pActions, pItems[i].pCommand );
        if ( pAction == nullptr ) {
            ++nSkipped;
            continue;
        }
        pMenu->addAction( pAction );
    }
    // Menus show current state whenever they open.
    for ( QMenu *pMenu : pMenuBar->findChildren<QMenu *>() ) {
        QObject::disconnect( pMenu, &QMenu::aboutToShow, nullptr, nullptr );
        QObject::connect( pMenu, &QMenu::aboutToShow, pMenu, [pActions]() { EditorActions_RefreshStates( pActions ); } );
    }
    if ( nSkipped != 0u ) { CY_LOG_WRITE( Info, Editor, "Menu entries for unregistered commands were skipped" ); }
    return nSkipped;
}

usize EditorActions_FillMenu( editor_actions_t *pActions, QMenu *pMenu, const char *const *ppCommands, usize nCommands )
{
    CY_ASSERT( pActions != nullptr && pMenu != nullptr );
    pMenu->setToolTipsVisible( true );
    usize nSkipped = 0u;
    for ( usize i = 0u; i < nCommands; ++i ) {
        if ( ppCommands[i] == nullptr ) {
            pMenu->addSeparator();
            continue;
        }
        QAction *pAction = EditorActions_Get( pActions, ppCommands[i] );
        if ( pAction == nullptr ) {
            ++nSkipped;
            continue;
        }
        pMenu->addAction( pAction );
    }
    EditorActions_RefreshStates( pActions );
    return nSkipped;
}

usize EditorActions_BuildToolBar( editor_actions_t *pActions, QToolBar *pToolBar, const char *const *ppCommands, usize nCommands )
{
    CY_ASSERT( pActions != nullptr && pToolBar != nullptr );
    usize nSkipped = 0u;
    for ( usize i = 0u; i < nCommands; ++i ) {
        if ( ppCommands[i] == nullptr ) {
            pToolBar->addSeparator();
            continue;
        }
        QAction *pAction = EditorActions_Get( pActions, ppCommands[i] );
        if ( pAction == nullptr ) {
            ++nSkipped;
            continue;
        }
        pToolBar->addAction( pAction );
    }
    EditorActions_RefreshStates( pActions );
    return nSkipped;
}

usize EditorActions_BuildToolPalette( editor_actions_t *pActions, QToolBar *pToolBar, const char *const *ppCommands, usize nCommands, int nColumns )
{
    CY_ASSERT( pActions != nullptr && pToolBar != nullptr && nColumns > 0 );
    auto *pPalette = new QWidget( pToolBar );
    pPalette->setObjectName( QStringLiteral( "EditorToolPalette" ) );
    auto *pGrid = new QGridLayout( pPalette );
    pGrid->setContentsMargins( 1, 1, 1, 1 );
    pGrid->setAlignment( Qt::AlignTop );
    usize nSkipped = 0u;
    int row = 0;
    int column = 0;
    bool dividerPending = false;
    QList<QToolButton *> buttons;
    for ( usize i = 0u; i < nCommands; ++i ) {
        if ( ppCommands[i] == nullptr ) {
            dividerPending = !buttons.isEmpty();
            continue;
        }
        QAction *pAction = EditorActions_Get( pActions, ppCommands[i] );
        if ( pAction == nullptr ) {
            ++nSkipped;
            continue;
        }
        // Add one compact divider only between actual tool groups. Leading,
        // repeated and trailing separator entries must not consume rail space.
        if ( dividerPending ) {
            if ( column != 0 ) { ++row; }
            auto *pDivider = new QFrame( pPalette );
            pDivider->setObjectName( QStringLiteral( "EditorToolPaletteDivider" ) );
            pDivider->setFrameShape( QFrame::HLine );
            pGrid->addWidget( pDivider, row++, 0, 1, nColumns );
            column = 0;
            dividerPending = false;
        }
        auto *pButton = new QToolButton( pPalette );
        pButton->setDefaultAction( pAction );
        pButton->setProperty( "authoringTool", pAction->objectName().startsWith( QLatin1String( "map.tool." ) ) );
        pButton->setAutoRaise( true );
        pButton->setIconSize( pToolBar->iconSize() );
        pButton->setToolButtonStyle( Qt::ToolButtonIconOnly );
        pGrid->addWidget( pButton, row, column );
        buttons.append( pButton );
        if ( ++column == nColumns ) {
            column = 0;
            ++row;
        }
    }
    auto *pScroll = new tool_palette_scroll_t( pToolBar );
    pScroll->setObjectName( QStringLiteral( "EditorToolPaletteScroll" ) );
    pScroll->setFrameShape( QFrame::NoFrame );
    pScroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    pScroll->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    pScroll->setWidgetResizable( true );
    pScroll->setAlignment( Qt::AlignHCenter | Qt::AlignTop );
    pScroll->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Expanding );
    pScroll->setFocusPolicy( Qt::NoFocus );
    pPalette->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Minimum );
    pScroll->setWidget( pPalette );
    ApplyToolPaletteMetrics( pPalette, *pActions->pStyle );
    QObject::connect( pToolBar, &QToolBar::iconSizeChanged, pPalette, [buttons, pScroll]( const QSize &size ) {
        for ( QToolButton *pButton : buttons ) { pButton->setIconSize( size ); }
        pScroll->updateGeometry();
    } );
    pToolBar->addWidget( pScroll );
    EditorActions_RefreshStates( pActions );
    return nSkipped;
}

void EditorActions_RefreshStates( editor_actions_t *pActions )
{
    CY_ASSERT( pActions != nullptr && pActions->pRegistry != nullptr );
    for ( auto it = pActions->actions.cbegin(); it != pActions->actions.cend(); ++it ) {
        QAction *pAction = it.value();
        const QByteArray id = it.key().toUtf8();
        const u32 state = EditorCommands_State( pActions->pRegistry, { id.constData(), static_cast<usize>( id.size() ) } );
        const bool enabledChanged = pAction->isEnabled() != ( ( state & COMMAND_STATE_ENABLED ) != 0u );
        pAction->setEnabled( ( state & COMMAND_STATE_ENABLED ) != 0u );
        if ( enabledChanged ) {
            const command_desc_t *pDesc = EditorCommands_Find( pActions->pRegistry, { id.constData(), static_cast<usize>( id.size() ) } );
            if ( pDesc != nullptr ) { UpdateToolTip( pAction, *pDesc ); }
        }
        if ( pAction->isCheckable() ) {
            // Blocked so reflecting state never re-runs the command.
            const QSignalBlocker blocker( pAction );
            pAction->setChecked( ( state & COMMAND_STATE_CHECKED ) != 0u );
        }
    }
}

void EditorActions_RefreshIcons( editor_actions_t *pActions )
{
    CY_ASSERT( pActions != nullptr && pActions->pRegistry != nullptr );
    for ( auto it = pActions->actions.cbegin(); it != pActions->actions.cend(); ++it ) {
        const QByteArray id = it.key().toUtf8();
        const command_desc_t *pDesc = EditorCommands_Find( pActions->pRegistry, { id.constData(), static_cast<usize>( id.size() ) } );
        if ( pDesc != nullptr && pDesc->pIcon != nullptr ) {
            const bool accentChecked = it.value()->isCheckable();
            it.value()->setIcon( EditorStyle_Icon( *pActions->pStyle, pDesc->pIcon, accentChecked ) );
        }
    }
    for ( QWidget *pPalette : pActions->pOwner->findChildren<QWidget *>( QStringLiteral( "EditorToolPalette" ) ) ) {
        ApplyToolPaletteMetrics( pPalette, *pActions->pStyle );
    }
}

QKeySequence EditorKeyChord_ToKeySequence( const key_chord_t &chord )
{
    QKeyCombination combinations[EDITOR_KEY_CHORD_MAX_STROKES]{};
    const usize nStrokes = chord.nStrokes < EDITOR_KEY_CHORD_MAX_STROKES ? chord.nStrokes : EDITOR_KEY_CHORD_MAX_STROKES;
    for ( usize i = 0u; i < nStrokes; ++i ) { combinations[i] = StrokeToCombination( chord.strokes[i] ); }
    switch ( nStrokes ) {
        case 1u: return QKeySequence( combinations[0] );
        case 2u: return QKeySequence( combinations[0], combinations[1] );
        case 3u: return QKeySequence( combinations[0], combinations[1], combinations[2] );
        case 4u: return QKeySequence( combinations[0], combinations[1], combinations[2], combinations[3] );
        default: return {};
    }
}

bool EditorKeyChord_FromKeySequence( const QKeySequence &sequence, key_chord_t *pChordOut )
{
    CY_ASSERT( pChordOut != nullptr );
    key_chord_t chord{};
    const int nStrokes = sequence.count();
    if ( nStrokes <= 0 || nStrokes > static_cast<int>( EDITOR_KEY_CHORD_MAX_STROKES ) ) { return false; }
    for ( int i = 0; i < nStrokes; ++i ) {
        if ( !CombinationToStroke( sequence[static_cast<uint>( i )], chord.strokes[i] ) ) { return false; }
    }
    chord.nStrokes = static_cast<u8>( nStrokes );
    *pChordOut = chord;
    return true;
}

} // namespace cypher::editor::gui
