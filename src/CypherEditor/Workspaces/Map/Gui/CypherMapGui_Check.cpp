//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Check.cpp
//  Purpose: Implements Check for Problems.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Instances.h"
#include "CypherMapGui_Check.h"
#include "CypherMap_Hierarchy.h"

#include "CypherEditor_AssetCatalog.h"
#include "CypherMap_Document.h"
#include "CypherMap_Edit.h"
#include "CypherMap_EntityOutputs.h"
#include "CypherMap_TopologyEdit.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMapGui_Game.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_MeshSource.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace cypher::editor::map
{

using namespace cypher::common;

namespace
{

constexpr int kIssueRole = Qt::UserRole;

QString EntityLabel( const map_wireframe_t &wire, u64 id )
{
    const map_wire_entity_t *pEntity = MapWireframe_FindEntity( wire, id );
    if ( pEntity == nullptr ) { return QStringLiteral( "object %1" ).arg( id ); }
    if ( pEntity->name[0] != '\0' ) { return QStringLiteral( "%1 (%2)" ).arg( QString::fromUtf8( pEntity->name ), QString::fromUtf8( pEntity->className ) ); }
    return QStringLiteral( "%1 #%2" ).arg( QString::fromUtf8( pEntity->className ) ).arg( id );
}

struct reference_t {
    QString path;
    editor_asset_kind_t kind;
};

void CollectReferences( const key_value_t *pRoot, QVector<reference_t> *pOut )
{
    EditorAssets_VisitReferences(
        pRoot,
        []( void *pContext, string_view_t path, editor_asset_kind_t kind ) {
            static_cast<QVector<reference_t> *>( pContext )->append(
                reference_t{ QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) ), kind } );
        },
        pOut );
}

class check_dialog_t final : public QDialog {
public:
    check_dialog_t( QWidget *pParent, map_workspace_t *pWorkspace, map_asset_exists_fn pfnAssetExists, void *pAssetContext )
        : QDialog( pParent ), m_pWorkspace( pWorkspace ), m_pfnAssetExists( pfnAssetExists ), m_pAssetContext( pAssetContext )
    {
        setObjectName( QStringLiteral( "MapCheckDialog" ) );
        setWindowTitle( QStringLiteral( "Check for Problems" ) );
        resize( 720, 420 );
        auto *pLayout = new QVBoxLayout( this );
        auto *pFilters = new QHBoxLayout();
        for ( int s = 2; s >= 0; --s ) {
            m_pShow[s] = new QCheckBox( s == 2 ? QStringLiteral( "Errors" ) : s == 1 ? QStringLiteral( "Warnings" ) : QStringLiteral( "Information" ), this );
            m_pShow[s]->setChecked( true );
            pFilters->addWidget( m_pShow[s] );
            QObject::connect( m_pShow[s], &QCheckBox::toggled, this, [this]( bool ) { Fill(); } );
        }
        pFilters->addStretch( 1 );
        m_pSummary = new QLabel( this );
        m_pSummary->setProperty( "muted", true );
        pFilters->addWidget( m_pSummary );
        pLayout->addLayout( pFilters );

        auto *pBody = new QHBoxLayout();
        m_pList = new QTreeWidget( this );
        m_pList->setObjectName( QStringLiteral( "MapCheckIssues" ) );
        m_pList->setColumnCount( 2 );
        m_pList->setHeaderLabels( { QStringLiteral( "Problem" ), QStringLiteral( "Object" ) } );
        m_pList->setRootIsDecorated( false );
        m_pList->setAlternatingRowColors( true );
        m_pList->setUniformRowHeights( true );
        m_pList->header()->setSectionResizeMode( 0, QHeaderView::Stretch );
        m_pList->header()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
        m_pList->header()->setStretchLastSection( false );
        pBody->addWidget( m_pList, 1 );
        auto *pButtons = new QVBoxLayout();
        m_pGoTo = new QPushButton( QStringLiteral( "Go to Error" ), this );
        m_pGoTo->setObjectName( QStringLiteral( "MapCheckGoTo" ) );
        m_pGoTo->setToolTip( QStringLiteral( "Select the object and frame it in every view" ) );
        m_pFix = new QPushButton( QStringLiteral( "Fix" ), this );
        m_pFix->setObjectName( QStringLiteral( "MapCheckFix" ) );
        m_pFixAll = new QPushButton( QStringLiteral( "Fix All" ), this );
        m_pFixAll->setObjectName( QStringLiteral( "MapCheckFixAll" ) );
        m_pFixAll->setToolTip( QStringLiteral( "Fix every listed problem that has an automatic fix, in one undo step" ) );
        auto *pRefresh = new QPushButton( QStringLiteral( "Check Again" ), this );
        auto *pClose = new QPushButton( QStringLiteral( "Close" ), this );
        pButtons->addWidget( m_pGoTo );
        pButtons->addWidget( m_pFix );
        pButtons->addWidget( m_pFixAll );
        pButtons->addWidget( pRefresh );
        pButtons->addStretch( 1 );
        pButtons->addWidget( pClose );
        pBody->addLayout( pButtons );
        pLayout->addLayout( pBody, 1 );

        QObject::connect( m_pGoTo, &QPushButton::clicked, this, [this]() {
            if ( const QTreeWidgetItem *pItem = m_pList->currentItem() ) { ( void )GoTo( pItem->data( 0, kIssueRole ).toInt() ); }
        } );
        QObject::connect( m_pList, &QTreeWidget::itemDoubleClicked, this, [this]( QTreeWidgetItem *pItem, int ) { ( void )GoTo( pItem->data( 0, kIssueRole ).toInt() ); } );
        QObject::connect( m_pList, &QTreeWidget::currentItemChanged, this, [this]( QTreeWidgetItem *pItem, QTreeWidgetItem * ) { UpdateGoTo( pItem ); } );
        QObject::connect( m_pFix, &QPushButton::clicked, this, [this]() {
            if ( const QTreeWidgetItem *pItem = m_pList->currentItem() ) { ( void )Fix( pItem->data( 0, kIssueRole ).toInt() ); }
        } );
        QObject::connect( m_pFixAll, &QPushButton::clicked, this, [this]() { ( void )FixAll(); } );
        QObject::connect( pRefresh, &QPushButton::clicked, this, [this]() { ( void )Refresh(); } );
        QObject::connect( pClose, &QPushButton::clicked, this, &QDialog::reject );
        ( void )Refresh();
    }

