cmake_minimum_required(VERSION 3.16)
project(dslc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# --- macOS libc++ header workaround -------------------------------------
# On some Command Line Tools installs the driver's default libc++ include dir
# (.../CommandLineTools/usr/include/c++/v1) is a broken stub missing <string>
# et al, while the real headers live under the SDK. Point the compiler at the
# SDK's libc++ explicitly when we detect that situation.
if(APPLE)
  execute_process(COMMAND xcrun --show-sdk-path
                  OUTPUT_VARIABLE DSL_SDK_PATH
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  if(DSL_SDK_PATH AND EXISTS "${DSL_SDK_PATH}/usr/include/c++/v1/string")
    message(STATUS "Using SDK libc++ headers: ${DSL_SDK_PATH}/usr/include/c++/v1")
    add_compile_options(-nostdinc++ -isystem ${DSL_SDK_PATH}/usr/include/c++/v1)
  endif()
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

# Core compiler library. Sources are appended as each stage lands so the
# build + test suite stay green at every commit.
set(DSLCORE_SOURCES
  src/lexer.cpp
  src/parser.cpp
  src/ast_print.cpp
  src/sema.cpp
  src/ir.cpp
  src/irgen.cpp
  src/passes/pass_manager.cpp
  src/passes/constfold.cpp
  src/passes/algebraic.cpp
  src/passes/constprop.cpp
  src/passes/dce.cpp
  src/codegen.cpp
  src/vm.cpp
)
add_library(dslcore ${DSLCORE_SOURCES})
target_include_directories(dslcore PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_executable(dslc src/main.cpp)
target_link_libraries(dslc PRIVATE dslcore)

# ---- Tests -------------------------------------------------------------
enable_testing()
function(add_dsl_test name)
  add_executable(${name} tests/${name}.cpp)
  target_link_libraries(${name} PRIVATE dslcore)
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_dsl_test(test_smoke)
add_dsl_test(test_lexer)
add_dsl_test(test_parser)
add_dsl_test(test_sema)
add_dsl_test(test_ir)
add_dsl_test(test_opt)
add_dsl_test(test_e2e)
