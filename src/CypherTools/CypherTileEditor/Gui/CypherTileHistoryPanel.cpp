//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileHistoryPanel.cpp
//  Purpose: Implements the editor's undo/redo history inspector.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileHistoryPanel.h"

#include "CypherTileEditorIcons.h"

#include <QAbstractItemView>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <utility>

namespace cypher::tools::tile_editor
{

namespace
{

constexpr int HISTORY_STATE_ROLE = Qt::UserRole;

QString FromHistoryLabel( string_view_t label )
{
    return label.pData != nullptr && label.cchLength != 0u
        ? QString::fromUtf8( label.pData, static_cast<qsizetype>( label.cchLength ) )
        : QString();
}

QString RevisionText( u64 revision )
{
    return QObject::tr( "r%1" ).arg( static_cast<qulonglong>( revision ) );
}

} // namespace

CypherTileHistoryPanel::CypherTileHistoryPanel( QWidget *pParent )
    : QWidget( pParent )
{
    setObjectName( QStringLiteral( "TileHistoryPanel" ) );
    auto *pRoot = new QVBoxLayout( this );
    pRoot->setContentsMargins( 6, 6, 6, 6 );
    pRoot->setSpacing( 6 );

    auto *pStatus = new QHBoxLayout();
    pStatus->setSpacing( 6 );
    m_pStateBadge = new QLabel( this );
    m_pStateBadge->setObjectName( QStringLiteral( "TileHistoryStateBadge" ) );
    m_pStateBadge->setAlignment( Qt::AlignCenter );
    m_pStateBadge->setMinimumWidth( 62 );
    m_pSummary = new QLabel( this );
    m_pSummary->setObjectName( QStringLiteral( "TileHistorySummary" ) );
    m_pSummary->setTextInteractionFlags( Qt::TextSelectableByMouse );
    m_pSummary->setWordWrap( true );
    pStatus->addWidget( m_pStateBadge );
    pStatus->addWidget( m_pSummary, 1 );
    pRoot->addLayout( pStatus );

    auto *pCommands = new QHBoxLayout();
    pCommands->setSpacing( 4 );
    m_pUndo = new QToolButton( this );
    m_pRedo = new QToolButton( this );
    m_pUndo->setObjectName( QStringLiteral( "TileHistoryUndo" ) );
    m_pRedo->setObjectName( QStringLiteral( "TileHistoryRedo" ) );
    m_pUndo->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    m_pRedo->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    m_pUndo->setIcon( CypherTileEditorIcon_Create( tile_editor_icon_t::UNDO ) );
    m_pRedo->setIcon( CypherTileEditorIcon_Create( tile_editor_icon_t::REDO ) );
    pCommands->addWidget( m_pUndo, 1 );
    pCommands->addWidget( m_pRedo, 1 );
    pRoot->addLayout( pCommands );

    m_pEntries = new QTreeWidget( this );
    m_pEntries->setObjectName( QStringLiteral( "TileHistoryTree" ) );
    m_pEntries->setColumnCount( 2 );
    m_pEntries->setHeaderLabels( { tr( "Action" ), tr( "State" ) } );
    m_pEntries->setRootIsDecorated( false );
    m_pEntries->setUniformRowHeights( true );
    m_pEntries->setAlternatingRowColors( true );
    // The timeline communicates its cursor with an explicit Current marker.
    // Keep Qt item selection disabled: Qt 6.11's macOS accessibility bridge can
    // retain a stale selected-child interface when an inspector tab is hidden
    // and shown, then dereference it from accessibilitySelectedChildren.
    m_pEntries->setSelectionMode( QAbstractItemView::NoSelection );
    m_pEntries->setSelectionBehavior( QAbstractItemView::SelectRows );
    m_pEntries->setEditTriggers( QAbstractItemView::NoEditTriggers );
    m_pEntries->setTextElideMode( Qt::ElideRight );
    m_pEntries->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    m_pEntries->header()->setStretchLastSection( false );
    m_pEntries->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
    m_pEntries->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
    pRoot->addWidget( m_pEntries, 1 );

    m_pDetails = new QLabel( this );
    m_pDetails->setObjectName( QStringLiteral( "TileHistoryDetails" ) );
    m_pDetails->setProperty( "muted", true );
    m_pDetails->setTextFormat( Qt::PlainText );
    m_pDetails->setTextInteractionFlags( Qt::TextSelectableByMouse );
    m_pDetails->setWordWrap( true );
    m_pDetails->setMinimumHeight( m_pDetails->fontMetrics().lineSpacing() * 3 );
    pRoot->addWidget( m_pDetails );

    connect( m_pUndo, &QToolButton::clicked, this, [this] {
        if ( m_undo ) m_undo();
    } );
    connect( m_pRedo, &QToolButton::clicked, this, [this] {
        if ( m_redo ) m_redo();
    } );
    connect( m_pEntries, &QTreeWidget::itemClicked,
             this, [this]( QTreeWidgetItem *pItem, int ) {
        if ( pItem == nullptr ) return;
        refreshStateDetails( static_cast<usize>(
            pItem->data( 0, HISTORY_STATE_ROLE ).toULongLong() ) );

        // NoSelection prevents a selected accessibility child. QTreeWidget may
        // still assign a current index while dispatching a mouse click, so
        // remove it before control returns to AppKit as well. The row itself is
        // retained and the requested details remain visible below the list.
        const QSignalBlocker blockEntries( m_pEntries );
        m_pEntries->setCurrentItem( nullptr );
    } );
    setDocument( nullptr );
}

void CypherTileHistoryPanel::setDocument(
    const tile_map_document_t *pDocument )
{
    m_pDocument = CypherTileMapDocument_IsInitialized( pDocument )
        ? pDocument
        : nullptr;
    refresh();
}

void CypherTileHistoryPanel::setUndoCallback(
    std::function<void()> callback )
{
    m_undo = std::move( callback );
    refresh();
}

void CypherTileHistoryPanel::setRedoCallback(
    std::function<void()> callback )
{
    m_redo = std::move( callback );
    refresh();
}

u64 CypherTileHistoryPanel::stateRevision(
    const tile_map_history_info_t &history,
    usize iState ) const
{
    if ( m_pDocument == nullptr || iState > history.nEntryCount ) return 0u;
    if ( history.nEntryCount == 0u ) return m_pDocument->nCurrentRevision;

    tile_map_history_entry_info_t entry{};
    if ( iState == 0u ) {
        return CypherTileMapDocument_HistoryEntryInfo(
                   m_pDocument, 0u, &entry )
            ? entry.nBeforeRevision
            : 0u;
    }
    return CypherTileMapDocument_HistoryEntryInfo(
               m_pDocument, iState - 1u, &entry )
        ? entry.nAfterRevision
        : 0u;
}

bool CypherTileHistoryPanel::savedRevisionIsVisible(
    const tile_map_history_info_t &history ) const
{
    if ( m_pDocument == nullptr ) return false;
    for ( usize iState = 0u; iState <= history.nEntryCount; ++iState ) {
        if ( stateRevision( history, iState ) == m_pDocument->nSavedRevision ) {
            return true;
        }
    }
    return false;
}

QTreeWidgetItem *CypherTileHistoryPanel::historyRow( usize iState )
{
    while ( m_historyRows.size() <= iState ) {
        auto *pItem = new QTreeWidgetItem( m_pEntries );
        pItem->setHidden( true );
        m_historyRows.push_back( pItem );
    }
    return m_historyRows[iState];
}

void CypherTileHistoryPanel::hideHistoryRows()
{
    // Keep every QTreeWidgetItem alive. Qt's macOS accessibility bridge can
    // retain item wrappers after an AppKit query, so refreshes only hide and
    // reuse rows. Selection is disabled for the entire lifetime of the view.
    m_pEntries->clearSelection();
    m_pEntries->setCurrentItem( nullptr );
    for ( auto *pItem : m_historyRows ) pItem->setHidden( true );
}

void CypherTileHistoryPanel::refresh()
{
    const QSignalBlocker blockEntries( m_pEntries );
    hideHistoryRows();
    m_history = {};

    if ( m_pDocument == nullptr ||
         !CypherTileMapDocument_HistoryInfo( m_pDocument, &m_history ) ) {
        m_pStateBadge->setText( tr( "NO MAP" ) );
        m_pStateBadge->setProperty( "historyState", QStringLiteral( "none" ) );
        m_pSummary->setText( tr( "Open a map to inspect its edit history." ) );
        m_pUndo->setText( tr( "Undo" ) );
        m_pRedo->setText( tr( "Redo" ) );
        m_pUndo->setEnabled( false );
        m_pRedo->setEnabled( false );
        m_pDetails->setText( tr( "No document history is available." ) );
        return;
    }

    const bool dirty = CypherTileMapDocument_IsDirty( m_pDocument );
    const bool canUndo = CypherTileMapDocument_CanUndo( m_pDocument );
    const bool canRedo = CypherTileMapDocument_CanRedo( m_pDocument );
    m_pStateBadge->setText( m_history.bEditGroupOpen
        ? tr( "EDITING" )
        : dirty ? tr( "UNSAVED" ) : tr( "SAVED" ) );
    m_pStateBadge->setProperty( "historyState", m_history.bEditGroupOpen
        ? QStringLiteral( "editing" )
        : dirty ? QStringLiteral( "dirty" ) : QStringLiteral( "saved" ) );
    m_pStateBadge->style()->unpolish( m_pStateBadge );
    m_pStateBadge->style()->polish( m_pStateBadge );

    QString summary = tr( "%1 / %2 actions applied · current %3 · saved %4" )
        .arg( static_cast<qulonglong>( m_history.iCursor ) )
        .arg( static_cast<qulonglong>( m_history.nEntryCount ) )
        .arg( RevisionText( m_pDocument->nCurrentRevision ) )
        .arg( RevisionText( m_pDocument->nSavedRevision ) );
    if ( !savedRevisionIsVisible( m_history ) ) {
        summary += tr( " · saved state is outside retained history" );
    }
    m_pSummary->setText( summary );

    const QString undoLabel = canUndo
        ? FromHistoryLabel( CypherTileMapDocument_UndoLabel( m_pDocument ) )
        : QString();
    const QString redoLabel = canRedo
        ? FromHistoryLabel( CypherTileMapDocument_RedoLabel( m_pDocument ) )
        : QString();
    // Fixed button labels stay readable in a narrow dock. The precise next
    // action remains available in the tooltip and the adjacent timeline.
    m_pUndo->setText( tr( "Undo" ) );
    m_pRedo->setText( tr( "Redo" ) );
    m_pUndo->setToolTip( canUndo
        ? tr( "Undo %1" ).arg( undoLabel )
        : tr( "There is no applied action to undo." ) );
    m_pRedo->setToolTip( canRedo
        ? tr( "Redo %1" ).arg( redoLabel )
        : tr( "There is no action to redo." ) );
    m_pUndo->setEnabled( canUndo && static_cast<bool>( m_undo ) );
    m_pRedo->setEnabled( canRedo && static_cast<bool>( m_redo ) );

    const auto addState = [this](
        usize iState,
        const QString &action,
        u64 revision ) {
        auto *pItem = historyRow( iState );
        pItem->setHidden( false );
        pItem->setData( 0, HISTORY_STATE_ROLE,
                        QVariant::fromValue<qulonglong>( iState ) );
        const bool current = iState == m_history.iCursor;
        const bool applied = iState <= m_history.iCursor;
        const bool saved = revision == m_pDocument->nSavedRevision;
        pItem->setText( 0, current ? tr( "●  %1" ).arg( action ) : action );
        QStringList state;
        state.push_back( RevisionText( revision ) );
        if ( current ) state.push_back( tr( "Current" ) );
        else if ( applied ) state.push_back( tr( "Applied" ) );
        else state.push_back( tr( "Redo" ) );
        if ( saved ) state.push_back( tr( "Saved" ) );
        pItem->setText( 1, state.join( QStringLiteral( " · " ) ) );
        pItem->setFont( 0, m_pEntries->font() );
        pItem->setFont( 1, m_pEntries->font() );
        pItem->setForeground( 0, QBrush{} );
        pItem->setForeground( 1, QBrush{} );
        if ( current ) {
            QFont font = pItem->font( 0 );
            font.setBold( true );
            pItem->setFont( 0, font );
            pItem->setFont( 1, font );
        } else if ( !applied ) {
            pItem->setForeground(
                0, palette().brush( QPalette::Disabled, QPalette::Text ) );
            pItem->setForeground(
                1, palette().brush( QPalette::Disabled, QPalette::Text ) );
        }
        pItem->setToolTip( 0, current
            ? tr( "This is the document's current state." )
            : applied
                ? tr( "This action is applied to the document." )
                : tr( "This action is currently available to redo." ) );
    };

    addState( 0u,
              m_history.nEntryCount == 0u
                  ? tr( "Current document" )
                  : tr( "History boundary" ),
              stateRevision( m_history, 0u ) );
    for ( usize iEntry = 0u; iEntry < m_history.nEntryCount; ++iEntry ) {
        tile_map_history_entry_info_t entry{};
        if ( !CypherTileMapDocument_HistoryEntryInfo(
                 m_pDocument, iEntry, &entry ) ) {
            continue;
        }
        QString action = FromHistoryLabel( entry.label );
        if ( action.isEmpty() ) action = tr( "Unnamed edit" );
        addState( iEntry + 1u, action, entry.nAfterRevision );
    }

    if ( m_history.iCursor < m_historyRows.size() ) {
        m_pEntries->scrollToItem(
            m_historyRows[m_history.iCursor],
            QAbstractItemView::PositionAtCenter );
    }
    refreshStateDetails( m_history.iCursor );
}

void CypherTileHistoryPanel::refreshStateDetails( usize iState )
{
    if ( m_pDocument == nullptr || iState > m_history.nEntryCount ) {
        m_pDetails->setText( tr( "No document history is available." ) );
        return;
    }

    const u64 revision = stateRevision( m_history, iState );
    if ( iState == 0u ) {
        m_pDetails->setText( m_history.nEntryCount == 0u
            ? tr( "The document has no committed edit actions. Current revision: %1." )
                .arg( RevisionText( revision ) )
            : tr( "State before the oldest retained action. Revision: %1." )
                .arg( RevisionText( revision ) ) );
        return;
    }

    tile_map_history_entry_info_t entry{};
    if ( !CypherTileMapDocument_HistoryEntryInfo(
             m_pDocument, iState - 1u, &entry ) ) {
        m_pDetails->setText( tr( "This history entry is no longer available." ) );
        return;
    }
    const QString state = iState == m_history.iCursor
        ? tr( "current" )
        : iState < m_history.iCursor ? tr( "applied" ) : tr( "available to redo" );
    m_pDetails->setText(
        tr( "Action %1 of %2 · %3 · %4 affected record(s) · %5 → %6 · %7 bytes stored" )
            .arg( static_cast<qulonglong>( iState ) )
            .arg( static_cast<qulonglong>( m_history.nEntryCount ) )
            .arg( state )
            .arg( static_cast<qulonglong>( entry.nAffectedElementCount ) )
            .arg( RevisionText( entry.nBeforeRevision ) )
            .arg( RevisionText( entry.nAfterRevision ) )
            .arg( static_cast<qulonglong>( entry.cbStoredChanges ) ) );
}

} // namespace cypher::tools::tile_editor
