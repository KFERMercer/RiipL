cmake_minimum_required(VERSION 3.21)
project(RiipL VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network LinguistTools)
qt_standard_project_setup(REQUIRES 6.8)

# qt_standard_project_setup enables MOC and UIC but not RCC.
set(CMAKE_AUTORCC ON)

add_compile_options(
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
    "$<$<CXX_COMPILER_ID:MSVC>:/W4>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra>"
)

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
    src/core/json/JsonUtils.h
)
add_library(riipl_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(riipl_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(riipl_core PUBLIC Qt6::Core Qt6::Network)

# Shared helpers used by the app shell and the test suite.
add_library(riipl_utils STATIC
    src/utils/SingleInstance.cpp
    src/utils/SingleInstance.h
    src/utils/TextUtils.cpp
    src/utils/TextUtils.h
)
target_include_directories(riipl_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(riipl_utils PUBLIC Qt6::Core Qt6::Network)

set(UI_SOURCES
    src/main.cpp
    src/ui/mainwindow/MainWindow.cpp
    src/ui/widgets/ConfigEditors.cpp
    src/ui/widgets/TranslationEdit.cpp
    src/ui/widgets/CandidatePopup.cpp
    src/ui/widgets/AppIcons.cpp
    src/ui/widgets/FlowLayout.cpp
    src/ui/widgets/ThemeColors.cpp
    src/ui/widgets/WindowState.cpp
    src/ui/dialogs/SettingsDialog.cpp
    src/ui/dialogs/ApiPresetDialog.cpp
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
    src/ui/widgets/WindowState.h
    src/ui/dialogs/SettingsDialog.h
    src/ui/dialogs/ApiPresetDialog.h
    src/ui/dialogs/GlossaryDialog.h
    src/ui/dialogs/ToneDialog.h
    src/ui/dialogs/HistoryDialog.h
    src/ui/dialogs/DocumentDialog.h
    src/ui/dialogs/AboutDialog.h
)

set(TS_FILES resources/i18n/riipl_en.ts resources/i18n/riipl_zh.ts)

qt_add_executable(RiipL
    ${UI_SOURCES}
    ${UI_HEADERS}
    resources/riipl.qrc
)
target_link_libraries(RiipL PRIVATE riipl_core riipl_utils Qt6::Widgets)
target_compile_definitions(RiipL PRIVATE RIIPL_VERSION="${PROJECT_VERSION}")

# Compiles the .ts catalogs into .qm files and embeds them at :/i18n, and
# exposes the standard update_translations target for refreshing the sources.
# SOURCE_TARGETS is explicit so the test target is not scanned for strings.
qt_add_translations(RiipL
    TS_FILES ${TS_FILES}
    SOURCE_TARGETS riipl_core riipl_utils RiipL
    LUPDATE_OPTIONS -no-obsolete
)

option(BUILD_TESTING "Build the unit test suite" OFF)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(riipl_tests
        tests/test_core.cpp
    )
    target_include_directories(riipl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(riipl_tests PRIVATE riipl_core riipl_utils Qt6::Test)
    add_test(NAME core_tests COMMAND riipl_tests)
endif()
