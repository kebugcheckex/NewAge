cmake_minimum_required(VERSION 3.25)

project(NewAge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NEWAGE_BUILD_TESTS "Build the test suite" ON)

include(cmake/Genieutils.cmake)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

# --- core: data/session logic, no widgets ------------------------------------
add_library(newage_core STATIC
  src/core/Session.cpp
  src/core/Session.h
  src/core/VersionProfile.cpp
  src/core/VersionProfile.h
)
target_include_directories(newage_core PUBLIC src)
target_link_libraries(newage_core PUBLIC Qt6::Core genie::genieutils)

# --- app: Qt Widgets front end ------------------------------------------------
qt_add_executable(NewAge WIN32
  src/app/main.cpp
  src/ui/MainWindow.cpp
  src/ui/MainWindow.h
)
target_link_libraries(NewAge PRIVATE newage_core Qt6::Widgets)
target_compile_definitions(NewAge PRIVATE NEWAGE_VERSION="${PROJECT_VERSION}")

# vcpkg's applocal step copies linked DLLs next to the exe but not Qt plugins,
# which Qt loads at runtime from <exe dir>/<plugin type>/. Copy the ones we need
# so the exe runs straight from the build tree (Debug or Release variant).
if(WIN32)
  foreach(plugin IN ITEMS
      "Qt6::QWindowsIntegrationPlugin|platforms"
      "Qt6::QModernWindowsStylePlugin|styles")
    string(REPLACE "|" ";" plugin "${plugin}")
    list(GET plugin 0 plugin_target)
    list(GET plugin 1 plugin_dir)
    if(TARGET ${plugin_target})
      add_custom_command(TARGET NewAge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NewAge>/${plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${plugin_target}>" "$<TARGET_FILE_DIR:NewAge>/${plugin_dir}/"
        VERBATIM)
    endif()
  endforeach()
endif()

foreach(target newage_core NewAge)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endforeach()

if(NEWAGE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