    int Refresh()
    {
        m_issues = MapCheck_Run( m_pWorkspace, m_pfnAssetExists, m_pAssetContext );
        Fill();
        return static_cast<int>( m_issues.size() );
    }

    int Fix( int iIssue )
    {
        if ( iIssue < 0 || iIssue >= m_issues.size() ) { return 0; }
        const int fixed = MapCheck_Fix( m_pWorkspace, { m_issues[iIssue] } );
        ( void )Refresh();
        return fixed;
    }

    // The issues the filters show, as Hammer's Fix All acts on the list.
    int FixAll()
    {
        QVector<map_issue_t> shown;
        for ( const map_issue_t &issue : m_issues ) { if ( m_pShow[static_cast<int>( issue.severity )]->isChecked() ) { shown.append( issue ); } }
        const int fixed = MapCheck_Fix( m_pWorkspace, shown );
        ( void )Refresh();
        return fixed;
    }

    bool GoTo( int iIssue )
    {
        if ( iIssue < 0 || iIssue >= m_issues.size() || m_issues[iIssue].id == 0u ) { return false; }
        MapWorkspace_Select( m_pWorkspace, m_issues[iIssue].id, MAP_SELECT_REPLACE );
        MapWorkspace_Frame( m_pWorkspace, CY_TRUE );
        return true;
    }

private:
    void Fill()
    {
        m_pList->clear();
        int counts[3]{};
        const gui::editor_style_t &style = m_pWorkspace->pGui->style;
        const QIcon icons[3]{ gui::EditorStyle_Icon( style, "log-info" ), gui::EditorStyle_Icon( style, "log-warning" ), gui::EditorStyle_Icon( style, "log-error" ) };
        for ( int i = 0; i < m_issues.size(); ++i ) {
            const map_issue_t &issue = m_issues[i];
            const int s = static_cast<int>( issue.severity );
            ++counts[s];
            if ( !m_pShow[s]->isChecked() ) { continue; }
            auto *pItem = new QTreeWidgetItem( m_pList );
            pItem->setIcon( 0, icons[s] );
            pItem->setText( 0, issue.message );
            pItem->setText( 1, issue.id != 0u ? QString::number( issue.id ) : QString() );
            pItem->setData( 0, kIssueRole, i );
            pItem->setToolTip( 0, QStringLiteral( "%1: %2" ).arg( QString::fromLatin1( MapCheck_SeverityName( issue.severity ) ), issue.kind ) );
        }
        m_pSummary->setText( m_issues.isEmpty() ? QStringLiteral( "No problems found" )
                                                : QStringLiteral( "%1 errors, %2 warnings, %3 notes" ).arg( counts[2] ).arg( counts[1] ).arg( counts[0] ) );
        if ( m_pList->topLevelItemCount() != 0 ) { m_pList->setCurrentItem( m_pList->topLevelItem( 0 ) ); }
        UpdateGoTo( m_pList->currentItem() );
    }

