cmake_minimum_required(VERSION 3.20)
project(clap_ipc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT TARGET music_router)
  set(MUSIC_ROUTER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/music-router music-router)
endif()

set(CLAP_IPC_SOURCES
  src/main.cpp
  src/host_context.cpp
  src/plugin_node.cpp
  src/graph.cpp
  src/graph_swap.cpp
  src/track_render.cpp
  src/event_route.cpp
  src/control_server.cpp
  src/notification_center.cpp
  src/backend_capture.cpp)

if(WIN32)
  list(APPEND CLAP_IPC_SOURCES src/shm_host_win.cpp src/rt_policy_win.cpp)
elseif(APPLE)
  list(APPEND CLAP_IPC_SOURCES src/shm_host.cpp src/rt_policy_macos.cpp)
else()
  list(APPEND CLAP_IPC_SOURCES src/shm_host.cpp src/rt_policy_linux.cpp)
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PORTAUDIO IMPORTED_TARGET portaudio-2.0)
endif()
if(PORTAUDIO_FOUND)
  list(APPEND CLAP_IPC_SOURCES src/backend_portaudio.cpp)
endif()

add_executable(clap_ipc ${CLAP_IPC_SOURCES})
set_target_properties(clap_ipc PROPERTIES OUTPUT_NAME clap-ipc)

target_include_directories(clap_ipc PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/clap/include)

target_link_libraries(clap_ipc PRIVATE music_router::music_router)

if(PORTAUDIO_FOUND)
  target_link_libraries(clap_ipc PRIVATE PkgConfig::PORTAUDIO)
  target_compile_definitions(clap_ipc PRIVATE MR_HAVE_PORTAUDIO)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(clap_ipc PRIVATE dl rt pthread)
endif()
