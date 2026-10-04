//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_ShortcutsDialog.cpp
//  Purpose: Implements the Keyboard Shortcuts reference.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditorGui_ShortcutsDialog.h"

#include "CypherEditor_FuzzyMatch.h"
#include "CypherEditor_Keymap.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <vector>

namespace cypher::editor::gui
{

using namespace cypher::common;

namespace
{

constexpr const char *kMouseGroup = "Camera and Mouse";

// Reading order of the groups: files and editing first, as menus are.
constexpr const char *kGroupOrder[]{ "File",   "Edit",     "Selection", "Map Tools",  "Transform", "Brushes", "Mesh",
                                     "Texture", "Terrain", "Grid",      "Views",      "Visibility", "Organisation", "Rendering",
                                     "Map",    "View",     "Assets",    "Tools",      "Console",   "Help",    kMouseGroup };

struct row_t {
    QString group;
    QString label;
    QString keys;      // Native text for display.
    QString keysText;  // Portable text for copying.
    QString id;
    QString description;
    QString context;
    QString emptyStatus;
    bool available{ true };
};

QString KeyText( string_view_t text )
{
    return QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) );
}

const char *PlatformName( keymap_platform_t platform )
{
    switch ( platform ) {
        case keymap_platform_t::MACOS: return "macos";
        case keymap_platform_t::WINDOWS: return "windows";
        case keymap_platform_t::LINUX: return "linux";
        default: return "";
    }
}

const key_value_t *Section( const key_value_t *pRoot, const char *pName, bool overlay )
{
    if ( overlay ) {
        pRoot = KeyValue_Find( KeyValue_Find( pRoot, StringView_FromCString( "platforms" ) ),
                               StringView_FromCString( PlatformName( EditorKeymap_HostPlatform() ) ) );
    }
    return KeyValue_Find( pRoot, StringView_FromCString( pName ) );
}

bool LogicalGesture( string_view_t id )
{
    return StringView_Equals( id, StringView_FromCString( "map.tool.confirm" ) ) ||
           StringView_Equals( id, StringView_FromCString( "map.tool.cancel" ) );
}

QString Humanize( const QString &id )
{
    QStringList parts = id.split( QLatin1Char( '.' ), Qt::SkipEmptyParts );
    if ( !parts.isEmpty() && parts.front() == QStringLiteral( "map" ) ) { parts.removeFirst(); }
    QStringList words;
    for ( const QString &part : parts ) {
        for ( const QString &word : part.split( QLatin1Char( '_' ), Qt::SkipEmptyParts ) ) { words.append( word.left( 1 ).toUpper() + word.mid( 1 ) ); }
    }
    return words.join( QLatin1Char( ' ' ) );
}

int GroupRank( const QString &group )
{
    for ( int i = 0; i < static_cast<int>( std::size( kGroupOrder ) ); ++i ) {
        if ( group == QLatin1String( kGroupOrder[i] ) ) { return i; }
    }
    return static_cast<int>( std::size( kGroupOrder ) );
}