    void UpdateGoTo( const QTreeWidgetItem *pItem )
    {
        const int iIssue = pItem != nullptr ? pItem->data( 0, kIssueRole ).toInt() : -1;
        const bool valid = iIssue >= 0 && iIssue < m_issues.size();
        m_pGoTo->setEnabled( valid && m_issues[iIssue].id != 0u );
        const QString fix = valid ? MapCheck_FixLabel( m_issues[iIssue] ) : QString();
        m_pFix->setEnabled( !fix.isEmpty() );
        m_pFix->setToolTip( fix.isEmpty() ? QStringLiteral( "This problem has no automatic fix" ) : fix );
        bool any = false;
        for ( const map_issue_t &issue : m_issues ) { any = any || ( m_pShow[static_cast<int>( issue.severity )]->isChecked() && !MapCheck_FixLabel( issue ).isEmpty() ); }
        m_pFixAll->setEnabled( any );
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    map_asset_exists_fn m_pfnAssetExists{ nullptr };
    void *m_pAssetContext{ nullptr };
    QVector<map_issue_t> m_issues{};
    QCheckBox *m_pShow[3]{};
    QLabel *m_pSummary{ nullptr };
    QTreeWidget *m_pList{ nullptr };
    QPushButton *m_pGoTo{ nullptr };
    QPushButton *m_pFix{ nullptr };
    QPushButton *m_pFixAll{ nullptr };
};

// A mesh's first face material: what a repair, which rebuilds the faces,
// gives them all.
QString MeshMaterial( const map_document_t &document, u64 meshId )
{
    namespace geo = geometry;
    const auto *mesh = geo::GeometryDocument_FindMesh( &document.geometry, { meshId } );
    geo::mesh_source_description_t description{};
    QString path;
    if ( mesh != nullptr && geo::MeshSourceDescription_Init( &description, document.pAllocator, { meshId } ) == geo::geometry_status_t::OK &&
         geo::MeshSource_TryDescribe( mesh, &description ) == geo::geometry_status_t::OK && description.faces.nCount != 0u ) {
        const string_view_t text = MapMaterials_Path( &document.materials, description.faces.pData[0].attributes.material.value );
        path = QString::fromUtf8( text.pData, static_cast<qsizetype>( text.cchLength ) );
    }
    geo::MeshSourceDescription_Shutdown( &description );
    return path.isEmpty() ? QStringLiteral( "materials/dev/dev_grid.cymat" ) : path;
}

} // namespace

const char *MapCheck_SeverityName( map_issue_severity_t severity ) noexcept
{
    switch ( severity ) {
        case map_issue_severity_t::INFO: return "Note";
        case map_issue_severity_t::WARNING: return "Warning";
        case map_issue_severity_t::ERROR: return "Error";
    }
    return "Problem";
}

