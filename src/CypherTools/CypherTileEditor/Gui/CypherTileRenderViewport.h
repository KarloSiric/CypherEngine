//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileRenderViewport.h
//  Purpose: Declares the Qt-hosted CypherRender tile-map viewport.
//  Details: Qt owns the OpenGL context and composition target. CypherRender
//           borrows both through its host-surface contract and continues to
//           own shaders, pipelines, buffers, and draw submission.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_RENDERVIEWPORT_H
#define CYPHER_TOOLS_TILEEDITOR_RENDERVIEWPORT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherTileDocumentBridge.h"

#include "Core/CypherTileMapGeometry.h"
#include "Core/CypherTileCamera.h"
#include "Core/CypherTileMaterialPreview.h"

#include "CypherCommon/Mathlib/CypherMath_Matrix4.h"
#include "CypherRender/CypherRender_Public.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QOpenGLWidget>
#include <QColor>
#include <QPoint>
#include <QSet>

#include <functional>
#include <array>
#include <optional>
#include <span>
#include <vector>

class QLabel;
class QHideEvent;
class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QShowEvent;
class QTimer;
class QWheelEvent;

namespace cypher::tools::tile_editor
{

class CypherTileAxisTriad;
enum class tile_canvas_tool_t : unsigned char;

// NDC coordinates use +X to the right and +Y up. Hit testing uses the same
// camera and generated boxes that are submitted to CypherRender.
bool CypherTileRenderViewport_PickGeometry(
    const tile_camera_t &camera,
    std::span<const tile_map_geometry_box_t> boxes,
    float normalizedX, float normalizedY, float aspect,
    tile_map_grid_coord_t &cellOut ) noexcept;

bool CypherTileRenderViewport_PickPlane(
    const tile_camera_t &camera, float normalizedX, float normalizedY, float aspect,
    float floorZ, const tile_map_document_t &document,
    tile_map_grid_coord_t &cellOut ) noexcept;

class CypherTileRenderViewport final : public QOpenGLWidget
{
public:
    using status_callback_t = std::function<void( const QString &, bool )>;
    using selection_callback_t = std::function<void( bool, tile_map_grid_coord_t )>;
    using multi_selection_callback_t = std::function<void( bool, tile_map_grid_coord_t, Qt::KeyboardModifiers )>;
    using camera_change_callback_t = std::function<void( const tile_camera_settings_t &, bool flyMode )>;
    using auto_orbit_change_callback_t = std::function<void( bool enabled )>;
    using context_menu_callback_t = std::function<void( const QPoint & )>;

    explicit CypherTileRenderViewport( QWidget *pParent = nullptr );
    ~CypherTileRenderViewport() override;

    void setDocumentBridge( CypherTileDocumentBridge *pBridge );
    void setStatusCallback( status_callback_t callback );
    void setSelection( bool bSelected, tile_map_grid_coord_t coordinate );
    void setSelectionRect( bool bSelected, const tile_map_grid_rect_t &rectangle );
    void setSelectedCells( std::span<const tile_map_grid_coord_t> cells );
    std::span<const tile_map_grid_coord_t> selectedCells() const { return m_selectedCells; }
    void setSelectionCallback( selection_callback_t callback );
    void setMultiSelectionCallback( multi_selection_callback_t callback );
    void setTool( tile_canvas_tool_t tool );
    void setPaint( const tile_map_paint_t &paint );
    void setDoorSide( tile_map_marker_side_t side );
    void setChangedCallback( std::function<void()> callback );
    void setPaintPickedCallback( std::function<void( const tile_map_paint_t & )> callback );
    void refreshDocument();
    void setMaterialRoot( const QString &root );
    void setViewAppearance( const QColor &background, bool showMetrics );
    void setAxisAppearance( bool visible, const QColor &xColor,
                            const QColor &yColor, const QColor &zColor );
    void reloadMaterials();
    void fitCamera();
    void frameSelection();
    bool goToPlayerSpawn();
    void setCameraSettings( const tile_camera_settings_t &settings, bool bFlyMode );
    // These explicit user commands retain the pose and report changed preferences.
    void setCameraMode( tile_camera_mode_t mode );
    void setMoveSpeed( float speed );
    void setCameraViewPreset( tile_camera_view_preset_t preset );
    void levelCamera();
    bool moveCameraLevel( int delta );
    void setAutoOrbitEnabled( bool enabled );
    void toggleAutoOrbit();
    bool isAutoOrbiting() const { return m_bAutoOrbit; }
    bool storeCameraBookmark( int slot );
    bool recallCameraBookmark( int slot );
    bool hasCameraBookmark( int slot ) const;
    void clearCameraBookmarks();
    void setCameraChangeCallback( camera_change_callback_t callback );
    void setAutoOrbitChangeCallback( auto_orbit_change_callback_t callback );
    void setContextMenuCallback( context_menu_callback_t callback );
    void setCameraHintsVisible( bool visible );
    const tile_camera_t &camera() const { return m_camera; }
    bool isNavigating() const { return m_navigation != navigation_t::NONE; }

protected:
    bool event( QEvent *pEvent ) override;
    void initializeGL() override;
    void resizeGL( int nWidth, int nHeight ) override;
    void paintGL() override;
    void mousePressEvent( QMouseEvent *pEvent ) override;
    void mouseMoveEvent( QMouseEvent *pEvent ) override;
    void mouseReleaseEvent( QMouseEvent *pEvent ) override;
    void wheelEvent( QWheelEvent *pEvent ) override;
    void keyPressEvent( QKeyEvent *pEvent ) override;
    void keyReleaseEvent( QKeyEvent *pEvent ) override;
    void focusOutEvent( QFocusEvent *pEvent ) override;
    void resizeEvent( QResizeEvent *pEvent ) override;
    void showEvent( QShowEvent *pEvent ) override;
    void hideEvent( QHideEvent *pEvent ) override;

private:
    friend struct tile_render_viewport_test_access_t;

