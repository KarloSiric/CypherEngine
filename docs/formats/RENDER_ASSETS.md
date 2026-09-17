<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/RENDER_ASSETS.md
//  Purpose: Defines the versioned renderer-facing asset contracts.
//  Details: This document specifies shader, texture, and material CYKV source
//           schemas, cooked resources, canonical identities, compatibility, and
//           the exact limits of the current offline compilers.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//  - Expanded for V2 source schemas and current cooked contracts on 2026-09-17
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Renderer Asset Contracts

## Status

The render-asset family has two authored CYKV schema generations and a matching
set of versioned cooked contracts:

- `.cyshader` V1 and V2 decode and compile. V1 emits legacy `CYSH` V2; V2 emits
  current `CYSH` V3 with a typed logical interface.
- `.cytex` V1 and V2 decode and compile. Both current writer paths emit `CYTX`
  V2; the reader retains `CYTX` V1 compatibility.
- `.cymat` V1 decodes and compiles to `CYMT` V1. `.cymat` V2 resolves bounded
  inheritance, validates its V2 shader and texture dependencies, packs the
  typed material interface, and emits current `CYMT` V2.

These are offline and runtime-data contracts. Native texture objects, shader
program objects, descriptor or binding allocation, draw submission, and editor
widgets remain renderer or tool-owned concerns.

The GLSL payload in `CYSH` is the current OpenGL target payload. It does not
define a software-renderer material language and does not make backend handles
part of a persistent resource.

## Pipeline And Ownership

```text
CYKV source recipe
    -> CYKV syntax parse
    -> exact schema ID/version selection
    -> closed-schema validation
    -> typed semantic decode
    -> dependency loading through VFS
    -> deterministic offline compiler
    -> versioned CYRS resource
    -> VFS and CypherResource
    -> validated borrowed view backed by an owned file blob
    -> renderer-owned native object
```

Source documents describe author intent. Cooked files contain bounded runtime
data. Neither side stores OpenGL object names, Vulkan handles, native pointers,
host `sizeof` results, or compiler-dependent structure padding.

The source file's canonical virtual path is the resource identity. A recipe does
not repeat an asset name. The typed source views borrow the parsed CYKV document.
Cooked views borrow the complete validated file. Their owner must keep that
backing storage alive and unchanged.

## Version And Compatibility Matrix

### Source-to-cooked routes

| Source | Decoder | Compiler route | Cooked output | Current qualification |
| --- | --- | --- | --- | --- |
| `cypher.shader` V1 | Implemented | Implemented | `CYSH` V2 | Frozen compatibility route |
| `cypher.shader` V2 | Implemented | Implemented | `CYSH` V3 | Current typed-interface route; gates listed below |
| `cypher.texture` V1 | Implemented | Implemented | `CYTX` V2 | Frozen source behavior through current writer |
| `cypher.texture` V2 | Implemented | Implemented | `CYTX` V2 | Current subresource/streaming route; gates listed below |
| `cypher.material` V1 | Implemented | Implemented | `CYMT` V1 | Frozen compatibility route |
| `cypher.material` V2 | Implemented | Implemented | `CYMT` V2 | Current layered, typed-interface route; gates listed below |

### Cooked-reader compatibility

| Resource | Version | Read | Write | Meaning |
| --- | ---: | --- | --- | --- |
| `CYSH` | 2 | Yes | Yes, legacy API | Vertex/fragment GLSL and no logical interface |
| `CYSH` | 3 | Yes | Yes, current API | V2 stages plus canonical typed logical interface |
| `CYTX` | 1 | Yes | No current production writer | Legacy 2D mip-chain layout |
| `CYTX` | 2 | Yes | Yes, current | Explicit subresources, alpha, target, and residency |
| `CYMT` | 1 | Yes | Yes | Legacy shader/texture/value material |
| `CYMT` | 2 | Yes | Yes, Common contract | Resolved features, shader interface identity, UV state, and packed constants |

#### Internal metadata versions

| Resource version | Internal metadata version |
| --- | --- |
| `CYSH` V2 | `SHMD` V2 |
| `CYSH` V3 | `SHMD` V3 and `SHRF` V1 |
| `CYTX` V1 | `TXMD` V1 |
| `CYTX` V2 | `TXMD` V2 |
| `CYMT` V1 | `MTMD` V1 |
| `CYMT` V2 | `MTMD` V2 |

Readers dispatch on the resource version in the `CYRS` header. Unknown source
schema, container, resource, metadata, enum, flag, or chunk versions are rejected.
Readers never guess a version from byte shape.

### Public Common entry points

