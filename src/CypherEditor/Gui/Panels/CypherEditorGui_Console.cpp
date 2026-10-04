//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Console.cpp
//  Purpose: Implements the editor console panel.
//  Details: Per-console state is a QObject child of the widget, so it lives
//           exactly as long as the widget without a widget subclass. The
//           view is rebuilt from the store only when a filter changes; new
//           records are appended one by one.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - 2026-09-30: rebuilt on the log store with filters, counts, and uptime
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_Console.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QStringListModel>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

constexpr const char *kStateName = "EditorConsoleState";

struct console_state_t final : QObject {
    ~console_state_t() override
    {
        if ( pLog != nullptr ) { EditorLog_RemoveListener( pLog, &console_state_t::OnLog, this ); }
    }

    static void OnLog( void *pContext, const editor_log_entry_t *pEntry ) noexcept;

    QPlainTextEdit *pOutput{ nullptr };
    QLineEdit *pInput{ nullptr };
    QLineEdit *pSearch{ nullptr };
    QComboBox *pChannel{ nullptr };
    QToolButton *pMessages{ nullptr };
    QToolButton *pWarnings{ nullptr };
    QToolButton *pErrors{ nullptr };
    QToolButton *pTime{ nullptr };
    QStringListModel *pCompletions{ nullptr };
    const command_registry_t *pRegistry{ nullptr };
    const editor_style_t *pStyle{ nullptr };
    editor_log_t *pLog{ nullptr };
    QStringList history{};
    int iHistory{ 0 }; // history.size() means "the line being typed".
};

console_state_t *State( QWidget *pConsole )
{
    CY_ASSERT( pConsole != nullptr );
    // Looked up as QObject: typed findChild needs Q_OBJECT, and the unique
    // object name already identifies the state this file created.
    console_state_t *pState = static_cast<console_state_t *>(
        pConsole->findChild<QObject *>( QLatin1String( kStateName ), Qt::FindDirectChildrenOnly ) );
    CY_ASSERT_MSG( pState != nullptr, "Widget was not created by EditorConsole_Create" );
    return pState;
}

// The console's colours are theme tokens (CYTHEME.md 4.8).
QColor LevelColor( const editor_style_t &style, log_level_t level )
{
    switch ( level ) {
        case log_level_t::Trace:
        case log_level_t::Debug: return EditorStyle_TokenColor( style, "console.muted" );
        case log_level_t::Warning: return EditorStyle_TokenColor( style, "console.warning" );
        case log_level_t::Error:
        case log_level_t::Fatal: return EditorStyle_TokenColor( style, "console.error" );
        default: return EditorStyle_TokenColor( style, "console.text" );
    }
}

bool Passes( const console_state_t &state, const editor_log_entry_t &entry )
{
    const bool bError = entry.level >= log_level_t::Error;
    const bool bWarning = entry.level == log_level_t::Warning;
    if ( bError && !state.pErrors->isChecked() ) { return false; }
    if ( bWarning && !state.pWarnings->isChecked() ) { return false; }
    if ( !bError && !bWarning && !state.pMessages->isChecked() ) { return false; }
    const int channel = state.pChannel->currentData().toInt();
    if ( channel >= 0 && static_cast<int>( entry.channel ) != channel ) { return false; }
    const QString search = state.pSearch->text().trimmed();
    return search.isEmpty() || entry.message.contains( search, Qt::CaseInsensitive );
}

void AppendEntry( console_state_t &state, const editor_log_entry_t &entry )
{
    const editor_style_t &style = *state.pStyle;
    QTextCursor cursor( state.pOutput->document() );
    cursor.movePosition( QTextCursor::End );
    if ( !state.pOutput->document()->isEmpty() ) { cursor.insertBlock(); }
    QTextCharFormat muted;
    muted.setForeground( EditorStyle_TokenColor( style, "console.muted" ) );
    if ( state.pTime->isChecked() ) {
        cursor.insertText( QStringLiteral( "[%1] " ).arg( entry.msTime / 1000.0, 8, 'f', 3 ), muted );
    }
    if ( entry.bCommand ) {
        QTextCharFormat command;
        command.setForeground( EditorStyle_TokenColor( style, "console.command" ) );
        cursor.insertText( entry.message, command );
        return;
    }
    QTextCharFormat tag;
    tag.setForeground( LevelColor( style, entry.level ) );
    tag.setFontWeight( QFont::Bold );
    cursor.insertText( QStringLiteral( "%1 " ).arg( QString::fromLatin1( EditorLog_LevelTag( entry.level ) ), -5 ), tag );
    cursor.insertText( QStringLiteral( "[%1] " ).arg( QString::fromUtf8( Cy_LogChannelName( entry.channel ) ) ), muted );
    QTextCharFormat message;
    message.setForeground( LevelColor( style, entry.level ) );
    cursor.insertText( entry.message, message );
}

