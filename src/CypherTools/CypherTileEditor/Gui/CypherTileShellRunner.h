//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileShellRunner.h
//  Purpose: Declares the tile editor's asynchronous local command runner.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_SHELLRUNNER_H
#define CYPHER_TOOLS_TILEEDITOR_SHELLRUNNER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <QString>
#include <QStringList>
#include <QWidget>

#include <QList>

#include <functional>

class QEvent;
class QLabel;
class QLineEdit;
class QObject;
class QPlainTextEdit;
class QProcess;
class QPushButton;

namespace cypher::tools::tile_editor
{

namespace detail
{
struct console_colors_t;
}

// Runs one command at a time through a non-interactive local shell. This is
// intentionally a command runner rather than a PTY-backed terminal emulator.
class CypherTileShellRunner final : public QWidget
{
public:
    enum class output_kind_t {
        COMMAND,
        STANDARD_OUTPUT,
        STANDARD_ERROR,
        STATUS,
        ERROR
    };

    using output_callback_t =
        std::function<void( const QString &, output_kind_t )>;

    explicit CypherTileShellRunner( QWidget *pParent = nullptr );
    ~CypherTileShellRunner() override;

    void setWorkingDirectory( const QString &directory );
    void setOutputCallback( output_callback_t callback );
    // Integrated mode keeps the directory, process state, stop and restart
    // controls while hiding the runner's private transcript and command line.
    // A parent console can then provide one shared output and input surface.
    void setIntegratedMode( bool bIntegrated );
    void executeCommand( const QString &command );
    void stopCommand();
    void restartCommand();
    void focusInput();

    [[nodiscard]] QString shellProgram() const;
    [[nodiscard]] QString workingDirectory() const;
    [[nodiscard]] bool isRunning() const;

protected:
    bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

private:
    struct output_entry_t {
        QString text{};
        output_kind_t kind{ output_kind_t::STANDARD_OUTPUT };
    };

    [[nodiscard]] QString resolveShellProgram() const;
    bool applyWorkingDirectory( const QString &directory );
    bool startCommand( const QString &command );
    void submitInput();
    void navigateHistory( int direction );
    void appendOutput( const QString &text, output_kind_t kind );
    void appendRenderedOutput(
        const output_entry_t &entry,
        const detail::console_colors_t &colors,
        bool bEnsureVisible );
    void renderOutput();
    void appendProcessOutput();
    void setRunnerState( const QString &state, const QString &message );
    void updateControls();

    QProcess *m_pProcess{ nullptr };
    QWidget *m_pDirectoryRow{ nullptr };
    QWidget *m_pControlRow{ nullptr };
    QWidget *m_pInputRow{ nullptr };
    QPlainTextEdit *m_pOutput{ nullptr };
    QLineEdit *m_pInput{ nullptr };
    QLineEdit *m_pWorkingDirectory{ nullptr };
    QLabel *m_pShellLabel{ nullptr };
    QLabel *m_pStatusLabel{ nullptr };
    QPushButton *m_pBrowseButton{ nullptr };
    QPushButton *m_pRunButton{ nullptr };
    QPushButton *m_pStopButton{ nullptr };
    QPushButton *m_pRestartButton{ nullptr };
    QPushButton *m_pClearButton{ nullptr };
    QPushButton *m_pCopyButton{ nullptr };

    QString m_shellProgram{};
    QString m_workingDirectory{};
    QString m_lastCommand{};
    QString m_pendingRestartCommand{};
    QStringList m_history{};
    QString m_historyDraft{};
    int m_iHistory{ 0 };
    unsigned long long m_nRunGeneration{ 0 };
    bool m_bStopping{ false };
    bool m_bIntegrated{ false };
    output_callback_t m_outputCallback{};
    QList<output_entry_t> m_outputEntries{};
};

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_SHELLRUNNER_H
