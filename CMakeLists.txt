cmake_minimum_required(VERSION 3.20)
project(PSICovertChannel C)

# Configure C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This project requires Linux cgroup features")
endif()

# Find all .c files in the src/ directory
file(GLOB SRC_FILES "src/*.c")

# Create an empty list to collect executable names
set(EXECUTABLES)

# For each source file, create an executable using its filename (without extension)
foreach(SRC ${SRC_FILES})
    get_filename_component(EXE_NAME ${SRC} NAME_WE)
    add_executable(${EXE_NAME} ${SRC})
    list(APPEND EXECUTABLES ${EXE_NAME})
endforeach()

# Installation rules: install all executables to bin/
install(TARGETS ${EXECUTABLES}
        DESTINATION bin)

# Build a single string with run instructions for all executables
set(RUN_INSTRUCTIONS "Build succeeded. Run with:\n")
foreach(EXE ${EXECUTABLES})
    set(RUN_INSTRUCTIONS "${RUN_INSTRUCTIONS}  sudo ./${EXE}\n")
endforeach()

# Create a custom target that prints the run instructions
add_custom_target(run_help
        COMMAND ${CMAKE_COMMAND} -E echo -e "${RUN_INSTRUCTIONS}"
)
