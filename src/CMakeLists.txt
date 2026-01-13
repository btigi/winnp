cmake_minimum_required(VERSION 3.15)
project(winnp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Source files
add_library(gen_winnp SHARED
    winnp.cpp
)

# Windows-specific settings
if(WIN32)
    set_target_properties(gen_winnp PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
    )
    
    # Link Windows libraries
    target_link_libraries(gen_winnp
        winmm
    )
endif()

# Compiler-specific options
target_compile_definitions(gen_winnp PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

# Installation (optional)
install(TARGETS gen_winnp
    RUNTIME DESTINATION .
)
