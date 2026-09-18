//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileConsole.h
//  Purpose: Declares the tile editor's interactive command console.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_CONSOLE_H
#define CYPHER_TOOLS_TILEEDITOR_CONSOLE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QEvent;
class QCompleter;
class QLabel;
class QLineEdit;
class QObject;
class QPlainTextEdit;
class QStringListModel;

namespace cypher::tools::tile_editor
{

class CypherTileShellRunner;
namespace detail
{
enum class console_color_role_t;
struct console_colors_t;
}

class CypherTileConsole final : public QWidget
{
public:
    using execute_callback_t = std::function<void( const QString & )>;

    explicit CypherTileConsole( QWidget *pParent = nullptr );

    void setExecuteCallback( execute_callback_t callback );
    // Installs complete command-line suggestions used by the popup and Tab
    // completion. Entries may include arguments (for example "door north").
    // They are normalized, sorted case-insensitively, and de-duplicated.
    void setCompletions( const QStringList &completions );
    void appendInfo( const QString &message );
    void appendWarning( const QString &message );
    void appendError( const QString &message );
    // Editor commands and local shell output share this transcript. Shell
    // commands can also be submitted from the common input with either
    // `! command` or `shell command`.
    void setWorkingDirectory( const QString &directory );
    void executeShellCommand( const QString &command );
    void stopShellCommand();
    void restartShellCommand();
    [[nodiscard]] QString shellProgram() const;
    [[nodiscard]] QString workingDirectory() const;
    [[nodiscard]] bool isShellRunning() const;
    void appendStartupTranscript(
        const QString &workspaceDirectory = {},
        const QString &documentPath = {} );
    void clear();
    void focusInput();

protected:
    bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

private:
    struct transcript_entry_t {
        QString timestamp{};
        QString prefix{};
        QString message{};
        detail::console_color_role_t colorRole{};
    };

    void append(
        const QString &prefix,
        detail::console_color_role_t colorRole,
        const QString &message );
    void appendRenderedEntry(
        const transcript_entry_t &entry,
        const detail::console_colors_t &colors,
        bool bEnsureVisible );
    void renderTranscript();
    void appendShellOutput( const QString &text, int kind );
    void submit();
    void navigateHistory( int direction );
    void refreshCompletions( bool bExplicitRequest = false );
    void cycleCompletion( int direction );
    void resetCompletionCycle();
    void applyCompletion( const QString &completion );
    [[nodiscard]] QString completionPrefix() const;

    QPlainTextEdit *m_pOutput{ nullptr };
    QLineEdit *m_pInput{ nullptr };
    QLabel *m_pCompletionHint{ nullptr };
    CypherTileShellRunner *m_pShellRunner{ nullptr };
    QCompleter *m_pCompleter{ nullptr };
    QStringListModel *m_pCompletionModel{ nullptr };
    execute_callback_t m_executeCallback{};
    QStringList m_completions{};
    QStringList m_history{};
    int m_iHistory{ 0 };
    QString m_historyDraft{};
    QString m_completionCyclePrefix{};
    int m_iCompletionRow{ -1 };
    bool m_bCompletionCycling{ false };
    QList<transcript_entry_t> m_transcript{};
};

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_CONSOLE_H
