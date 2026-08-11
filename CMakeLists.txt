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
#   * cpp-httplib : REST server + HTTPS client for Finnhub
#   * nlohmann/json : request/response + external API parsing
#   * Catch2 v3   : unit test framework
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

# ---------------------------------------------------------------------------
# Core library: the graded DSA + domain + services live under include/ and src/.
# Header-only structures need no compilation; concrete .cpp implementations are
# globbed in as they are added in later phases.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE PT_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM PT_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")

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

# ---------------------------------------------------------------------------
# Server executable
# ---------------------------------------------------------------------------
add_executable(papertrade src/main.cpp)
target_link_libraries(papertrade PRIVATE
  pt_core
  httplib::httplib
  nlohmann_json::nlohmann_json)

# MinGW/Windows sockets for cpp-httplib
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
