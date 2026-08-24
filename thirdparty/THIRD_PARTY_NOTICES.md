<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: thirdparty/THIRD_PARTY_NOTICES.md
//  Purpose: Tracks third-party provenance and distribution obligations.
//  Details: This inventory is an engineering aid, not a replacement for the license
//           text shipped by each dependency. Release packaging must regenerate and
//           verify notices for the exact dependency graph being distributed.
//
//  History:
//  - Created by Karlo Siric on 2026-08-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Third-Party Notices

## Vendored source inventory

| Dependency | Pinned revision | License location |
| --- | --- | --- |
| GLAD 2.0.8 generated loader | OpenGL 4.1 core snapshot | Generated file headers |
| Dear ImGui | `b48d1afbe8ee8b238e2961dc363a949dd7304e23` | `imgui/LICENSE.txt` |
| cgltf | `85cd62382dfea638278962690cf515023f33ed00` | `cgltf/LICENSE` |
| MikkTSpace | `3e895b49d05ea07e4c2133156cfa94369e19e409` | Notice in `mikktspace.h` |
| Lucide icon subset | `86eb89c794cb941bf24eb16d5ef432faabaac7be` | `../src/CypherTools/Picasso/Resources/Icons/LUCIDE_LICENSE.txt` |
| Material Symbols Rounded subset | `e083cc60a0828fdd3b404cea0cb8a5b900e9c23e` | `../src/CypherTools/Picasso/Resources/Icons/material/APACHE_LICENSE.txt` |

## vcpkg-managed inventory

Versions are resolved from the `builtin-baseline` in `vcpkg.json`. vcpkg installs
the exact upstream copyright text beside each package under
`vcpkg_installed/<triplet>/share/<port>/copyright`.

| Feature | Dependencies | License class |
| --- | --- | --- |
| Platform | SDL3 | zlib |
| Tests | Catch2 | BSL-1.0 |
| Benchmarks | Google Benchmark | Apache-2.0 |
| Math and scripting | GLM, Lua | MIT |
| Profiling | Tracy | BSD-3-Clause |
| Compression | LZ4, Zstd, xxHash | permissive BSD-family |
| Security | libsodium | ISC |
| Text | FreeType, HarfBuzz | FTL/GPL dual; MIT |
| Image import | libpng, libjpeg-turbo, TinyEXR | permissive |
| Texture pipeline | KTX-Software/Basis Universal | Apache-2.0 and bundled notices |
| Mesh pipeline | meshoptimizer, Assimp | MIT; BSD-3-Clause |
| Audio | OpenAL Soft, libsndfile, Opus, opusfile | LGPL; LGPL; BSD-family |
| Archive tools | libzip | BSD-3-Clause |
| Networking | curl, GameNetworkingSockets | curl license; BSD-3-Clause |
| Shader tools | shaderc, glslang, SPIRV-Tools, SPIRV-Cross | Apache/BSD-family |
| Editor support | SQLite, nativefiledialog-extended | public domain; zlib |

OpenAL Soft and libsndfile require deliberate LGPL-compliant distribution.
Shipping proprietary targets should dynamically link them and include the
applicable notices and license texts unless legal review approves another route.

## External SDK inventory

| SDK | Policy |
| --- | --- |
| Qt 6 | Use LGPLv3-compliant dynamic deployment or a commercial Qt license. Review every selected module. |
| FMOD | Optional game integration only under an applicable FMOD license. Never redistribute the SDK as part of CypherEngine. |
| Vulkan SDK | Install externally. Package only the redistributable components required by an enabled Vulkan backend. |

Before release, generate the final notice bundle from the actual linked package
graph. Declaring a dependency here does not by itself authorize redistribution.
