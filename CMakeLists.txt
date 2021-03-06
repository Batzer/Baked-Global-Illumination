cmake_minimum_required(VERSION 3.0)
project(BakedGI)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

file(GLOB SOURCES "src/*.cc" "src/*.hh")
file(GLOB SHADERS "bin/shaders/*.*sh" "bin/shaders/*.glsl")
file(GLOB THIRD_PARTY_SOURCES "src/third-party/*.h" "src/third-party/*.hpp")

source_group("Source" FILES ${SOURCES})
source_group("Shaders" FILES ${SHADERS})
source_group("Source\\ThirdParty" FILES ${THIRD_PARTY_SOURCES})

add_subdirectory(libs/assimp-lean)
add_subdirectory(libs/aion)
add_subdirectory(libs/AntTweakBar)
add_subdirectory(libs/glow)
add_subdirectory(libs/glow-extras)

option(GLFW_BUILD_EXAMPLES "" OFF)
option(GLFW_BUILD_TESTS "" OFF)
option(GLFW_BUILD_DOCS "" OFF)
option(GLFW_INSTALL "" OFF)
option(BUILD_SHARED_LIBS "" ON)
add_subdirectory(libs/glfw)

if (NOT MSVC)
    find_package(embree 3.1 REQUIRED PATHS libs/embree-3.1.0.x86_64.linux NO_DEFAULT_PATH)
else()
    find_package(embree 3.1 REQUIRED)
endif()
include_directories(${EMBREE_INCLUDE_DIRS})

find_package(OpenMP)
if (OPENMP_FOUND)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()

add_executable(BakedGI ${SOURCES} ${SHADERS} ${THIRD_PARTY_SOURCES})

if (NOT MSVC)
    target_compile_options(BakedGI PRIVATE -std=c++14)
endif()

target_link_libraries(BakedGI PUBLIC
	glow
	glow-extras
	glfw
	AntTweakBar
	assimp
	fmt
	${EMBREE_LIBRARY}
	)