class shortcuts_dialog_t final : public QDialog {
public:
    shortcuts_dialog_t( QWidget *pParent, editor_gui_t *pGui, const editor_actions_t *pActions ) : QDialog( pParent ), m_pGui( pGui ), m_pActions( pActions )
    {
        CY_ASSERT( pGui != nullptr && pActions != nullptr );
        setObjectName( QStringLiteral( "EditorShortcutsDialog" ) );
        setWindowTitle( QStringLiteral( "Keyboard Shortcuts" ) );
        resize( 940, 680 );
        auto *pLayout = new QVBoxLayout( this );
        auto *pTop = new QHBoxLayout();
        m_pSearch = new QLineEdit( this );
        m_pSearch->setObjectName( QStringLiteral( "EditorShortcutsSearch" ) );
        m_pSearch->setPlaceholderText( QStringLiteral( "Search commands, keys, or contexts" ) );
        m_pSearch->setClearButtonEnabled( true );
        m_pBoundOnly = new QCheckBox( QStringLiteral( "Only rows with keys" ), this );
        m_pBoundOnly->setObjectName( QStringLiteral( "EditorShortcutsBoundOnly" ) );
        pTop->addWidget( m_pSearch, 1 );
        pTop->addWidget( m_pBoundOnly );
        pLayout->addLayout( pTop );

        m_pTree = new QTreeWidget( this );
        m_pTree->setObjectName( QStringLiteral( "EditorShortcutsList" ) );
        m_pTree->setColumnCount( 4 );
        m_pTree->setHeaderLabels( { QStringLiteral( "Command" ), QStringLiteral( "Keys" ), QStringLiteral( "Context" ), QStringLiteral( "ID" ) } );
        m_pTree->setUniformRowHeights( true );
        m_pTree->setAlternatingRowColors( true );
        m_pTree->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        m_pTree->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
        m_pTree->header()->setSectionResizeMode( 2, QHeaderView::ResizeToContents );
        m_pTree->header()->setSectionResizeMode( 3, QHeaderView::ResizeToContents );
        m_pTree->header()->setStretchLastSection( false );
        pLayout->addWidget( m_pTree, 1 );

        auto *pScope = new QLabel( QStringLiteral( "Window rows show live action shortcuts. Other rows show keymap declarations; "
                                                 "the active tool, selection mode, and viewport determine their priority. "
                                                 "Declarations may require a supported tool or gesture." ), this );
        pScope->setObjectName( QStringLiteral( "EditorShortcutsScope" ) );
        pScope->setWordWrap( true );
        pScope->setProperty( "muted", true );
        pLayout->addWidget( pScope );

        m_pStatus = new QLabel( this );
        m_pStatus->setProperty( "muted", true );
        pLayout->addWidget( m_pStatus );
        auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Close, this );
        QPushButton *pCopy = pButtons->addButton( QStringLiteral( "Copy List" ), QDialogButtonBox::ActionRole );
        pCopy->setToolTip( QStringLiteral( "Copy the listed commands and keys as plain text" ) );
        pLayout->addWidget( pButtons );

        QObject::connect( m_pSearch, &QLineEdit::textChanged, this, [this]( const QString & ) { Fill(); } );
        QObject::connect( m_pBoundOnly, &QCheckBox::toggled, this, [this]( bool ) { Fill(); } );
        QObject::connect( pCopy, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText( Text() ); } );
        QObject::connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
        m_keymapSubscribed = EditorGui_AddKeymapListener( m_pGui, OnKeymap, this );
        Refresh();
        m_pSearch->setFocus();
    }

    ~shortcuts_dialog_t() override
    {
        if ( m_keymapSubscribed ) { EditorGui_RemoveKeymapListener( m_pGui, OnKeymap, this ); }
    }

    void Refresh()
    {
        m_rows.clear();
        CollectCommands();
        CollectDeclarations();
        std::stable_sort( m_rows.begin(), m_rows.end(), []( const row_t &a, const row_t &b ) {
            const int ra = GroupRank( a.group ), rb = GroupRank( b.group );
            if ( ra != rb ) { return ra < rb; }
            if ( a.group != b.group ) { return a.group < b.group; }
            const int labelOrder = a.label.localeAwareCompare( b.label );
            if ( labelOrder != 0 ) { return labelOrder < 0; }
            if ( a.context == QStringLiteral( "Window" ) && b.context != a.context ) { return true; }
            if ( b.context == QStringLiteral( "Window" ) && a.context != b.context ) { return false; }
            if ( a.context != b.context ) { return a.context < b.context; }
            return a.id < b.id;
        } );
        Fill();
    }

    void SetSearch( const QString &text ) { m_pSearch->setText( text ); }
    void SetBoundOnly( bool bBoundOnly ) { m_pBoundOnly->setChecked( bBoundOnly ); }

    QStringList Rows() const
    {
        QStringList rows;
        for ( int g = 0; g < m_pTree->topLevelItemCount(); ++g ) {
            const QTreeWidgetItem *pGroup = m_pTree->topLevelItem( g );
            for ( int c = 0; c < pGroup->childCount(); ++c ) {
                const QTreeWidgetItem *pItem = pGroup->child( c );
                rows.append( QStringLiteral( "%1\t%2\t%3\t%4" ).arg( pGroup->text( 0 ), pItem->text( 0 ), pItem->data( 1, Qt::UserRole ).toString(),
                                                                     pItem->text( 2 ) ) );
            }
        }
        return rows;
    }

    QString Text() const
    {
        QString text;
        for ( int g = 0; g < m_pTree->topLevelItemCount(); ++g ) {
            const QTreeWidgetItem *pGroup = m_pTree->topLevelItem( g );
            text += pGroup->text( 0 ) + QLatin1Char( '\n' );
            for ( int c = 0; c < pGroup->childCount(); ++c ) {
                const QTreeWidgetItem *pItem = pGroup->child( c );
                const QString keys = pItem->data( 1, Qt::UserRole ).toString();
                text += QStringLiteral( "  %1%2  [%3]\n" ).arg( pItem->text( 0 ).leftJustified( 40, QLatin1Char( ' ' ) ),
                                                                 keys.isEmpty() ? pItem->text( 1 ) : keys, pItem->text( 2 ) );
            }
            text += QLatin1Char( '\n' );
        }
        return text;
    }

