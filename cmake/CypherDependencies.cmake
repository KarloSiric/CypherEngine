# //////////////////////////////////////////////////////////////////////////
# //
# //  CypherEngine Source Code
# //  Copyright (c) 2026 Karlo Siric. All rights reserved.
# //
# //  File: cmake/CypherDependencies.cmake
# //  Purpose: Owns third-party discovery and dependency-facing CMake targets.
# //  Details: Engine targets consume Cypher-owned dependency targets instead of
# //           scattering package names and source locations across the build graph.
# //           Optional packages are discovered only by the subsystem that owns them.
# //
# //  History:
# //  - Created by Karlo Siric on 2026-08-03
# //
# //  This file is proprietary and confidential. See LICENSE for details.
# //
# //////////////////////////////////////////////////////////////////////////

include_guard(GLOBAL)

function(cypher_configure_vendored_dependencies)
    set(cypher_imgui_dir "${CYPHERENGINE_THIRDPARTY_DIR}/imgui")
    if (EXISTS "${cypher_imgui_dir}/imgui.cpp")
        add_library(CypherThirdPartyImGui STATIC EXCLUDE_FROM_ALL
            "${cypher_imgui_dir}/imgui.cpp"
            "${cypher_imgui_dir}/imgui_draw.cpp"
            "${cypher_imgui_dir}/imgui_tables.cpp"
            "${cypher_imgui_dir}/imgui_widgets.cpp"
        )
        add_library(Cypher::ThirdPartyImGui ALIAS CypherThirdPartyImGui)
        target_include_directories(CypherThirdPartyImGui PUBLIC "${cypher_imgui_dir}")
    else()
        message(STATUS "Dear ImGui submodule is not initialized; development UI target is unavailable.")
    endif()

    set(cypher_cgltf_dir "${CYPHERENGINE_THIRDPARTY_DIR}/cgltf")
    if (EXISTS "${cypher_cgltf_dir}/cgltf.h")
        add_library(CypherThirdPartyCgltf INTERFACE)
        add_library(Cypher::ThirdPartyCgltf ALIAS CypherThirdPartyCgltf)
        target_include_directories(CypherThirdPartyCgltf INTERFACE "${cypher_cgltf_dir}")
    else()
        message(STATUS "cgltf submodule is not initialized; focused glTF import is unavailable.")
    endif()

    set(cypher_mikktspace_dir "${CYPHERENGINE_THIRDPARTY_DIR}/mikktspace")
    if (EXISTS "${cypher_mikktspace_dir}/mikktspace.c")
        add_library(CypherThirdPartyMikkTSpace STATIC EXCLUDE_FROM_ALL
            "${cypher_mikktspace_dir}/mikktspace.c"
        )
        add_library(Cypher::ThirdPartyMikkTSpace ALIAS CypherThirdPartyMikkTSpace)
        target_include_directories(CypherThirdPartyMikkTSpace PUBLIC "${cypher_mikktspace_dir}")
    else()
        message(STATUS "MikkTSpace submodule is not initialized; tangent generation target is unavailable.")
    endif()
endfunction()

function(cypher_configure_runtime_dependencies out_link_libraries)
    set(cypher_runtime_libraries "")

    if (CMAKE_DL_LIBS)
        list(APPEND cypher_runtime_libraries ${CMAKE_DL_LIBS})
    endif()

    set(cypher_glad_source "${CYPHERENGINE_THIRDPARTY_DIR}/glad/src/gl.c")
    set(cypher_glad_include_dir "${CYPHERENGINE_THIRDPARTY_DIR}/glad/include")
    if (NOT EXISTS "${cypher_glad_source}" OR
        NOT EXISTS "${cypher_glad_include_dir}/glad/gl.h")
        message(FATAL_ERROR
            "The generated GLAD source is missing from thirdparty/glad. "
            "Restore the vendored loader before configuring the OpenGL runtime."
        )
    endif()

    add_library(CypherThirdPartyGlad STATIC "${cypher_glad_source}")
    add_library(Cypher::ThirdPartyGlad ALIAS CypherThirdPartyGlad)
    target_include_directories(CypherThirdPartyGlad PUBLIC "${cypher_glad_include_dir}")
    list(APPEND cypher_runtime_libraries Cypher::ThirdPartyGlad)

    find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3)
    add_library(CypherThirdPartyPlatform INTERFACE)
    add_library(Cypher::ThirdPartyPlatform ALIAS CypherThirdPartyPlatform)
    target_link_libraries(CypherThirdPartyPlatform INTERFACE SDL3::SDL3)
    list(APPEND cypher_runtime_libraries Cypher::ThirdPartyPlatform)

    cypher_configure_vendored_dependencies()

    set(${out_link_libraries} "${cypher_runtime_libraries}" PARENT_SCOPE)
