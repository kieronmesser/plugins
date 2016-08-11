
# FFmpeg plugin is separate from the source tree of Papillon, but added as a submodule (from GitHub)
# First block is to configure the build in a Papillon development environment
# Second block is to configure the build against a Papillon installation (guided by the PAPILLON_INSTALL_DIR)

set(INPUT_PLUGIN_NAME PPluginInputVideoStreamFFmpeg)
set(OUTPUT_PLUGIN_NAME PPluginOutputVideoStreamFFmpeg)

if (DEFINED PAPILLON_BUILD_ROOT)

    # This is the wrapper directory where functionality of 3rd-party
    # libraries are shared between input and output plugins

    # These plugins rely on Externals/FFmpeg on Windows, install native packages for Linux
    if (WIN32)
        link_directories($ENV{PAPILLON_EXTERNAL_LIB_FFMPEG}/lib)
    endif()

    include_directories($ENV{PAPILLON_EXTERNAL_LIB_FFMPEG}/include)
    include_directories(wrappers)

    # Add input plugin
    add_library(${INPUT_PLUGIN_NAME}
        SHARED
        documentation.h
        pluginInputVideoStreamFFmpeg.cpp
    )

    target_link_libraries(${INPUT_PLUGIN_NAME} ${PAPILLON_LIB_PREFIX}Core ${FFMPEG_LIBRARIES} pthread)

    # Copy plugin to specific output directory when compiled
    papillon_copy_target_binary(${INPUT_PLUGIN_NAME} ${PAPILLON_PLUGIN_DIR})
    install(TARGETS ${INPUT_PLUGIN_NAME} RUNTIME DESTINATION plugins LIBRARY DESTINATION plugins)

    # Add output plugin
    add_library(${OUTPUT_PLUGIN_NAME}
        SHARED
        documentation.h
        pluginOutputVideoStreamFFmpeg.cpp
    )

    # Libraries to link with
    target_link_libraries(${OUTPUT_PLUGIN_NAME} ${PAPILLON_LIB_PREFIX}Core ${FFMPEG_LIBRARIES})

    # Copy plugin to specific output directory when compiled
    papillon_copy_target_binary(${OUTPUT_PLUGIN_NAME} ${PAPILLON_PLUGIN_DIR})
    install(TARGETS ${OUTPUT_PLUGIN_NAME} RUNTIME DESTINATION plugins  LIBRARY DESTINATION plugins)

else()

    # Papillon SDK - CMake File
    # -----------------------------------------------------------------------------
    # Description: plugin to create an output video stream from a video file
    # -----------------------------------------------------------------------------

    find_package(FFMPEG REQUIRED)

    # FFMPEG library needs UINT64_C so we have to define macro for stdint.c to enable this
    add_definitions(-D__STDC_CONSTANT_MACROS)

    #we need to add special link flags when using static FFMPEG libraries
    if (UNIX)
        if (${FFMPEG_LIBAVCODEC} MATCHES "a$")
            message("Adding special option for building with static FFMPEG")
            set(MY_LINKER_FLAGS "-Wl,-Bsymbolic")
        endif()
    endif()

    include_directories(${PAPILLON_INCLUDE_DIRS} ${FFMPEG_INCLUDE_DIR})

    # Add input plugin
    add_library(${INPUT_PLUGIN_NAME} SHARED pluginInputVideoStreamFFmpeg.cpp)

    # Libraries to link with
    target_link_libraries(${INPUT_PLUGIN_NAME} ${PAPILLON_LIBRARIES} ${FFMPEG_LIBRARIES} ${MY_LINKER_FLAGS})

    # Copy plugin to specific output directory when compiled
    install(TARGETS ${INPUT_PLUGIN_NAME} RUNTIME DESTINATION plugins LIBRARY DESTINATION ${PAPILLON_INSTALL_DIR}/plugins)

    # Add output plugin
    add_library(${OUTPUT_PLUGIN_NAME} SHARED pluginOutputVideoStreamFFmpeg.cpp)

    # Libraries to link with
    target_link_libraries(${OUTPUT_PLUGIN_NAME} ${PAPILLON_LIBRARIES} ${FFMPEG_LIBRARIES} ${MY_LINKER_FLAGS})

    # Copy plugin to specific output directory when compiled
    install(TARGETS ${OUTPUT_PLUGIN_NAME} RUNTIME DESTINATION plugins LIBRARY DESTINATION ${PAPILLON_INSTALL_DIR}/plugins)

endif()

