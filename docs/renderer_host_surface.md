# Embedding CypherRender in an editor viewport

CypherRender supports two presentation owners:

- `R_Init(window, config)` creates a graphics context for a CypherSystem window
  and presents that window itself.
- `R_InitHostSurface(surface, config)` borrows a context and presentation target
  created by an editor framework. CypherRender owns its GPU resources, while the
  host keeps ownership of the context and the widget.

The second path is the correct boundary for an in-process CypherTileEditor
preview. Wrapping a Qt native child with SDL is not a dependable macOS solution:
SDL's current Cocoa external-window path can replace the parent window's content
view instead of behaving as an isolated child view ([SDL issue #12141][sdl-12141]).
A readback-based preview would also add a render-target, texture, transfer, and
synchronization API before the renderer needs those contracts. Letting
`QOpenGLWidget` own the context keeps Qt's event loop and composition rules intact
and lets CypherRender draw directly into the widget's framebuffer.

## Ownership and call order

The host must keep the object referenced by `userData`, its graphics context,
and all callback targets alive until `R_Shutdown` returns. The renderer copies
the descriptor, but it borrows those objects.

The current renderer frontend is a singleton. One process can have one active
standalone surface or one active hosted surface. CypherTileEditor can therefore
embed one 3D map panel now. Multiple simultaneous Mason viewports will need
renderer instances, or a later split between one shared device and several
surfaces.

All calls belong on the widget's graphics thread. `R_InitHostSurface`,
`R_BeginFrame`, `R_Resize`, `R_EndFrame`, `R_WaitIdle`, and `R_Shutdown` activate
the borrowed context through `ActivateContext`. Resource calls use the active
backend directly, so create and destroy buffers, shaders, vertex inputs, and
pipelines from `initializeGL`, `paintGL`, or another scope in which the widget
context is current.

`QOpenGLWidget` renders into a Qt-owned framebuffer object, not necessarily
framebuffer zero. Qt may replace that object after a resize. The host must bind
`defaultFramebufferObject()` from `PrepareFrame` on every frame; it must not
cache the framebuffer name.

## Minimal `QOpenGLWidget` adapter

Request the portable renderer baseline before constructing the widget. On
macOS, set the application default format before constructing `QApplication` if
Qt context sharing is enabled.

```cpp
QSurfaceFormat format;
format.setRenderableType( QSurfaceFormat::OpenGL );
format.setVersion( 4, 1 );
format.setProfile( QSurfaceFormat::CoreProfile );
format.setSwapBehavior( QSurfaceFormat::DoubleBuffer );
format.setSwapInterval( 1 );
format.setSamples( 0 );
format.setColorSpace( QColorSpace( QColorSpace::SRgb ) );
QSurfaceFormat::setDefaultFormat( format );
```

The adapter callbacks can remain private static functions on the viewport
widget. They expose Qt's existing context without putting Qt types in the public
renderer API.

```cpp
using namespace cypher::engine::render;

bool Viewport::activateContext( void *userData ) noexcept
{
    auto *viewport = static_cast<Viewport *>( userData );
    if ( viewport == nullptr || viewport->context() == nullptr ) return false;
    viewport->makeCurrent();
    return QOpenGLContext::currentContext() == viewport->context();
}

render_host_proc_t Viewport::resolveProcedure(
    const char *name,
    void *userData ) noexcept
{
    auto *viewport = static_cast<Viewport *>( userData );
    if ( viewport == nullptr || viewport->context() == nullptr || name == nullptr ) {
        return nullptr;
    }
    return reinterpret_cast<render_host_proc_t>(
        viewport->context()->getProcAddress( name ) );
}

bool Viewport::prepareFrame( void *userData ) noexcept
{
    auto *viewport = static_cast<Viewport *>( userData );
    if ( viewport == nullptr || viewport->context() == nullptr ) return false;
    viewport->context()->functions()->glBindFramebuffer(
        GL_FRAMEBUFFER,
        viewport->defaultFramebufferObject() );
    return true;
}
```

Initialize only after Qt has created the context. The requested renderer
configuration must describe the target Qt actually created. Use physical pixels
for the drawable extent.

```cpp
void Viewport::initializeGL()
{
    const QSurfaceFormat actual = context()->format();
    const qreal scale = devicePixelRatioF();

    render_host_surface_desc_t surface{};
    surface.backend = render_backend_t::OPENGL;
    surface.drawableExtent = {
        static_cast<cypher::common::u32>( width() * scale ),
        static_cast<cypher::common::u32>( height() * scale )
    };
    surface.presentMode = render_present_mode_t::FIFO;
    surface.apiMajorVersion = static_cast<cypher::common::u16>( actual.majorVersion() );
    surface.apiMinorVersion = static_cast<cypher::common::u16>( actual.minorVersion() );
    surface.sampleCount = static_cast<cypher::common::u8>(
        std::max( 0, actual.samples() ) );
    surface.sRGBFramebuffer =
        actual.colorSpace() == QColorSpace( QColorSpace::SRgb );
    surface.accelerated = true;
    surface.userData = this;
    surface.ActivateContext = &Viewport::activateContext;
    surface.ResolveProcAddress = &Viewport::resolveProcedure;
    surface.PrepareFrame = &Viewport::prepareFrame;
    surface.Present = nullptr; // QOpenGLWidget composites after paintGL returns.

    render_config_t config = R_DefaultConfig();
    config.backend = render_backend_t::OPENGL;
    config.presentMode = surface.presentMode;
    config.sampleCount = surface.sampleCount;
    config.sRGBFramebuffer = surface.sRGBFramebuffer;
    config.requireAcceleration = surface.accelerated;

    const render_error_t result = R_InitHostSurface( surface, config );
    // Record and display result through the editor console on failure.
}
```

Drive exactly one renderer frame from each `paintGL` call. The map-to-mesh bridge
can update GPU resources before `R_BeginFrame` while Qt has the context current.

```cpp
void Viewport::paintGL()
{
    if ( !R_IsInitialized() ) return;

    const qreal scale = devicePixelRatioF();
    render_frame_info_t frame{};
    frame.frameIndex = m_frameIndex++;
    frame.deltaSeconds = m_frameTimer.restart() / 1000.0f;
    frame.drawableExtent = {
        static_cast<cypher::common::u32>( width() * scale ),
        static_cast<cypher::common::u32>( height() * scale )
    };
    frame.clearFlags = R_CLEAR_COLOR | R_CLEAR_DEPTH;
    frame.clearColor = { 0.035f, 0.04f, 0.05f, 1.0f };
    frame.clearDepth = 1.0f;

    if ( R_BeginFrame( frame ) != render_error_t::OK ) return;
    // Bind the preview pipeline and draw the current map mesh here.
    (void)R_EndFrame();
}
```

Call `R_Resize` from `resizeGL` using the same physical-pixel conversion. A
zero-by-zero extent is legal while minimized. Before the widget context is
destroyed, make it current, destroy editor-owned renderer handles, call
`R_Shutdown`, and then call `doneCurrent`.

The GUI target will need `Qt6::OpenGLWidgets` and `Cypher::Render` when the
viewport class is added. No SDL window should be created for the embedded path,
and the existing detached preview can remain as a temporary fallback while the
map mesh bridge is connected.

## Present policy

When `Present` is null, `R_EndFrame` issues `glFlush` and returns. Qt performs
composition after `paintGL` returns, avoiding a second swap. If a different host
owns a real swapchain, it can supply `Present`. `SetPresentMode` is optional; if
it is absent, the requested configuration must match `surface.presentMode`.

This contract deliberately does not expose Qt, SDL, OpenGL framebuffer names,
or native window handles through `CypherRender_Public.h`. The only API-specific
part is the host's concrete backend choice and procedure resolver.

[sdl-12141]: https://github.com/libsdl-org/SDL/issues/12141
