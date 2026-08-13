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

Version 1 source schemas and typed decoders are implemented for `.cyshader`,
`.cytex`, and `.cymat`. The common cooked-resource envelope, first cooked shader
payload, deterministic shader compiler, and ResourceCompiler CLI path are
implemented. The VFS-backed runtime shader loader, OpenGL resource consumer,
and texture/material payloads are not yet implemented.

These contracts are the first vertical format slice. They exist to support a
real shader resource moving through VFS, `CypherResource`, and the OpenGL
renderer. They are not a claim that every future renderer feature is known.

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
    -> renderer-owned GPU object
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
then preserves canonical preprocessed GLSL. It records recipe, stage, compiler,
and toolchain dependencies in the tool host. Version 1 rejects source includes
until canonical include resolution can record the complete dependency graph. A future Vulkan
path may introduce a separate SPIR-V variant, but version 1 deliberately freezes
only the backend that has a real runtime consumer. The source contract does not
choose a GPU backend.

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

`source` is required and currently accepts PNG, JPEG, EXR, KTX, or KTX2 virtual
paths. `usage` is `color`, `normal`, or `data`. The defaults are `color`, `srgb`,
and mip generation enabled. Normal and data textures default to linear and
cannot request sRGB.

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
is deferred until the renderer exposes its first real shader reflection contract.

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

## Cooked Shader Version 1

Resource FourCC: `CYSH`

Extension: `.cyshader_c`

Cooked shader version 1 uses the `CYRS` envelope with one `SHMD` metadata chunk
followed by one `SHCD` code chunk per stage. Chunk and stage order is canonical:

```text
CYRS header and chunk table
SHMD: shader backend, program kind, flags, ordered stage records
SHCD: vertex GLSL bytes
SHCD: fragment GLSL bytes
```

The format currently supports one OpenGL graphics program containing exactly
one vertex stage and one fragment stage, encoded as null-terminated UTF-8 GLSL.
The two stages are unique and sorted by their serialized stage ID. Compute,
geometry, and tessellation stages remain unfrozen until a real source contract,
cooker, and runtime consumer need them.

`SHMD` records identify each stage, code format, flags, code-chunk index, and
decoded byte count. Metadata and code chunks are uncompressed in version 1,
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

Texture and material payload chunks remain undefined until their shader-based
runtime consumer establishes the data it actually needs.

## Completion Gate

A renderer format is complete only when all of these exist:

1. source syntax and exact schema
2. typed semantic decoder
3. deterministic cooker and dependency tracking
4. versioned cooked payload layout
5. runtime loader through VFS and `CypherResource`
6. compatibility and migration policy
7. malformed-input, round-trip, determinism, and integration tests

The shader format now satisfies items 1 through 4 and has source/cooked,
malformed-input, determinism, compiler-module, and executable-level integration
tests. The next work is item 5: a VFS-backed `CYSH` resource loader followed by
the OpenGL consumer. Compatibility policy remains version-1 rejection of unknown
schema, container, resource, metadata, and stage encodings until a migration is
explicitly implemented.
