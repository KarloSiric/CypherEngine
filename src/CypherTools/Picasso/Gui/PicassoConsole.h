//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/Picasso/Gui/PicassoConsole.h
//  Purpose: Declares Picasso's interactive command-console widget.
//  Details: The widget owns presentation-only concerns such as colored records,
//           command history, keyboard navigation, and completion display. Command
//           parsing and execution remain in the Common command system.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_PICASSO_CONSOLE_H
#define CYPHER_TOOLS_PICASSO_CONSOLE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <QString>
#include <QStringList>
#include <QList>
#include <QWidget>

#include <functional>

class QCompleter;
class QEvent;
class QLineEdit;
class QObject;
class QPlainTextEdit;
class QStringListModel;

namespace cypher::tools::picasso
{

enum class picasso_console_record_t : unsigned char {
    COMMAND = 0u,
    INFO,
    WARNING,
    ERROR
};

// Tool channels stay separate from severity so users can isolate compiler,
// image, material, resource, or VFS traffic without hiding errors globally.
enum class picasso_console_channel_t : unsigned char {
    ALL = 0u,
    PICASSO,
    IMAGE,
    MATERIAL,
    COMPILER,
    RESOURCE,
    VFS
};

class PicassoConsole final : public QWidget
{
public:
    using execute_callback_t = std::function<void( const QString & )>;
    using complete_callback_t = std::function<QStringList( const QString & )>;

    explicit PicassoConsole( QWidget *pParent = nullptr );

    void setExecuteCallback( execute_callback_t callback );
    void setCompleteCallback( complete_callback_t callback );

    void appendRecord(
        picasso_console_record_t type,
        const QString &message );
    void appendRecord(
        picasso_console_record_t type,
        picasso_console_channel_t channel,
        const QString &message );
    void clearRecords();
    void focusCommandLine();

protected:
    bool eventFilter( QObject *pObject, QEvent *pEvent ) override;

private:
    void submitCommand();
    void refreshCompletions( const QString &partial );
    void navigateHistory( int direction );
    void setChannelFilter( picasso_console_channel_t channel );
    void rebuildOutput();

    struct console_entry_t {
        picasso_console_record_t type{ picasso_console_record_t::INFO };
        picasso_console_channel_t channel{ picasso_console_channel_t::PICASSO };
        QString timestamp{};
        QString message{};
    };

    QPlainTextEdit *m_pOutput{ nullptr };
    QLineEdit *m_pInput{ nullptr };
    QCompleter *m_pCompleter{ nullptr };
    QStringListModel *m_pCompletionModel{ nullptr };

    execute_callback_t m_executeCallback{};
    complete_callback_t m_completeCallback{};
    QStringList m_history{};
    QList<console_entry_t> m_records{};
    int m_iHistory{ 0 };
    QString m_historyDraft{};
    picasso_console_channel_t m_channelFilter{
        picasso_console_channel_t::ALL
    };
};

} // namespace cypher::tools::picasso

#endif // CYPHER_TOOLS_PICASSO_CONSOLE_H
