//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Panels.cpp
//  Purpose: Implements the Map outliner and properties panels.
//  Details: The outliner is rebuilt from the wireframe on document changes
//           and only re-selected on selection changes; rebuilding thousands
//           of rows for every click would make selecting feel sluggish.
//           A guard flag stops the panel's own selection change from coming
//           back to it through the workspace notification.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Panels.h"
#include "CypherMapGui_EntityProperties.h"
#include "CypherMap_EntityEdit.h"

#include "CypherEditorGui_Style.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMap>
#include <QPainter>
#include <QSet>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace cypher::editor::map
{

using namespace cypher::common;

namespace
{

constexpr int kIdRole = Qt::UserRole;
constexpr int kScalarListMax = 16; // Longer arrays expand into rows instead of one long value.
constexpr int kEntityKeyRole = Qt::UserRole + 20;
constexpr int kEntityIdentityRole = Qt::UserRole + 21;
constexpr int kEntityEditableRole = Qt::UserRole + 22;

const char *KindName( map_wire_kind_t kind ) noexcept
{
    switch ( kind ) {
        case map_wire_kind_t::BRUSH: return "Brush";
        case map_wire_kind_t::MESH: return "Mesh";
        case map_wire_kind_t::PATCH: return "Patch";
        case map_wire_kind_t::TERRAIN: return "Terrain";
        case map_wire_kind_t::ENTITY: return "Entity";
    }
    return "Object";
}

// ---------------------------------------------------------------------------
// Outliner
// ---------------------------------------------------------------------------

// Presentation metadata is separate from the document's stable object ID.
constexpr int kOutlinerKindRole = Qt::UserRole + 1;
constexpr int kOutlinerKeyRole = Qt::UserRole + 2;
constexpr int kOutlinerIconRole = Qt::UserRole + 3;
constexpr int kOutlinerCountRole = Qt::UserRole + 4;
constexpr int kOutlinerTipRole = Qt::UserRole + 5;
constexpr int kOutlinerLabelRole = Qt::UserRole + 6;
constexpr int kOutlinerNamedRole = Qt::UserRole + 7;
constexpr int kOutlinerGroupKind = 5;
constexpr const char *kTreeLinesSetting = "editor.ui.tree_lines";
constexpr const char *kShowIdsSetting = "editor.outliner.show_ids";

// Draw the hierarchy independently of platform/QSS branch resources. Native
// disclosure arrows remain, but lines can be switched off without removing
// the ability to expand a group, entity, or layer.
class map_outliner_tree_t final : public QTreeWidget {
public:
    map_outliner_tree_t( QWidget *pParent, gui::editor_gui_t *pGui ) : QTreeWidget( pParent ), m_pGui( pGui ) {}

protected:
    void drawBranches( QPainter *pPainter, const QRect &rect, const QModelIndex &index ) const override
    {
        if ( rect.isEmpty() || !index.isValid() ) { return; }
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        const int step = rtl ? indentation() : -indentation();
        const int x = rtl ? rect.left() + indentation() / 2 : rect.right() - indentation() / 2;
        const int middle = rect.center().y();
        const bool children = model()->hasChildren( index );
        pPainter->save();
        pPainter->setClipRect( rect, Qt::IntersectClip );
        if ( EditorSettings_Bool( &m_pGui->settings, kTreeLinesSetting, CY_TRUE ) ) {
            QPen pen( gui::EditorStyle_TokenColor( m_pGui->style, "ui.text.disabled" ) );
            pen.setWidthF( 1.0 );
            pen.setCosmetic( true );
            pPainter->setPen( pen );
            if ( index.parent().isValid() ) {
                const int gap = children ? 4 : 0;
                pPainter->drawLine( x, rect.top(), x, middle - gap );
                if ( HasFollowingSibling( index ) ) { pPainter->drawLine( x, middle + gap, x, rect.bottom() ); }
                pPainter->drawLine( x + ( rtl ? -gap : gap ), middle, rtl ? rect.left() : rect.right(), middle );
            }
            int ancestorX = x + step;
            for ( QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent(), ancestorX += step ) {
                if ( parent.parent().isValid() && HasFollowingSibling( parent ) ) {
                    pPainter->drawLine( ancestorX, rect.top(), ancestorX, rect.bottom() );
                }
            }
        }
        if ( children ) {
            QStyleOptionViewItem option;
            option.initFrom( this );
            option.rect = QRect( x - indentation() / 2, rect.top(), indentation(), rect.height() );
            option.state |= QStyle::State_Children;
            if ( isExpanded( index ) ) { option.state |= QStyle::State_Open; }
            // No State_Item/State_Sibling: the style draws only its arrow.
            style()->drawPrimitive( QStyle::PE_IndicatorBranch, &option, pPainter, this );
        }
        pPainter->restore();
    }

private:
    bool HasFollowingSibling( const QModelIndex &index ) const
    {
        const QModelIndex parent = index.parent();
        for ( int row = index.row() + 1; row < model()->rowCount( parent ); ++row ) {
            if ( !isRowHidden( row, parent ) ) { return true; }
        }
        return false;
    }
    gui::editor_gui_t *m_pGui;
};

const key_value_t *OutlinerMember( const key_value_t *pRecord, const char *pName )
{
    return KeyValue_Find( pRecord, StringView_FromCString( pName ) );
}

u64 OutlinerId( const key_value_t *pValue )
{
    u64 id = 0u;
    i64 signedId = 0;
    if ( KeyValue_GetU64( pValue, &id ) ) { return id; }
    return KeyValue_GetI64( pValue, &signedId ) && signedId > 0 ? static_cast<u64>( signedId ) : 0u;
}

QString OutlinerText( const key_value_t *pValue )
{
    string_view_t text{};
    return KeyValue_GetString( pValue, &text ) ? QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ) : QString();
}

const char *OutlinerIcon( map_wire_kind_t kind, const QString &className = QString() )
{
    switch ( kind ) {
        case map_wire_kind_t::BRUSH: return "tool-block";
        case map_wire_kind_t::MESH: return "select-meshes";
        case map_wire_kind_t::PATCH: return "tool-patch";
        case map_wire_kind_t::TERRAIN: return "tool-terrain";
        case map_wire_kind_t::ENTITY: break;
    }
    if ( className.startsWith( QLatin1StringView( "light" ) ) ) { return "entity-light"; }
    if ( className.startsWith( QLatin1StringView( "trigger_" ) ) ) { return "entity-trigger"; }
    if ( className.startsWith( QLatin1StringView( "prop_" ) ) ) { return "entity-prop"; }
    if ( className.startsWith( QLatin1StringView( "info_player" ) ) || className.startsWith( QLatin1StringView( "info_enemy_spawn" ) ) ) {
        return "entity-spawn";
    }
    if ( className.startsWith( QLatin1StringView( "ambient_" ) ) || className.startsWith( QLatin1StringView( "sound" ) ) ) { return "entity-sound"; }
    if ( className.startsWith( QLatin1StringView( "logic_" ) ) ) { return "entity-logic"; }
    return "entity-point";
}

class map_outliner_t final : public QWidget {
public:
    map_outliner_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QWidget( pParent ), m_pWorkspace( pWorkspace )
    {
        CY_ASSERT( pWorkspace != nullptr );
        setObjectName( QStringLiteral( "MapOutliner" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 2 );
        auto *pFilterRow = new QHBoxLayout();
        pFilterRow->setContentsMargins( 2, 2, 2, 0 );
        pFilterRow->setSpacing( 2 );
        m_pFilter = new QLineEdit( this );
        m_pFilter->setObjectName( QStringLiteral( "MapOutlinerFilter" ) );
        m_pFilter->setPlaceholderText( QStringLiteral( "Mapnode filter" ) );
        m_pFilter->setToolTip( QStringLiteral( "Find by name, class, type, or ID. Matching ancestors remain visible.\nFiltering does not change the map selection." ) );
        m_pFilter->setClearButtonEnabled( true );
        m_pFilter->setMinimumWidth( 40 );
        m_pFilters = new QToolButton( this );
        m_pFilters->setObjectName( QStringLiteral( "MapOutlinerFilters" ) );
        m_pFilters->setText( QStringLiteral( "Filter" ) );
        m_pFilters->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        m_pFilters->setPopupMode( QToolButton::InstantPopup );
        m_pFilters->setToolTip( QStringLiteral( "Filter mapnode types and visibility" ) );
        auto *pMenu = new QMenu( m_pFilters );
        pMenu->setToolTipsVisible( true );
        pMenu->addSection( QStringLiteral( "Mapnode types" ) );
        constexpr const char *kNames[]{ "Brushes", "Meshes", "Patches", "Terrain", "Entities", "Groups" };
        for ( int i = 0; i < 6; ++i ) {
            m_pKinds[i] = pMenu->addAction( QString::fromUtf8( kNames[i] ) );
            m_pKinds[i]->setObjectName( QStringLiteral( "MapOutlinerKind%1" ).arg( i ) );
            m_pKinds[i]->setCheckable( true );
            m_pKinds[i]->setChecked( true );
            QObject::connect( m_pKinds[i], &QAction::toggled, this, [this]() { ApplyFilter(); } );
        }
        pMenu->addSeparator();
        m_pShowHidden = pMenu->addAction( QStringLiteral( "Show Hidden Objects" ) );
        m_pShowHidden->setObjectName( QStringLiteral( "MapOutlinerShowHidden" ) );
        m_pShowHidden->setCheckable( true );
        m_pShowHidden->setChecked( true );
        m_pShowHidden->setToolTip( QStringLiteral( "Include objects hidden in the views. Hidden objects are dimmed; this does not unhide them." ) );
        m_pSelectedOnly = pMenu->addAction( QStringLiteral( "Selected Objects Only" ) );
        m_pSelectedOnly->setObjectName( QStringLiteral( "MapOutlinerSelectedOnly" ) );
        m_pSelectedOnly->setCheckable( true );
        QObject::connect( m_pShowHidden, &QAction::toggled, this, [this]() { ApplyFilter(); } );
        QObject::connect( m_pSelectedOnly, &QAction::toggled, this, [this]() { ApplyFilter(); } );
        pMenu->addSeparator();
        m_pShowIds = pMenu->addAction( QStringLiteral( "Show Mapnode IDs" ) );
        m_pShowIds->setObjectName( QStringLiteral( "MapOutlinerShowIds" ) );
        m_pShowIds->setCheckable( true );
        m_pShowIds->setChecked( EditorSettings_Bool( &pWorkspace->pGui->settings, kShowIdsSetting, CY_FALSE ) );
        m_pShowIds->setToolTip( QStringLiteral( "Append stable IDs to named nodes. Unnamed nodes use their ID in either mode." ) );
        QObject::connect( m_pShowIds, &QAction::toggled, this, [this]( bool checked ) {
            const setting_descriptor_t *pSetting = EditorSettings_Find( &m_pWorkspace->pGui->settings, StringView_FromCString( kShowIdsSetting ) );
            setting_value_t value{};
            value.type = setting_type_t::BOOL;
            value.bValue = checked;
            if ( pSetting != nullptr ) { ( void )EditorSettings_Write( &m_pWorkspace->pGui->settings, settings_scope_t::USER, *pSetting, value ); }
            RefreshLabels();
            ApplyFilter();
        } );
        pMenu->addSeparator();
        auto *pReset = pMenu->addAction( QStringLiteral( "Reset Filters" ) );
        pReset->setObjectName( QStringLiteral( "MapOutlinerResetFilters" ) );
        QObject::connect( pReset, &QAction::triggered, this, [this]() {
            const QSignalBlocker filterBlocker( m_pFilter );
            m_pFilter->clear();
            for ( QAction *pKind : m_pKinds ) { const QSignalBlocker blocker( pKind ); pKind->setChecked( true ); }
            { const QSignalBlocker blocker( m_pShowHidden ); m_pShowHidden->setChecked( true ); }
            { const QSignalBlocker blocker( m_pSelectedOnly ); m_pSelectedOnly->setChecked( false ); }
            ApplyFilter();
        } );
        m_pFilters->setMenu( pMenu );
        pFilterRow->addWidget( m_pFilter, 1 );
        pFilterRow->addWidget( m_pFilters );
        m_pTree = new map_outliner_tree_t( this, pWorkspace->pGui );
        m_pTree->setObjectName( QStringLiteral( "MapOutlinerTree" ) );
        m_pTree->setColumnCount( 3 );
        m_pTree->setHeaderLabels( { QStringLiteral( "Name" ), QStringLiteral( "Kind" ), QStringLiteral( "ID" ) } );
        m_pTree->setSelectionMode( QAbstractItemView::ExtendedSelection );
        m_pTree->setUniformRowHeights( true );
        m_pTree->setHeaderHidden( true );
        m_pTree->setColumnHidden( 1, true );
        m_pTree->setColumnHidden( 2, true );
        m_pTree->setIndentation( 14 );
        m_pTree->setIconSize( QSize( 16, 16 ) );
        m_pTree->setRootIsDecorated( true );
        m_pTree->setToolTip( QStringLiteral( "Select map objects. Selections outside the current filter are preserved.\nGroups show authored membership; group editing is not available yet." ) );
        m_pStatus = new QLabel( this );
        m_pStatus->setObjectName( QStringLiteral( "MapOutlinerStatus" ) );
        m_pStatus->setProperty( "muted", true );
        pLayout->addLayout( pFilterRow );
        pLayout->addWidget( m_pTree, 1 );
        pLayout->addWidget( m_pStatus );
        QObject::connect( m_pFilter, &QLineEdit::textChanged, this, [this]() { ApplyFilter(); } );
        QObject::connect( m_pTree, &QTreeWidget::itemSelectionChanged, this, [this]() { PushSelection(); } );
        ( void )MapWorkspace_AddListener( pWorkspace, &map_outliner_t::OnChanged, this );
        ( void )gui::EditorGui_AddStyleListener( pWorkspace->pGui, &map_outliner_t::OnStyleChanged, this );
        ( void )EditorSettings_AddListener( &pWorkspace->pGui->settings, &map_outliner_t::OnSettingsChanged, this );
        Rebuild();
    }

    ~map_outliner_t() override
    {
        gui::EditorGui_RemoveStyleListener( m_pWorkspace->pGui, &map_outliner_t::OnStyleChanged, this );
        EditorSettings_RemoveListener( &m_pWorkspace->pGui->settings, &map_outliner_t::OnSettingsChanged, this );
        MapWorkspace_RemoveListener( m_pWorkspace, &map_outliner_t::OnChanged, this );
    }

    int VisibleRowCount() const
    {
        int count = 0;
        for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
            if ( IsVisible( *it ) && ( *it )->data( 0, kOutlinerCountRole ).toBool() ) { ++count; }
        }
        return count;
    }

    void SetFilter( const QString &text ) { m_pFilter->setText( text ); }

private:
    struct group_t { u64 id; QString name; QString layer; const key_value_t *pMembers; };

    static void OnChanged( void *pContext, u32 changes ) noexcept
    {
        auto *pOutliner = static_cast<map_outliner_t *>( pContext );
        if ( ( changes & MAP_CHANGE_DOCUMENT ) != 0u ) { pOutliner->Rebuild(); }
        else if ( ( changes & ( MAP_CHANGE_SELECTION | MAP_CHANGE_VIEW | MAP_CHANGE_TITLE ) ) != 0u ) {
            pOutliner->ApplyFilter();
            pOutliner->PullSelection();
        }
    }

    static void OnStyleChanged( void *pContext ) noexcept
    {
        auto *pOutliner = static_cast<map_outliner_t *>( pContext );
        pOutliner->RefreshAppearance();
        pOutliner->ApplyFilter();
    }

    static void OnSettingsChanged( void *pContext, string_view_t path ) noexcept
    {
        auto *pOutliner = static_cast<map_outliner_t *>( pContext );
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( kShowIdsSetting ) ) ) {
            const QSignalBlocker blocker( pOutliner->m_pShowIds );
            pOutliner->m_pShowIds->setChecked( EditorSettings_Bool( &pOutliner->m_pWorkspace->pGui->settings, kShowIdsSetting, CY_FALSE ) );
            pOutliner->RefreshLabels();
            pOutliner->ApplyFilter();
        }
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( kTreeLinesSetting ) ) ) {
            pOutliner->m_pTree->viewport()->update();
        }
    }

    static bool IsVisible( const QTreeWidgetItem *pItem )
    {
        for ( const QTreeWidgetItem *p = pItem; p != nullptr; p = p->parent() ) { if ( p->isHidden() ) { return false; } }
        return true;
    }

    bool IsFiltered() const
    {
        if ( !m_pFilter->text().trimmed().isEmpty() || !m_pShowHidden->isChecked() || m_pSelectedOnly->isChecked() ) { return true; }
        for ( const QAction *pKind : m_pKinds ) { if ( !pKind->isChecked() ) { return true; } }
        return false;
    }

    QSet<QString> CollapsedKeys() const
    {
        QSet<QString> result;
        for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
            if ( !( *it )->isExpanded() ) { result.insert( ( *it )->data( 0, kOutlinerKeyRole ).toString() ); }
        }
        return result;
    }

    QTreeWidgetItem *AddRow( QTreeWidgetItem *pParent, const QString &name, const QString &kind, u64 id, int type, const char *pIcon )
    {
        auto *pItem = new QTreeWidgetItem( pParent );
        const bool named = !name.isEmpty() && name != kind;
        const QString label = named ? name : QStringLiteral( "%1 #%2" ).arg( kind ).arg( id );
        pItem->setText( 0, label );
        pItem->setData( 0, kOutlinerLabelRole, label );
        pItem->setData( 0, kOutlinerNamedRole, named );
        pItem->setText( 1, kind );
        pItem->setText( 2, QString::number( id ) );
        pItem->setData( 0, kIdRole, QVariant::fromValue<qulonglong>( id ) );
        pItem->setData( 0, kOutlinerKindRole, type );
        pItem->setData( 0, kOutlinerKeyRole, QString::number( id ) );
        pItem->setData( 0, kOutlinerIconRole, QString::fromUtf8( pIcon ) );
        pItem->setData( 0, kOutlinerCountRole, type != kOutlinerGroupKind );
        pItem->setData( 0, kOutlinerTipRole, QStringLiteral( "%1 · %2 · ID %3" ).arg( name, kind ).arg( id ) );
        pItem->setExpanded( true );
        if ( type == kOutlinerGroupKind ) { pItem->setFlags( pItem->flags() & ~Qt::ItemIsSelectable ); }
        else { m_items.insert( id, pItem ); }
        return pItem;
    }

    // One pass over authored records retains complete names for filtering.
    // Do not repeatedly call the document's linear FindObject for every row.
    static void IndexNames( const key_value_t *pContainer, QHash<quint64, QString> &names )
    {
        constexpr const char *kSections[]{ "entities", "brushes", "meshes", "patches", "terrain" };
        for ( const char *pSection : kSections ) {
            const key_value_t *pRecords = OutlinerMember( pContainer, pSection );
            if ( KeyValue_Type( pRecords ) != key_value_type_t::ARRAY ) { continue; }
            for ( usize i = 0; i < KeyValue_ChildCount( pRecords ); ++i ) {
                const key_value_t *pRecord = KeyValue_ChildAt( pRecords, i );
                const u64 id = OutlinerId( OutlinerMember( pRecord, "id" ) );
                const QString name = OutlinerText( OutlinerMember( pRecord, "name" ) );
                if ( id != 0u && !name.isEmpty() ) { names.insert( id, name ); }
                if ( qstrcmp( pSection, "entities" ) == 0 ) { IndexNames( pRecord, names ); }
            }
        }
    }

    void Rebuild()
    {
        const QSignalBlocker blocker( m_pTree );
        const QSet<QString> collapsed = m_bFiltering ? m_collapsedBeforeFilter : CollapsedKeys();
        m_pTree->clear();
        m_items.clear();
        const map_document_t &map = *m_pWorkspace->pDocument;
        const map_wireframe_t &wire = m_pWorkspace->wire;
        QHash<quint64, QString> names;
        std::vector<group_t> groups;
        for ( usize i = 0u; i < map.chunks.nCount; ++i ) {
            const map_chunk_t &chunk = *map.chunks.pData[i];
            if ( chunk.bDamaged ) { continue; }
            const key_value_t *pRoot = SettingsDocument_Root( &chunk.store );
            IndexNames( pRoot, names );
            const key_value_t *pGroups = OutlinerMember( pRoot, "groups" );
            if ( KeyValue_Type( pGroups ) != key_value_type_t::ARRAY ) { continue; }
            for ( usize g = 0; g < KeyValue_ChildCount( pGroups ); ++g ) {
                const key_value_t *pGroup = KeyValue_ChildAt( pGroups, g );
                const u64 id = OutlinerId( OutlinerMember( pGroup, "id" ) );
                if ( id != 0u ) {
                    groups.push_back( { id, OutlinerText( OutlinerMember( pGroup, "name" ) ), QString::fromUtf8( chunk.layer ), OutlinerMember( pGroup, "members" ) } );
                }
            }
        }
        m_pRoot = new QTreeWidgetItem( m_pTree );
        m_pRoot->setText( 0, MapWorkspace_DisplayName( m_pWorkspace ) );
        m_pRoot->setData( 0, kOutlinerKeyRole, QStringLiteral( "map" ) );
        m_pRoot->setData( 0, kOutlinerIconRole, QStringLiteral( "asset-map" ) );
        m_pRoot->setFlags( m_pRoot->flags() & ~Qt::ItemIsSelectable );
        m_pRoot->setExpanded( true );
        QHash<QString, QTreeWidgetItem *> layersByName;
        std::vector<QTreeWidgetItem *> layers;
        for ( usize i = 0u; i < map.layers.nCount; ++i ) {
            auto *pLayer = new QTreeWidgetItem( m_pRoot );
            const QString layer = QString::fromUtf8( map.layers.pData[i].id );
            pLayer->setText( 0, layer );
            pLayer->setText( 1, QStringLiteral( "Layer" ) );
            pLayer->setData( 0, kOutlinerKeyRole, QStringLiteral( "layer:" ) + layer );
            pLayer->setData( 0, kOutlinerIconRole, QStringLiteral( "asset-folder" ) );
            pLayer->setData( 0, kOutlinerCountRole, true );
            pLayer->setFlags( pLayer->flags() & ~Qt::ItemIsSelectable );
            pLayer->setExpanded( true );
            layers.push_back( pLayer );
            layersByName.insert( layer, pLayer );
        }
        auto layerRow = [&layers, this]( u32 iLayer ) { return iLayer < layers.size() ? layers[iLayer] : m_pRoot; };
        for ( usize i = 0u; i < wire.entities.nCount; ++i ) {
            const map_wire_entity_t &entity = wire.entities.pData[i];
            const QString className = QString::fromUtf8( entity.className );
            auto *pRow = AddRow( layerRow( entity.iLayer ), names.value( entity.id, QString::fromUtf8( entity.name ) ), className, entity.id,
                                static_cast<int>( map_wire_kind_t::ENTITY ), OutlinerIcon( map_wire_kind_t::ENTITY, className ) );
            pRow->setText( 1, QStringLiteral( "Entity " ) + className );
        }
        for ( usize i = 0u; i < wire.objects.nCount; ++i ) {
            const map_wire_object_t &object = wire.objects.pData[i];
            if ( object.kind == map_wire_kind_t::ENTITY ) { continue; }
            QTreeWidgetItem *pParent = object.owner != 0u ? m_items.value( object.owner, nullptr ) : nullptr;
            if ( pParent == nullptr ) { pParent = layerRow( object.iLayer ); }
            const QString kind = QString::fromUtf8( KindName( object.kind ) );
            AddRow( pParent, names.value( object.id, kind ), kind, object.id, static_cast<int>( object.kind ), OutlinerIcon( object.kind ) );
        }
        // CYMAP groups are authored membership, not ownership. This read-only
        // tree gives a multiply-listed member to the lowest group ID, keeps
        // tied geometry under its entity, and skips edges forming a cycle.
        std::stable_sort( groups.begin(), groups.end(), []( const group_t &a, const group_t &b ) { return a.id < b.id; } );
        QHash<quint64, QTreeWidgetItem *> groupRows;
        for ( const group_t &group : groups ) {
            if ( groupRows.contains( group.id ) || m_items.contains( group.id ) ) { continue; }
            auto *pRow = AddRow( layersByName.value( group.layer, m_pRoot ), group.name, QStringLiteral( "Group" ), group.id, kOutlinerGroupKind, "select-groups" );
            pRow->setData( 0, kOutlinerTipRole, pRow->data( 0, kOutlinerTipRole ).toString() + QStringLiteral( "\nAuthored membership. Group editing is not available yet." ) );
            groupRows.insert( group.id, pRow );
        }
        QSet<quint64> assigned;
        QSet<quint64> processed;
        for ( const group_t &group : groups ) {
            QTreeWidgetItem *pGroup = groupRows.value( group.id, nullptr );
            if ( pGroup == nullptr || processed.contains( group.id ) || KeyValue_Type( group.pMembers ) != key_value_type_t::ARRAY ) { continue; }
            processed.insert( group.id );
            for ( usize i = 0; i < KeyValue_ChildCount( group.pMembers ); ++i ) {
                const u64 id = OutlinerId( KeyValue_ChildAt( group.pMembers, i ) );
                QTreeWidgetItem *pMember = m_items.value( id, groupRows.value( id, nullptr ) );
                if ( pMember == nullptr || assigned.contains( id ) ) { continue; }
                const map_wire_object_t *pObject = MapWireframe_FindObject( wire, id );
                if ( pObject != nullptr && pObject->owner != 0u ) { continue; }
                bool cycle = false;
                for ( QTreeWidgetItem *p = pGroup; p != nullptr; p = p->parent() ) { if ( p == pMember ) { cycle = true; break; } }
                if ( cycle ) { continue; }
                pMember->parent()->removeChild( pMember );
                pGroup->addChild( pMember );
                assigned.insert( id );
            }
        }
        for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
            ( *it )->setExpanded( !collapsed.contains( ( *it )->data( 0, kOutlinerKeyRole ).toString() ) );
        }
        if ( m_bFiltering ) { m_collapsedBeforeFilter = collapsed; }
        RefreshLabels();
        RefreshAppearance();
        ApplyFilter();
        PullSelection();
    }

    void RefreshAppearance()
    {
        const auto &style = m_pWorkspace->pGui->style;
        m_pFilters->setIcon( gui::EditorStyle_Icon( style, "search" ) );
        for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
            const QByteArray icon = ( *it )->data( 0, kOutlinerIconRole ).toString().toUtf8();
            ( *it )->setIcon( 0, gui::EditorStyle_Icon( style, icon.constData() ) );
        }
    }

    void RefreshLabels()
    {
        if ( m_pTree == nullptr ) { return; }
        const QSignalBlocker blocker( m_pTree );
        for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
            const QVariant label = ( *it )->data( 0, kOutlinerLabelRole );
            if ( !label.isValid() ) { continue; }
            QString text = label.toString();
            if ( m_pShowIds->isChecked() && ( *it )->data( 0, kOutlinerNamedRole ).toBool() ) {
                text += QStringLiteral( "  #%1" ).arg( ( *it )->data( 0, kIdRole ).toULongLong() );
            }
            ( *it )->setText( 0, text );
        }
    }

    bool FilterItem( QTreeWidgetItem *pItem, const QString &text, bool inheritedMatch )
    {
        const bool container = !pItem->data( 0, kOutlinerKindRole ).isValid();
        const bool textMatch = inheritedMatch || text.isEmpty() || pItem->text( 0 ).contains( text, Qt::CaseInsensitive ) ||
                               pItem->text( 1 ).contains( text, Qt::CaseInsensitive ) || pItem->text( 2 ) == text;
        bool childVisible = false;
        for ( int i = 0; i < pItem->childCount(); ++i ) { childVisible = FilterItem( pItem->child( i ), text, textMatch ) || childVisible; }
        const int type = pItem->data( 0, kOutlinerKindRole ).toInt();
        const u64 id = pItem->data( 0, kIdRole ).toULongLong();
        const map_wire_object_t *pObject = container || type == kOutlinerGroupKind ? nullptr : MapWireframe_FindObject( m_pWorkspace->wire, id );
        // Logic entities without an origin are not in the pickable object
        // array. Visibility depends on identity/class, not a world position.
        map_wire_object_t logicEntity{};
        if ( pObject == nullptr && !container && type == static_cast<int>( map_wire_kind_t::ENTITY ) ) {
            logicEntity.id = id;
            logicEntity.kind = map_wire_kind_t::ENTITY;
            pObject = &logicEntity;
        }
        const bool hidden = pObject != nullptr && !MapWorkspace_IsVisible( m_pWorkspace, *pObject );
        bool selfVisible = textMatch;
        if ( container ) { selfVisible = !m_bFiltering; }
        else {
            selfVisible = selfVisible && m_pKinds[type]->isChecked() && ( m_pShowHidden->isChecked() || !hidden ) &&
                          ( !m_pSelectedOnly->isChecked() || MapWorkspace_IsSelected( m_pWorkspace, id ) );
        }
        const bool visible = selfVisible || childVisible;
        pItem->setHidden( !visible );
        pItem->setForeground( 0, hidden ? m_pTree->palette().brush( QPalette::Disabled, QPalette::Text ) : QBrush() );
        QFont font = pItem->font( 0 );
        font.setItalic( hidden );
        pItem->setFont( 0, font );
        QString tip = pItem->data( 0, kOutlinerTipRole ).toString();
        if ( hidden ) { tip += QStringLiteral( "\nHidden in map views" ); }
        if ( !tip.isEmpty() ) { pItem->setToolTip( 0, tip ); }
        if ( m_bFiltering && childVisible ) { pItem->setExpanded( true ); }
        return visible;
    }

    void ApplyFilter()
    {
        if ( m_pTree == nullptr || m_pRoot == nullptr ) { return; }
        const QSignalBlocker blocker( m_pTree );
        const bool filtering = IsFiltered();
        if ( filtering && !m_bFiltering ) { m_collapsedBeforeFilter = CollapsedKeys(); }
        if ( !filtering && m_bFiltering ) {
            for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
                ( *it )->setExpanded( !m_collapsedBeforeFilter.contains( ( *it )->data( 0, kOutlinerKeyRole ).toString() ) );
            }
        }
        m_bFiltering = filtering;
        const QString text = m_pFilter->text().trimmed();
        for ( int i = 0; i < m_pRoot->childCount(); ++i ) { FilterItem( m_pRoot->child( i ), text, false ); }
        int shown = 0;
        for ( auto it = m_items.cbegin(); it != m_items.cend(); ++it ) {
            shown += IsVisible( it.value() ) ? 1 : 0;
            // Qt may omit filtered rows from selectedItems; restore their
            // presentation state from the workspace when they reappear.
            it.value()->setSelected( MapWorkspace_IsSelected( m_pWorkspace, it.key() ) );
        }
        m_pStatus->setText( QStringLiteral( "%1 / %2 objects · %3 selected" ).arg( shown ).arg( m_items.size() ).arg( EditorSelection_Count( &m_pWorkspace->selection ) ) );
        m_pFilters->setText( filtering ? QStringLiteral( "Filtered" ) : QStringLiteral( "Filter" ) );
        m_pRoot->setText( 0, MapWorkspace_DisplayName( m_pWorkspace ) );
    }

    void PushSelection()
    {
        if ( m_bSyncing ) { return; }
        std::vector<u64> ids;
        // Filtering is a view operation: preserve selected objects omitted
        // from this projection when the user changes the visible selection.
        for ( usize i = 0; i < EditorSelection_Count( &m_pWorkspace->selection ); ++i ) {
            const u64 id = EditorSelection_At( &m_pWorkspace->selection, i );
            QTreeWidgetItem *pItem = m_items.value( id, nullptr );
            if ( pItem != nullptr && !IsVisible( pItem ) ) { ids.push_back( id ); }
        }
        for ( QTreeWidgetItem *pItem : m_pTree->selectedItems() ) {
            if ( IsVisible( pItem ) && pItem->data( 0, kIdRole ).isValid() ) { ids.push_back( pItem->data( 0, kIdRole ).toULongLong() ); }
        }
        m_bSyncing = true;
        MapWorkspace_SetSelection( m_pWorkspace, ids.data(), ids.size() );
        m_bSyncing = false;
        ApplyFilter();
    }

    void PullSelection()
    {
        if ( m_bSyncing ) { return; }
        m_bSyncing = true;
        {
            const QSignalBlocker blocker( m_pTree );
            m_pTree->clearSelection();
            QTreeWidgetItem *pFirst = nullptr;
            for ( usize i = 0u; i < EditorSelection_Count( &m_pWorkspace->selection ); ++i ) {
                QTreeWidgetItem *pItem = m_items.value( EditorSelection_At( &m_pWorkspace->selection, i ), nullptr );
                if ( pItem == nullptr ) { continue; }
                pItem->setSelected( true );
                if ( pFirst == nullptr && IsVisible( pItem ) ) { pFirst = pItem; }
            }
            if ( pFirst != nullptr ) { m_pTree->scrollToItem( pFirst ); }
        }
        m_pTree->viewport()->update();
        m_bSyncing = false;
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    QLineEdit *m_pFilter{ nullptr };
    QToolButton *m_pFilters{ nullptr };
    QAction *m_pKinds[6]{};
    QAction *m_pShowHidden{ nullptr };
    QAction *m_pSelectedOnly{ nullptr };
    QAction *m_pShowIds{ nullptr };
    QTreeWidget *m_pTree{ nullptr };
    QTreeWidgetItem *m_pRoot{ nullptr };
    QLabel *m_pStatus{ nullptr };
    QHash<quint64, QTreeWidgetItem *> m_items{};
    QSet<QString> m_collapsedBeforeFilter{};
    bool m_bSyncing{ false };
    bool m_bFiltering{ false };
};

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

