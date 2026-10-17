//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_EntityIo.cpp
//  Purpose: Implements the Entity I/O window over CypherMap_EntityOutputs
//           and the wireframe's resolved connections.
//
//  History:
//  - Created by Karlo Siric on 2026-10-06
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_EntityIo.h"
#include "CypherMapGui_Game.h"
#include "CypherMapGui_Layers.h"
#include "CypherMapGui_Notice.h"

#include "CypherMap_EntityEdit.h"
#include "CypherMap_EntityOutputs.h"

#include <QCompleter>

#include <cstring>
#include <functional>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <new>
#include <unordered_map>
#include <vector>

namespace cypher::editor::map
{
using namespace common;

namespace
{
enum output_column_t : int { COL_OUTPUT = 0, COL_TARGET, COL_INPUT, COL_PARAMETER, COL_DELAY, COL_TIMES, COL_COUNT };

QString Q( const std::string &text ) { return QString::fromStdString( text ); }
std::string S( const QString &text ) { return text.toStdString(); }

// Text cells get a line edit with the map's names; Delay and Times spin.
class output_delegate_t final : public QStyledItemDelegate
{
public:
    explicit output_delegate_t( QObject *pParent ) : QStyledItemDelegate( pParent ) {}
    QStringList outputs, targets, inputs;
    // The inputs of the classes a target name reaches (game profile), first.
    std::function<QStringList( const QString &target )> inputsOf{};

