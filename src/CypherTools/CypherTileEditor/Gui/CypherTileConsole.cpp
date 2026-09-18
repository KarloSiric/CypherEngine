//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileConsole.cpp
//  Purpose: Implements command history and formatted console output.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileConsole.h"
#include "CypherTileConsoleColors.h"
#include "CypherTileShellRunner.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QDir>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTime>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace cypher::tools::tile_editor
{
namespace
{
constexpr int TRANSCRIPT_ENTRY_LIMIT = 5000;
}

CypherTileConsole::CypherTileConsole( QWidget *pParent )
    : QWidget( pParent )
{
    auto *pRoot = new QVBoxLayout( this );
    pRoot->setContentsMargins( 0, 0, 0, 0 );
    pRoot->setSpacing( 0 );

    m_pShellRunner = new CypherTileShellRunner( this );
    m_pShellRunner->setIntegratedMode( true );
    m_pShellRunner->setOutputCallback(
        [this]( const QString &text,
                CypherTileShellRunner::output_kind_t kind ) {
            appendShellOutput( text, static_cast<int>( kind ) );
        } );
    pRoot->addWidget( m_pShellRunner );

    m_pOutput = new QPlainTextEdit( this );
    m_pOutput->setObjectName( QStringLiteral( "TileConsoleOutput" ) );
    m_pOutput->setReadOnly( true );
    m_pOutput->setMaximumBlockCount( 5000 );
    m_pOutput->setLineWrapMode( QPlainTextEdit::NoWrap );
    m_pOutput->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
    pRoot->addWidget( m_pOutput, 1 );

    auto *pInputRow = new QWidget( this );
    pInputRow->setObjectName( QStringLiteral( "TileConsoleInputRow" ) );
    auto *pInputLayout = new QHBoxLayout( pInputRow );
    pInputLayout->setContentsMargins( 7, 2, 5, 2 );
    pInputLayout->setSpacing( 5 );
    auto *pPrompt = new QLabel( QStringLiteral( ">" ), pInputRow );
    pPrompt->setObjectName( QStringLiteral( "TileConsolePrompt" ) );
    m_pInput = new QLineEdit( pInputRow );
    m_pInput->setObjectName( QStringLiteral( "TileConsoleInput" ) );
    m_pInput->setPlaceholderText( tr(
        "Editor command, or ! <local command>; type 'help' for commands" ) );
    m_pInput->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
    pInputLayout->addWidget( pPrompt );
    pInputLayout->addWidget( m_pInput, 1 );

    m_pCompletionHint = new QLabel( pInputRow );
    m_pCompletionHint->setObjectName( QStringLiteral( "TileConsoleCompletionHint" ) );
    m_pCompletionHint->setProperty( "muted", true );
    m_pCompletionHint->setFont(
        QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
    m_pCompletionHint->setTextFormat( Qt::PlainText );
    m_pCompletionHint->hide();
    pInputLayout->addWidget( m_pCompletionHint );
    pRoot->addWidget( pInputRow );

    m_pCompletionModel = new QStringListModel( this );
    m_pCompleter = new QCompleter( m_pCompletionModel, this );
    m_pCompleter->setWidget( m_pInput );
    m_pCompleter->setCaseSensitivity( Qt::CaseInsensitive );
    m_pCompleter->setCompletionMode( QCompleter::PopupCompletion );
    m_pCompleter->setFilterMode( Qt::MatchStartsWith );
    m_pCompleter->setWrapAround( false );
    m_pCompleter->popup()->setObjectName(
        QStringLiteral( "TileConsoleCompletionPopup" ) );

    connect(
        m_pCompleter,
        QOverload<const QString &>::of( &QCompleter::activated ),
        this,
        [this]( const QString &completion ) { applyCompletion( completion ); } );
    connect( m_pInput, &QLineEdit::returnPressed, this, [this] { submit(); } );
    connect(
        m_pInput,
        &QLineEdit::textEdited,
        this,
        [this] {
            resetCompletionCycle();
            refreshCompletions();
        } );

    // Install this filter after QCompleter so history and explicit Tab
    // completion take precedence whenever the completion popup is hidden.
    m_pInput->installEventFilter( this );

    qApp->installEventFilter( this );

    appendStartupTranscript();
}

void CypherTileConsole::setExecuteCallback( execute_callback_t callback )
{
    m_executeCallback = std::move( callback );
}

void CypherTileConsole::setCompletions( const QStringList &completions )
{
    resetCompletionCycle();
    m_completions.clear();
    m_completions.reserve( completions.size() );
    for ( const QString &candidate : completions ) {
        const QString normalized = candidate.trimmed();
        if ( !normalized.isEmpty() ) m_completions.push_back( normalized );
    }
    m_completions.append( {
        QStringLiteral( "shell" ),
        QStringLiteral( "shell <command>" ),
        QStringLiteral( "shell_restart" ),
        QStringLiteral( "shell_stop" )
    } );

    std::sort(
        m_completions.begin(),
        m_completions.end(),
        []( const QString &a, const QString &b ) {
            return QString::compare( a, b, Qt::CaseInsensitive ) < 0;
        } );
    const auto newEnd = std::unique(
        m_completions.begin(),
        m_completions.end(),
        []( const QString &a, const QString &b ) {
            return a.compare( b, Qt::CaseInsensitive ) == 0;
        } );
    m_completions.erase( newEnd, m_completions.end() );
    m_pCompletionModel->setStringList( m_completions );
    refreshCompletions();
}

void CypherTileConsole::appendInfo( const QString &message )
{
    append( QStringLiteral( "info" ),
            detail::console_color_role_t::INFO, message );
}

void CypherTileConsole::appendWarning( const QString &message )
{
    append( QStringLiteral( "warn" ),
            detail::console_color_role_t::WARNING, message );
}

void CypherTileConsole::appendError( const QString &message )
{
    append( QStringLiteral( "error" ),
            detail::console_color_role_t::ERROR, message );
}

void CypherTileConsole::setWorkingDirectory( const QString &directory )
{
    if ( m_pShellRunner == nullptr ) return;
    const QString previous = m_pShellRunner->workingDirectory();
    m_pShellRunner->setWorkingDirectory( directory );
    const QString current = m_pShellRunner->workingDirectory();
    if ( !current.isEmpty() && current != previous ) {
        append(
            QStringLiteral( "workspace" ),
            detail::console_color_role_t::MUTED,
            QDir::toNativeSeparators( current ) );
    }
}

void CypherTileConsole::executeShellCommand( const QString &command )
{
    const QString normalized = command.trimmed();
    if ( normalized.isEmpty() ) {
        appendWarning( tr(
            "Local command is empty. Use ! <command> or shell <command>." ) );
        return;
    }
    m_pShellRunner->executeCommand( normalized );
}

void CypherTileConsole::stopShellCommand()
{
    if ( m_pShellRunner != nullptr ) m_pShellRunner->stopCommand();
}

void CypherTileConsole::restartShellCommand()
{
    if ( m_pShellRunner != nullptr ) m_pShellRunner->restartCommand();
}

QString CypherTileConsole::shellProgram() const
{
    return m_pShellRunner == nullptr ? QString()
                                     : m_pShellRunner->shellProgram();
}

QString CypherTileConsole::workingDirectory() const
{
    return m_pShellRunner == nullptr ? QString()
                                     : m_pShellRunner->workingDirectory();
}

bool CypherTileConsole::isShellRunning() const
{
    return m_pShellRunner != nullptr && m_pShellRunner->isRunning();
}

void CypherTileConsole::appendStartupTranscript(
    const QString &workspaceDirectory,
    const QString &documentPath )
{
    append(
        QStringLiteral( "session" ),
        detail::console_color_role_t::ACCENT,
        tr( "Cypher Tile Editor console initialized." ) );
    append(
        QStringLiteral( "session" ),
        detail::console_color_role_t::MUTED,
        shellProgram().isEmpty()
            ? tr( "Local shell unavailable." )
            : tr( "Local shell: %1" ).arg(
                  QDir::toNativeSeparators( shellProgram() ) ) );
    if ( shellProgram().isEmpty() ) {
        append(
            QStringLiteral( "process" ),
            detail::console_color_role_t::ERROR,
            tr( "No executable local shell was found. Configure SHELL or "
                "COMSPEC with an absolute executable path." ) );
    }
    append(
        QStringLiteral( "hint" ),
        detail::console_color_role_t::MUTED,
        tr( "Run editor commands directly. Prefix local commands with !, "
            "or use shell <command>." ) );
    if ( !workspaceDirectory.trimmed().isEmpty() ) {
        setWorkingDirectory( workspaceDirectory );
    }
    if ( !documentPath.trimmed().isEmpty() ) {
        append(
            QStringLiteral( "document" ),
            detail::console_color_role_t::MUTED,
            QDir::toNativeSeparators( documentPath ) );
    }
}

void CypherTileConsole::clear()
{
    m_transcript.clear();
    m_pOutput->clear();
}

void CypherTileConsole::focusInput()
{
    m_pInput->setFocus( Qt::ShortcutFocusReason );
    m_pInput->selectAll();
}

bool CypherTileConsole::eventFilter( QObject *pObject, QEvent *pEvent )
{
    if ( pObject == qApp && pEvent != nullptr &&
         pEvent->type() == QEvent::ApplicationPaletteChange ) {
        renderTranscript();
    }
    if ( pObject == m_pInput && pEvent->type() == QEvent::KeyPress ) {
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
        if ( pKey->key() == Qt::Key_Tab ||
             pKey->key() == Qt::Key_Backtab ) {
            const bool bReverse = pKey->key() == Qt::Key_Backtab ||
                pKey->modifiers().testFlag( Qt::ShiftModifier );
            cycleCompletion( bReverse ? -1 : 1 );
            return true;
        }
        if ( pKey->key() == Qt::Key_Space &&
             pKey->modifiers().testFlag( Qt::ControlModifier ) ) {
            resetCompletionCycle();
            refreshCompletions( true );
            return true;
        }
        if ( ( pKey->key() == Qt::Key_Return ||
               pKey->key() == Qt::Key_Enter ) &&
             bCompletionVisible ) {
            QString completion = m_pCompleter->popup()
                ->currentIndex().data().toString();
            if ( completion.isEmpty() ) {
                completion = m_pCompleter->currentCompletion();
            }
            if ( !completion.isEmpty() ) {
                applyCompletion( completion );
                return true;
            }
        }
        if ( pKey->key() == Qt::Key_Escape ) {
            if ( bCompletionVisible ) {
                m_pCompleter->popup()->hide();
                m_pCompletionHint->hide();
                resetCompletionCycle();
                return true;
            }
            m_pInput->clear();
            m_pCompletionHint->hide();
            resetCompletionCycle();
            return true;
        }
    }
    return QWidget::eventFilter( pObject, pEvent );
}

void CypherTileConsole::append(
    const QString &prefix,
    detail::console_color_role_t colorRole,
    const QString &message )
{
    QString normalized = message;
    while ( normalized.endsWith( QLatin1Char( '\n' ) ) ||
            normalized.endsWith( QLatin1Char( '\r' ) ) ) {
        normalized.chop( 1 );
    }
    m_transcript.push_back( {
        QTime::currentTime().toString( QStringLiteral( "HH:mm:ss" ) ),
        prefix,
        normalized,
        colorRole
    } );
    while ( m_transcript.size() > TRANSCRIPT_ENTRY_LIMIT )
        m_transcript.pop_front();
    const detail::console_colors_t colors = detail::ConsoleThemeColors();
    appendRenderedEntry( m_transcript.back(), colors, true );
}

void CypherTileConsole::appendRenderedEntry(
    const transcript_entry_t &entry,
    const detail::console_colors_t &colors,
    bool bEnsureVisible )
{
    QTextCursor cursor( m_pOutput->document() );
    cursor.movePosition( QTextCursor::End );
    QTextCharFormat metadata;
    metadata.setForeground( colors.muted );
    cursor.insertText(
        QStringLiteral( "[%1] " ).arg( entry.timestamp ),
        metadata );
    QTextCharFormat category;
    category.setForeground(
        detail::ConsoleRoleColor( colors, entry.colorRole ) );
    category.setFontWeight( QFont::DemiBold );
    cursor.insertText(
        QStringLiteral( "%1: " ).arg( entry.prefix ), category );
    QTextCharFormat body;
    body.setForeground(
        detail::ConsoleBodyColor( colors, entry.colorRole ) );
    cursor.insertText( entry.message + QLatin1Char( '\n' ), body );
    if ( bEnsureVisible ) {
        m_pOutput->setTextCursor( cursor );
        m_pOutput->ensureCursorVisible();
    }
}

void CypherTileConsole::renderTranscript()
{
    if ( m_pOutput == nullptr ) return;
    QScrollBar *pScrollBar = m_pOutput->verticalScrollBar();
    const int oldScroll = pScrollBar == nullptr ? 0 : pScrollBar->value();
    const bool bWasAtBottom = pScrollBar == nullptr ||
        oldScroll >= pScrollBar->maximum();

    const detail::console_colors_t colors = detail::ConsoleThemeColors();
    m_pOutput->setUpdatesEnabled( false );
    m_pOutput->clear();
    for ( const transcript_entry_t &entry : std::as_const( m_transcript ) )
        appendRenderedEntry( entry, colors, false );

    QTextCursor cursor( m_pOutput->document() );
    cursor.movePosition( QTextCursor::End );
    m_pOutput->setTextCursor( cursor );
    m_pOutput->setUpdatesEnabled( true );

    if ( pScrollBar != nullptr ) {
        if ( bWasAtBottom ) m_pOutput->ensureCursorVisible();
        else pScrollBar->setValue(
            std::min( oldScroll, pScrollBar->maximum() ) );
    }
}

void CypherTileConsole::appendShellOutput( const QString &text, int kind )
{
    using output_kind_t = CypherTileShellRunner::output_kind_t;
    switch ( static_cast<output_kind_t>( kind ) ) {
        case output_kind_t::COMMAND:
            append( QStringLiteral( "shell" ),
                    detail::console_color_role_t::ACCENT, text );
            break;
        case output_kind_t::STANDARD_OUTPUT:
            append( QStringLiteral( "stdout" ),
                    detail::console_color_role_t::TEXT, text );
            break;
        case output_kind_t::STANDARD_ERROR:
            append( QStringLiteral( "stderr" ),
                    detail::console_color_role_t::WARNING, text );
            break;
        case output_kind_t::STATUS:
            append( QStringLiteral( "process" ),
                    detail::console_color_role_t::MUTED, text );
            break;
        case output_kind_t::ERROR:
            append( QStringLiteral( "process" ),
                    detail::console_color_role_t::ERROR, text );
            break;
    }
}

void CypherTileConsole::submit()
{
    const QString command = m_pInput->text().trimmed();
    if ( command.isEmpty() ) return;

    if ( m_history.isEmpty() || m_history.back() != command ) {
        m_history.push_back( command );
        if ( m_history.size() > 256 ) m_history.pop_front();
    }
    m_iHistory = m_history.size();
    m_historyDraft.clear();
    m_pInput->clear();
    m_pCompleter->popup()->hide();
    m_pCompletionHint->hide();
    resetCompletionCycle();

    if ( command.startsWith( QLatin1Char( '!' ) ) ) {
        executeShellCommand( command.mid( 1 ) );
        return;
    }
    if ( command.startsWith( QStringLiteral( "shell " ),
                             Qt::CaseInsensitive ) ) {
        executeShellCommand( command.mid( 6 ) );
        return;
    }
    if ( command.compare( QStringLiteral( "shell" ),
                          Qt::CaseInsensitive ) == 0 ) {
        appendInfo( tr(
            "The local shell shares this console. Use ! <command> or "
            "shell <command>." ) );
        return;
    }
    if ( command.compare( QStringLiteral( "shell_stop" ),
                          Qt::CaseInsensitive ) == 0 ) {
        stopShellCommand();
        return;
    }
    if ( command.compare( QStringLiteral( "shell_restart" ),
                          Qt::CaseInsensitive ) == 0 ) {
        restartShellCommand();
        return;
    }

    append( QStringLiteral( "cmd" ),
            detail::console_color_role_t::ACCENT, command );
    if ( m_executeCallback ) m_executeCallback( command );
}

void CypherTileConsole::navigateHistory( int direction )
{
    if ( m_history.isEmpty() ) return;
    if ( m_iHistory == m_history.size() && direction < 0 ) {
        m_historyDraft = m_pInput->text();
    }
    m_iHistory = qBound( 0, m_iHistory + direction, m_history.size() );
    if ( m_iHistory == m_history.size() ) {
        m_pInput->setText( m_historyDraft );
    } else {
        m_pInput->setText( m_history[m_iHistory] );
    }
    m_pInput->setCursorPosition( m_pInput->text().size() );
    m_pCompleter->popup()->hide();
    m_pCompletionHint->hide();
    resetCompletionCycle();
}

QString CypherTileConsole::completionPrefix() const
{
    const QString text = m_pInput->text();
    if ( m_pInput->cursorPosition() != text.size() ) return {};
    QString prefix = text;
    while ( !prefix.isEmpty() && prefix.front().isSpace() ) {
        prefix.remove( 0, 1 );
    }
    return prefix;
}

void CypherTileConsole::refreshCompletions( bool bExplicitRequest )
{
    const QString input = m_pInput->text();
    if ( m_pInput->cursorPosition() != input.size() ) {
        m_pCompleter->popup()->hide();
        m_pCompletionHint->hide();
        resetCompletionCycle();
        return;
    }

    const QString prefix = completionPrefix();
    if ( m_completions.isEmpty() || ( prefix.isEmpty() && !bExplicitRequest ) ) {
        m_pCompleter->popup()->hide();
        m_pCompletionHint->hide();
        return;
    }

    m_pCompleter->setCompletionPrefix( prefix );
    if ( !m_pCompleter->setCurrentRow( 0 ) ) {
        m_pCompleter->popup()->hide();
        m_pCompletionHint->hide();
        return;
    }

    const int nCompletionCount = m_pCompleter->completionCount();
    QString candidate = m_pCompleter->currentCompletion();
    const bool bOnlyExactMatch = nCompletionCount == 1 &&
        candidate.compare( prefix, Qt::CaseInsensitive ) == 0;
    if ( bOnlyExactMatch && !bExplicitRequest ) {
        m_pCompleter->popup()->hide();
        m_pCompletionHint->hide();
        return;
    }

    // If a complete command is also the prefix for argument suggestions,
    // select the first extension. Keeping the popup open makes entries such
    // as `door north` discoverable after the user has already typed `door`.
    if ( nCompletionCount > 1 &&
         candidate.compare( prefix, Qt::CaseInsensitive ) == 0 ) {
        for ( int iRow = 1; iRow < nCompletionCount; ++iRow ) {
            if ( !m_pCompleter->setCurrentRow( iRow ) ) break;
            const QString extended = m_pCompleter->currentCompletion();
            if ( extended.compare( prefix, Qt::CaseInsensitive ) != 0 ) {
                candidate = extended;
                break;
            }
        }
    }

    m_pCompletionHint->setText(
        tr( "Tab / Shift+Tab  %1" ).arg( candidate ) );
    m_pCompletionHint->show();
    m_pCompleter->complete();
}

void CypherTileConsole::cycleCompletion( int direction )
{
    if ( direction == 0 ) return;

    const QString input = m_pInput->text();
    if ( m_pInput->cursorPosition() != input.size() ) {
        m_pCompleter->popup()->hide();
        m_pCompletionHint->hide();
        resetCompletionCycle();
        return;
    }

    bool bContinueCycle = m_bCompletionCycling;
    if ( bContinueCycle ) {
        m_pCompleter->setCompletionPrefix( m_completionCyclePrefix );
        bContinueCycle = m_pCompleter->setCurrentRow( m_iCompletionRow ) &&
            input.compare(
                m_pCompleter->currentCompletion(),
                Qt::CaseInsensitive ) == 0;
    }

    if ( !bContinueCycle ) {
        resetCompletionCycle();
        m_completionCyclePrefix = completionPrefix();
        m_pCompleter->setCompletionPrefix( m_completionCyclePrefix );
        const int nCompletionCount = m_pCompleter->completionCount();
        if ( nCompletionCount <= 0 ) {
            m_pCompleter->popup()->hide();
            m_pCompletionHint->hide();
            return;
        }

        m_iCompletionRow = direction > 0 ? 0 : nCompletionCount - 1;
        if ( !m_pCompleter->setCurrentRow( m_iCompletionRow ) ) return;

        // Skip a no-op exact match when argument completions are available.
        if ( nCompletionCount > 1 &&
             m_pCompleter->currentCompletion().compare(
                 m_completionCyclePrefix,
                 Qt::CaseInsensitive ) == 0 ) {
            m_iCompletionRow = direction > 0 ? 1 : nCompletionCount - 1;
        }
        m_bCompletionCycling = true;
    } else {
        const int nCompletionCount = m_pCompleter->completionCount();
        if ( nCompletionCount <= 0 ) {
            resetCompletionCycle();
            return;
        }
        m_iCompletionRow = ( m_iCompletionRow + direction ) % nCompletionCount;
        if ( m_iCompletionRow < 0 ) m_iCompletionRow += nCompletionCount;
    }

    m_pCompleter->setCompletionPrefix( m_completionCyclePrefix );
    if ( !m_pCompleter->setCurrentRow( m_iCompletionRow ) ) {
        resetCompletionCycle();
        return;
    }
    const QString completion = m_pCompleter->currentCompletion();
    m_pInput->setText( completion );
    m_pInput->setCursorPosition( completion.size() );

    // Programmatic setText does not emit textEdited, so the original prefix
    // remains stable while repeated Tab/Shift+Tab traverses the same matches.
    m_pCompleter->setCompletionPrefix( m_completionCyclePrefix );
    m_pCompleter->setCurrentRow( m_iCompletionRow );
    m_pCompletionHint->setText(
        tr( "Tab / Shift+Tab  %1 of %2" )
            .arg( m_iCompletionRow + 1 )
            .arg( m_pCompleter->completionCount() ) );
    m_pCompletionHint->show();
    m_pCompleter->complete();
}

void CypherTileConsole::resetCompletionCycle()
{
    m_completionCyclePrefix.clear();
    m_iCompletionRow = -1;
    m_bCompletionCycling = false;
}

void CypherTileConsole::applyCompletion( const QString &completion )
{
    if ( completion.isEmpty() ) return;
    m_pInput->setText( completion );
    m_pInput->setCursorPosition( completion.size() );
    m_pCompleter->popup()->hide();
    m_pCompletionHint->hide();
    resetCompletionCycle();
}

} // namespace cypher::tools::tile_editor
