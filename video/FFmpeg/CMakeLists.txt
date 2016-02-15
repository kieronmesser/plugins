# Papillon SDK - CMake File
# -----------------------------------------------------------------------------
# Description: plugin to create an output video stream from a video file
# -----------------------------------------------------------------------------

find_package(FFMPEG REQUIRED)


######## Input
add_library(PPluginInputVideoStreamFFmpeg   SHARED
    pluginInputVideoStreamFFmpeg.cpp
)

target_include_directories(PPluginInputVideoStreamFFmpeg PRIVATE ${PAPILLON_INCLUDE_DIRS} ${FFMPEG_INCLUDE_DIR})

# Libraries to link with
target_link_libraries(PPluginInputVideoStreamFFmpeg PRIVATE ${PAPILLON_LIBRARIES} ${FFMPEG_LIBRARIES})

# Copy plugin to specific output directory when compiled
papillon_copy_target_binary(PPluginInputVideoStreamFFmpeg ${PAPILLON_INSTALL_DIR}/plugins/)
install(TARGETS PPluginInputVideoStreamFFmpeg RUNTIME DESTINATION plugins LIBRARY DESTINATION plugins)


######## Output

add_library(PPluginOutputVideoStreamFFmpeg   SHARED
    pluginOutputVideoStreamFFmpeg.cpp
)

target_include_directories(PPluginOutputVideoStreamFFmpeg PRIVATE ${PAPILLON_INCLUDE_DIRS} ${FFMPEG_INCLUDE_DIR})

# Libraries to link with
target_link_libraries(PPluginOutputVideoStreamFFmpeg PRIVATE ${PAPILLON_LIBRARIES} ${FFMPEG_LIBRARIES})

# Copy plugin to specific output directory when compiled
papillon_copy_target_binary(PPluginOutputVideoStreamFFmpeg ${PAPILLON_INSTALL_DIR}/plugins/)
install(TARGETS PPluginOutputVideoStreamFFmpeg RUNTIME DESTINATION plugins LIBRARY DESTINATION plugins)

