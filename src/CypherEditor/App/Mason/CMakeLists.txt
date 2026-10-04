##########################################################################
#
#  CypherEngine Source Code
#  Copyright (c) 2026 Karlo Siric. All rights reserved.
#
#  File: src/CypherEditor/App/Mason/CMakeLists.txt
#  Purpose: Builds Mason, the CypherEngine level editor (ADR 0008).
#  Details: The window is a library so the smoke tests build exactly what
#           users run, on the offscreen platform. Mason is the only target
#           that links every workspace; nothing links Mason.
#
##########################################################################

add_library(CypherMasonApp STATIC
    CypherMason_Branding.cpp
    CypherMason_Branding.h
    CypherMason_MainWindow.cpp
    CypherMason_MainWindow.h
    CypherMason_RecentFiles.cpp
    CypherMason_RecentFiles.h
    CypherMason_Welcome.cpp
    CypherMason_Welcome.h
    CypherMason_WorkspaceState.cpp
    CypherMason_WorkspaceState.h
)
add_library(Cypher::MasonApp ALIAS CypherMasonApp)

target_compile_features(CypherMasonApp PUBLIC cxx_std_20)
target_compile_definitions(CypherMasonApp PRIVATE CYPHER_MASON_VERSION="${PROJECT_VERSION}")
target_include_directories(CypherMasonApp
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(CypherMasonApp
    PUBLIC
        Cypher::EditorGui
        Cypher::EditorMapGui
)

add_executable(Mason
    CypherMason_Main.cpp
)
target_link_libraries(Mason
    PRIVATE
        Cypher::MasonApp
)
if(APPLE)
    set_target_properties(Mason PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Mason"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.cypherengine.mason"
        MACOSX_BUNDLE_ICON_FILE "mason.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
    set(mason_bundle_icon "${CMAKE_CURRENT_SOURCE_DIR}/../../Resources/Branding/mason.icns")
    target_sources(Mason PRIVATE "${mason_bundle_icon}")
    set_source_files_properties("${mason_bundle_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    # Preserve the existing command-line launch path alongside the native app.
    add_custom_command(TARGET Mason POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Mason>" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Mason"
        VERBATIM
    )
endif()

if (CYPHERENGINE_BUILD_TESTS)
    add_executable(cypher_mason_smoke_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_TestMain.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/App/Mason/CypherMason_Smoke_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/App/Mason/CypherMason_WorkspaceState_Tests.cpp"
    )
    target_link_libraries(cypher_mason_smoke_tests
        PRIVATE
            Catch2::Catch2
            Cypher::MasonApp
    )
    target_compile_definitions(cypher_mason_smoke_tests
        PRIVATE
            CYPHER_MAP_EXAMPLE_DIR="${CYPHERENGINE_ROOT_DIR}/docs/formats/examples/cymap"
    )
    add_test(
        NAME cypher_mason_smoke_tests
        COMMAND cypher_mason_smoke_tests
    )
    set_tests_properties(cypher_mason_smoke_tests PROPERTIES
        LABELS "CypherEditor;Mason;Smoke"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120
    )
endif()
