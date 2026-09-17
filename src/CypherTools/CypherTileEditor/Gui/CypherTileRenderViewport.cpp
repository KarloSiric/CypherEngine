//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileRenderViewport.cpp
//  Purpose: Implements the Qt-hosted CypherRender tile-map viewport.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileRenderViewport.h"
#include "CypherTileCanvas.h"

#include "Core/CypherTileMapMaterials.h"

#include "CypherCommon/Formats/CypherCommon_CookedShader.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_Vector.h"
#include "CypherRender/CypherRender_Draw.h"
#include "CypherRender/CypherRender_Pipeline.h"
#include "CypherRender/CypherRender_Shader.h"

#include <QByteArray>
#include <QApplication>
#include <QCursor>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QStringList>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <filesystem>

namespace cypher::tools::tile_editor
{

namespace common = ::cypher::common;
namespace math = ::cypher::math;
namespace render = ::cypher::engine::render;

class CypherTileAxisTriad final : public QWidget
{
public:
    explicit CypherTileAxisTriad( QWidget *pParent )
        : QWidget( pParent )
    {
        setObjectName( QStringLiteral( "TileAxisTriad" ) );
        setAttribute( Qt::WA_StyledBackground, true );
        setAttribute( Qt::WA_TransparentForMouseEvents, true );
        setFocusPolicy( Qt::NoFocus );
        resize( 88, 88 );
    }

    void setCamera( const tile_camera_t &camera )
    {
        m_camera = camera;
        update();
    }

    void setAxisColors( const QColor &xColor, const QColor &yColor,
                        const QColor &zColor )
    {
        m_colors = { xColor, yColor, zColor };
        update();
    }

protected:
    void paintEvent( QPaintEvent *pEvent ) override
    {
        QWidget::paintEvent( pEvent );
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing, true );

        const math::vec3_t right = CypherTileCamera_Right( m_camera );
        const math::vec3_t up = CypherTileCamera_Up( m_camera );
        const math::vec3_t forward = CypherTileCamera_Forward( m_camera );
        const std::array<math::vec3_t, 3> worldAxes{
            math::vec3_t{ 1.0f, 0.0f, 0.0f },
            math::vec3_t{ 0.0f, 1.0f, 0.0f },
            math::vec3_t{ 0.0f, 0.0f, 1.0f }
        };
        const std::array<QString, 3> labels{
            QStringLiteral( "X" ), QStringLiteral( "Y" ), QStringLiteral( "Z" ) };
        struct projected_axis_t {
            QPointF direction{};
            float depth{};
            QColor color{};
            QString label{};
        };
        std::array<projected_axis_t, 3> axes{};
        for ( std::size_t i = 0; i < axes.size(); ++i ) {
            axes[i] = {
                QPointF( math::Vec3_Dot( worldAxes[i], right ),
                         -math::Vec3_Dot( worldAxes[i], up ) ),
                math::Vec3_Dot( worldAxes[i], forward ),
                m_colors[i], labels[i]
            };
        }
        // Paint the axes pointing away from the camera first so nearer ones
        // remain legible where their projected directions overlap.
        std::sort( axes.begin(), axes.end(), []( const auto &left, const auto &rightAxis ) {
            return left.depth > rightAxis.depth;
        } );

        const QPointF origin( width() * 0.5, height() * 0.5 );
        constexpr qreal axisScale = 27.0;
        QFont axisFont = painter.font();
        axisFont.setBold( true );
        painter.setFont( axisFont );

