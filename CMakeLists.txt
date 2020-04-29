cmake_minimum_required(VERSION 3.6)
project(webrecorder)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_MODULE_PATH})

set(GIT_COMMIT_LAST_TAG "unknown")
find_package(Git)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  OUTPUT_VARIABLE GIT_COMMIT_LAST_TAG
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
configure_file("src/version.h.in" "${CMAKE_SOURCE_DIR}/src/_version.h")

set(CMAKE_CXX_STANDARD 17)

set(SOURCES
    src/main.cpp
    src/common.cpp
    src/platform.cpp
    src/Archive.cpp
    src/Client.cpp
    src/CookieStore.cpp
    src/HeaderStore.cpp
    src/HtmlPatcher.cpp
    src/Logic.cpp
    src/Server.cpp
    src/Settings.cpp
    src/HostList.cpp
    src/LossyCompressor.cpp
    src/test.cpp
)

if(NOT MSVC)
    set_source_files_properties(${SOURCES}
       PROPERTIES COMPILE_FLAGS  "-Wall -Wextra -Wsign-conversion -Wconversion")
    find_package(Threads REQUIRED)
    link_libraries(${CMAKE_THREAD_LIBS_INIT})
    link_libraries(stdc++fs)
else()
    add_compile_options(/GR- /W1)
    set(MSVC_SHARED_RUNTIME OFF)
endif()

set(SOURCES ${SOURCES}
    libs/utf8/utf8.cpp
    libs/siphash/siphash.c
    libs/minizip/zip.c
    libs/minizip/unzip.c
    libs/minizip/ioapi.c
    libs/stb/stb_impl.c
)

file(GLOB_RECURSE HEADERS include *.h *.hpp *.inc)

link_libraries(gumbo z)
add_compile_definitions(NOCRYPT UNICODE ASIO_STANDALONE USE_STANDALONE_ASIO)

if (WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0501)
    link_libraries(ws2_32 wsock32)

    set(SOURCES ${SOURCES} libs/minizip/iowin32.c)
    set(INCLUDES libs/asio/include)
endif()

find_package(OpenSSL REQUIRED)
add_compile_definitions(HAVE_OPENSSL)
link_libraries(${OPENSSL_LIBRARIES})
include_directories(${OPENSSL_INCLUDE_DIR})

include_directories(.)
include_directories(SYSTEM ${INCLUDES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})
add_executable(webrecorder ${SOURCES} ${HEADERS})

if(WIN32 AND NOT MSVC)
  target_link_options(webrecorder BEFORE PUBLIC -municode)
endif()

if(NOT WIN32)
    install(TARGETS webrecorder DESTINATION "bin")
else()
    install(TARGETS webrecorder DESTINATION .)
endif()
