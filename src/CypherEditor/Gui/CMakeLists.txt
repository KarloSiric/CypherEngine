##########################################################################
#
#  CypherEngine Source Code
#  Copyright (c) 2026 Karlo Siric. All rights reserved.
#
#  File: src/CypherEditor/Gui/CMakeLists.txt
#  Purpose: Builds Cypher::EditorGui, the Qt framework shared by every editor
#           tool (ADR 0008): style, actions, docking, panels, application.
#  Details: Qt subclassing stays inside this library and workspace Gui leaf
#           code; the contracts are structs and free functions. The editor
#           resources (theme, keymap, layouts, stylesheet, icons) are compiled
#           in here and registered by EditorGui_Init.
#
##########################################################################

add_library(CypherEditorGui STATIC
    Actions/CypherEditorGui_Actions.cpp
    Actions/CypherEditorGui_Actions.h
    Application/CypherEditorGui_Application.cpp
    Application/CypherEditorGui_Application.h
    Dialogs/CypherEditorGui_AppearancePage.cpp
    Dialogs/CypherEditorGui_AppearancePage.h
    Dialogs/CypherEditorGui_AssetWindow.cpp
    Dialogs/CypherEditorGui_AssetWindow.h
    Dialogs/CypherEditorGui_DatabaseView.cpp
    Dialogs/CypherEditorGui_DatabaseView.h
    Dialogs/CypherEditorGui_KeymapSettings.cpp
    Dialogs/CypherEditorGui_KeymapSettings.h
    Dialogs/CypherEditorGui_SettingsDialog.cpp
    Dialogs/CypherEditorGui_SettingsDialog.h
    Dialogs/CypherEditorGui_SettingsTransfer.cpp
    Dialogs/CypherEditorGui_SettingsTransfer.h
    Dialogs/CypherEditorGui_ShortcutsDialog.cpp
    Dialogs/CypherEditorGui_ShortcutsDialog.h
    Dialogs/CypherEditorGui_ThemeDraft.cpp
    Dialogs/CypherEditorGui_ThemeDraft.h
    Dialogs/CypherEditorGui_ThemeEditor.cpp
    Dialogs/CypherEditorGui_ThemeEditor.h
    Docking/CypherEditorGui_Docking.cpp
    Docking/CypherEditorGui_Docking.h
    History/CypherEditorGui_History.cpp
    History/CypherEditorGui_History.h
    Panels/CypherEditorGui_AssetBrowser.cpp
    Panels/CypherEditorGui_AssetBrowser.h
    Panels/CypherEditorGui_CommandHistory.cpp
    Panels/CypherEditorGui_CommandHistory.h
    Panels/CypherEditorGui_Console.cpp
    Panels/CypherEditorGui_Console.h
    Panels/CypherEditorGui_Log.cpp
    Panels/CypherEditorGui_Log.h
    Panels/CypherEditorGui_LogPanels.cpp
    Panels/CypherEditorGui_LogPanels.h
    Style/CypherEditorGui_Style.cpp
    Style/CypherEditorGui_Style.h
    Widgets/CypherEditorGui_CommandPalette.cpp
    Widgets/CypherEditorGui_CommandPalette.h
    Widgets/CypherEditorGui_CodeEditor.cpp
    Widgets/CypherEditorGui_CodeEditor.h
    Widgets/CypherEditorGui_Section.cpp
    Widgets/CypherEditorGui_Section.h
    Widgets/CypherEditorGui_SettingControl.cpp
    Widgets/CypherEditorGui_SettingControl.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../Resources/CypherEditorResources.qrc"
)
add_library(Cypher::EditorGui ALIAS CypherEditorGui)

target_compile_features(CypherEditorGui PUBLIC cxx_std_20)
set_target_properties(CypherEditorGui PROPERTIES AUTORCC ON)
target_include_directories(CypherEditorGui
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Actions"
        "${CMAKE_CURRENT_SOURCE_DIR}/Application"
        "${CMAKE_CURRENT_SOURCE_DIR}/Dialogs"
        "${CMAKE_CURRENT_SOURCE_DIR}/Docking"
        "${CMAKE_CURRENT_SOURCE_DIR}/History"
        "${CMAKE_CURRENT_SOURCE_DIR}/Panels"
        "${CMAKE_CURRENT_SOURCE_DIR}/Style"
        "${CMAKE_CURRENT_SOURCE_DIR}/Widgets"
)
target_link_libraries(CypherEditorGui
    PUBLIC
        Cypher::EditorCore
        Cypher::ThirdPartyQtAds
        Qt6::Core
        Qt6::Gui
        Qt6::Svg
        Qt6::Widgets
)

if (CYPHERENGINE_BUILD_TESTS)
    add_executable(cypher_editor_gui_contract_tests
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_TestMain.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_Actions_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_AppearancePage_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_AssetBrowser_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_AssetWindow_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_DatabaseView_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_CommandHistory_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_Application_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_Console_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_Docking_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_History_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_Style_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_CommandPalette_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_SettingsDialog_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_SettingsTransfer_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_KeymapSettings_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_ShortcutsDialog_Tests.cpp"
        "${CYPHERENGINE_ROOT_DIR}/tests/CypherEditor/Gui/CypherEditorGui_ThemeEditor_Tests.cpp"
    )
    target_link_libraries(cypher_editor_gui_contract_tests
        PRIVATE
            Catch2::Catch2
            Cypher::EditorGui
    )
    add_test(
        NAME cypher_editor_gui_contract_tests
        COMMAND cypher_editor_gui_contract_tests
    )
    # Headless: the offscreen platform needs no display or window server.
    set_tests_properties(cypher_editor_gui_contract_tests PROPERTIES
        LABELS "CypherEditor;Gui;Contract"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120
    )
endif()