// Keep this presentation local: asset trees and the outliner retain their own
// branch drawing. Explicit strokes remain visible under native and QSS styles.
class map_properties_tree_t final : public QTreeWidget {
public:
    map_properties_tree_t( QWidget *pParent, gui::editor_gui_t *pGui ) : QTreeWidget( pParent ), m_pGui( pGui ) {}

protected:
    void drawBranches( QPainter *pPainter, const QRect &rect, const QModelIndex &index ) const override
    {
        if ( rect.isEmpty() || !index.isValid() ) { return; }
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        const int step = rtl ? indentation() : -indentation();
        const int x = rtl ? rect.left() + indentation() / 2 : rect.right() - indentation() / 2;
        const int middle = rect.center().y();
        const bool children = model()->hasChildren( index );
        pPainter->save();
        pPainter->setClipRect( rect, Qt::IntersectClip );
        QPen pen( gui::EditorStyle_TokenColor( m_pGui->style, "ui.text.disabled" ) );
        pen.setWidthF( 1.0 );
        pen.setCosmetic( true );
        pPainter->setPen( pen );
        if ( EditorSettings_Bool( &m_pGui->settings, kTreeLinesSetting, CY_TRUE ) ) {
            if ( index.parent().isValid() ) {
                const int gap = children ? 5 : 0;
                pPainter->drawLine( x, rect.top(), x, middle - gap );
                if ( HasFollowingSibling( index ) ) { pPainter->drawLine( x, middle + gap, x, rect.bottom() ); }
                pPainter->drawLine( x + ( rtl ? -gap : gap ), middle, rtl ? rect.left() : rect.right(), middle );
            }
            int ancestorX = x + step;
            for ( QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent(), ancestorX += step ) {
                if ( parent.parent().isValid() && HasFollowingSibling( parent ) ) {
                    pPainter->drawLine( ancestorX, rect.top(), ancestorX, rect.bottom() );
                }
            }
        }
        if ( children ) {
            pen.setColor( gui::EditorStyle_TokenColor( m_pGui->style, isEnabled() ? "ui.text" : "ui.text.disabled" ) );
            pPainter->setPen( pen );
            if ( isExpanded( index ) ) {
                pPainter->drawLine( x - 3, middle - 2, x, middle + 1 );
                pPainter->drawLine( x, middle + 1, x + 3, middle - 2 );
            } else {
                const int direction = rtl ? -1 : 1;
                pPainter->drawLine( x - direction, middle - 3, x + direction * 2, middle );
                pPainter->drawLine( x + direction * 2, middle, x - direction, middle + 3 );
            }
        }
        pPainter->restore();
    }

private:
    bool HasFollowingSibling( const QModelIndex &index ) const
    {
        const QModelIndex parent = index.parent();
        for ( int row = index.row() + 1; row < model()->rowCount( parent ); ++row ) {
            if ( !isRowHidden( row, parent ) ) { return true; }
        }
        return false;
    }
    gui::editor_gui_t *m_pGui;
};