QVector<map_issue_t> MapCheck_Run( const map_workspace_t *pWorkspace, map_asset_exists_fn pfnAssetExists, void *pAssetContext )
{
    QVector<map_issue_t> issues;
    if ( pWorkspace == nullptr ) { return issues; }
    const map_wireframe_t &wire = pWorkspace->wire;
    const auto add = [&issues]( map_issue_severity_t severity, u64 id, const char *pKind, const QString &message, u64 detail = 0u ) {
        issues.append( map_issue_t{ severity, id, QString::fromLatin1( pKind ), message, detail } );
    };

    if ( const map_document_t *pDocument = pWorkspace->pDocument ) {
        for ( usize i = 0u; i < pDocument->problems.nCount; ++i ) {
            const map_problem_t &problem = pDocument->problems.pData[i];
            const QString where = problem.layer[0] != '\0' ? QStringLiteral( "layer %1" ).arg( QString::fromUtf8( problem.layer ) ) : QStringLiteral( "the map file" );
            const QString member = problem.member[0] != '\0' ? QStringLiteral( " (%1)" ).arg( QString::fromUtf8( problem.member ) ) : QString();
            add( map_issue_severity_t::WARNING, problem.id, "load_problem",
                 QStringLiteral( "Read with a problem: %1 in %2%3" ).arg( QString::fromLatin1( MapDocument_ProblemName( problem.code ) ), where, member ) );
        }
    }

    // Prefab instances whose prefab file cannot be read show only a marker
    // and are left out of a compile.
    for ( usize i = 0u; i < wire.objects.nCount; ++i ) {
        map_instance_info_t instance{};
        if ( wire.objects.pData[i].kind != map_wire_kind_t::PREFAB || !MapInstances_Info( pWorkspace, wire.objects.pData[i].id, &instance ) || instance.bLoaded ) { continue; }
        add( map_issue_severity_t::ERROR, wire.objects.pData[i].id, "missing_prefab",
             QStringLiteral( "Prefab instance #%1: %2" ).arg( wire.objects.pData[i].id ).arg( instance.problem ) );
    }

    // Attachment: an entity hanging from one that is gone (CYMAP `parent`).
    if ( pWorkspace->pDocument != nullptr ) {
        for ( const u64 orphan : MapHierarchy_Orphans( pWorkspace->pDocument ) ) {
            add( map_issue_severity_t::WARNING, orphan, "missing_parent",
                 QStringLiteral( "%1 is attached to #%2, which is not an entity in the map" ).arg( EntityLabel( wire, orphan ) ).arg( MapHierarchy_Parent( pWorkspace->pDocument, orphan ) ) );
        }
    }

    // Outputs: a target nothing is called, or an output that cannot be read.
    for ( usize i = 0u; i < wire.connections.nCount; ++i ) {
        const map_wire_connection_t &connection = wire.connections.pData[i];
        if ( connection.status == map_wire_connection_status_t::MISSING_TARGET ) {
            add( map_issue_severity_t::WARNING, connection.sourceId, "missing_target",
                 QStringLiteral( "Output %1 of %2 targets \"%3\", which no entity is called" )
                     .arg( QString::fromUtf8( connection.output ), EntityLabel( wire, connection.sourceId ), QString::fromUtf8( connection.target ) ),
                 connection.iOutput );
        } else if ( connection.status == map_wire_connection_status_t::INVALID_OUTPUT ) {
            add( map_issue_severity_t::ERROR, connection.sourceId, "invalid_output",
                 QStringLiteral( "Output %1 of %2 cannot be read (output, input, or target missing)" )
                     .arg( connection.iOutput + 1 )
                     .arg( EntityLabel( wire, connection.sourceId ) ),
                 connection.iOutput );
        }
    }

    // Entities: a class, a player start, and nothing stacked on its twin.
    bool bPlayerStart = false;
    QHash<QString, u64> placed;
    for ( usize i = 0u; i < wire.entities.nCount; ++i ) {
        const map_wire_entity_t &entity = wire.entities.pData[i];
        const QString className = QString::fromUtf8( entity.className );
        if ( className.isEmpty() ) {
            add( map_issue_severity_t::ERROR, entity.id, "no_class", QStringLiteral( "Entity %1 has no class" ).arg( entity.id ) );
            continue;
        }
        bPlayerStart = bPlayerStart || className.startsWith( QStringLiteral( "info_player_start" ) );
        if ( entity.bHasOrigin && entity.nOwned == 0u ) {
            const QString key = QStringLiteral( "%1@%2,%3,%4" ).arg( className ).arg( entity.origin.x, 0, 'f', 3 ).arg( entity.origin.y, 0, 'f', 3 ).arg( entity.origin.z, 0, 'f', 3 );
            if ( const auto it = placed.constFind( key ); it != placed.constEnd() ) {
                add( map_issue_severity_t::WARNING, entity.id, "stacked_entity",
                     QStringLiteral( "%1 stands exactly on %2 (same class and position)" ).arg( EntityLabel( wire, entity.id ), EntityLabel( wire, it.value() ) ) );
            } else {
                placed.insert( key, entity.id );
            }
        }
    }
    if ( !bPlayerStart ) { add( map_issue_severity_t::WARNING, 0u, "no_player_start", QStringLiteral( "The map has no player start (info_player_start)" ) ); }

    // The game profile: classes that exist and can be placed, values their
    // definitions accept, and outputs and inputs the classes have.
    if ( const map_game_profile_t *pProfile = MapGame_Profile( pWorkspace ); pProfile != nullptr && pWorkspace->pDocument != nullptr ) {
        for ( usize i = 0u; i < wire.entities.nCount; ++i ) {
            const map_wire_entity_t &entity = wire.entities.pData[i];
            if ( entity.className[0] == '\0' ) { continue; }
            const map_game_class_t *pClass = MapGame_FindClass( *pProfile, StringView_FromCString( entity.className ) );
            const QString label = EntityLabel( wire, entity.id );
            if ( pClass == nullptr ) {
                add( map_issue_severity_t::WARNING, entity.id, "unknown_class", QStringLiteral( "%1: class %2 is not in the game profile" ).arg( label, QString::fromUtf8( entity.className ) ) );
                continue;
            }
            if ( pClass->kind == map_game_kind_t::ABSTRACT ) {
                add( map_issue_severity_t::ERROR, entity.id, "abstract_class", QStringLiteral( "%1: class %2 is only a base for other classes" ).arg( label, QString::fromUtf8( entity.className ) ) );
                continue;
            }
            if ( pClass->kind == map_game_kind_t::SOLID && entity.nOwned == 0u ) {
                add( map_issue_severity_t::WARNING, entity.id, "solid_without_geometry", QStringLiteral( "%1 is a solid class with no brushes or meshes" ).arg( label ) );
            } else if ( pClass->kind == map_game_kind_t::POINT && entity.nOwned != 0u ) {
                add( map_issue_severity_t::WARNING, entity.id, "point_with_geometry", QStringLiteral( "%1 is a point class but owns geometry" ).arg( label ) );
            }
            const key_value_t *pProperties = KeyValue_Find( MapEntityEdit_FindEntity( pWorkspace->pDocument, entity.id ), StringView_FromCString( "properties" ) );
            usize nMissing = 0u;
            for ( usize k = 0u; k < pClass->properties.size(); ++k ) {
                const map_game_property_t &property = pClass->properties[k];
                const key_value_t *pValue = KeyValue_Find( pProperties, { property.key.data(), property.key.size() } );
                string_view_t text{};
                const bool bEmpty = pValue == nullptr || ( KeyValue_GetString( pValue, &text ) && text.cchLength == 0u );
                const QString name = QString::fromStdString( property.label.empty() ? property.key : property.label );
                if ( property.bRequired && bEmpty ) {
                    add( map_issue_severity_t::WARNING, entity.id, "required_property", QStringLiteral( "%1 needs %2" ).arg( label, name ), k );
                    continue;
                }
                if ( pValue == nullptr ) { nMissing += property.pDefault != nullptr ? 1u : 0u; continue; }
                std::string why;
                if ( !MapGame_CheckValue( property, pValue, &why ) ) {
                    add( map_issue_severity_t::WARNING, entity.id, "invalid_property", QStringLiteral( "%1: %2 %3" ).arg( label, name, QString::fromStdString( why ) ), k );
                }
            }
            if ( nMissing != 0u ) {
                add( map_issue_severity_t::INFO, entity.id, "missing_defaults",
                     QStringLiteral( "%1 has no value for %2 of its class's properties" ).arg( label ).arg( nMissing ) );
            }
        }
        QSet<QString> seen;
        for ( usize i = 0u; i < wire.connections.nCount; ++i ) {
            const map_wire_connection_t &connection = wire.connections.pData[i];
            if ( connection.status == map_wire_connection_status_t::INVALID_OUTPUT ) { continue; } // Reported above.
            const QString key = QStringLiteral( "%1/%2" ).arg( connection.sourceId ).arg( connection.iOutput );
            const auto *pSource = MapWireframe_FindEntity( wire, connection.sourceId );
            const auto *pSourceClass = pSource != nullptr ? MapGame_FindClass( *pProfile, StringView_FromCString( pSource->className ) ) : nullptr;
            const auto has = []( const std::vector<map_game_io_t> &list, const char *pName ) {
                return std::any_of( list.begin(), list.end(), [&]( const map_game_io_t &io ) { return io.name == pName; } );
            };
            if ( pSourceClass != nullptr && !has( pSourceClass->outputs, connection.output ) && !seen.contains( key + QStringLiteral( "/out" ) ) ) {
                seen.insert( key + QStringLiteral( "/out" ) );
                add( map_issue_severity_t::WARNING, connection.sourceId, "unknown_output",
                     QStringLiteral( "%1 fires %2, which class %3 does not have" ).arg( EntityLabel( wire, connection.sourceId ), QString::fromUtf8( connection.output ), QString::fromUtf8( pSource->className ) ),
                     connection.iOutput );
            }
            const auto *pTarget = connection.status == map_wire_connection_status_t::RESOLVED ? MapWireframe_FindEntity( wire, connection.targetId ) : nullptr;
            const auto *pTargetClass = pTarget != nullptr ? MapGame_FindClass( *pProfile, StringView_FromCString( pTarget->className ) ) : nullptr;
            if ( pTargetClass != nullptr && !has( pTargetClass->inputs, connection.input ) && !seen.contains( key + QStringLiteral( "/in" ) ) ) {
                seen.insert( key + QStringLiteral( "/in" ) );
                add( map_issue_severity_t::WARNING, connection.sourceId, "unknown_input",
                     QStringLiteral( "%1 sends %2 to %3, which class %4 does not take" )
                         .arg( EntityLabel( wire, connection.sourceId ), QString::fromUtf8( connection.input ), EntityLabel( wire, connection.targetId ), QString::fromUtf8( pTarget->className ) ),
                     connection.iOutput );
            }
        }
    }

    // Geometry: rebuildable, and somewhere near the world.
    if ( wire.nBrokenBrushes != 0u ) {
        add( map_issue_severity_t::ERROR, 0u, "broken_brush", QStringLiteral( "%1 brushes could not be rebuilt from their planes" ).arg( wire.nBrokenBrushes ) );
    }
    for ( usize i = 0u; i < wire.objects.nCount; ++i ) {
        const map_wire_object_t &object = wire.objects.pData[i];
        if ( !object.bounds.bHas ) { continue; }
        const math::vec3d_t &a = object.bounds.box.minimum;
        const math::vec3d_t &b = object.bounds.box.maximum;
        const f64 reach = std::max( { std::fabs( a.x ), std::fabs( a.y ), std::fabs( a.z ), std::fabs( b.x ), std::fabs( b.y ), std::fabs( b.z ) } );
        if ( reach > MAP_CHECK_WORLD_EXTENT ) {
            add( map_issue_severity_t::WARNING, object.id, "far_away",
                 QStringLiteral( "Object %1 lies %2 units from the origin, outside the world" ).arg( object.id ).arg( reach, 0, 'f', 0 ) );
        }
    }

    // Meshes: what Repair Mesh would fix (open borders are allowed).
    if ( const map_document_t *pDocument = pWorkspace->pDocument ) {
        for ( usize i = 0u; i < pDocument->geometry.meshes.nCount; ++i ) {
            const u64 meshId = pDocument->geometry.meshes.pData[i]->sourceId.value;
            map_repair_report_t found{};
            if ( MapTopologyEdit_Inspect( pDocument, meshId, &found ) != map_status_t::OK ) { continue; }
            QStringList parts;
            const auto part = [&parts]( u32 count, const char *pOne, const char *pMany ) {
                if ( count != 0u ) { parts << QStringLiteral( "%1 %2" ).arg( count ).arg( QString::fromLatin1( count == 1u ? pOne : pMany ) ); }
            };
            part( found.welded, "vertex on top of another but not joined", "vertices on top of others but not joined" );
            part( found.degenerate, "degenerate face", "degenerate faces" );
            part( found.duplicates, "duplicate face", "duplicate faces" );
            part( found.flipped, "face wound against its neighbours", "faces wound against their neighbours" );
            part( found.nonManifoldEdges, "edge shared by more than two faces", "edges shared by more than two faces" );
            if ( !parts.isEmpty() ) { add( map_issue_severity_t::WARNING, meshId, "mesh_problems", QStringLiteral( "Mesh %1 has %2" ).arg( meshId ).arg( parts.join( QStringLiteral( ", " ) ) ) ); }
        }
    }

    // Assets the content folders do not have.
    if ( pfnAssetExists != nullptr && pWorkspace->pDocument != nullptr ) {
        const map_document_t &document = *pWorkspace->pDocument;
        QVector<reference_t> references;
        for ( u64 ref = 1u; ref <= document.materials.entries.nCount; ++ref ) {
            const string_view_t path = MapMaterials_Path( &document.materials, ref );
            if ( path.cchLength != 0u ) { references.append( reference_t{ QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) ), editor_asset_kind_t::MATERIAL } ); }
        }
        CollectReferences( SettingsDocument_Root( &document.root ), &references );
        for ( usize i = 0u; i < document.layerStores.nCount; ++i ) { CollectReferences( SettingsDocument_Root( &document.layerStores.pData[i]->store ), &references ); }
        QSet<QString> reported;
        for ( const reference_t &reference : references ) {
            if ( reported.contains( reference.path ) || pfnAssetExists( pAssetContext, reference.path ) ) { continue; }
            reported.insert( reference.path );
            add( map_issue_severity_t::WARNING, 0u, "missing_asset",
                 QStringLiteral( "%1 not found in the content folders: %2" ).arg( QString::fromLatin1( EditorAssets_KindLabel( reference.kind ) ), reference.path ) );
        }
    }

    std::stable_sort( issues.begin(), issues.end(), []( const map_issue_t &a, const map_issue_t &b ) {
        if ( a.severity != b.severity ) { return a.severity > b.severity; }
        return a.id < b.id;
    } );
    return issues;
}

