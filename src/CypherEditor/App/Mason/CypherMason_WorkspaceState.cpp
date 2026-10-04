// CypherEngine Source Code. Copyright (c) 2026 Karlo Siric. All rights reserved.
#include "CypherMason_WorkspaceState.h"
#include "CypherMapGui_Views.h"
#include "DockAreaWidget.h"
#include "DockManager.h"
#include "DockWidget.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <utility>

namespace cypher::mason
{
using namespace cypher::editor::map;
namespace
{
constexpr int kVersion = 1;
constexpr qint64 kMaximumBytes = 4 * 1024 * 1024;

QByteArray Decode( const QJsonObject &object, const char *key )
{
    const QJsonValue value = object.value( QLatin1String( key ) );
    if ( !value.isString() ) { return {}; }
    const QByteArray encoded = value.toString().toLatin1();
    const QByteArray bytes = QByteArray::fromBase64( encoded );
    return bytes.toBase64() == encoded ? bytes : QByteArray{};
}

// ADS's own test pass skips missing dock IDs and does not reject every XML
// parser error. Do not give it a partial/foreign tree: it unassigns every dock
// not visited by the restore, even when it ultimately reports success.
struct dock_validation_t {
    QSet<QString> known;
    QSet<QString> seen;
    QString central;
};

bool Integer( QStringView value, int minimum, int maximum, int &out )
{
    bool ok = false;
    out = value.toInt( &ok );
    return ok && out >= minimum && out <= maximum;
}

bool ValidateDockNode( QXmlStreamReader &xml, dock_validation_t &validation, int depth )
{
    if ( depth > 64 ) { return false; }
    const QString tag = xml.name().toString();
    const auto attributes = xml.attributes();
    if ( tag == QStringLiteral( "Widget" ) ) {
        const QString name = attributes.value( QLatin1String( "Name" ) ).toString();
        int closed = 0;
        if ( !validation.known.contains( name ) || validation.seen.contains( name ) ||
             !Integer( attributes.value( QLatin1String( "Closed" ) ), 0, 1, closed ) ||
             ( name == validation.central && closed != 0 ) ) { return false; }
        validation.seen.insert( name );
        return xml.readElementText( QXmlStreamReader::ErrorOnUnexpectedElement ).trimmed().isEmpty() && !xml.hasError();
    }
    const bool splitter = tag == QStringLiteral( "Splitter" );
    const bool area = tag == QStringLiteral( "Area" );
    const bool sidebar = tag == QStringLiteral( "SideBar" );
    if ( !splitter && !area && !sidebar ) { return false; }
    int expected = 0;
    if ( !Integer( attributes.value( splitter ? QLatin1String( "Count" ) : QLatin1String( "Tabs" ) ),
                   0, validation.known.size() * 2 + 1, expected ) ) { return false; }
    if ( splitter ) {
        const auto orientation = attributes.value( QLatin1String( "Orientation" ) );
        if ( orientation != QLatin1String( "|" ) && orientation != QLatin1String( "-" ) ) { return false; }
    }
    if ( sidebar ) {
        int location = 0;
        if ( !ads::CDockManager::testAutoHideConfigFlag( ads::CDockManager::AutoHideFeatureEnabled ) ||
             !Integer( attributes.value( QLatin1String( "Area" ) ), 0, 3, location ) ) { return false; }
    }
    int children = 0;
    bool sizesRead = false;
    QSet<QString> localNames;
    while ( xml.readNextStartElement() ) {
        if ( splitter && xml.name() == QLatin1String( "Sizes" ) ) {
            if ( sizesRead ) { return false; }
            sizesRead = true;
            const QString text = xml.readElementText( QXmlStreamReader::ErrorOnUnexpectedElement ).trimmed();
            const QStringList sizes = text.split( QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
            if ( sizes.size() != expected ) { return false; }
            for ( const QString &size : sizes ) {
                int pixels = 0;
                if ( !Integer( QStringView( size ), 0, 1000000, pixels ) ) { return false; }
            }
            continue;
        }
        if ( splitter ) {
            if ( sizesRead || ( xml.name() != QLatin1String( "Splitter" ) && xml.name() != QLatin1String( "Area" ) ) ) { return false; }
        } else {
            if ( xml.name() != QLatin1String( "Widget" ) ) { return false; }
            localNames.insert( xml.attributes().value( QLatin1String( "Name" ) ).toString() );
            if ( sidebar ) {
                int pixels = 0;
                if ( !Integer( xml.attributes().value( QLatin1String( "Size" ) ), 0, 1000000, pixels ) ) { return false; }
            }
        }
        if ( !ValidateDockNode( xml, validation, depth + 1 ) ) { return false; }
        ++children;
    }
    const QString current = attributes.value( QLatin1String( "Current" ) ).toString();
    return !xml.hasError() && children == expected && ( !splitter || sizesRead ) &&
        ( !area || current.isEmpty() || localNames.contains( current ) );
}

bool ValidateDocks( const QByteArray &state, ads::CDockManager *pDocks, bool bAllowLegacyAssets = false,
                    bool *pHasLegacyAssets = nullptr )
{
    if ( pHasLegacyAssets != nullptr ) { *pHasLegacyAssets = false; }
    // Persist raw XML inside the base64 envelope. Never decompress bytes from
    // disk: qUncompress has no maximum-output bound for malformed input.
    if ( !state.startsWith( "<?xml" ) || state.size() > kMaximumBytes || state.contains( "<!DOCTYPE" ) ) { return false; }
    dock_validation_t validation;
    const auto widgets = pDocks->dockWidgetsMap();
    for ( auto it = widgets.cbegin(); it != widgets.cend(); ++it ) { validation.known.insert( it.key() ); }
    const QSet<QString> required = validation.known;
    // The retired dock is the only permitted unknown ID. Migration removes it
    // after validation, including its splitter slot when it was alone. All
    // live IDs must still be present exactly once before restore mutates UI.
    const QString legacyAssets = QStringLiteral( "assets" );
    if ( bAllowLegacyAssets ) { validation.known.insert( legacyAssets ); }
    if ( pDocks->centralWidget() != nullptr ) { validation.central = pDocks->centralWidget()->objectName(); }
    QXmlStreamReader xml( state );
    if ( !xml.readNextStartElement() || xml.name() != QLatin1String( "QtAdvancedDockingSystem" ) ) { return false; }
    const auto attributes = xml.attributes();
    int version = 0, userVersion = 0, expected = 0;
    if ( !Integer( attributes.value( QLatin1String( "Version" ) ), 1, 1, version ) ||
         !Integer( attributes.value( QLatin1String( "UserVersion" ) ), kVersion, kVersion, userVersion ) ||
         !Integer( attributes.value( QLatin1String( "Containers" ) ), 1, validation.known.size() + 1, expected ) ||
         attributes.value( QLatin1String( "CentralWidget" ) ) != validation.central ) { return false; }
    int containers = 0;
    while ( xml.readNextStartElement() ) {
        int floating = 0;
        if ( xml.name() != QLatin1String( "Container" ) ||
             !Integer( xml.attributes().value( QLatin1String( "Floating" ) ), 0, 1, floating ) ||
             floating != ( containers == 0 ? 0 : 1 ) ) { return false; }
        if ( floating != 0 ) {
            if ( !xml.readNextStartElement() || xml.name() != QLatin1String( "Geometry" ) ) { return false; }
            const QByteArray encoded = xml.readElementText( QXmlStreamReader::ErrorOnUnexpectedElement ).toLatin1();
            const QByteArray geometry = QByteArray::fromHex( encoded );
            QWidget probe;
            if ( geometry.toHex( ' ' ) != encoded || !probe.restoreGeometry( geometry ) ) { return false; }
        }
        int roots = 0;
        while ( xml.readNextStartElement() ) {
            if ( xml.name() != QLatin1String( "SideBar" ) ) {
                if ( ( xml.name() != QLatin1String( "Splitter" ) && xml.name() != QLatin1String( "Area" ) ) ||
                     ++roots != 1 ) { return false; }
            }
            if ( !ValidateDockNode( xml, validation, 1 ) ) { return false; }
        }
        if ( roots != 1 ) { return false; }
        ++containers;
    }
    // Consume to EndDocument so trailing garbage or an unfinished root is
    // rejected before ADS can hide or detach any live panel.
    while ( !xml.atEnd() ) { xml.readNext(); }
    const bool hasLegacyAssets = bAllowLegacyAssets && !required.contains( legacyAssets ) && validation.seen.contains( legacyAssets );
    if ( hasLegacyAssets ) { validation.seen.remove( legacyAssets ); }
    const bool valid = !xml.hasError() && containers == expected && validation.seen == required;
    if ( valid && pHasLegacyAssets != nullptr ) { *pHasLegacyAssets = hasLegacyAssets; }
    return valid;
}

// A small staging tree keeps each splitter child paired with its saved size.
// Letting ADS skip a missing widget would drop the child but retain its size,
// shifting subsequent live siblings onto the wrong saved proportions.
struct dock_node_t {
    QString tag;
    QXmlStreamAttributes attributes;
    QString text;
    QList<dock_node_t> children;
};

bool ReadDockTree( QXmlStreamReader &xml, dock_node_t &node, int depth )
{
    if ( depth > 66 ) { return false; } // Validated root/container + 64 node levels.
    node.tag = xml.name().toString();
    node.attributes = xml.attributes();
    while ( !xml.atEnd() ) {
        const auto token = xml.readNext();
        if ( token == QXmlStreamReader::StartElement ) {
            dock_node_t child;
            if ( !ReadDockTree( xml, child, depth + 1 ) ) { return false; }
            node.children.append( std::move( child ) );
        } else if ( token == QXmlStreamReader::Characters ) {
            node.text += xml.text();
        } else if ( token == QXmlStreamReader::EndElement ) {
            return !xml.hasError();
        }
    }
    return false;
}

void SetDockAttribute( dock_node_t &node, const char *key, const QString &value )
{
    for ( auto &attribute : node.attributes ) {
        if ( attribute.name() == QLatin1String( key ) ) {
            attribute = QXmlStreamAttribute( QLatin1String( key ), value );
            return;
        }
    }
    node.attributes.append( QXmlStreamAttribute( QLatin1String( key ), value ) );
}

bool RemoveLegacyAssetNodes( dock_node_t &node )
{
    if ( node.tag == QLatin1String( "Widget" ) ) {
        return node.attributes.value( QLatin1String( "Name" ) ) != QLatin1String( "assets" );
    }
    if ( node.tag == QLatin1String( "Area" ) || node.tag == QLatin1String( "SideBar" ) ) {
        for ( auto it = node.children.begin(); it != node.children.end(); ) {
            if ( !RemoveLegacyAssetNodes( *it ) ) { it = node.children.erase( it ); }
            else { ++it; }
        }
        if ( node.children.isEmpty() ) { return false; }
        SetDockAttribute( node, "Tabs", QString::number( node.children.size() ) );
        if ( node.attributes.value( QLatin1String( "Current" ) ) == QLatin1String( "assets" ) ) {
            const dock_node_t *current = &node.children.front();
            for ( const auto &child : node.children ) {
                if ( child.attributes.value( QLatin1String( "Closed" ) ) == QLatin1String( "0" ) ) { current = &child; break; }
            }
            SetDockAttribute( node, "Current", current->attributes.value( QLatin1String( "Name" ) ).toString() );
        }
        return true;
    }
    if ( node.tag == QLatin1String( "Splitter" ) ) {
        const QStringList savedSizes = node.children.back().text.trimmed().split(
            QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
        QList<dock_node_t> children;
        QStringList sizes;
        for ( qsizetype i = 0; i < savedSizes.size(); ++i ) {
            dock_node_t child = std::move( node.children[i] );
            if ( RemoveLegacyAssetNodes( child ) ) {
                children.append( std::move( child ) );
                sizes.append( savedSizes[i] );
            }
        }
        if ( children.isEmpty() ) { return false; }
        SetDockAttribute( node, "Count", QString::number( children.size() ) );
        dock_node_t sizeNode;
        sizeNode.tag = QStringLiteral( "Sizes" );
        sizeNode.text = sizes.join( QLatin1Char( ' ' ) );
        children.append( std::move( sizeNode ) );
        node.children = std::move( children );
        return true;
    }
    if ( node.tag == QLatin1String( "Container" ) ) {
        bool hasRoot = false, hasSidebar = false;
        for ( auto it = node.children.begin(); it != node.children.end(); ) {
            if ( it->tag == QLatin1String( "Geometry" ) ) { ++it; continue; }
            if ( !RemoveLegacyAssetNodes( *it ) ) { it = node.children.erase( it ); }
            else {
                hasSidebar |= it->tag == QLatin1String( "SideBar" );
                hasRoot |= it->tag != QLatin1String( "SideBar" );
                ++it;
            }
        }
        if ( !hasRoot ) {
            if ( !hasSidebar && node.attributes.value( QLatin1String( "Floating" ) ) == QLatin1String( "1" ) ) { return false; }
            // ADS still requires a root for the main container or one holding
            // live auto-hide tabs. An empty splitter preserves that container.
            dock_node_t root;
            root.tag = QStringLiteral( "Splitter" );
            SetDockAttribute( root, "Orientation", QStringLiteral( "|" ) );
            SetDockAttribute( root, "Count", QStringLiteral( "0" ) );
            dock_node_t sizes;
            sizes.tag = QStringLiteral( "Sizes" );
            root.children.append( std::move( sizes ) );
            node.children.insert( node.attributes.value( QLatin1String( "Floating" ) ) == QLatin1String( "1" ) ? 1 : 0, std::move( root ) );
        }
        return true;
    }
    return true;
}

void WriteDockTree( QXmlStreamWriter &xml, const dock_node_t &node )
{
    xml.writeStartElement( node.tag );
    xml.writeAttributes( node.attributes );
    if ( !node.text.trimmed().isEmpty() ) { xml.writeCharacters( node.text ); }
    for ( const dock_node_t &child : node.children ) { WriteDockTree( xml, child ); }
    xml.writeEndElement();
}

bool MigrateLegacyAssets( QByteArray &state )
{
    QXmlStreamReader input( state );
    dock_node_t root;
    if ( !input.readNextStartElement() || !ReadDockTree( input, root, 0 ) ) { return false; }
    for ( auto it = root.children.begin(); it != root.children.end(); ) {
        if ( !RemoveLegacyAssetNodes( *it ) ) { it = root.children.erase( it ); }
        else { ++it; }
    }
    SetDockAttribute( root, "Containers", QString::number( root.children.size() ) );
    for ( auto it = root.attributes.begin(); it != root.attributes.end(); ) {
        if ( it->name() == QLatin1String( "FocusedDockWidget" ) && it->value() == QLatin1String( "assets" ) ) { it = root.attributes.erase( it ); }
        else { ++it; }
    }
    QByteArray migrated;
    QXmlStreamWriter output( &migrated );
    output.writeStartDocument();
    WriteDockTree( output, root );
    output.writeEndDocument();
    if ( output.hasError() || migrated.size() > kMaximumBytes ) { return false; }
    state = std::move( migrated );
    return true;
}

QByteArray SaveDockXml( ads::CDockManager *pDocks )
{
    const QByteArray state = pDocks->saveState( kVersion );
    // These bytes are produced by the live dock manager, not loaded from disk.
    return state.startsWith( "<?xml" ) ? state : qUncompress( state );
}
}

workspace_state_result_t MasonWorkspace_Save( QMainWindow *pWindow, ads::CDockManager *pDocks,
                                              QWidget *pViews, const QString &path )
{
    if ( pWindow == nullptr || pDocks == nullptr || pViews == nullptr || path.isEmpty() ) {
        return workspace_state_result_t::INVALID;
    }
    const QByteArray views = MapViews_SavePresentation( pViews );
    const QByteArray docks = SaveDockXml( pDocks );
    if ( views.isEmpty() || !ValidateDocks( docks, pDocks ) ) { return workspace_state_result_t::INVALID; }
    const QJsonObject object{
        { QStringLiteral( "schema" ), QStringLiteral( "cypher.mason.workspace" ) },
        { QStringLiteral( "version" ), kVersion },
        { QStringLiteral( "geometry" ), QString::fromLatin1( pWindow->saveGeometry().toBase64() ) },
        { QStringLiteral( "toolbars" ), QString::fromLatin1( pWindow->saveState( kVersion ).toBase64() ) },
        { QStringLiteral( "docks" ), QString::fromLatin1( docks.toBase64() ) },
        { QStringLiteral( "views" ), QString::fromLatin1( views.toBase64() ) },
    };
    const QByteArray bytes = QJsonDocument( object ).toJson();
    if ( bytes.size() > kMaximumBytes ) { return workspace_state_result_t::INVALID; }
    if ( !QDir().mkpath( QFileInfo( path ).absolutePath() ) ) { return workspace_state_result_t::IO_ERROR; }
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) || file.write( bytes ) != bytes.size() || !file.commit() ) {
        return workspace_state_result_t::IO_ERROR;
    }
    return workspace_state_result_t::OK;
}

workspace_state_result_t MasonWorkspace_Restore( QMainWindow *pWindow, ads::CDockManager *pDocks,
                                                 QWidget *pViews, const QString &path )
{
    if ( pWindow == nullptr || pDocks == nullptr || pViews == nullptr || path.isEmpty() ) {
        return workspace_state_result_t::INVALID;
    }
    QFile file( path );
    if ( !file.exists() ) { return workspace_state_result_t::NOT_FOUND; }
    if ( !file.open( QIODevice::ReadOnly ) ) { return workspace_state_result_t::IO_ERROR; }
    if ( file.size() <= 0 || file.size() > kMaximumBytes ) { return workspace_state_result_t::INVALID; }
    const QByteArray bytes = file.read( kMaximumBytes + 1 );
    if ( file.error() != QFileDevice::NoError ) { return workspace_state_result_t::IO_ERROR; }
    if ( bytes.size() > kMaximumBytes ) { return workspace_state_result_t::INVALID; }
    const QJsonDocument document = QJsonDocument::fromJson( bytes );
    const QJsonObject object = document.object();
    if ( !document.isObject() || object.value( QStringLiteral( "schema" ) ) != QStringLiteral( "cypher.mason.workspace" ) ||
         object.value( QStringLiteral( "version" ) ) != kVersion ) { return workspace_state_result_t::INVALID; }
    const QByteArray geometry = Decode( object, "geometry" );
    const QByteArray toolbars = Decode( object, "toolbars" );
    QByteArray docks = Decode( object, "docks" );
    const QByteArray views = Decode( object, "views" );
    bool hasLegacyAssets = false;
    if ( geometry.isEmpty() || toolbars.isEmpty() || docks.isEmpty() ||
         !MapViews_ValidatePresentation( views ) || !ValidateDocks( docks, pDocks, true, &hasLegacyAssets ) ) {
        return workspace_state_result_t::INVALID;
    }
    if ( hasLegacyAssets && ( !MigrateLegacyAssets( docks ) || !ValidateDocks( docks, pDocks ) ) ) {
        return workspace_state_result_t::INVALID;
    }
    // Qt validates the serialized state even when the referenced toolbars do
    // not exist in the staging window. It never looks up another window's
    // widgets. Reject corrupt blobs before live geometry/layout can change.
    QMainWindow probe;
    if ( !probe.restoreGeometry( geometry ) || !probe.restoreState( toolbars, kVersion ) ) {
        return workspace_state_result_t::INVALID;
    }
    const QByteArray oldGeometry = pWindow->saveGeometry();
    const QByteArray oldToolbars = pWindow->saveState( kVersion );
    const QByteArray oldDocks = pDocks->saveState( kVersion );
    // View restoration validates first, then activates prepared pane contents.
    // Do it last so a later dock/Qt failure cannot change live presentation.
    if ( !pWindow->restoreGeometry( geometry ) || !pWindow->restoreState( toolbars, kVersion ) ||
         !pDocks->restoreState( docks, kVersion ) || !MapViews_RestorePresentation( pViews, views ) ) {
        pWindow->restoreGeometry( oldGeometry );
        pWindow->restoreState( oldToolbars, kVersion );
        pDocks->restoreState( oldDocks, kVersion );
        return workspace_state_result_t::INVALID;
    }
    return workspace_state_result_t::OK;
}

bool MasonWorkspace_ApplyPanelPolicy( ads::CDockManager *pDocks, bool bCollapseConsole )
{
    if ( pDocks == nullptr ) { return false; }
    auto *pConsole = pDocks->findDockWidget( QStringLiteral( "console" ) );
    auto *pProperties = pDocks->findDockWidget( QStringLiteral( "map.properties" ) );
    if ( pConsole == nullptr || pProperties == nullptr || pProperties->dockAreaWidget() == nullptr ) { return false; }
    const bool bOpenConsole = !bCollapseConsole && !pConsole->isClosed();
    if ( pConsole->isAutoHide() ) { pConsole->setAutoHide( false ); }
    if ( pConsole->dockAreaWidget() != pProperties->dockAreaWidget() ) {
        if ( pDocks->addDockWidgetTabToArea( pConsole, pProperties->dockAreaWidget() ) == nullptr ) { return false; }
    }
    pConsole->toggleView( bOpenConsole );
    if ( bCollapseConsole && !pProperties->isClosed() ) { pProperties->setAsCurrentTab(); }
    return true;
}
}
