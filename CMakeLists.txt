cmake_minimum_required(VERSION 3.16)
project(musicplayer VERSION 2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Toggle for static Qt6 build: cmake -DSTATIC_QT=ON ..
option(STATIC_QT "Use statically linked Qt6" OFF)

if(STATIC_QT)
    set(CMAKE_PREFIX_PATH "/opt/qt6-static")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(TAGLIB REQUIRED taglib)

find_library(BASS_LIB NAMES bass PATHS ${CMAKE_CURRENT_SOURCE_DIR}/lib NO_DEFAULT_PATH)
find_library(BASS_AAC_LIB NAMES bass_aac PATHS ${CMAKE_CURRENT_SOURCE_DIR}/lib NO_DEFAULT_PATH)
find_library(BASS_FLAC_LIB NAMES bassflac PATHS ${CMAKE_CURRENT_SOURCE_DIR}/lib NO_DEFAULT_PATH)
find_library(DISCORD_RPC_LIB NAMES discord-rpc PATHS ${CMAKE_CURRENT_SOURCE_DIR}/discord-rpc/linux-static/lib NO_DEFAULT_PATH)

include_directories(include ${SQLITE3_INCLUDE_DIRS} ${TAGLIB_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/discord-rpc/linux-static/include)

add_executable(musicplayer
    src/main.cpp
    src/mainwindow.cpp
    src/library.cpp
    src/lyrics.cpp
    src/importer.cpp
    include/importer.h
    src/mainwindow.h
)

target_link_libraries(musicplayer PRIVATE
    Qt6::Widgets
    ${SQLITE3_LIBRARIES}
    ${TAGLIB_LIBRARIES}
    ${BASS_LIB}
    ${BASS_AAC_LIB}
    ${BASS_FLAC_LIB}
    ${DISCORD_RPC_LIB}
    pthread
    m
)

set_target_properties(musicplayer PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/lib:$ORIGIN/../lib"
)