    QWidget *createEditor( QWidget *pParent, const QStyleOptionViewItem &option, const QModelIndex &index ) const override
    {
        if ( index.column() == COL_DELAY ) {
            auto *spin = new QDoubleSpinBox( pParent );
            spin->setRange( 0.0, 1.0e6 ); spin->setDecimals( 3 ); spin->setSingleStep( 0.1 ); spin->setSuffix( QStringLiteral( " s" ) );
            return spin;
        }
        if ( index.column() == COL_TIMES ) {
            auto *spin = new QSpinBox( pParent );
            spin->setRange( -1, 1000000 ); spin->setSpecialValueText( QStringLiteral( "Unlimited" ) ); // -1 shows as Unlimited; 0 is skipped below.
            return spin;
        }
        auto *edit = qobject_cast<QLineEdit *>( QStyledItemDelegate::createEditor( pParent, option, index ) );
        QStringList targetInputs;
        if ( index.column() == COL_INPUT && inputsOf ) {
            targetInputs = inputsOf( index.siblingAtColumn( COL_TARGET ).data().toString() );
            for ( const QString &input : inputs ) { if ( !targetInputs.contains( input ) ) { targetInputs.append( input ); } }
        }
        const QStringList *names = index.column() == COL_OUTPUT ? &outputs : index.column() == COL_TARGET ? &targets : index.column() == COL_INPUT ? ( inputsOf ? &targetInputs : &inputs ) : nullptr;
        if ( edit != nullptr && names != nullptr ) {
            auto *completer = new QCompleter( *names, edit );
            completer->setCaseSensitivity( Qt::CaseInsensitive );
            completer->setFilterMode( Qt::MatchContains );
            edit->setCompleter( completer );
        }
        return edit;
    }
    // Times -1 reads as Unlimited, delays in seconds; the stored values stay numbers.
    void initStyleOption( QStyleOptionViewItem *pOption, const QModelIndex &index ) const override
    {
        QStyledItemDelegate::initStyleOption( pOption, index );
        if ( index.column() == COL_TIMES && index.data( Qt::EditRole ).toInt() == -1 ) { pOption->text = QStringLiteral( "Unlimited" ); }
        if ( index.column() == COL_DELAY ) { pOption->text = QStringLiteral( "%1 s" ).arg( index.data( Qt::EditRole ).toDouble() ); }
    }
    void setEditorData( QWidget *pEditor, const QModelIndex &index ) const override
    {
        if ( auto *spin = qobject_cast<QDoubleSpinBox *>( pEditor ) ) { spin->setValue( index.data( Qt::EditRole ).toDouble() ); return; }
        if ( auto *spin = qobject_cast<QSpinBox *>( pEditor ) ) { const int times = index.data( Qt::EditRole ).toInt(); spin->setValue( times == -1 ? -1 : times ); return; }
        QStyledItemDelegate::setEditorData( pEditor, index );
    }
    void setModelData( QWidget *pEditor, QAbstractItemModel *pModel, const QModelIndex &index ) const override
    {
        if ( auto *spin = qobject_cast<QDoubleSpinBox *>( pEditor ) ) { pModel->setData( index, spin->value(), Qt::EditRole ); return; }
        if ( auto *spin = qobject_cast<QSpinBox *>( pEditor ) ) {
            const int value = spin->value() == 0 ? 1 : spin->value(); // Firing zero times is not a connection.
            pModel->setData( index, value, Qt::EditRole );
            return;
        }
        QStyledItemDelegate::setModelData( pEditor, pModel, index );
    }
};

// The Outputs and Inputs pages, added to a tab bar: the Entity I/O window's
// own, or Object Properties' (Hammer 5 shows them beside the properties).
// The widget itself only owns the logic; the pages live in the tabs.
class entity_io_t final : public QWidget
{
public:
    entity_io_t( QWidget *pParent, map_workspace_t *ws, QTabWidget *tabs ) : QWidget( pParent ), m_ws( ws ), m_tabs( tabs )
    {
        setObjectName( QStringLiteral( "MapEntityIoPages" ) );
        hide();
        auto *outputsPage = new QWidget( tabs );
        auto *outputsLayout = new QVBoxLayout( outputsPage );
        m_identity = new QLabel( outputsPage );
        m_identity->setObjectName( QStringLiteral( "MapEntityIoIdentity" ) );
        outputsLayout->addWidget( m_identity );
        m_outputs = new QTableWidget( 0, COL_COUNT, outputsPage );
        m_outputs->setObjectName( QStringLiteral( "MapEntityOutputs" ) );
        m_outputs->setHorizontalHeaderLabels( { QStringLiteral( "My Output" ), QStringLiteral( "Target Entity" ), QStringLiteral( "Target Input" ),
                                                QStringLiteral( "Parameter" ), QStringLiteral( "Delay" ), QStringLiteral( "Times" ) } );
        m_outputs->horizontalHeader()->setStretchLastSection( true );
        m_outputs->setSelectionBehavior( QAbstractItemView::SelectRows );
        m_outputs->setSelectionMode( QAbstractItemView::SingleSelection );
        m_delegate = new output_delegate_t( m_outputs );
        m_outputs->setItemDelegate( m_delegate );
        outputsLayout->addWidget( m_outputs, 1 );
        auto *buttons = new QHBoxLayout();
        const auto button = [&]( const char *name, const char *text, auto action ) {
            auto *b = new QPushButton( QString::fromLatin1( text ), outputsPage );
            b->setObjectName( QString::fromLatin1( name ) );
            QObject::connect( b, &QPushButton::clicked, this, [this, action]() {
                try { action(); }
                catch ( const std::bad_alloc & ) { MemoryRefusal(); }
            } );
            buttons->addWidget( b );
            return b;
        };
        m_add = button( "MapEntityOutputAdd", "Add", [this]() { Add(); } );
        m_duplicate = button( "MapEntityOutputDuplicate", "Duplicate", [this]() { Duplicate(); } );
        m_remove = button( "MapEntityOutputRemove", "Remove", [this]() { Remove(); } );
        m_up = button( "MapEntityOutputUp", "Move Up", [this]() { Move( -1 ); } );
        m_down = button( "MapEntityOutputDown", "Move Down", [this]() { Move( 1 ); } );
        buttons->addStretch( 1 );
        outputsLayout->addLayout( buttons );
        m_outputsTab = tabs->addTab( outputsPage, QStringLiteral( "Outputs" ) );

        m_inputs = new QTableWidget( 0, 5, tabs );
        m_inputs->setObjectName( QStringLiteral( "MapEntityInputs" ) );
        m_inputs->setHorizontalHeaderLabels( { QStringLiteral( "Source Entity" ), QStringLiteral( "Output" ), QStringLiteral( "My Input" ),
                                               QStringLiteral( "Parameter" ), QStringLiteral( "Delay" ) } );
        m_inputs->horizontalHeader()->setStretchLastSection( true );
        m_inputs->setEditTriggers( QAbstractItemView::NoEditTriggers );
        m_inputs->setSelectionBehavior( QAbstractItemView::SelectRows );
        m_inputs->setToolTip( QStringLiteral( "Connections of other entities that reach this one. Double-click to select the source." ) );
        QObject::connect( m_inputs, &QTableWidget::cellDoubleClicked, this, [this]( int row, int ) {
            const u64 source = m_inputs->item( row, 0 )->data( Qt::UserRole ).toULongLong();
            if ( source != 0 ) { MapWorkspace_Select( m_ws, source, MAP_SELECT_REPLACE ); }
        } );
        m_inputsTab = tabs->addTab( m_inputs, QStringLiteral( "Inputs" ) );

        // Committing rebuilds the table: never from inside the editor's own
        // write-back, so the edit lands on the next turn of the event loop.
        QObject::connect( m_outputs, &QTableWidget::itemChanged, this, [this]( QTableWidgetItem * ) { QueueCommit(); } );
        QObject::connect( m_outputs, &QTableWidget::itemSelectionChanged, this, [this]() { RefreshButtons(); } );
        m_subscribed = MapWorkspace_AddListener( m_ws, &entity_io_t::OnChanged, this ) != CY_FALSE;
        Refresh();
    }
    ~entity_io_t() override { if ( m_subscribed ) { MapWorkspace_RemoveListener( m_ws, &entity_io_t::OnChanged, this ); } }