endfunction()

function(cypher_configure_common_tier1_dependencies out_link_libraries)
    find_package(lz4 CONFIG REQUIRED)
    find_package(xxHash CONFIG REQUIRED)
    find_package(zstd CONFIG REQUIRED)

    if (NOT TARGET CypherThirdPartyHash)
        add_library(CypherThirdPartyHash INTERFACE)
        add_library(Cypher::ThirdPartyHash ALIAS CypherThirdPartyHash)
        target_link_libraries(CypherThirdPartyHash INTERFACE xxHash::xxhash)
    endif()

    if (NOT TARGET CypherThirdPartyCompression)
        add_library(CypherThirdPartyCompression INTERFACE)
        add_library(Cypher::ThirdPartyCompression ALIAS CypherThirdPartyCompression)
        target_link_libraries(
            CypherThirdPartyCompression
            INTERFACE
                lz4::lz4
                zstd::libzstd
        )
    endif()

    set(
        ${out_link_libraries}
        Cypher::ThirdPartyCompression
        Cypher::ThirdPartyHash
        PARENT_SCOPE
    )
endfunction()

function(cypher_configure_security_dependencies out_link_libraries)
    find_package(unofficial-sodium CONFIG REQUIRED)

    if (NOT TARGET CypherThirdPartySecurity)
        add_library(CypherThirdPartySecurity INTERFACE)
        add_library(Cypher::ThirdPartySecurity ALIAS CypherThirdPartySecurity)
        target_link_libraries(
            CypherThirdPartySecurity
            INTERFACE unofficial-sodium::sodium
        )
    endif()

    set(${out_link_libraries} Cypher::ThirdPartySecurity PARENT_SCOPE)
endfunction()

function(cypher_configure_shader_tool_dependencies out_link_libraries)
    find_package(glslang CONFIG REQUIRED)

    if (NOT TARGET CypherThirdPartyShaderCompiler)
        add_library(CypherThirdPartyShaderCompiler INTERFACE)
        add_library(Cypher::ThirdPartyShaderCompiler ALIAS CypherThirdPartyShaderCompiler)
        target_link_libraries(
            CypherThirdPartyShaderCompiler
            INTERFACE
                glslang::glslang
                glslang::glslang-default-resource-limits
        )
    endif()

    set(${out_link_libraries} Cypher::ThirdPartyShaderCompiler PARENT_SCOPE)
endfunction()

function(cypher_configure_texture_tool_dependencies out_link_libraries)
    find_package(PNG CONFIG REQUIRED)
    find_package(libjpeg-turbo CONFIG REQUIRED)
    find_package(tinyexr CONFIG REQUIRED)

    if (NOT TARGET CypherThirdPartyImageImport)
        add_library(CypherThirdPartyImageImport INTERFACE)
        add_library(Cypher::ThirdPartyImageImport ALIAS CypherThirdPartyImageImport)
        target_link_libraries(
            CypherThirdPartyImageImport
            INTERFACE
                PNG::PNG
                libjpeg-turbo::turbojpeg-static
                unofficial::tinyexr::tinyexr
        )
    endif()

    set(${out_link_libraries} Cypher::ThirdPartyImageImport PARENT_SCOPE)
endfunction()

function(cypher_require_test_dependencies)
    find_package(Catch2 3 CONFIG REQUIRED)
    set(Catch2_FOUND TRUE PARENT_SCOPE)
endfunction()

function(cypher_require_benchmark_dependencies)
    find_package(benchmark CONFIG REQUIRED)
    set(benchmark_FOUND TRUE PARENT_SCOPE)
endfunction()