QString MapCheck_FixLabel( const map_issue_t &issue )
{
    if ( issue.id == 0u ) { return QString(); }
    if ( issue.kind == QLatin1String( "stacked_entity" ) ) { return QStringLiteral( "Delete this duplicate" ); }
    if ( issue.kind == QLatin1String( "no_class" ) ) { return QStringLiteral( "Delete the entity" ); }
    if ( issue.kind == QLatin1String( "far_away" ) ) { return QStringLiteral( "Delete the object" ); }
    if ( issue.kind == QLatin1String( "missing_target" ) || issue.kind == QLatin1String( "invalid_output" ) ) { return QStringLiteral( "Remove the output" ); }
    if ( issue.kind == QLatin1String( "mesh_problems" ) ) { return QStringLiteral( "Repair the mesh: join, rewind and drop bad faces (faces take its first material; UVs are re-projected)" ); }
    if ( issue.kind == QLatin1String( "missing_defaults" ) ) { return QStringLiteral( "Add the class's defaults" ); }
    if ( issue.kind == QLatin1String( "invalid_property" ) ) { return QStringLiteral( "Reset to the class's default" ); }
    if ( issue.kind == QLatin1String( "missing_parent" ) ) { return QStringLiteral( "Detach it (clear its parent)" ); }
    return QString();
}