void UpdateCounts( console_state_t &state )
{
    const usize nWarnings = EditorLog_Count( state.pLog, log_level_t::Warning );
    const usize nErrors = EditorLog_Count( state.pLog, log_level_t::Error ) + EditorLog_Count( state.pLog, log_level_t::Fatal );
    const usize nMessages = static_cast<usize>( state.pLog->entries.size() ) >= nWarnings + nErrors
                                ? static_cast<usize>( state.pLog->entries.size() ) - nWarnings - nErrors
                                : 0u;
    // Icon and count only, an IDE error list's way: the console often shares
    // the bottom row with the asset browser and must not elide its counts.
    state.pMessages->setText( QString::number( nMessages ) );
    state.pWarnings->setText( QString::number( nWarnings ) );
    state.pErrors->setText( QString::number( nErrors ) );
    state.pMessages->setToolTip( QStringLiteral( "%1 messages; click to show or hide them" ).arg( nMessages ) );
    state.pWarnings->setToolTip( QStringLiteral( "%1 warnings; click to show or hide them" ).arg( nWarnings ) );
    state.pErrors->setToolTip( QStringLiteral( "%1 errors; click to show or hide them" ).arg( nErrors ) );
}

void AddChannel( console_state_t &state, log_channel_t channel )
{
    if ( state.pChannel->findData( static_cast<int>( channel ) ) >= 0 ) { return; }
    const QSignalBlocker blocker( state.pChannel );
    state.pChannel->addItem( QString::fromUtf8( Cy_LogChannelName( channel ) ), static_cast<int>( channel ) );
}

void Rebuild( console_state_t &state )
{
    state.pOutput->clear();
    for ( const editor_log_entry_t &entry : state.pLog->entries ) {
        AddChannel( state, entry.channel );
        if ( Passes( state, entry ) ) { AppendEntry( state, entry ); }
    }
    state.pOutput->verticalScrollBar()->setValue( state.pOutput->verticalScrollBar()->maximum() );
    UpdateCounts( state );
}

void console_state_t::OnLog( void *pContext, const editor_log_entry_t *pEntry ) noexcept
{
    auto &state = *static_cast<console_state_t *>( pContext );
    if ( pEntry == nullptr ) {
        state.pOutput->clear();
        UpdateCounts( state );
        return;
    }
    AddChannel( state, pEntry->channel );
    UpdateCounts( state );
    if ( !Passes( state, *pEntry ) ) { return; }
    QScrollBar *pScroll = state.pOutput->verticalScrollBar();
    // Follow new output only when already at the bottom, so reading older
    // lines is not interrupted.
    const bool bAtBottom = pScroll->value() >= pScroll->maximum() - 2;
    AppendEntry( state, *pEntry );
    if ( bAtBottom ) { pScroll->setValue( pScroll->maximum() ); }
}

void RefreshCompletions( console_state_t *pState )
{
    QStringList ids;
    const usize nCommands = EditorCommands_Count( pState->pRegistry );
    ids.reserve( static_cast<qsizetype>( nCommands ) );
    for ( usize i = 0u; i < nCommands; ++i ) { ids.append( QString::fromUtf8( EditorCommands_At( pState->pRegistry, i )->pId ) ); }
    pState->pCompletions->setStringList( ids );
}

void StepHistory( console_state_t *pState, int step )
{
    if ( pState->history.isEmpty() ) { return; }
    pState->iHistory = std::clamp( pState->iHistory + step, 0, static_cast<int>( pState->history.size() ) );
    pState->pInput->setText( pState->iHistory < pState->history.size() ? pState->history[pState->iHistory] : QString() );
}

QToolButton *FilterButton( QWidget *pParent, const editor_style_t &style, const char *pIcon, const QString &tip )
{
    auto *pButton = new QToolButton( pParent );
    pButton->setCheckable( true );
    pButton->setChecked( true );
    pButton->setAutoRaise( true );
    pButton->setIcon( EditorStyle_Icon( style, pIcon ) );
    pButton->setIconSize( QSize( 16, 16 ) );
    pButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    pButton->setToolTip( tip );
    return pButton;
}

