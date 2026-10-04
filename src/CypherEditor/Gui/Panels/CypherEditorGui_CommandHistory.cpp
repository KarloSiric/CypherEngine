//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_CommandHistory.cpp
//  Purpose: Implements the Command History panel.
//  Details: Entries are kept in a deque the panel owns; the tree shows the
//           ones the filter allows. Repeating executes through the command
//           registry like any other caller, so repeated commands are
//           recorded again, as they are in Hammer.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_CommandHistory.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <deque>
#include <vector>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

constexpr int kEntryRole = Qt::UserRole;

// Modules whose commands look, open, or configure rather than edit.
constexpr const char *kNotRepeatable[]{ "view.", "file.", "tools.", "help.", "console.", "assets.", "edit.undo", "edit.redo", "edit.repeat" };

struct entry_t {
    QString id;
    QString label;
    QStringList args;
    command_result_t result{ command_result_t::OK };
    qint64 msTime{ 0 };
    bool bRepeatable{ false };
};

QString ResultText( command_result_t result )
{
    switch ( result ) {
        case command_result_t::OK: return QString();
        case command_result_t::DISABLED: return QStringLiteral( "Not available" );
        case command_result_t::FAILED: return QStringLiteral( "Failed" );
        default: return QStringLiteral( "Not run" );
    }
}

