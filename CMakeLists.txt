cmake_minimum_required(VERSION 3.6)
project(webrecorder)

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
    src/HostBlocker.cpp
    src/test.cpp
    libs/utf8/utf8.cpp
    libs/siphash/siphash.c
    libs/minizip/zip.c
    libs/minizip/unzip.c
    libs/minizip/ioapi.c
  )
file(GLOB_RECURSE HEADERS include *.h *.hpp *.inc)

link_libraries(gumbo z)
add_compile_definitions(NOCRYPT UNICODE ASIO_STANDALONE USE_STANDALONE_ASIO)

if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Wsign-conversion)

    find_package(Threads REQUIRED)
    link_libraries(${CMAKE_THREAD_LIBS_INIT})
    link_libraries(stdc++fs)
else()
    add_compile_options(/GR- /W1 -D_WIN32_WINNT=0x0501)
    set(MSVC_SHARED_RUNTIME OFF)

    set(SOURCES ${SOURCES} libs/minizip/iowin32.c)
    set(INCLUDES libs/gumbo/include libs/zlib/include libs/asio/include)
    link_directories(libs/gumbo/lib64 libs/zlib/lib64)
endif()

find_package(OpenSSL REQUIRED)
add_compile_definitions(HAVE_OPENSSL)
link_libraries(${OPENSSL_LIBRARIES})
include_directories(${OPENSSL_INCLUDE_DIR})

include_directories(. ${INCLUDES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})
add_executable(webrecorder ${SOURCES} ${HEADERS})

if(NOT WIN32)
    install(TARGETS webrecorder DESTINATION "bin")
else()
    install(TARGETS webrecorder DESTINATION .)
    install(FILES libs/gumbo/bin64/gumbo.dll DESTINATION .)
    install(FILES libs/zlib/bin64/zlib.dll DESTINATION .)
    install(FILES "C:/windows/system32/libcrypto-1_1-x64.dll" DESTINATION .)
    install(FILES "C:/windows/system32/libssl-1_1-x64.dll" DESTINATION .)
endif()