command_result_t ConsoleClear( void *pContext, const command_args_t & )
{
    EditorLog_Clear( State( static_cast<QWidget *>( pContext ) )->pLog );
    return command_result_t::OK;
}

command_result_t ConsoleHelp( void *pContext, const command_args_t &args )
{
    QWidget *pConsole = static_cast<QWidget *>( pContext );
    console_state_t *pState = State( pConsole );
    const QString filter = args.nArgs > 0u ? QString::fromUtf8( args.pArgs[0].pData, static_cast<qsizetype>( args.pArgs[0].cchLength ) ) : QString();
    const usize nCommands = EditorCommands_Count( pState->pRegistry );
    for ( usize i = 0u; i < nCommands; ++i ) {
        const command_desc_t *pDesc = EditorCommands_At( pState->pRegistry, i );
        const QString id = QString::fromUtf8( pDesc->pId );
        if ( !filter.isEmpty() && !id.startsWith( filter ) ) { continue; }
        QString line = QStringLiteral( "  %1" ).arg( pDesc->pUsage != nullptr ? QString::fromUtf8( pDesc->pUsage ) : id, -32 );
        if ( pDesc->pDescription != nullptr ) { line += QStringLiteral( "  " ) + QString::fromUtf8( pDesc->pDescription ); }
        EditorConsole_Append( pConsole, log_level_t::Info, line );
    }
    return command_result_t::OK;
}

} // namespace