int MapCheck_Fix( map_workspace_t *pWorkspace, const QVector<map_issue_t> &issues )
{
    if ( pWorkspace == nullptr || pWorkspace->pDocument == nullptr ) { return 0; }
    struct plan_t {
        std::vector<u64> deletes;
        std::vector<std::pair<u64, std::vector<usize>>> outputs; // Entity, output indices.
        std::vector<std::pair<u64, QByteArray>> repairs;         // Mesh, its material.
        std::vector<u64> defaults;                               // Entities that take their class's missing defaults.
        std::vector<std::pair<u64, std::string>> resets;         // Entity, property back to its default.
        std::vector<u64> detaches;                               // Entities whose parent is gone.
        const map_workspace_t *pWorkspace{ nullptr };
        f64 weld{ 0.0 };
        int fixed{ 0 };
    } plan;
    plan.pWorkspace = pWorkspace;
    plan.weld = pWorkspace->pDocument->geometryPolicy.numerical.fAbsoluteDistanceTolerance;
    for ( const map_issue_t &issue : issues ) {
        if ( MapCheck_FixLabel( issue ).isEmpty() ) { continue; }
        ++plan.fixed;
        if ( issue.kind == QLatin1String( "missing_target" ) || issue.kind == QLatin1String( "invalid_output" ) ) {
            auto it = std::find_if( plan.outputs.begin(), plan.outputs.end(), [&]( const auto &entry ) { return entry.first == issue.id; } );
            if ( it == plan.outputs.end() ) { plan.outputs.push_back( { issue.id, {} } ); it = plan.outputs.end() - 1; }
            it->second.push_back( static_cast<usize>( issue.detail ) );
        } else if ( issue.kind == QLatin1String( "mesh_problems" ) ) {
            plan.repairs.push_back( { issue.id, MeshMaterial( *pWorkspace->pDocument, issue.id ).toUtf8() } );
        } else if ( issue.kind == QLatin1String( "missing_defaults" ) ) {
            plan.defaults.push_back( issue.id );
        } else if ( issue.kind == QLatin1String( "missing_parent" ) ) {
            plan.detaches.push_back( issue.id );
        } else if ( issue.kind == QLatin1String( "invalid_property" ) ) {
            const auto *pEntity = MapWireframe_FindEntity( pWorkspace->wire, issue.id );
            const auto *pClass = pEntity != nullptr ? MapGame_Class( pWorkspace, StringView_FromCString( pEntity->className ) ) : nullptr;
            if ( pClass != nullptr && issue.detail < pClass->properties.size() ) { plan.resets.push_back( { issue.id, pClass->properties[issue.detail].key } ); }
        } else if ( std::find( plan.deletes.begin(), plan.deletes.end(), issue.id ) == plan.deletes.end() ) {
            plan.deletes.push_back( issue.id );
        }
    }
    if ( plan.fixed == 0 ) { return 0; }
    const bool done = MapWorkspace_ApplyMetadataEdit( pWorkspace, "Fix Problems", []( void *pContext, map_document_t *copy, vector_t<u64> *, bool *pChanged ) noexcept {
        auto &p = *static_cast<plan_t *>( pContext );
        const auto deleted = [&p]( u64 id ) { return std::find( p.deletes.begin(), p.deletes.end(), id ) != p.deletes.end(); };
        for ( auto &[entity, indices] : p.outputs ) {
            if ( deleted( entity ) ) { continue; }
            std::vector<map_output_t> outputs;
            auto status = MapEntityOutputs_Read( copy, entity, outputs );
            if ( status != map_status_t::OK ) { return status; }
            std::sort( indices.begin(), indices.end() );
            indices.erase( std::unique( indices.begin(), indices.end() ), indices.end() );
            for ( auto it = indices.rbegin(); it != indices.rend(); ++it ) { if ( *it < outputs.size() ) { outputs.erase( outputs.begin() + static_cast<std::ptrdiff_t>( *it ) ); } }
            bool changed = false;
            status = MapEntityOutputs_Write( copy, entity, outputs, &changed );
            if ( status != map_status_t::OK ) { return status; }
            *pChanged = *pChanged || changed;
        }
        for ( const auto &[mesh, material] : p.repairs ) {
            if ( deleted( mesh ) ) { continue; }
            const u64 revision = copy->geometry.revision;
            const auto status = MapTopologyEdit_Repair( copy, mesh, p.weld, StringView_FromCString( material.constData() ), 128.0, nullptr, false );
            if ( status != map_status_t::OK ) { return status; }
            *pChanged = *pChanged || copy->geometry.revision != revision;
        }
        // A bad value goes back to the default (or goes, without one); then
        // missing defaults are written, the reset ones among them.
        for ( const auto &[entity, key] : p.resets ) {
            if ( deleted( entity ) ) { continue; }
            const auto *pEntity = MapWireframe_FindEntity( p.pWorkspace->wire, entity );
            const auto *pClass = pEntity != nullptr ? MapGame_Class( p.pWorkspace, StringView_FromCString( pEntity->className ) ) : nullptr;
            const auto *pProperty = pClass != nullptr ? MapGame_FindProperty( *pClass, { key.data(), key.size() } ) : nullptr;
            bool changed = false;
            const auto status = pProperty != nullptr && pProperty->pDefault != nullptr
                ? MapEntityEdit_SetProperty( copy, { &entity, 1 }, { key.data(), key.size() }, pProperty->pDefault, &changed )
                : MapEntityEdit_RemoveProperty( copy, { &entity, 1 }, { key.data(), key.size() }, &changed );
            if ( status != map_status_t::OK ) { return status; }
            *pChanged = *pChanged || changed;
        }
        std::vector<u64> detach;
        for ( u64 entity : p.detaches ) { if ( !deleted( entity ) ) { detach.push_back( entity ); } }
        if ( !detach.empty() ) {
            const auto status = MapHierarchy_SetParent( copy, { detach.data(), detach.size() }, 0 );
            if ( status != map_status_t::OK ) { return status; }
            *pChanged = true;
        }
        for ( u64 entity : p.defaults ) {
            if ( deleted( entity ) ) { continue; }
            const auto *pEntity = MapWireframe_FindEntity( p.pWorkspace->wire, entity );
            if ( pEntity == nullptr ) { continue; }
            const auto status = MapGame_WriteDefaults( p.pWorkspace, copy, entity, StringView_FromCString( pEntity->className ) );
            if ( status != map_status_t::OK ) { return status; }
            *pChanged = true;
        }
        if ( !p.deletes.empty() ) {
            const auto status = MapEdit_Delete( copy, { p.deletes.data(), p.deletes.size() } );
            if ( status != map_status_t::OK ) { return status; }
            *pChanged = true;
        }
        return map_status_t::OK;
    }, &plan );
    return done ? plan.fixed : 0;
}

