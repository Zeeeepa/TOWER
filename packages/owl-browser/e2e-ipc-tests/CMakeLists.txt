cmake_minimum_required(VERSION 3.16)
project(ipc_test_client)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find threading library (required for std::thread on all platforms)
find_package(Threads REQUIRED)

# Platform detection
if(APPLE)
    set(OS_MACOS 1)
    add_compile_definitions(OS_MACOS=1)
elseif(UNIX)
    set(OS_LINUX 1)
    add_compile_definitions(OS_LINUX=1)
endif()

# Source files
set(IPC_TEST_SOURCES
    main.cc
    ipc_client.cc
    response_validator.cc
    resource_monitor.cc
    test_runner.cc
    report_generator.cc
    html_report_generator.cc
    method_tests.cc
)

# Create executable
add_executable(ipc_test_client ${IPC_TEST_SOURCES})

# Include directories
target_include_directories(ipc_test_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Link libraries
# Threading is required for parallel test execution
target_link_libraries(ipc_test_client Threads::Threads)

if(APPLE)
    # macOS frameworks for resource monitoring
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    find_library(IOKIT_LIBRARY IOKit)
    target_link_libraries(ipc_test_client ${COREFOUNDATION_LIBRARY} ${IOKIT_LIBRARY})
elseif(UNIX)
    # Linux: uuid library
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UUID REQUIRED uuid)
    target_include_directories(ipc_test_client PRIVATE ${UUID_INCLUDE_DIRS})
    target_link_libraries(ipc_test_client ${UUID_LIBRARIES})
endif()

# Compiler warnings
target_compile_options(ipc_test_client PRIVATE -Wall -Wextra -Wpedantic)

# Install target
install(TARGETS ipc_test_client DESTINATION bin)