| Contract | V1/compatibility entry point | V2/current entry point | Shared reader or identity helper |
| --- | --- | --- | --- |
| Shader source | `RenderShaderSource_Decode` | `RenderShaderSourceV2_Decode` | `RenderShaderSchema_V1`, `RenderShaderSchema_V2` |
| Texture source | `RenderTextureSource_Decode` | `RenderTextureSourceV2_Decode` | `RenderTextureSchema_V1`, `RenderTextureSchema_V2` |
| Material source | `RenderMaterialSource_Decode` | `RenderMaterialSourceV2_Decode` | `RenderMaterialSchema_V1`, `RenderMaterialSchema_V2` |
| Cooked shader | `CookedShader_Write` | `CookedShader_WriteV3` | `CookedShader_Read`, `CookedShader_MakeLogicalBindingId`, `CookedShader_ComputeInterfaceHash` |
| Cooked texture | `CookedTexture_Write` for the compatibility 2D input API; it emits V2 | `CookedTexture_WriteSubresources` | `CookedTexture_Read`, `CookedTexture_GetSubresource` |
| Cooked material | `CookedMaterial_Write` | `CookedMaterial_WriteV2` | `CookedMaterial_Read`, `CookedMaterial_ComputeVariantHash` |

Every size-query/write pair uses the same canonicalization and bounds. Callers
must treat the reader result as unpublished until its status is successful.

## Shared Source Rules

- Every source document uses CYKV language version 1 and an exact schema ID and
  schema version.
- Source schema V1 remains frozen. New authored semantics enter a new schema
  version rather than silently changing V1 defaults.
- Resource paths are lowercase canonical virtual paths using `/`. They are
  relative, contain no empty, `.` or `..` components, and are at most 259 bytes.
- Logical names, defines, entries, presets, feature values, and streaming classes
  use bounded ASCII identifiers of at most 64 bytes where the field requires an
  identifier.
- Closed objects reject unknown members. Dynamic maps still validate every key,
  every value, count limit, duplicate, and cross-field rule.
- Source member order and insignificant CYKV formatting do not define semantic
  identity. Compilers use the canonical CYKV document hash.
- Schema validation checks shape and simple bounds. Typed decoders enforce path
  extensions, typed defaults, duplicate policy, and cross-field semantics.

## Shader Source

Extension: `.cyshader`

### Shader schema V1

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

`language`, `vertex`, and `fragment` are required. V1 supports desktop GLSL core
with one vertex stage and one fragment stage. Stage paths accept their specific
extension or `.glsl`. `defines` is optional and contains at most 64 unique ASCII
identifiers.

The V1 compiler route preprocesses, parses, and cross-stage-links the pair with
glslang, then emits `CYSH` V2. Its behavior remains frozen for existing content.

### Shader schema V2

This example uses only compiler-supported fields and produces `CYSH` V3:

```cykv
@cykv 1
@schema "cypher.shader" 2

{
    language = "glsl"

    stages = {
        vertex = {
            source = "shaders/world.vert"
            entry = "main"
        }
        fragment = {
            source = "shaders/world.frag"
        }
    }

    defines = ["CY_WORLD_PASS"]

    interface = {
        textures = {
            base_color = {
                type = "texture2d"
                usage = "color"
                color_space = "srgb"
                required = true
            }
            normal_map = {
                type = "texture2d"
                usage = "normal"
                color_space = "linear"
                required = false
            }
        }

        parameters = {
            roughness = {
                type = "f32"
                default = 0.6
                minimum = 0
                maximum = 1
            }
            tint = {
                type = "color4"
                default = [1, 0.5, 0.25, 1]
            }
            model = {
                type = "mat4"
                required = true
            }
        }
    }

    variant_budget = 8u
}
```

The `entry` member defaults to `main`. The schema can represent another entry
identifier, but the current compiler rejects it because `CYSH` V3 has no
versioned runtime entry-point field. Both authored entries must therefore be
`main`.

The V2 interface is a logical material-facing contract. Authors declare stable
names and types; they never author OpenGL locations, descriptor numbers, register
indices, or byte offsets.

| Interface category | Maximum | Supported authored types | Defaults and rules |
| --- | ---: | --- | --- |
| Textures | 32 | `texture2d`, `texture_cube`, `texture2d_array`, `texture3d` | `usage` is required; non-color data is linear; `required` defaults to true |
| Samplers | 16 | `filtering`, `comparison` | Optional identifier preset; `required` defaults to true |
| Parameters | 64 | `bool`, `i32`, `u32`, `f32`, `f32x2`, `f32x3`, `f32x4`, `color3`, `color4`, `mat3`, `mat4` | Typed optional default; `required` defaults to false |

Color textures default to sRGB when `color_space` is omitted; normal and data
textures default to linear and cannot request sRGB.

Parameter defaults must match the declared component count and scalar category.
`minimum` and `maximum` are valid only for `i32`, `u32`, and `f32`; an integer
bound must be integral and fit the declared type. Names are unique across
textures, samplers, and parameters, because one logical name maps to one stable
binding identity.

