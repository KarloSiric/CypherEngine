# Renderer resource bindings: first two-slot contract

An immutable graphics pipeline declares zero, one, or two named GLSL uniform
blocks and zero, one, or two named sampled-image uniforms. Each sampled slot
declares either ordinary `sampler2D` (the default) or `sampler2DShadow`.
Bindings occupy consecutive logical slots beginning at zero. The matching
indexed draw supplies one live uniform buffer or texture handle for every
declared slot and leaves every unused slot invalid. A block uses its whole
buffer; the buffer must be at least as large as the block's reflected byte
size and may not be CPU-mapped.

The OpenGL 4.1 backend maps uniform slot *i* to uniform-buffer binding point
*i*, and texture slot *i* to texture unit *i*. Pipeline creation reflects the
linked shader's active interface: names, exact block sizes, uniform types and
counts must match the declaration. An optimized-out block or sampler is not an
active resource and cannot be declared. The backend sets block and sampler
assignments at each draw because multiple pipelines may share one GL program
with different slot orders.

For example, a shader with active `ViewData` and `ObjectData` blocks plus
`base_color` and `detail_color` samplers can use this description:

```cpp
render_pipeline_desc_t pipeline{};
pipeline.shader = shader;
pipeline.vertexLayout = layout;
pipeline.uniformBlockCount = 2;
pipeline.uniformBlocks[0] = { "ViewData", sizeof( view_data_t ) };
pipeline.uniformBlocks[1] = { "ObjectData", sizeof( object_data_t ) };
pipeline.sampledTextureCount = 2;
pipeline.sampledTextures[0].name = "base_color";
pipeline.sampledTextures[1].name = "detail_color";

render_draw_indexed_desc_t draw{};
draw.pipeline = pipelineHandle;
draw.vertexInput = vertexInput;
draw.uniformBuffers[0] = viewBuffer;
draw.uniformBuffers[1] = objectBuffer;
draw.sampledTextures[0] = baseTexture;
draw.sampledTextures[1] = detailTexture;
draw.indexCount = indexCount;
draw.vertexCount = vertexCount;
```

The frontend rejects missing, extra, stale, mapped, wrongly typed, and
undersized resources before backend submission. It also rejects sampling
either attachment of the active render target in either slot. A failed
validation or backend draw releases any temporary uniform-buffer references.

## Sampling policy

A draw may optionally pair each declared texture slot with an immutable
`render_sampler_handle_t`. Its description selects nearest or linear minification
and magnification, no/nearest/linear mip filtering, and repeat or edge-clamp
addressing independently for U and V. This lets one image be reused with
different sampling policies in the two slots. An invalid sampler in a declared
slot keeps that texture's existing creation-time filter and wrap settings;
existing draws therefore need no sampler changes. A sampler in an undeclared
texture slot is a binding mismatch. A mip-filtered sampler cannot be paired with
a texture created without mip levels, including a render-target texture.
This includes D24 target depth: it has one mip level and can be read by a
scalar `sampler2D` after its target pass ends. The shader reads depth from the
sample's red component. For hardware depth comparison, declare that pipeline
slot as `DEPTH_COMPARE_2D`, use an active `sampler2DShadow` uniform, and bind
a D24 texture with an explicit sampler whose `compare` is, for example,
`LESS_EQUAL`. A shadow slot rejects a color texture, an omitted sampler, or a
sampler with `compare = NONE`. A scalar slot rejects a comparison sampler.
These checks prevent undefined OpenGL shader/sampler combinations. The D24
texture itself retains ordinary scalar sampling as its default; the explicit
sampler supplies comparison behavior for that draw. Nearest filtering gives a
single comparison; the exact filtering of multiple depth comparisons with
linear sampling is implementation-dependent.

Sampler handles are generational renderer objects, independent of texture
handles. Draws resolve them immediately on the renderer thread; neither object
retains the other. The OpenGL 4.1 backend binds an explicit sampler object on
the corresponding texture unit, or binds sampler zero for the legacy policy.
Hosted frames restore the host's sampler bindings for both used units.

This is a binding contract, not yet a material system. Cooked material loading,
anisotropy, arrays, cubemaps, storage buffers, ordinary
uniforms, more than two slots, and reusable draw queues remain separate work.
The hosted frame preserves both slots of borrowed OpenGL texture, sampler, and
indexed uniform-buffer state. It currently queries this state each frame; its
effect on editor frame time still needs measurement.
