cmake_minimum_required(VERSION 3.21)
project(RiipL VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_compile_options(
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
    "$<$<CXX_COMPILER_ID:MSVC>:/W4>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra>"
)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network LinguistTools)

set(CORE_SOURCES
    src/core/config/ConfigManager.cpp
    src/core/network/ApiClient.cpp
    src/core/translation/PromptBuilder.cpp
    src/core/translation/TranslationEngine.cpp
    src/core/models/Glossary.cpp
    src/core/history/HistoryManager.cpp
)
set(CORE_HEADERS
    src/core/config/Defaults.h
    src/core/config/ConfigManager.h
    src/core/network/ApiClient.h
    src/core/translation/Language.h
    src/core/translation/Tone.h
    src/core/translation/PromptBuilder.h
    src/core/translation/TranslationEngine.h
    src/core/models/Glossary.h
    src/core/history/HistoryManager.h
)
add_library(riip_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(riip_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(riip_core PUBLIC Qt6::Core Qt6::Network)

# Shared helpers used by the app shell and the test suite.
add_library(riip_utils STATIC
    src/utils/GeometryUtils.h
    src/utils/JsonUtils.h
    src/utils/SingleInstance.cpp
    src/utils/SingleInstance.h
    src/utils/TextUtils.cpp
    src/utils/TextUtils.h
)
target_include_directories(riip_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(riip_utils PUBLIC Qt6::Core Qt6::Network)

set(UI_SOURCES
    src/main.cpp
    src/ui/mainwindow/MainWindow.cpp
    src/ui/widgets/ConfigEditors.cpp
    src/ui/widgets/TranslationEdit.cpp
    src/ui/widgets/CandidatePopup.cpp
    src/ui/widgets/AppIcons.cpp
    src/ui/widgets/FlowLayout.cpp
    src/ui/widgets/ThemeColors.cpp
    src/ui/dialogs/SettingsDialog.cpp
    src/ui/dialogs/GlossaryDialog.cpp
    src/ui/dialogs/ToneDialog.cpp
    src/ui/dialogs/HistoryDialog.cpp
    src/ui/dialogs/DocumentDialog.cpp
    src/ui/dialogs/AboutDialog.cpp
)
set(UI_HEADERS
    src/ui/mainwindow/MainWindow.h
    src/ui/widgets/ConfigEditors.h
    src/ui/widgets/TranslationEdit.h
    src/ui/widgets/CandidatePopup.h
    src/ui/widgets/AppIcons.h
    src/ui/widgets/FlowLayout.h
    src/ui/widgets/ThemeColors.h
    src/ui/dialogs/SettingsDialog.h
    src/ui/dialogs/GlossaryDialog.h
    src/ui/dialogs/ToneDialog.h
    src/ui/dialogs/HistoryDialog.h
    src/ui/dialogs/DocumentDialog.h
    src/ui/dialogs/AboutDialog.h
)

set(TS_FILES resources/i18n/riip_en.ts resources/i18n/riip_zh.ts)
qt6_add_translation(QM_FILES ${TS_FILES})
set(QM_EN ${CMAKE_CURRENT_BINARY_DIR}/riip_en.qm)
set(QM_ZH ${CMAKE_CURRENT_BINARY_DIR}/riip_zh.qm)
set(I18N_QRC ${CMAKE_CURRENT_BINARY_DIR}/i18n.qrc)
configure_file(resources/i18n.qrc.in ${I18N_QRC} @ONLY)

qt_add_executable(RiipL
    ${UI_SOURCES}
    ${UI_HEADERS}
    resources/riipL.qrc
    ${I18N_QRC}
    ${QM_FILES}
)
target_link_libraries(RiipL PRIVATE riip_core riip_utils Qt6::Widgets)
target_compile_definitions(RiipL PRIVATE RIIP_VERSION="${PROJECT_VERSION}")

add_custom_target(update_translations
    COMMAND Qt6::lupdate -no-obsolete ${CMAKE_CURRENT_SOURCE_DIR}/src -ts ${TS_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Updating translation source files"
    VERBATIM
)

option(BUILD_TESTING "Build the unit test suite" OFF)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(riip_tests
        tests/test_core.cpp
    )
    target_include_directories(riip_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(riip_tests PRIVATE riip_core riip_utils Qt6::Test)
    add_test(NAME core_tests COMMAND riip_tests)
endif()