private:
    static void OnKeymap( void *pContext ) noexcept { static_cast<shortcuts_dialog_t *>( pContext )->Refresh(); }

    row_t CommandRow( const command_desc_t *pDesc, string_view_t id ) const
    {
        row_t row{};
        row.id = KeyText( id );
        row.label = pDesc != nullptr ? QString::fromUtf8( pDesc->pLabel != nullptr ? pDesc->pLabel : pDesc->pId ).remove( QStringLiteral( "..." ) )
                                    : row.id == QStringLiteral( "map.tool.confirm" ) ? QStringLiteral( "Confirm Tool" ) : QStringLiteral( "Cancel Tool" );
        row.description = pDesc == nullptr ? QStringLiteral( "Logical gesture handled by the active viewport." )
                                            : pDesc->pDescription != nullptr ? QString::fromUtf8( pDesc->pDescription ) : row.id;
        row.group = EditorShortcutsDialog_GroupOf( row.id );
        row.available = pDesc == nullptr || ( EditorCommands_State( &m_pGui->commands, id ) & COMMAND_STATE_ENABLED ) != 0u;
        return row;
    }

    void CollectCommands()
    {
        for ( usize i = 0u; i < EditorCommands_Count( &m_pGui->commands ); ++i ) {
            const command_desc_t *pDesc = EditorCommands_At( &m_pGui->commands, i );
            row_t row = CommandRow( pDesc, StringView_FromCString( pDesc->pId ) );
            row.context = QStringLiteral( "Window" );
            row.emptyStatus = QStringLiteral( "Not bound" );
            if ( const QAction *pAction = m_pActions->actions.value( row.id, nullptr ) ) {
                QStringList native, portable;
                for ( const QKeySequence &sequence : pAction->shortcuts() ) {
                    native.append( sequence.toString( QKeySequence::NativeText ) );
                    portable.append( sequence.toString( QKeySequence::PortableText ) );
                }
                row.keys = native.join( QStringLiteral( ",  " ) );
                row.keysText = portable.join( QStringLiteral( ", " ) );
            }
            m_rows.push_back( row );
        }
    }

    void CollectDeclaration( keymap_section_t section, string_view_t context, string_view_t id )
    {
        row_t row{};
        const auto platform = EditorKeymap_HostPlatform();
        if ( section == keymap_section_t::BINDINGS ) {
            const command_desc_t *pDesc = EditorCommands_Find( &m_pGui->commands, id );
            // Future/plugin declarations must not advertise an implemented
            // command. Confirm/Cancel are logical viewport gestures, however.
            if ( pDesc == nullptr && !LogicalGesture( id ) ) { return; }
            row = CommandRow( pDesc, id );
            keymap_binding_t binding{};
            const auto status = EditorKeymap_FindBindingOn( m_pGui->keymapChain, m_pGui->nKeymapChain, platform, context, id, &binding );
            if ( status == keymap_lookup_t::NOT_DEFINED ) { return; }
            row.emptyStatus = status == keymap_lookup_t::UNBOUND ? QStringLiteral( "Unbound" ) : QStringLiteral( "No valid keys" );
            QStringList native, portable;
            for ( usize i = 0u; i < binding.nChords; ++i ) {
                char text[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
                const usize length = EditorKeyChord_Format( binding.chords[i], text );
                if ( length == 0u ) { continue; }
                portable.append( QString::fromUtf8( text, static_cast<qsizetype>( length ) ) );
                native.append( EditorKeyChord_ToKeySequence( binding.chords[i] ).toString( QKeySequence::NativeText ) );
            }
            row.keys = native.join( QStringLiteral( ",  " ) );
            row.keysText = portable.join( QStringLiteral( ", " ) );
            row.description += QStringLiteral( "\nContext binding declaration resolved through the active keymap and platform overlay." );
        } else {
            keymap_triggers_t triggers{};
            const auto status = EditorKeymap_FindTriggers( m_pGui->keymapChain, m_pGui->nKeymapChain, section, platform, context, id, &triggers );
            if ( status == keymap_lookup_t::NOT_DEFINED ) { return; }
            row.id = KeyText( id );
            row.label = Humanize( row.id );
            row.group = QString::fromLatin1( kMouseGroup );
            row.emptyStatus = status == keymap_lookup_t::UNBOUND ? QStringLiteral( "Unbound" ) : QStringLiteral( "No valid gestures" );
            QStringList texts;
            for ( usize i = 0u; i < triggers.nTexts; ++i ) {
                if ( section == keymap_section_t::HELD ) {
                    key_stroke_t trigger{};
                    char text[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
                    if ( !EditorHeldKey_Parse( triggers.texts[i], &trigger ) ) { continue; }
                    const usize length = EditorHeldKey_Format( trigger, text );
                    if ( length != 0u ) { texts.append( QString::fromUtf8( text, static_cast<qsizetype>( length ) ) ); }
                } else {
                    mouse_gesture_t trigger{};
                    char text[EDITOR_MOUSE_GESTURE_TEXT_CAPACITY]{};
                    if ( !EditorMouseGesture_Parse( triggers.texts[i], &trigger ) ) { continue; }
                    const usize length = EditorMouseGesture_Format( trigger, text );
                    if ( length != 0u ) { texts.append( QString::fromUtf8( text, static_cast<qsizetype>( length ) ) ); }
                }
            }
            row.keys = texts.join( QStringLiteral( ",  " ) );
            row.keysText = texts.join( QStringLiteral( ", " ) );
            row.description = section == keymap_section_t::HELD ? QStringLiteral( "Held-key declaration; activation belongs to the owning view or tool." )
                                                                : QStringLiteral( "Mouse declaration; gesture support and activation belong to the owning view or tool." );
        }
        row.context = KeyText( context );
        m_rows.push_back( std::move( row ) );
    }

    // Discover host-platform-only entries as well as main entries, then
    // resolve each declaration once through the actual inheritance chain.
    // This catalogue intentionally does not invent an active viewport stack.
    void CollectDeclarations()
    {
        QSet<QString> seen;
        for ( const auto &[pSection, section] : { std::pair{ "bindings", keymap_section_t::BINDINGS }, std::pair{ "held", keymap_section_t::HELD },
                                                std::pair{ "mouse", keymap_section_t::MOUSE } } ) {
            for ( usize iChain = 0u; iChain < m_pGui->nKeymapChain && iChain < EDITOR_KEYMAP_MAX_DEPTH; ++iChain ) {
                const key_value_t *pRoot = m_pGui->keymapChain[iChain];
                for ( const bool overlay : { true, false } ) {
                    const key_value_t *pContexts = Section( pRoot, pSection, overlay );
                    for ( usize c = 0u; pContexts != nullptr && c < KeyValue_ChildCount( pContexts ); ++c ) {
                        const key_value_t *pContext = KeyValue_ChildAt( pContexts, c );
                        if ( KeyValue_Type( pContext ) != key_value_type_t::OBJECT ) { continue; }
                        const string_view_t context = KeyValue_Name( pContext );
                        for ( usize a = 0u; a < KeyValue_ChildCount( pContext ); ++a ) {
                            const string_view_t id = KeyValue_Name( KeyValue_ChildAt( pContext, a ) );
                            const QString key = QString::fromUtf8( pSection ) + QLatin1Char( '/' ) + KeyText( context ) + QLatin1Char( '/' ) + KeyText( id );
                            if ( seen.contains( key ) ) { continue; }
                            seen.insert( key );
                            CollectDeclaration( section, context, id );
                        }
                    }
                }
            }
        }
    }

    bool Matches( const row_t &row, const QByteArray &query ) const
    {
        if ( m_pBoundOnly->isChecked() && row.keys.isEmpty() ) { return false; }
        if ( query.isEmpty() ) { return true; }
        const string_view_t q{ query.constData(), static_cast<usize>( query.size() ) };
        for ( const QString &field : { row.label, row.id, row.keysText, row.context, row.group, row.description } ) {
            const QByteArray utf8 = field.toUtf8();
            if ( EditorFuzzy_Score( string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) }, q ) != EDITOR_FUZZY_NO_MATCH ) { return true; }
        }
        return false;
    }

    void Fill()
    {
        m_pTree->clear();
        const QByteArray query = m_pSearch->text().trimmed().toUtf8();
        QTreeWidgetItem *pGroup = nullptr;
        int nShown = 0, nBound = 0;
        for ( const row_t &row : m_rows ) {
            nBound += row.keys.isEmpty() ? 0 : 1;
            if ( !Matches( row, query ) ) { continue; }
            if ( pGroup == nullptr || pGroup->text( 0 ) != row.group ) {
                pGroup = new QTreeWidgetItem( m_pTree );
                pGroup->setText( 0, row.group );
                pGroup->setFirstColumnSpanned( true );
                pGroup->setFlags( Qt::ItemIsEnabled );
                QFont font = pGroup->font( 0 );
                font.setBold( true );
                pGroup->setFont( 0, font );
            }
            auto *pItem = new QTreeWidgetItem( pGroup );
            pItem->setText( 0, row.label );
            pItem->setText( 1, row.keys.isEmpty() ? row.emptyStatus : row.keys );
            pItem->setData( 1, Qt::UserRole, row.keysText );
            pItem->setText( 2, row.context );
            pItem->setText( 3, row.id );
            QString tip = row.description.isEmpty() ? row.id : row.description;
            if ( !row.available ) {
                tip += QStringLiteral( "\nCurrently unavailable; the command may need a selection or a supported tool." );
                pItem->setForeground( 0, EditorStyle_TokenColor( m_pGui->style, "ui.text.muted" ) );
            }
            for ( int column = 0; column < 4; ++column ) { pItem->setToolTip( column, tip ); }
            if ( row.keys.isEmpty() ) { pItem->setForeground( 1, EditorStyle_TokenColor( m_pGui->style, "ui.text.muted" ) ); }
            ++nShown;
        }
        m_pTree->expandAll();
        m_pStatus->setText( QStringLiteral( "%1 rows shown · %2 of %3 rows have keys" ).arg( nShown ).arg( nBound ).arg( m_rows.size() ) );
    }

    editor_gui_t *m_pGui{ nullptr };
    const editor_actions_t *m_pActions{ nullptr };
    std::vector<row_t> m_rows{};
    QLineEdit *m_pSearch{ nullptr };
    QCheckBox *m_pBoundOnly{ nullptr };
    QTreeWidget *m_pTree{ nullptr };
    QLabel *m_pStatus{ nullptr };
    bool m_keymapSubscribed{ false };
};