The V2 schema also defines bounded feature declarations:

```cykv
features = {
    normal_map = {
        type = "bool"
        mode = "static"
        default = false
    }
    quality = {
        type = "enum"
        mode = "static"
        values = ["low", "medium", "high"]
        default = "high"
    }
    fog = {
        type = "bool"
        mode = "dynamic"
        default = true
    }
}
variant_budget = 8u
```

There may be at most 32 features and 16 values per enum. Enum values are unique,
an enum must provide a default that names one of them, and the Cartesian count
of static values must not exceed `variant_budget`. Feature mode defaults to
`static`; a Boolean default defaults to false. The default variant budget is 64
and the maximum is 1024.

#### Current shader compiler gates

| Authored request | Current result | Reason |
| --- | --- | --- |
| Vertex and fragment entry `main` | Supported | Matches the fixed runtime entry contract |
| Any alternate entry | Rejected | `CYSH` V3 does not persist alternate entry names |
| No `features` object | Supported | One stage pair has one unambiguous identity |
| Any non-empty feature set, static or dynamic | Rejected after schema/budget validation | `CYSH` V3 has no variant table and the compiler must not pretend every permutation was cooked |
| Texture and parameter declarations | Supported in `CYSH` V3 | They become canonical logical bindings and must exactly match active linked GLSL resources |
| Independent sampler declarations | Rejected | The current material contract associates a sampler preset with a combined sampled-texture binding; a separate sampler-to-texture association is not versioned yet |
| Physical GLSL reflection cross-check | Required for V2 | glslang emits validation-only OpenGL-semantics SPIR-V and SPIRV-Cross reflects active resources after successful cross-stage linking |

V2 reflection is bidirectional. Every active combined sampled image must match an
`interface.textures` declaration, every active plain uniform must match an
`interface.parameters` declaration, and every authored texture or parameter must
be active in at least one linked stage. Names, binding kinds, value types, texture
dimensions, and array shapes must agree. A mismatch is a compile error and no
cooked artifact is published.

The current reflected material subset is deliberately closed:

- combined float `sampler2D`, `sampler2DArray`, `sampler3D`, and `samplerCube`;
- non-array `bool`, `int`, `uint`, `float`, `vec2`, `vec3`, `vec4`, `mat3`, and
  `mat4` plain uniforms.

Descriptor arrays, plain-uniform arrays, shadow samplers, multisample samplers,
integer samplers, unsupported image dimensions, and unsupported value shapes are
rejected. Uniform blocks, storage buffers, and storage images are outside the
authored material-interface comparison for this version, so engine-owned blocks
may coexist with an empty material interface.

Physical stage use is validation data. `CYSH` persists conservative
vertex-plus-fragment visibility for material bindings so the shader compiler and
material compiler reconstruct the same interface hash without depending on
driver reflection. The generated SPIR-V is not a runtime payload; `CYSH` continues
to store canonical preprocessed GLSL for the OpenGL runtime.

Each recipe define is injected as `#define NAME 1`. Quoted project-local `.glsl`
includes resolve through the source VFS, are canonicalized beneath the source
root, obey depth/file/byte limits, and are recorded once as transitive
dependencies. Absolute paths, root escapes, and `<system>` includes are rejected.

Each stage must declare one supported desktop core profile with
`#version NNN core`, and both stages must use the same version. Supported GLSL
versions are 330, 400, 410, 420, 430, 440, and 450. Apple targets are capped at
410; Windows and Linux desktop targets are capped at 450. Compatibility-profile
GLSL and GLSL ES are outside this contract.

Shader compiler version 5 includes the selected target and profile in a
versioned configuration hash. It also includes glslang identity, the required
`exceptions-v1` static-library build contract and, for V2, the SPIRV-Cross
public C API version plus the project's pinned vcpkg baseline. Those hashes
participate in `sourceHash`, and the compiler emits matching `CONFIGURATION`
and `TOOLCHAIN` dependencies. Target-specific GLSL ceilings, dependency ABI
changes, and reflection-toolchain upgrades therefore cannot alias through the
build cache.

## Texture Source

Extension: `.cytex`

Sampler state is absent from texture recipes and `CYTX`. Filtering, wrapping,
and comparison behavior belong to material/sampler state so the same cooked
image can be sampled in more than one way.

### Texture schema V1

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

V1 accepts PNG, JPEG, TGA, and finite RGBA EXR sources. `usage` is `color`,
`normal`, or `data`. Its defaults are color, sRGB, and generated mips. Normal,
data, and EXR input require linear color space. The current compatibility writer
converts this source contract to `CYTX` V2 while preserving V1 behavior.

### Texture schema V2

This example is accepted by the current compiler when the source image is large
enough to produce at least three mip levels:

```cykv
@cykv 1
@schema "cypher.texture" 2

{
    source = "textures/source/fence.png"
    type = "2d"
    usage = "color"
    color_space = "srgb"

    alpha = {
        mode = "mask"
        cutoff = 0.45
    }

    mips = {
        mode = "generate"
        filter = "box"
        edge = "clamp"
    }

    output = {
        format = "auto"
        quality = "production"
    }

    streaming = {
        class = "world"
        resident_mips = 3u
    }
}
```

`source`, `type`, `usage`, and `color_space` are required. V2 source recipes
currently expose only `type = "2d"`. The schema recognizes PNG, JPEG, TGA, EXR,
DDS, and KTX2 paths, while the compiler support matrix below is narrower.

Alpha modes are `none`, `straight`, `premultiplied`, `mask`, and `data`.
`cutoff` is in `[0, 1]` and is legal only for `mask`. `dilate_rgb` is legal only
for `straight` or `mask`. A non-color texture may use only `none` or `data`.
When `alpha` is absent, the V2 decoded policy is `none`, cutoff 0.5, with no RGB
dilation.

Mip modes are `generate`, `preserve`, and `none`. `filter`, `edge`, and
`preserve_alpha_coverage` are meaningful only with `generate`; coverage
preservation additionally requires mask alpha. `preserve` is semantically legal
only for DDS or KTX2 sources. With `mips.mode = "none"`, a streaming recipe may
request only one resident mip. An absent `mips` object means generated mips with
the box filter, clamped edges, and no alpha-coverage preservation.

Output formats are `auto` and `uncompressed`; quality is `fast`, `balanced`, or
`production`. Quality and policy remain part of source identity even when the
current output encoder selects the same uncompressed storage. An absent `output`
object means `auto` and `balanced`.

`streaming.class` is a stable scheduling class and `resident_mips` is the count
of coarsest mip levels that must be available when the resource is published.
It is bounded to 1 through 15 and must not exceed the produced mip chain. An
absent `streaming` object means fully resident output.

| Streaming class | Serialized priority |
| --- | ---: |
| `critical` | 255 |
| `ui` | 224 |
| `character` | 192 |
| `world` | 128 |
| `effects` | 96 |
| `background` | 32 |
| Any other valid identifier | 128 |

#### Current texture compiler support and gates

| Policy | Current result |
| --- | --- |
| PNG, JPEG, TGA | Imported as canonical RGBA8 |
| Finite RGBA EXR | Imported as canonical RGBA32F, linear only |
| `generate` with `box` and `clamp` | Supported |
| `none` mip mode | Supported |
| DDS, KTX2, or `preserve` | Rejected: preserved-container import is not implemented |
| `kaiser` or `lanczos` | Rejected: those filters are not implemented |
| `repeat` or `mirror` during mip generation | Rejected: only clamped edges are implemented |
| `preserve_alpha_coverage = true` | Rejected: coverage-preserving downsampling is not implemented |
| `dilate_rgb = true` | Rejected: transparent-pixel RGB dilation is not implemented |
| `auto` or `uncompressed` | Supported; both currently choose canonical uncompressed storage |
| Resident tail larger than the produced chain | Rejected as invalid streaming policy |

Texture compiler version 4 includes the selected target and profile in a
versioned configuration hash. That hash participates in `sourceHash`, and the
compiler emits the same identity as a `CONFIGURATION` dependency. Build caches
therefore cannot alias outputs cooked for different target/profile pairs. Its
toolchain identity also includes the pinned vcpkg baseline so a TinyEXR upgrade
cannot reuse EXR output decoded by another dependency revision.

The deterministic compiler flag covers repeated cooking with the same pinned
host build image and floating-point runtime. The current sRGB and normal-map
reducers use the host implementations of `std::pow` and `std::sqrt`; byte-for-byte
identity across different standard libraries, math libraries, CPUs, or floating-
point modes is not yet a qualified contract. A distributed cook farm must pin
that host image or place each build image in a separate cache namespace until
cross-architecture golden vectors or a versioned deterministic math contract are
added.

Generated sRGB color mips convert color channels to linear space before
filtering and encode the result back to sRGB. The box reducer uses exact
rational area weights, including for odd dimensions, so every destination texel
integrates its complete source footprint. Straight-alpha images are temporarily
premultiplied for filtering and safely unpremultiplied afterward. Both RGBA8 and
RGBA32F normal-map paths decode the stored normal, average it, and renormalize
the result; floating-point source components must also be finite. These rules
apply across non-power-of-two chains and include the final edge texels.
Before allocating any derived mip level, the compiler computes the exact decoded
chain footprint and rejects chains above the 512 MiB `CYTX` data ceiling with a
capacity diagnostic.

## Material Source

Extension: `.cymat`

### Material schema V1

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
maps. V1 parameters support Boolean, scalar `f64`, and two- through four-component
`f64` vectors. The V1 compiler validates direct shader and texture source
dependencies and emits `CYMT` V1. It does not use shader reflection.

