<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/RENDER_ASSETS.md
//  Purpose: Defines the first renderer-facing source asset contracts.
//  Details: This document specifies shader, texture, and material CYKV version 1,
//           their semantic policies, and their boundary with cooked runtime data.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Renderer Asset Contracts

## Status

Version 1 source schemas, typed decoders, deterministic compiler modules, and
versioned cooked payloads are implemented for `.cyshader`, `.cytex`, and
`.cymat`. All three modules are registered with `CypherResourceCompiler` and
consume authored dependencies through the shared VFS contract.

The offline render-asset slice is complete for its version 1 scope. VFS-backed
runtime loader adapters now own complete cooked blobs and publish validated
borrowed views through `CypherResource`. Renderer-owned texture/shader objects,
material binding, and the Qt 6 Picasso application remain separate consumers.

These contracts are the first vertical format slice. They exist to support a
real resource moving through the offline toolchain and, later, through VFS and
`CypherResource`. Renderer integration is intentionally deferred while the
format/schema/compiler foundation is completed.

The GLSL cooked shader is retained as an OpenGL-specific future payload. It does
not define the software-renderer material program and does not make OpenGL the
next implementation task.

## Pipeline

```text
CYKV source recipe
    -> syntax parse
    -> exact schema validation
    -> typed semantic decode
    -> offline/development cooker
    -> versioned `_c` resource
    -> VFS
    -> CypherResource
    -> validated view backed by an owned cooked blob
    -> owning runtime consumer
```

Source documents describe author intent. Cooked files contain validated runtime
data. Neither representation stores OpenGL object names, Vulkan handles, native
pointers, or compiler struct padding.

## Shared Rules

- Source documents use CYKV language version 1 and an exact schema ID/version.
- Resource paths are canonical lowercase virtual paths using `/`.
- Paths are relative, contain no empty, `.` or `..` components, and are at most
  259 bytes.
- The source file's canonical virtual path is its resource identity. A redundant
  name field is not stored inside the document.
- Decoded string views borrow the CYKV document and remain valid only while that
  document remains alive and unchanged.
- Schema validation checks shape and bounds. Typed decoders enforce extension,
  identifier, duplicate, default, and cross-field policies.

## Shader Source

Extension: `.cyshader`

Schema: `cypher.shader`, version 1

```cykv
@cykv 1
@schema "cypher.shader" 1

{
    language = "glsl"
    vertex = "shaders/world.vert"
    fragment = "shaders/world.frag"
    defines = ["CY_WORLD_PASS", "CY_FOG"]
}
```

`language`, `vertex`, and `fragment` are required. Version 1 supports GLSL only.
Stage paths accept their stage-specific extension or `.glsl`. `defines` is an
optional array of at most 64 unique ASCII identifiers.

The first OpenGL cooker validates and cross-stage-links GLSL through glslang,
then preserves canonical preprocessed GLSL. Each stage must declare one of the
supported desktop core profiles using `#version NNN core`, and both stages must
declare the same version. Supported language versions are 330, 400, 410, 420,
430, 440, and 450. macOS targets are capped at 410; Windows and Linux targets
are capped at 450. Compatibility-profile GLSL and GLSL ES are not part of this
contract.

Quoted project-local `.glsl` includes resolve through the source VFS. Includes
are canonicalized below the source root, bounded by depth/file/byte limits, and
recorded once as transitive dependencies. Absolute paths, root escapes, and
`<system>` includes are rejected. The compiler records recipe, stage, include,
compiler, and toolchain dependencies in the tool host. A future Vulkan path may
introduce a separate SPIR-V variant, but version 1 deliberately freezes only
the source contract backed by the current OpenGL toolchain.

### Shader Version 1 Capability Matrix

| Capability | Version 1 contract |
| --- | --- |
| Recipe language | CYKV schema `cypher.shader` version 1 |
| Runtime backend | OpenGL |
| Source language | Desktop GLSL core |
| GLSL versions | 330, 400, 410, 420, 430, 440, 450 |
| Program kind | Graphics |
| Required stages | One vertex and one fragment stage |
| Stage consistency | Both stages use the same GLSL version |
| Defines | Up to 64 unique recipe-defined ASCII identifiers |
| Includes | Quoted project-local `.glsl` files through VFS |
| Validation | Preprocess, stage parse, and cross-stage link through glslang |
| Cooked payload | Canonical null-terminated UTF-8 preprocessed GLSL |
| Deferred | Compute/geometry/tessellation, compatibility/ES, SPIR-V, reflection, and permutations |

