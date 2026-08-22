cmake_minimum_required(VERSION 3.31.6)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

project(
  luatest
  VERSION 1.0.0
  LANGUAGES C CXX
)

add_executable(${PROJECT_NAME})

file(GLOB SRC src/*.c*)
target_sources(${PROJECT_NAME} PRIVATE ${SRC})
target_include_directories(${PROJECT_NAME} PRIVATE include)

include(cmake/CPM.cmake)

# fetch fmt
CPMAddPackage(
  NAME fmt
  GITHUB_REPOSITORY fmtlib/fmt
  GIT_TAG 12.1.0
  GIT_SHALLOW TRUE
)

# fetch lua
CPMAddPackage(
  NAME lua_src
  GITHUB_REPOSITORY lua/lua
  GIT_TAG v5.4.8
  GIT_SHALLOW TRUE
)

file(GLOB LUA_SRC ${lua_src_SOURCE_DIR}/*.c)

list(
  REMOVE_ITEM LUA_SRC
  ${lua_src_SOURCE_DIR}/lua.c    # exclude interpreter
  ${lua_src_SOURCE_DIR}/onelua.c # exclude core, libraries, and interpreter
)

add_library(lua STATIC ${LUA_SRC})
target_include_directories(lua PUBLIC ${lua_src_SOURCE_DIR})

# fetch sol3
set(SOL2_BUILD_LUA OFF)
set(SOL2_LUA_VERSION "5.4.8")

CPMAddPackage(
  NAME sol2
  GITHUB_REPOSITORY ThePhD/sol2
  GIT_TAG v3.5.0
  GIT_SHALLOW TRUE
)

target_link_libraries(
  ${PROJECT_NAME} PRIVATE
  fmt::fmt
  lua
  sol2::sol2
)