    void Refresh()
    {
        // Rebuilding cancels every pending table write, even when New/Open or
        // Undo restores the same entity ID. No old callback may read a new table.
        ++m_tableGeneration;
        m_commitQueued = false;
        m_entity = MapEntityIo_Entity( m_ws );
        std::vector<map_output_t> outputs;
        const bool readable = m_entity != 0 && MapEntityOutputs_Read( m_ws->pDocument, m_entity, outputs ) == map_status_t::OK;
        const auto *entity = m_entity != 0 ? MapWireframe_FindEntity( m_ws->wire, m_entity ) : nullptr;
        m_identity->setText( entity != nullptr ? QStringLiteral( "%1 \"%2\" · #%3" ).arg( QString::fromUtf8( entity->className ) ).arg( QString::fromUtf8( entity->name ) ).arg( m_entity )
                                               : QStringLiteral( "Select one entity to edit its outputs." ) );
        Suggestions();
        {
            const QSignalBlocker blocker( m_outputs );
            m_outputs->setRowCount( readable ? static_cast<int>( outputs.size() ) : 0 );
            for ( int row = 0; readable && row < static_cast<int>( outputs.size() ); ++row ) {
                const auto &o = outputs[static_cast<usize>( row )];
                const QString texts[]{ Q( o.output ), Q( o.target ), Q( o.input ), Q( o.parameter ) };
                for ( int column = 0; column < 4; ++column ) { m_outputs->setItem( row, column, new QTableWidgetItem( texts[column] ) ); }
                auto *delay = new QTableWidgetItem(); delay->setData( Qt::EditRole, o.delay );
                auto *times = new QTableWidgetItem(); times->setData( Qt::EditRole, static_cast<int>( o.times ) );
                if ( o.times == -1 ) { times->setToolTip( QStringLiteral( "Unlimited" ) ); }
                m_outputs->setItem( row, COL_DELAY, delay ); m_outputs->setItem( row, COL_TIMES, times );
                Status( row );
            }
        }
        RefreshInputs();
        RefreshButtons();
        m_outputs->resizeColumnsToContents();
        m_inputs->resizeColumnsToContents();
        // The tab titles say how many connections there are.
        if ( m_tabs != nullptr && m_outputsTab >= 0 && m_inputsTab >= 0 ) {
            m_tabs->setTabText( m_outputsTab, m_entity != 0 ? QStringLiteral( "Outputs (%1)" ).arg( m_outputs->rowCount() ) : QStringLiteral( "Outputs" ) );
            m_tabs->setTabText( m_inputsTab, m_entity != 0 ? QStringLiteral( "Inputs (%1)" ).arg( m_inputs->rowCount() ) : QStringLiteral( "Inputs" ) );
        }
    }

private:
    static void OnChanged( void *pContext, u32 changes ) noexcept
    {
        auto *self = static_cast<entity_io_t *>( pContext );
        try {
            if ( ( changes & ( MAP_CHANGE_DOCUMENT | MAP_CHANGE_SELECTION ) ) != 0u ) { self->Refresh(); }
            else if ( ( changes & ( MAP_CHANGE_VIEW | MAP_CHANGE_HISTORY ) ) != 0u ) {
                // Lock changes and edit/preview takeover can invalidate a queued
                // write without replacing the document. Normal navigation should
                // not rebuild a table that has no pending changes.
                if ( self->m_commitQueued ) { self->Refresh(); }
                else { self->RefreshButtons(); }
            }
        } catch ( const std::bad_alloc & ) { self->MemoryRefusal(); }
    }

