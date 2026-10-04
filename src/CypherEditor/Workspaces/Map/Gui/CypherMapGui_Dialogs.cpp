//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Dialogs.cpp
//  Purpose: Implements Map Info and Go To.
//  Details: Both read the workspace's wireframe, which already knows every
//           object's kind, owner, layer, and bounds, and every entity's
//           class and name; nothing here walks the document trees again.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Dialogs.h"

#include "CypherEditor_AssetCatalog.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherMap_Document.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace cypher::editor::map
{

using namespace cypher::common;

namespace
{

constexpr f64 kGoToHalfExtent = 128.0; // Frames a point with a little of its surroundings.

QString Count( int n, const char *pOne, const char *pMany )
{
    return QStringLiteral( "%1 %2" ).arg( n ).arg( QString::fromLatin1( n == 1 ? pOne : pMany ) );
}

QString Extent( const map_bounds_t &bounds )
{
    if ( !bounds.bHas ) { return QStringLiteral( "empty" ); }
    const math::vec3d_t &a = bounds.box.minimum;
    const math::vec3d_t &b = bounds.box.maximum;
    return QStringLiteral( "%1w %2l %3h from (%4 %5 %6) to (%7 %8 %9)" )
        .arg( b.x - a.x, 0, 'f', 0 )
        .arg( b.y - a.y, 0, 'f', 0 )
        .arg( b.z - a.z, 0, 'f', 0 )
        .arg( a.x, 0, 'f', 0 )
        .arg( a.y, 0, 'f', 0 )
        .arg( a.z, 0, 'f', 0 )
        .arg( b.x, 0, 'f', 0 )
        .arg( b.y, 0, 'f', 0 )
        .arg( b.z, 0, 'f', 0 );
}

class map_info_dialog_t final : public QDialog {
public:
    map_info_dialog_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QDialog( pParent ), m_pWorkspace( pWorkspace )
    {
        setObjectName( QStringLiteral( "MapInfoDialog" ) );
        setWindowTitle( QStringLiteral( "Map Info" ) );
        resize( 560, 600 );
        auto *pLayout = new QVBoxLayout( this );
        m_pTitle = new QLabel( this );
        m_pTitle->setObjectName( QStringLiteral( "MapInfoTitle" ) );
        QFont font = m_pTitle->font();
        font.setBold( true );
        m_pTitle->setFont( font );
        pLayout->addWidget( m_pTitle );
        m_pSummary = new QFormLayout();
        m_pSummary->setLabelAlignment( Qt::AlignRight );
        pLayout->addLayout( m_pSummary );
        auto *pClassesTitle = new QLabel( QStringLiteral( "Entities by class" ), this );
        pClassesTitle->setFont( font );
        pLayout->addWidget( pClassesTitle );
        m_pClasses = new QTreeWidget( this );
        m_pClasses->setObjectName( QStringLiteral( "MapInfoClasses" ) );
        m_pClasses->setColumnCount( 2 );
        m_pClasses->setHeaderLabels( { QStringLiteral( "Class" ), QStringLiteral( "Count" ) } );
        m_pClasses->setRootIsDecorated( false );
        m_pClasses->setAlternatingRowColors( true );
        m_pClasses->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        m_pClasses->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
        m_pClasses->header()->setStretchLastSection( false );
        pLayout->addWidget( m_pClasses, 1 );

        auto *pButtons = new QDialogButtonBox( QDialogButtonBox::Close, this );
        m_pSelectClass = pButtons->addButton( QStringLiteral( "Select All of Class" ), QDialogButtonBox::ActionRole );
        m_pSelectClass->setToolTip( QStringLiteral( "Select every entity of the chosen class" ) );
        QPushButton *pCopy = pButtons->addButton( QStringLiteral( "Copy" ), QDialogButtonBox::ActionRole );
        QPushButton *pRefresh = pButtons->addButton( QStringLiteral( "Refresh" ), QDialogButtonBox::ActionRole );
        pLayout->addWidget( pButtons );
        QObject::connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
        QObject::connect( pCopy, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText( MapInfo_Text( m_pWorkspace ) ); } );
        QObject::connect( pRefresh, &QPushButton::clicked, this, [this]() { Refresh(); } );
        QObject::connect( m_pSelectClass, &QPushButton::clicked, this, [this]() { SelectCurrentClass(); } );
        QObject::connect( m_pClasses, &QTreeWidget::itemDoubleClicked, this, [this]( QTreeWidgetItem *, int ) { SelectCurrentClass(); } );
        QObject::connect( m_pClasses, &QTreeWidget::currentItemChanged, this, [this]( QTreeWidgetItem *pItem, QTreeWidgetItem * ) {
            m_pSelectClass->setEnabled( pItem != nullptr );
        } );
        Refresh();
    }

    void Refresh()
    {
        const map_info_t info = MapInfo_Compute( m_pWorkspace );
        m_pTitle->setText( MapWorkspace_DisplayName( m_pWorkspace ) );
        while ( m_pSummary->rowCount() > 0 ) { m_pSummary->removeRow( 0 ); }
        const auto row = [this]( const QString &label, const QString &value ) {
            auto *pValue = new QLabel( value, this );
            pValue->setTextInteractionFlags( Qt::TextSelectableByMouse );
            m_pSummary->addRow( label, pValue );
        };
        row( QStringLiteral( "Geometry" ), QStringLiteral( "%1, %2, %3, %4" )
                                               .arg( Count( info.nBrushes, "brush", "brushes" ), Count( info.nMeshes, "mesh", "meshes" ),
                                                     Count( info.nPatches, "patch", "patches" ), Count( info.nTerrains, "terrain", "terrains" ) ) );
        row( QStringLiteral( "Entities" ), QStringLiteral( "%1 (%2 point, %3 with geometry)" )
                                               .arg( info.nEntities )
                                               .arg( info.nPointEntities )
                                               .arg( info.nBrushEntities ) );
        row( QStringLiteral( "Tied geometry" ), Count( info.nTiedObjects, "object", "objects" ) );
        row( QStringLiteral( "Connections" ), info.nBrokenConnections != 0
                                                  ? QStringLiteral( "%1 (%2 without a target)" ).arg( info.nConnections ).arg( info.nBrokenConnections )
                                                  : QString::number( info.nConnections ) );
        row( QStringLiteral( "Materials" ), Count( info.nMaterials, "material", "materials" ) );
        row( QStringLiteral( "Layers" ), QString::number( info.nLayers ) );
        row( QStringLiteral( "Chunk files" ), QString::number( info.nChunkFiles ) );
        row( QStringLiteral( "Extent" ), Extent( info.bounds ) );
        if ( info.nProblems != 0 || info.nBrokenBrushes != 0 ) {
            row( QStringLiteral( "Problems" ), QStringLiteral( "%1 load problems, %2 brushes that could not be rebuilt" ).arg( info.nProblems ).arg( info.nBrokenBrushes ) );
        }
        m_pClasses->clear();
        for ( const map_info_class_t &entry : info.classes ) {
            auto *pItem = new QTreeWidgetItem( m_pClasses );
            pItem->setText( 0, entry.className );
            pItem->setText( 1, QString::number( entry.nCount ) );
            pItem->setTextAlignment( 1, Qt::AlignRight | Qt::AlignVCenter );
        }
        m_pSelectClass->setEnabled( false );
    }

private:
    void SelectCurrentClass()
    {
        const QTreeWidgetItem *pItem = m_pClasses->currentItem();
        if ( pItem != nullptr ) { ( void )MapInfo_SelectClass( m_pWorkspace, pItem->text( 0 ) ); }
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    QLabel *m_pTitle{ nullptr };
    QFormLayout *m_pSummary{ nullptr };
    QTreeWidget *m_pClasses{ nullptr };
    QPushButton *m_pSelectClass{ nullptr };
};

class go_to_dialog_t final : public QDialog {
public:
    go_to_dialog_t( QWidget *pParent, map_workspace_t *pWorkspace ) : QDialog( pParent ), m_pWorkspace( pWorkspace )
    {
        setObjectName( QStringLiteral( "MapGoToDialog" ) );
        setWindowTitle( QStringLiteral( "Go To" ) );
        auto *pLayout = new QVBoxLayout( this );
        m_pInput = new QLineEdit( this );
        m_pInput->setObjectName( QStringLiteral( "MapGoToInput" ) );
        m_pInput->setPlaceholderText( QStringLiteral( "Object ID, entity name, or position x y z" ) );
        m_pInput->setMinimumWidth( 320 );
        m_pResult = new QLabel( this );
        m_pResult->setObjectName( QStringLiteral( "MapGoToResult" ) );
        m_pResult->setProperty( "muted", true );
        auto *pButtons = new QDialogButtonBox( this );
        QPushButton *pGo = pButtons->addButton( QStringLiteral( "Go" ), QDialogButtonBox::AcceptRole );
        pGo->setDefault( true );
        pButtons->addButton( QDialogButtonBox::Close );
        pLayout->addWidget( m_pInput );
        pLayout->addWidget( m_pResult );
        pLayout->addWidget( pButtons );
        QObject::connect( pGo, &QPushButton::clicked, this, [this]() { Go(); } );
        QObject::connect( m_pInput, &QLineEdit::returnPressed, this, [this]() { Go(); } );
        QObject::connect( pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject );
    }

private:
    void Go()
    {
        QString message;
        const map_go_to_status_t status = MapGoTo_Run( m_pWorkspace, m_pInput->text(), &message );
        m_pResult->setText( message );
        m_pResult->setProperty( "error", status == map_go_to_status_t::NOT_FOUND || status == map_go_to_status_t::INVALID );
        m_pResult->style()->unpolish( m_pResult );
        m_pResult->style()->polish( m_pResult );
        if ( status == map_go_to_status_t::OBJECT || status == map_go_to_status_t::POSITION ) { m_pInput->selectAll(); }
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    QLineEdit *m_pInput{ nullptr };
    QLabel *m_pResult{ nullptr };
};

} // namespace

map_info_t MapInfo_Compute( const map_workspace_t *pWorkspace )
{
    map_info_t info{};
    if ( pWorkspace == nullptr ) { return info; }
    const map_wireframe_t &wire = pWorkspace->wire;
    for ( usize i = 0u; i < wire.objects.nCount; ++i ) {
        const map_wire_object_t &object = wire.objects.pData[i];
        switch ( object.kind ) {
            case map_wire_kind_t::BRUSH: ++info.nBrushes; break;
            case map_wire_kind_t::MESH: ++info.nMeshes; break;
            case map_wire_kind_t::PATCH: ++info.nPatches; break;
            case map_wire_kind_t::TERRAIN: ++info.nTerrains; break;
            case map_wire_kind_t::ENTITY: break; // Counted from the entity list below.
        }
        if ( object.kind != map_wire_kind_t::ENTITY && object.owner != 0u ) { ++info.nTiedObjects; }
    }
    QHash<QString, int> classes;
    for ( usize i = 0u; i < wire.entities.nCount; ++i ) {
        const map_wire_entity_t &entity = wire.entities.pData[i];
        ++info.nEntities;
        ++( entity.nOwned != 0u ? info.nBrushEntities : info.nPointEntities );
        ++classes[QString::fromUtf8( entity.className )];
    }
    for ( auto it = classes.cbegin(); it != classes.cend(); ++it ) { info.classes.append( { it.key(), it.value() } ); }
    std::sort( info.classes.begin(), info.classes.end(), []( const map_info_class_t &a, const map_info_class_t &b ) {
        return a.nCount != b.nCount ? a.nCount > b.nCount : a.className < b.className;
    } );
    for ( usize i = 0u; i < wire.connections.nCount; ++i ) {
        ++info.nConnections;
        info.nBrokenConnections += wire.connections.pData[i].status == map_wire_connection_status_t::MISSING_TARGET ? 1 : 0;
    }
    info.nBrokenBrushes = static_cast<int>( wire.nBrokenBrushes );
    info.bounds = wire.bounds;
    if ( const map_document_t *pDocument = pWorkspace->pDocument ) {
        info.nLayers = static_cast<int>( pDocument->layers.nCount );
        info.nMaterials = static_cast<int>( pDocument->materials.entries.nCount );
        info.nChunkFiles = static_cast<int>( pDocument->chunks.nCount );
        info.nProblems = static_cast<int>( pDocument->problems.nCount );
    }
    return info;
}

QString MapInfo_Text( const map_workspace_t *pWorkspace )
{
    const map_info_t info = MapInfo_Compute( pWorkspace );
    QString text = QStringLiteral( "%1\n" ).arg( MapWorkspace_DisplayName( pWorkspace ) );
    text += QStringLiteral( "Brushes %1, meshes %2, patches %3, terrains %4\n" ).arg( info.nBrushes ).arg( info.nMeshes ).arg( info.nPatches ).arg( info.nTerrains );
    text += QStringLiteral( "Entities %1 (%2 point, %3 with geometry), tied geometry %4\n" )
                .arg( info.nEntities )
                .arg( info.nPointEntities )
                .arg( info.nBrushEntities )
                .arg( info.nTiedObjects );
    text += QStringLiteral( "Connections %1 (%2 without a target), materials %3, layers %4, chunk files %5\n" )
                .arg( info.nConnections )
                .arg( info.nBrokenConnections )
                .arg( info.nMaterials )
                .arg( info.nLayers )
                .arg( info.nChunkFiles );
    text += QStringLiteral( "Extent %1\n\n" ).arg( Extent( info.bounds ) );
    for ( const map_info_class_t &entry : info.classes ) { text += QStringLiteral( "%1  %2\n" ).arg( entry.className.leftJustified( 36, QLatin1Char( ' ' ) ) ).arg( entry.nCount ); }
    return text;
}

int MapInfo_SelectClass( map_workspace_t *pWorkspace, const QString &className )
{
    if ( pWorkspace == nullptr ) { return 0; }
    std::vector<u64> ids;
    const QByteArray wanted = className.toUtf8();
    for ( usize i = 0u; i < pWorkspace->wire.entities.nCount; ++i ) {
        const map_wire_entity_t &entity = pWorkspace->wire.entities.pData[i];
        if ( wanted == entity.className ) { ids.push_back( entity.id ); }
    }
    MapWorkspace_SetSelection( pWorkspace, ids.data(), ids.size() );
    return static_cast<int>( ids.size() );
}

QStringList MapInfo_SelectionAssets( const map_workspace_t *pWorkspace )
{
    if ( pWorkspace == nullptr || pWorkspace->pDocument == nullptr ) { return {}; }
    map_document_t &map = *pWorkspace->pDocument;
    QSet<QString> paths;
    QSet<u64> materials;
    const auto addPath = []( void *pContext, string_view_t path, editor_asset_kind_t ) {
        static_cast<QSet<QString> *>( pContext )->insert( QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) ) );
    };
    for ( usize i = 0u; i < EditorSelection_Count( &pWorkspace->selection ); ++i ) {
        const u64 id = EditorSelection_At( &pWorkspace->selection, i );
        // Live geometry first: an edit not yet saved still names its material.
        const geometry::geometry_source_id_t sourceId{ id };
        if ( const auto *pBrush = geometry::GeometryDocument_FindBrush( &map.geometry, sourceId ) ) {
            const auto *pAttributes = geometry::GeometryDocument_FindBrushAttributes( &map.geometry, sourceId );
            for ( usize s = 0u; s < pBrush->sides.nCount && pAttributes != nullptr; ++s ) {
                const usize index = pBrush->sides.pData[s].iAttributeIndex;
                if ( index < pAttributes->records.nCount ) { materials.insert( pAttributes->records.pData[index].material.value ); }
            }
        } else if ( const auto *pMesh = geometry::GeometryDocument_FindMesh( &map.geometry, sourceId ) ) {
            ( void )GenerationPool_ForEach( &pMesh->mesh.faces, [&]( geometry::geometry_mesh_face_handle_t handle, const geometry::mesh_face_record_t & ) noexcept -> bool_t {
                materials.insert( geometry::MeshAttributeStore_GetFace( &pMesh->attributes, handle ).material.value );
                return CY_TRUE;
            } );
        } else if ( const auto *pPatch = geometry::GeometryDocument_FindPatch( &map.geometry, sourceId ) ) {
            materials.insert( pPatch->materialId );
        }
        // The record: entity keys and anything else that names an asset.
        if ( const key_value_t *pRecord = MapDocument_FindObject( &map, id, nullptr ) ) { EditorAssets_VisitReferences( pRecord, addPath, &paths ); }
    }
    for ( const u64 ref : materials ) {
        const string_view_t path = MapMaterials_Path( &map.materials, ref );
        if ( ref != 0u && path.cchLength != 0u ) { paths.insert( QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) ) ); }
    }
    QStringList sorted( paths.cbegin(), paths.cend() );
    std::sort( sorted.begin(), sorted.end() );
    return sorted;
}