shortcuts_dialog_t *AsDialog( QDialog *pDialog )
{
    auto *pImpl = dynamic_cast<shortcuts_dialog_t *>( pDialog );
    CY_ASSERT( pImpl != nullptr );
    return pImpl;
}

} // namespace

QString EditorShortcutsDialog_GroupOf( const QString &commandId )
{
    const QStringList parts = commandId.split( QLatin1Char( '.' ) );
    const QString first = parts.value( 0 );
    if ( first != QStringLiteral( "map" ) ) { return first.left( 1 ).toUpper() + first.mid( 1 ); }
    const QString second = parts.value( 1 );
    struct map_group_t {
        const char *pPrefix;
        const char *pGroup;
    };
    static constexpr map_group_t kMapGroups[]{
        { "tool", "Map Tools" },       { "select", "Selection" },  { "select_mode", "Selection" }, { "view", "Views" },
        { "camera", "Views" },         { "grid", "Grid" },         { "texture", "Texture" },       { "mesh", "Mesh" },
        { "brush", "Brushes" },        { "terrain", "Terrain" },   { "align", "Transform" },       { "transform", "Transform" },
        { "hide", "Visibility" },      { "show", "Visibility" },   { "visgroup", "Visibility" },   { "group", "Organisation" },
        { "layer", "Organisation" },   { "prefab", "Organisation" }, { "render", "Rendering" },
    };
    for ( const map_group_t &group : kMapGroups ) {
        if ( second == QLatin1String( group.pPrefix ) ) { return QString::fromLatin1( group.pGroup ); }
    }
    return QStringLiteral( "Map" );
}