    bool Writable() const noexcept
    {
        return m_entity != 0 && m_ws != nullptr && m_ws->pGui != nullptr && m_ws->pDocument != nullptr &&
            !m_ws->pDocument->bReadOnly && !m_ws->editPreview.bActive && !EditorHistory_IsTransactionOpen( &m_ws->history ) &&
            MapWireframe_FindEntity( m_ws->wire, m_entity ) != nullptr && !MapLayers_IsObjectLocked( m_ws, m_entity );
    }

    struct edit_context_t {
        const map_document_t *document{};
        u64 entity{}, geometryRevision{}, selectionRevision{}, generation{};
    };

    edit_context_t Context() const noexcept
    {
        return { m_ws->pDocument, m_entity, m_ws->pDocument != nullptr ? m_ws->pDocument->geometry.revision : 0,
                 m_ws->selection.revision, m_tableGeneration };
    }

    bool Current( const edit_context_t &context ) const noexcept
    {
        return Writable() && context.document == m_ws->pDocument && context.entity == m_entity &&
            context.geometryRevision == m_ws->pDocument->geometry.revision &&
            context.selectionRevision == m_ws->selection.revision && context.generation == m_tableGeneration;
    }

    void MemoryRefusal( bool outputsUnchanged = false )
    {
        try {
            MapNotice_Post( m_ws, map_notice_t::ERROR, outputsUnchanged ?
                QStringLiteral( "Entity I/O: not enough memory; outputs were not changed." ) :
                QStringLiteral( "Entity I/O: not enough memory to finish the action. Review the current outputs before retrying." ) );
            Refresh();
        } catch ( const std::bad_alloc & ) {
            // A system allocation can also refuse the UI refresh itself. Keep
            // old rows inspectable but disable every write until a fresh read.
            ++m_tableGeneration; m_commitQueued = false; m_entity = 0;
            RefreshButtons();
        }
    }

    void QueueCommit()
    {
        if ( m_commitQueued ) { return; } // Coalesce cells edited in one event turn.
        const edit_context_t context = Context();
        m_commitQueued = true;
        QTimer::singleShot( 0, this, [this, context]() {
            // A refresh can have queued a newer request. An old timer must not
            // clear that request or publish the new selection's table.
            if ( context.generation != m_tableGeneration ) { return; }
            m_commitQueued = false;
            try {
                if ( !Current( context ) ) { Refresh(); return; }
                Commit( "Edit Output", Table() );
            }
            catch ( const std::bad_alloc & ) { MemoryRefusal(); }
        } );
    }

    void RefreshButtons()
    {
        const int row = m_outputs->currentRow(), rows = m_outputs->rowCount();
        const bool picked = row >= 0 && row < rows;
        const bool writable = Writable();
        m_add->setEnabled( writable && rows < static_cast<int>( MAP_ENTITY_OUTPUTS_MAX ) );
        m_duplicate->setEnabled( writable && picked && rows < static_cast<int>( MAP_ENTITY_OUTPUTS_MAX ) );
        m_remove->setEnabled( writable && picked );
        m_up->setEnabled( writable && picked && row > 0 );
        m_down->setEnabled( writable && picked && row + 1 < rows );
        m_outputs->setEditTriggers( writable ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked
                                               : QAbstractItemView::NoEditTriggers );
    }

