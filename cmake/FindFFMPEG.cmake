# - Try to find ffmpeg libraries (libavcodec, libavformat and libavutil)
# Once done this will define
#
#  FFMPEG_ROOT
#  FFMPEG_FOUND - system has ffmpeg or libav
#  FFMPEG_INCLUDE_DIR - the ffmpeg include directory
#  FFMPEG_LIBRARIES - Link these to use ffmpeg
#  FFMPEG_LIBAVCODEC
#  FFMPEG_LIBAVFORMAT
#  FFMPEG_LIBAVUTIL
#  FFMPEG_SWSCALE
#
#  Copyright (c) 2008 Andreas Schneider <mail@cynapses.org>
#  Modified for other libraries by Lasse Kärkkäinen <tronic>
#  Modified for Hedgewars by Stepik777
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#

if (FFMPEG_LIBRARIES AND FFMPEG_INCLUDE_DIR)
  # in cache already
  set(FFMPEG_FOUND TRUE)
else (FFMPEG_LIBRARIES AND FFMPEG_INCLUDE_DIR)
  # Look for FFmpeg in the standard paths using pkg-config only if we don't have one in externals
  if ("${FFMPEG_ROOT}" STREQUAL "")
    # use pkg-config to get the directories and then use these values
    # in the FIND_PATH() and FIND_LIBRARY() calls
    find_package(PkgConfig)
    if (PKG_CONFIG_FOUND)
      pkg_check_modules(_FFMPEG_AVCODEC libavcodec)
      pkg_check_modules(_FFMPEG_AVFORMAT libavformat)
      pkg_check_modules(_FFMPEG_AVUTIL libavutil)
      pkg_check_modules(_FFMPEG_SWSCALE libswscale)
    endif (PKG_CONFIG_FOUND)
  else()
    # We have a copy in externals, tell find_library() and find_path() not to hangout in standard paths
    set(SEARCH_PARAMETERS NO_DEFAULT_PATH)
  endif()

  # Find include directory root
  find_path(FFMPEG_AVCODEC_INCLUDE_DIR
    NAMES libavcodec/avcodec.h
    PATHS ${FFMPEG_ROOT}/include ${_FFMPEG_AVCODEC_INCLUDE_DIRS} /usr/include /usr/local/include /opt/local/include /sw/include
    PATH_SUFFIXES ffmpeg libav
    ${SEARCH_PARAMETERS}
  )

  # Find libraries
  find_library(FFMPEG_LIBAVCODEC
    NAMES avcodec
    PATHS ${FFMPEG_ROOT}/lib ${_FFMPEG_AVCODEC_LIBRARY_DIRS} /usr/lib /usr/local/lib /opt/local/lib /sw/lib
    ${SEARCH_PARAMETERS}
  )

  find_library(FFMPEG_LIBAVFORMAT
    NAMES avformat
    PATHS ${FFMPEG_ROOT}/lib ${_FFMPEG_AVFORMAT_LIBRARY_DIRS} /usr/lib /usr/local/lib /opt/local/lib /sw/lib
    ${SEARCH_PARAMETERS}
  )

  find_library(FFMPEG_LIBAVUTIL
    NAMES avutil
    PATHS ${FFMPEG_ROOT}/lib ${_FFMPEG_AVUTIL_LIBRARY_DIRS} /usr/lib /usr/local/lib /opt/local/lib /sw/lib
    ${SEARCH_PARAMETERS}
  )

  find_library(FFMPEG_SWSCALE
    NAMES swscale
    PATHS ${FFMPEG_ROOT}/lib ${_FFMPEG_SWSCALE_LIBRARY_DIRS} /usr/lib /usr/local/lib /opt/local/lib /sw/lib
    ${SEARCH_PARAMETERS}
  )

  if (FFMPEG_LIBAVCODEC AND FFMPEG_LIBAVFORMAT AND FFMPEG_LIBAVUTIL AND FFMPEG_SWSCALE)
    set(FFMPEG_FOUND TRUE)
  endif()

  # Set the parameters that'll be used in the project
  if (FFMPEG_FOUND)
    set(FFMPEG_INCLUDE_DIR ${FFMPEG_AVCODEC_INCLUDE_DIR})
    set(FFMPEG_LIBRARIES ${FFMPEG_LIBAVCODEC} ${FFMPEG_LIBAVFORMAT} ${FFMPEG_LIBAVUTIL} ${FFMPEG_SWSCALE})
  endif (FFMPEG_FOUND)

  if (FFMPEG_FOUND)
    if (NOT FFMPEG_FIND_QUIETLY)
      message(STATUS "Found FFMPEG or Libav: ${FFMPEG_LIBRARIES}, ${FFMPEG_INCLUDE_DIR}")
    endif (NOT FFMPEG_FIND_QUIETLY)
  else (FFMPEG_FOUND)
    if (FFMPEG_FIND_REQUIRED)
      message(FATAL_ERROR "Could not find libavcodec or libavformat or libavutil")
    endif (FFMPEG_FIND_REQUIRED)
  endif (FFMPEG_FOUND)

endif (FFMPEG_LIBRARIES AND FFMPEG_INCLUDE_DIR)

