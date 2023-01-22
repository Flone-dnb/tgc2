cmake_minimum_required(VERSION 3.8)

project(tgc CXX) # also change name in Doxyfile (if changing)

# Build mode.
set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "Using build mode '${default_build_type}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING "Choose the type of build." FORCE)
endif()
message(STATUS "${PROJECT_NAME}: build type: ${CMAKE_BUILD_TYPE}.")

# Add `DEBUG` macro for debug builds.
if(CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug")
    message(STATUS "Adding DEBUG macro for this build type.")
    add_compile_definitions(DEBUG)
endif()

# Sources.
set(PROJECT_SOURCES
    include/tgc2.h
    include/tgc2.cpp
    # add your .h/.cpp files here
)

# Target.
add_library(${PROJECT_NAME} STATIC ${PROJECT_SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC include)

# More warnings.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${PROJECT_NAME} PRIVATE /W3 /WX)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

# Set C++ standard.
set(PROJECT_CXX_STANDARD_VERSION 17)
set(CMAKE_CXX_STANDARD ${PROJECT_CXX_STANDARD_VERSION})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_${PROJECT_CXX_STANDARD_VERSION})

# Enable Address Sanitizer.
if(CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug")
    if(NOT WIN32)
        message(STATUS "Adding Address Sanitizer flags.")
        set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
        set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    endif()
endif()

# Tests.
option(tgc_BUILD_TEST "Build tgc tests" OFF)
if (tgc_BUILD_TEST)
    # Define tests target.
    set(TESTS_TARGET_NAME tgc_tests)
    add_executable(${TESTS_TARGET_NAME}
        tests/main.cpp
        # add your test .cpp files here
    )
    target_link_libraries(${TESTS_TARGET_NAME} PRIVATE ${PROJECT_NAME})
endif()

message(STATUS "${PROJECT_NAME} is configured to use the following C++ standard: ${CMAKE_CXX_STANDARD}")