    // How the wireframe resolved a row: its first connection's status.
    void Status( int row )
    {
        auto *item = m_outputs->item( row, COL_TARGET );
        for ( usize i = 0; item != nullptr && i < m_ws->wire.connections.nCount; ++i ) {
            const auto &c = m_ws->wire.connections.pData[i];
            if ( c.sourceId != m_entity || c.iOutput != static_cast<usize>( row ) ) { continue; }
            const char *text = c.status == map_wire_connection_status_t::RESOLVED ? "Reaches a named entity." :
                               c.status == map_wire_connection_status_t::MISSING_TARGET ? "No entity has this name." :
                               c.status == map_wire_connection_status_t::RUNTIME_TARGET ? "Resolved when the game runs." : "Output, input or target is missing or malformed.";
            item->setToolTip( QString::fromLatin1( text ) );
            if ( c.status == map_wire_connection_status_t::MISSING_TARGET || c.status == map_wire_connection_status_t::INVALID_OUTPUT ) {
                item->setForeground( QColor( 230, 90, 80 ) );
            }
            break;
        }
    }

    void RefreshInputs()
    {
        m_inputs->setRowCount( 0 );
        for ( usize i = 0; m_entity != 0 && i < m_ws->wire.connections.nCount; ++i ) {
            const auto &c = m_ws->wire.connections.pData[i];
            if ( c.targetId != m_entity || c.status != map_wire_connection_status_t::RESOLVED ) { continue; }
            std::vector<map_output_t> sourceOutputs;
            ( void )MapEntityOutputs_Read( m_ws->pDocument, c.sourceId, sourceOutputs );
            const map_output_t o = c.iOutput < sourceOutputs.size() ? sourceOutputs[c.iOutput] : map_output_t{};
            const auto *source = MapWireframe_FindEntity( m_ws->wire, c.sourceId );
            const int row = m_inputs->rowCount();
            m_inputs->insertRow( row );
            auto *name = new QTableWidgetItem( source != nullptr && source->name[0] != '\0' ? QString::fromUtf8( source->name )
                                               : QStringLiteral( "%1 #%2" ).arg( source != nullptr ? QString::fromUtf8( source->className ) : QString() ).arg( c.sourceId ) );
            name->setData( Qt::UserRole, QVariant::fromValue( qulonglong{ c.sourceId } ) );
            m_inputs->setItem( row, 0, name );
            m_inputs->setItem( row, 1, new QTableWidgetItem( QString::fromUtf8( c.output ) ) );
            m_inputs->setItem( row, 2, new QTableWidgetItem( QString::fromUtf8( c.input ) ) );
            m_inputs->setItem( row, 3, new QTableWidgetItem( Q( o.parameter ) ) );
            m_inputs->setItem( row, 4, new QTableWidgetItem( QString::number( o.delay ) ) );
        }
    }

    // Names already in the map: entity names (and the special targets) for
    // Target, outputs and inputs already used for the other two.
    void Suggestions()
    {
        QStringList targets{ QStringLiteral( "!self" ), QStringLiteral( "!activator" ), QStringLiteral( "!caller" ), QStringLiteral( "!player" ) };
        QStringList outputs, inputs;
        for ( usize i = 0; i < m_ws->wire.entities.nCount; ++i ) {
            const QString name = QString::fromUtf8( m_ws->wire.entities.pData[i].name );
            if ( !name.isEmpty() && !targets.contains( name ) ) { targets.append( name ); }
        }
        for ( usize i = 0; i < m_ws->wire.connections.nCount; ++i ) {
            const auto &c = m_ws->wire.connections.pData[i];
            const QString output = QString::fromUtf8( c.output ), input = QString::fromUtf8( c.input );
            if ( !output.isEmpty() && !outputs.contains( output ) ) { outputs.append( output ); }
            if ( !input.isEmpty() && !inputs.contains( input ) ) { inputs.append( input ); }
        }
        targets.sort(); outputs.sort(); inputs.sort();
        // The game's outputs of this entity's class come first.
        if ( const auto *entity = m_entity != 0 ? MapWireframe_FindEntity( m_ws->wire, m_entity ) : nullptr; entity != nullptr ) {
            QStringList own = MapGame_Outputs( m_ws, StringView_FromCString( entity->className ) );
            for ( const QString &output : outputs ) { if ( !own.contains( output ) ) { own.append( output ); } }
            outputs = own;
        }
        m_delegate->targets = targets; m_delegate->outputs = outputs; m_delegate->inputs = inputs;
        m_delegate->inputsOf = [ws = m_ws]( const QString &target ) {
            QStringList names;
            const QByteArray utf8 = target.toUtf8();
            for ( usize i = 0; i < ws->wire.entities.nCount; ++i ) {
                const auto &entity = ws->wire.entities.pData[i];
                if ( utf8.isEmpty() || std::strcmp( entity.name, utf8.constData() ) != 0 ) { continue; }
                for ( const QString &input : MapGame_Inputs( ws, StringView_FromCString( entity.className ) ) ) { if ( !names.contains( input ) ) { names.append( input ); } }
            }
            return names;
        };
    }