    static bool ActivateHostContext( void *pUserData ) noexcept;
    static ::cypher::engine::render::render_host_proc_t ResolveHostProcedure(
        const char *pName,
        void *pUserData ) noexcept;
    static bool PrepareHostFrame( void *pUserData ) noexcept;

    bool initializeRenderer();
    bool createRenderResources();
    bool reloadMaterialResources();
    void destroyRenderResources();
    void shutdownRenderer();
    void handleHostContextAboutToBeDestroyed();
    bool drawGeometry(
        const ::cypher::math::mat4_t &view,
        const ::cypher::math::mat4_t &projection );
    bool buildCameraMatrices(
        ::cypher::engine::render::render_extent_t extent,
        ::cypher::math::mat4_t &viewOut,
        ::cypher::math::mat4_t &projectionOut ) const;
    ::cypher::engine::render::render_extent_t drawableExtent() const;
    QString findCookedShaderPath() const;
    void clearGeometry();
    void updateCameraBounds( bool bResetView );
    void synchronizeFrameTimer();
    void stopNavigation();
    void setAutoOrbitState( bool enabled );
    void updateCameraOverlay();
    void updateAxisTriad();
    void notifyCameraChange();
    void report( const QString &message, bool bError = false );
    void reportMaterialError( const QString &message );
    bool checkRender(
        ::cypher::engine::render::render_error_t result,
        const QString &operation, bool bMaterialError = false );

    CypherTileDocumentBridge *m_pBridge{ nullptr };
    QString m_lastReportedError{};
    QString m_materialError{}; // Cleared only after a complete successful material reload.
    tile_map_geometry_t m_geometry{};
    bool m_bGeometryInitialized{ false };
    std::vector<tile_map_grid_coord_t> m_selectedCells{};
    tile_canvas_tool_t m_tool{};
    tile_map_paint_t m_paint{};
    tile_map_marker_side_t m_doorSide{ tile_map_marker_side_t::NORTH };
    std::function<void()> m_changedCallback{};
    std::function<void( const tile_map_paint_t & )> m_paintPickedCallback{};

    ::cypher::engine::render::render_shader_handle_t m_shader{};
    ::cypher::engine::render::render_pipeline_handle_t m_pipeline{};
    ::cypher::engine::render::render_vertex_input_handle_t m_vertexInput{};
    ::cypher::engine::render::render_buffer_handle_t m_vertices{};
    ::cypher::engine::render::render_buffer_handle_t m_indices{};
    ::cypher::engine::render::render_buffer_handle_t m_transforms{};
    QColor m_backgroundColor{ "#111315" };
    bool m_showViewMetrics{ true };
    bool m_showCameraHints{ true };
    QString m_materialRoot{};
    // Desired authored bindings, not proof that their GPU resources were loaded.
    // Failed attempts retain the previous resources and a separate visible error.
    QByteArray m_requestedMaterialSignature{};
    bool m_materialReloadPending{ true };
    tile_material_preview_set_t m_materials{};
    ::cypher::engine::render::render_shader_handle_t m_materialShader{};
    ::cypher::engine::render::render_pipeline_handle_t m_materialPipeline{};
    bool m_bOwnsRenderer{ false };
    bool m_bResourcesReady{ false };
    ::cypher::common::u64 m_nFrameIndex{ 0u };

    enum class navigation_t { NONE, LOOK, ORBIT, PAN };
    struct camera_bookmark_t {
        ::cypher::math::vec3_t position{};
        float yawRadians{};
        float pitchRadians{};
        float orbitDistance{};
    };
    tile_camera_t m_camera{};
    std::array<std::optional<camera_bookmark_t>, 4> m_cameraBookmarks{};
    tile_camera_settings_t m_configuredSettings{};
    bool m_bCameraPreferencesApplied{ false };
    navigation_t m_navigation{ navigation_t::NONE };
    Qt::MouseButton m_navigationButton{ Qt::NoButton };
    tile_camera_mode_t m_previousMode{ tile_camera_mode_t::FLY };
    bool m_bTemporaryOrbit{ false };
    bool m_bFlyMode{ true };
    QSet<int> m_pressedKeys{};
    QPoint m_savedPointer{};
    bool m_bPointerCaptured{ false };
    bool m_bAutoOrbit{ false };
    bool m_bUserNavigated{ false };
    QPoint m_lastMouse{};
    bool m_bContextMenuCandidate{ false };
    QPoint m_contextMenuGlobal{};
    QElapsedTimer m_frameClock{};
    qint64 m_nPreviousFrameNanoseconds{ 0 };

    QLabel *m_pOverlay{ nullptr };
    CypherTileAxisTriad *m_pAxisTriad{ nullptr };
    QTimer *m_pFrameTimer{ nullptr };
    QMetaObject::Connection m_contextDestroyConnection{};
    status_callback_t m_statusCallback{};
    selection_callback_t m_selectionCallback{};
    multi_selection_callback_t m_multiSelectionCallback{};
    camera_change_callback_t m_cameraChangeCallback{};
    auto_orbit_change_callback_t m_autoOrbitChangeCallback{};
    context_menu_callback_t m_contextMenuCallback{};
};

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_RENDERVIEWPORT_H
