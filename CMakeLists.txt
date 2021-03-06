cmake_minimum_required(VERSION 3.9)
project(Seq)
include(ExternalProject)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden -pedantic -Wall -Wno-return-type-c-linkage")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
include_directories(.)
include_directories(compiler)

if(NOT DEFINED SEQ_DEP)
  message(FATAL_ERROR "You must set SEQ_DEP environment variable")
endif()
message(STATUS "Dependency directory: ${SEQ_DEP}")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(LLVM REQUIRED CONFIG PATHS ${SEQ_DEP} NO_DEFAULT_PATH)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

set(ZLIB ${SEQ_DEP}/lib/libz.a)
if (NOT EXISTS ${ZLIB})
  message(FATAL_ERROR "Cannot find ${ZLIB}")
endif()
message(STATUS "Found zlib: ${ZLIB}")

set(BDWGC ${SEQ_DEP}/lib/libgc.a)
if (NOT EXISTS ${BDWGC})
  message(FATAL_ERROR "Cannot find ${BDWGC}")
endif()
message(STATUS "Found bdwgc: ${BDWGC}")

execute_process(COMMAND ${SEQ_DEP}/bin/ocamlc -where
                RESULT_VARIABLE result
                OUTPUT_VARIABLE OCAML_STDLIB_PATH
                OUTPUT_STRIP_TRAILING_WHITESPACE)
if (result)
  message(FATAL_ERROR "CMake step for ocaml failed: ${result}")
endif()
message(STATUS "Found OCaml: ${OCAML_STDLIB_PATH}")

set(MENHIRLIB ${SEQ_DEP}/share/menhir)
if (NOT EXISTS ${MENHIRLIB}/menhirLib.cmx)
  message(FATAL_ERROR "Cannot find ${MENHIRLIB}/menhirLib.cmx")
endif()
message(STATUS "Found Menhir: ${MENHIRLIB}")

if (APPLE)
  set(OMPLIB ${SEQ_DEP}/lib/libomp.dylib)
else()
  set(OMPLIB ${SEQ_DEP}/lib/libomp.so)
endif()
if (NOT EXISTS ${OMPLIB})
  message(FATAL_ERROR "Cannot find ${OMPLIB}")
endif()
message(STATUS "Found OpenMP: ${OMPLIB}")
add_library(seqomp SHARED IMPORTED)
set_target_properties(seqomp PROPERTIES IMPORTED_LOCATION ${OMPLIB})

set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path;@loader_path/../lib/seq")
else()
    set(CMAKE_INSTALL_RPATH "$ORIGIN:$ORIGIN/../lib/seq")
endif()

# Seq runtime library
set(SEQRT_FILES runtime/lib.h
                runtime/lib.cpp
                runtime/exc.cpp
                runtime/sw/ksw2.h
                runtime/sw/ksw2_extd2_sse.cpp
                runtime/sw/ksw2_exts2_sse.cpp
                runtime/sw/ksw2_extz2_sse.cpp
                runtime/sw/ksw2_gg2_sse.cpp
                runtime/sw/intersw.h
                runtime/sw/intersw.cpp)
add_library(seqrt SHARED ${SEQRT_FILES})
target_include_directories(seqrt PRIVATE ${SEQ_DEP}/include runtime)
if (APPLE)
  target_link_libraries(seqrt PUBLIC seqomp -static-libstdc++ -Wl,-force_load,${ZLIB} -Wl,-force_load,${BDWGC})
else()
  target_link_libraries(seqrt PUBLIC seqomp -static-libstdc++ -Wl,--whole-archive ${ZLIB} ${BDWGC} -Wl,--no-whole-archive)
endif()
set_source_files_properties(runtime/sw/intersw.cpp PROPERTIES COMPILE_FLAGS "-march=native")

# Seq parsing library
include_directories(${OCAML_STDLIB_PATH})
link_directories(${OCAML_STDLIB_PATH})

ExternalProject_Add(seqparse_target
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/compiler/parser/ocaml
  CONFIGURE_COMMAND cp -r ${CMAKE_SOURCE_DIR}/compiler/parser/ocaml ${CMAKE_BINARY_DIR}/seqparse
  BINARY_DIR ${CMAKE_BINARY_DIR}/seqparse
  BUILD_COMMAND make -C ocaml
    OCAML=${SEQ_DEP}/bin/ocamlopt
    OCAMLLEX=${SEQ_DEP}/bin/ocamllex
    MENHIR=${SEQ_DEP}/bin/menhir
    MENHIR_LIB=${MENHIRLIB}
  INSTALL_COMMAND "")
ExternalProject_Get_Property(seqparse_target BINARY_DIR)
set(LIB_SEQPARSE ${BINARY_DIR}/ocaml/seqparser.o)
set_property(SOURCE ${LIB_SEQPARSE} PROPERTY GENERATED 1)

# Seq compiler library
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
file(GLOB SEQ_HPPFILES compiler/lang/*.h
                       compiler/types/*.h
                       compiler/parser/*.h
                       compiler/parser/ast/*.h
                       compiler/parser/ast/transform/*.h
                       compiler/parser/ast/codegen/*.h
                       compiler/parser/ast/format/*.h
                       compiler/util/*.h
                       compiler/util/fmt/*.h)
file(GLOB SEQ_CPPFILES compiler/lang/*.cpp
                       compiler/types/*.cpp
                       compiler/parser/*.cpp
                       compiler/parser/ast/*.cpp
                       compiler/parser/ast/transform/*.cpp
                       compiler/parser/ast/codegen/*.cpp
                       compiler/parser/ast/format/*.cpp
                       compiler/util/*.cpp
                       compiler/util/fmt/*.cpp)
add_library(seq SHARED ${SEQ_HPPFILES})
add_dependencies(seq seqparse_target)
target_sources(seq PRIVATE ${LIB_SEQPARSE} ${SEQ_CPPFILES})
llvm_map_components_to_libnames(LLVM_LIBS support core passes irreader x86asmparser x86info x86codegen mcjit orcjit ipo coroutines)
target_link_libraries(seq -static-libstdc++ ${LLVM_LIBS} dl seqrt)

# Seq command-line tool
add_executable(seqc runtime/main.cpp)
target_link_libraries(seqc -static-libstdc++ seq Threads::Threads)

# Seq test
# Download and unpack googletest at configure time
configure_file(test/CMakeLists.txt.in googletest-download/CMakeLists.txt)
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
                RESULT_VARIABLE result
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download)
if(result)
  message(FATAL_ERROR "CMake step for googletest failed: ${result}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build .
                RESULT_VARIABLE result
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
if(result)
  message(FATAL_ERROR "Build step for googletest failed: ${result}")
endif()

# Prevent overriding the parent project's compiler/linker
# settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Add googletest directly to our build. This defines
# the gtest and gtest_main targets.
add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/googletest-src
                 ${CMAKE_CURRENT_BINARY_DIR}/googletest-build
                 EXCLUDE_FROM_ALL)

add_executable(seqtest test/main.cpp)
target_include_directories(seqtest PRIVATE ${SEQ_DEP}/include)
target_link_libraries(seqtest seq gtest_main)
target_compile_definitions(seqtest PRIVATE TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test")

include(GoogleTest)
gtest_discover_tests(seqtest)
enable_testing()
