//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_CommandPalette.cpp
//  Purpose: Implements the command palette.
//  Details: The palette is a frameless child of the main window rather
//           than a separate top-level popup, so it moves, docks, and
//           screenshots with the window on every platform. It closes when
//           focus leaves it. A command with an action runs through the
//           action, so its checked and enabled states refresh exactly as
//           when chosen from a menu.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_CommandPalette.h"

#include "CypherEditor_FuzzyMatch.h"
#include "CypherEditor_SettingsRegistry.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <vector>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

constexpr int kIdRole = Qt::UserRole;
constexpr int kEnabledRole = Qt::UserRole + 1;
constexpr int kShortcutRole = Qt::UserRole + 2;
constexpr int kWidth = 560;
constexpr int kVisibleRows = 12;
constexpr int kMaxRecent = 64;

QIcon PaletteCommandIcon( const editor_actions_t &actions, const command_desc_t &desc, const QAction *pAction )
{
    // Actions are lazy: a palette-only command may never have appeared in a
    // menu or toolbar. Its descriptor still supplies the same themed icon.
    if ( pAction != nullptr && !pAction->icon().isNull() && !pAction->icon().pixmap( 16, 16 ).isNull() ) { return pAction->icon(); }
    const editor_style_t &style = *actions.pStyle;
    const bool colorChecked = ( desc.flags & COMMAND_FLAG_CHECKABLE ) != 0u;
    if ( desc.pIcon != nullptr && desc.pIcon[0] != '\0' ) {
        QIcon icon = EditorStyle_Icon( style, desc.pIcon, colorChecked );
        if ( !icon.isNull() ) { return icon; }
    }

    const QString id = QString::fromUtf8( desc.pId );
    struct alias_t { const char *pCommand; const char *pIcon; };
    constexpr alias_t aliases[]{
        { "file.exit", "file-close" },
        { "help.about", "help" },
        { "map.view.center_selection_2d", "view-frame" },
        { "map.view.center_selection_3d", "view-frame" },
    };
    for ( const alias_t &alias : aliases ) {
        if ( id == QLatin1String( alias.pCommand ) ) { return EditorStyle_Icon( style, alias.pIcon, colorChecked ); }
    }

    // Most command IDs directly describe an existing semantic SVG, such as
    // file.save or map.mesh.bevel. Prefer that to a category fallback.
    QStringList names{ id };
    if ( id.startsWith( QStringLiteral( "map." ) ) ) { names.append( id.mid( 4 ) ); }
    for ( QString name : names ) {
        name.replace( QLatin1Char( '.' ), QLatin1Char( '-' ) ).replace( QLatin1Char( '_' ), QLatin1Char( '-' ) );
        const QByteArray utf8 = name.toUtf8();
        if ( QIcon icon = EditorStyle_Icon( style, utf8.constData(), colorChecked ); !icon.isNull() ) { return icon; }
    }

    constexpr alias_t categories[]{
        { "map.tool.", "tool-select" }, { "map.select", "select-objects" }, { "edit.select", "select-objects" },
        { "map.mesh.", "select-meshes" }, { "map.brush.", "tool-block" }, { "map.texture.", "asset-material" },
        { "map.grid", "grid-show" }, { "map.view.", "view-3d" }, { "map.render.", "render-flat" },
        { "map.transform.", "tool-translate" }, { "map.nudge", "tool-translate" }, { "map.terrain.", "tool-terrain" },
        { "map.entity.", "entity-point" }, { "assets.", "asset-browser" }, { "tools.", "settings" },
        { "view.", "view-3d" }, { "file.", "file-new" }, { "edit.", "edit-undo" }, { "help.", "help" },
        { "map.", "asset-map" }, { "plugins.", "plugins" },
    };
    for ( const alias_t &category : categories ) {
        if ( id.startsWith( QLatin1String( category.pCommand ) ) ) { return EditorStyle_Icon( style, category.pIcon, colorChecked ); }
    }
    // A command from a plug-in with no recognized category still has a
    // visible command glyph, rather than an unexplained blank icon column.
    return EditorStyle_Icon( style, "command-palette", colorChecked );
}