## Texture Source

Extension: `.cytex`

Schema: `cypher.texture`, version 1

```cykv
@cykv 1
@schema "cypher.texture" 1

{
    source = "textures/source/panel_n.png"
    usage = "normal"
    color_space = "linear"
    generate_mips = true
}
```

`source` is required and version 1 accepts 8-bit PNG, 8-bit JPEG, and finite
RGBA EXR virtual paths. `usage` is `color`, `normal`, or `data`. The defaults are
`color`, `srgb`, and mip generation enabled. Normal and data textures default to
linear and cannot request sRGB. EXR always uses linear color space. KTX/KTX2,
Basis Universal, block compression, texture arrays, cube maps, and 3D textures
remain future format versions rather than no-op version 1 options.

Sampler state is intentionally absent. Filtering, wrapping, and comparison mode
belong to material/sampler state so one cooked image can be sampled in different
ways.

## Material Source

Extension: `.cymat`

Schema: `cypher.material`, version 1

```cykv
@cykv 1
@schema "cypher.material" 1

{
    shader = "shaders/world.cyshader"

    textures = {
        base_color = "textures/panel.cytex"
        normal_map = "textures/panel_n.cytex"
    }

    parameters = {
        roughness = 1
        emissive = false
        tint = [1, 0.5, 0.25, 1]
    }
}
```

`shader` is required. `textures` and `parameters` are optional non-empty dynamic
maps. Binding and parameter keys are ASCII identifiers. Texture values reference
`.cytex` resources. Parameters support booleans, scalar numbers, and two- to
four-component numeric vectors. Integer magnitudes are limited to the exact
integer range of the decoded `f64` view.

Material parameter compatibility with shader reflection is a cooker concern and
is deferred until the renderer exposes its first real shader interface contract.
The current compiler still validates every direct `.cyshader` and `.cytex`
recipe as a typed resource dependency before publishing a material.

## Cooked Envelope

All `_c` resources use `CYRS` container version 1:

```text
80-byte fixed header
ordered 64-byte chunk descriptors
optional alignment padding
payload chunks
```

The encoding is explicitly little-endian. The header records resource FourCC,
resource version, total file size, chunk-table location, flags, and optional
128-bit source/content hashes. Each chunk records a FourCC, codec, flags,
power-of-two alignment, file offset, stored and decoded sizes, and an optional
content hash.

The source hash identifies canonical source, dependencies, cooker version, and
relevant options. The file content hash covers every serialized byte after the
fixed header: chunk table, deterministic zero padding, and stored payloads. A
chunk hash identifies its decoded payload, allowing codec changes without
changing semantic identity. These fast hashes detect corruption and support
caches; they are not signatures and do not authenticate hostile content.

Readers reject unknown flags/codecs, unsupported container versions, invalid
hash presence, malformed alignment, overlaps, out-of-bounds ranges, unordered
chunks, and file-size disagreement before publishing output descriptors.

## Cooked Shader Version 2

Resource FourCC: `CYSH`

Extension: `.cyshader_c`

Cooked shader version 2 uses the `CYRS` envelope with one `SHMD` metadata chunk
followed by one `SHCD` code chunk per stage. Chunk and stage order is canonical:

```text
CYRS header and chunk table
SHMD: backend, program kind, language profile/version, flags, stage records
SHCD: vertex GLSL bytes
SHCD: fragment GLSL bytes
```

The format currently supports one OpenGL graphics program containing exactly
one vertex stage and one fragment stage, encoded as null-terminated UTF-8 GLSL.
The two stages are unique and sorted by their serialized stage ID. Compute,
geometry, and tessellation stages remain unfrozen until a real source contract,
cooker, and runtime consumer need them.

`SHMD` records the required GLSL profile/version and identifies each stage, code
format, flags, code-chunk index, and decoded byte count. This lets the runtime
reject an incompatible resource before invoking a graphics driver. Metadata and
code chunks are uncompressed in version 2,
carry decoded-content hashes, use deterministic zero padding, and occupy all
bytes through end-of-file without trailing data. The whole `CYRS` content hash
is required. Readers validate all of these invariants before returning borrowed
stage views.

The GLSL payload includes its null terminator so it can be passed to the first
OpenGL compilation path without copying. Embedded null bytes and malformed
UTF-8 are rejected. OpenGL object names and program binaries are never stored;
program binaries are tied to driver and device state and are not portable cooked
assets.