const char *PropertyIcon( const QString &name, key_value_type_t type = key_value_type_t::NULL_VALUE )
{
    if ( type == key_value_type_t::OBJECT ) { return "asset-folder"; }
    if ( type == key_value_type_t::ARRAY ) { return "view-properties"; }
    const QString key = name.toLower();
    if ( key.contains( QLatin1StringView( "material" ) ) ) { return "asset-material"; }
    if ( key.contains( QLatin1StringView( "layer" ) ) ) { return "view-visgroups"; }
    if ( key.contains( QLatin1StringView( "origin" ) ) || key.contains( QLatin1StringView( "position" ) ) || key.contains( QLatin1StringView( "bounds" ) ) ) { return "tool-translate"; }
    if ( key.contains( QLatin1StringView( "rotation" ) ) || key == QLatin1StringView( "normal" ) ) { return "tool-rotate"; }
    if ( key.contains( QLatin1StringView( "scale" ) ) || key.contains( QLatin1StringView( "size" ) ) ) { return "tool-scale"; }
    if ( key == QLatin1StringView( "vertices" ) || key == QLatin1StringView( "control points" ) ) { return "select-vertices"; }
    if ( key == QLatin1StringView( "edges" ) || key == QLatin1StringView( "half-edges" ) ) { return "select-edges"; }
    if ( key == QLatin1StringView( "faces" ) || key == QLatin1StringView( "planes" ) ) { return "select-faces"; }
    if ( key == QLatin1StringView( "color" ) || key == QLatin1StringView( "intensity" ) || key == QLatin1StringView( "radius" ) || key == QLatin1StringView( "range" ) || key == QLatin1StringView( "cast_shadows" ) ) { return "entity-light"; }
    if ( key.contains( QLatin1StringView( "script" ) ) || key == QLatin1StringView( "outputs" ) || key == QLatin1StringView( "inputs" ) ) { return "entity-logic"; }
    if ( key == QLatin1StringView( "owner entity" ) || key == QLatin1StringView( "parent entity" ) || key == QLatin1StringView( "parent" ) || key == QLatin1StringView( "owner" ) ) { return "group-create"; }
    if ( key == QLatin1StringView( "id" ) || key == QLatin1StringView( "source file" ) || key == QLatin1StringView( "(chunk)" ) ) { return "map-info"; }
    if ( key == QLatin1StringView( "class" ) || key == QLatin1StringView( "type" ) ) { return "entity-point"; }
    return "keymap";
}

