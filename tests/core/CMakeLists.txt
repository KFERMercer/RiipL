# One executable per core unit, so a failing suite names the unit it covers.
set(RIIPL_TEST_SUITES
    tst_apiclient
    tst_apptranslator
    tst_apipresets
    tst_configmanager
    tst_glossary
    tst_historymanager
    tst_languages
    tst_promptbuilder
    tst_singleinstance
    tst_textutils
    tst_translationengine
)

foreach(suite IN LISTS RIIPL_TEST_SUITES)
    add_executable(${suite} ${suite}.cpp TestSupport.h)
    target_link_libraries(${suite} PRIVATE riipl_core riipl_utils Qt6::Test)
    # The application embeds its catalogs through rcc, which the suites do not
    # link; point them at the compiled ones so translation behaviour stays
    # testable without a second copy of the resources.
    target_compile_definitions(${suite} PRIVATE
        RIIPL_CATALOG_DIR="${CMAKE_BINARY_DIR}")
    add_test(NAME ${suite} COMMAND ${suite})
    set_tests_properties(${suite} PROPERTIES LABELS core)
endforeach()
