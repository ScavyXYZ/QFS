cmake_minimum_required(VERSION 3.8)

# Set policy for MSVC debug information format if available
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Project definition
project("qfs"
        DESCRIPTION "Quick File Search - Multi-threaded file search with logical operators and regex support"
        LANGUAGES CXX)

# Create executable
add_executable(qfs "QFS.cpp")

# Set C++ standard to C++17 (required for <filesystem>)
target_compile_features(qfs PRIVATE cxx_std_17)

# Ensure C++17 is required
set_target_properties(QFS PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Platform-specific settings
if(WIN32)
    # Windows-specific settings
    target_compile_definitions(qfs PRIVATE 
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
elseif(UNIX)
    # Linux/Unix-specific settings
    target_link_libraries(qfs PRIVATE pthread)
    
    # Link filesystem library if needed (required for GCC < 9)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
        target_link_libraries(qfs PRIVATE stdc++fs)
    endif()
endif()

# Compiler-specific optimizations
if(MSVC)
    target_compile_options(qfs PRIVATE 
        /W4          # Warning level 4
        /permissive- # Standards conformance
        $<$<CONFIG:Release>:/O2 /GL>  # Optimize for speed and whole program optimization
    )
    target_link_options(qfs PRIVATE
        $<$<CONFIG:Release>:/LTCG>    # Link-time code generation
    )
else()
    target_compile_options(qfs PRIVATE
        -Wall -Wextra -Wpedantic      # Enable warnings
        $<$<CONFIG:Release>:-O3>      # Optimize for speed
    )
endif()

# Installation rules (optional)
install(TARGETS qfs
        RUNTIME DESTINATION bin
)

# Print build information
message(STATUS "qfs Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: C++17")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