    std::vector<map_output_t> Table() const
    {
        std::vector<map_output_t> out;
        for ( int row = 0; row < m_outputs->rowCount(); ++row ) {
            const auto text = [&]( int column ) { const auto *item = m_outputs->item( row, column ); return item != nullptr ? S( item->text() ) : std::string(); };
            map_output_t o{ text( COL_OUTPUT ), text( COL_TARGET ), text( COL_INPUT ), text( COL_PARAMETER ), 0.0, -1 };
            if ( const auto *delay = m_outputs->item( row, COL_DELAY ) ) { o.delay = delay->data( Qt::EditRole ).toDouble(); }
            if ( const auto *times = m_outputs->item( row, COL_TIMES ) ) { o.times = times->data( Qt::EditRole ).toInt(); }
            out.push_back( std::move( o ) );
        }
        return out;
    }

    // One undo step; on refusal the table shows the record again.
    void Commit( const char *label, std::vector<map_output_t> outputs, int select = -1 )
    {
        if ( !Writable() ) { Refresh(); return; }
        struct job_t { edit_context_t context; std::vector<map_output_t> outputs; const entity_io_t *page; } job{ Context(), std::move( outputs ), this };
        const bool done = MapWorkspace_ApplyMetadataEdit( m_ws, label, []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
            auto &j = *static_cast<job_t *>( context );
            if ( !j.page->Current( j.context ) ) { return map_status_t::INVALID_ARGUMENT; }
            const auto *ws = j.page->m_ws;
            for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) { if ( !Vector_PushBack( out, ws->selection.ids.pData[i] ) ) { return map_status_t::OUT_OF_MEMORY; } }
            map_status_t status;
            try { status = MapEntityOutputs_Write( copy, j.context.entity, j.outputs, changed ); }
            catch ( const std::bad_alloc & ) { return map_status_t::OUT_OF_MEMORY; }
            return status == map_status_t::OK && !j.page->Current( j.context ) ? map_status_t::INVALID_ARGUMENT : status;
        }, &job );
        if ( !done ) {
            if ( m_ws->lastEdit.status == map_status_t::OUT_OF_MEMORY ) { MemoryRefusal( true ); }
            else {
                if ( m_ws->lastEdit.outcome != map_edit_outcome_t::UNCHANGED ) {
                    MapNotice_Post( m_ws, map_notice_t::WARNING, QStringLiteral( "Entity I/O: the outputs could not be applied; nothing changed." ) );
                }
                Refresh();
            }
        }
        if ( select >= 0 && select < m_outputs->rowCount() ) { m_outputs->selectRow( select ); }
        RefreshButtons();
    }