map_go_to_status_t MapGoTo_Run( map_workspace_t *pWorkspace, const QString &text, QString *pMessageOut )
{
    const auto say = [pMessageOut]( map_go_to_status_t status, const QString &message ) {
        if ( pMessageOut != nullptr ) { *pMessageOut = message; }
        return status;
    };
    const QString input = text.trimmed();
    if ( pWorkspace == nullptr || input.isEmpty() ) { return say( map_go_to_status_t::INVALID, QStringLiteral( "Type an object ID, an entity name, or x y z." ) ); }

    // Three numbers: a position.
    const QStringList parts = input.split( QRegularExpression( QStringLiteral( "[\\s,;()]+" ) ), Qt::SkipEmptyParts );
    if ( parts.size() == 3 ) {
        bool bOk[3]{};
        const f64 x = parts[0].toDouble( &bOk[0] ), y = parts[1].toDouble( &bOk[1] ), z = parts[2].toDouble( &bOk[2] );
        if ( bOk[0] && bOk[1] && bOk[2] ) {
            map_bounds_t bounds{};
            MapBounds_AddPoint( bounds, math::Vec3d_Make( x - kGoToHalfExtent, y - kGoToHalfExtent, z - kGoToHalfExtent ) );
            MapBounds_AddPoint( bounds, math::Vec3d_Make( x + kGoToHalfExtent, y + kGoToHalfExtent, z + kGoToHalfExtent ) );
            pWorkspace->frameBounds = bounds;
            pWorkspace->frameTarget = map_frame_target_t::ALL;
            MapWorkspace_Notify( pWorkspace, MAP_CHANGE_FRAME );
            return say( map_go_to_status_t::POSITION, QStringLiteral( "Views framed on (%1 %2 %3)." ).arg( x ).arg( y ).arg( z ) );
        }
    }
    // A whole number: an object ID.
    QString idText = input;
    if ( idText.startsWith( QLatin1Char( '#' ) ) ) { idText.remove( 0, 1 ); }
    bool bNumber = false;
    const qulonglong id = idText.toULongLong( &bNumber );
    u64 found = 0u;
    QString what;
    if ( bNumber ) {
        if ( const map_wire_object_t *pObject = MapWireframe_FindObject( pWorkspace->wire, id ) ) {
            found = pObject->id;
            what = QStringLiteral( "object %1" ).arg( found );
        }
    } else {
        // A name: entity name first (exact, then case-insensitive), then class.
        const QByteArray utf8 = input.toUtf8();
        for ( int pass = 0; pass < 3 && found == 0u; ++pass ) {
            for ( usize i = 0u; i < pWorkspace->wire.entities.nCount && found == 0u; ++i ) {
                const map_wire_entity_t &entity = pWorkspace->wire.entities.pData[i];
                const bool bMatch = pass == 0   ? utf8 == entity.name
                                    : pass == 1 ? QString::fromUtf8( entity.name ).compare( input, Qt::CaseInsensitive ) == 0
                                                : QString::fromUtf8( entity.className ).compare( input, Qt::CaseInsensitive ) == 0;
                if ( bMatch ) {
                    found = entity.id;
                    what = entity.name[0] != '\0' ? QStringLiteral( "%1 (%2)" ).arg( QString::fromUtf8( entity.name ), QString::fromUtf8( entity.className ) )
                                                  : QString::fromUtf8( entity.className );
                }
            }
        }
    }
    if ( found == 0u ) { return say( map_go_to_status_t::NOT_FOUND, QStringLiteral( "Nothing is called \"%1\"." ).arg( input ) ); }
    MapWorkspace_Select( pWorkspace, found, MAP_SELECT_REPLACE );
    MapWorkspace_Frame( pWorkspace, CY_TRUE );
    return say( map_go_to_status_t::OBJECT, QStringLiteral( "Selected and framed %1." ).arg( what ) );
}

QDialog *MapInfoDialog_Create( QWidget *pParent, map_workspace_t *pWorkspace )
{
    CY_ASSERT( pWorkspace != nullptr );
    return pWorkspace != nullptr ? new map_info_dialog_t( pParent, pWorkspace ) : nullptr;
}

void MapInfoDialog_Refresh( QDialog *pDialog )
{
    if ( auto *pImpl = dynamic_cast<map_info_dialog_t *>( pDialog ) ) { pImpl->Refresh(); }
}

QDialog *MapGoToDialog_Create( QWidget *pParent, map_workspace_t *pWorkspace )
{
    CY_ASSERT( pWorkspace != nullptr );
    return pWorkspace != nullptr ? new go_to_dialog_t( pParent, pWorkspace ) : nullptr;
}

} // namespace cypher::editor::map