### Material schema V2

The following is a schema-valid inheritance and override example. The V2
compiler implements the inheritance and override mechanics shown here. Fields
listed under the explicit compiler gates remain rejected until their dependent
runtime contracts exist.

```cykv
@cykv 1
@schema "cypher.material" 2

{
    base = "materials/templates/world_surface.cymat"
    shader = "shaders/world.cyshader"
    domain = "surface"
    surface = "surfaces/concrete.cysurface"

    features = {
        normal_map = true
        quality = "high"
        legacy_detail = null
    }

    state = {
        alpha_mode = "mask"
        alpha_cutoff = 0.5
        two_sided = false
        casts_shadows = true
        receives_shadows = true
    }

    textures = {
        base_color = {
            resource = "textures/wall/base_color.cytex"
            sampler = "linear_wrap"
            uv = {
                set = 1u
                scale = [2, 2]
                offset = [0.25, 0]
                rotation = 0.5
            }
        }
        normal_map = {
            sampler = "anisotropic_wrap"
        }
        old_mask = null
    }

    parameters = {
        enabled = true
        detail_layer = -7
        material_id = 7u
        roughness = 0.6
        transform = [1, 0, 0, 1, 0, 0, 0, 1, 0]
        obsolete = null
    }
}
```

A V2 material must name at least `base` or `shader`. `base` references another
`.cymat`; `shader` references `.cyshader`. A standalone material without `base`
must provide every active texture resource and cannot contain `null` removals.
With a base, omitted fields inherit and a `null` feature, texture, or parameter
removes the inherited member. Cooked `CYMT` V2 stores the resolved result; it
contains no inheritance operations or removal markers.

Domains are `surface`, `decal`, `ui`, `postprocess`, and `particle`. State alpha
modes are `opaque`, `mask`, `blend`, and `additive`. `alpha_cutoff` is meaningful
only for mask state. Two-sided defaults false; casts-shadows and receives-shadows
default true when the resolved material does not inherit another value. The
optional `.cysurface` reference is semantically valid only for the `surface` and
`decal` domains, independently of the current compiler gate on that format.

Texture overrides may replace a `.cytex` resource, a sampler preset, UV state,
or any combination allowed by inheritance. UV sets range from 0 through 7 and
carry scale, offset, and rotation. New UV state defaults to set 0, scale `[1, 1]`,
offset `[0, 0]`, and zero rotation. Parameter values are Boolean, signed integer,
unsigned integer, floating point, or numeric arrays of 2 through 16 components;
the material compiler must match them to the declared `CYSH` V3 type. A recipe
is bounded to 32 feature operations, 32 texture operations, and 64 parameter
operations.

#### Current material compiler behavior and gates

The V2 compiler resolves up to 16 material layers, detects canonical-path
cycles, applies deepest-base-to-derived overrides, and records direct and
transitive base dependencies. Texture, parameter, and feature entries support
`null` removal; texture overrides may replace only selected resource, sampler,
or UV fields. Every material in a V2 chain, and every referenced shader and
texture recipe, must itself use schema V2.

After resolution, the compiler enforces these cross-format limits:

- Non-empty material feature overrides are rejected until `CYSH` gains a
  versioned variant table. `CYMT` V2 can persist resolved features and their
  variant hash, but current `CYSH` V3 identifies only one stage pair.
- `.cysurface` is a reserved cooked-path field. It is rejected until the surface
  format and its dependency contract exist.
- A material texture's `sampler` is a preset for that combined sampled-texture
  binding. It does not name or associate a separate `CYSH` `SAMPLER` record.
  Although the generic `CYSH` V3 record model can describe an independent
  sampler, the current V2 shader and material compilers reject independent
  samplers until a sampler-to-texture association is versioned end to end.
- Every resolved texture and parameter name/ID/type must match the referenced
  `CYSH` V3 interface, and the material embeds that exact interface hash.
- Required shader bindings must resolve to a material value; optional parameters
  use shader defaults when available and otherwise remain absent.
- Integer conversion is range checked, floating-point values must be finite and
  representable, vectors and matrices require exact component counts, and
  declared shader ranges are enforced.
- Referenced texture recipes must describe 2D V2 textures whose usage and color
  space exactly match the shader declaration.

## CYRS Cooked Envelope

All `_c` resources use `CYRS` container version 1:

```text
80-byte fixed header
ordered 64-byte chunk descriptors
deterministic zero alignment padding
payload chunks
```

Encoding is explicitly little-endian. The header records resource FourCC,
resource version, total file size, chunk-table location, flags, and optional
128-bit source and content hashes. Each chunk records a FourCC, codec, flags,
power-of-two alignment, file offset, stored and decoded sizes, and an optional
decoded-content hash.