QWidget *EditorConsole_Create( QWidget *pParent, const command_registry_t *pRegistry, const editor_style_t *pStyle, editor_log_t *pLog )
{
    CY_ASSERT( pRegistry != nullptr && pStyle != nullptr && pLog != nullptr );
    QWidget *pConsole = new QWidget( pParent );
    pConsole->setObjectName( QStringLiteral( "EditorConsole" ) );
    console_state_t *pState = new console_state_t();
    pState->setParent( pConsole );
    pState->setObjectName( QLatin1String( kStateName ) );
    pState->pRegistry = pRegistry;
    pState->pStyle = pStyle;
    pState->pLog = pLog;

    // Filter bar: level toggles with counts, channel, search, time, actions.
    auto *pBar = new QWidget( pConsole );
    pBar->setObjectName( QStringLiteral( "EditorConsoleToolbar" ) );
    auto *pBarLayout = new QHBoxLayout( pBar );
    pBarLayout->setContentsMargins( 4, 2, 4, 2 );
    pBarLayout->setSpacing( 2 );
    pState->pMessages = FilterButton( pBar, *pStyle, "log-info", QStringLiteral( "Show messages" ) );
    pState->pWarnings = FilterButton( pBar, *pStyle, "log-warning", QStringLiteral( "Show warnings" ) );
    pState->pErrors = FilterButton( pBar, *pStyle, "log-error", QStringLiteral( "Show errors" ) );
    pState->pChannel = new QComboBox( pBar );
    pState->pChannel->setObjectName( QStringLiteral( "EditorConsoleChannel" ) );
    pState->pChannel->addItem( QStringLiteral( "All" ), -1 );
    pState->pChannel->setToolTip( QStringLiteral( "Show one channel (Gui, Editor, Map, ...) or all" ) );
    pState->pChannel->setSizeAdjustPolicy( QComboBox::AdjustToContents );
    pState->pSearch = new QLineEdit( pBar );
    pState->pSearch->setObjectName( QStringLiteral( "EditorConsoleSearch" ) );
    pState->pSearch->setPlaceholderText( QStringLiteral( "Filter output" ) );
    pState->pSearch->setClearButtonEnabled( true );
    pState->pSearch->setMinimumWidth( 60 );
    pState->pTime = new QToolButton( pBar );
    pState->pTime->setText( QStringLiteral( "Time" ) );
    pState->pTime->setCheckable( true );
    pState->pTime->setChecked( true );
    pState->pTime->setAutoRaise( true );
    pState->pTime->setToolTip( QStringLiteral( "Show seconds since the editor started" ) );
    auto *pCopy = new QToolButton( pBar );
    pCopy->setIcon( EditorStyle_Icon( *pStyle, "edit-copy" ) );
    pCopy->setToolTip( QStringLiteral( "Copy the visible output" ) );
    pCopy->setAutoRaise( true );
    auto *pClear = new QToolButton( pBar );
    pClear->setIcon( EditorStyle_Icon( *pStyle, "log-clear" ) );
    pClear->setToolTip( QStringLiteral( "Clear the log (console.clear)" ) );
    pClear->setAutoRaise( true );
    for ( QToolButton *pButton : { pCopy, pClear } ) { pButton->setIconSize( QSize( 16, 16 ) ); }
    pBarLayout->addWidget( pState->pMessages );
    pBarLayout->addWidget( pState->pWarnings );
    pBarLayout->addWidget( pState->pErrors );
    pBarLayout->addSpacing( 6 );
    pBarLayout->addWidget( pState->pChannel );
    pBarLayout->addWidget( pState->pSearch, 1 );
    pBarLayout->addWidget( pState->pTime );
    pBarLayout->addWidget( pCopy );
    pBarLayout->addWidget( pClear );

    pState->pOutput = new QPlainTextEdit( pConsole );
    pState->pOutput->setObjectName( QStringLiteral( "EditorConsoleOutput" ) );
    pState->pOutput->setReadOnly( true );
    pState->pOutput->setMaximumBlockCount( EDITOR_CONSOLE_MAX_BLOCKS );
    pState->pOutput->setFont( pStyle->consoleFont );
    pState->pOutput->setLineWrapMode( QPlainTextEdit::NoWrap );

    QWidget *pRow = new QWidget( pConsole );
    pRow->setObjectName( QStringLiteral( "EditorConsoleInputRow" ) );
    QLabel *pPrompt = new QLabel( QStringLiteral( ">" ), pRow );
    pPrompt->setObjectName( QStringLiteral( "EditorConsolePrompt" ) );
    pPrompt->setFont( pStyle->consoleFont );
    pState->pInput = new QLineEdit( pRow );
    pState->pInput->setObjectName( QStringLiteral( "EditorConsoleInput" ) );
    pState->pInput->setFont( pStyle->consoleFont );
    pState->pInput->setPlaceholderText( QStringLiteral( "command, e.g. console.help" ) );
    QHBoxLayout *pRowLayout = new QHBoxLayout( pRow );
    pRowLayout->setContentsMargins( 0, 0, 0, 0 );
    pRowLayout->setSpacing( 4 );
    pRowLayout->addWidget( pPrompt );
    pRowLayout->addWidget( pState->pInput, 1 );

    QVBoxLayout *pLayout = new QVBoxLayout( pConsole );
    pLayout->setContentsMargins( 0, 0, 0, 0 );
    pLayout->setSpacing( 0 );
    pLayout->addWidget( pBar );
    pLayout->addWidget( pState->pOutput, 1 );
    pLayout->addWidget( pRow );

    pState->pCompletions = new QStringListModel( pState );
    QCompleter *pCompleter = new QCompleter( pState->pCompletions, pState->pInput );
    pCompleter->setCaseSensitivity( Qt::CaseInsensitive );
    pCompleter->setCompletionMode( QCompleter::PopupCompletion );
    pState->pInput->setCompleter( pCompleter );
    RefreshCompletions( pState );
    // Commands come and go with plugins; refresh when typing starts.
    QObject::connect( pState->pInput, &QLineEdit::textEdited, pState, [pState]( const QString &text ) {
        if ( text.size() == 1 ) { RefreshCompletions( pState ); }
    } );
    QObject::connect( pState->pInput, &QLineEdit::returnPressed, pConsole, [pConsole, pState]() {
        const QString line = pState->pInput->text().trimmed();
        pState->pInput->clear();
        if ( !line.isEmpty() ) { EditorConsole_Submit( pConsole, line ); }
    } );
    QShortcut *pUp = new QShortcut( QKeySequence( Qt::Key_Up ), pState->pInput, nullptr, nullptr, Qt::WidgetShortcut );
    QObject::connect( pUp, &QShortcut::activated, pState, [pState]() { StepHistory( pState, -1 ); } );
    QShortcut *pDown = new QShortcut( QKeySequence( Qt::Key_Down ), pState->pInput, nullptr, nullptr, Qt::WidgetShortcut );
    QObject::connect( pDown, &QShortcut::activated, pState, [pState]() { StepHistory( pState, 1 ); } );

    for ( QToolButton *pButton : { pState->pMessages, pState->pWarnings, pState->pErrors, pState->pTime } ) {
        QObject::connect( pButton, &QToolButton::toggled, pState, [pState]( bool ) { Rebuild( *pState ); } );
    }
    QObject::connect( pState->pChannel, &QComboBox::currentIndexChanged, pState, [pState]( int ) { Rebuild( *pState ); } );
    QObject::connect( pState->pSearch, &QLineEdit::textChanged, pState, [pState]( const QString & ) { Rebuild( *pState ); } );
    QObject::connect( pCopy, &QToolButton::clicked, pState, [pState]() { QApplication::clipboard()->setText( pState->pOutput->toPlainText() ); } );
    QObject::connect( pClear, &QToolButton::clicked, pState, [pState]() { EditorLog_Clear( pState->pLog ); } );

    ( void )EditorLog_AddListener( pLog, &console_state_t::OnLog, pState );
    Rebuild( *pState );
    return pConsole;
}