QDialog *MapCheckDialog_Create( QWidget *pParent, map_workspace_t *pWorkspace, map_asset_exists_fn pfnAssetExists, void *pAssetContext )
{
    CY_ASSERT( pWorkspace != nullptr );
    return pWorkspace != nullptr ? new check_dialog_t( pParent, pWorkspace, pfnAssetExists, pAssetContext ) : nullptr;
}

int MapCheckDialog_Refresh( QDialog *pDialog )
{
    auto *pImpl = dynamic_cast<check_dialog_t *>( pDialog );
    return pImpl != nullptr ? pImpl->Refresh() : 0;
}

bool MapCheckDialog_GoTo( QDialog *pDialog, int iIssue )
{
    auto *pImpl = dynamic_cast<check_dialog_t *>( pDialog );
    return pImpl != nullptr && pImpl->GoTo( iIssue );
}

int MapCheckDialog_Fix( QDialog *pDialog, int iIssue )
{
    auto *pImpl = dynamic_cast<check_dialog_t *>( pDialog );
    return pImpl != nullptr ? pImpl->Fix( iIssue ) : 0;
}

int MapCheckDialog_FixAll( QDialog *pDialog )
{
    auto *pImpl = dynamic_cast<check_dialog_t *>( pDialog );
    return pImpl != nullptr ? pImpl->FixAll() : 0;
}

} // namespace cypher::editor::map
