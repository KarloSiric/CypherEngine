//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileShellRunner.cpp
//  Purpose: Implements an asynchronous, non-interactive local command runner.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileShellRunner.h"
#include "CypherTileConsoleColors.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStyle>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace cypher::tools::tile_editor
{
namespace
{
constexpr int COMMAND_HISTORY_LIMIT = 256;
constexpr int OUTPUT_BLOCK_LIMIT = 10000;
constexpr int OUTPUT_ENTRY_LIMIT = 10000;
constexpr int TERMINATE_GRACE_PERIOD_MS = 1500;

QString UnquoteEnvironmentPath( QString path )
{
    path = path.trimmed();
    if ( path.size() >= 2 &&
         ( ( path.front() == QLatin1Char( '"' ) &&
             path.back() == QLatin1Char( '"' ) ) ||
           ( path.front() == QLatin1Char( '\'' ) &&
             path.back() == QLatin1Char( '\'' ) ) ) ) {
        path = path.mid( 1, path.size() - 2 );
    }
    return QDir::cleanPath( path );
}

bool IsAbsoluteExecutable( const QString &path )
{
    if ( path.isEmpty() ) return false;
    const QFileInfo file( path );
    return file.isAbsolute() && file.exists() && file.isFile() &&
        file.isExecutable();
}

QString ValidExecutablePath( const QString &path )
{
    const QString cleaned = UnquoteEnvironmentPath( path );
    if ( !IsAbsoluteExecutable( cleaned ) ) return {};
    const QString canonical = QFileInfo( cleaned ).canonicalFilePath();
    return canonical.isEmpty() ? QFileInfo( cleaned ).absoluteFilePath() : canonical;
}

QString FindExecutable( const QString &name )
{
    return ValidExecutablePath( QStandardPaths::findExecutable( name ) );
}

bool UsesWindowsCommandSyntax( const QString &program )
{
    const QString executable = QFileInfo( program ).fileName();
    return executable.compare( QStringLiteral( "cmd" ), Qt::CaseInsensitive ) == 0 ||
        executable.compare( QStringLiteral( "cmd.exe" ), Qt::CaseInsensitive ) == 0;
}

void RefreshDynamicStyle( QWidget *pWidget )
{
    if ( pWidget == nullptr || pWidget->style() == nullptr ) return;
    pWidget->style()->unpolish( pWidget );
    pWidget->style()->polish( pWidget );
    pWidget->update();
}
} // namespace

CypherTileShellRunner::CypherTileShellRunner( QWidget *pParent )
    : QWidget( pParent ),
      m_pProcess( new QProcess( this ) ),
      m_shellProgram( resolveShellProgram() )
{
    setObjectName( QStringLiteral( "TileShellRunner" ) );

    auto *pRoot = new QVBoxLayout( this );
    pRoot->setContentsMargins( 5, 5, 5, 5 );
    pRoot->setSpacing( 4 );

    m_pDirectoryRow = new QWidget( this );
    m_pDirectoryRow->setObjectName( QStringLiteral( "TileShellDirectoryRow" ) );
    auto *pDirectoryLayout = new QHBoxLayout( m_pDirectoryRow );
    pDirectoryLayout->setContentsMargins( 0, 0, 0, 0 );
    pDirectoryLayout->setSpacing( 4 );
    auto *pDirectoryLabel = new QLabel( tr( "Directory" ), m_pDirectoryRow );
    pDirectoryLabel->setObjectName( QStringLiteral( "TileShellDirectoryLabel" ) );
    m_pWorkingDirectory = new QLineEdit( m_pDirectoryRow );
    m_pWorkingDirectory->setObjectName(
        QStringLiteral( "TileShellWorkingDirectory" ) );
    m_pWorkingDirectory->setClearButtonEnabled( false );
    m_pWorkingDirectory->setFont(
        QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
    m_pWorkingDirectory->setToolTip(
        tr( "Working directory used by the next local shell command." ) );
    pDirectoryLabel->setBuddy( m_pWorkingDirectory );
    m_pBrowseButton = new QPushButton( tr( "Choose…" ), m_pDirectoryRow );
    m_pBrowseButton->setObjectName( QStringLiteral( "TileShellBrowseDirectory" ) );
    m_pBrowseButton->setToolTip( tr( "Choose a working directory" ) );
    pDirectoryLayout->addWidget( pDirectoryLabel );
    pDirectoryLayout->addWidget( m_pWorkingDirectory, 1 );
    pDirectoryLayout->addWidget( m_pBrowseButton );
    pRoot->addWidget( m_pDirectoryRow );

    m_pControlRow = new QWidget( this );
    m_pControlRow->setObjectName( QStringLiteral( "TileShellControlRow" ) );
    auto *pControlLayout = new QHBoxLayout( m_pControlRow );
    pControlLayout->setContentsMargins( 0, 0, 0, 0 );
    pControlLayout->setSpacing( 4 );
    m_pShellLabel = new QLabel( m_pControlRow );
    m_pShellLabel->setObjectName( QStringLiteral( "TileShellProgram" ) );
    m_pShellLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
    m_pShellLabel->setFont(
        QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
    m_pShellLabel->setProperty( "muted", true );
    m_pShellLabel->setText(
        m_shellProgram.isEmpty()
            ? tr( "Shell: unavailable" )
            : tr( "Shell: %1" ).arg( m_shellProgram ) );
    m_pShellLabel->setToolTip( tr(
        "Each command starts a non-interactive shell. Interactive terminal "
        "programs and PTY features are not supported." ) );

    m_pRestartButton = new QPushButton( tr( "Restart" ), m_pControlRow );
    m_pRestartButton->setObjectName( QStringLiteral( "TileShellRestart" ) );
    m_pRestartButton->setToolTip( tr( "Stop and rerun the last command" ) );
    m_pStopButton = new QPushButton( tr( "Stop" ), m_pControlRow );
    m_pStopButton->setObjectName( QStringLiteral( "TileShellStop" ) );
    m_pStopButton->setToolTip(
        tr( "Terminate the running command, then force-stop it if necessary" ) );
    m_pClearButton = new QPushButton( tr( "Clear" ), m_pControlRow );
    m_pClearButton->setObjectName( QStringLiteral( "TileShellClear" ) );
    m_pCopyButton = new QPushButton( tr( "Copy Output" ), m_pControlRow );
    m_pCopyButton->setObjectName( QStringLiteral( "TileShellCopyOutput" ) );
    pControlLayout->addWidget( m_pShellLabel, 1 );
    pControlLayout->addWidget( m_pRestartButton );
    pControlLayout->addWidget( m_pStopButton );
    pControlLayout->addWidget( m_pClearButton );
    pControlLayout->addWidget( m_pCopyButton );
    pRoot->addWidget( m_pControlRow );

    m_pOutput = new QPlainTextEdit( this );
    m_pOutput->setObjectName( QStringLiteral( "TileShellOutput" ) );
    m_pOutput->setReadOnly( true );
    m_pOutput->setMaximumBlockCount( OUTPUT_BLOCK_LIMIT );
    m_pOutput->setLineWrapMode( QPlainTextEdit::NoWrap );
    m_pOutput->setFont(
        QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
    m_pOutput->setPlaceholderText( tr(
        "Local command output appears here. This runner does not provide an "
        "interactive terminal or PTY." ) );
    pRoot->addWidget( m_pOutput, 1 );

    m_pInputRow = new QWidget( this );
    m_pInputRow->setObjectName( QStringLiteral( "TileShellInputRow" ) );
    auto *pInputLayout = new QHBoxLayout( m_pInputRow );
    pInputLayout->setContentsMargins( 0, 0, 0, 0 );
    pInputLayout->setSpacing( 4 );
    auto *pPrompt = new QLabel( QStringLiteral( "$" ), m_pInputRow );
    pPrompt->setObjectName( QStringLiteral( "TileShellPrompt" ) );
    pPrompt->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
    m_pInput = new QLineEdit( m_pInputRow );
    m_pInput->setObjectName( QStringLiteral( "TileShellInput" ) );
    m_pInput->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
    m_pInput->setPlaceholderText(
        tr( "Run one local shell command (Up/Down: history)" ) );
    m_pInput->setToolTip( tr(
        "Runs this line through the displayed non-interactive local shell." ) );
    pPrompt->setBuddy( m_pInput );
    m_pRunButton = new QPushButton( tr( "Run" ), m_pInputRow );
    m_pRunButton->setObjectName( QStringLiteral( "TileShellRun" ) );
    m_pRunButton->setToolTip( tr( "Run the command" ) );
    m_pRunButton->setAutoDefault( false );
    pInputLayout->addWidget( pPrompt );
    pInputLayout->addWidget( m_pInput, 1 );
    pInputLayout->addWidget( m_pRunButton );
    pRoot->addWidget( m_pInputRow );

    m_pStatusLabel = new QLabel( this );
    m_pStatusLabel->setObjectName( QStringLiteral( "TileShellStatus" ) );
    m_pStatusLabel->setProperty( "muted", true );
    m_pStatusLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
    pRoot->addWidget( m_pStatusLabel );

    m_pProcess->setObjectName( QStringLiteral( "TileShellProcess" ) );
    m_pProcess->setProcessChannelMode( QProcess::SeparateChannels );

    connect( m_pInput, &QLineEdit::returnPressed,
             this, [this] { submitInput(); } );
    connect( m_pInput, &QLineEdit::textChanged,
             this, [this] { updateControls(); } );
    connect( m_pRunButton, &QPushButton::clicked,
             this, [this] { submitInput(); } );
    connect( m_pStopButton, &QPushButton::clicked,
             this, [this] { stopCommand(); } );
    connect( m_pRestartButton, &QPushButton::clicked,
             this, [this] { restartCommand(); } );
    connect( m_pClearButton, &QPushButton::clicked, this, [this] {
        m_outputEntries.clear();
        m_pOutput->clear();
        updateControls();
    } );
    connect( m_pCopyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText( m_pOutput->toPlainText() );
    } );
    connect( m_pBrowseButton, &QPushButton::clicked, this, [this] {
        const QString selected = QFileDialog::getExistingDirectory(
            this, tr( "Choose command working directory" ),
            m_workingDirectory );
        if ( !selected.isEmpty() ) setWorkingDirectory( selected );
    } );
    connect( m_pWorkingDirectory, &QLineEdit::editingFinished, this, [this] {
        applyWorkingDirectory( m_pWorkingDirectory->text() );
    } );

    connect( m_pProcess, &QProcess::started, this, [this] {
        setRunnerState( QStringLiteral( "running" ),
                        tr( "Running process %1" ).arg(
                            m_pProcess->processId() ) );
        updateControls();
    } );
    connect( m_pProcess, &QProcess::readyReadStandardOutput,
             this, [this] { appendProcessOutput(); } );
    connect( m_pProcess, &QProcess::readyReadStandardError,
             this, [this] { appendProcessOutput(); } );
    connect( m_pProcess, &QProcess::errorOccurred,
             this, [this]( QProcess::ProcessError error ) {
        if ( error == QProcess::FailedToStart ) {
            appendOutput(
                tr( "Could not start %1: %2\n" )
                    .arg( m_shellProgram, m_pProcess->errorString() ),
                output_kind_t::ERROR );
            m_bStopping = false;
            m_pendingRestartCommand.clear();
            setRunnerState( QStringLiteral( "error" ),
                            tr( "Failed to start command" ) );
            updateControls();
        } else if ( !m_bStopping && error != QProcess::Crashed ) {
            appendOutput(
                tr( "Process error: %1\n" ).arg( m_pProcess->errorString() ),
                output_kind_t::ERROR );
        }
    } );
    connect(
        m_pProcess,
        qOverload<int, QProcess::ExitStatus>( &QProcess::finished ),
        this,
        [this]( int nExitCode, QProcess::ExitStatus exitStatus ) {
            appendProcessOutput();
            const bool bWasStopped = m_bStopping;
            m_bStopping = false;
            if ( bWasStopped ) {
                appendOutput( tr( "[runner] command stopped\n" ),
                              output_kind_t::STATUS );
                setRunnerState( QStringLiteral( "idle" ), tr( "Stopped" ) );
            } else if ( exitStatus == QProcess::CrashExit ) {
                appendOutput(
                    tr( "[runner] command terminated unexpectedly\n" ),
                    output_kind_t::ERROR );
                setRunnerState( QStringLiteral( "error" ),
                                tr( "Command terminated unexpectedly" ) );
            } else {
                appendOutput(
                    tr( "[runner] command exited with code %1\n" ).arg(
                        nExitCode ),
                    nExitCode == 0 ? output_kind_t::STATUS
                                   : output_kind_t::ERROR );
                setRunnerState(
                    nExitCode == 0 ? QStringLiteral( "success" )
                                   : QStringLiteral( "error" ),
                    tr( "Exited with code %1" ).arg( nExitCode ) );
            }
            updateControls();

            const QString restart = m_pendingRestartCommand;
            m_pendingRestartCommand.clear();
            if ( !restart.isEmpty() ) {
                QTimer::singleShot( 0, this,
                    [this, restart] { startCommand( restart ); } );
            }
        } );

    m_pInput->installEventFilter( this );
    qApp->installEventFilter( this );
    setWorkingDirectory( QDir::currentPath() );
    setRunnerState(
        m_shellProgram.isEmpty() ? QStringLiteral( "error" )
                                 : QStringLiteral( "idle" ),
        m_shellProgram.isEmpty()
            ? tr( "No supported local shell executable was found" )
            : tr( "Ready" ) );
    if ( m_shellProgram.isEmpty() ) {
        appendOutput(
            tr( "[runner] no executable shell was found. Configure SHELL or "
                "COMSPEC with an absolute executable path.\n" ),
            output_kind_t::ERROR );
    }
    updateControls();
}

CypherTileShellRunner::~CypherTileShellRunner()
{
    if ( m_pProcess != nullptr &&
         m_pProcess->state() != QProcess::NotRunning ) {
        disconnect( m_pProcess, nullptr, this, nullptr );
        m_pProcess->kill();
        m_pProcess->close();
    }
}

void CypherTileShellRunner::setWorkingDirectory( const QString &directory )
{
    applyWorkingDirectory( directory );
}

void CypherTileShellRunner::setOutputCallback( output_callback_t callback )
{
    m_outputCallback = std::move( callback );
}

void CypherTileShellRunner::setIntegratedMode( bool bIntegrated )
{
    m_bIntegrated = bIntegrated;
    // In standalone mode the constructor reports a missing shell in the
    // runner's transcript. The parent console provides the same actionable
    // diagnostic in its shared startup transcript, so do not retain a second
    // invisible copy after this private transcript is hidden.
    if ( bIntegrated ) {
        m_outputEntries.clear();
        m_pOutput->clear();
    }
    m_pOutput->setVisible( !bIntegrated );
    m_pInputRow->setVisible( !bIntegrated );
    m_pClearButton->setVisible( !bIntegrated );
    m_pCopyButton->setVisible( !bIntegrated );
    if ( auto *pRoot = qobject_cast<QVBoxLayout *>( layout() ) ) {
        pRoot->setContentsMargins(
            bIntegrated ? 0 : 5,
            bIntegrated ? 0 : 5,
            bIntegrated ? 0 : 5,
            bIntegrated ? 2 : 5 );
    }
    updateControls();
}

void CypherTileShellRunner::executeCommand( const QString &command )
{
    startCommand( command );
}

void CypherTileShellRunner::focusInput()
{
    m_pInput->setFocus( Qt::ShortcutFocusReason );
    m_pInput->selectAll();
}

QString CypherTileShellRunner::shellProgram() const
{
    return m_shellProgram;
}

QString CypherTileShellRunner::workingDirectory() const
{
    return m_workingDirectory;
}

bool CypherTileShellRunner::isRunning() const
{
    return m_pProcess != nullptr &&
        m_pProcess->state() != QProcess::NotRunning;
}

bool CypherTileShellRunner::eventFilter( QObject *pObject, QEvent *pEvent )
{
    if ( pObject == qApp && pEvent != nullptr &&
         pEvent->type() == QEvent::ApplicationPaletteChange ) {
        renderOutput();
    }
    if ( pObject == m_pInput && pEvent->type() == QEvent::KeyPress ) {
        auto *pKey = static_cast<QKeyEvent *>( pEvent );
        if ( pKey->key() == Qt::Key_Up ) {
            navigateHistory( -1 );
            return true;
        }
        if ( pKey->key() == Qt::Key_Down ) {
            navigateHistory( 1 );
            return true;
        }
        if ( pKey->key() == Qt::Key_Escape && !m_pInput->text().isEmpty() ) {
            m_pInput->clear();
            m_iHistory = m_history.size();
            m_historyDraft.clear();
            return true;
        }
    }
    return QWidget::eventFilter( pObject, pEvent );
}

QString CypherTileShellRunner::resolveShellProgram() const
{
    const QProcessEnvironment environment =
        QProcessEnvironment::systemEnvironment();
#if defined( Q_OS_WIN )
    const QStringList variables{ QStringLiteral( "COMSPEC" ),
                                 QStringLiteral( "SHELL" ) };
#else
    const QStringList variables{ QStringLiteral( "SHELL" ),
                                 QStringLiteral( "COMSPEC" ) };
#endif
    for ( const QString &variable : variables ) {
        const QString candidate = ValidExecutablePath(
            environment.value( variable ) );
        if ( !candidate.isEmpty() ) return candidate;
    }

#if defined( Q_OS_WIN )
    const QStringList fallbackNames{ QStringLiteral( "zsh.exe" ),
                                     QStringLiteral( "bash.exe" ),
                                     QStringLiteral( "cmd.exe" ) };
#else
    const QStringList fallbackNames{ QStringLiteral( "zsh" ),
                                     QStringLiteral( "bash" ) };
#endif
    for ( const QString &name : fallbackNames ) {
        const QString candidate = FindExecutable( name );
        if ( !candidate.isEmpty() ) return candidate;
    }
    return {};
}

bool CypherTileShellRunner::applyWorkingDirectory( const QString &directory )
{
    QString path = directory.trimmed();
    if ( path.startsWith( QStringLiteral( "~/" ) ) ) {
        path = QDir::home().filePath( path.mid( 2 ) );
    } else if ( path == QStringLiteral( "~" ) ) {
        path = QDir::homePath();
    }
    if ( QFileInfo( path ).isRelative() ) {
        const QString base = m_workingDirectory.isEmpty()
            ? QDir::currentPath()
            : m_workingDirectory;
        path = QDir( base ).absoluteFilePath( path );
    }

    const QFileInfo info( QDir::cleanPath( path ) );
    if ( !info.exists() || !info.isDir() ) {
        m_pWorkingDirectory->setText( directory );
        m_pWorkingDirectory->setProperty( "invalid", true );
        RefreshDynamicStyle( m_pWorkingDirectory );
        if ( !isRunning() ) {
            setRunnerState( QStringLiteral( "error" ),
                            tr( "Working directory does not exist" ) );
        }
        return false;
    }

    const QString canonical = info.canonicalFilePath();
    m_workingDirectory = canonical.isEmpty()
        ? info.absoluteFilePath()
        : canonical;
    m_pWorkingDirectory->setText( QDir::toNativeSeparators(
        m_workingDirectory ) );
    m_pWorkingDirectory->setProperty( "invalid", false );
    RefreshDynamicStyle( m_pWorkingDirectory );
    if ( !isRunning() && !m_shellProgram.isEmpty() ) {
        setRunnerState( QStringLiteral( "idle" ), tr( "Ready" ) );
    }
    return true;
}

bool CypherTileShellRunner::startCommand( const QString &command )
{
    const QString normalized = command.trimmed();
    if ( normalized.isEmpty() ) return false;
    if ( m_shellProgram.isEmpty() ) {
        appendOutput( tr( "[runner] no local shell is available\n" ),
                      output_kind_t::ERROR );
        setRunnerState( QStringLiteral( "error" ),
                        tr( "Shell unavailable" ) );
        return false;
    }
    if ( isRunning() ) {
        setRunnerState( QStringLiteral( "running" ),
                        tr( "A command is already running" ) );
        return false;
    }
    if ( !applyWorkingDirectory( m_pWorkingDirectory->text() ) ) return false;

    if ( m_history.isEmpty() || m_history.back() != normalized ) {
        m_history.push_back( normalized );
        if ( m_history.size() > COMMAND_HISTORY_LIMIT ) m_history.pop_front();
    }
    m_iHistory = m_history.size();
    m_historyDraft.clear();
    m_lastCommand = normalized;
    m_bStopping = false;
    ++m_nRunGeneration;

    appendOutput(
        QStringLiteral( "[%1] $ %2\n" ).arg(
            QDir::toNativeSeparators( m_workingDirectory ), normalized ),
        output_kind_t::COMMAND );

    QProcessEnvironment environment =
        QProcessEnvironment::systemEnvironment();
    environment.insert( QStringLiteral( "TERM" ), QStringLiteral( "dumb" ) );
    environment.insert( QStringLiteral( "NO_COLOR" ), QStringLiteral( "1" ) );
    environment.insert( QStringLiteral( "PAGER" ), QStringLiteral( "cat" ) );
    m_pProcess->setProcessEnvironment( environment );
    m_pProcess->setWorkingDirectory( m_workingDirectory );
    m_pProcess->setProgram( m_shellProgram );
    if ( UsesWindowsCommandSyntax( m_shellProgram ) ) {
        m_pProcess->setArguments(
            { QStringLiteral( "/d" ), QStringLiteral( "/s" ),
              QStringLiteral( "/c" ), normalized } );
    } else {
        m_pProcess->setArguments(
            { QStringLiteral( "-lc" ), normalized } );
    }
    setRunnerState( QStringLiteral( "starting" ), tr( "Starting…" ) );
    updateControls();
    m_pProcess->start();
    return true;
}

void CypherTileShellRunner::submitInput()
{
    const QString command = m_pInput->text();
    if ( startCommand( command ) ) m_pInput->clear();
}

void CypherTileShellRunner::stopCommand()
{
    if ( !isRunning() || m_bStopping ) return;
    m_bStopping = true;
    setRunnerState( QStringLiteral( "stopping" ), tr( "Stopping…" ) );
    updateControls();
    m_pProcess->terminate();

    const unsigned long long generation = m_nRunGeneration;
    QTimer::singleShot(
        TERMINATE_GRACE_PERIOD_MS, this, [this, generation] {
            if ( generation != m_nRunGeneration || !m_bStopping ||
                 !isRunning() ) return;
            appendOutput( tr( "[runner] terminate timed out; forcing stop\n" ),
                          output_kind_t::STATUS );
            m_pProcess->kill();
        } );
}

void CypherTileShellRunner::restartCommand()
{
    if ( m_lastCommand.isEmpty() ) return;
    if ( isRunning() ) {
        m_pendingRestartCommand = m_lastCommand;
        stopCommand();
    } else {
        startCommand( m_lastCommand );
    }
}

void CypherTileShellRunner::navigateHistory( int direction )
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
}

void CypherTileShellRunner::appendOutput(
    const QString &text, output_kind_t kind )
{
    if ( text.isEmpty() ) return;
    if ( m_outputCallback ) m_outputCallback( text, kind );

    // The integrated editor console owns the only visible transcript. Keep
    // this private document empty so output limits and copying describe the
    // surface the user can actually see.
    if ( m_bIntegrated ) {
        updateControls();
        return;
    }

    m_outputEntries.push_back( { text, kind } );
    while ( m_outputEntries.size() > OUTPUT_ENTRY_LIMIT )
        m_outputEntries.pop_front();
    const detail::console_colors_t colors = detail::ConsoleThemeColors();
    appendRenderedOutput( m_outputEntries.back(), colors, true );
    updateControls();
}

void CypherTileShellRunner::appendRenderedOutput(
    const output_entry_t &entry,
    const detail::console_colors_t &colors,
    bool bEnsureVisible )
{
    QTextCharFormat format;
    switch ( entry.kind ) {
        case output_kind_t::COMMAND:
            format.setForeground( colors.accent );
            format.setFontWeight( QFont::DemiBold );
            break;
        case output_kind_t::STANDARD_OUTPUT:
            format.setForeground( colors.text );
            break;
        case output_kind_t::STANDARD_ERROR:
            format.setForeground( colors.warning );
            break;
        case output_kind_t::STATUS:
            format.setForeground( colors.muted );
            break;
        case output_kind_t::ERROR:
            format.setForeground( colors.error );
            format.setFontWeight( QFont::DemiBold );
            break;
    }

    QTextCursor cursor( m_pOutput->document() );
    cursor.movePosition( QTextCursor::End );
    cursor.insertText( entry.text, format );
    if ( bEnsureVisible ) {
        m_pOutput->setTextCursor( cursor );
        m_pOutput->ensureCursorVisible();
    }
}

void CypherTileShellRunner::renderOutput()
{
    if ( m_pOutput == nullptr || m_bIntegrated ) return;
    QScrollBar *pScrollBar = m_pOutput->verticalScrollBar();
    const int oldScroll = pScrollBar == nullptr ? 0 : pScrollBar->value();
    const bool bWasAtBottom = pScrollBar == nullptr ||
        oldScroll >= pScrollBar->maximum();

    const detail::console_colors_t colors = detail::ConsoleThemeColors();
    m_pOutput->setUpdatesEnabled( false );
    m_pOutput->clear();
    for ( const output_entry_t &entry : std::as_const( m_outputEntries ) )
        appendRenderedOutput( entry, colors, false );

    QTextCursor cursor( m_pOutput->document() );
    cursor.movePosition( QTextCursor::End );
    m_pOutput->setTextCursor( cursor );
    m_pOutput->setUpdatesEnabled( true );

    if ( pScrollBar != nullptr ) {
        if ( bWasAtBottom ) m_pOutput->ensureCursorVisible();
        else pScrollBar->setValue(
            std::min( oldScroll, pScrollBar->maximum() ) );
    }
    updateControls();
}

void CypherTileShellRunner::appendProcessOutput()
{
    const QByteArray standardOutput = m_pProcess->readAllStandardOutput();
    if ( !standardOutput.isEmpty() ) {
        appendOutput( QString::fromUtf8( standardOutput ),
                      output_kind_t::STANDARD_OUTPUT );
    }
    const QByteArray standardError = m_pProcess->readAllStandardError();
    if ( !standardError.isEmpty() ) {
        appendOutput( QString::fromUtf8( standardError ),
                      output_kind_t::STANDARD_ERROR );
    }
}

void CypherTileShellRunner::setRunnerState(
    const QString &state, const QString &message )
{
    const bool bBusy = state == QStringLiteral( "starting" ) ||
        state == QStringLiteral( "running" ) ||
        state == QStringLiteral( "stopping" );
    setProperty( "state", state );
    setProperty( "busy", bBusy );
    m_pStatusLabel->setProperty( "state", state );
    m_pStatusLabel->setText( message );
    RefreshDynamicStyle( this );
    RefreshDynamicStyle( m_pStatusLabel );
}

void CypherTileShellRunner::updateControls()
{
    const bool bRunning = isRunning();
    const bool bHasShell = !m_shellProgram.isEmpty();
    m_pWorkingDirectory->setEnabled( !bRunning );
    m_pBrowseButton->setEnabled( !bRunning );
    m_pInput->setEnabled( bHasShell );
    m_pRunButton->setEnabled(
        bHasShell && !bRunning && !m_pInput->text().trimmed().isEmpty() );
    m_pStopButton->setEnabled( bRunning && !m_bStopping );
    m_pRestartButton->setEnabled(
        bHasShell && !m_lastCommand.isEmpty() && !m_bStopping );
    m_pClearButton->setEnabled( !m_pOutput->document()->isEmpty() );
    m_pCopyButton->setEnabled( !m_pOutput->document()->isEmpty() );
}

} // namespace cypher::tools::tile_editor