const char *PropertyGroupIcon( const QString &name )
{
    if ( name == QLatin1StringView( "Entity keys" ) || name == QLatin1StringView( "Identity" ) ) { return "entity-point"; }
    if ( name == QLatin1StringView( "Placement" ) ) { return "tool-translate"; }
    if ( name == QLatin1StringView( "Materials" ) ) { return "asset-material"; }
    if ( name == QLatin1StringView( "Geometry" ) || name == QLatin1StringView( "Geometry Data" ) ) { return "tool-block"; }
    if ( name == QLatin1StringView( "Selected mesh face" ) ) { return "select-faces"; }
    if ( name == QLatin1StringView( "Selection" ) ) { return "select-objects"; }
    return "asset-folder";
}

void DecorateProperty( QTreeWidgetItem *pItem, const gui::editor_style_t &style, map_property_row_kind_t kind, const char *pIcon )
{
    pItem->setData( 0, MAP_PROPERTY_KIND_ROLE, static_cast<int>( kind ) );
    pItem->setData( 0, MAP_PROPERTY_ICON_ROLE, QString::fromLatin1( pIcon ) );
    pItem->setIcon( 0, gui::EditorStyle_Icon( style, pIcon ) );
}

QString FormatScalar( const key_value_t *pValue )
{
    bool_t bValue = CY_FALSE;
    i64 nSigned = 0;
    u64 nUnsigned = 0u;
    f64 flValue = 0.0;
    string_view_t text{};
    switch ( KeyValue_Type( pValue ) ) {
        case key_value_type_t::NULL_VALUE: return QStringLiteral( "null" );
        case key_value_type_t::BOOL:
            return KeyValue_GetBool( pValue, &bValue ) && bValue ? QStringLiteral( "true" ) : QStringLiteral( "false" );
        case key_value_type_t::I64: return KeyValue_GetI64( pValue, &nSigned ) ? QString::number( nSigned ) : QString();
        case key_value_type_t::U64: return KeyValue_GetU64( pValue, &nUnsigned ) ? QString::number( nUnsigned ) : QString();
        case key_value_type_t::F64: return KeyValue_GetF64( pValue, &flValue ) ? QString::number( flValue, 'g', 15 ) : QString();
        case key_value_type_t::STRING:
            return KeyValue_GetString( pValue, &text ) ? QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) ) : QString();
        case key_value_type_t::BINARY: return QStringLiteral( "<binary>" );
        case key_value_type_t::OBJECT:
        case key_value_type_t::ARRAY: break;
    }
    return QString();
}

bool IsContainer( const key_value_t *pValue ) noexcept
{
    const key_value_type_t type = KeyValue_Type( pValue );
    return type == key_value_type_t::OBJECT || type == key_value_type_t::ARRAY;
}

// Short scalar arrays read best on one line: origin = [128, -64, 16].
bool IsScalarList( const key_value_t *pValue ) noexcept
{
    if ( KeyValue_Type( pValue ) != key_value_type_t::ARRAY || KeyValue_ChildCount( pValue ) > static_cast<usize>( kScalarListMax ) ) {
        return false;
    }
    for ( usize i = 0u; i < KeyValue_ChildCount( pValue ); ++i ) {
        if ( IsContainer( KeyValue_ChildAt( pValue, i ) ) ) { return false; }
    }
    return true;
}

QString FormatValue( const key_value_t *pValue )
{
    if ( IsScalarList( pValue ) ) {
        QStringList parts;
        for ( usize i = 0u; i < KeyValue_ChildCount( pValue ); ++i ) { parts.append( FormatScalar( KeyValue_ChildAt( pValue, i ) ) ); }
        return QStringLiteral( "[" ) + parts.join( QStringLiteral( ", " ) ) + QStringLiteral( "]" );
    }
    if ( KeyValue_Type( pValue ) == key_value_type_t::ARRAY ) {
        const usize nItems = KeyValue_ChildCount( pValue );
        return nItems == 1u ? QStringLiteral( "1 item" ) : QStringLiteral( "%1 items" ).arg( nItems );
    }
    if ( KeyValue_Type( pValue ) == key_value_type_t::OBJECT ) { return QString(); }
    return FormatScalar( pValue );
}

// Containers retain their backing node and populate only when expanded. Long
// arrays are paged, so selecting a terrain never creates a widget per sample.
constexpr int kPropertyNodeRole = Qt::UserRole + 20;
constexpr int kPropertyBeginRole = Qt::UserRole + 21;
constexpr int kPropertyEndRole = Qt::UserRole + 22;
constexpr int kPropertyLoadedRole = Qt::UserRole + 23;
constexpr int kPropertyGeometryRole = Qt::UserRole + 24;
constexpr usize kPropertyPageSize = 128u;

const key_value_t *PropertyNode( const QTreeWidgetItem *pItem )
{
    return reinterpret_cast<const key_value_t *>( pItem->data( 0, kPropertyNodeRole ).value<quintptr>() );
}

void SetPropertyContainer( QTreeWidgetItem *pItem, const key_value_t *pNode, usize begin, usize end )
{
    pItem->setData( 0, kPropertyNodeRole, QVariant::fromValue( reinterpret_cast<quintptr>( pNode ) ) );
    pItem->setData( 0, kPropertyBeginRole, QVariant::fromValue<qulonglong>( begin ) );
    pItem->setData( 0, kPropertyEndRole, QVariant::fromValue<qulonglong>( end ) );
    pItem->setChildIndicatorPolicy( begin < end ? QTreeWidgetItem::ShowIndicator : QTreeWidgetItem::DontShowIndicator );
}

void AddMembers( QTreeWidgetItem *pParent, QTreeWidget *pTree, const gui::editor_style_t &style, const key_value_t *pContainer, int depth,
                 usize begin = 0u, usize end = CY_INVALID_SIZE )
{
    if ( pContainer == nullptr ) { return; }
    end = std::min( end, KeyValue_ChildCount( pContainer ) );
    const bool bArray = KeyValue_Type( pContainer ) == key_value_type_t::ARRAY;
    const bool bPages = end - begin > kPropertyPageSize;
    for ( usize i = begin; i < end; i += bPages ? kPropertyPageSize : 1u ) {
        auto *pItem = pParent != nullptr ? new QTreeWidgetItem( pParent ) : new QTreeWidgetItem( pTree );
        if ( bPages ) {
            const usize pageEnd = std::min( end, i + kPropertyPageSize );
            pItem->setText( 0, QStringLiteral( "[%1 ... %2]" ).arg( i ).arg( pageEnd - 1u ) );
            pItem->setText( 1, QStringLiteral( "%1 items" ).arg( pageEnd - i ) );
            DecorateProperty( pItem, style, map_property_row_kind_t::ARRAY_PAGE, "view-properties" );
            SetPropertyContainer( pItem, pContainer, i, pageEnd );
            continue;
        }
        const key_value_t *pChild = KeyValue_ChildAt( pContainer, i );
        const string_view_t name = KeyValue_Name( pChild );
        pItem->setText( 0, bArray ? QStringLiteral( "[%1]" ).arg( i ) : QString::fromUtf8( name.pData, static_cast<qsizetype>( name.cchLength ) ) );
        pItem->setText( 1, FormatValue( pChild ) );
        const auto type = KeyValue_Type( pChild );
        DecorateProperty( pItem, style, type == key_value_type_t::OBJECT ? map_property_row_kind_t::OBJECT :
            type == key_value_type_t::ARRAY ? map_property_row_kind_t::ARRAY : map_property_row_kind_t::VALUE, PropertyIcon( pItem->text( 0 ), type ) );
        if ( IsContainer( pChild ) && !IsScalarList( pChild ) ) {
            SetPropertyContainer( pItem, pChild, 0u, KeyValue_ChildCount( pChild ) );
            if ( depth == 0 && KeyValue_ChildCount( pChild ) <= static_cast<usize>( kScalarListMax ) ) {
                AddMembers( pItem, pTree, style, pChild, depth + 1 );
                pItem->setData( 0, kPropertyLoadedRole, true );
                pItem->setExpanded( true );
            }
        }
    }
}