    void Add()
    {
        auto outputs = Table();
        // A new connection starts with the most used output name, if any.
        outputs.push_back( { m_delegate->outputs.isEmpty() ? std::string() : S( m_delegate->outputs.front() ), "", "", "", 0.0, -1 } );
        const int row = static_cast<int>( outputs.size() ) - 1;
        Commit( "Add Output", std::move( outputs ), row );
    }
    void Duplicate()
    {
        auto outputs = Table();
        const int row = m_outputs->currentRow();
        if ( row < 0 || row >= static_cast<int>( outputs.size() ) ) { return; }
        outputs.insert( outputs.begin() + row + 1, outputs[static_cast<usize>( row )] );
        Commit( "Duplicate Output", std::move( outputs ), row + 1 );
    }
    void Remove()
    {
        auto outputs = Table();
        const int row = m_outputs->currentRow();
        if ( row < 0 || row >= static_cast<int>( outputs.size() ) ) { return; }
        outputs.erase( outputs.begin() + row );
        const int nextRow = std::min( row, static_cast<int>( outputs.size() ) - 1 );
        Commit( "Remove Output", std::move( outputs ), nextRow );
    }
    void Move( int step )
    {
        auto outputs = Table();
        const int row = m_outputs->currentRow(), to = row + step;
        if ( row < 0 || to < 0 || to >= static_cast<int>( outputs.size() ) ) { return; }
        std::swap( outputs[static_cast<usize>( row )], outputs[static_cast<usize>( to )] );
        Commit( "Reorder Outputs", std::move( outputs ), to );
    }

    map_workspace_t *m_ws{};
    u64 m_entity{ 0 };
    bool m_subscribed{ false };
    bool m_commitQueued{ false };
    u64 m_tableGeneration{ 0 };
    QLabel *m_identity{};
    QTableWidget *m_outputs{};
    QTableWidget *m_inputs{};
    QTabWidget *m_tabs{};
    int m_outputsTab{ -1 }, m_inputsTab{ -1 };
    output_delegate_t *m_delegate{};
    QPushButton *m_add{}, *m_duplicate{}, *m_remove{}, *m_up{}, *m_down{};
};

// The Entity I/O window: the identity line and the two pages.
class entity_io_dialog_t final : public QDialog
{
public:
    entity_io_dialog_t( QWidget *pParent, map_workspace_t *ws ) : QDialog( pParent )
    {
        setObjectName( QStringLiteral( "MapEntityIo" ) );
        setWindowTitle( QStringLiteral( "Entity I/O" ) );
        setModal( false );
        resize( 760, 360 );
        auto *layout = new QVBoxLayout( this );
        auto *tabs = new QTabWidget( this );
        layout->addWidget( tabs, 1 );
        m_pages = new entity_io_t( this, ws, tabs );
    }
    void Refresh() { m_pages->Refresh(); }
private:
    entity_io_t *m_pages{};
};

std::unordered_map<const map_workspace_t *, QPointer<entity_io_dialog_t>> g_windows;
} // namespace

void MapEntityIo_AddTabs( map_workspace_t *ws, QTabWidget *pTabs ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || pTabs == nullptr ) { return; }
    ( void )new entity_io_t( pTabs, ws, pTabs );
}

u64 MapEntityIo_Entity( const map_workspace_t *ws ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr || ws->pDocument == nullptr || ws->selection.ids.nCount == 0 ) { return 0; }
    vector_t<u64> owners{};
    if ( !Vector_Init( &owners, ws->pGui->pAllocator ) ) { return 0; }
    const bool resolved = MapEntityEdit_ResolveOwners( ws->pDocument, { ws->selection.ids.pData, ws->selection.ids.nCount }, &owners ) == map_status_t::OK;
    const u64 entity = resolved && owners.nCount == 1 ? owners.pData[0] : 0;
    Vector_Shutdown( &owners );
    return entity;
}

QDialog *MapEntityIo_Show( map_workspace_t *ws, QWidget *pParent ) noexcept
{
    if ( ws == nullptr || ws->pGui == nullptr ) { return nullptr; }
    auto &window = g_windows[ws];
    if ( window == nullptr ) { window = new entity_io_dialog_t( pParent, ws ); }
    window->Refresh();
    window->show(); window->raise(); window->activateWindow();
    return window;
}

void MapEntityIo_Release( const map_workspace_t *ws ) noexcept
{
    const auto found = g_windows.find( ws );
    if ( found == g_windows.end() ) { return; }
    delete found->second.data();
    g_windows.erase( found );
}

} // namespace cypher::editor::map
