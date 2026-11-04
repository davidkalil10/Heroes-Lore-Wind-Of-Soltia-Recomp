cmake_minimum_required(VERSION 3.16)
project(heroes_lore_recomp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)
find_package(SDL2_mixer QUIET)
if (NOT SDL2_mixer_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)
endif()

set(SOURCES
    src/main.cpp
    src/vm/vm.cpp
    src/vm/interp.cpp
    src/vm/natives.cpp
    src/midp/midp.cpp
    src/platform/platform_sdl.cpp
    src/platform/midi_synth.cpp
    src/platform/updater.cpp
    src/platform/cloud_save.cpp
)

if (WIN32)
    set(RC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/heroes_lore.rc)
    if (EXISTS ${RC_FILE})
        list(APPEND SOURCES ${RC_FILE})
    endif()
endif()

add_executable(heroes_lore ${SOURCES})

target_include_directories(heroes_lore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_MIXER_INCLUDE_DIRS}
)

if (TARGET SDL2::SDL2main)
    target_link_libraries(heroes_lore PRIVATE SDL2::SDL2main)
endif()

if (TARGET SDL2::SDL2)
    target_link_libraries(heroes_lore PRIVATE SDL2::SDL2)
else()
    target_link_libraries(heroes_lore PRIVATE ${SDL2_LIBRARIES})
endif()

if (TARGET SDL2_mixer::SDL2_mixer)
    target_link_libraries(heroes_lore PRIVATE SDL2_mixer::SDL2_mixer)
elseif (TARGET PkgConfig::SDL2_MIXER)
    target_link_libraries(heroes_lore PRIVATE PkgConfig::SDL2_MIXER)
else()
    target_link_libraries(heroes_lore PRIVATE ${SDL2_MIXER_LIBRARIES})
endif()

if (WIN32)
    target_link_libraries(heroes_lore PRIVATE winhttp)
elseif (UNIX AND NOT ANDROID AND NOT APPLE)
    find_package(CURL REQUIRED)
    target_link_libraries(heroes_lore PRIVATE CURL::libcurl)
endif()

if (WIN32)
    set(RUNTIME_DLLS
        SDL2.dll
        SDL2_mixer.dll
        libgcc_s_seh-1.dll
        libstdc++-6.dll
        libwinpthread-1.dll
        libFLAC.dll
        libmpg123-0.dll
        libopusfile-0.dll
        libvorbisfile-3.dll
        libwavpack-1.dll
        libxmp.dll
        libogg-0.dll
        libopus-0.dll
        libvorbis-0.dll
    )
    foreach(dll ${RUNTIME_DLLS})
        find_file(DLL_PATH_${dll} NAMES ${dll}
            PATHS
                "$ENV{MINGW_PREFIX}/bin"
                "$ENV{MSYSTEM_PREFIX}/bin"
                "C:/msys64/ucrt64/bin"
                "C:/msys64/mingw64/bin"
        )
        if (DLL_PATH_${dll})
            add_custom_command(TARGET heroes_lore POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL_PATH_${dll}}" "$<TARGET_FILE_DIR:heroes_lore>"
            )
        endif()
    endforeach()
endif()

add_custom_command(TARGET heroes_lore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted"
        "$<TARGET_FILE_DIR:heroes_lore>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:heroes_lore>/assets"
)