QDialog *EditorShortcutsDialog_Create( QWidget *pParent, editor_gui_t *pGui, const editor_actions_t *pActions )
{
    CY_ASSERT( pGui != nullptr && pActions != nullptr );
    if ( pGui == nullptr || pActions == nullptr ) { return nullptr; }
    return new shortcuts_dialog_t( pParent, pGui, pActions );
}

void EditorShortcutsDialog_Refresh( QDialog *pDialog )
{
    if ( shortcuts_dialog_t *pImpl = AsDialog( pDialog ) ) { pImpl->Refresh(); }
}

void EditorShortcutsDialog_SetSearch( QDialog *pDialog, const QString &text )
{
    if ( shortcuts_dialog_t *pImpl = AsDialog( pDialog ) ) { pImpl->SetSearch( text ); }
}

void EditorShortcutsDialog_SetBoundOnly( QDialog *pDialog, bool bBoundOnly )
{
    if ( shortcuts_dialog_t *pImpl = AsDialog( pDialog ) ) { pImpl->SetBoundOnly( bBoundOnly ); }
}

QStringList EditorShortcutsDialog_Rows( QDialog *pDialog )
{
    shortcuts_dialog_t *pImpl = AsDialog( pDialog );
    return pImpl != nullptr ? pImpl->Rows() : QStringList{};
}

QString EditorShortcutsDialog_Text( QDialog *pDialog )
{
    shortcuts_dialog_t *pImpl = AsDialog( pDialog );
    return pImpl != nullptr ? pImpl->Text() : QString();
}

} // namespace cypher::editor::gui