class command_history_t final : public QWidget {
public:
    command_history_t( QWidget *pParent, editor_gui_t *pGui ) : QWidget( pParent ), m_pGui( pGui )
    {
        CY_ASSERT( pGui != nullptr && pGui->bInitialized );
        setObjectName( QStringLiteral( "EditorCommandHistory" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 0 );

        auto *pBar = new QWidget( this );
        pBar->setObjectName( QStringLiteral( "EditorCommandHistoryToolbar" ) );
        auto *pBarLayout = new QHBoxLayout( pBar );
        pBarLayout->setContentsMargins( 3, 3, 3, 3 );
        pBarLayout->setSpacing( 3 );
        m_pRepeat = new QToolButton( pBar );
        m_pRepeat->setObjectName( QStringLiteral( "EditorCommandHistoryRepeat" ) );
        m_pRepeat->setText( QStringLiteral( "Repeat" ) );
        m_pRepeat->setIcon( EditorStyle_Icon( pGui->style, "command-repeat" ) );
        m_pRepeat->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        m_pRepeat->setToolTip( QStringLiteral( "Run the selected commands again, in order (Shift+G)" ) );
        m_pShowAll = new QToolButton( pBar );
        m_pShowAll->setObjectName( QStringLiteral( "EditorCommandHistoryShowAll" ) );
        m_pShowAll->setText( QStringLiteral( "All" ) );
        m_pShowAll->setCheckable( true );
        m_pShowAll->setToolTip( QStringLiteral( "Also list view, file, and dialog commands, which are not repeatable" ) );
        auto *pClear = new QToolButton( pBar );
        pClear->setObjectName( QStringLiteral( "EditorCommandHistoryClear" ) );
        pClear->setIcon( EditorStyle_Icon( pGui->style, "log-clear" ) );
        pClear->setToolTip( QStringLiteral( "Clear the history" ) );
        pBarLayout->addWidget( m_pRepeat );
        pBarLayout->addStretch( 1 );
        pBarLayout->addWidget( m_pShowAll );
        pBarLayout->addWidget( pClear );
        pLayout->addWidget( pBar );

        m_pTree = new QTreeWidget( this );
        m_pTree->setObjectName( QStringLiteral( "EditorCommandHistoryList" ) );
        m_pTree->setColumnCount( 3 );
        m_pTree->setHeaderLabels( { QStringLiteral( "Command" ), QStringLiteral( "Arguments" ), QStringLiteral( "Time" ) } );
        m_pTree->setRootIsDecorated( false );
        m_pTree->setUniformRowHeights( true );
        m_pTree->setSelectionMode( QAbstractItemView::ExtendedSelection );
        m_pTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        m_pTree->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
        m_pTree->header()->setSectionResizeMode( 2, QHeaderView::ResizeToContents );
        m_pTree->header()->setStretchLastSection( false );
        pLayout->addWidget( m_pTree, 1 );

        m_pStatus = new QLabel( this );
        m_pStatus->setObjectName( QStringLiteral( "EditorCommandHistoryStatus" ) );
        m_pStatus->setProperty( "muted", true );
        pLayout->addWidget( m_pStatus );

        QObject::connect( m_pRepeat, &QToolButton::clicked, this, [this]() { ( void )Repeat(); } );
        QObject::connect( m_pShowAll, &QToolButton::toggled, this, [this]( bool ) { Fill(); } );
        QObject::connect( pClear, &QToolButton::clicked, this, [this]() { Clear(); } );
        QObject::connect( m_pTree, &QTreeWidget::itemDoubleClicked, this, [this]( QTreeWidgetItem *, int ) { ( void )Repeat(); } );
        QObject::connect( m_pTree, &QTreeWidget::itemSelectionChanged, this, [this]() { UpdateStatus(); } );
        Fill();
    }

    void Record( const command_desc_t &command, const command_args_t &args, command_result_t result )
    {
        entry_t entry{};
        entry.id = QString::fromUtf8( command.pId );
        entry.label = QString::fromUtf8( command.pLabel != nullptr ? command.pLabel : command.pId );
        for ( usize i = 0u; i < args.nArgs; ++i ) {
            entry.args.append( QString::fromUtf8( args.pArgs[i].pData, static_cast<qsizetype>( args.pArgs[i].cchLength ) ) );
        }
        entry.result = result;
        entry.msTime = QDateTime::currentMSecsSinceEpoch();
        const QByteArray id = entry.id.toUtf8();
        entry.bRepeatable = EditorCommandHistory_IsRepeatable( string_view_t{ id.constData(), static_cast<usize>( id.size() ) } ) &&
                            result == command_result_t::OK;
        m_entries.push_back( entry );
        if ( m_entries.size() > EDITOR_COMMAND_HISTORY_MAX ) {
            m_entries.pop_front();
            Fill();
            return;
        }
        if ( Shows( entry ) ) {
            AddItem( m_entries.size() - 1u );
            m_pTree->scrollToBottom();
        }
        UpdateStatus();
    }

    command_result_t Repeat()
    {
        std::vector<usize> chosen;
        for ( QTreeWidgetItem *pItem : m_pTree->selectedItems() ) {
            const usize iEntry = static_cast<usize>( pItem->data( 0, kEntryRole ).toULongLong() );
            if ( iEntry < m_entries.size() && m_entries[iEntry].bRepeatable ) { chosen.push_back( iEntry ); }
        }
        std::sort( chosen.begin(), chosen.end() );
        if ( chosen.empty() ) {
            for ( usize i = m_entries.size(); i-- > 0u; ) {
                if ( m_entries[i].bRepeatable ) {
                    chosen.push_back( i );
                    break;
                }
            }
        }
        if ( chosen.empty() ) { return command_result_t::DISABLED; }
        // Copy first: running a command records into m_entries.
        std::vector<entry_t> run;
        for ( usize i : chosen ) { run.push_back( m_entries[i] ); }
        command_result_t result = command_result_t::OK;
        for ( const entry_t &entry : run ) {
            std::vector<QByteArray> storage;
            std::vector<string_view_t> views;
            for ( const QString &arg : entry.args ) { storage.push_back( arg.toUtf8() ); }
            for ( const QByteArray &bytes : storage ) { views.push_back( string_view_t{ bytes.constData(), static_cast<usize>( bytes.size() ) } ); }
            const QByteArray id = entry.id.toUtf8();
            command_args_t args{};
            args.pArgs = views.empty() ? nullptr : views.data();
            args.nArgs = views.size();
            const command_result_t one = EditorCommands_Execute( &m_pGui->commands, string_view_t{ id.constData(), static_cast<usize>( id.size() ) }, args );
            if ( one != command_result_t::OK ) { result = one; }
        }
        return result;
    }

    void Clear()
    {
        m_entries.clear();
        Fill();
    }

    void SetShowAll( bool bShowAll ) { m_pShowAll->setChecked( bShowAll ); }

    void SelectRows( const QList<int> &rows )
    {
        m_pTree->clearSelection();
        for ( int row : rows ) {
            if ( QTreeWidgetItem *pItem = m_pTree->topLevelItem( row ) ) { pItem->setSelected( true ); }
        }
    }

    QStringList Rows() const
    {
        QStringList rows;
        for ( int i = 0; i < m_pTree->topLevelItemCount(); ++i ) {
            const usize iEntry = static_cast<usize>( m_pTree->topLevelItem( i )->data( 0, kEntryRole ).toULongLong() );
            const entry_t &entry = m_entries[iEntry];
            rows.append( entry.args.isEmpty() ? entry.id : entry.id + QLatin1Char( ' ' ) + entry.args.join( QLatin1Char( ' ' ) ) );
        }
        return rows;
    }

private:
    bool Shows( const entry_t &entry ) const { return m_pShowAll->isChecked() || entry.bRepeatable; }

    void AddItem( usize iEntry )
    {
        const entry_t &entry = m_entries[iEntry];
        auto *pItem = new QTreeWidgetItem( m_pTree );
        pItem->setText( 0, entry.label );
        pItem->setText( 1, entry.args.join( QLatin1Char( ' ' ) ) );
        pItem->setText( 2, QDateTime::fromMSecsSinceEpoch( entry.msTime ).toString( QStringLiteral( "HH:mm:ss" ) ) );
        pItem->setData( 0, kEntryRole, static_cast<qulonglong>( iEntry ) );
        QString tip = entry.id;
        if ( !ResultText( entry.result ).isEmpty() ) { tip += QStringLiteral( "\n%1" ).arg( ResultText( entry.result ) ); }
        if ( !entry.bRepeatable ) { tip += QStringLiteral( "\nNot repeatable" ); }
        pItem->setToolTip( 0, tip );
        if ( !entry.bRepeatable ) {
            for ( int c = 0; c < 3; ++c ) { pItem->setForeground( c, EditorStyle_TokenColor( m_pGui->style, "ui.text.disabled" ) ); }
        }
    }

    void Fill()
    {
        m_pTree->clear();
        for ( usize i = 0u; i < m_entries.size(); ++i ) {
            if ( Shows( m_entries[i] ) ) { AddItem( i ); }
        }
        m_pTree->scrollToBottom();
        UpdateStatus();
    }

    void UpdateStatus()
    {
        const int nSelected = m_pTree->selectedItems().size();
        m_pStatus->setText( nSelected > 1 ? QStringLiteral( "%1 commands selected; Repeat runs them in order" ).arg( nSelected )
                                          : QStringLiteral( "%1 commands" ).arg( m_pTree->topLevelItemCount() ) );
        bool bAny = false;
        for ( const entry_t &entry : m_entries ) { bAny = bAny || entry.bRepeatable; }
        m_pRepeat->setEnabled( bAny );
    }

    editor_gui_t *m_pGui{ nullptr };
    std::deque<entry_t> m_entries{};
    QToolButton *m_pRepeat{ nullptr };
    QToolButton *m_pShowAll{ nullptr };
    QTreeWidget *m_pTree{ nullptr };
    QLabel *m_pStatus{ nullptr };
};

command_history_t *AsHistory( QWidget *pPanel )
{
    auto *pImpl = dynamic_cast<command_history_t *>( pPanel );
    CY_ASSERT( pImpl != nullptr );
    return pImpl;
}

} // namespace