The whole-file content hash covers every serialized byte after the fixed header:
the chunk table, zero padding, and payloads. A chunk hash identifies its decoded
payload, so a later storage codec can change without redefining semantic chunk
identity. These hashes detect corruption and support deterministic caches; they
are not cryptographic signatures for hostile input.

Readers reject unknown flags or codecs, malformed alignment, overlaps,
out-of-bounds ranges, unordered chunks, unexpected trailing bytes, nonzero
padding, file-size disagreement, and required hash mismatches before publishing
a view. Writers calculate the required size first and publish a complete
canonical output transactionally.

## Canonical Identities

The asset family uses several identities for different purposes:

| Identity | Scope | Canonical input |
| --- | --- | --- |
| Virtual resource path | Project resource | Lowercase canonical path of the source recipe |
| Source hash | One compiler output | Canonical recipe, compiler/schema/cooked versions, direct and transitive dependency content, toolchain identity, and any target/options explicitly included by that compiler |
| Logical binding ID | One named shader interface member | Stable domain-tagged 64-bit hash of the exact validated logical name |
| Shader interface hash | Complete `CYSH` V3 logical ABI | Canonical name-sorted logical binding records plus canonical string table |
| Material variant hash | Complete resolved feature set | Canonical name-sorted Boolean/enum feature records |
| Chunk content hash | One decoded chunk | Exact decoded payload bytes |
| CYRS content hash | One complete cooked file | All bytes after the fixed CYRS header |

`CookedShader_MakeLogicalBindingId` is the normative name-to-ID operation.
Writers recompute IDs and reject zero, mismatches, and collisions. Record order
never assigns identity. This allows shader and material compilers to reproduce
the same binding ID independently.

`CookedShader_ComputeInterfaceHash` computes the exact shader interface identity
without serializing a complete shader file. `CYMT` V2 embeds that value, allowing
a loader or compiler to reject a material built for another shader interface.

The interface hash is an ABI identity: it covers logical names, IDs, kinds,
types, array shape, visibility, flags, and constant-storage layout. Authoring
defaults/ranges and texture usage/color-space policy do not change that ABI.
They remain part of canonical shader/material source identity and emitted
dependency hashes, so the build, package, and hot-reload layers must recook or
reject a material whose semantic shader dependency is stale even when the ABI
is unchanged. A separate persisted semantic material-contract hash should be
added only if products are allowed to mix independently built artifact
generations.

Changing a compiler's source-to-cooked semantics requires a compiler version
bump. Compiler identity includes the actual source schema and cooked resource
version, so V1 compatibility and V2 current routes cannot alias in a cache.

## Cooked Shader

Resource FourCC: `CYSH`

Extension: `.cyshader_c`

### `CYSH` V2 compatibility layout

```text
SHMD, alignment 8: backend, program kind, language profile/version, stage records
SHCD, alignment 4: vertex GLSL bytes
SHCD, alignment 4: fragment GLSL bytes
```

V2 stores one OpenGL graphics program with exactly one vertex and one fragment
stage. Code is canonical, valid, null-terminated UTF-8 desktop GLSL. It contains
no logical interface. The current reader retains V2 support, and the V1 shader
source compiler continues to write it. Its `SHMD` payload starts with a 40-byte
header and uses one 24-byte record per stage.

### `CYSH` V3 current layout

```text
SHMD, alignment 8: V3 program metadata, interface hash, stage records
SHRF, alignment 8: canonical fixed-size logical binding records
SHST, alignment 1: canonical NUL-terminated logical-name strings
SHCD, alignment 4: vertex GLSL bytes
SHCD, alignment 4: fragment GLSL bytes
```

V3 retains the V2 stage contract and adds a backend-neutral logical interface.
Its `SHMD` payload starts with a 72-byte header and retains the 24-byte stage
record. `SHRF` starts with a 32-byte header and uses one 56-byte record per
binding. The fixed limits are two stages, 128 bindings, 1,024 array elements per
binding, 127 bytes per binding name, a 64 KiB string table, and 16 MiB of code
per stage.

Each binding record contains:

- logical name and stable nonzero 64-bit logical binding ID
- binding kind: value, sampled texture, sampler, uniform buffer, storage buffer,
  storage image, vertex input, or fragment output
- value type or resource type, as required by the kind
- array element count
- vertex/fragment stage visibility mask
- required, material, instance, and read-only flags
- logical byte offset and occupied storage size

The format can represent more binding kinds and resource shapes than the current
source compiler emits. Shader source V2 currently emits material `VALUE` and
`SAMPLED_TEXTURE` records; independent authored samplers are rejected until their
association contract exists. Opaque resources have zero byte size; parameters
use the deterministic material-constant packing policy below.

The writer sorts bindings by name, builds one canonical string table, requires
unique names and IDs, recomputes every ID, computes the interface hash, zeros all
padding, and writes hashed chunks. The reader requires that exact ordering and
identity. Input declaration order therefore cannot change cooked bytes.

