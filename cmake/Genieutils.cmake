# Builds genieutils (+ pcrio) as a static library target from sibling checkouts.
#
# genieutils' own CMakeLists uses directory-wide include paths and GCC-only
# flags, so we compile its sources here instead of add_subdirectory()-ing it.
# The sources are used straight from the working tree (including uncommitted
# changes); switch to submodules once genieutils is stable.

set(GENIEUTILS_DIR "${CMAKE_SOURCE_DIR}/../genieutils" CACHE PATH "Path to the genieutils checkout")
set(PCRIO_DIR "${CMAKE_SOURCE_DIR}/../pcrio" CACHE PATH "Path to the pcrio checkout")

if(NOT EXISTS "${GENIEUTILS_DIR}/include/genie/dat/DatFile.h")
  message(FATAL_ERROR "genieutils not found at GENIEUTILS_DIR=${GENIEUTILS_DIR}")
endif()
if(NOT EXISTS "${PCRIO_DIR}/pcrio.h")
  message(FATAL_ERROR "pcrio not found at PCRIO_DIR=${PCRIO_DIR}")
endif()

find_package(Boost CONFIG REQUIRED COMPONENTS iostreams interprocess)
find_package(ZLIB REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(Iconv REQUIRED)

# Scenario (script/) support is left out on purpose: not needed yet.
file(GLOB_RECURSE GENIEUTILS_SOURCES CONFIGURE_DEPENDS
  "${GENIEUTILS_DIR}/src/dat/*.cpp"
  "${GENIEUTILS_DIR}/src/file/*.cpp"
  "${GENIEUTILS_DIR}/src/lang/*.cpp"
  "${GENIEUTILS_DIR}/src/resource/*.cpp"
  "${GENIEUTILS_DIR}/src/util/*.cpp"
)

add_library(genieutils STATIC ${GENIEUTILS_SOURCES} "${PCRIO_DIR}/pcrio.c")
add_library(genie::genieutils ALIAS genieutils)

# pcrio.c is compiled as C++, same as the upstream build.
set_source_files_properties("${PCRIO_DIR}/pcrio.c" PROPERTIES LANGUAGE CXX)

target_include_directories(genieutils
  PUBLIC "${GENIEUTILS_DIR}/include"
  # LangFile.cpp includes "pcrio/pcrio.h", so expose the parent directory.
  PRIVATE "${PCRIO_DIR}/.."
)
target_compile_features(genieutils PUBLIC cxx_std_17)
target_link_libraries(genieutils
  PUBLIC Boost::iostreams Iconv::Iconv
  PRIVATE Boost::interprocess ZLIB::ZLIB $<IF:$<TARGET_EXISTS:lz4::lz4>,lz4::lz4,LZ4::lz4>
)

if(MSVC)
  target_compile_definitions(genieutils PRIVATE _CRT_SECURE_NO_WARNINGS)
  # Third-party code: keep warnings quiet so they don't drown ours.
  target_compile_options(genieutils PRIVATE /W0)
else()
  target_compile_options(genieutils PRIVATE -w)
endif()