QWidget *EditorCommandHistory_Create( QWidget *pParent, editor_gui_t *pGui )
{
    CY_ASSERT( pGui != nullptr && pGui->bInitialized );
    if ( pGui == nullptr || !pGui->bInitialized ) { return nullptr; }
    return new command_history_t( pParent, pGui );
}

void EditorCommandHistory_Record( QWidget *pPanel, const command_desc_t &command, const command_args_t &args, command_result_t result )
{
    if ( command_history_t *pImpl = AsHistory( pPanel ) ) { pImpl->Record( command, args, result ); }
}

bool EditorCommandHistory_IsRepeatable( string_view_t commandId ) noexcept
{
    for ( const char *pPrefix : kNotRepeatable ) {
        const string_view_t prefix = StringView_FromCString( pPrefix );
        if ( commandId.cchLength >= prefix.cchLength && StringView_Equals( string_view_t{ commandId.pData, prefix.cchLength }, prefix ) ) { return false; }
    }
    return commandId.cchLength != 0u;
}

command_result_t EditorCommandHistory_Repeat( QWidget *pPanel )
{
    command_history_t *pImpl = AsHistory( pPanel );
    return pImpl != nullptr ? pImpl->Repeat() : command_result_t::DISABLED;
}

void EditorCommandHistory_Clear( QWidget *pPanel )
{
    if ( command_history_t *pImpl = AsHistory( pPanel ) ) { pImpl->Clear(); }
}

void EditorCommandHistory_SetShowAll( QWidget *pPanel, bool bShowAll )
{
    if ( command_history_t *pImpl = AsHistory( pPanel ) ) { pImpl->SetShowAll( bShowAll ); }
}

void EditorCommandHistory_SelectRows( QWidget *pPanel, const QList<int> &rows )
{
    if ( command_history_t *pImpl = AsHistory( pPanel ) ) { pImpl->SelectRows( rows ); }
}

QStringList EditorCommandHistory_Rows( QWidget *pPanel )
{
    command_history_t *pImpl = AsHistory( pPanel );
    return pImpl != nullptr ? pImpl->Rows() : QStringList{};
}

} // namespace cypher::editor::gui
