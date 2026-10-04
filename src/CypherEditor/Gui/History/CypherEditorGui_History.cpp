//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_History.cpp
//  Purpose: Displays and navigates the document's actual undo history.
//  Details: Listeners refresh committed history without polling. An open
//           gesture disables history navigation until commit or cancel.
//           Clean describes the history marker, which can also be a new
//           unsaved document; the panel never labels that state "Saved".
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_History.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

QString Label( string_view_t text )
{
    return text.pData != nullptr ? QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ) : QString();
}

class history_panel_t final : public QWidget {
public:
    history_panel_t( QWidget *pParent, const editor_style_t *pStyle, editor_history_panel_applied_fn pfnApplied, void *pContext )
        : QWidget( pParent ), m_pStyle( pStyle ), m_pfnApplied( pfnApplied ), m_pContext( pContext )
    {
        setObjectName( QStringLiteral( "EditorHistoryPanel" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 2 );
        auto *pBar = new QWidget( this );
        pBar->setObjectName( QStringLiteral( "EditorHistoryToolbar" ) );
        auto *pBarLayout = new QHBoxLayout( pBar );
        pBarLayout->setContentsMargins( 4, 3, 4, 3 );
        pBarLayout->setSpacing( 3 );
        m_pUndo = Button( pBar, "EditorHistoryUndo", "Undo" );
        m_pRedo = Button( pBar, "EditorHistoryRedo", "Redo" );
        m_pCount = new QLabel( pBar );
        m_pCount->setObjectName( QStringLiteral( "EditorHistoryCount" ) );
        m_pCount->setProperty( "muted", true );
        m_pCount->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
        pBarLayout->addWidget( m_pUndo );
        pBarLayout->addWidget( m_pRedo );
        pBarLayout->addWidget( m_pCount, 1 );
        pLayout->addWidget( pBar );

        m_pTree = new QTreeWidget( this );
        m_pTree->setObjectName( QStringLiteral( "EditorHistorySteps" ) );
        m_pTree->setHeaderLabels( { QStringLiteral( "Action" ), QStringLiteral( "State" ) } );
        m_pTree->setHeaderHidden( true );
        // Keep state metadata for accessibility and tests, but give the
        // action label the full width of a narrow inspector tab.
        m_pTree->setColumnHidden( 1, true );
        m_pTree->setRootIsDecorated( false );
        m_pTree->setIconSize( QSize( 14, 14 ) );
        m_pTree->setUniformRowHeights( true );
        m_pTree->setAlternatingRowColors( true );
        m_pTree->setSelectionMode( QAbstractItemView::SingleSelection );
        m_pTree->setEditTriggers( QAbstractItemView::NoEditTriggers );
        m_pTree->header()->setStretchLastSection( false );
        m_pTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        m_pTree->setToolTip( QStringLiteral( "Click a row to undo or redo to that document state." ) );
        pLayout->addWidget( m_pTree, 1 );

        m_pStatus = new QLabel( this );
        m_pStatus->setObjectName( QStringLiteral( "EditorHistoryStatus" ) );
        m_pStatus->setWordWrap( true );
        m_pStatus->setMargin( 4 );
        m_pStatus->setTextFormat( Qt::PlainText );
        m_pStatus->setToolTip( QStringLiteral( "Clean means the document matches its history clean marker. A new unsaved document can also be clean." ) );
        pLayout->addWidget( m_pStatus );

        QObject::connect( m_pUndo, &QToolButton::clicked, this, [this]() { Apply( action_t::UNDO, 0u ); } );
        QObject::connect( m_pRedo, &QToolButton::clicked, this, [this]() { Apply( action_t::REDO, 0u ); } );
        const auto activate = [this]( QTreeWidgetItem *pItem, int ) {
            if ( pItem != nullptr ) { Apply( action_t::STEP, static_cast<usize>( pItem->data( 0, Qt::UserRole ).toULongLong() ) ); }
        };
        QObject::connect( m_pTree, &QTreeWidget::itemClicked, this, activate );
        QObject::connect( m_pTree, &QTreeWidget::itemActivated, this, activate );
    }

    ~history_panel_t() override
    {
        if ( m_pHistory != nullptr ) { EditorHistory_RemoveListener( m_pHistory, &OnChanged, this ); }
    }

    bool_t Bind( editor_history_t *pHistory )
    {
        if ( pHistory == m_pHistory ) {
            Refresh();
            return CY_TRUE;
        }
        if ( pHistory != nullptr && !EditorHistory_AddListener( pHistory, &OnChanged, this ) ) { return CY_FALSE; }
        if ( m_pHistory != nullptr ) { EditorHistory_RemoveListener( m_pHistory, &OnChanged, this ); }
        m_pHistory = pHistory;
        Refresh();
        return CY_TRUE;
    }

    void Refresh()
    {
        const QSignalBlocker blocker( m_pTree );
        m_pTree->clear();
        m_pUndo->setIcon( EditorStyle_Icon( *m_pStyle, "edit-undo" ) );
        m_pRedo->setIcon( EditorStyle_Icon( *m_pStyle, "edit-redo" ) );
        const bool bInitialized = EditorHistory_IsInitialized( m_pHistory );
        const bool bEditing = EditorHistory_IsTransactionOpen( m_pHistory );
        m_pTree->setEnabled( bInitialized && !bEditing );
        m_pUndo->setEnabled( bInitialized && !bEditing && EditorHistory_CanUndo( m_pHistory ) );
        m_pRedo->setEnabled( bInitialized && !bEditing && EditorHistory_CanRedo( m_pHistory ) );
        const QString undoLabel = Label( EditorHistory_UndoLabel( m_pHistory ) );
        const QString redoLabel = Label( EditorHistory_RedoLabel( m_pHistory ) );
        m_pUndo->setToolTip( undoLabel.isEmpty() ? QStringLiteral( "Undo" ) : QStringLiteral( "Undo %1" ).arg( undoLabel ) );
        m_pRedo->setToolTip( redoLabel.isEmpty() ? QStringLiteral( "Redo" ) : QStringLiteral( "Redo %1" ).arg( redoLabel ) );
        if ( !bInitialized ) {
            m_pCount->clear();
            m_pCount->setToolTip( QString() );
            m_pStatus->setText( QStringLiteral( "No document history." ) );
            return;
        }
        if ( bEditing ) {
            // The common stack can contain a partial group while a gesture
            // is open; do not present that as a committed history row.
            m_pCount->setText( QStringLiteral( "Editing" ) );
            m_pCount->setToolTip( QStringLiteral( "An edit is in progress." ) );
            m_pStatus->setText( QStringLiteral( "Finish or cancel the current edit to navigate history." ) );
            return;
        }

        const usize nSteps = EditorHistory_StepCount( m_pHistory );
        const usize nApplied = EditorHistory_AppliedStepCount( m_pHistory );
        const bool bClean = EditorHistory_IsClean( m_pHistory );
        QTreeWidgetItem *pCurrent = nullptr;
        for ( usize i = 0u; i <= nSteps; ++i ) {
            auto *pItem = new QTreeWidgetItem( m_pTree );
            QString label = i == 0u ? QStringLiteral( "Base state" ) : Label( EditorHistory_StepLabel( m_pHistory, i - 1u ) );
            if ( label.isEmpty() ) { label = QStringLiteral( "Unnamed edit" ); }
            pItem->setText( 0, label );
            pItem->setData( 0, Qt::UserRole, QVariant::fromValue<qulonglong>( i ) );
            if ( i == nApplied ) {
                pItem->setText( 1, bClean ? QStringLiteral( "Current \u00B7 clean" ) : QStringLiteral( "Current" ) );
                pItem->setIcon( 0, EditorStyle_Icon( *m_pStyle, "view-history" ) );
                QFont font = pItem->font( 0 );
                font.setBold( true );
                pItem->setFont( 0, font );
                pItem->setFont( 1, font );
                pCurrent = pItem;
            } else {
                pItem->setText( 1, i < nApplied ? QStringLiteral( "Applied" ) : QStringLiteral( "Redo" ) );
                if ( i > nApplied ) {
                    const QColor muted = EditorStyle_TokenColor( *m_pStyle, "ui.text.muted" );
                    pItem->setForeground( 0, muted );
                    pItem->setForeground( 1, muted );
                }
            }
            const QString state = pItem->text( 1 );
            const QString description = i == 0u ? QStringLiteral( "Oldest state reachable with the retained undo steps. Older edits may have left the history." )
                                                : QStringLiteral( "Return to the document after: %1" ).arg( label );
            pItem->setToolTip( 0, QStringLiteral( "%1 \u00B7 %2\n%3" ).arg( label, state, description ) );
            pItem->setData( 0, Qt::AccessibleTextRole, QStringLiteral( "%1, %2" ).arg( label, state ) );
        }
        m_pTree->setCurrentItem( pCurrent );
        if ( pCurrent != nullptr ) { m_pTree->scrollToItem( pCurrent ); }
        m_pCount->setText( nApplied == nSteps ? QStringLiteral( "%1 %2" ).arg( nSteps ).arg( nSteps == 1u ? QStringLiteral( "edit" ) : QStringLiteral( "edits" ) )
                                              : QStringLiteral( "%1 / %2" ).arg( nApplied ).arg( nSteps ) );
        m_pCount->setToolTip( QStringLiteral( "%1 of %2 edits applied" ).arg( nApplied ).arg( nSteps ) );
        m_pStatus->setText( bClean ? QStringLiteral( "Clean" ) : QStringLiteral( "Modified" ) );
    }

private:
    enum class action_t { UNDO, REDO, STEP };

    static QToolButton *Button( QWidget *pParent, const char *pName, const char *pLabel )
    {
        auto *pButton = new QToolButton( pParent );
        pButton->setObjectName( QString::fromLatin1( pName ) );
        pButton->setText( QString::fromLatin1( pLabel ) );
        pButton->setAccessibleName( QString::fromLatin1( pLabel ) );
        pButton->setAutoRaise( true );
        pButton->setToolButtonStyle( Qt::ToolButtonIconOnly );
        pButton->setIconSize( QSize( 18, 18 ) );
        return pButton;
    }

    static void OnChanged( void *pContext ) noexcept { static_cast<history_panel_t *>( pContext )->Refresh(); }

    void Apply( action_t action, usize nTarget )
    {
        if ( !EditorHistory_IsInitialized( m_pHistory ) || EditorHistory_IsTransactionOpen( m_pHistory ) ) { return; }
        if ( action == action_t::STEP && nTarget == EditorHistory_AppliedStepCount( m_pHistory ) ) { return; }
        const editor_history_status_t status = action == action_t::UNDO ? EditorHistory_Undo( m_pHistory )
                                              : action == action_t::REDO ? EditorHistory_Redo( m_pHistory )
                                                                         : EditorHistory_StepTo( m_pHistory, nTarget );
        Refresh();
        if ( status == editor_history_status_t::APPLY_FAILED ) {
            m_pStatus->setText( QStringLiteral( "History step failed. The document may be partly changed; see Console." ) );
        }
        if ( m_pfnApplied != nullptr && ( status == editor_history_status_t::OK || status == editor_history_status_t::APPLY_FAILED ) ) {
            // Last operation: a document refresh may close this panel.
            m_pfnApplied( m_pContext, status );
        }
    }

    const editor_style_t *m_pStyle{ nullptr };
    editor_history_t *m_pHistory{ nullptr };
    editor_history_panel_applied_fn m_pfnApplied{ nullptr };
    void *m_pContext{ nullptr };
    QToolButton *m_pUndo{ nullptr };
    QToolButton *m_pRedo{ nullptr };
    QTreeWidget *m_pTree{ nullptr };
    QLabel *m_pCount{ nullptr };
    QLabel *m_pStatus{ nullptr };
};

} // namespace

QWidget *EditorHistoryPanel_Create( QWidget *pParent, editor_history_t *pHistory, const editor_style_t *pStyle,
                                   editor_history_panel_applied_fn pfnApplied, void *pContext )
{
    CY_ASSERT( pStyle != nullptr );
    if ( pStyle == nullptr ) { return nullptr; }
    auto *pPanel = new history_panel_t( pParent, pStyle, pfnApplied, pContext );
    if ( !pPanel->Bind( pHistory ) ) {
        delete pPanel;
        return nullptr;
    }
    return pPanel;
}

bool_t EditorHistoryPanel_Bind( QWidget *pPanel, editor_history_t *pHistory )
{
    auto *pImpl = dynamic_cast<history_panel_t *>( pPanel );
    CY_ASSERT( pImpl != nullptr );
    return pImpl != nullptr ? pImpl->Bind( pHistory ) : CY_FALSE;
}

void EditorHistoryPanel_Refresh( QWidget *pPanel )
{
    auto *pImpl = dynamic_cast<history_panel_t *>( pPanel );
    CY_ASSERT( pImpl != nullptr );
    if ( pImpl != nullptr ) { pImpl->Refresh(); }
}

} // namespace cypher::editor::gui
