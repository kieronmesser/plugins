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

######## Input
add_library(PPluginInputVideoStreamFFmpeg   SHARED
    pluginInputVideoStreamFFmpeg.cpp
)

# Libraries to link with
target_link_libraries(PPluginInputVideoStreamFFmpeg ${PAPILLON_LIBRARIES} ${FFMPEG_LIBRARIES} ${MY_LINKER_FLAGS})

# Copy plugin to specific output directory when compiled
install(TARGETS PPluginInputVideoStreamFFmpeg RUNTIME DESTINATION plugins LIBRARY DESTINATION ${PAPILLON_INSTALL_DIR}/plugins)


######## Output
add_library(PPluginOutputVideoStreamFFmpeg   SHARED
    pluginOutputVideoStreamFFmpeg.cpp
)

# Libraries to link with
target_link_libraries(PPluginOutputVideoStreamFFmpeg ${PAPILLON_LIBRARIES} ${FFMPEG_LIBRARIES} ${MY_LINKER_FLAGS})

# Copy plugin to specific output directory when compiled
install(TARGETS PPluginOutputVideoStreamFFmpeg RUNTIME DESTINATION plugins LIBRARY DESTINATION ${PAPILLON_INSTALL_DIR}/plugins)