void EditorConsole_Append( QWidget *pConsole, log_level_t level, const QString &text )
{
    EditorLog_Append( State( pConsole )->pLog, level, log_channel_t::Command, text );
}

void EditorConsole_Submit( QWidget *pConsole, const QString &line )
{
    console_state_t *pState = State( pConsole );
    if ( pState->history.isEmpty() || pState->history.back() != line ) { pState->history.append( line ); }
    while ( pState->history.size() > EDITOR_CONSOLE_MAX_HISTORY ) { pState->history.removeFirst(); }
    pState->iHistory = static_cast<int>( pState->history.size() );
    EditorLog_Append( pState->pLog, log_level_t::Info, log_channel_t::Command, QStringLiteral( "> " ) + line, nullptr, 0, true );
    const QByteArray utf8 = line.toUtf8();
    const command_result_t result = EditorCommands_ExecuteLine( pState->pRegistry, { utf8.constData(), static_cast<usize>( utf8.size() ) } );
    if ( result != command_result_t::OK && result != command_result_t::DISABLED ) {
        EditorConsole_Append( pConsole, log_level_t::Warning, QStringLiteral( "  %1" ).arg( QString::fromUtf8( EditorCommands_ResultName( result ) ) ) );
    } else if ( result == command_result_t::DISABLED ) {
        EditorConsole_Append( pConsole, log_level_t::Info, QStringLiteral( "  %1: not available yet" ).arg( line.section( QLatin1Char( ' ' ), 0, 0 ) ) );
    }
}

command_registry_status_t EditorConsole_RegisterCommands( command_registry_t *pRegistry, QWidget *pConsole )
{
    CY_ASSERT( pRegistry != nullptr && pConsole != nullptr );
    const command_desc_t commands[]{
        { "console.clear", "Clear Console", "Removes every line from the console.", "log-clear", "console.clear",
          COMMAND_FLAG_CONSOLE_ONLY, ConsoleClear, nullptr, pConsole },
        { "console.help", "Console Help", "Lists commands, optionally those starting with a prefix.", "help",
          "console.help [prefix]", COMMAND_FLAG_CONSOLE_ONLY, ConsoleHelp, nullptr, pConsole },
    };
    const command_registry_status_t status = EditorCommands_Register( pRegistry, commands, std::size( commands ) );
    if ( status == command_registry_status_t::OK ) {
        QObject::connect( pConsole, &QObject::destroyed, [pRegistry]() {
            ( void )EditorCommands_UnregisterModule( pRegistry, StringView_FromCString( "console" ) );
        } );
    }
    return status;
}

void EditorConsole_SetLevels( QWidget *pConsole, bool bMessages, bool bWarnings, bool bErrors )
{
    console_state_t *pState = State( pConsole );
    const QSignalBlocker a( pState->pMessages );
    const QSignalBlocker b( pState->pWarnings );
    const QSignalBlocker c( pState->pErrors );
    pState->pMessages->setChecked( bMessages );
    pState->pWarnings->setChecked( bWarnings );
    pState->pErrors->setChecked( bErrors );
    Rebuild( *pState );
}

void EditorConsole_SetSearch( QWidget *pConsole, const QString &text )
{
    State( pConsole )->pSearch->setText( text );
}

QString EditorConsole_Text( QWidget *pConsole )
{
    return State( pConsole )->pOutput->toPlainText();
}

} // namespace cypher::editor::gui