### Material-constant packing

Parameters are sorted by logical name before offset assignment. Each offset is
aligned from the end of the previous parameter. `cbByteSize` is the occupied
storage extent, including type-defined padding. `cbValue` in `CYMT` is the active
typed byte count.

| Authored shader type | Cooked value type | Value bytes | Alignment | Storage bytes |
| --- | --- | ---: | ---: | ---: |
| `bool` | `BOOL` | 4 | 4 | 4 |
| `i32` | `I32` | 4 | 4 | 4 |
| `u32` | `U32` | 4 | 4 | 4 |
| `f32` | `F32` | 4 | 4 | 4 |
| `f32x2` | `F32X2` | 8 | 8 | 8 |
| `f32x3`, `color3` | `F32X3` | 12 | 16 | 16 |
| `f32x4`, `color4` | `F32X4` | 16 | 16 | 16 |
| `mat3` | `F32X3X3` | 36 | 16 | 48 |
| `mat4` | `F32X4X4` | 64 | 16 | 64 |

`mat3` uses three padded four-component columns. All inter-value gaps, vector
tails, matrix column padding, and trailing constant-storage bytes are zero.

The shared `CookedShader_ValueTypeValueSize`,
`CookedShader_ValueTypeStorageAlignment`, and
`CookedShader_ValueTypeStorageSize` functions define this policy for both
shader and material compilers.

## Cooked Texture

Resource FourCC: `CYTX`

Extension: `.cytex_c`

### `CYTX` V1 compatibility layout

V1 contains one `TXMD` descriptor and one `TXDT` per mip for a single 2D image.
The reader maps it into the current view as frame 0, layer 0, face 0. New writes
use V2. The compatibility metadata layout has a 64-byte header and one 32-byte
record per mip.

### `CYTX` V2 current layout

```text
TXMD, alignment 8: 128-byte header followed by 64-byte subresource records
TXDT, alignment 16: independently hashed subresource 0
TXDT, alignment 16: independently hashed subresource 1
...
```

The canonical subresource order is mip, then frame, then layer, then face. With
zero-based indices, the record index is:

```text
(((mip * frame_count) + frame) * layer_count + layer) * face_count + face
```

Mip 0 is the sharpest level. Coarser mips have increasing indices, so a streamed
coarse resident tail is contiguous at the end of the descriptor and payload
order.

The V2 descriptor persists:

- dimension and stable `render_format_t` storage format
- semantic usage, color space, alpha mode, and mask cutoff
- target profile: portable, desktop, Apple, mobile, or web
- residency mode: fully resident or mip streamed
- width, height, depth, layer, face, frame, mip, and subresource counts
- generated-mip flag and 0 through 255 streaming priority
- resident mip count plus derived resident-first index, resident subresource
  count, resident bytes, and total bytes

The target profile comes from the tool compile request rather than an authored
CYKV member.

Each subresource persists its four coordinates, extent, row pitch, slice pitch,
data chunk index, and encoded byte count. Records and chunks must be in exact
canonical order. Each `TXDT` has its own content hash, which allows validation
and future independent streaming.

The cooked contract represents 1D, 2D, 3D, and cube dimensions, multiple layers,
faces, frames, and block-compressed storage formats. The current source compiler
emits one 2D image with one layer, face, and frame. It currently chooses
`RGBA8_SRGB`, `RGBA8_UNORM`, or `RGBA32_FLOAT`; compression remains a later
encoder concern even though `CYTX` V2 can validate block layouts.

Limits are 16,384 texels per dimension, 15 mip levels, 256 layers, 256 frames,
six cube faces, 1,024 total subresources, and 512 MiB total encoded data.
Sizing, writing, and reading stream the maximum-size descriptor/chunk tables in
bounded passes; the 1,024-subresource limit does not require a maximum-sized
table on an engine worker stack.

Alpha is semantic rather than inferred from channel count:

| Cooked alpha mode | Meaning |
| --- | --- |
| `none` | Alpha has no material meaning |
| `straight` | RGB is unassociated with alpha |
| `premultiplied` | RGB is already multiplied by alpha |
| `mask` | Alpha is coverage tested against the stored cutoff |
| `data` | Alpha is an independent data channel |

## Cooked Material

Resource FourCC: `CYMT`

Extension: `.cymat_c`

### `CYMT` V1 compatibility layout

```text
MTMD, alignment 8: shader path references, texture records, legacy value records
MTST, alignment 1: canonical NUL-terminated string table
```

V1 stores one `.cyshader` path, up to 32 named `.cytex` paths, and up to 64
Boolean/scalar/vector values. Writers sort texture bindings and parameters by
name, normalize floating-point zero, clear inactive fields, and emit one
deterministic representation. Its `MTMD` payload uses a 48-byte header, 16-byte
texture records, and 48-byte parameter records.

