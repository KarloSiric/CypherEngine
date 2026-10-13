# Renderer resource bindings: first two-slot contract

An immutable graphics pipeline declares zero, one, or two named GLSL uniform
blocks and zero, one, or two named scalar `sampler2D` uniforms. Entries occupy
consecutive logical slots beginning at zero. The matching indexed draw supplies
one live uniform buffer or texture handle for every declared slot, and leaves
every unused slot invalid. A block uses its whole buffer; the buffer must be at
least as large as the block's reflected byte size and may not be CPU-mapped.

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
undersized resources before backend submission. It also rejects sampling the
active render target's color attachment in either slot. A failed validation or
backend draw releases any temporary uniform-buffer references.

This is a binding contract, not yet a material system. Cooked material loading,
texture sampler choices, arrays, cubemaps, storage buffers, ordinary uniforms,
more than two slots, and reusable draw queues remain separate work. The
hosted frame preserves both slots of borrowed OpenGL texture, sampler, and
indexed uniform-buffer state. It currently queries this state each frame; its
effect on editor frame time still needs measurement.
