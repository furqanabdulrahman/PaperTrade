cmake_minimum_required(VERSION 3.16)
project(papertrade LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Put binaries in build/bin so the working dir (with data/ and web/dist) is
# predictable when we run the server.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# Third-party dependencies (infrastructure only — never used as graded DSA)
#   * cpp-httplib   : HTTPS client for Finnhub (outbound; the REST server that
#                     once served the web UI was retired when we moved to a
#                     native desktop GUI — see git history for src/main.cpp)
#   * nlohmann/json : external API parsing + local persistence
#   * Catch2 v3     : unit test framework
#   * GLFW + OpenGL : windowing + rendering for the desktop app
#   * Dear ImGui    : immediate-mode GUI widgets
#   * ImPlot        : charts (candlestick / line) for the trading views
# ---------------------------------------------------------------------------
include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# Once the deps are cloned, skip the per-configure `git` update step. This makes
# reconfigures offline-reproducible and lets us strip the deps' large .git
# histories to save disk without breaking the build.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# cpp-httplib (header-only). OpenSSL is opt-in and only needed from Phase 4
# onward (Finnhub HTTPS). Toggle with -DPAPERTRADE_ENABLE_SSL=ON.
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.15.3
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.4
)

FetchContent_MakeAvailable(httplib nlohmann_json Catch2)

# --- Desktop GUI stack ------------------------------------------------------
# GLFW builds via its own CMake. Dear ImGui and ImPlot ship no CMakeLists, so we
# compile their sources (core + GLFW/OpenGL3 backends + ImPlot) into a single
# `imgui` static library ourselves.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.90.9)
FetchContent_Declare(implot
  GIT_REPOSITORY https://github.com/epezent/implot.git
  GIT_TAG        v0.16)
FetchContent_MakeAvailable(glfw imgui implot)

find_package(OpenGL REQUIRED)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  ${implot_SOURCE_DIR}/implot.cpp
  ${implot_SOURCE_DIR}/implot_items.cpp)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${implot_SOURCE_DIR})
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# ---------------------------------------------------------------------------
# Core library: the graded DSA + domain + services live under include/ and src/.
# UI-agnostic — the desktop layer (src/app/**) is deliberately excluded so the
# core stays reusable and unit-testable. Header-only structures need no
# compilation; concrete .cpp implementations are globbed in as later phases add
# them.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PT_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER PT_SOURCES EXCLUDE REGEX "/src/app/")

if(PT_SOURCES)
  add_library(pt_core STATIC ${PT_SOURCES})
else()
  add_library(pt_core INTERFACE)
endif()

if(PT_SOURCES)
  target_include_directories(pt_core PUBLIC "${CMAKE_SOURCE_DIR}/include")
  target_compile_features(pt_core PUBLIC cxx_std_17)
  target_link_libraries(pt_core PUBLIC nlohmann_json::nlohmann_json)
else()
  target_include_directories(pt_core INTERFACE "${CMAKE_SOURCE_DIR}/include")
  target_compile_features(pt_core INTERFACE cxx_std_17)
  target_link_libraries(pt_core INTERFACE nlohmann_json::nlohmann_json)
endif()

# Live market data (Yahoo Finance keyless, or Finnhub with a key) fetches over
# HTTPS using WinHTTP — the OS's own TLS stack. No OpenSSL, no 64-bit toolchain:
# it just works on the existing MinGW. Link the system WinHTTP library.
if(WIN32)
  target_link_libraries(pt_core PUBLIC winhttp)
endif()

# ---------------------------------------------------------------------------
# Desktop application executable — Dear ImGui GUI built on top of pt_core.
# All UI sources live under src/app/; the entrypoint is src/app/main.cpp.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PT_APP_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/src/app/*.cpp")
add_executable(papertrade ${PT_APP_SOURCES})
target_link_libraries(papertrade PRIVATE pt_core imgui)

# MinGW/Windows sockets for the cpp-httplib Finnhub client used inside pt_core.
if(WIN32)
  target_link_libraries(papertrade PRIVATE ws2_32)
endif()

# ---------------------------------------------------------------------------
# Tests (Catch2 v3) — one executable, per-test-case discovery via ctest.
# ---------------------------------------------------------------------------
enable_testing()

file(GLOB_RECURSE PT_TEST_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/tests/*.cpp")

if(PT_TEST_SOURCES)
  add_executable(pt_tests ${PT_TEST_SOURCES})
  target_link_libraries(pt_tests PRIVATE pt_core Catch2::Catch2WithMain)

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(pt_tests)
endif()
