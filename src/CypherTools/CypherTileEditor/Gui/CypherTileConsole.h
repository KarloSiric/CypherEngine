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

#include <QColor>
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
    void clear();
    void focusInput();

protected:
    bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

private:
    void append( const QString &prefix, const QColor &color, const QString &message );
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
};

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_CONSOLE_H