`CookedShader_Write` packages already prepared stage bytes. `CypherShaderCompiler`
owns CYKV parsing, exact schema and semantic validation, stage loading, define
application, glslang preprocessing/parsing/linking, source identity, structured
diagnostics, dependency reporting, and transactional output publication.
`CookedShader_Read` is the Common-side format reader; VFS ownership, resource
lifetime, and OpenGL compilation remain outside Common.

## Runtime Ownership And Preview Boundary

`Cypher::RenderResourceRuntime` registers the stable resource type names
`cypher.shader`, `cypher.texture`, and `cypher.material` with `CypherResource`.
Each loader reads through the provider-neutral VFS, enforces a per-format file
limit, owns the complete cooked file, validates it, and only then publishes a
typed view. Every string and byte range in that view remains valid while at least
one reference to its resource handle exists. Releasing the final reference makes
both the payload and all borrowed views invalid.

`Cypher::RenderPreview` is a separate Common contract. It accepts either a
retained runtime handle or a borrowed cooked in-memory block for an unsaved
Picasso document. The future renderer writes one caller-owned RGBA8 sRGB frame;
Qt images, OpenGL names, Vulkan handles, and renderer classes do not cross the
contract.

## Cooked Texture Version 1

Resource FourCC: `CYTX`

Extension: `.cytex_c`

Cooked texture version 1 uses one `TXMD` metadata chunk followed by one `TXDT`
chunk for every mip level:

```text
CYRS header and chunk table
TXMD: dimension, pixel format, usage, color space, extent, flags, mip records
TXDT: mip level 0 tightly packed pixels
TXDT: mip level 1 tightly packed pixels
...
```

Version 1 represents one 2D image with maximum dimensions of 16384 by 16384 and
up to 15 mip levels. Canonical pixel formats are `RGBA8_UNORM`, `RGBA8_SRGB`,
and little-endian `RGBA32_FLOAT`. PNG and JPEG imports become RGBA8. EXR imports
become finite RGBA32F. Total decoded image data is bounded to 512 MiB.

Generated color mips average sRGB channels in linear space, generated normal
mips decode and renormalize their XYZ vectors, and float mips average finite
components. Non-power-of-two levels partition the complete source footprint so
edge texels are not silently discarded. Sampler state is not serialized here.

Every mip has an independently validated content hash and a canonical 16-byte
alignment. Readers validate format/usage/color-space combinations, exact row
pitch and extent progression, chunk order, zero padding, hashes, and end-of-file
layout before returning borrowed pixel views.

## Cooked Material Version 1

Resource FourCC: `CYMT`

Extension: `.cymat_c`

Cooked material version 1 contains two chunks:

```text
CYRS header and chunk table
MTMD: flags, counts, string references, texture records, parameter records
MTST: canonical null-terminated UTF-8 string table
```

The payload stores one `.cyshader` resource reference, up to 32 named `.cytex`
references, and up to 64 named values. Values are boolean, scalar `f64`, or
two- to four-component `f64` vectors. Names use bounded ASCII identifiers and
the complete string table is limited to 64 KiB.

Writers sort texture bindings and parameters by name, normalize floating-point
zero, clear inactive fields, and emit one deterministic representation. Readers
reject duplicate or unsorted names, non-finite and non-canonical values, invalid
resource paths, malformed string references, nonzero padding, and hash damage
before publishing a borrowed material view. Lookup helpers use binary search on
the canonical name order.

Version 1 intentionally does not freeze render state, sampler descriptors,
shader permutations, or reflected binding layouts. Those fields enter a later
version only when the first renderer consumer defines their exact semantics.

## Completion Gate

A complete runtime renderer format eventually requires all of these:

1. source syntax and exact schema
2. typed semantic decoder
3. deterministic cooker and dependency tracking
4. versioned cooked payload layout
5. runtime loader through VFS and `CypherResource`
6. compatibility and migration policy
7. malformed-input, round-trip, determinism, and integration tests

Shader, texture, and material now satisfy items 1 through 4 and 7 for their
declared offline scope. They have source/cooked, malformed-input, determinism,
compiler-module, VFS dependency, and ResourceCompiler integration coverage.

Item 5 is the next runtime boundary: load `CYSH`, `CYTX`, and `CYMT` through VFS
and `CypherResource`, then hand validated views to an owning renderer. Current
compatibility policy is strict rejection of unknown CYKV schema, CYRS container,
resource, metadata, enum, and chunk versions. GLSL source versions 330 through
450 are supported according to target limits; this is separate from cooked-file
versioning. Older cooked layouts require an explicit reader or offline migration
tool and are never guessed from byte shape.
