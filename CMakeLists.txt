cmake_minimum_required(VERSION 3.25)
project(pengubirds LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# SwARc's runtime and HLE modules are shared libraries: rebuilding one
# does not relink the game.
set(CMAKE_LINK_DEPENDS_NO_SHARED ON)

set(SWARC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../SwARc"
    CACHE PATH "SwARc source tree")

enable_testing()
add_subdirectory("${SWARC_DIR}" swarc)

recomp_add_lifted(absw1_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated")
if(TARGET absw1_lifted)
  add_executable(absw1 src/main.cpp src/hle_ext.cpp src/mpg123_hle.cpp $<TARGET_OBJECTS:absw1_lifted>)
  # The HLE modules are shared libraries with static registrars; name them
  # all so nothing gets dropped as unused (--no-as-needed).
  target_link_libraries(absw1 PRIVATE recomp_win32 recomp_opengl recomp_dsound mpg123)
  target_compile_definitions(absw1 PRIVATE AB_GAME_SW1)
  if(NOT APPLE)
    target_link_options(absw1 PRIVATE "LINKER:--no-as-needed")
  endif()
  set_target_properties(absw1 PROPERTIES ENABLE_EXPORTS ON)
endif()

recomp_add_lifted(absw2_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated_sw2")
if(TARGET absw2_lifted)
  add_executable(absw2 src/main_sw2.cpp src/hle_ext.cpp src/seh_native.cpp
                 src/mpg123_hle.cpp $<TARGET_OBJECTS:absw2_lifted>)
  target_link_libraries(absw2 PRIVATE recomp_win32 recomp_opengl recomp_dsound recomp_xinput mpg123)
  target_compile_definitions(absw2 PRIVATE AB_GAME_SW2)
  if(NOT APPLE)
    target_link_options(absw2 PRIVATE "LINKER:--no-as-needed")
  endif()
  set_target_properties(absw2 PROPERTIES ENABLE_EXPORTS ON)
endif()

recomp_add_lifted(abclassic_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated_classic")
if(TARGET abclassic_lifted)
  add_executable(abclassic src/main_classic.cpp src/hle_ext.cpp
                 src/seh_native.cpp src/mpg123_hle.cpp $<TARGET_OBJECTS:abclassic_lifted>)
  target_link_libraries(abclassic PRIVATE recomp_win32 recomp_opengl recomp_dsound mpg123)
  target_compile_definitions(abclassic PRIVATE AB_GAME_CLASSIC)
  if(NOT APPLE)
    target_link_options(abclassic PRIVATE "LINKER:--no-as-needed")
  endif()
  set_target_properties(abclassic PROPERTIES ENABLE_EXPORTS ON)
endif()

recomp_add_lifted(abseasons_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated_seasons")
if(TARGET abseasons_lifted)
  add_executable(abseasons src/main_seasons.cpp src/hle_ext.cpp
                 src/seh_native.cpp src/mpg123_hle.cpp $<TARGET_OBJECTS:abseasons_lifted>)
  target_link_libraries(abseasons PRIVATE recomp_win32 recomp_opengl recomp_dsound recomp_xinput mpg123)
  target_compile_definitions(abseasons PRIVATE AB_GAME_SEASONS)
  if(NOT APPLE)
    target_link_options(abseasons PRIVATE "LINKER:--no-as-needed")
  endif()
  set_target_properties(abseasons PROPERTIES ENABLE_EXPORTS ON)
endif()

recomp_add_lifted(abrio_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated_rio")
if(TARGET abrio_lifted)
  add_executable(abrio src/main_rio.cpp src/hle_ext.cpp
                 src/seh_native.cpp src/mpg123_hle.cpp $<TARGET_OBJECTS:abrio_lifted>)
  target_link_libraries(abrio PRIVATE recomp_win32 recomp_opengl recomp_dsound recomp_xinput mpg123)
  target_compile_definitions(abrio PRIVATE AB_GAME_RIO)
  if(NOT APPLE)
    target_link_options(abrio PRIVATE "LINKER:--no-as-needed")
  endif()
  set_target_properties(abrio PROPERTIES ENABLE_EXPORTS ON)
endif()

recomp_add_lifted(abspace_lifted "${CMAKE_CURRENT_SOURCE_DIR}/generated_space")
if(TARGET abspace_lifted)
  add_executable(abspace src/main_space.cpp src/hle_ext.cpp
                 src/seh_native.cpp src/mpg123_hle.cpp $<TARGET_OBJECTS:abspace_lifted>)
  target_link_libraries(abspace PRIVATE recomp_win32 recomp_opengl recomp_dsound recomp_xinput mpg123)
  target_compile_definitions(abspace PRIVATE AB_GAME_SPACE)
  if(NOT APPLE)
    target_link_options(abspace PRIVATE "LINKER:--no-as-needed")
  endif()
  set_target_properties(abspace PROPERTIES ENABLE_EXPORTS ON)
endif()

# pengubirds installer tool (TUI + native Advanced Installer extractor).
if(TARGET recomp_x86)
  add_executable(pengubirds tools/pengubirds/main.cpp tools/pengubirds/extractor.cpp
                 tools/pengubirds/icon.cpp)
  target_include_directories(pengubirds PRIVATE tools/pengubirds)
  set_target_properties(pengubirds PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endif()
