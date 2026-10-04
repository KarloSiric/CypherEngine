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

#include "CypherMapGui_Check.h"

#include "CypherEditor_AssetCatalog.h"
#include "CypherMap_Document.h"

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
        auto *pFix = new QPushButton( QStringLiteral( "Fix" ), this );
        pFix->setEnabled( false );
        pFix->setToolTip( QStringLiteral( "Automatic fixes arrive with map editing operations" ) );
        auto *pRefresh = new QPushButton( QStringLiteral( "Check Again" ), this );
        auto *pClose = new QPushButton( QStringLiteral( "Close" ), this );
        pButtons->addWidget( m_pGoTo );
        pButtons->addWidget( pFix );
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
        m_pGoTo->setEnabled( iIssue >= 0 && iIssue < m_issues.size() && m_issues[iIssue].id != 0u );
    }

    map_workspace_t *m_pWorkspace{ nullptr };
    map_asset_exists_fn m_pfnAssetExists{ nullptr };
    void *m_pAssetContext{ nullptr };
    QVector<map_issue_t> m_issues{};
    QCheckBox *m_pShow[3]{};
    QLabel *m_pSummary{ nullptr };
    QTreeWidget *m_pList{ nullptr };
    QPushButton *m_pGoTo{ nullptr };
};

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
    const auto add = [&issues]( map_issue_severity_t severity, u64 id, const char *pKind, const QString &message ) {
        issues.append( map_issue_t{ severity, id, QString::fromLatin1( pKind ), message } );
    };

    if ( const map_document_t *pDocument = pWorkspace->pDocument ) {
        for ( usize i = 0u; i < pDocument->problems.nCount; ++i ) {
            const map_problem_t &problem = pDocument->problems.pData[i];
            const QString where = problem.path[0] != '\0' ? QString::fromUtf8( problem.path ) : QStringLiteral( "the root file" );
            const QString member = problem.member[0] != '\0' ? QStringLiteral( " (%1)" ).arg( QString::fromUtf8( problem.member ) ) : QString();
            add( map_issue_severity_t::WARNING, problem.id, "load_problem",
                 QStringLiteral( "Read with a problem: %1 in %2%3" ).arg( QString::fromLatin1( MapDocument_ProblemName( problem.code ) ), where, member ) );
        }
    }

    // Outputs: a target nothing is called, or an output that cannot be read.
    for ( usize i = 0u; i < wire.connections.nCount; ++i ) {
        const map_wire_connection_t &connection = wire.connections.pData[i];
        if ( connection.status == map_wire_connection_status_t::MISSING_TARGET ) {
            add( map_issue_severity_t::WARNING, connection.sourceId, "missing_target",
                 QStringLiteral( "Output %1 of %2 targets \"%3\", which no entity is called" )
                     .arg( QString::fromUtf8( connection.output ), EntityLabel( wire, connection.sourceId ), QString::fromUtf8( connection.target ) ) );
        } else if ( connection.status == map_wire_connection_status_t::INVALID_OUTPUT ) {
            add( map_issue_severity_t::ERROR, connection.sourceId, "invalid_output",
                 QStringLiteral( "Output %1 of %2 cannot be read (output, input, or target missing)" )
                     .arg( connection.iOutput + 1 )
                     .arg( EntityLabel( wire, connection.sourceId ) ) );
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

    // Assets the content folders do not have.
    if ( pfnAssetExists != nullptr && pWorkspace->pDocument != nullptr ) {
        const map_document_t &document = *pWorkspace->pDocument;
        QVector<reference_t> references;
        for ( u64 ref = 1u; ref <= document.materials.entries.nCount; ++ref ) {
            const string_view_t path = MapMaterials_Path( &document.materials, ref );
            if ( path.cchLength != 0u ) { references.append( reference_t{ QString::fromUtf8( path.pData, static_cast<qsizetype>( path.cchLength ) ), editor_asset_kind_t::MATERIAL } ); }
        }
        CollectReferences( SettingsDocument_Root( &document.root ), &references );
        for ( usize i = 0u; i < document.chunks.nCount; ++i ) { CollectReferences( SettingsDocument_Root( &document.chunks.pData[i]->store ), &references ); }
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

} // namespace cypher::editor::map