### `CYMT` V2 current contract

```text
MTMD, alignment 8: 128-byte header plus feature, texture, and parameter records
MTCD, alignment 16: optional packed little-endian constants when parameters exist
MTST, alignment 1: canonical NUL-terminated string table
```

The V2 reader, writer, and material compiler route are implemented and tested.
The compiler gates listed above define the current supported source subset.

`MTMD` persists:

- the resolved `.cyshader` path and optional reserved `.cysurface` path
- required nonzero `CYSH` V3 interface hash and a feature-derived variant hash
- material domain, alpha mode/cutoff, two-sided, casts-shadows, and
  receives-shadows state
- canonical resolved Boolean/enum features
- texture binding name, `.cytex` path, optional combined-binding sampler preset,
  stable logical ID, UV set, scale, offset, and rotation
- parameter name, stable logical ID, reflected shader value type, reflected byte
  offset, natural value size, and padded storage size

The metadata layout uses a 128-byte header, 32-byte feature records, 64-byte
texture records, and 40-byte parameter records. It is bounded to 32 features,
32 textures, and 64 parameters.

Features, textures, and parameters are sorted by name. Logical IDs are recomputed
from those names and are unique across the texture/parameter binding space.
Cooked features contain resolved values only; inheritance and `null` removal are
source operations.

`MTCD` stores active values at the exact offsets declared by the matching
`CYSH` V3 interface. Values are little-endian. Every unoccupied gap and every
storage-padding byte is zero. The chunk is omitted when there are no parameters.
The complete constant payload is bounded to 64 KiB, and the string table is also
bounded to 64 KiB.

The shader interface hash prevents a material from being paired with a
same-named shader whose logical layout changed. The variant hash identifies the
canonical resolved feature set, although current compilers gate non-empty
features until a shader variant table exists.

## Runtime Boundary

`Cypher::RenderResourceRuntime` registers the stable resource type names
`cypher.shader`, `cypher.texture`, and `cypher.material` with `CypherResource`.
Loaders read through the provider-neutral VFS, enforce per-format file limits,
own the complete cooked file, validate it, and publish a typed borrowed view only
after success. Releasing the final resource reference invalidates every borrowed
string and byte range from that view.

`Cypher::RenderPreview` is a separate Common contract. It accepts a retained
runtime handle or a borrowed cooked in-memory block for an unsaved editor
document. Qt images, native renderer handles, and backend classes do not cross
the format boundary.

## Principles Adopted From Source

Source is a design reference, not a binary-layout template. Valve's published
[VTF contract](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/vtf/vtf.h)
demonstrates a versioned compiled texture carrying mip/frame/face data and
processing semantics. Its
[material interface](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/materialsystem/imaterial.h)
keeps named material variables and shader selection above native texture
objects. Published Source
[shader declarations](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/materialsystem/stdshaders/vertexlitgeneric_dx9.cpp)
show typed, named parameters with defaults, while the
[shader API boundary](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/public/shaderapi/ishaderapi.h)
keeps those material concepts separate from backend command submission.

Cypher adopts the durable principles behind those boundaries:

- author-friendly named material data is separate from target-ready cooked
  texture and shader data;
- expensive validation, preprocessing, mip generation, and dependency discovery
  happen offline;
- a material refers to a shader through logical names and typed parameters;
- texture image storage is reusable independently of material sampling state;
- persistent formats are explicitly versioned and runtime loaders validate them
  before creating native objects.

Cypher's CYKV schemas, CYRS container, FourCCs, chunk layouts, hashes, logical
binding IDs, packing rules, and compatibility policy are independent designs.
No Source code or serialized layout is copied. Cypher additionally makes
canonical ordering, size limits, zero padding, content identity, and
transactional publication explicit so determinism and malformed-input behavior
are part of the contract rather than implementation accidents.

## Completion Criteria

A render asset generation is complete only when it has:

1. exact source syntax and schema version;
2. typed semantic decoder and cross-field rules;
3. deterministic compiler and dependency tracking;
4. versioned cooked payload with canonical identity;
5. strict compatibility reader and migration policy;
6. VFS and `CypherResource` ownership integration;
7. renderer consumption with explicit lifetime and failure behavior;
8. malformed-input, round-trip, determinism, compiler, and integration tests.

The current shader, texture, and material generations satisfy the offline items
for their documented supported subsets. Renderer now creates generation-checked
OpenGL programs from validated `CYSH` views and immutable RGBA8 2D textures for
the first sampled draw path. Tile-map preview code resolves a bounded
base-color/tint/UV subset from cooked materials. General `CYTX` subresource
upload and streaming, dependency-owned runtime material objects, variant
selection, and complete `CYMT` binding remain later renderer/resource steps.
