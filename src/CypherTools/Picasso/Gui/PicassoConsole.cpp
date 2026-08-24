//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Gui/PicassoConsole.cpp
//  Purpose: Implements Picasso's interactive command-console widget.
//  Details: The console delegates every submitted line and completion query to its
//           host. This keeps the widget reusable and prevents Qt code from becoming
//           a second command parser beside CypherCommon_CommandSystem.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoConsole.h"

#include "CypherCommon_ConCommand.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QColor>
#include <QCompleter>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QStringList>
#include <QStringListModel>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTime>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace cypher::tools::picasso
{

namespace
{

constexpr int PICASSO_CONSOLE_HISTORY_CAPACITY = 256;
constexpr int PICASSO_CONSOLE_RECORD_CAPACITY = 5000;

QColor ConsoleRecordColor( picasso_console_record_t type )
{
    switch ( type ) {
        case picasso_console_record_t::COMMAND: return QColor( 116, 207, 210 );
        case picasso_console_record_t::WARNING: return QColor( 224, 163, 75 );
        case picasso_console_record_t::ERROR:   return QColor( 237, 105, 105 );
        case picasso_console_record_t::INFO:
        default:                                return QColor( 202, 207, 209 );
    }
}

QString ConsoleChannelName( picasso_console_channel_t channel )
{
    switch ( channel ) {
        case picasso_console_channel_t::IMAGE:    return QStringLiteral( "Image" );
        case picasso_console_channel_t::MATERIAL: return QStringLiteral( "Material" );
        case picasso_console_channel_t::COMPILER: return QStringLiteral( "Compiler" );
        case picasso_console_channel_t::RESOURCE: return QStringLiteral( "Resource" );
        case picasso_console_channel_t::VFS:      return QStringLiteral( "VFS" );
        case picasso_console_channel_t::PICASSO:  return QStringLiteral( "Picasso" );
        case picasso_console_channel_t::ALL:
        default:                                  return QStringLiteral( "All" );
    }
}

} // namespace

PicassoConsole::PicassoConsole( QWidget *pParent )
    : QWidget( pParent )
{
    auto *pLayout = new QVBoxLayout( this );
    pLayout->setContentsMargins( 0, 0, 0, 0 );
    pLayout->setSpacing( 0 );

    auto *pChannels = new QWidget( this );
    pChannels->setObjectName( QStringLiteral( "PicassoConsoleChannels" ) );
    auto *pChannelLayout = new QHBoxLayout( pChannels );
    pChannelLayout->setContentsMargins( 5, 3, 5, 3 );
    pChannelLayout->setSpacing( 2 );
    auto *pChannelGroup = new QButtonGroup( pChannels );
    pChannelGroup->setExclusive( true );
    for ( const picasso_console_channel_t channel : {
              picasso_console_channel_t::ALL,
              picasso_console_channel_t::PICASSO,
              picasso_console_channel_t::IMAGE,
              picasso_console_channel_t::MATERIAL,
              picasso_console_channel_t::COMPILER,
              picasso_console_channel_t::RESOURCE,
              picasso_console_channel_t::VFS } ) {
        auto *pButton = new QToolButton( pChannels );
        pButton->setText( ConsoleChannelName( channel ) );
        pButton->setCheckable( true );
        pButton->setProperty( "consoleChannel", true );
        pButton->setChecked( channel == picasso_console_channel_t::ALL );
        pChannelGroup->addButton( pButton );
        pChannelLayout->addWidget( pButton );
        connect( pButton, &QToolButton::clicked, this, [this, channel] {
            setChannelFilter( channel );
        } );
    }
    pChannelLayout->addStretch( 1 );
    auto *pClear = new QToolButton( pChannels );
    pClear->setText( tr( "Clear" ) );
    pClear->setToolTip( tr( "Clear console records" ) );
    connect( pClear, &QToolButton::clicked, this, [this] { clearRecords(); } );
    pChannelLayout->addWidget( pClear );
    pLayout->addWidget( pChannels );

    m_pOutput = new QPlainTextEdit( this );
    m_pOutput->setObjectName( QStringLiteral( "PicassoConsoleOutput" ) );
    m_pOutput->setReadOnly( true );
    m_pOutput->setMaximumBlockCount( 5000 );
    m_pOutput->setLineWrapMode( QPlainTextEdit::NoWrap );
    m_pOutput->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
    pLayout->addWidget( m_pOutput, 1 );

    auto *pInputRow = new QWidget( this );
    pInputRow->setObjectName( QStringLiteral( "PicassoConsoleInputRow" ) );
    auto *pInputLayout = new QHBoxLayout( pInputRow );
    pInputLayout->setContentsMargins( 7, 4, 7, 5 );
    pInputLayout->setSpacing( 6 );
    auto *pPrompt = new QLabel( QStringLiteral( ">" ), pInputRow );
    pPrompt->setObjectName( QStringLiteral( "ConsolePrompt" ) );
    m_pInput = new QLineEdit( pInputRow );
    m_pInput->setObjectName( QStringLiteral( "PicassoConsoleInput" ) );
    m_pInput->setPlaceholderText( tr( "Enter command" ) );
    m_pInput->setClearButtonEnabled( true );
    m_pInput->setMaxLength( static_cast<int>(
        cypher::common::CY_COMMAND_MAX_LINE_BYTES ) );
    m_pInput->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
    pInputLayout->addWidget( pPrompt );
    pInputLayout->addWidget( m_pInput, 1 );
    pLayout->addWidget( pInputRow );

    m_pCompletionModel = new QStringListModel( this );
    m_pCompleter = new QCompleter( m_pCompletionModel, this );
    m_pCompleter->setCaseSensitivity( Qt::CaseInsensitive );
    m_pCompleter->setCompletionMode( QCompleter::PopupCompletion );
    m_pCompleter->setFilterMode( Qt::MatchStartsWith );
    m_pCompleter->setWrapAround( false );
    m_pInput->setCompleter( m_pCompleter );
    m_pInput->installEventFilter( this );

    connect( m_pInput, &QLineEdit::returnPressed,
             this, [this] { submitCommand(); } );
    connect( m_pInput, &QLineEdit::textEdited,
             this, [this]( const QString &partial ) {
        refreshCompletions( partial );
    } );
}

void PicassoConsole::setExecuteCallback( execute_callback_t callback )
{
    m_executeCallback = std::move( callback );
}

void PicassoConsole::setCompleteCallback( complete_callback_t callback )
{
    m_completeCallback = std::move( callback );
}

void PicassoConsole::appendRecord(
    picasso_console_record_t type,
    const QString &message )
{
    appendRecord(
        type,
        picasso_console_channel_t::PICASSO,
        message );
}

void PicassoConsole::appendRecord(
    picasso_console_record_t type,
    picasso_console_channel_t channel,
    const QString &message )
{
    if ( message.isEmpty() ) {
        return;
    }

    m_records.append( {
        type,
        channel,
        QTime::currentTime().toString( QStringLiteral( "HH:mm:ss" ) ),
        message
    } );
    while ( m_records.size() > PICASSO_CONSOLE_RECORD_CAPACITY ) {
        m_records.removeFirst();
    }
    if ( m_channelFilter != picasso_console_channel_t::ALL &&
         m_channelFilter != channel ) {
        return;
    }

    QTextCursor cursor( m_pOutput->document() );
    cursor.movePosition( QTextCursor::End );
    if ( !m_pOutput->document()->isEmpty() ) {
        cursor.insertBlock();
    }
    QTextCharFormat format;
    format.setForeground( ConsoleRecordColor( type ) );
    cursor.insertText(
        QStringLiteral( "[%1] %2: %3" ).arg(
            m_records.constLast().timestamp,
            ConsoleChannelName( channel ),
            message ),
        format );
    m_pOutput->setTextCursor( cursor );
    m_pOutput->ensureCursorVisible();
}

void PicassoConsole::clearRecords()
{
    m_records.clear();
    m_pOutput->clear();
}

void PicassoConsole::focusCommandLine()
{
    m_pInput->setFocus( Qt::ShortcutFocusReason );
    m_pInput->selectAll();
}

bool PicassoConsole::eventFilter( QObject *pObject, QEvent *pEvent )
{
    if ( pObject != m_pInput || pEvent->type() != QEvent::KeyPress ) {
        return QWidget::eventFilter( pObject, pEvent );
    }

    auto *pKey = static_cast<QKeyEvent *>( pEvent );
    const bool bCompletionVisible = m_pCompleter->popup()->isVisible();
    if ( pKey->key() == Qt::Key_Up && !bCompletionVisible ) {
        navigateHistory( -1 );
        return true;
    }
    if ( pKey->key() == Qt::Key_Down && !bCompletionVisible ) {
        navigateHistory( 1 );
        return true;
    }
    if ( pKey->key() == Qt::Key_Tab ) {
        refreshCompletions( m_pInput->text() );
        if ( m_pCompletionModel->rowCount() > 0 ) {
            const QModelIndex first = m_pCompletionModel->index( 0, 0 );
            m_pInput->setText( first.data().toString() );
            m_pInput->setCursorPosition( m_pInput->text().size() );
        }
        return true;
    }
    return QWidget::eventFilter( pObject, pEvent );
}

void PicassoConsole::submitCommand()
{
    const QString command = m_pInput->text().trimmed();
    if ( command.isEmpty() ) {
        return;
    }

    appendRecord(
        picasso_console_record_t::COMMAND,
        QStringLiteral( "> %1" ).arg( command ) );
    if ( m_history.isEmpty() || m_history.constLast() != command ) {
        m_history.append( command );
        while ( m_history.size() > PICASSO_CONSOLE_HISTORY_CAPACITY ) {
            m_history.removeFirst();
        }
    }
    m_iHistory = static_cast<int>( m_history.size() );
    m_historyDraft.clear();
    m_pInput->clear();
    m_pCompleter->popup()->hide();

    if ( m_executeCallback ) {
        m_executeCallback( command );
    }
}

void PicassoConsole::refreshCompletions( const QString &partial )
{
    const QStringList suggestions = m_completeCallback
        ? m_completeCallback( partial )
        : QStringList{};
    m_pCompletionModel->setStringList( suggestions );
    if ( suggestions.isEmpty() ) {
        m_pCompleter->popup()->hide();
        return;
    }
    m_pCompleter->setCompletionPrefix( partial );
    m_pCompleter->complete();
}

void PicassoConsole::navigateHistory( int direction )
{
    if ( m_history.isEmpty() ) {
        return;
    }
    const int historyCount = static_cast<int>( m_history.size() );
    if ( m_iHistory == historyCount && direction < 0 ) {
        m_historyDraft = m_pInput->text();
    }

    m_iHistory = qBound(
        0,
        m_iHistory + direction,
        historyCount );
    m_pInput->setText(
        m_iHistory == historyCount
            ? m_historyDraft
            : m_history.at( m_iHistory ) );
    m_pInput->setCursorPosition( m_pInput->text().size() );
}

void PicassoConsole::setChannelFilter( picasso_console_channel_t channel )
{
    if ( m_channelFilter == channel ) {
        return;
    }
    m_channelFilter = channel;
    rebuildOutput();
}

void PicassoConsole::rebuildOutput()
{
    m_pOutput->clear();
    for ( const console_entry_t &entry : m_records ) {
        if ( m_channelFilter != picasso_console_channel_t::ALL &&
             m_channelFilter != entry.channel ) {
            continue;
        }
        QTextCursor cursor( m_pOutput->document() );
        cursor.movePosition( QTextCursor::End );
        if ( !m_pOutput->document()->isEmpty() ) {
            cursor.insertBlock();
        }
        QTextCharFormat format;
        format.setForeground( ConsoleRecordColor( entry.type ) );
        cursor.insertText(
            QStringLiteral( "[%1] %2: %3" ).arg(
                entry.timestamp,
                ConsoleChannelName( entry.channel ),
                entry.message ),
            format );
    }
    m_pOutput->moveCursor( QTextCursor::End );
    m_pOutput->ensureCursorVisible();
}

} // namespace cypher::tools::picasso
