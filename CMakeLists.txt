cmake_minimum_required(VERSION 3.25)

project(NewAge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NEWAGE_BUILD_TESTS "Build the test suite" ON)

# Visual Studio builds a project's sources one at a time unless cl gets /MP
# (Ninja already runs one compiler per core).
if(CMAKE_GENERATOR MATCHES "Visual Studio")
  add_compile_options(/MP)
endif()

include(cmake/Genieutils.cmake)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

# --- core: data/session logic, no widgets ------------------------------------
add_library(newage_core STATIC
  src/core/Config.cpp
  src/core/Config.h
  src/core/GameInstall.cpp
  src/core/GameInstall.h
  src/core/NameProvider.cpp
  src/core/NameProvider.h
  src/core/Session.cpp
  src/core/Session.h
  src/core/VersionProfile.cpp
  src/core/VersionProfile.h
)
target_include_directories(newage_core PUBLIC src)
target_link_libraries(newage_core PUBLIC Qt6::Core genie::genieutils)

# --- model: field descriptors and Qt item models, no widgets -------------------
add_library(newage_model STATIC
  src/model/EntityListModel.cpp
  src/model/EntityListModel.h
  src/model/FieldDesc.h
  src/model/FieldTreeModel.cpp
  src/model/FieldTreeModel.h
  src/model/ListFilterModel.cpp
  src/model/ListFilterModel.h
  src/model/TechFields.cpp
  src/model/TechFields.h
  src/model/TechListModel.cpp
  src/model/TechListModel.h
  src/model/UnitFields.cpp
  src/model/UnitFields.h
  src/model/UnitListModel.cpp
  src/model/UnitListModel.h
)
target_link_libraries(newage_model PUBLIC newage_core)

# vcpkg's applocal step copies linked DLLs next to an exe but not Qt plugins,
# which Qt loads at runtime from <exe dir>/<plugin type>/. Copy the ones we need
# so `target` runs straight from the build tree (Debug or Release variant).
function(newage_deploy_qt_plugins target)
  if(NOT WIN32)
    return()
  endif()
  foreach(plugin IN ITEMS
      "Qt6::QWindowsIntegrationPlugin|platforms"
      "Qt6::QModernWindowsStylePlugin|styles")
    string(REPLACE "|" ";" plugin "${plugin}")
    list(GET plugin 0 plugin_target)
    list(GET plugin 1 plugin_dir)
    if(TARGET ${plugin_target})
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/${plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${plugin_target}>" "$<TARGET_FILE_DIR:${target}>/${plugin_dir}/"
        VERBATIM)
    endif()
  endforeach()
endfunction()

# --- ui: Qt Widgets front end -------------------------------------------------
add_library(newage_ui STATIC
  src/ui/EntityBrowser.cpp
  src/ui/EntityBrowser.h
  src/ui/MainWindow.cpp
  src/ui/MainWindow.h
  src/ui/OptionsDialog.cpp
  src/ui/OptionsDialog.h
)
target_link_libraries(newage_ui PUBLIC newage_model Qt6::Widgets)

# --- app ----------------------------------------------------------------------
qt_add_executable(NewAge WIN32
  src/app/main.cpp
)
target_link_libraries(NewAge PRIVATE newage_ui)
target_compile_definitions(NewAge PRIVATE NEWAGE_VERSION="${PROJECT_VERSION}")
newage_deploy_qt_plugins(NewAge)

foreach(target newage_core newage_model newage_ui NewAge)
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