class map_properties_t final : public QWidget {
public:
    map_properties_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QWidget( pParent ), m_pWorkspace( pWorkspace )
    {
        CY_ASSERT( pWorkspace != nullptr );
        // Hammer 5's Object Properties: a name filter and options gear on
        // top, the property grid, and a help pane for the current row.
        setObjectName( QStringLiteral( "MapObjectProperties" ) );
        auto *pLayout = new QVBoxLayout( this );
        pLayout->setContentsMargins( 0, 0, 0, 0 );
        pLayout->setSpacing( 2 );
        auto *pTop = new QHBoxLayout();
        pTop->setContentsMargins( 2, 2, 2, 0 );
        pTop->setSpacing( 2 );
        m_pFilter = new QLineEdit( this );
        m_pFilter->setObjectName( QStringLiteral( "MapPropertiesFilter" ) );
        m_pFilter->setPlaceholderText( QStringLiteral( "Filter shown properties..." ) );
        m_pFilter->setToolTip( QStringLiteral( "Search loaded property names and values. Expand Geometry Data and array pages to include their values; unopened data is not searched." ) );
        m_pFilter->setClearButtonEnabled( true );
        auto *pGear = new QToolButton( this );
        pGear->setObjectName( QStringLiteral( "MapPropertiesOptions" ) );
        pGear->setIcon( gui::EditorStyle_Icon( pWorkspace->pGui->style, "settings" ) );
        pGear->setToolTip( QStringLiteral( "Property view options" ) );
        pGear->setPopupMode( QToolButton::InstantPopup );
        auto *pMenu = new QMenu( pGear );
        m_pShowInternal = pMenu->addAction( QStringLiteral( "Show Editor Rows (chunk, bounds)" ) );
        m_pShowInternal->setCheckable( true );
        m_pShowInternal->setChecked( true );
        pMenu->addSeparator();
        QObject::connect( pMenu->addAction( QStringLiteral( "Expand All" ) ), &QAction::triggered, this, [this]() { m_pTree->expandAll(); } );
        QObject::connect( pMenu->addAction( QStringLiteral( "Collapse All" ) ), &QAction::triggered, this, [this]() { m_pTree->collapseAll(); } );
        pGear->setMenu( pMenu );
        pTop->addWidget( m_pFilter, 1 );
        pTop->addWidget( pGear );
        m_pTitle = new QLabel( this );
        m_pTitle->setObjectName( QStringLiteral( "MapPropertiesTitle" ) );
        m_pTitle->setTextFormat( Qt::PlainText );
        m_pTitle->setTextInteractionFlags( Qt::TextSelectableByMouse );
        m_pSelectionSummary = new QLabel( this );
        m_pSelectionSummary->setObjectName( QStringLiteral( "MapSelectionSummary" ) );
        m_pSelectionSummary->setProperty( "muted", true );
        m_pSelectionSummary->setTextFormat( Qt::PlainText );
        m_pSelectionSummary->setTextInteractionFlags( Qt::TextSelectableByMouse );
        m_pSelectionSummary->setWordWrap( true );
        m_pSelectionSummary->setMargin( 5 );
        auto *pSplitter = new QSplitter( Qt::Vertical, this );
        m_pTree = new map_properties_tree_t( pSplitter, pWorkspace->pGui );
        m_pTree->setObjectName( QStringLiteral( "MapPropertiesTree" ) );
        m_pTree->setRootIsDecorated( true );
        m_pTree->setItemsExpandable( true );
        m_pTree->setEditTriggers( QAbstractItemView::NoEditTriggers );
        m_pTree->setUniformRowHeights( true );
        m_pTree->setColumnCount( 2 );
        m_pTree->setHeaderLabels( { QStringLiteral( "Property" ), QStringLiteral( "Value" ) } );
        m_pTree->header()->setSectionResizeMode( 0, QHeaderView::Interactive );
        m_pTree->setColumnWidth( 0, 160 );
        m_pTree->setAlternatingRowColors( true );
        m_pHelp = new QLabel( pSplitter );
        m_pHelp->setObjectName( QStringLiteral( "MapPropertiesHelp" ) );
        m_pHelp->setWordWrap( true );
        m_pHelp->setAlignment( Qt::AlignTop | Qt::AlignLeft );
        m_pHelp->setTextInteractionFlags( Qt::TextSelectableByMouse );
        pSplitter->addWidget( m_pTree );
        pSplitter->addWidget( m_pHelp );
        pSplitter->setStretchFactor( 0, 4 );
        pSplitter->setStretchFactor( 1, 1 );
        pLayout->addLayout( pTop );
        pLayout->addWidget( m_pTitle );
        pLayout->addWidget( m_pSelectionSummary );
        m_pEntityTools = new QWidget( this );
        m_pEntityTools->setObjectName( QStringLiteral( "MapEntityPropertyTools" ) );
        auto *pKeyTools = new QHBoxLayout( m_pEntityTools );
        pKeyTools->setContentsMargins( 2, 0, 2, 0 );
        pKeyTools->setSpacing( 2 );
        const auto button = [&]( const char *name, const QString &text, const char *icon, const QString &tip ) {
            auto *tool = new QToolButton( m_pEntityTools );
            tool->setObjectName( QString::fromLatin1( name ) );
            tool->setText( text );
            tool->setIcon( gui::EditorStyle_Icon( pWorkspace->pGui->style, icon ) );
            tool->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
            tool->setToolTip( tip );
            pKeyTools->addWidget( tool );
            return tool;
        };
        m_pKeyAdd = button( "MapEntityPropertyAdd", QStringLiteral( "Add" ), "layer-new", QStringLiteral( "Add a typed key to every selected entity owner" ) );
        m_pKeyEdit = button( "MapEntityPropertyEdit", QStringLiteral( "Edit" ), "view-properties", QStringLiteral( "Edit this key's value; double-click also edits" ) );
        m_pKeyRename = button( "MapEntityPropertyRename", QStringLiteral( "Rename" ), "keymap", QStringLiteral( "Rename this custom key, preserving its value (F2)" ) );
        m_pKeyRemove = button( "MapEntityPropertyRemove", QStringLiteral( "Remove" ), "edit-delete", QStringLiteral( "Remove this custom key from every target (Delete). Undo restores it." ) );
        pKeyTools->addStretch( 1 );
        pLayout->addWidget( m_pEntityTools );
        pLayout->addWidget( pSplitter, 1 );
        QObject::connect( m_pFilter, &QLineEdit::textChanged, this, [this]( const QString & ) { ApplyFilter(); } );
        QObject::connect( m_pShowInternal, &QAction::toggled, this, [this]( bool ) { Rebuild(); } );
        QObject::connect( m_pTree, &QTreeWidget::currentItemChanged, this, [this]( QTreeWidgetItem *pItem, QTreeWidgetItem * ) { ShowHelp( pItem ); RefreshKeyActions(); } );
        QObject::connect( m_pTree, &QTreeWidget::itemExpanded, this, [this]( QTreeWidgetItem *pItem ) { Populate( pItem ); ApplyFilter(); } );
        QObject::connect( m_pKeyAdd, &QToolButton::clicked, this, [this]() { EditKey( map_entity_property_action_t::ADD ); } );
        QObject::connect( m_pKeyEdit, &QToolButton::clicked, this, [this]() { EditKey( map_entity_property_action_t::EDIT ); } );
        QObject::connect( m_pKeyRename, &QToolButton::clicked, this, [this]() { EditKey( map_entity_property_action_t::RENAME ); } );
        QObject::connect( m_pKeyRemove, &QToolButton::clicked, this, [this]() { RemoveKey(); } );
        QObject::connect( m_pTree, &QTreeWidget::itemDoubleClicked, this, [this]( QTreeWidgetItem *item, int ) {
            if ( item != nullptr && item->data( 0, kEntityKeyRole ).isValid() ) {
                m_pTree->setCurrentItem( item ); EditKey( map_entity_property_action_t::EDIT );
            }
        } );
        auto *rename = new QShortcut( QKeySequence( Qt::Key_F2 ), m_pTree );
        rename->setContext( Qt::WidgetShortcut );
        QObject::connect( rename, &QShortcut::activated, this, [this]() { EditKey( map_entity_property_action_t::RENAME ); } );
        auto *remove = new QShortcut( QKeySequence( Qt::Key_Delete ), m_pTree );
        remove->setContext( Qt::WidgetShortcut );
        QObject::connect( remove, &QShortcut::activated, this, [this]() { RemoveKey(); } );
        ( void )MapWorkspace_AddListener( pWorkspace, &map_properties_t::OnChanged, this );
        ( void )gui::EditorGui_AddStyleListener( pWorkspace->pGui, &map_properties_t::OnStyleChanged, this );
        ( void )EditorSettings_AddListener( &pWorkspace->pGui->settings, &map_properties_t::OnSettingsChanged, this );
        RefreshAppearance();
        Rebuild();
    }

    ~map_properties_t() override
    {
        gui::EditorGui_RemoveStyleListener( m_pWorkspace->pGui, &map_properties_t::OnStyleChanged, this );
        EditorSettings_RemoveListener( &m_pWorkspace->pGui->settings, &map_properties_t::OnSettingsChanged, this );
        MapWorkspace_RemoveListener( m_pWorkspace, &map_properties_t::OnChanged, this );
        m_pTree->clear();
        KeyValue_DestroyDocument( m_pGeometrySnapshot );
    }

    QString Text() const
    {
        QString text = m_pTitle->text() + QLatin1Char( '\n' );
        if ( !m_pSelectionSummary->text().isEmpty() ) { text += m_pSelectionSummary->text() + QLatin1Char( '\n' ); }
        for ( int i = 0; i < m_pTree->topLevelItemCount(); ++i ) { AppendText( m_pTree->topLevelItem( i ), 0, text ); }
        return text;
    }

private:
    void RefreshKeyActions()
    {
        // Owners are validated on rebuild. Button selection should not rescan
        // every entity and nested property in the map; commits validate again.
        const bool canEdit = !m_entityOwners.empty() && !m_pWorkspace->pDocument->bReadOnly &&
            !m_pWorkspace->editPreview.bActive && !EditorHistory_IsTransactionOpen( &m_pWorkspace->history );
        const auto *row = m_pTree->currentItem();
        const bool hasKey = row != nullptr && row->data( 0, kEntityKeyRole ).isValid();
        const bool custom = hasKey && !row->data( 0, kEntityIdentityRole ).toBool();
        m_pKeyAdd->setEnabled( canEdit );
        m_pKeyEdit->setEnabled( canEdit && hasKey && row->data( 0, kEntityEditableRole ).toBool() );
        m_pKeyRename->setEnabled( canEdit && custom );
        m_pKeyRemove->setEnabled( canEdit && custom );
        m_pEntityTools->setVisible( !m_entityOwners.empty() );
    }

