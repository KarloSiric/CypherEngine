//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_FindEntities.cpp
//  Purpose: Implements Find Entities and Find and Replace.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_FindEntities.h"
#include "CypherMapGui_Notice.h"
#include "CypherMap_EntityEdit.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace cypher::editor::map
{
using namespace common;

namespace
{
QString Text( const key_value_t *value )
{
    string_view_t text{}; f64 real = 0.0; i64 whole = 0; u64 count = 0; bool_t flag = CY_FALSE;
    if ( KeyValue_GetString( value, &text ) ) { return QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ); }
    if ( KeyValue_GetI64( value, &whole ) ) { return QString::number( whole ); }
    if ( KeyValue_GetU64( value, &count ) ) { return QString::number( count ); }
    if ( KeyValue_GetF64( value, &real ) ) { return QString::number( real ); }
    if ( KeyValue_GetBool( value, &flag ) ) { return flag ? QStringLiteral( "true" ) : QStringLiteral( "false" ); }
    return QString();
}

// Every searchable (key, value) of an entity: class, name, then properties.
template <typename Visit>
void EachValue( const map_workspace_t *ws, u64 id, Visit visit )
{
    const key_value_t *record = MapEntityEdit_FindEntity( ws->pDocument, id );
    if ( record == nullptr ) { return; }
    for ( const char *field : { "class", "name" } ) {
        if ( const key_value_t *node = KeyValue_Find( record, StringView_FromCString( field ) ) ) { visit( QString::fromLatin1( field ), Text( node ), true ); }
    }
    const key_value_t *properties = KeyValue_Find( record, StringView_FromCString( "properties" ) );
    for ( usize i = 0; i < KeyValue_ChildCount( properties ); ++i ) {
        const key_value_t *child = KeyValue_ChildAt( properties, i );
        const string_view_t key = KeyValue_Name( child );
        string_view_t text{};
        visit( QString::fromUtf8( key.pData, static_cast<qsizetype>( key.cchLength ) ), Text( child ), KeyValue_GetString( child, &text ) != CY_FALSE );
    }
}

class find_window_t final : public QDialog {
public:
    find_window_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QDialog( pParent ), m_pWorkspace( pWorkspace )
    {
        setObjectName( QStringLiteral( "MapFindEntities" ) );
        setAttribute( Qt::WA_DeleteOnClose );
        auto *pLayout = new QVBoxLayout( this );
        const auto field = [this, pLayout]( const QString &label, const QString &name, const QString &hint ) {
            auto *row = new QHBoxLayout();
            auto *caption = new QLabel( label, this ); caption->setMinimumWidth( 90 );
            auto *edit = new QLineEdit( this ); edit->setObjectName( name ); edit->setPlaceholderText( hint ); edit->setClearButtonEnabled( true );
            row->addWidget( caption ); row->addWidget( edit, 1 );
            pLayout->addLayout( row );
            return std::pair<QLabel *, QLineEdit *>{ caption, edit };
        };
        m_pFind = field( QStringLiteral( "Find" ), QStringLiteral( "MapFindText" ), QStringLiteral( "class, name or value (empty: every entity)" ) ).second;
        m_pKey = field( QStringLiteral( "In key" ), QStringLiteral( "MapFindKey" ), QStringLiteral( "any key (or class, name, a property)" ) ).second;
        std::tie( m_pWithLabel, m_pWith ) = field( QStringLiteral( "Replace with" ), QStringLiteral( "MapFindReplaceWith" ), QStringLiteral( "the new text" ) );
        m_pResults = new QTreeWidget( this );
        m_pResults->setObjectName( QStringLiteral( "MapFindResults" ) );
        m_pResults->setColumnCount( 4 );
        m_pResults->setHeaderLabels( { QStringLiteral( "Class" ), QStringLiteral( "Name" ), QStringLiteral( "Match" ), QStringLiteral( "ID" ) } );
        m_pResults->setRootIsDecorated( false ); m_pResults->setAlternatingRowColors( true );
        m_pResults->setSelectionMode( QAbstractItemView::ExtendedSelection );
        m_pResults->header()->setSectionResizeMode( 2, QHeaderView::Stretch );
        pLayout->addWidget( m_pResults, 1 );
        auto *pButtons = new QHBoxLayout();
        m_pSummary = new QLabel( this ); m_pSummary->setProperty( "muted", true );
        auto *pSelect = new QPushButton( QStringLiteral( "Select All Found" ), this );
        m_pReplace = new QPushButton( QStringLiteral( "Replace All" ), this );
        m_pReplace->setObjectName( QStringLiteral( "MapFindReplaceAll" ) );
        auto *pClose = new QPushButton( QStringLiteral( "Close" ), this );
        pButtons->addWidget( m_pSummary, 1 ); pButtons->addWidget( pSelect ); pButtons->addWidget( m_pReplace ); pButtons->addWidget( pClose );
        pLayout->addLayout( pButtons );
        QObject::connect( m_pFind, &QLineEdit::textChanged, this, [this]() { Search(); } );
        QObject::connect( m_pKey, &QLineEdit::textChanged, this, [this]() { Search(); } );
        QObject::connect( m_pResults, &QTreeWidget::itemDoubleClicked, this, [this]( QTreeWidgetItem *item, int ) {
            const u64 id = item->data( 0, Qt::UserRole ).toULongLong();
            MapWorkspace_Select( m_pWorkspace, id, MAP_SELECT_REPLACE ); MapWorkspace_Frame( m_pWorkspace, CY_TRUE );
        } );
        QObject::connect( pSelect, &QPushButton::clicked, this, [this]() {
            std::vector<u64> ids;
            for ( const auto &match : m_matches ) { if ( std::find( ids.begin(), ids.end(), match.id ) == ids.end() ) { ids.push_back( match.id ); } }
            MapWorkspace_SetSelection( m_pWorkspace, ids.data(), ids.size() );
            if ( !ids.empty() ) { MapWorkspace_Frame( m_pWorkspace, CY_TRUE ); }
        } );
        QObject::connect( m_pReplace, &QPushButton::clicked, this, [this]() {
            QVector<u64> ids;
            for ( const auto &match : m_matches ) { if ( !ids.contains( match.id ) ) { ids.append( match.id ); } }
            const int changed = MapFindEntities_Replace( m_pWorkspace, ids, m_pFind->text(), m_pWith->text(), m_pKey->text().trimmed() );
            MapNotice_Post( m_pWorkspace, changed > 0 ? map_notice_t::SUCCESS : map_notice_t::INFO,
                            changed > 0 ? QStringLiteral( "Replaced in %1 values" ).arg( changed ) : QStringLiteral( "Find and Replace: nothing to replace" ) );
            Search();
        } );
        QObject::connect( pClose, &QPushButton::clicked, this, &QDialog::close );
        ( void )MapWorkspace_AddListener( pWorkspace, &find_window_t::OnChanged, this );
        resize( 640, 460 );
        Search();
    }
    // The workspace may already be gone when a deferred delete runs (it told
    // us at shutdown): only a live one is unsubscribed from.
    ~find_window_t() override { if ( m_pWorkspace != nullptr ) { MapWorkspace_RemoveListener( m_pWorkspace, &find_window_t::OnChanged, this ); } }

    void SetReplace( bool replace )
    {
        setWindowTitle( replace ? QStringLiteral( "Find and Replace" ) : QStringLiteral( "Find Entities" ) );
        m_pWithLabel->setVisible( replace ); m_pWith->setVisible( replace ); m_pReplace->setVisible( replace );
        m_pFind->setFocus(); m_pFind->selectAll();
    }

private:
    static void OnChanged( void *pContext, u32 changes )
    {
        auto *pWindow = static_cast<find_window_t *>( pContext );
        if ( ( changes & MAP_CHANGE_SHUTDOWN ) != 0u ) {
            pWindow->m_pWorkspace = nullptr;
            pWindow->close(); // Deletes itself (WA_DeleteOnClose), with nothing left to reach.
            return;
        }
        if ( ( changes & MAP_CHANGE_DOCUMENT ) != 0u ) { pWindow->Search(); }
    }
    void Search()
    {
        if ( m_pWorkspace == nullptr ) { return; } // Shut down: nothing left to search.
        m_matches = MapFindEntities_Search( m_pWorkspace, m_pFind->text(), m_pKey->text().trimmed() );
        m_pResults->clear();
        for ( const auto &match : m_matches ) {
            const auto *entity = MapWireframe_FindEntity( m_pWorkspace->wire, match.id );
            auto *item = new QTreeWidgetItem( m_pResults );
            item->setText( 0, entity != nullptr ? QString::fromUtf8( entity->className ) : QString() );
            item->setText( 1, entity != nullptr ? QString::fromUtf8( entity->name ) : QString() );
            item->setText( 2, QStringLiteral( "%1 = %2" ).arg( match.key, match.value ) );
            item->setText( 3, QString::number( match.id ) );
            item->setData( 0, Qt::UserRole, QVariant::fromValue<qulonglong>( match.id ) );
        }
        m_pSummary->setText( QStringLiteral( "%1 matches" ).arg( m_matches.size() ) );
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    QLineEdit *m_pFind{ nullptr };
    QLineEdit *m_pKey{ nullptr };
    QLineEdit *m_pWith{ nullptr };
    QLabel *m_pWithLabel{ nullptr };
    QTreeWidget *m_pResults{ nullptr };
    QLabel *m_pSummary{ nullptr };
    QPushButton *m_pReplace{ nullptr };
    QVector<map_entity_match_t> m_matches{};
};
} // namespace

QVector<map_entity_match_t> MapFindEntities_Search( const map_workspace_t *ws, const QString &text, const QString &key )
{
    QVector<map_entity_match_t> matches;
    if ( ws == nullptr || ws->pDocument == nullptr ) { return matches; }
    for ( usize i = 0; i < ws->wire.entities.nCount; ++i ) {
        const u64 id = ws->wire.entities.pData[i].id;
        bool any = false;
        EachValue( ws, id, [&]( const QString &k, const QString &v, bool ) {
            if ( any && text.isEmpty() ) { return; }
            if ( !key.isEmpty() && k.compare( key, Qt::CaseInsensitive ) != 0 ) { return; }
            if ( text.isEmpty() ? k == QStringLiteral( "class" ) : v.contains( text, Qt::CaseInsensitive ) ) { matches.append( { id, k, v } ); any = true; }
        } );
    }
    return matches;
}

int MapFindEntities_Replace( map_workspace_t *ws, const QVector<u64> &entities, const QString &text, const QString &with, const QString &key )
{
    if ( ws == nullptr || ws->pDocument == nullptr || text.isEmpty() || entities.isEmpty() ) { return 0; }
    struct change_t { u64 id; QByteArray key; QByteArray value; bool identity; };
    std::vector<change_t> changes;
    for ( u64 id : entities ) {
        EachValue( ws, id, [&]( const QString &k, const QString &v, bool isString ) {
            if ( !isString || ( !key.isEmpty() && k.compare( key, Qt::CaseInsensitive ) != 0 ) || !v.contains( text, Qt::CaseInsensitive ) ) { return; }
            QString next = v; next.replace( text, with, Qt::CaseInsensitive );
            if ( next == v || ( k == QStringLiteral( "class" ) && next.isEmpty() ) ) { return; }
            changes.push_back( { id, k.toUtf8(), next.toUtf8(), k == QStringLiteral( "class" ) || k == QStringLiteral( "name" ) } );
        } );
    }
    if ( changes.empty() ) { return 0; }
    struct job_t { const std::vector<change_t> *pChanges; const map_workspace_t *pWorkspace; } job{ &changes, ws };
    const bool done = MapWorkspace_ApplyMetadataEdit( ws, "Find and Replace", []( void *pContext, map_document_t *copy, vector_t<u64> *pSelection, bool *pChanged ) noexcept {
        const auto &j = *static_cast<job_t *>( pContext );
        for ( usize i = 0; i < j.pWorkspace->selection.ids.nCount; ++i ) { if ( !Vector_PushBack( pSelection, j.pWorkspace->selection.ids.pData[i] ) ) { return map_status_t::OUT_OF_MEMORY; } }
        key_value_document_desc_t desc{}; desc.pAllocator = copy->pAllocator;
        for ( const auto &c : *j.pChanges ) {
            const string_view_t keyView{ c.key.constData(), static_cast<usize>( c.key.size() ) }, valueView{ c.value.constData(), static_cast<usize>( c.value.size() ) };
            bool changed = false;
            map_status_t status;
            if ( c.identity ) { status = MapEntityEdit_SetIdentityField( copy, { &c.id, 1 }, keyView, valueView, &changed ); }
            else {
                key_value_document_t *document = KeyValue_CreateDocument( desc );
                key_value_t *node = document != nullptr && KeyValue_SetRootType( document, key_value_type_t::OBJECT ) ?
                    KeyValue_ObjectInsert( document, KeyValue_Root( document ), StringView_FromCString( "value" ), key_value_type_t::STRING ) : nullptr;
                status = node != nullptr && KeyValue_SetString( document, node, valueView ) ? MapEntityEdit_SetProperty( copy, { &c.id, 1 }, keyView, node, &changed ) : map_status_t::OUT_OF_MEMORY;
                KeyValue_DestroyDocument( document );
            }
            if ( status != map_status_t::OK ) { return status; }
            *pChanged = *pChanged || changed;
        }
        return map_status_t::OK;
    }, &job );
    return done ? static_cast<int>( changes.size() ) : 0;
}

QDialog *MapFindEntities_Show( map_workspace_t *pWorkspace, QWidget *pParent, bool bReplace ) noexcept
{
    if ( pWorkspace == nullptr ) { return nullptr; }
    static QHash<const map_workspace_t *, QPointer<find_window_t>> windows;
    QPointer<find_window_t> &window = windows[pWorkspace];
    if ( window.isNull() ) { window = new find_window_t( pParent, pWorkspace ); }
    window->SetReplace( bReplace );
    window->show(); window->raise(); window->activateWindow();
    return window.data();
}

} // namespace cypher::editor::map