// Label on the left, shortcut right-aligned and muted, as menus show them.
class palette_delegate_t final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint( QPainter *pPainter, const QStyleOptionViewItem &option, const QModelIndex &index ) const override
    {
        QStyleOptionViewItem base = option;
        initStyleOption( &base, index );
        const QString shortcut = index.data( kShortcutRole ).toString();
        QStyledItemDelegate::paint( pPainter, base, index );
        if ( shortcut.isEmpty() ) { return; }
        pPainter->save();
        pPainter->setPen( option.palette.color( QPalette::Disabled, QPalette::Text ) );
        pPainter->drawText( option.rect.adjusted( 0, 0, -8, 0 ), Qt::AlignRight | Qt::AlignVCenter, shortcut );
        pPainter->restore();
    }
};
constexpr i32 kIdPenalty = 2; // A label match reads better than an ID match of the same quality.

class command_palette_t final : public QFrame {
public:
    command_palette_t( QWidget *pWindow, editor_actions_t *pActions, const settings_registry_t *pSettings )
        : QFrame( pWindow ), m_pActions( pActions ), m_pSettings( pSettings )
    {
        CY_ASSERT( pWindow != nullptr && pActions != nullptr && pActions->pRegistry != nullptr );
        setObjectName( QStringLiteral( "EditorCommandPalette" ) );
        setFrameShape( QFrame::StyledPanel );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 6, 6, 6, 6 );
        pLayout->setSpacing( 4 );
        m_pQuery = new QLineEdit( this );
        m_pQuery->setObjectName( QStringLiteral( "EditorCommandPaletteQuery" ) );
        m_pQuery->setPlaceholderText( QStringLiteral( "Type a command" ) );
        m_pList = new QListWidget( this );
        m_pList->setObjectName( QStringLiteral( "EditorCommandPaletteList" ) );
        m_pList->setUniformItemSizes( true );
        m_pList->setFocusPolicy( Qt::NoFocus ); // Keys stay in the box; the list follows.
        m_pList->setItemDelegate( new palette_delegate_t( m_pList ) );
        pLayout->addWidget( m_pQuery );
        pLayout->addWidget( m_pList );
        m_pQuery->installEventFilter( this );
        QObject::connect( m_pQuery, &QLineEdit::textChanged, this, [this]( const QString & ) { Refill(); } );
        QObject::connect( m_pList, &QListWidget::itemActivated, this, [this]( QListWidgetItem * ) { ( void )Accept(); } );
        QObject::connect( qApp, &QApplication::focusChanged, this, [this]( QWidget *, QWidget *pNow ) {
            // Clicking anywhere else dismisses it, like a popup.
            if ( isVisible() && ( pNow == nullptr || !isAncestorOf( pNow ) ) ) { hide(); }
        } );
        hide();
    }

    void Open()
    {
        QWidget *pWindow = parentWidget();
        const int width = std::min( kWidth, std::max( 240, pWindow->width() - 40 ) );
        m_pQuery->clear();
        Refill();
        setGeometry( ( pWindow->width() - width ) / 2, 48, width, height() );
        show();
        raise();
        m_pQuery->setFocus( Qt::PopupFocusReason );
    }

    void SetQuery( const QString &query ) { m_pQuery->setText( query ); }

    QStringList Results() const
    {
        QStringList ids;
        for ( int i = 0; i < m_pList->count(); ++i ) { ids.append( m_pList->item( i )->data( kIdRole ).toString() ); }
        return ids;
    }

    bool Accept()
    {
        QListWidgetItem *pItem = m_pList->currentItem();
        if ( pItem == nullptr || !pItem->data( kEnabledRole ).toBool() ) { return false; }
        const QString id = pItem->data( kIdRole ).toString();
        hide();
        const int recentLimit = RecentLimit();
        if ( recentLimit != 0 ) {
            m_recent.removeAll( id );
            m_recent.prepend( id );
        }
        while ( m_recent.size() > recentLimit ) { m_recent.removeLast(); }
        const QByteArray utf8 = id.toUtf8();
        if ( QAction *pAction = EditorActions_Get( m_pActions, utf8.constData() ) ) {
            pAction->trigger();
        } else {
            ( void )EditorCommands_Execute( m_pActions->pRegistry, string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) },
                                            command_args_t{} );
        }
        return true;
    }

