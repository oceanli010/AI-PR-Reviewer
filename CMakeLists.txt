cmake_minimum_required(VERSION 3.22)
project(ai-pr-reviewer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Dependencies
# ============================================================================
find_package(CLI11 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpprestsdk CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(inja CONFIG REQUIRED)

# ============================================================================
# Core library
# ============================================================================
add_library(ai_pr_reviewer_lib STATIC
    src/config.cpp
    src/github_client.cpp
    src/diff_parser.cpp
    src/ai_analyzer.cpp
    src/html_report.cpp
)

target_include_directories(ai_pr_reviewer_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ai_pr_reviewer_lib PUBLIC
    CLI11::CLI11
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
    cpprestsdk::cpprest
    spdlog::spdlog
    pantor::inja
)

# ============================================================================
# Executable
# ============================================================================
add_executable(ai-pr-reviewer src/main.cpp)
target_link_libraries(ai-pr-reviewer PRIVATE ai_pr_reviewer_lib)

# ============================================================================
# Install
# ============================================================================
install(TARGETS ai-pr-reviewer RUNTIME DESTINATION bin)
install(DIRECTORY templates/ DESTINATION share/ai-pr-reviewer/templates)

# ============================================================================
# Tests (optional)
# ============================================================================
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)

    add_executable(ai_pr_reviewer_tests
        tests/test_diff_parser.cpp
        tests/test_config.cpp
        tests/test_github_client.cpp
        tests/test_ai_analyzer.cpp
    )

    target_link_libraries(ai_pr_reviewer_tests PRIVATE
        ai_pr_reviewer_lib
        GTest::gtest
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(ai_pr_reviewer_tests)
endif()

# ============================================================================
# Packaging
# ============================================================================
set(CPACK_PACKAGE_NAME "AI-PR-Reviewer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AI-powered Pull Request code review tool")
set(CPACK_PACKAGE_VENDOR "AI-PR-Reviewer")
set(CPACK_GENERATOR "NSIS")
set(CPACK_NSIS_DISPLAY_NAME "AI PR Reviewer")
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
include(CPack)
