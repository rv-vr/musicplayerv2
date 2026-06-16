cmake_minimum_required(VERSION 3.16)
project(musicplayer VERSION 2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(GLIB2 REQUIRED glib-2.0)

# Locate BASS libraries
find_library(BASS_LIB NAMES bass PATHS ${CMAKE_CURRENT_SOURCE_DIR}/lib NO_DEFAULT_PATH)
find_library(BASS_AAC_LIB NAMES bass_aac PATHS ${CMAKE_CURRENT_SOURCE_DIR}/lib NO_DEFAULT_PATH)

include_directories(include ${SQLITE3_INCLUDE_DIRS} ${GLIB2_INCLUDE_DIRS})

add_executable(musicplayer
    src/main.cpp
    src/mainwindow.cpp
    src/library.c
    src/lyrics.c
)

target_link_libraries(musicplayer PRIVATE
    Qt6::Widgets
    ${SQLITE3_LIBRARIES}
    ${GLIB2_LIBRARIES}
    ${BASS_LIB}
    ${BASS_AAC_LIB}
    m
)

# Set rpath to look in lib folder
set_target_properties(musicplayer PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/lib:$ORIGIN/../lib"
)
