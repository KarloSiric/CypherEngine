<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/README.md
//  Purpose: Documents the CypherCommon Image folder.
//  Details: Image contains CPU image descriptors, storage, and primitive processing
//           contracts, while codecs and GPU textures remain separate subsystems.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Image

`Image` owns backend-neutral contracts for uncompressed CPU image data. It does
not represent an OpenGL/Vulkan texture and does not make image storage a per-frame
resource. PNG, JPG, TGA, HDR, EXR, KTX, and other codec implementations belong in
image or asset-pipeline subsystems hidden behind Cypher APIs.

## Ownership paths

- `ImageSurface_Create` allocates final storage. Decoders and generators should
  request uninitialized storage and write directly through `ImageSurface_GetView`.
- `ImageSurface_CreateFromView` is the explicit allocate-and-copy convenience path.
- `ImageSurface_CopyFromView` refreshes compatible storage without allocating.
- `ImageSurface_Recreate` retains compatible capacity and allocates transactionally
  only when growth or a new alignment requires replacement storage.
- `ImageSurface_Move` and `ImageSurface_Swap` transfer ownership without pixel copies.

The active byte size is `layout.cbTotalSize`. The owned allocation can retain a
larger `allocation.cbSize` capacity after recreation; borrowed views expose only
the active bytes. Row-pitch alignment is tracked independently from the allocation
alignment so tighter layouts can reuse stronger-aligned storage. Destruction always
releases the complete original allocation with its original allocator metadata.

## Processing contract

`ImageProcess` performs allocation-free operations over caller-owned image views.
The Phase 1 API provides full and regional copy, full and regional fill, horizontal
and vertical flip, and 90/180-degree rotation. Every operation validates complete
source and destination state before writing any pixels.

- Operations touch logical pixels only and preserve row and slice padding.
- Pixel format, color space, and alpha semantics must match between images.
- Exact self-copy is a no-op.
- Horizontal flip, vertical flip, and 180-degree rotation support exact in-place use.
- Ninety-degree rotation requires separate storage with swapped width and height.
- Other overlapping source and destination ranges are rejected rather than relying
  on an unsafe row-copy order.
- `ImageSurface_CopyFromView` uses the same processing copy contract, then clears
  owned destination padding for deterministic hashes and cooked artifacts.

Format conversion, color-space conversion, alpha conversion, channel swizzling,
and numeric conversion are provided by `ImageConvert`. Conversion uses a canonical
straight-alpha linear RGBA value per pixel, so color transfer never changes alpha
and premultiplication is performed in linear light. The current scalar path covers
all declared UNORM8, UNORM16, FLOAT16, and FLOAT32 formats, supports constants in
channel swizzles, preserves padding, and rejects unsafe overlapping storage.

Compositing, filters, procedural generators, codecs, block compression, SIMD
dispatch, and job scheduling are later processing layers. They must build on
these storage and validation rules instead of weakening them.

`ImageResize` adds allocation-free nearest, linear, and area-box resizing. Nearest
filtering copies any matching raw format. Linear and box filters deliberately
require LINEAR 32-bit float working images; callers convert packed or sRGB sources
through `ImageConvert`, reuse a float scratch surface, resize there, and convert the
result to final storage. Straight alpha is premultiplied during filtering and then
restored, preventing transparent texels from bleeding unrelated RGB into edges.
Arbitrary ratios retain scalar reference kernels. Exact RGBA32 float 2:1 and 1:2
2D paths use fixed scalar kernels because those are the dominant mip-cooking and
preview workloads; benchmark evidence, not API behavior, controls such dispatch.

`ImageMip` calculates complete 1D/2D/3D mip extents and generates one caller-owned
child level through the area-box path. It does not own a mip-chain container or
allocate every level implicitly; the texture cooker remains responsible for level
storage, conversion, serialization, and compression policy.

## Runtime policy

Authored images are decoded or cooked into CPU surfaces, uploaded to renderer-owned
textures, and then referenced by resource handles. A normal frame must not recreate
or duplicate complete image surfaces. Dynamic content should reuse staging storage
and update only changed regions; render targets and software-renderer framebuffers
should likewise be allocated once and reused.