    void EditKey( map_entity_property_action_t action )
    {
        const auto *row = m_pTree->currentItem();
        if ( action == map_entity_property_action_t::ADD ) {
            if ( !m_pKeyAdd->isEnabled() ) { return; }
        } else {
            if ( row == nullptr || !row->data( 0, kEntityKeyRole ).isValid() ||
                 ( action == map_entity_property_action_t::RENAME ? !m_pKeyRename->isEnabled() : !m_pKeyEdit->isEnabled() ) ) { return; }
            if ( row->data( 0, kEntityIdentityRole ).toBool() ) { action = map_entity_property_action_t::IDENTITY; }
        }
        const QByteArray key = action == map_entity_property_action_t::ADD ? QByteArray() : row->data( 0, kEntityKeyRole ).toString().toUtf8();
        // No item or document-node pointer survives the dialog's transaction.
        QString committedKey;
        if ( MapEntityPropertyDialog_Show( this, m_pWorkspace, action, { key.constData(), static_cast<usize>( key.size() ) }, &committedKey ) ) {
            const QString path = ( action == map_entity_property_action_t::IDENTITY ? QStringLiteral( "entity.identity:" ) : QStringLiteral( "entity.property:" ) ) + committedKey;
            for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
                if ( PropertyPath( *it ) == path ) { m_pTree->setCurrentItem( *it ); m_pTree->scrollToItem( *it ); break; }
            }
        }
    }

    void RemoveKey()
    {
        if ( !m_pKeyRemove->isEnabled() ) { return; }
        const QByteArray key = m_pTree->currentItem()->data( 0, kEntityKeyRole ).toString().toUtf8();
        if ( !MapWorkspace_RemoveEntityProperty( m_pWorkspace, { key.constData(), static_cast<usize>( key.size() ) } ) ) {
            m_pHelp->setText( QStringLiteral( "Could not remove the key. The live map was preserved." ) );
        }
    }

    void AddEntityKeys()
    {
        m_entityOwners.clear();
        auto &map = *m_pWorkspace->pDocument;
        vector_t<u64> owners{};
        if ( !Vector_Init( &owners, map.pAllocator ) || MapEntityEdit_ResolveOwners( &map,
             { m_pWorkspace->selection.ids.pData, m_pWorkspace->selection.ids.nCount }, &owners ) != map_status_t::OK ) { return; }
        m_entityOwners.assign( owners.pData, owners.pData + owners.nCount );
        QStringList ids;
        for ( const auto id : m_entityOwners ) { ids.append( QString::number( id ) ); }
        auto *group = Group( QStringLiteral( "Entity keys" ), QStringLiteral( "Authored data for %1 owning entities (#%2). Selecting tied geometry targets its entity owner. Class and name are canonical identity fields. Custom keys retain their CYKV types. Double-click to edit." )
            .arg( m_entityOwners.size() ).arg( ids.join( QStringLiteral( ", #" ) ) ) );
        struct row_state_t {
            const key_value_t *first{};
            QString text{};
            usize present{};
            bool mixed{}, canEdit{ true };
        };
        QMap<QString, row_state_t> identityRows, customRows;
        identityRows.insert( QStringLiteral( "class" ), {} );
        identityRows.insert( QStringLiteral( "name" ), {} );
        const auto accumulate = []( row_state_t &row, const key_value_t *value ) {
            if ( value == nullptr ) { return; }
            ++row.present;
            if ( row.first == nullptr ) { row.first = value; row.text = MapEntityProperty_ValueText( value, &row.canEdit ); }
            else {
                row.mixed |= !MapEntityEdit_ValuesEqual( row.first, value );
                row.canEdit &= MapEntityProperty_CanEditValue( value );
            }
        };
        for ( const auto id : m_entityOwners ) {
            const auto *record = MapEntityEdit_FindEntity( &map, id );
            accumulate( identityRows[QStringLiteral( "class" )], KeyValue_Find( record, StringView_FromCString( "class" ) ) );
            accumulate( identityRows[QStringLiteral( "name" )], KeyValue_Find( record, StringView_FromCString( "name" ) ) );
            const auto *properties = KeyValue_Find( record, StringView_FromCString( "properties" ) );
            for ( auto *value = KeyValue_FirstChild( properties ); value != nullptr; value = KeyValue_NextSibling( value ) ) {
                const auto key = KeyValue_Name( value );
                accumulate( customRows[QString::fromUtf8( key.pData, static_cast<qsizetype>( key.cchLength ) )], value );
            }
        }
        const auto &classRow = identityRows[QStringLiteral( "class" )];
        const QString className = !classRow.mixed ? OutlinerText( classRow.first ) : QString();
        const char *entityIcon = OutlinerIcon( map_wire_kind_t::ENTITY, className );
        DecorateProperty( group, m_pWorkspace->pGui->style, map_property_row_kind_t::GROUP, entityIcon );
        const auto addRow = [&]( const QString &key, bool identity, const row_state_t &state ) {
            const QString value = state.mixed ? QStringLiteral( "Multiple values" ) : state.text;
            auto *row = Property( group, key, value );
            const auto type = state.first != nullptr ? KeyValue_Type( state.first ) : key_value_type_t::STRING;
            DecorateProperty( row, m_pWorkspace->pGui->style, type == key_value_type_t::OBJECT ? map_property_row_kind_t::OBJECT :
                type == key_value_type_t::ARRAY ? map_property_row_kind_t::ARRAY : map_property_row_kind_t::VALUE,
                identity && key == QLatin1StringView( "class" ) ? entityIcon : PropertyIcon( key, type ) );
            row->setData( 0, kEntityKeyRole, key );
            row->setData( 0, kEntityIdentityRole, identity );
            row->setData( 0, kEntityEditableRole, state.canEdit );
            QString tip = identity ? QStringLiteral( "Canonical entity %1 field; edit the value. Renaming and removal are unavailable." ).arg( key ) :
                QStringLiteral( "Custom entity key. Type: %1. Edit, rename, or remove through undo." ).arg( state.first != nullptr ? QString::fromLatin1( MapEntityProperty_TypeName( KeyValue_Type( state.first ) ) ) : QStringLiteral( "String" ) );
            if ( state.present < m_entityOwners.size() ) {
                tip += QStringLiteral( "\nPresent on %1 of %2 entities. Editing adds the key where it is missing." ).arg( state.present ).arg( m_entityOwners.size() );
                row->setForeground( 0, gui::EditorStyle_TokenColor( m_pWorkspace->pGui->style, "ui.text.disabled" ) );
            }
            if ( state.mixed ) { tip += QStringLiteral( "\nMultiple values or types. Applying a value replaces it on every target." ); }
            if ( !state.canEdit ) { tip += QStringLiteral( "\nThis value exceeds the text editor limits or contains binary data; it remains preserved." ); }
            row->setToolTip( 0, tip ); row->setToolTip( 1, tip );
        };
        for ( auto it = identityRows.cbegin(); it != identityRows.cend(); ++it ) { addRow( it.key(), true, it.value() ); }
        for ( auto it = customRows.cbegin(); it != customRows.cend(); ++it ) { addRow( it.key(), false, it.value() ); }
    }

    static void OnChanged( void *pContext, u32 changes ) noexcept
    {
        auto *panel = static_cast<map_properties_t *>( pContext );
        if ( ( changes & ( MAP_CHANGE_DOCUMENT | MAP_CHANGE_SELECTION ) ) != 0u ) { panel->Rebuild(); }
        else if ( ( changes & ( MAP_CHANGE_VIEW | MAP_CHANGE_HISTORY ) ) != 0u ) { panel->RefreshKeyActions(); }
    }

    static void OnStyleChanged( void *pContext ) noexcept
    {
        static_cast<map_properties_t *>( pContext )->RefreshAppearance();
    }

    static void OnSettingsChanged( void *pContext, string_view_t path ) noexcept
    {
        if ( path.cchLength == 0u || StringView_Equals( path, StringView_FromCString( kTreeLinesSetting ) ) ) {
            static_cast<map_properties_t *>( pContext )->m_pTree->viewport()->update();
        }
    }

    void RefreshAppearance()
    {
        const auto &style = m_pWorkspace->pGui->style;
        const int size = std::clamp( static_cast<int>( 16.0 * style.density ), 12, 24 );
        m_pTree->setIconSize( QSize( size, size ) );
        for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
            const QByteArray name = ( *it )->data( 0, MAP_PROPERTY_ICON_ROLE ).toString().toLatin1();
            if ( !name.isEmpty() ) { ( *it )->setIcon( 0, gui::EditorStyle_Icon( style, name.constData() ) ); }
        }
        m_pTree->viewport()->update();
    }

    static void AppendText( const QTreeWidgetItem *pItem, int depth, QString &text )
    {
        text += QString( depth * 2, QLatin1Char( ' ' ) ) + pItem->text( 0 ) + QStringLiteral( " = " ) + pItem->text( 1 ) + QLatin1Char( '\n' );
        for ( int i = 0; i < pItem->childCount(); ++i ) { AppendText( pItem->child( i ), depth + 1, text ); }
    }

    void AddRow( const QString &member, const QString &value )
    {
        if ( !m_pShowInternal->isChecked() ) { return; }
        auto *pItem = new QTreeWidgetItem( m_pTree );
        pItem->setText( 0, member );
        pItem->setText( 1, value );
        DecorateProperty( pItem, m_pWorkspace->pGui->style, map_property_row_kind_t::VALUE, PropertyIcon( member ) );
    }

    // Search only materialized rows. Deferred CYKV arrays may contain millions
    // of values; searching must not serialize or walk them implicitly.
    // Matching container labels remain visible with their loaded children.
    static bool FilterItem( QTreeWidgetItem *pItem, const QString &text )
    {
        const bool bSelf = text.isEmpty() || pItem->text( 0 ).contains( text, Qt::CaseInsensitive ) ||
                           pItem->text( 1 ).contains( text, Qt::CaseInsensitive );
        bool bAnyChild = false;
        for ( int i = 0; i < pItem->childCount(); ++i ) { bAnyChild = FilterItem( pItem->child( i ), bSelf ? QString() : text ) || bAnyChild; }
        pItem->setHidden( !( bSelf || bAnyChild ) );
        return bSelf || bAnyChild;
    }

    void ApplyFilter()
    {
        const QString text = m_pFilter->text().trimmed();
        for ( int i = 0; i < m_pTree->topLevelItemCount(); ++i ) { FilterItem( m_pTree->topLevelItem( i ), text ); }
    }

    // The member's full path and value; class and property descriptions
    // come from the game definition once .cygame is decoded.
    void ShowHelp( const QTreeWidgetItem *pItem )
    {
        if ( pItem == nullptr ) {
            m_pHelp->clear(); // Hammer's help area stays empty until a property is chosen.
            return;
        }
        QStringList path;
        for ( const QTreeWidgetItem *pAt = pItem; pAt != nullptr; pAt = pAt->parent() ) { path.prepend( pAt->text( 0 ) ); }
        m_pHelp->setText( QStringLiteral( "<b>%1</b><br>%2" ).arg( path.join( QLatin1Char( '.' ) ).toHtmlEscaped(), ( pItem->text( 1 ) + ( pItem->toolTip( 0 ).isEmpty() ? QString() : QStringLiteral( "\n\n" ) + pItem->toolTip( 0 ) ) ).toHtmlEscaped().replace( QLatin1Char( '\n' ), QStringLiteral( "<br>" ) ) ) );
    }

    static QString PropertyPath( const QTreeWidgetItem *pItem )
    {
        if ( pItem->data( 0, kEntityKeyRole ).isValid() ) {
            // Custom properties.class/name are distinct from canonical identity.
            return ( pItem->data( 0, kEntityIdentityRole ).toBool() ? QStringLiteral( "entity.identity:" ) : QStringLiteral( "entity.property:" ) ) +
                pItem->data( 0, kEntityKeyRole ).toString();
        }
        QStringList path;
        for ( const auto *p = pItem; p != nullptr; p = p->parent() ) { path.prepend( p->text( 0 ) ); }
        return path.join( QChar( 0x1f ) );
    }

    void RestoreExpansion( QTreeWidgetItem *pItem, const QSet<QString> &expanded )
    {
        const bool open = expanded.contains( PropertyPath( pItem ) );
        pItem->setExpanded( open );
        if ( open ) { Populate( pItem ); }
        for ( int i = 0; i < pItem->childCount(); ++i ) { RestoreExpansion( pItem->child( i ), expanded ); }
    }

    void Rebuild()
    {
        QStringList selected;
        for ( usize i = 0u; i < EditorSelection_Count( &m_pWorkspace->selection ); ++i ) {
            selected.append( QString::number( EditorSelection_At( &m_pWorkspace->selection, i ) ) );
        }
        QString selectionKey = selected.join( QLatin1Char( ',' ) );
        if ( MapWorkspace_HasMeshFace( m_pWorkspace ) ) { selectionKey += QStringLiteral( "/face:%1" ).arg( m_pWorkspace->selectedMeshFaceId ); }
        const bool restore = selectionKey == m_selectionKey;
        const QString currentPath = restore && m_pTree->currentItem() != nullptr ? PropertyPath( m_pTree->currentItem() ) : QString();
        QSet<QString> expanded;
        if ( restore ) {
            for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
                if ( ( *it )->isExpanded() ) { expanded.insert( PropertyPath( *it ) ); }
            }
        }
        const QSignalBlocker blocker( m_pTree );
        m_pTree->clear();
        m_pGeometryData = nullptr;
        KeyValue_DestroyDocument( m_pGeometrySnapshot );
        m_pGeometrySnapshot = nullptr;
        RebuildRows();
        if ( restore ) {
            for ( int i = 0; i < m_pTree->topLevelItemCount(); ++i ) { RestoreExpansion( m_pTree->topLevelItem( i ), expanded ); }
        }
        m_selectionKey = selectionKey;
        ApplyFilter();
        ShowHelp( nullptr );
        if ( !currentPath.isEmpty() ) {
            for ( QTreeWidgetItemIterator it( m_pTree ); *it != nullptr; ++it ) {
                if ( PropertyPath( *it ) == currentPath ) { m_pTree->setCurrentItem( *it ); ShowHelp( *it ); break; }
            }
        }
        RefreshKeyActions();
    }

    static QString BoundsText( const map_bounds_t &bounds )
    {
        const math::aabbd_t &box = bounds.box;
        return QStringLiteral( "[%1, %2, %3] to [%4, %5, %6]" )
            .arg( box.minimum.x ).arg( box.minimum.y ).arg( box.minimum.z )
            .arg( box.maximum.x ).arg( box.maximum.y ).arg( box.maximum.z );
    }

    map_bounds_t RefreshSelectionSummary()
    {
        map_bounds_t bounds{};
        const map_workspace_t &workspace = *m_pWorkspace;
        const usize nSelected = EditorSelection_Count( &workspace.selection );
        m_pSelectionSummary->clear();
        m_pSelectionSummary->setToolTip( QString() );
        m_pSelectionSummary->setVisible( nSelected != 0u );
        if ( nSelected == 0u ) { return bounds; }

        // Read the same cached geometry as the views. Entity boxes describe
        // editor markers, not physical geometry, so they must not inflate
        // a mixed selection's dimensions.
        usize counts[5]{};
        usize nUnknown = 0u;
        for ( usize i = 0u; i < nSelected; ++i ) {
            const map_wire_object_t *pObject = MapWireframe_FindObject( workspace.wire, EditorSelection_At( &workspace.selection, i ) );
            if ( pObject == nullptr ) {
                if ( MapWireframe_FindEntity( workspace.wire, EditorSelection_At( &workspace.selection, i ) ) != nullptr ) { ++counts[4]; }
                else { ++nUnknown; }
                continue;
            }
            ++counts[static_cast<usize>( pObject->kind )];
            if ( pObject->kind != map_wire_kind_t::ENTITY ) { MapBounds_AddBounds( bounds, pObject->bounds ); }
        }
        constexpr const char *singular[]{ "brush", "mesh", "patch", "terrain", "entity" };
        constexpr const char *plural[]{ "brushes", "meshes", "patches", "terrains", "entities" };
        QStringList parts;
        for ( usize i = 0u; i < 5u; ++i ) {
            if ( counts[i] != 0u ) {
                parts.append( QStringLiteral( "%1 %2" ).arg( counts[i] ).arg( QString::fromLatin1( counts[i] == 1u ? singular[i] : plural[i] ) ) );
            }
        }
        if ( nUnknown != 0u ) { parts.append( QStringLiteral( "%1 other %2" ).arg( nUnknown ).arg( nUnknown == 1u ? "object" : "objects" ) ); }
        QString summary = parts.join( QStringLiteral( " \u00B7 " ) );
        if ( MapWorkspace_HasMeshFace( &workspace ) ) { summary += QStringLiteral( " \u00B7 1 face selected" ); }
        if ( bounds.bHas ) {
            const math::vec3d_t &lo = bounds.box.minimum;
            const math::vec3d_t &hi = bounds.box.maximum;
            summary += ( MapWorkspace_HasMeshFace( &workspace ) ? QStringLiteral( "\nMesh size  X %1 \u00B7 Y %2 \u00B7 Z %3 u" ) :
                         QStringLiteral( "\nSize  X %1 \u00B7 Y %2 \u00B7 Z %3 u" ) ).arg( hi.x - lo.x ).arg( hi.y - lo.y ).arg( hi.z - lo.z );
            m_pSelectionSummary->setToolTip( QStringLiteral( "World-space geometry bounds (X, Y, Z)\n%1\nEntity helper boxes are excluded." )
                                               .arg( BoundsText( bounds ) ) );
        }
        m_pSelectionSummary->setText( summary );
        return bounds;
    }

    QTreeWidgetItem *Group( const QString &name, const QString &description = QString() )
    {
        auto *pItem = new QTreeWidgetItem( m_pTree );
        pItem->setText( 0, name );
        DecorateProperty( pItem, m_pWorkspace->pGui->style, map_property_row_kind_t::GROUP, PropertyGroupIcon( name ) );
        pItem->setToolTip( 0, description );
        pItem->setFirstColumnSpanned( true );
        QFont font = pItem->font( 0 );
        font.setBold( true );
        pItem->setFont( 0, font );
        pItem->setExpanded( true );
        return pItem;
    }

    QTreeWidgetItem *Property( QTreeWidgetItem *pParent, const QString &name, const QString &value, const QString &description = QString() )
    {
        auto *pItem = new QTreeWidgetItem( pParent, { name, value } );
        const bool material = pParent != nullptr && pParent->text( 0 ) == QLatin1StringView( "Materials" );
        DecorateProperty( pItem, m_pWorkspace->pGui->style, map_property_row_kind_t::VALUE, material ? "asset-material" : PropertyIcon( name ) );
        pItem->setToolTip( 0, description );
        pItem->setToolTip( 1, value );
        return pItem;
    }

    // Sandbox's small folder at the end of a row: opens what the row names
    // (a material, or "#id" for an entity) in the application's Database
    // View. The value text stays in the cell; the button sits over its end.
    void AddOpenButton( QTreeWidgetItem *pItem, const QString &target, const QString &tip )
    {
        if ( pItem == nullptr || target.isEmpty() ||
             EditorCommands_Find( &m_pWorkspace->pGui->commands, StringView_FromCString( "assets.database" ) ) == nullptr ) { return; }
        auto *pBox = new QWidget();
        pBox->setAttribute( Qt::WA_TranslucentBackground );
        auto *pLayout = new QHBoxLayout( pBox );
        pLayout->setContentsMargins( 0, 0, 2, 0 );
        pLayout->addStretch( 1 );
        auto *pOpen = new QToolButton( pBox );
        pOpen->setObjectName( QStringLiteral( "MapPropertiesOpen" ) );
        pOpen->setAutoRaise( true );
        pOpen->setIcon( gui::EditorStyle_Icon( m_pWorkspace->pGui->style, "asset-folder" ) );
        pOpen->setIconSize( QSize( 14, 14 ) );
        pOpen->setToolTip( tip );
        pOpen->setProperty( "databaseTarget", target );
        QObject::connect( pOpen, &QToolButton::clicked, this, [this, target]() {
            const QByteArray utf8 = target.toUtf8();
            const string_view_t argument{ utf8.constData(), static_cast<usize>( utf8.size() ) };
            ( void )EditorCommands_Execute( &m_pWorkspace->pGui->commands, StringView_FromCString( "assets.database" ), command_args_t{ &argument, 1u } );
        } );
        pLayout->addWidget( pOpen );
        m_pTree->setItemWidget( pItem, 1, pBox );
    }

    static QString VectorText( const math::vec3d_t &v )
    {
        return QStringLiteral( "[%1, %2, %3]" ).arg( v.x, 0, 'g', 15 ).arg( v.y, 0, 'g', 15 ).arg( v.z, 0, 'g', 15 );
    }

    void Populate( QTreeWidgetItem *pItem )
    {
        if ( pItem->data( 0, kPropertyLoadedRole ).toBool() ) { return; }
        const QSignalBlocker blocker( m_pTree );
        if ( pItem->data( 0, kPropertyGeometryRole ).toBool() ) {
            pItem->setData( 0, kPropertyLoadedRole, true );
            auto &map = *m_pWorkspace->pDocument;
            key_value_document_desc_t desc{};
            desc.pAllocator = map.pAllocator;
            m_pGeometrySnapshot = KeyValue_CreateDocument( desc );
            bool success = m_pGeometrySnapshot != nullptr && KeyValue_SetRootType( m_pGeometrySnapshot, key_value_type_t::OBJECT );
            if ( success ) {
                auto *pOut = KeyValue_Root( m_pGeometrySnapshot );
                const geometry::geometry_source_id_t id{ m_geometryId };
                if ( geometry::GeometryDocument_FindBrush( &map.geometry, id ) != nullptr ) {
                    geometry::brush_source_t brush{};
                    success = geometry::GeometryDocument_TryCopyBrushSource( &map.geometry, id, map.pAllocator, &brush ) == geometry::geometry_status_t::OK;
                    if ( success ) { success = MapGeometry_WriteBrush( brush, map.materials, m_pGeometrySnapshot, pOut ); }
                    geometry::BrushSource_Shutdown( &brush );
                } else if ( const auto *pMesh = geometry::GeometryDocument_FindMesh( &map.geometry, id ) ) {
                    success = MapGeometry_WriteMesh( *pMesh, map.materials, map.pAllocator, m_pGeometrySnapshot, pOut );
                } else if ( const auto *pPatch = geometry::GeometryDocument_FindPatch( &map.geometry, id ) ) {
                    success = MapGeometry_WritePatch( *pPatch, map.materials, m_pGeometrySnapshot, pOut );
                } else if ( const auto *pTerrain = geometry::GeometryDocument_FindHeightField( &map.geometry, id ) ) {
                    success = MapGeometry_WriteTerrain( *pTerrain, m_pGeometrySnapshot, pOut );
                } else { success = false; }
            }
            if ( success ) { AddMembers( pItem, m_pTree, m_pWorkspace->pGui->style, KeyValue_Root( m_pGeometrySnapshot ), 1 ); }
            else { Property( pItem, QStringLiteral( "Unavailable" ), QStringLiteral( "Could not describe the current geometry." ) ); }
            return;
        }
        if ( const auto *pNode = PropertyNode( pItem ) ) {
            pItem->setData( 0, kPropertyLoadedRole, true );
            AddMembers( pItem, m_pTree, m_pWorkspace->pGui->style, pNode, 1, pItem->data( 0, kPropertyBeginRole ).toULongLong(), pItem->data( 0, kPropertyEndRole ).toULongLong() );
        }
    }

    bool AddGeometry( u64 objectId )
    {
        auto &map = *m_pWorkspace->pDocument;
        const geometry::geometry_source_id_t id{ objectId };
        const auto *pBrush = geometry::GeometryDocument_FindBrush( &map.geometry, id );
        const auto *pMesh = geometry::GeometryDocument_FindMesh( &map.geometry, id );
        const auto *pPatch = geometry::GeometryDocument_FindPatch( &map.geometry, id );
        const auto *pTerrain = geometry::GeometryDocument_FindHeightField( &map.geometry, id );
        if ( pBrush == nullptr && pMesh == nullptr && pPatch == nullptr && pTerrain == nullptr ) { return false; }
        auto *pGeometry = Group( QStringLiteral( "Geometry" ), QStringLiteral( "Current editable geometry, including unsaved changes. Counts describe the authoring representation." ) );
        const char *geometryIcon = pBrush != nullptr ? "tool-block" : pMesh != nullptr ? "select-meshes" : pPatch != nullptr ? "tool-patch" : "tool-terrain";
        DecorateProperty( pGeometry, m_pWorkspace->pGui->style, map_property_row_kind_t::GROUP, geometryIcon );
        const auto count = [this, pGeometry]( const char *pName, usize value ) { Property( pGeometry, QString::fromLatin1( pName ), QString::number( value ) ); };
        QMap<qulonglong, qulonglong> materialUses;
        if ( pBrush != nullptr ) {
            count( "Planes", pBrush->sides.nCount );
            const auto *pAttributes = geometry::GeometryDocument_FindBrushAttributes( &map.geometry, id );
            for ( usize i = 0u; i < pBrush->sides.nCount; ++i ) {
                const usize index = pBrush->sides.pData[i].iAttributeIndex;
                if ( pAttributes != nullptr && index < pAttributes->records.nCount ) { ++materialUses[pAttributes->records.pData[index].material.value]; }
            }
        } else if ( pMesh != nullptr ) {
            count( "Vertices", geometry::EditableMesh_VertexCount( &pMesh->mesh ) );
            count( "Edges", geometry::EditableMesh_EdgeCount( &pMesh->mesh ) );
            count( "Faces", geometry::EditableMesh_FaceCount( &pMesh->mesh ) );
            count( "Half-edges", geometry::EditableMesh_HalfEdgeCount( &pMesh->mesh ) );
            count( "Loops", geometry::EditableMesh_LoopCount( &pMesh->mesh ) );
            count( "Shells", geometry::EditableMesh_ShellCount( &pMesh->mesh ) );
            (void)GenerationPool_ForEach( &pMesh->mesh.faces, [&]( geometry::geometry_mesh_face_handle_t handle, const geometry::mesh_face_record_t & ) noexcept -> bool_t {
                ++materialUses[geometry::MeshAttributeStore_GetFace( &pMesh->attributes, handle ).material.value];
                return CY_TRUE;
            } );
        } else if ( pPatch != nullptr ) {
            Property( pGeometry, QStringLiteral( "Basis" ), pPatch->basis == geometry::patch_basis_t::BICUBIC_BEZIER ? QStringLiteral( "Bicubic Bezier" ) : QStringLiteral( "Biquadratic Bezier" ) );
            Property( pGeometry, QStringLiteral( "Control grid" ), QStringLiteral( "%1 x %2" ).arg( pPatch->cColumns ).arg( pPatch->cRows ) );
            count( "Control points", pPatch->controls.nCount );
            ++materialUses[pPatch->materialId];
        } else if ( pTerrain != nullptr ) {
            Property( pGeometry, QStringLiteral( "Cells" ), QStringLiteral( "%1 x %2" ).arg( pTerrain->cCellsX ).arg( pTerrain->cCellsY ) );
            Property( pGeometry, QStringLiteral( "Sample spacing" ), QString::number( pTerrain->cellSize, 'g', 15 ) );
            Property( pGeometry, QStringLiteral( "Origin" ), VectorText( pTerrain->origin ) );
            count( "Height samples", pTerrain->heights.nCount );
            count( "Tiles", pTerrain->tiles.nCount );
            count( "Cells per tile axis", pTerrain->tileCells );
            usize holes = 0u;
            for ( usize i = 0u; i < pTerrain->holes.nCount; ++i ) { holes += pTerrain->holes.pData[i] != 0u ? 1u : 0u; }
            count( "Hole cells", holes );
        }
        if ( !materialUses.isEmpty() ) {
            auto *pMaterials = Group( QStringLiteral( "Materials" ), QStringLiteral( "Current assignments: brush sides, mesh faces, or patch surface. Numeric references are map-local, not persistent asset IDs." ) );
            for ( auto it = materialUses.cbegin(); it != materialUses.cend(); ++it ) {
                const string_view_t path = MapMaterials_Path( &map.materials, it.key() );
                const QString name = it.key() == 0u ? QStringLiteral( "Unassigned" ) : path.cchLength == 0u ? QStringLiteral( "Unresolved reference #%1" ).arg( it.key() ) : QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) );
                const QString unit = pBrush != nullptr ? ( it.value() == 1u ? QStringLiteral( "side" ) : QStringLiteral( "sides" ) )
                    : pMesh != nullptr ? ( it.value() == 1u ? QStringLiteral( "face" ) : QStringLiteral( "faces" ) ) : QStringLiteral( "surface" );
                QTreeWidgetItem *pMaterial = Property( pMaterials, name, QStringLiteral( "%1 %2" ).arg( it.value() ).arg( unit ), QStringLiteral( "Map-local material reference: %1" ).arg( it.key() ) );
                if ( it.key() != 0u && path.cchLength != 0u ) { AddOpenButton( pMaterial, name, QStringLiteral( "Open %1 in the Database View: shader, textures, parameters" ).arg( name ) ); }
            }
        }
        m_geometryId = objectId;
        m_pGeometryData = Group( QStringLiteral( "Geometry Data" ), QStringLiteral( "Live CYMAP geometry fields: positions, source IDs, topology, UVs and explicit surface attributes. Defaults follow the map format. Expand arrays to inspect their values." ) );
        DecorateProperty( m_pGeometryData, m_pWorkspace->pGui->style, map_property_row_kind_t::GROUP, geometryIcon );
        m_pGeometryData->setData( 0, kPropertyGeometryRole, true );
        m_pGeometryData->setChildIndicatorPolicy( QTreeWidgetItem::ShowIndicator );
        m_pGeometryData->setExpanded( false );
        return true;
    }

    void AddSelectedMeshFace( u64 objectId )
    {
        if ( !MapWorkspace_HasMeshFace( m_pWorkspace ) || m_pWorkspace->selectedMeshFaceObject != objectId ) { return; }
        auto &map = *m_pWorkspace->pDocument;
        const auto *mesh = geometry::GeometryDocument_FindMesh( &map.geometry, { objectId } );
        geometry::geometry_mesh_face_handle_t handle{};
        if ( mesh == nullptr || !geometry::MeshSource_TryFindFace( mesh, { m_pWorkspace->selectedMeshFaceId }, &handle ) ) { return; }
        const map_wire_face_t *face = nullptr;
        for ( usize i = 0; i < m_pWorkspace->wire.faces.nCount; ++i ) {
            const auto &candidate = m_pWorkspace->wire.faces.pData[i];
            if ( candidate.id == objectId && candidate.faceId == m_pWorkspace->selectedMeshFaceId ) { face = &candidate; break; }
        }
        if ( face == nullptr ) { return; }
        auto *group = Group( QStringLiteral( "Selected mesh face" ), QStringLiteral( "The authored surface selected for face modeling. Mesh size above describes the whole object; these bounds describe only this face." ) );
        Property( group, QStringLiteral( "Face ID" ), QString::number( face->faceId ), QStringLiteral( "Stable authored face identity. Extrude and inset retain this cap or inner face." ) );
        Property( group, QStringLiteral( "Corners" ), QString::number( face->nIndices ) );
        Property( group, QStringLiteral( "Normal" ), VectorText( face->normal ), QStringLiteral( "World-space direction used by outward extrusion." ) );
        map_bounds_t bounds{};
        QStringList vertices;
        for ( usize i = 0; i < face->nIndices; ++i ) {
            const u32 point = m_pWorkspace->wire.faceIndices.pData[face->iFirstIndex + i];
            MapBounds_AddPoint( bounds, m_pWorkspace->wire.points.pData[point] );
            if ( i < 32 ) { vertices.append( QString::number( m_pWorkspace->wire.pointSourceIds.pData[point] ) ); }
        }
        if ( face->nIndices > 32 ) { vertices.append( QStringLiteral( "... %1 more" ).arg( face->nIndices - 32 ) ); }
        Property( group, QStringLiteral( "Vertex IDs" ), vertices.join( QStringLiteral( ", " ) ) );
        if ( bounds.bHas ) { Property( group, QStringLiteral( "Face bounds" ), BoundsText( bounds ) ); }
        const auto attributes = geometry::MeshAttributeStore_GetFace( &mesh->attributes, handle );
        Property( group, QStringLiteral( "Smoothing groups" ), QString::number( attributes.smoothingGroups ), QStringLiteral( "Authored smoothing-group bitmask." ) );
        const auto path = MapMaterials_Path( &map.materials, attributes.material.value );
        const QString material = attributes.material.value == 0 ? QStringLiteral( "Unassigned" ) : path.cchLength == 0 ?
            QStringLiteral( "Unresolved reference #%1" ).arg( attributes.material.value ) : QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) );
        auto *row = Property( group, QStringLiteral( "Material" ), material );
        if ( attributes.material.value != 0 && path.cchLength != 0 ) { AddOpenButton( row, material, QStringLiteral( "Inspect this face's material in the Database View" ) ); }
    }

    void RebuildRows()
    {
        map_workspace_t &workspace = *m_pWorkspace;
        const usize nSelected = EditorSelection_Count( &workspace.selection );
        const map_bounds_t selectionBounds = RefreshSelectionSummary();
        AddEntityKeys();
        if ( nSelected == 0u ) {
            // Nothing selected: the map's own properties, as Hammer shows
            // worldspawn.
            m_pTitle->setText( QStringLiteral( "Map: %1" ).arg( MapWorkspace_DisplayName( &workspace ) ) );
            AddMembers( nullptr, m_pTree, m_pWorkspace->pGui->style, SettingsDocument_Root( &workspace.pDocument->root ), 0 );
            return;
        }
        if ( nSelected > 1u ) {
            m_pTitle->setText( QStringLiteral( "%1 objects selected" ).arg( nSelected ) );
            auto *pSelection = Group( QStringLiteral( "Selection" ) );
            Property( pSelection, QStringLiteral( "Objects" ), QString::number( nSelected ) );
            QSet<QString> layers;
            QStringList identities;
            for ( usize i = 0u; i < nSelected; ++i ) {
                const u64 id = EditorSelection_At( &workspace.selection, i );
                if ( i < 32u ) { identities.append( QString::number( id ) ); }
                const auto *pObject = MapWireframe_FindObject( workspace.wire, id );
                const auto *pEntity = MapWireframe_FindEntity( workspace.wire, id );
                const u32 layer = pObject != nullptr ? pObject->iLayer : pEntity != nullptr ? pEntity->iLayer : ~u32{ 0u };
                layers.insert( layer < workspace.pDocument->layers.nCount ? QString::fromUtf8( workspace.pDocument->layers.pData[layer].id ) : QStringLiteral( "Unresolved" ) );
            }
            if ( nSelected > 32u ) { identities.append( QStringLiteral( "... %1 more" ).arg( nSelected - 32u ) ); }
            Property( pSelection, QStringLiteral( "Object IDs" ), identities.join( QStringLiteral( ", " ) ) );
            Property( pSelection, QStringLiteral( "Layer" ), layers.size() == 1 ? *layers.cbegin() : QStringLiteral( "Mixed (%1 layers)" ).arg( layers.size() ) );
            if ( selectionBounds.bHas ) { AddRow( QStringLiteral( "(bounds)" ), BoundsText( selectionBounds ) ); }
            return;
        }
        const u64 id = EditorSelection_At( &workspace.selection, 0u );
        const map_wire_object_t *pObject = MapWireframe_FindObject( workspace.wire, id );
        const map_wire_entity_t *pEntity = MapWireframe_FindEntity( workspace.wire, id );
        map_chunk_t *pChunk = nullptr;
        const key_value_t *pRecord = pEntity != nullptr ? MapEntityEdit_FindEntity( workspace.pDocument, id, &pChunk ) :
            MapDocument_FindObject( workspace.pDocument, id, &pChunk );
        const QString className = OutlinerText( OutlinerMember( pRecord, "class" ) );
        const QString kind = pEntity != nullptr ? QStringLiteral( "Entity" ) : QString::fromUtf8( pObject != nullptr ? KindName( pObject->kind ) : "Object" );
        m_pTitle->setText( QStringLiteral( "%1  #%2" ).arg( className.isEmpty() ? kind : className ).arg( id ) );
        auto *pIdentity = Group( QStringLiteral( "Identity" ) );
        DecorateProperty( pIdentity, m_pWorkspace->pGui->style, map_property_row_kind_t::GROUP,
            OutlinerIcon( pEntity != nullptr ? map_wire_kind_t::ENTITY : pObject != nullptr ? pObject->kind : map_wire_kind_t::ENTITY, className ) );
        Property( pIdentity, QStringLiteral( "Type" ), kind );
        Property( pIdentity, QStringLiteral( "ID" ), QString::number( id ), QStringLiteral( "Stable object source identity." ) );
        const QString name = OutlinerText( OutlinerMember( pRecord, "name" ) );
        Property( pIdentity, QStringLiteral( "Name" ), name.isEmpty() ? QStringLiteral( "(unnamed)" ) : name );
        if ( !className.isEmpty() ) {
            QTreeWidgetItem *pClass = Property( pIdentity, QStringLiteral( "Class" ), className );
            if ( pEntity != nullptr ) { AddOpenButton( pClass, QStringLiteral( "#%1" ).arg( id ), QStringLiteral( "Inspect this entity's keys, origin, and rotation in the Database View" ) ); }
        }
        Property( pIdentity, QStringLiteral( "Document" ), workspace.pDocument->bReadOnly ? QStringLiteral( "Read-only" ) : QStringLiteral( "Editable through commands" ) );
        auto *pPlacement = Group( QStringLiteral( "Placement" ) );
        const u32 layer = pObject != nullptr ? pObject->iLayer : pEntity != nullptr ? pEntity->iLayer : 0u;
        const QString layerName = pObject != nullptr || pEntity != nullptr
            ? layer < workspace.pDocument->layers.nCount ? QString::fromUtf8( workspace.pDocument->layers.pData[layer].id ) : QStringLiteral( "Unresolved layer index %1" ).arg( layer )
            : pChunk != nullptr ? QString::fromUtf8( pChunk->layer ) : QStringLiteral( "Unassigned" );
        Property( pPlacement, QStringLiteral( "Layer" ), layerName );
        if ( pObject != nullptr && pObject->kind != map_wire_kind_t::ENTITY ) {
            Property( pPlacement, QStringLiteral( "Owner entity" ), pObject->owner != 0u ? QString::number( pObject->owner ) : QStringLiteral( "World" ), QStringLiteral( "The entity that owns this geometry; distinct from an entity's attachment parent." ) );
        }
        if ( pEntity != nullptr ) {
            const u64 parent = OutlinerId( OutlinerMember( pRecord, "parent" ) );
            Property( pPlacement, QStringLiteral( "Parent entity" ), parent != 0u ? QString::number( parent ) : QStringLiteral( "None" ) );
            Property( pPlacement, QStringLiteral( "Origin" ), pEntity->bHasOrigin ? VectorText( pEntity->origin ) : QStringLiteral( "Not authored" ) );
            Property( pPlacement, QStringLiteral( "Owned geometry" ), QString::number( pEntity->nOwned ) );
        }
        if ( m_pShowInternal->isChecked() ) {
            if ( pChunk != nullptr ) {
                char path[MAP_CHUNK_PATH_CAPACITY]{};
                if ( MapDocument_ChunkPath( *pChunk, path ) != 0u ) {
                    Property( pPlacement, QStringLiteral( "(chunk)" ), QString::fromUtf8( path ), QStringLiteral( "Chunk containing retained source metadata. Geometry moved across cells can be reassigned when saved." ) );
                }
                if ( pChunk->sourcePath[0] != '\0' ) { Property( pPlacement, QStringLiteral( "Source file" ), QString::fromUtf8( pChunk->sourcePath ) ); }
            } else { Property( pPlacement, QStringLiteral( "(chunk)" ), QStringLiteral( "Assigned when saved" ) ); }
            if ( pObject != nullptr && pObject->bounds.bHas && pObject->kind != map_wire_kind_t::ENTITY ) {
                Property( pPlacement, QStringLiteral( "(bounds)" ), BoundsText( pObject->bounds ), QStringLiteral( "Current world-space geometry bounds. Entity helper boxes are excluded." ) );
                Property( pPlacement, QStringLiteral( "Bounds center" ), VectorText( MapBounds_Center( pObject->bounds ) ) );
            }
        }
        AddSelectedMeshFace( id );
        const bool bGeometry = AddGeometry( id );
        if ( pRecord != nullptr ) {
            auto *pSource = Group( QStringLiteral( "Source Fields" ), bGeometry
                ? QStringLiteral( "Retained CYKV metadata, including extension fields. Live shape and surface data are shown in Geometry Data." )
                : QStringLiteral( "Authored CYKV fields. This inspector does not invent runtime entity properties or bypass undo." ) );
            AddMembers( pSource, m_pTree, m_pWorkspace->pGui->style, pRecord, 0 );
        }
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    QLineEdit *m_pFilter{ nullptr };
    QAction *m_pShowInternal{ nullptr };
    QLabel *m_pTitle{ nullptr };
    QLabel *m_pSelectionSummary{ nullptr };
    QTreeWidget *m_pTree{ nullptr };
    QLabel *m_pHelp{ nullptr };
    QWidget *m_pEntityTools{ nullptr };
    QToolButton *m_pKeyAdd{ nullptr }, *m_pKeyEdit{ nullptr }, *m_pKeyRename{ nullptr }, *m_pKeyRemove{ nullptr };
    std::vector<u64> m_entityOwners{};
    key_value_document_t *m_pGeometrySnapshot{ nullptr };
    QTreeWidgetItem *m_pGeometryData{ nullptr };
    u64 m_geometryId{ 0u };
    QString m_selectionKey{ QStringLiteral( "uninitialized" ) };
};

} // namespace

QWidget *MapOutliner_Create( QWidget *pParent, map_workspace_t *pWorkspace )
{
    return new map_outliner_t( pParent, pWorkspace );
}

int MapOutliner_VisibleRowCount( QWidget *pOutliner )
{
    auto *pImpl = dynamic_cast<map_outliner_t *>( pOutliner );
    CY_ASSERT( pImpl != nullptr );
    return pImpl != nullptr ? pImpl->VisibleRowCount() : 0;
}

void MapOutliner_SetFilter( QWidget *pOutliner, const QString &text )
{
    auto *pImpl = dynamic_cast<map_outliner_t *>( pOutliner );
    CY_ASSERT( pImpl != nullptr );
    if ( pImpl != nullptr ) { pImpl->SetFilter( text ); }
}

QWidget *MapProperties_Create( QWidget *pParent, map_workspace_t *pWorkspace )
{
    return new map_properties_t( pParent, pWorkspace );
}

QString MapProperties_Text( QWidget *pProperties )
{
    auto *pImpl = dynamic_cast<map_properties_t *>( pProperties );
    CY_ASSERT( pImpl != nullptr );
    return pImpl != nullptr ? pImpl->Text() : QString();
}

} // namespace cypher::editor::map