protected:
    bool eventFilter( QObject *pWatched, QEvent *pEvent ) override
    {
        if ( pWatched != m_pQuery || pEvent->type() != QEvent::KeyPress ) { return QFrame::eventFilter( pWatched, pEvent ); }
        auto *pKey = static_cast<QKeyEvent *>( pEvent );
        const int count = m_pList->count();
        switch ( pKey->key() ) {
            case Qt::Key_Down:
                if ( count != 0 ) { m_pList->setCurrentRow( ( m_pList->currentRow() + 1 ) % count ); }
                return true;
            case Qt::Key_Up:
                if ( count != 0 ) { m_pList->setCurrentRow( ( m_pList->currentRow() + count - 1 ) % count ); }
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter: ( void )Accept(); return true;
            case Qt::Key_Escape: hide(); return true;
            default: return QFrame::eventFilter( pWatched, pEvent );
        }
    }

private:
    struct candidate_t {
        const command_desc_t *pDesc;
        i32 score;
        int recentRank; // Position in the recent list, or a large number.
        bool bEnabled;
    };

    int RecentLimit() const
    {
        const i64 limit = m_pSettings != nullptr ? EditorSettings_Integer( m_pSettings, "editor.ui.command_palette_recent", EDITOR_PALETTE_MAX_RECENT )
                                                : EDITOR_PALETTE_MAX_RECENT;
        return static_cast<int>( std::clamp<i64>( limit, 0, kMaxRecent ) );
    }

    void Refill()
    {
        const int recentLimit = RecentLimit();
        while ( m_recent.size() > recentLimit ) { m_recent.removeLast(); }
        const bool showUnavailable = m_pSettings == nullptr || EditorSettings_Bool( m_pSettings, "editor.ui.command_palette_show_unavailable", CY_TRUE );
        const QByteArray query = m_pQuery->text().trimmed().toUtf8();
        const string_view_t queryView{ query.constData(), static_cast<usize>( query.size() ) };
        std::vector<candidate_t> candidates;
        const usize nCommands = EditorCommands_Count( m_pActions->pRegistry );
        for ( usize i = 0u; i < nCommands; ++i ) {
            const command_desc_t *pDesc = EditorCommands_At( m_pActions->pRegistry, i );
            if ( ( pDesc->flags & COMMAND_FLAG_CONSOLE_ONLY ) != 0u ) { continue; }
            const bool bEnabled = ( EditorCommands_State( m_pActions->pRegistry, StringView_FromCString( pDesc->pId ) ) & COMMAND_STATE_ENABLED ) != 0u;
            if ( !showUnavailable && !bEnabled ) { continue; }
            const i32 labelScore = EditorFuzzy_Score( StringView_FromCString( pDesc->pLabel ), queryView );
            const i32 idScore = EditorFuzzy_Score( StringView_FromCString( pDesc->pId ), queryView );
            const i32 score = std::max( labelScore, idScore == EDITOR_FUZZY_NO_MATCH ? EDITOR_FUZZY_NO_MATCH : idScore - kIdPenalty );
            if ( score == EDITOR_FUZZY_NO_MATCH ) { continue; }
            const int recent = static_cast<int>( m_recent.indexOf( QString::fromUtf8( pDesc->pId ) ) );
            candidates.push_back( { pDesc, score, recent < 0 ? kMaxRecent + 1 : recent, bEnabled } );
        }
        const bool bEmptyQuery = query.isEmpty();
        std::stable_sort( candidates.begin(), candidates.end(), [bEmptyQuery]( const candidate_t &a, const candidate_t &b ) {
            if ( bEmptyQuery && a.recentRank != b.recentRank ) { return a.recentRank < b.recentRank; }
            if ( a.score != b.score ) { return a.score > b.score; }
            return std::strcmp( a.pDesc->pLabel, b.pDesc->pLabel ) < 0;
        } );
        m_pList->clear();
        const QColor muted = palette().color( QPalette::Disabled, QPalette::Text );
        for ( const candidate_t &candidate : candidates ) {
            if ( m_pList->count() >= EDITOR_PALETTE_MAX_RESULTS ) { break; }
            const command_desc_t &desc = *candidate.pDesc;
            const QString text = QString::fromUtf8( desc.pLabel );
            // The shortcut the keymap gave this command, if it has an action.
            QString shortcut;
            const QAction *pAction = m_pActions->actions.value( QString::fromUtf8( desc.pId ), nullptr );
            if ( pAction != nullptr ) {
                shortcut = pAction->shortcut().toString( QKeySequence::NativeText );
            }
            auto *pItem = new QListWidgetItem( PaletteCommandIcon( *m_pActions, desc, pAction ), text, m_pList );
            pItem->setData( kShortcutRole, shortcut );
            Decorate( pItem, desc, candidate.bEnabled, muted );
        }
        if ( m_pList->count() != 0 ) { m_pList->setCurrentRow( 0 ); }
        // The visible row budget is independent of the searchable result count.
        const int rowHeight = std::max( 18, m_pList->sizeHintForRow( 0 ) );
        const i64 configuredRows = m_pSettings != nullptr ? EditorSettings_Integer( m_pSettings, "editor.ui.command_palette_rows", kVisibleRows )
                                                       : kVisibleRows;
        const int visibleRows = static_cast<int>( std::clamp<i64>( configuredRows, 4, 24 ) );
        const int rows = std::clamp( m_pList->count(), 1, visibleRows );
        const int listHeight = rows * rowHeight + 2 * m_pList->frameWidth() + 4;
        m_pList->setFixedHeight( listHeight );
        const QMargins margins = layout()->contentsMargins();
        resize( width(), margins.top() + m_pQuery->sizeHint().height() + layout()->spacing() + listHeight + margins.bottom() );
    }

    static void Decorate( QListWidgetItem *pItem, const command_desc_t &desc, bool bEnabled, const QColor &muted )
    {
        pItem->setData( kIdRole, QString::fromUtf8( desc.pId ) );
        pItem->setData( kEnabledRole, bEnabled );
        pItem->setToolTip( QStringLiteral( "%1\n%2" ).arg( QString::fromUtf8( desc.pId ), QString::fromUtf8( desc.pDescription != nullptr ? desc.pDescription : "" ) ) );
        if ( !bEnabled ) { pItem->setForeground( muted ); }
    }

    editor_actions_t *m_pActions{ nullptr };
    const settings_registry_t *m_pSettings{ nullptr }; // Borrowed; resolve again on each refill.
    QLineEdit *m_pQuery{ nullptr };
    QListWidget *m_pList{ nullptr };
    QStringList m_recent{};
};

