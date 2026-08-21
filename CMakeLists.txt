cmake_minimum_required(VERSION 3.21)
project(RiipL VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-fPIC)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network LinguistTools)

set(CORE_SOURCES
    src/core/config/ConfigManager.cpp
    src/core/network/ApiClient.cpp
    src/core/translation/PromptBuilder.cpp
    src/core/translation/TranslationEngine.cpp
    src/core/models/Glossary.cpp
    src/core/history/HistoryManager.cpp
    src/platform/GlobalHotkey.cpp
    src/utils/SingleInstance.cpp
    src/utils/TextUtils.cpp
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
    src/utils/JsonUtils.h
    src/utils/TextUtils.h
    src/platform/GlobalHotkey.h
    src/utils/SingleInstance.h
)
add_library(riip_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(riip_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(riip_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network)

if(UNIX AND NOT APPLE)
    find_package(X11 QUIET)
    if(X11_FOUND)
        target_link_libraries(riip_core PUBLIC X11::X11)
        target_compile_definitions(riip_core PUBLIC RIIP_HAVE_X11)
    endif()
endif()

set(UI_SOURCES
    src/main.cpp
    src/ui/mainwindow/MainWindow.cpp
    src/ui/widgets/ConfigEditors.cpp
    src/ui/widgets/TranslationEdit.cpp
    src/ui/widgets/CandidatePopup.cpp
    src/ui/widgets/AppIcons.cpp
    src/ui/dialogs/SettingsDialog.cpp
    src/ui/dialogs/GlossaryDialog.cpp
    src/ui/dialogs/ToneDialog.cpp
    src/ui/dialogs/HistoryDialog.cpp
    src/ui/dialogs/DocumentDialog.cpp
)
set(UI_HEADERS
    src/ui/mainwindow/MainWindow.h
    src/ui/widgets/ConfigEditors.h
    src/ui/widgets/TranslationEdit.h
    src/ui/widgets/CandidatePopup.h
    src/ui/widgets/AppIcons.h
    src/ui/dialogs/SettingsDialog.h
    src/ui/dialogs/GlossaryDialog.h
    src/ui/dialogs/ToneDialog.h
    src/ui/dialogs/HistoryDialog.h
    src/ui/dialogs/DocumentDialog.h
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
target_link_libraries(RiipL PRIVATE riip_core Qt6::Widgets Qt6::Network)
target_link_options(RiipL PRIVATE -pie)
target_compile_definitions(RiipL PRIVATE RIIP_VERSION="${PROJECT_VERSION}")

add_custom_target(update_translations
    COMMAND Qt6::lupdate ${CMAKE_CURRENT_SOURCE_DIR}/src -ts ${TS_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Updating translation source files"
    VERBATIM
)

find_package(Qt6 REQUIRED COMPONENTS Test)
enable_testing()
add_executable(riip_tests
    tests/test_core.cpp
    src/ui/widgets/TranslationEdit.cpp
    src/ui/widgets/TranslationEdit.h
)
target_include_directories(riip_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(riip_tests PRIVATE riip_core Qt6::Test Qt6::Widgets)
add_test(NAME core_tests COMMAND riip_tests)
