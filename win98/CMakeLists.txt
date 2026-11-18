# MKSC98: Windows 98 SE build of the classic game from MKSC Neo
# (https://github.com/arcanite24/mksc-neo), classic 240x160 software
# renderer only. Upstream sources under ../portable are compiled unchanged;
# everything specific to this port lives in this directory.
#
#   Win98 .exe : cmake -DCMAKE_TOOLCHAIN_FILE=toolchain-win98.cmake   (see build.sh)
#   reference  : native cmake on the build host -> headless mksc98 with the
#                null backend, used to record reference frame hashes.
cmake_minimum_required(VERSION 3.20)
project(mksc_win98 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(P "${CMAKE_CURRENT_SOURCE_DIR}/../portable")
set(GEN "${CMAKE_CURRENT_BINARY_DIR}/generated")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ---- generated sources (same generators as upstream's CMakeLists) ---------
set(embedded_files "${P}/third_party/fonts/NunitoBlack.ttf" "${P}/third_party/fonts/NunitoExtraBold.ttf"
    "${P}/third_party/fonts/NunitoBlackItalic.ttf" "${P}/data/menu-names.json")
string(REPLACE ";" "|" embedded_list "${embedded_files}")
add_custom_command(OUTPUT "${GEN}/embedded_files.c"
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${GEN}/embedded_files.c" "-DFILES=${embedded_list}"
        -P "${P}/embed_files.cmake"
    DEPENDS ${embedded_files} "${P}/embed_files.cmake" VERBATIM)
foreach(profile drift air)
    add_custom_command(OUTPUT "${GEN}/modern_${profile}_payload.h"
        COMMAND "${Python3_EXECUTABLE}" "${P}/../tools/portable/modern_drift_format.py"
            "${P}/data/modern-${profile}.json" --header "${GEN}/modern_${profile}_payload.h"
        DEPENDS "${P}/data/modern-${profile}.json" "${P}/../tools/portable/modern_drift_format.py" VERBATIM)
endforeach()
add_custom_target(mksc98_generated DEPENDS "${GEN}/embedded_files.c" "${GEN}/modern_drift_payload.h"
    "${GEN}/modern_air_payload.h")

# ---- upstream sources -------------------------------------------------------
# The simulation, content readers, audio and the Super Circuit importer are
# platform independent and taken whole. They go into a static library, so
# only what the classic game actually references is linked into the .exe.
file(GLOB upstream_sources CONFIGURE_DEPENDS
    "${P}/core/*.c" "${P}/app/*.c" "${P}/assets/*.c" "${P}/audio/*.c" "${P}/import/*.c" "${P}/platform/*.c")
# Not built for this target (out of scope, or replaced by win98/*.c):
set(excluded_names
    # entry points and SDL-only tools
    main.c audio_sdl_smoke_main.c
    # replaced by this port
    sdl_audio_queue.c load_job.c loading_screen.c first_run.c
    # 3D courses, SDL_GPU / OpenGL ES renderers, HD-2D
    course_3d_host.c render_gpu.c render_gles.c ui_gpu.c ui_geometry.c ui_gles_font.c scene_3d_workers.c
    # modern (window-resolution) menus host, course editor, split-screen
    modern_menu_host.c modern_menu.c modern_menu_pages.c modern_menu_select.c modern_menu_options.c modern_menu_mods.c
    modern_menu_games.c modern_menu_setup.c modern_menu_pause.c modern_menu_results.c modern_menu_transition.c
    modern_menu_loading.c menu_driver_model.c ui_font.c ui_canvas.c ui_draw_list.c course_editor_host.c course_editor_pack.c multi_race_host.c
    # mods, Lua, online store, other-game importers
    script_host.c mod_store.c mod_store_job.c mod_loader.c mod_manager.c mod_install.c mod_archive.c
    game_import_host.c game_library.c ui_background_host.c
    # controller, touch, Android, file chooser
    sdl_gamepad.c mobile_touch.c mobile_lifecycle.c file_dialog.c source_path.c)
# 3D-course simulation and art (mesh_*), the 3D scene/model renderers and the
# Mario Kart DS / 64 music sequencers.
list(FILTER upstream_sources EXCLUDE REGEX "/(mesh_[a-z0-9_]*|course_3d|scene_3d|race_ground_3d|render_model|model_renderer|model_asset|nds_sequence|n64_sequence|mod_menu_renderer|mod_manifest)\\.c$")
foreach(name IN LISTS excluded_names)
    list(FILTER upstream_sources EXCLUDE REGEX "/${name}$")
endforeach()

add_library(mksc98_upstream STATIC ${upstream_sources} "${GEN}/embedded_files.c")
add_dependencies(mksc98_upstream mksc98_generated)
target_include_directories(mksc98_upstream PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/sdl3" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${P}/app" "${P}/core" "${P}/assets" "${P}/platform" "${P}/audio" "${P}/import" "${P}/import/nds" "${P}/import/n64"
    "${P}/third_party/stb" "${P}/third_party/lua" "${GEN}")
target_compile_definitions(mksc98_upstream PUBLIC PORTABLE_ENABLE_SDL3=1 MKSC_WIN98=1
    PORTABLE_SOFTWARE_FRAME_NATIVE_ONLY=1)
option(MKSC98_LTO "Link-time optimisation across upstream and the port" OFF)
set(mksc98_opt -O2)
if(MKSC98_LTO)
    set(mksc98_opt -O2 -flto)
    target_link_options(mksc98_upstream PUBLIC -flto -O2)
    set(CMAKE_AR "${CMAKE_C_COMPILER_AR}")
    set(CMAKE_RANLIB "${CMAKE_C_COMPILER_RANLIB}")
endif()
target_compile_options(mksc98_upstream PUBLIC ${mksc98_opt} -ffunction-sections -fdata-sections -fno-strict-aliasing)
if(MKSC98_PROFILE)
    target_compile_definitions(mksc98_upstream PUBLIC MKSC98_PROFILE=1)
endif()

# ---- the port ----------------------------------------------------------------
set(port_sources main_win98.c shim.c audio_queue.c loading.c input.c stubs.c harness.c tall.c)
if(WIN32)
    list(APPEND port_sources backend_win32.c backend_d3d7.c backend_dsound.c)
else()
    list(APPEND port_sources backend_null.c)
endif()
add_executable(mksc98 ${port_sources})
target_compile_options(mksc98 PRIVATE -Wall -Wextra)
target_link_libraries(mksc98 PRIVATE mksc98_upstream)
target_link_options(mksc98 PRIVATE -Wl,--gc-sections "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/mksc98.map"
    # Bench timers bracket these upstream functions without editing them (harness.c).
    -Wl,--wrap=portable_gp_race_session_tick -Wl,--wrap=portable_race_scene_render -Wl,--wrap=route_audio_render_gp
    # Taller race pictures place these draws over the extra sky rows as well (tall.c).
    -Wl,--wrap=portable_hud_render_entry_bands -Wl,--wrap=portable_gp_opening_render
    -Wl,--wrap=portable_frontend_image_draw_scaled -Wl,--wrap=portable_software_frame_window_width
    -Wl,--wrap=portable_gp_results_render_laps_with_hud -Wl,--wrap=portable_gp_time_board_render
    -Wl,--wrap=portable_hud_render_gp_finish_portraits -Wl,--wrap=portable_hud_render_quick_finish_portraits
    -Wl,--wrap=portable_gp_results_render_coins -Wl,--wrap=portable_time_board_render_slide
    -Wl,--wrap=portable_battle_board_render -Wl,--wrap=portable_gp_standings_render_over
    -Wl,--wrap=portable_gp_standings_render -Wl,--wrap=portable_gp_results_render_action_window_open
    -Wl,--wrap=portable_gp_results_render_action_window -Wl,--wrap=portable_frontend_image_draw
    -Wl,--wrap=portable_frontend_image_draw_palette)
if(WIN32)
    target_link_libraries(mksc98 PRIVATE winmm gdi32 user32 kernel32)
    # Content decoding recurses deeply; upstream gives its loader an 8 MiB stack.
    target_link_options(mksc98 PRIVATE -mwindows -Wl,--stack,8388608)
else()
    target_link_libraries(mksc98 PRIVATE m)
endif()