        for ( const auto &axis : axes ) {
            const QPointF endpoint = origin + axis.direction * axisScale;
            const qreal projectedLength = std::hypot(
                axis.direction.x(), axis.direction.y() ) * axisScale;
            if ( projectedLength < 4.0 ) {
                painter.setPen( QPen( QColor( 0, 0, 0, 190 ), 5.0 ) );
                painter.setBrush( axis.color );
                painter.drawEllipse( origin, 4.5, 4.5 );
                painter.setPen( axis.color );
                painter.drawText( QRectF( origin.x() + 5.0, origin.y() - 17.0,
                                          14.0, 16.0 ), Qt::AlignCenter,
                                  axis.label );
                continue;
            }

            const QPointF direction = axis.direction / std::hypot(
                axis.direction.x(), axis.direction.y() );
            const QPointF perpendicular( -direction.y(), direction.x() );
            const QPointF arrowBase = endpoint - direction * 6.5;
            painter.setPen( QPen( QColor( 0, 0, 0, 190 ), 4.5,
                                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
            painter.drawLine( origin, endpoint );
            painter.setPen( QPen( axis.color, 2.25, Qt::SolidLine,
                                  Qt::RoundCap, Qt::RoundJoin ) );
            painter.drawLine( origin, endpoint );
            painter.drawLine( endpoint, arrowBase + perpendicular * 3.5 );
            painter.drawLine( endpoint, arrowBase - perpendicular * 3.5 );
            painter.drawText( QRectF( endpoint.x() - 7.0, endpoint.y() - 9.0,
                                      14.0, 18.0 ), Qt::AlignCenter,
                              axis.label );
        }

        painter.setPen( QPen( palette().color( QPalette::Text ), 1.0 ) );
        painter.setBrush( palette().color( QPalette::Base ) );
        painter.drawEllipse( origin, 2.5, 2.5 );
    }

private:
    tile_camera_t m_camera{};
    std::array<QColor, 3> m_colors{
        QColor( 222, 82, 76 ), QColor( 82, 190, 105 ), QColor( 75, 139, 232 ) };
};

namespace
{

bool CellBefore( tile_map_grid_coord_t left, tile_map_grid_coord_t right )
{
    return left.y < right.y || ( left.y == right.y && left.x < right.x );
}

bool SameCell( tile_map_grid_coord_t left, tile_map_grid_coord_t right )
{
    return left.x == right.x && left.y == right.y;
}

bool SelectionContains( std::span<const tile_map_grid_coord_t> cells,
    tile_map_grid_coord_t cell ) noexcept
{
    return std::binary_search( cells.begin(), cells.end(), cell, CellBefore );
}

struct preview_vertex_t {
    float position[3];
    float normal[3];
    float uv[2];
};

static_assert( sizeof( preview_vertex_t ) == 32u );
static_assert( offsetof( preview_vertex_t, normal ) == 12u );
static_assert( offsetof( preview_vertex_t, uv ) == 24u );

constexpr preview_vertex_t PREVIEW_CUBE_VERTICES[]{
    { { 1, -1, -1 }, { 1, 0, 0 }, { 0, 0 } },
    { { 1, 1, -1 }, { 1, 0, 0 }, { 1, 0 } },
    { { 1, 1, 1 }, { 1, 0, 0 }, { 1, 1 } },
    { { 1, -1, 1 }, { 1, 0, 0 }, { 0, 1 } },
    { { -1, 1, -1 }, { -1, 0, 0 }, { 0, 0 } },
    { { -1, -1, -1 }, { -1, 0, 0 }, { 1, 0 } },
    { { -1, -1, 1 }, { -1, 0, 0 }, { 1, 1 } },
    { { -1, 1, 1 }, { -1, 0, 0 }, { 0, 1 } },
    { { 1, 1, -1 }, { 0, 1, 0 }, { 0, 0 } },
    { { -1, 1, -1 }, { 0, 1, 0 }, { 1, 0 } },
    { { -1, 1, 1 }, { 0, 1, 0 }, { 1, 1 } },
    { { 1, 1, 1 }, { 0, 1, 0 }, { 0, 1 } },
    { { -1, -1, -1 }, { 0, -1, 0 }, { 0, 0 } },
    { { 1, -1, -1 }, { 0, -1, 0 }, { 1, 0 } },
    { { 1, -1, 1 }, { 0, -1, 0 }, { 1, 1 } },
    { { -1, -1, 1 }, { 0, -1, 0 }, { 0, 1 } },
    { { -1, -1, 1 }, { 0, 0, 1 }, { 0, 0 } },
    { { 1, -1, 1 }, { 0, 0, 1 }, { 1, 0 } },
    { { 1, 1, 1 }, { 0, 0, 1 }, { 1, 1 } },
    { { -1, 1, 1 }, { 0, 0, 1 }, { 0, 1 } },
    { { -1, 1, -1 }, { 0, 0, -1 }, { 0, 0 } },
    { { 1, 1, -1 }, { 0, 0, -1 }, { 1, 0 } },
    { { 1, -1, -1 }, { 0, 0, -1 }, { 1, 1 } },
    { { -1, -1, -1 }, { 0, 0, -1 }, { 0, 1 } }
};

constexpr common::u16 PREVIEW_CUBE_INDICES[]{
    0, 1, 2, 0, 2, 3,       4, 5, 6, 4, 6, 7,
    8, 9, 10, 8, 10, 11,    12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23
};

struct alignas( 16 ) preview_transforms_t {
    math::mat4_t model;
    math::mat4_t view;
    math::mat4_t projection;
    alignas( 16 ) float tint[4];
    alignas( 16 ) float uvScale[4]{ 1, 1, 0, 0 };
};

static_assert( sizeof( preview_transforms_t ) == 224u );
static_assert( offsetof( preview_transforms_t, tint ) == 192u );

bool CameraPoseIsRenderable(
    const tile_camera_t &camera, float aspect ) noexcept
{
    if ( !math::Vec3_IsFinite( camera.position ) ||
         !std::isfinite( camera.yawRadians ) ||
         !std::isfinite( camera.pitchRadians ) ||
         !std::isfinite( camera.orbitDistance ) ||
         camera.orbitDistance <= 0.0f ) return false;
    math::mat4_t view{}, projection{};
    return CypherTileCamera_BuildMatrices(
        camera, aspect, view, projection );
}

bool RepresentableFloat( double value, float &result ) noexcept
{
    constexpr double maximum = std::numeric_limits<float>::max();
    if ( !std::isfinite( value ) || value < -maximum || value > maximum )
        return false;
    result = static_cast<float>( value );
    return std::isfinite( result );
}

} // namespace

bool CypherTileRenderViewport_PickGeometry(
    const tile_camera_t &camera,
    std::span<const tile_map_geometry_box_t> boxes,
    float normalizedX, float normalizedY, float aspect,
    tile_map_grid_coord_t &cellOut ) noexcept
{
    if ( !std::isfinite( normalizedX ) || !std::isfinite( normalizedY ) ||
         !std::isfinite( aspect ) || aspect <= 0.0f ||
         !math::Vec3_IsFinite( camera.position ) ||
         !std::isfinite( camera.settings.verticalFovDegrees ) ||
         camera.settings.verticalFovDegrees <= 0.0f || camera.settings.verticalFovDegrees >= 180.0f ) return false;
    const auto forward = CypherTileCamera_Forward( camera );
    const auto right = CypherTileCamera_Right( camera );
    const auto up = math::Vec3_Cross( right, forward );
    const float halfHeight = std::tan( camera.settings.verticalFovDegrees * 0.00872664626f );
    const auto direction = math::Vec3_Add( forward, math::Vec3_Add(
        math::Vec3_Scale( right, normalizedX * aspect * halfHeight ),
        math::Vec3_Scale( up, normalizedY * halfHeight ) ) );
    if ( !math::Vec3_IsFinite( direction ) ) return false;
    const float origin[]{ camera.position.x, camera.position.y, camera.position.z };
    const float ray[]{ direction.x, direction.y, direction.z };
    // The unnormalized ray has forward depth 1. Its parameter therefore uses
    // the same near/far plane distances as the perspective projection.
    const float farDepth = std::max( 100.0f,
        math::Vec3_Distance( camera.position, camera.boundsCenter ) + camera.boundsRadius * 2.0f );
    float closestDepth = farDepth;
    bool found = false;
    for ( const auto &box : boxes ) {
        const float center[]{ box.centerX, box.centerY, box.centerZ };
        const float halfExtent[]{ box.halfExtentX, box.halfExtentY, box.halfExtentZ };
        float nearDepth = 0.025f;
        float exitDepth = closestDepth;
        bool intersects = true;
        for ( int axis = 0; axis < 3; ++axis ) {
            if ( !std::isfinite( center[axis] ) || !std::isfinite( halfExtent[axis] ) || halfExtent[axis] <= 0.0f ) {
                intersects = false;
                break;
            }
            const float minimum = center[axis] - halfExtent[axis];
            const float maximum = center[axis] + halfExtent[axis];
            if ( std::abs( ray[axis] ) < 0.0000001f ) {
                if ( origin[axis] < minimum || origin[axis] > maximum ) { intersects = false; break; }
                continue;
            }
            float first = ( minimum - origin[axis] ) / ray[axis];
            float last = ( maximum - origin[axis] ) / ray[axis];
            if ( first > last ) std::swap( first, last );
            nearDepth = std::max( nearDepth, first );
            exitDepth = std::min( exitDepth, last );
            if ( nearDepth > exitDepth ) { intersects = false; break; }
        }
        if ( intersects && ( !found || nearDepth < closestDepth ) ) {
            closestDepth = nearDepth;
            cellOut = box.sourceCell;
            found = true;
        }
    }
    return found;
}

bool CypherTileRenderViewport_PickPlane(
    const tile_camera_t &camera, float normalizedX, float normalizedY, float aspect,
    float floorZ, const tile_map_document_t &document, tile_map_grid_coord_t &cellOut ) noexcept
{
    if ( !std::isfinite( normalizedX ) || !std::isfinite( normalizedY ) ||
         !std::isfinite( aspect ) || aspect <= 0 || !std::isfinite( floorZ ) ||
         !math::Vec3_IsFinite( camera.position ) || !std::isfinite( document.nCellSize ) || document.nCellSize <= 0 ||
         !std::isfinite( camera.settings.verticalFovDegrees ) || camera.settings.verticalFovDegrees <= 0 ||
         camera.settings.verticalFovDegrees >= 180 ) return false;
    const auto forward = CypherTileCamera_Forward( camera );
    const auto right = CypherTileCamera_Right( camera );
    const auto up = math::Vec3_Cross( right, forward );
    const float halfHeight = std::tan( camera.settings.verticalFovDegrees * 0.00872664626f );
    const auto direction = math::Vec3_Add( forward, math::Vec3_Add(
        math::Vec3_Scale( right, normalizedX * aspect * halfHeight ),
        math::Vec3_Scale( up, normalizedY * halfHeight ) ) );
    if ( !math::Vec3_IsFinite( direction ) || std::abs( direction.z ) < 0.0000001f ) return false;
    const double distance = ( static_cast<double>( floorZ ) - camera.position.z ) / direction.z;
    if ( !std::isfinite( distance ) || distance < 0.025 ) return false;
    const double x = std::floor( ( camera.position.x + direction.x * distance ) / document.nCellSize );
    const double y = std::floor( ( camera.position.y + direction.y * distance ) / document.nCellSize );
    if ( !std::isfinite( x ) || !std::isfinite( y ) || x < 0 || y < 0 || x >= document.nWidth || y >= document.nHeight ) return false;
    cellOut = { static_cast<i32>( x ), static_cast<i32>( y ) };
    return true;
}

CypherTileRenderViewport::CypherTileRenderViewport( QWidget *pParent )
    : QOpenGLWidget( pParent )
{
    setObjectName( QStringLiteral( "CypherTileRenderViewport" ) );
    setFocusPolicy( Qt::StrongFocus );
    setMouseTracking( true );
    setMinimumSize( 160, 120 );
    setToolTip( tr( "Left click selects map geometry. Hold right mouse: look + WASD, Q/E down/up; Shift fast, Ctrl slow. "
                   "Alt+right mouse orbits; middle mouse pans; "
                   "Use the Camera menu for navigation mode and shortcuts." ) );

    QSurfaceFormat format;
    format.setRenderableType( QSurfaceFormat::OpenGL );
    format.setVersion( 4, 1 );
    format.setProfile( QSurfaceFormat::CoreProfile );
    format.setDepthBufferSize( 24 );
    format.setStencilBufferSize( 8 );
    format.setSwapBehavior( QSurfaceFormat::DoubleBuffer );
    format.setSwapInterval( 1 );
    setFormat( format );

    const tile_map_document_status_t geometryStatus =
        CypherTileMapGeometry_Init(
            &m_geometry,
            common::Allocator_GetSystem() );
    m_bGeometryInitialized =
        geometryStatus == tile_map_document_status_t::OK;

    m_pOverlay = new QLabel(
        tr( "Initializing CypherRender..." ), this );
    m_pOverlay->setObjectName( QStringLiteral( "TileRenderOverlay" ) );
    m_pOverlay->setAlignment( Qt::AlignCenter );
    m_pOverlay->setWordWrap( true );
    m_pOverlay->setAttribute( Qt::WA_TransparentForMouseEvents );

    m_pAxisTriad = new CypherTileAxisTriad( this );
    updateAxisTriad();

    m_pFrameTimer = new QTimer( this );
    m_pFrameTimer->setObjectName( QStringLiteral( "TileRenderFrameTimer" ) );
    m_pFrameTimer->setInterval( 16 );
    connect( m_pFrameTimer, &QTimer::timeout, this, [this] { update(); } );
}

CypherTileRenderViewport::~CypherTileRenderViewport()
{
    stopNavigation();
    QObject::disconnect( m_contextDestroyConnection );
    shutdownRenderer();
    if ( m_bGeometryInitialized ) {
        CypherTileMapGeometry_Shutdown( &m_geometry );
    }
}

void CypherTileRenderViewport::setDocumentBridge(
    CypherTileDocumentBridge *pBridge )
{
    stopNavigation();
    m_pBridge = pBridge;
    m_selectedCells.clear();
    m_bUserNavigated = false;
    refreshDocument();
}

void CypherTileRenderViewport::setStatusCallback(
    status_callback_t callback )
{
    m_statusCallback = std::move( callback );
}

void CypherTileRenderViewport::setSelection(
    bool bSelected,
    tile_map_grid_coord_t coordinate )
{
    setSelectedCells( bSelected ? std::span( &coordinate, 1 ) : std::span<const tile_map_grid_coord_t>{} );
}

void CypherTileRenderViewport::setSelectionRect(
    bool bSelected, const tile_map_grid_rect_t &rectangle )
{
    std::vector<tile_map_grid_coord_t> cells;
    if ( bSelected ) {
        const qint64 x0 = std::clamp<qint64>( rectangle.x, 0, TILE_MAP_MAX_WIDTH );
        const qint64 y0 = std::clamp<qint64>( rectangle.y, 0, TILE_MAP_MAX_HEIGHT );
        const qint64 x1 = std::clamp<qint64>( static_cast<qint64>( rectangle.x ) + rectangle.nWidth, 0, TILE_MAP_MAX_WIDTH );
        const qint64 y1 = std::clamp<qint64>( static_cast<qint64>( rectangle.y ) + rectangle.nHeight, 0, TILE_MAP_MAX_HEIGHT );
        for ( qint64 y = y0; y < y1; ++y )
            for ( qint64 x = x0; x < x1; ++x ) cells.push_back( { static_cast<i32>( x ), static_cast<i32>( y ) } );
    }
    setSelectedCells( cells );
}

void CypherTileRenderViewport::setSelectedCells( std::span<const tile_map_grid_coord_t> cells )
{
    std::vector<tile_map_grid_coord_t> canonical( cells.begin(), cells.end() );
    std::erase_if( canonical, []( tile_map_grid_coord_t cell ) {
        return cell.x < 0 || cell.y < 0 || cell.x >= static_cast<i32>( TILE_MAP_MAX_WIDTH ) ||
            cell.y >= static_cast<i32>( TILE_MAP_MAX_HEIGHT );
    } );
    std::sort( canonical.begin(), canonical.end(), CellBefore );
    canonical.erase( std::unique( canonical.begin(), canonical.end(), SameCell ), canonical.end() );
    if ( std::equal( canonical.begin(), canonical.end(), m_selectedCells.begin(), m_selectedCells.end(), SameCell ) ) return;
    m_selectedCells = std::move( canonical );
    update();
}

void CypherTileRenderViewport::setSelectionCallback( selection_callback_t callback )
{
    m_selectionCallback = std::move( callback );
}

void CypherTileRenderViewport::setMultiSelectionCallback( multi_selection_callback_t callback )
{
    m_multiSelectionCallback = std::move( callback );
}

void CypherTileRenderViewport::setTool( tile_canvas_tool_t tool ) { m_tool = tool; updateCameraOverlay(); }
void CypherTileRenderViewport::setPaint( const tile_map_paint_t &paint ) { m_paint = paint; updateCameraOverlay(); }
void CypherTileRenderViewport::setDoorSide( tile_map_marker_side_t side ) { m_doorSide = side; }
void CypherTileRenderViewport::setChangedCallback( std::function<void()> callback ) { m_changedCallback = std::move( callback ); }
void CypherTileRenderViewport::setPaintPickedCallback( std::function<void( const tile_map_paint_t & )> callback ) { m_paintPickedCallback = std::move( callback ); }

void CypherTileRenderViewport::setViewAppearance( const QColor &background, bool showMetrics )
{
    if ( background.isValid() ) m_backgroundColor = background;
    m_showViewMetrics = showMetrics;
    updateCameraOverlay();
    update();
}

void CypherTileRenderViewport::setAxisAppearance(
    bool visible, const QColor &xColor, const QColor &yColor,
    const QColor &zColor )
{
    if ( m_pAxisTriad == nullptr ) return;
    m_pAxisTriad->setAxisColors(
        xColor.isValid() ? xColor : QColor( 222, 82, 76 ),
        yColor.isValid() ? yColor : QColor( 82, 190, 105 ),
        zColor.isValid() ? zColor : QColor( 75, 139, 232 ) );
    m_pAxisTriad->setVisible( visible );
    updateAxisTriad();
}

void CypherTileRenderViewport::setMaterialRoot( const QString &root )
{
    if ( m_materialRoot == root ) return;
    m_materialRoot = root;
    reloadMaterials();
}

void CypherTileRenderViewport::reloadMaterials()
{
    m_materialReloadPending = true;
    update();
}

void CypherTileRenderViewport::refreshDocument()
{
    QByteArray signature;
    if ( m_pBridge != nullptr && m_pBridge->isInitialized() ) {
        for ( const auto &binding : std::span( m_pBridge->document()->materialBindings.pData,
                                             m_pBridge->document()->materialBindings.nCount ) ) {
            signature.append( QByteArray::number( binding.nSlot ) );
            signature.append( ':' );
            signature.append( binding.path );
            signature.append( '\n' );
        }
    }
    if ( signature != m_requestedMaterialSignature ) {
        m_requestedMaterialSignature = signature;
        m_materialReloadPending = true;
    }
    if ( !m_bGeometryInitialized ) {
        report( tr( "3D map unavailable: geometry storage could not be initialized." ),
                true );
        update();
        return;
    }
    if ( m_pBridge == nullptr || !m_pBridge->isInitialized() ) {
        clearGeometry();
        if ( !m_materialError.isEmpty() ) updateCameraOverlay();
        else m_pOverlay->setText( tr( "No map loaded. Create or open a map to begin." ) );
        update();
        return;
    }
    const tile_map_document_status_t status = CypherTileMapGeometry_Build(
        m_pBridge->document(), {}, &m_geometry );
    if ( status != tile_map_document_status_t::OK ) {
        // The core builder preserves the previous geometry transactionally on
        // failure. The editor must not present that old map as the current one.
        clearGeometry();
        report( tr( "3D geometry unavailable: %1. See Validation and Console." ).arg(
            QString::fromLatin1(
                CypherTileMapDocument_StatusName( status ) ) ), true );
        update();
        return;
    }
    m_lastReportedError.clear();
    updateCameraBounds( !m_bUserNavigated );
    if ( m_bResourcesReady ) updateCameraOverlay();
    synchronizeFrameTimer();
    update();
}

void CypherTileRenderViewport::fitCamera()
{
    stopNavigation();
    setAutoOrbitState( false );
    m_bUserNavigated = false;
    updateCameraBounds( true );
    updateCameraOverlay();
    update();
}

void CypherTileRenderViewport::setCameraSettings( const tile_camera_settings_t &settings, bool bFlyMode )
{
    if ( m_bCameraPreferencesApplied && bFlyMode == m_bFlyMode &&
         settings == m_configuredSettings ) return;
    stopNavigation();
    setAutoOrbitState( false );
    synchronizeFrameTimer();
    m_configuredSettings = settings;
    m_bCameraPreferencesApplied = true;
    CypherTileCamera_SetSettings( m_camera, settings );
    m_bFlyMode = bFlyMode;
    CypherTileCamera_SetMode( m_camera, bFlyMode ? tile_camera_mode_t::FLY : tile_camera_mode_t::ORBIT );
    updateCameraBounds( !m_bUserNavigated );
    updateCameraOverlay();
    update();
}

void CypherTileRenderViewport::setCameraMode( tile_camera_mode_t mode )
{
    if ( mode != tile_camera_mode_t::FLY && mode != tile_camera_mode_t::ORBIT ) return;
    stopNavigation();
    setAutoOrbitState( false );
    m_bFlyMode = mode == tile_camera_mode_t::FLY;
    CypherTileCamera_SetMode( m_camera, mode );
    synchronizeFrameTimer();
    notifyCameraChange();
    updateCameraOverlay();
    update();
}

void CypherTileRenderViewport::setMoveSpeed( float speed )
{
    auto settings = m_camera.settings;
    settings.moveSpeed = std::isfinite( speed ) ? std::clamp( speed, 0.1f, 1000.0f ) : 8.0f;
    CypherTileCamera_SetSettings( m_camera, settings );
    notifyCameraChange();
    updateCameraOverlay();
}

void CypherTileRenderViewport::setCameraViewPreset(
    tile_camera_view_preset_t preset )
{
    if ( preset < tile_camera_view_preset_t::PERSPECTIVE ||
         preset > tile_camera_view_preset_t::RIGHT ) return;
    stopNavigation();
    setAutoOrbitState( false );
    m_bUserNavigated = true;
    CypherTileCamera_ApplyViewPreset( m_camera, preset );
    // Axis-aligned viewpoints behave as inspection views. Orbiting preserves
    // their focus point. This is a transient pose command: the user's saved
    // Fly/Orbit navigation preference remains unchanged.
    CypherTileCamera_SetMode( m_camera, tile_camera_mode_t::ORBIT );
    synchronizeFrameTimer();
    updateCameraOverlay();
    update();
}

void CypherTileRenderViewport::levelCamera()
{
    stopNavigation();
    m_bUserNavigated = true;
    // Level the mode currently being inspected. In particular, auto orbit is
    // an active Orbit view even when Fly is the saved navigation preference.
    CypherTileCamera_Level( m_camera );
    setAutoOrbitState( false );
    updateCameraOverlay();
    update();
}

bool CypherTileRenderViewport::moveCameraLevel( int delta )
{
    if ( delta == 0 || m_pBridge == nullptr || !m_pBridge->isInitialized() ) return false;
    const float levelHeight = m_pBridge->document()->nLevelHeight;
    if ( !std::isfinite( levelHeight ) || levelHeight <= 0.0f ) return false;
    const double verticalOffset = static_cast<double>( levelHeight ) * delta;
    if ( !std::isfinite( verticalOffset ) ||
         verticalOffset < -std::numeric_limits<float>::max() ||
         verticalOffset > std::numeric_limits<float>::max() ) return false;
    tile_camera_t candidate = m_camera;
    if ( !CypherTileCamera_TranslateWorld( candidate,
             { 0.0f, 0.0f, static_cast<float>( verticalOffset ) } ) ) return false;
    stopNavigation();
    setAutoOrbitState( false );
    m_bUserNavigated = true;
    m_camera.position = candidate.position;
    updateCameraOverlay();
    update();
    return true;
}

void CypherTileRenderViewport::setAutoOrbitEnabled( bool enabled )
{
    if ( m_bAutoOrbit == enabled ) return;
    stopNavigation();
    setAutoOrbitState( enabled );
    CypherTileCamera_SetMode( m_camera, enabled
        ? tile_camera_mode_t::ORBIT
        : ( m_bFlyMode ? tile_camera_mode_t::FLY : tile_camera_mode_t::ORBIT ) );
    m_bUserNavigated = true;
    m_nPreviousFrameNanoseconds = 0;
    synchronizeFrameTimer();
    updateCameraOverlay();
    update();
}

void CypherTileRenderViewport::toggleAutoOrbit()
{
    setAutoOrbitEnabled( !m_bAutoOrbit );
}

bool CypherTileRenderViewport::storeCameraBookmark( int slot )
{
    if ( slot < 0 || slot >= static_cast<int>( m_cameraBookmarks.size() ) ) return false;
    const float aspect = static_cast<float>( std::max( width(), 1 ) ) /
        static_cast<float>( std::max( height(), 1 ) );
    if ( !CameraPoseIsRenderable( m_camera, aspect ) ) return false;
    m_cameraBookmarks[static_cast<std::size_t>( slot )] = camera_bookmark_t{
        m_camera.position, m_camera.yawRadians, m_camera.pitchRadians,
        m_camera.orbitDistance };
    return true;
}

bool CypherTileRenderViewport::recallCameraBookmark( int slot )
{
    if ( !hasCameraBookmark( slot ) ) return false;
    const camera_bookmark_t &bookmark =
        *m_cameraBookmarks[static_cast<std::size_t>( slot )];
    tile_camera_t candidate = m_camera;
    candidate.position = bookmark.position;
    candidate.yawRadians = bookmark.yawRadians;
    candidate.pitchRadians = bookmark.pitchRadians;
    candidate.orbitDistance = bookmark.orbitDistance;
    CypherTileCamera_SetMode( candidate,
        m_bFlyMode ? tile_camera_mode_t::FLY : tile_camera_mode_t::ORBIT );
    const float aspect = static_cast<float>( std::max( width(), 1 ) ) /
        static_cast<float>( std::max( height(), 1 ) );
    if ( !CameraPoseIsRenderable( candidate, aspect ) ) return false;
    stopNavigation();
    setAutoOrbitState( false );
    m_camera.position = candidate.position;
    m_camera.yawRadians = candidate.yawRadians;
    m_camera.pitchRadians = candidate.pitchRadians;
    m_camera.orbitDistance = candidate.orbitDistance;
    CypherTileCamera_SetMode( m_camera, candidate.mode );
    m_bUserNavigated = true;
    updateCameraOverlay();
    update();
    return true;
}

bool CypherTileRenderViewport::hasCameraBookmark( int slot ) const
{
    return slot >= 0 && slot < static_cast<int>( m_cameraBookmarks.size() ) &&
        m_cameraBookmarks[static_cast<std::size_t>( slot )].has_value();
}

void CypherTileRenderViewport::clearCameraBookmarks()
{
    for ( auto &bookmark : m_cameraBookmarks ) bookmark.reset();
}

void CypherTileRenderViewport::setCameraChangeCallback( camera_change_callback_t callback )
{
    m_cameraChangeCallback = std::move( callback );
}

void CypherTileRenderViewport::setAutoOrbitChangeCallback(
    auto_orbit_change_callback_t callback )
{
    m_autoOrbitChangeCallback = std::move( callback );
}

void CypherTileRenderViewport::setContextMenuCallback(
    context_menu_callback_t callback )
{
    m_contextMenuCallback = std::move( callback );
}

void CypherTileRenderViewport::setAutoOrbitState( bool enabled )
{
    if ( m_bAutoOrbit == enabled ) return;
    m_bAutoOrbit = enabled;
    if ( !enabled ) {
        CypherTileCamera_SetMode( m_camera,
            m_bFlyMode ? tile_camera_mode_t::FLY : tile_camera_mode_t::ORBIT );
    }
    // Commands commonly stop pointer navigation before cancelling auto orbit.
    // Re-evaluate the timer after the state flip so that sequence cannot leave
    // an otherwise idle viewport repainting at 60 Hz.
    synchronizeFrameTimer();
    if ( m_autoOrbitChangeCallback ) m_autoOrbitChangeCallback( enabled );
}

void CypherTileRenderViewport::setCameraHintsVisible( bool visible )
{
    m_showCameraHints = visible;
    updateCameraOverlay();
}

void CypherTileRenderViewport::notifyCameraChange()
{
    // Keep the comparison baseline in sync so unrelated preference changes do
    // not reset a speed chosen with the wheel or stop a navigation gesture.
    m_configuredSettings = m_camera.settings;
    m_bCameraPreferencesApplied = true;
    if ( m_cameraChangeCallback ) m_cameraChangeCallback( m_camera.settings, m_bFlyMode );
}

void CypherTileRenderViewport::frameSelection()
{
    if ( m_selectedCells.empty() ) { fitCamera(); return; }
    bool found = false;
    math::vec3_t minimum{}, maximum{};
    for ( common::usize i = 0; i < common::Vector_Count( &m_geometry.boxes ); ++i ) {
        const auto &box = m_geometry.boxes.pData[i];
        if ( !SelectionContains( m_selectedCells, box.sourceCell ) ) continue;
        const math::vec3_t lo{ box.centerX - box.halfExtentX, box.centerY - box.halfExtentY, box.centerZ - box.halfExtentZ };
        const math::vec3_t hi{ box.centerX + box.halfExtentX, box.centerY + box.halfExtentY, box.centerZ + box.halfExtentZ };
        if ( !found ) { minimum = lo; maximum = hi; found = true; }
        else {
            minimum = { std::min( minimum.x, lo.x ), std::min( minimum.y, lo.y ), std::min( minimum.z, lo.z ) };
            maximum = { std::max( maximum.x, hi.x ), std::max( maximum.y, hi.y ), std::max( maximum.z, hi.z ) };
        }
    }
    if ( !found ) { fitCamera(); return; }
    stopNavigation();
    setAutoOrbitState( false );
    m_bUserNavigated = true;
    CypherTileCamera_FrameBounds( m_camera, minimum, maximum,
        static_cast<float>( std::max( width(), 1 ) ) / std::max( height(), 1 ) );
    // Selection determines framing, but clipping must still cover the map.
    updateCameraBounds( false );
    updateCameraOverlay();
    update();
}

bool CypherTileRenderViewport::goToPlayerSpawn()
{
    if ( m_pBridge == nullptr || !m_pBridge->isInitialized() ) return false;
    const auto *document = m_pBridge->document();
    const auto *spawn = CypherTileMapDocument_PlayerSpawn( document );
    if ( spawn == nullptr ) return false;
    const auto *cell = CypherTileMapDocument_CellAt( document, spawn->cell );
    if ( cell == nullptr || !( cell->flags & TILE_MAP_CELL_FLAG_FLOOR ) ||
         cell->shape != tile_map_cell_shape_t::FLAT ) return false;
    tile_camera_t candidate = m_camera;
    const double cellSize = document->nCellSize;
    const double levelHeight = document->nLevelHeight;
    const double eyeHeight = std::min( levelHeight * 0.6, 1.7 );
    if ( !RepresentableFloat(
             ( static_cast<double>( spawn->cell.x ) + 0.5 ) * cellSize,
             candidate.position.x ) ||
         !RepresentableFloat(
             ( static_cast<double>( spawn->cell.y ) + 0.5 ) * cellSize,
             candidate.position.y ) ||
         !RepresentableFloat(
             static_cast<double>( cell->nFloorLevel ) * levelHeight + eyeHeight,
             candidate.position.z ) ||
         !RepresentableFloat(
             static_cast<double>( spawn->yawDegrees ) * 0.017453292519943295,
             candidate.yawRadians ) ) return false;
    candidate.pitchRadians = 0.0f;
    CypherTileCamera_SetMode( candidate, tile_camera_mode_t::FLY );
    const float aspect = static_cast<float>( std::max( width(), 1 ) ) /
        static_cast<float>( std::max( height(), 1 ) );
    if ( !CameraPoseIsRenderable( candidate, aspect ) ) return false;
    stopNavigation();
    setAutoOrbitState( false );
    m_bUserNavigated = true;
    m_camera.position = candidate.position;
    m_camera.yawRadians = candidate.yawRadians;
    m_camera.pitchRadians = candidate.pitchRadians;
    m_bFlyMode = true;
    CypherTileCamera_SetMode( m_camera, tile_camera_mode_t::FLY );
    notifyCameraChange();
    updateCameraOverlay();
    update();
    return true;
}

void CypherTileRenderViewport::initializeGL()
{
    QObject::disconnect( m_contextDestroyConnection );
    m_contextDestroyConnection = connect(
        context(),
        &QOpenGLContext::aboutToBeDestroyed,
        this,
        &CypherTileRenderViewport::handleHostContextAboutToBeDestroyed,
        Qt::DirectConnection );

    if ( initializeRenderer() ) {
        m_pOverlay->setProperty( "ready", true );
        m_pOverlay->style()->unpolish( m_pOverlay );
        m_pOverlay->style()->polish( m_pOverlay );
        m_frameClock.start();
        m_nPreviousFrameNanoseconds = 0;
        m_nFrameIndex = 0u;
        synchronizeFrameTimer();
        report( tr( "CypherRender 3D map ready" ) );
        refreshDocument();
    } else {
        m_pFrameTimer->stop();
    }
}

void CypherTileRenderViewport::resizeGL( int, int )
{
    // Construction happens before the quad splitters have their final sizes.
    // Keep the full map framed until the user chooses a camera position.
    updateCameraBounds( !m_bUserNavigated );
    if ( !m_bOwnsRenderer || !render::R_IsInitialized() ) return;
    (void)checkRender(
        render::R_Resize( drawableExtent() ),
        tr( "Resize embedded renderer" ) );
}

void CypherTileRenderViewport::paintGL()
{
    if ( !m_bResourcesReady || !m_bOwnsRenderer ||
         !render::R_IsInitialized() ) return;
    const render::render_extent_t extent = drawableExtent();
    if ( extent.width == 0u || extent.height == 0u ) return;
    if ( m_materialReloadPending ) {
        m_materialReloadPending = false;
        (void)reloadMaterialResources();
    }

    const qint64 now = m_frameClock.nsecsElapsed();
    const float deltaSeconds = m_nPreviousFrameNanoseconds == 0
        ? 1.0f / 60.0f
        : static_cast<float>( std::clamp(
              ( now - m_nPreviousFrameNanoseconds ) / 1.0e9,
              0.0,
              0.1 ) );
    m_nPreviousFrameNanoseconds = now;
    if ( m_bAutoOrbit ) {
        CypherTileCamera_ApplyLook( m_camera, deltaSeconds * 0.14f / m_camera.settings.lookSensitivity, 0.0f );
    }
    updateAxisTriad();
    if ( m_navigation == navigation_t::LOOK ) {
        tile_camera_input_t input{};
        input.forward = ( m_pressedKeys.contains( Qt::Key_W ) ? 1.0f : 0.0f ) - ( m_pressedKeys.contains( Qt::Key_S ) ? 1.0f : 0.0f );
        input.right = ( m_pressedKeys.contains( Qt::Key_D ) ? 1.0f : 0.0f ) - ( m_pressedKeys.contains( Qt::Key_A ) ? 1.0f : 0.0f );
        input.up = ( m_pressedKeys.contains( Qt::Key_E ) ? 1.0f : 0.0f ) - ( m_pressedKeys.contains( Qt::Key_Q ) ? 1.0f : 0.0f );
        input.fast = QApplication::keyboardModifiers().testFlag( Qt::ShiftModifier );
        input.slow = QApplication::keyboardModifiers().testFlag( Qt::ControlModifier );
        CypherTileCamera_Move( m_camera, input, deltaSeconds );
        updateCameraOverlay();
    }

    math::mat4_t view{};
    math::mat4_t projection{};
    if ( !buildCameraMatrices( extent, view, projection ) ) {
        report( tr( "Could not build the embedded preview camera." ), true );
        return;
    }

    render::render_frame_info_t frame{};
    frame.frameIndex = m_nFrameIndex++;
    frame.deltaSeconds = deltaSeconds;
    frame.drawableExtent = extent;
    frame.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
    frame.clearColor = { static_cast<float>( m_backgroundColor.redF() ), static_cast<float>( m_backgroundColor.greenF() ), static_cast<float>( m_backgroundColor.blueF() ), 1.0f };
    if ( !checkRender(
             render::R_BeginFrame( frame ),
             tr( "Begin embedded frame" ) ) ) return;
    const bool bDrawOK = drawGeometry( view, projection );
    const bool bEndOK = checkRender(
        render::R_EndFrame(),
        tr( "End embedded frame" ) );
    if ( !bDrawOK || !bEndOK ) m_pFrameTimer->stop();
}

bool CypherTileRenderViewport::event( QEvent *pEvent )
{
    if ( pEvent->type() == QEvent::ShortcutOverride && isNavigating() ) {
        // A captured camera owns its keyboard completely. This prevents tool,
        // selection and configurable camera actions from firing mid-gesture.
        pEvent->accept();
        return true;
    }
    if ( pEvent->type() == QEvent::WindowDeactivate || pEvent->type() == QEvent::UngrabMouse ) stopNavigation();
    return QOpenGLWidget::event( pEvent );
}

void CypherTileRenderViewport::mousePressEvent( QMouseEvent *pEvent )
{
    const bool selectionModifier = pEvent->modifiers() & ( Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier );
    if ( pEvent->button() == Qt::LeftButton && ( m_tool != tile_canvas_tool_t::PAN || selectionModifier ) ) {
        if ( !isNavigating() ) {
            setFocus( Qt::MouseFocusReason );
            tile_map_grid_coord_t cell{};
            const float viewportWidth = std::max( width(), 1 );
            const float viewportHeight = std::max( height(), 1 );
            const float normalizedX = static_cast<float>( pEvent->position().x() ) / viewportWidth * 2.0f - 1.0f;
            const float normalizedY = 1.0f - static_cast<float>( pEvent->position().y() ) / viewportHeight * 2.0f;
            const bool hit = CypherTileRenderViewport_PickGeometry( m_camera,
                { m_geometry.boxes.pData, m_geometry.boxes.nCount },
                normalizedX, normalizedY, viewportWidth / viewportHeight, cell );
            if ( m_tool == tile_canvas_tool_t::SELECT || selectionModifier ) {
                if ( m_multiSelectionCallback ) m_multiSelectionCallback( hit, cell, pEvent->modifiers() );
                else {
                    setSelection( hit, cell );
                    if ( m_selectionCallback ) m_selectionCallback( hit, cell );
                }
            } else if ( m_pBridge != nullptr && m_pBridge->isInitialized() ) {
                const bool supported = m_tool == tile_canvas_tool_t::PAINT || m_tool == tile_canvas_tool_t::ERASE ||
                    m_tool == tile_canvas_tool_t::PLAYER_SPAWN || m_tool == tile_canvas_tool_t::DOOR || m_tool == tile_canvas_tool_t::EYEDROPPER;
                const bool target = hit || ( m_tool == tile_canvas_tool_t::PAINT &&
                    CypherTileRenderViewport_PickPlane( m_camera, normalizedX, normalizedY, viewportWidth / viewportHeight,
                        m_paint.nFloorLevel * m_pBridge->document()->nLevelHeight, *m_pBridge->document(), cell ) );
                if ( !supported ) report( tr( "Use Paint, Erase, Spawn, Door or Eyedropper in 3D; drag drawing tools use orthographic views." ), false );
                else if ( !target ) report( tr( "No map cell at the pointer. Paint can add cells on the brush floor plane inside map bounds." ), false );
                else if ( m_tool == tile_canvas_tool_t::EYEDROPPER ) {
                    const auto *picked = CypherTileMapDocument_CellAt( m_pBridge->document(), cell );
                    if ( picked != nullptr ) {
                        m_paint = { picked->nFloorLevel, picked->nWallHeightLevels, picked->nMaterialSlot, picked->shape, picked->nStairSteps };
                        if ( m_paintPickedCallback ) m_paintPickedCallback( m_paint );
                    }
                } else {
                    QString error;
                    bool applied = m_pBridge->beginEdit( tr( "Author in 3D view" ), &error );
                    if ( applied ) {
                        if ( m_tool == tile_canvas_tool_t::PAINT ) applied = m_pBridge->paintCell( cell, m_paint, &error );
                        else if ( m_tool == tile_canvas_tool_t::ERASE ) applied = m_pBridge->eraseCell( cell, &error );
                        else if ( m_tool == tile_canvas_tool_t::PLAYER_SPAWN ) applied = m_pBridge->placePlayerSpawn( cell, 0.0f, &error );
                        else applied = CypherTileMapDocument_DoorAt( m_pBridge->document(), cell, m_doorSide ) != nullptr
                            ? m_pBridge->removeDoorAt( cell, m_doorSide, &error ) : m_pBridge->placeDoor( cell, m_doorSide, nullptr, &error );
                        if ( applied ) applied = m_pBridge->commitEdit( &error );
                        if ( !applied ) m_pBridge->cancelEdit();
                    }
                    if ( applied ) {
                        m_bUserNavigated = true;
                        refreshDocument();
                        if ( m_changedCallback ) m_changedCallback();
                    } else report( error, true );
                }
            }
        }
        pEvent->accept();
        return;
    }
    if ( pEvent->button() != Qt::RightButton && pEvent->button() != Qt::MiddleButton &&
         !( pEvent->button() == Qt::LeftButton && m_tool == tile_canvas_tool_t::PAN ) ) {
        QOpenGLWidget::mousePressEvent( pEvent );
        return;
    }
    stopNavigation();
    setAutoOrbitState( false );
    setFocus( Qt::MouseFocusReason );
    m_previousMode = m_camera.mode;
    m_navigationButton = pEvent->button();
    m_bContextMenuCandidate = pEvent->button() == Qt::RightButton &&
        pEvent->modifiers() == Qt::NoModifier;
    m_contextMenuGlobal = pEvent->globalPosition().toPoint();
    const bool pan = pEvent->button() == Qt::MiddleButton ||
        pEvent->button() == Qt::LeftButton;
    m_bTemporaryOrbit = !pan && m_bFlyMode && pEvent->button() == Qt::RightButton &&
        pEvent->modifiers().testFlag( Qt::AltModifier );
    m_navigation = pan ? navigation_t::PAN
        : ( !m_bFlyMode || pEvent->modifiers().testFlag( Qt::AltModifier ) ) ? navigation_t::ORBIT : navigation_t::LOOK;
    if ( m_navigation != navigation_t::PAN ) {
        CypherTileCamera_SetMode( m_camera, m_navigation == navigation_t::LOOK ? tile_camera_mode_t::FLY : tile_camera_mode_t::ORBIT );
    }
    m_lastMouse = pEvent->position().toPoint();
    m_savedPointer = pEvent->globalPosition().toPoint();
    if ( m_navigation == navigation_t::LOOK &&
         QApplication::platformName() != QStringLiteral( "offscreen" ) && QApplication::platformName() != QStringLiteral( "minimal" ) ) {
        m_bPointerCaptured = true;
        grabMouse( QCursor( Qt::BlankCursor ) );
        m_lastMouse = rect().center();
        QCursor::setPos( mapToGlobal( m_lastMouse ) );
    } else setCursor( Qt::ClosedHandCursor );
    m_nPreviousFrameNanoseconds = 0;
    synchronizeFrameTimer();
    updateCameraOverlay();
    pEvent->accept();
}

void CypherTileRenderViewport::mouseMoveEvent( QMouseEvent *pEvent )
{
    if ( !isNavigating() ) { QOpenGLWidget::mouseMoveEvent( pEvent ); return; }
    const QPoint current = pEvent->position().toPoint();
    const QPoint delta = current - m_lastMouse;
    if ( delta.isNull() ) { pEvent->accept(); return; }
    m_bContextMenuCandidate = false;
    m_bUserNavigated = true;
    if ( m_navigation == navigation_t::PAN ) {
        CypherTileCamera_Pan( m_camera, delta.x(), delta.y(), height() );
    } else {
        CypherTileCamera_ApplyLook( m_camera, delta.x(), delta.y() );
    }
    if ( m_bPointerCaptured ) {
        m_lastMouse = rect().center();
        QCursor::setPos( mapToGlobal( m_lastMouse ) );
    } else m_lastMouse = current;
    updateCameraOverlay();
    update();
    pEvent->accept();
}

void CypherTileRenderViewport::stopNavigation()
{
    m_navigation = navigation_t::NONE;
    m_navigationButton = Qt::NoButton;
    m_bContextMenuCandidate = false;
    m_pressedKeys.clear();
    if ( m_bTemporaryOrbit ) {
        m_bTemporaryOrbit = false;
        CypherTileCamera_SetMode( m_camera, m_previousMode );
    }
    const bool captured = m_bPointerCaptured;
    m_bPointerCaptured = false;
    if ( captured ) {
        if ( QWidget::mouseGrabber() == this ) releaseMouse();
        QCursor::setPos( m_savedPointer );
    }
    unsetCursor();
    synchronizeFrameTimer();
    updateCameraOverlay();
}

void CypherTileRenderViewport::mouseReleaseEvent( QMouseEvent *pEvent )
{
    if ( pEvent->button() == m_navigationButton ) {
        const bool showContextMenu = m_bContextMenuCandidate &&
            pEvent->button() == Qt::RightButton;
        const QPoint globalPosition = m_contextMenuGlobal;
        stopNavigation();
        updateCameraOverlay();
        if ( showContextMenu && m_contextMenuCallback ) {
            QTimer::singleShot( 0, this, [this, globalPosition] {
                if ( m_contextMenuCallback ) m_contextMenuCallback( globalPosition );
            } );
        }
        pEvent->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent( pEvent );
}

void CypherTileRenderViewport::wheelEvent( QWheelEvent *pEvent )
{
    const float ticks = pEvent->angleDelta().y() != 0
        ? static_cast<float>( pEvent->angleDelta().y() ) / 120.0f
        : static_cast<float>( pEvent->pixelDelta().y() ) / 40.0f;
    if ( ticks == 0.0f ) {
        QOpenGLWidget::wheelEvent( pEvent );
        return;
    }
    const bool adjustedFlySpeed = m_camera.mode == tile_camera_mode_t::FLY;
    CypherTileCamera_Wheel( m_camera, ticks );
    // Auto orbit temporarily uses Orbit even when Fly is the saved preference.
    // Apply this gesture to the mode the user was actually viewing, then end
    // auto orbit and restore that preference.
    setAutoOrbitState( false );
    if ( adjustedFlySpeed ) {
        m_camera.settings.moveSpeed = std::clamp( m_camera.settings.moveSpeed, 0.1f, 1000.0f );
        notifyCameraChange();
    }
    m_bUserNavigated = true;
    synchronizeFrameTimer();
    updateCameraOverlay();
    update();
    pEvent->accept();
}

void CypherTileRenderViewport::keyPressEvent( QKeyEvent *pEvent )
{
    if ( pEvent->key() == Qt::Key_Escape && isNavigating() ) {
        stopNavigation();
        updateCameraOverlay();
        pEvent->accept();
        return;
    }
    if ( isNavigating() ) {
        m_bContextMenuCandidate = false;
        if ( pEvent->key() == Qt::Key_W || pEvent->key() == Qt::Key_A ||
             pEvent->key() == Qt::Key_S || pEvent->key() == Qt::Key_D ||
             pEvent->key() == Qt::Key_Q || pEvent->key() == Qt::Key_E ) {
            m_bUserNavigated = true;
        }
        m_pressedKeys.insert( pEvent->key() );
        pEvent->accept();
        return;
    }
    QOpenGLWidget::keyPressEvent( pEvent );
}

void CypherTileRenderViewport::keyReleaseEvent( QKeyEvent *pEvent )
{
    if ( !pEvent->isAutoRepeat() ) m_pressedKeys.remove( pEvent->key() );
    if ( isNavigating() ) { pEvent->accept(); return; }
    QOpenGLWidget::keyReleaseEvent( pEvent );
}

void CypherTileRenderViewport::focusOutEvent( QFocusEvent *pEvent )
{
    stopNavigation();
    QOpenGLWidget::focusOutEvent( pEvent );
}

void CypherTileRenderViewport::resizeEvent( QResizeEvent *pEvent )
{
    QOpenGLWidget::resizeEvent( pEvent );
    if ( m_pAxisTriad != nullptr ) {
        constexpr int size = 88;
        constexpr int margin = 8;
        m_pAxisTriad->setGeometry(
            std::max( 0, width() - size - margin ), margin, size, size );
        m_pAxisTriad->raise();
    }
    if ( m_pOverlay != nullptr ) m_pOverlay->raise();
    updateCameraOverlay();
}

void CypherTileRenderViewport::showEvent( QShowEvent *pEvent )
{
    QOpenGLWidget::showEvent( pEvent );
    if ( m_bResourcesReady ) {
        m_nPreviousFrameNanoseconds = 0;
        synchronizeFrameTimer();
        update();
    }
}

void CypherTileRenderViewport::hideEvent( QHideEvent *pEvent )
{
    stopNavigation();
    m_pFrameTimer->stop();
    QOpenGLWidget::hideEvent( pEvent );
}

bool CypherTileRenderViewport::ActivateHostContext( void *pUserData ) noexcept
{
    auto *pViewport = static_cast<CypherTileRenderViewport *>( pUserData );
    if ( pViewport == nullptr || pViewport->context() == nullptr ) return false;
    pViewport->makeCurrent();
    return QOpenGLContext::currentContext() == pViewport->context();
}

render::render_host_proc_t CypherTileRenderViewport::ResolveHostProcedure(
    const char *pName,
    void *pUserData ) noexcept
{
    auto *pViewport = static_cast<CypherTileRenderViewport *>( pUserData );
    if ( pViewport == nullptr || pViewport->context() == nullptr ||
         pName == nullptr ) return nullptr;
    return reinterpret_cast<render::render_host_proc_t>(
        pViewport->context()->getProcAddress( QByteArray( pName ) ) );
}

bool CypherTileRenderViewport::PrepareHostFrame( void *pUserData ) noexcept
{
    auto *pViewport = static_cast<CypherTileRenderViewport *>( pUserData );
    if ( pViewport == nullptr || pViewport->context() == nullptr ) return false;
    pViewport->makeCurrent();
    if ( QOpenGLContext::currentContext() != pViewport->context() ) {
        return false;
    }
    QOpenGLFunctions *pFunctions = pViewport->context()->functions();
    if ( pFunctions == nullptr ) return false;
    pFunctions->glBindFramebuffer(
        GL_FRAMEBUFFER,
        pViewport->defaultFramebufferObject() );
    return true;
}

bool CypherTileRenderViewport::initializeRenderer()
{
    QOpenGLContext *pContext = context();
    if ( pContext == nullptr ) {
        report( tr( "Qt did not create an OpenGL context." ), true );
        return false;
    }
    const QSurfaceFormat actual = pContext->format();
    render::render_host_surface_desc_t surface{};
    surface.backend = render::render_backend_t::OPENGL;
    surface.drawableExtent = drawableExtent();
    surface.presentMode = render::render_present_mode_t::FIFO;
    surface.apiMajorVersion = static_cast<common::u16>( actual.majorVersion() );
    surface.apiMinorVersion = static_cast<common::u16>( actual.minorVersion() );
    surface.sampleCount = static_cast<common::u8>( std::clamp(
        actual.samples(),
        0,
        static_cast<int>( std::numeric_limits<common::u8>::max() ) ) );
    surface.sRGBFramebuffer = false;
    surface.accelerated = true;
    surface.userData = this;
    surface.ActivateContext = &ActivateHostContext;
    surface.ResolveProcAddress = &ResolveHostProcedure;
    surface.PrepareFrame = &PrepareHostFrame;

    render::render_config_t config = render::R_DefaultConfig();
    config.backend = render::render_backend_t::OPENGL;
    config.presentMode = render::render_present_mode_t::FIFO;
    config.sRGBFramebuffer = false;
    config.sampleCount = surface.sampleCount;
    config.requireAcceleration = false;
    config.optionalCapabilities = render::R_CAPABILITY_NONE;
    if ( !checkRender(
             render::R_InitHostSurface( surface, config ),
             tr( "Initialize embedded CypherRender" ) ) ) return false;
    m_bOwnsRenderer = true;
    if ( !createRenderResources() ) {
        shutdownRenderer();
        return false;
    }
    m_bResourcesReady = true;
    return true;
}

bool CypherTileRenderViewport::createRenderResources()
{
    const QString shaderPath = findCookedShaderPath();
    if ( shaderPath.isEmpty() ) {
        report( tr( "The embedded preview shader was not staged." ), true );
        return false;
    }
    QFile file( shaderPath );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        report( tr( "Could not read %1: %2" )
            .arg( QDir::toNativeSeparators( shaderPath ), file.errorString() ),
            true );
        return false;
    }
    const QByteArray bytes = file.readAll();
    common::cooked_shader_view_t shaderView{};
    const common::cooked_shader_result_t shaderRead =
        common::CookedShader_Read(
            { reinterpret_cast<const common::byte *>( bytes.constData() ),
              static_cast<common::usize>( bytes.size() ) },
            &shaderView );
    if ( !common::CookedShader_Succeeded( shaderRead ) ) {
        report( tr( "Invalid embedded preview shader: %1" ).arg(
            QString::fromLatin1(
                common::CookedShader_StatusName( shaderRead.status ) ) ), true );
        return false;
    }
    const render::render_shader_desc_t shaderDescription{
        &shaderView,
        "CypherTileEditor embedded preview shader"
    };
    if ( !checkRender(
             render::R_CreateShader( shaderDescription, &m_shader ),
             tr( "Create embedded shader" ) ) ) return false;

    const auto createBuffer = [this](
        render::render_buffer_handle_t &buffer,
        const void *pData,
        common::u64 cbData,
        render::render_buffer_usage_flags_t usage,
        render::render_buffer_update_t updatePolicy,
        render::render_buffer_memory_t memory,
        const char *pName ) {
        render::render_buffer_desc_t description{};
        description.byteSize = cbData;
        description.usage = usage;
        description.updatePolicy = updatePolicy;
        description.memory = memory;
        description.debugName = pName;
        const render::render_buffer_data_t data{ pData, cbData };
        return checkRender(
            render::R_CreateBuffer( description, &data, &buffer ),
            QString::fromUtf8( pName ) );
    };

    const preview_transforms_t initial{
        math::CY_MAT4_IDENTITY,
        math::CY_MAT4_IDENTITY,
        math::CY_MAT4_IDENTITY,
        { 1.0f, 1.0f, 1.0f, 1.0f }
    };
    if ( !createBuffer(
             m_vertices,
             PREVIEW_CUBE_VERTICES,
             sizeof( PREVIEW_CUBE_VERTICES ),
             render::R_BUFFER_USAGE_VERTEX,
             render::render_buffer_update_t::IMMUTABLE,
             render::render_buffer_memory_t::DEVICE_LOCAL,
             "Tile viewport cube vertices" ) ||
         !createBuffer(
             m_indices,
             PREVIEW_CUBE_INDICES,
             sizeof( PREVIEW_CUBE_INDICES ),
             render::R_BUFFER_USAGE_INDEX,
             render::render_buffer_update_t::IMMUTABLE,
             render::render_buffer_memory_t::DEVICE_LOCAL,
             "Tile viewport cube indices" ) ||
         !createBuffer(
             m_transforms,
             &initial,
             sizeof( initial ),
             render::R_BUFFER_USAGE_UNIFORM |
                 render::R_BUFFER_USAGE_TRANSFER_DESTINATION,
             render::render_buffer_update_t::STREAM,
             render::render_buffer_memory_t::UPLOAD,
             "Tile viewport transform stream" ) ) return false;

    render::render_vertex_input_desc_t input{};
    input.layout.bindingCount = 1u;
    input.layout.bindings[0].stride = sizeof( preview_vertex_t );
    input.layout.attributeCount = 3u;
    input.layout.attributes[0] = {
        render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    input.layout.attributes[1] = {
        render::render_format_t::RGB32_FLOAT, 12u, 1u, 0u };
    input.layout.attributes[2] = {
        render::render_format_t::RG32_FLOAT, 24u, 2u, 0u };
    input.vertexBufferCount = 1u;
    input.vertexBuffers[0] = { m_vertices, 0u, 0u };
    input.indexBuffer = {
        m_indices, 0u, render::render_index_type_t::UINT16 };
    input.debugName = "Tile viewport shared cube input";
    if ( !checkRender(
             render::R_CreateVertexInput( input, &m_vertexInput ),
             tr( "Create embedded vertex input" ) ) ) return false;

    render::render_pipeline_desc_t pipeline{};
    pipeline.shader = m_shader;
    pipeline.vertexLayout = input.layout;
    pipeline.depthTest = true;
    pipeline.depthWrite = true;
    pipeline.cullBackFaces = true;
    pipeline.frontCounterClockwise = true;
    pipeline.alphaBlend = false;
    pipeline.uniformBlockName = "Transforms";
    pipeline.uniformBlockBytes = offsetof( preview_transforms_t, uvScale );
    pipeline.debugName = "Tile editor embedded blockout pipeline";
    return checkRender(
        render::R_CreateGraphicsPipeline( pipeline, &m_pipeline ),
        tr( "Create embedded pipeline" ) );
}

bool CypherTileRenderViewport::reloadMaterialResources()
{
    if ( m_pBridge == nullptr || !m_pBridge->isInitialized() ) return true;
    const auto *document = m_pBridge->document();
    const QByteArray utf8Root = m_materialRoot.toUtf8();
    const auto root = std::filesystem::path( std::u8string( reinterpret_cast<const char8_t *>( utf8Root.constData() ), static_cast<size_t>( utf8Root.size() ) ) );
    render::render_shader_handle_t candidateShader{};
    render::render_pipeline_handle_t candidatePipeline{};
    const auto cleanup = [&] {
        if ( candidatePipeline.value ) (void)render::R_DestroyGraphicsPipeline( candidatePipeline );
        if ( candidateShader.value ) (void)render::R_DestroyShader( candidateShader );
    };
    if ( document->materialBindings.nCount != 0 ) {
        QFile file( QDir( m_materialRoot ).filePath( "shaders/tile_surface.cyshader_c" ) );
        if ( !file.open( QIODevice::ReadOnly ) || file.size() > 4 * 1024 * 1024 ) {
            reportMaterialError( tr( "Material shader is missing or too large. Previous materials retained; cook the project material and reload." ) );
            return false;
        }
        const auto bytes = file.readAll();
        common::cooked_shader_view_t shader{};
        const auto parsed = common::CookedShader_Read( { reinterpret_cast<const common::byte *>( bytes.constData() ),
            static_cast<common::usize>( bytes.size() ) }, &shader );
        if ( !common::CookedShader_Succeeded( parsed ) ) {
            reportMaterialError( tr( "Invalid cooked material shader. Previous materials retained; cook the project material and reload." ) );
            return false;
        }
        if ( !checkRender( render::R_CreateShader( { &shader, "Tile material shader" }, &candidateShader ), tr( "Create material shader" ), true ) ) return false;
        render::render_pipeline_info_t base{};
        if ( !checkRender( render::R_GetGraphicsPipelineInfo( m_pipeline, &base ), tr( "Read base pipeline" ), true ) ) { cleanup(); return false; }
        render::render_pipeline_desc_t pipeline{};
        pipeline.shader = candidateShader;
        pipeline.vertexLayout = base.vertexLayout;
        pipeline.uniformBlockName = "Transforms";
        pipeline.uniformBlockBytes = sizeof( preview_transforms_t );
        pipeline.sampledTextureName = "base_color";
        pipeline.debugName = "Tile material pipeline";
        if ( !checkRender( render::R_CreateGraphicsPipeline( pipeline, &candidatePipeline ), tr( "Create material pipeline" ), true ) ) { cleanup(); return false; }
    }
    std::string error;
    if ( !CypherTileMaterialPreview_Reload( m_materials, *document, root, error ) ) {
        cleanup();
        reportMaterialError( tr( "Material reload failed; previous materials retained: %1" ).arg( QString::fromStdString( error ) ) );
        return false;
    }
    if ( m_materialPipeline.value ) (void)render::R_DestroyGraphicsPipeline( m_materialPipeline );
    if ( m_materialShader.value ) (void)render::R_DestroyShader( m_materialShader );
    m_materialPipeline = candidatePipeline;
    m_materialShader = candidateShader;
    // A geometry refresh or successful frame must not acknowledge a failed
    // material publication. Only this complete transactional reload does so.
    m_materialError.clear();
    updateCameraOverlay();
    return true;
}

void CypherTileRenderViewport::destroyRenderResources()
{
    if ( !render::R_IsInitialized() ) return;
    CypherTileMaterialPreview_Shutdown( m_materials );
    if ( m_materialPipeline.value ) { (void)render::R_DestroyGraphicsPipeline( m_materialPipeline ); m_materialPipeline = {}; }
    if ( m_materialShader.value ) { (void)render::R_DestroyShader( m_materialShader ); m_materialShader = {}; }
    m_materialReloadPending = true;
    if ( m_pipeline.value != 0u ) {
        (void)render::R_DestroyGraphicsPipeline( m_pipeline );
        m_pipeline = {};
    }
    if ( m_vertexInput.value != 0u ) {
        (void)render::R_DestroyVertexInput( m_vertexInput );
        m_vertexInput = {};
    }
    for ( render::render_buffer_handle_t *pBuffer : {
              &m_transforms, &m_indices, &m_vertices } ) {
        if ( pBuffer->value != 0u ) {
            (void)render::R_DestroyBuffer( *pBuffer );
            *pBuffer = {};
        }
    }
    if ( m_shader.value != 0u ) {
        (void)render::R_DestroyShader( m_shader );
        m_shader = {};
    }
    m_bResourcesReady = false;
}

void CypherTileRenderViewport::shutdownRenderer()
{
    if ( !m_bOwnsRenderer ) return;
    makeCurrent();
    if ( render::R_IsFrameActive() ) (void)render::R_EndFrame();
    if ( render::R_IsInitialized() ) {
        (void)render::R_WaitIdle();
        destroyRenderResources();
        (void)render::R_Shutdown();
    }
    doneCurrent();
    m_bOwnsRenderer = false;
}

void CypherTileRenderViewport::handleHostContextAboutToBeDestroyed()
{
    // QOpenGLWidget may recreate its context when it moves between top-level
    // windows. Release every renderer-owned GL object while the borrowed Qt
    // context is still alive; initializeGL() will rebuild them for the next
    // context.
    m_pFrameTimer->stop();
    shutdownRenderer();
}

bool CypherTileRenderViewport::drawGeometry(
    const math::mat4_t &view,
    const math::mat4_t &projection )
{
    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = m_pipeline;
    draw.vertexInput = m_vertexInput;
    draw.uniformBuffer = m_transforms;
    draw.indexCount = 36u;
    draw.vertexCount = 24u;

    for ( common::usize iBox = 0u;
          iBox < common::Vector_Count( &m_geometry.boxes );
          ++iBox ) {
        const tile_map_geometry_box_t &box = m_geometry.boxes.pData[iBox];
        preview_transforms_t transforms{};
        transforms.model = math::Mat4_Multiply(
            math::Mat4_FromTranslation(
                { box.centerX, box.centerY, box.centerZ } ),
            math::Mat4_FromScale(
                { box.halfExtentX, box.halfExtentY, box.halfExtentZ } ) );
        transforms.view = view;
        transforms.projection = projection;
        const tile_map_material_definition_t material =
            CypherTileMapMaterial_Resolve( box.nMaterialSlot );
        switch ( box.kind ) {
            case tile_map_geometry_box_kind_t::STAIR:
            case tile_map_geometry_box_kind_t::FLOOR:
                transforms.tint[0] = material.colorR;
                transforms.tint[1] = material.colorG;
                transforms.tint[2] = material.colorB;
                break;
            case tile_map_geometry_box_kind_t::WALL:
                transforms.tint[0] = material.colorR * 0.82f;
                transforms.tint[1] = material.colorG * 0.82f;
                transforms.tint[2] = material.colorB * 0.82f;
                break;
            case tile_map_geometry_box_kind_t::DOOR:
                transforms.tint[0] = 0.92f;
                transforms.tint[1] = 0.39f;
                transforms.tint[2] = 0.10f;
                break;
        }
        const auto *textured = box.kind == tile_map_geometry_box_kind_t::DOOR ? nullptr
            : CypherTileMaterialPreview_Find( m_materials, box.nMaterialSlot );
        draw.pipeline = textured != nullptr ? m_materialPipeline : m_pipeline;
        draw.sampledTexture = textured != nullptr ? textured->texture : render::render_texture_handle_t{};
        if ( textured != nullptr ) {
            for ( int i = 0; i < 4; ++i ) transforms.tint[i] = textured->tint[i];
            transforms.uvScale[0] = textured->uvScale[0];
            transforms.uvScale[1] = textured->uvScale[1];
        }
        if ( SelectionContains( m_selectedCells, box.sourceCell ) ) {
            // Selection is an editor display tint; document material slots and
            // the geometry used by the runtime remain unchanged.
            transforms.tint[0] = transforms.tint[0] * 0.25f + 0.75f;
            transforms.tint[1] = transforms.tint[1] * 0.25f + 0.60f;
            transforms.tint[2] = transforms.tint[2] * 0.25f + 0.075f;
        }
        transforms.tint[3] = 1.0f;
        if ( !checkRender(
                 render::R_UpdateBuffer(
                     m_transforms,
                     0u,
                     { &transforms, sizeof( transforms ) } ),
                 tr( "Upload embedded transform" ) ) ||
             !checkRender(
                 render::R_DrawIndexed( draw ),
                 tr( "Draw embedded map box" ) ) ) return false;
    }
    return true;
}

bool CypherTileRenderViewport::buildCameraMatrices(
    render::render_extent_t extent,
    math::mat4_t &viewOut,
    math::mat4_t &projectionOut ) const
{
    return CypherTileCamera_BuildMatrices( m_camera,
        static_cast<float>( extent.width ) / std::max( extent.height, 1u ), viewOut, projectionOut );
}

render::render_extent_t CypherTileRenderViewport::drawableExtent() const
{
    const qreal scale = devicePixelRatioF();
    return {
        static_cast<common::u32>( std::max(
            1,
            qRound( width() * scale ) ) ),
        static_cast<common::u32>( std::max(
            1,
            qRound( height() * scale ) ) )
    };
}

QString CypherTileRenderViewport::findCookedShaderPath() const
{
    const QDir applicationDirectory( QCoreApplication::applicationDirPath() );
    const QString relative =
        QStringLiteral( "resources/render_smoke/cube.cyshader_c" );
    const QStringList candidates{
        applicationDirectory.filePath( relative ),
        applicationDirectory.absoluteFilePath(
            QStringLiteral( "../../../" ) + relative ),
        applicationDirectory.absoluteFilePath(
            QStringLiteral( "../Resources/render_smoke/cube.cyshader_c" ) )
    };
    for ( const QString &candidate : candidates ) {
        const QFileInfo file( candidate );
        if ( file.isFile() && file.isReadable() ) {
            return file.absoluteFilePath();
        }
    }
    return {};
}

void CypherTileRenderViewport::clearGeometry()
{
    common::Vector_Clear( &m_geometry.boxes );
    m_geometry.bHasBounds = common::CY_FALSE;
    m_geometry.boundsMinX = m_geometry.boundsMinY = m_geometry.boundsMinZ = 0.0f;
    m_geometry.boundsMaxX = m_geometry.boundsMaxY = m_geometry.boundsMaxZ = 0.0f;
    synchronizeFrameTimer();
}

void CypherTileRenderViewport::updateCameraBounds( bool bResetView )
{
    const math::vec3_t minimum = m_geometry.bHasBounds
        ? math::vec3_t{ m_geometry.boundsMinX, m_geometry.boundsMinY, m_geometry.boundsMinZ }
        : math::vec3_t{ -2.0f, -2.0f, -2.0f };
    const math::vec3_t maximum = m_geometry.bHasBounds
        ? math::vec3_t{ m_geometry.boundsMaxX, m_geometry.boundsMaxY, m_geometry.boundsMaxZ }
        : math::vec3_t{ 2.0f, 2.0f, 2.0f };
    if ( bResetView ) {
        CypherTileCamera_FrameBounds( m_camera, minimum, maximum,
            static_cast<float>( std::max( width(), 1 ) ) / std::max( height(), 1 ), true );
    } else CypherTileCamera_UpdateBounds( m_camera, minimum, maximum );
}

void CypherTileRenderViewport::synchronizeFrameTimer()
{
    if ( m_pFrameTimer == nullptr ) return;
    if ( m_bResourcesReady && ( m_bAutoOrbit || m_navigation == navigation_t::LOOK ) && isVisible() ) {
        if ( !m_pFrameTimer->isActive() ) m_pFrameTimer->start();
        return;
    }
    m_pFrameTimer->stop();
}

void CypherTileRenderViewport::updateCameraOverlay()
{
    updateAxisTriad();
    if ( m_pOverlay == nullptr ) return;
    const QString error = !m_lastReportedError.isEmpty()
        ? m_lastReportedError : m_materialError;
    QStringList lines;
    if ( !error.isEmpty() ) lines.append( error );
    else {
        if ( m_showViewMetrics ) {
            lines.append( tr( "XYZ %1, %2, %3  ·  %4 boxes  ·  FOV %5°  ·  %6  ·  %7 u/s" )
                .arg( m_camera.position.x, 0, 'f', 1 ).arg( m_camera.position.y, 0, 'f', 1 )
                .arg( m_camera.position.z, 0, 'f', 1 )
                .arg( static_cast<qulonglong>( common::Vector_Count( &m_geometry.boxes ) ) )
                .arg( m_camera.settings.verticalFovDegrees, 0, 'f', 0 )
                .arg( m_bAutoOrbit ? tr( "Auto orbit" )
                    : m_camera.mode == tile_camera_mode_t::FLY ? tr( "Fly" ) : tr( "Orbit" ) )
                .arg( m_camera.settings.moveSpeed, 0, 'f', 1 ) );
        }
        if ( m_showCameraHints ) {
            lines.append( m_camera.mode == tile_camera_mode_t::FLY
                ? tr( "RMB + WASD/QE fly · MMB pan · Alt+RMB orbit · wheel speed · Camera menu" )
                : tr( "RMB orbit · MMB pan · wheel zoom · Camera menu" ) );
        }
    }
    m_pOverlay->setText( lines.join( QLatin1Char( '\n' ) ) );
    m_pOverlay->setVisible( !lines.isEmpty() );
    constexpr int margin = 6;
    const int availableWidth = std::max( 1, width() - margin * 2 );
    const int textHeight = m_pOverlay->fontMetrics().boundingRect(
        QRect( 0, 0, std::max( 1, availableWidth - 12 ), 1000 ),
        Qt::TextWordWrap | Qt::AlignCenter, m_pOverlay->text() ).height();
    const int overlayHeight = std::min( std::max( 24, textHeight + 10 ), std::max( 24, height() / 3 ) );
    m_pOverlay->setGeometry( margin, std::max( margin, height() - overlayHeight - margin ),
        availableWidth, overlayHeight );
    const bool hasError = !error.isEmpty();
    if ( m_pOverlay->property( "error" ).toBool() != hasError ) {
        m_pOverlay->setProperty( "error", hasError );
        m_pOverlay->style()->unpolish( m_pOverlay );
        m_pOverlay->style()->polish( m_pOverlay );
    }
}

void CypherTileRenderViewport::updateAxisTriad()
{
    if ( m_pAxisTriad != nullptr ) m_pAxisTriad->setCamera( m_camera );
}

void CypherTileRenderViewport::report(
    const QString &message,
    bool bError )
{
    const bool shouldReport = !bError || message != m_lastReportedError;
    m_lastReportedError = bError ? message : QString();
    updateCameraOverlay();
    if ( shouldReport && m_statusCallback ) m_statusCallback( message, bError );
}

void CypherTileRenderViewport::reportMaterialError( const QString &message )
{
    const bool changed = message != m_materialError;
    m_materialError = message;
    updateCameraOverlay();
    if ( changed && m_statusCallback ) m_statusCallback( message, true );
}

bool CypherTileRenderViewport::checkRender(
    render::render_error_t result,
    const QString &operation,
    bool bMaterialError )
{
    if ( result == render::render_error_t::OK ) return true;
    const QString message = tr( "%1: %2 (%3)" ).arg(
        operation,
        QString::fromLatin1( render::R_ErrorName( result ) ),
        QString::fromLatin1( render::R_ErrorDescription( result ) ) );
    if ( bMaterialError ) reportMaterialError(
        tr( "Material reload failed; previous materials retained: %1" ).arg( message ) );
    else report( message, true );
    return false;
}

} // namespace cypher::tools::tile_editor