command_palette_t *AsPalette( QWidget *pWidget )
{
    auto *pPalette = dynamic_cast<command_palette_t *>( pWidget );
    CY_ASSERT( pPalette != nullptr );
    return pPalette;
}

} // namespace

QWidget *EditorCommandPalette_Create( QWidget *pWindow, editor_actions_t *pActions, const settings_registry_t *pSettings )
{
    return new command_palette_t( pWindow, pActions, pSettings );
}

void EditorCommandPalette_Open( QWidget *pPalette )
{
    if ( command_palette_t *pImpl = AsPalette( pPalette ) ) { pImpl->Open(); }
}

void EditorCommandPalette_SetQuery( QWidget *pPalette, const QString &query )
{
    if ( command_palette_t *pImpl = AsPalette( pPalette ) ) { pImpl->SetQuery( query ); }
}

QStringList EditorCommandPalette_Results( QWidget *pPalette )
{
    command_palette_t *pImpl = AsPalette( pPalette );
    return pImpl != nullptr ? pImpl->Results() : QStringList{};
}

bool EditorCommandPalette_Accept( QWidget *pPalette )
{
    command_palette_t *pImpl = AsPalette( pPalette );
    return pImpl != nullptr && pImpl->Accept();
}

} // namespace cypher::editor::gui
