/*
 * Copyright (C) 2015 Digital Barriers plc. All rights reserved.
 * Contact: http://www.digitalbarriers.com/
 *
 * This file is part of the Papillon SDK.
 *
 * You can't use, modify or distribute any part of this file without
 * the explicit written agreements of Digital Barriers plc.
 */

#ifndef SDK_SRC_PLUGINS_WRAPPERS_FFMPEG_PFFMPEGSTREAMHANDLER_H_
#define SDK_SRC_PLUGINS_WRAPPERS_FFMPEG_PFFMPEGSTREAMHANDLER_H_

#include <PByteStream.h>
#include <PResult.h>
#include <PGuid.h>
#include <PLog.h>
#include "PFFmpegUtility.h"
using namespace papillon;

/**
 * @file PFFmpegStreamHandler.h
 * @brief Class PFFmpegStreamHandler Provides a high-level file and network I/O stream handler using FFmpeg.
 */
class PFFmpegStreamHandler
{
    public:
        enum StreamType
        {
            E_STREAM_FILE,
            E_STREAM_NETWORK
        };

        enum StreamDirection
        {
            E_STREAM_INPUT,
            E_STREAM_OUTPUT
        };

    public:
        PFFmpegStreamHandler()
              : m_streamType      (StreamType::E_STREAM_FILE)
              , m_streamDirection (StreamDirection::E_STREAM_INPUT)
              , m_codecID         (AV_CODEC_ID_NONE)
              , m_mutex           (true)
              , m_isOpen          (false)
        {
#if FFMPEG_DEBUG
            PLog::OpenConsoleLogger(PLog::E_LEVEL_TRACE);
            av_log_set_level(AV_LOG_TRACE);
#else
            av_log_set_level(AV_LOG_ERROR);
#endif
        }


        virtual ~PFFmpegStreamHandler() {}


        /**
         * Registers formats and codecs, and networking if given URI is a network one like rtp://..
         * @param uri URI as a PUri.
         * @return PResult::C_OK on success, PResult::Error otherwise.
         */
        PResult Initialise(const PUri& uri)
        {
            PMutexLocker lock(&m_mutex);

            if (uri.IsValid() == false)
            {
                return PResult::ErrorBadURIFormat(PString("Incoming URI '%1' is not valid").Arg(uri.ToString()));
            }

            // Query 'fourcc' is defined only for output streams to let user to pick a codec
            PString fourcc;
            if (IsOutputStream())
            {
                if (uri.GetQueryValue("fourcc", fourcc) == false)
                {
                    P_LOG_WARNING << "No specific 'fourcc' was requested, using 'H264' by default";
                    fourcc = "H264";
                }
            }
            else
            {
                // TODO For input formats I'm setting FOURCC to the default value 'H264' but
                // av_probe_input_format3() would be wiser here
                fourcc = "H264";
            }

            if (fourcc.Compare("H264") == 0)
                m_codecID = AV_CODEC_ID_H264;
            else if (fourcc.Compare("MJPG") == 0)
                m_codecID = AV_CODEC_ID_MJPEG;
            else
                return PResult::ErrorBadURIFormat(PString("Unsupported FOURCC value '%1' was given").Arg(fourcc));

            // Grab stream type from given URI
            if (uri.IsFile())
                m_streamType = StreamType::E_STREAM_FILE;
            else if (uri.IsNetwork())
                m_streamType = StreamType::E_STREAM_NETWORK;
            else
                return PResult::ErrorBadURIFormat(PString("URI should point to either a file or a network stream"));

            // Register all encoders
            avcodec_register_all();
            av_register_all();

            // Initialise network if necessary
            if (IsStreaming() && (avformat_network_init() < 0))
            {
                return PResult::Error("Cannot initialise network for RTSP stream");
            }

#if FFMPEG_DEBUG
            av_log_set_level(AV_LOG_DEBUG);
#endif

            m_uri = uri;
            return PResult::C_OK;
        }


        /**
         * Opens the codec, and then file or RTSP stream, and writes stream header.
         * @return PResult::C_OK on success, PResult::Error otherwise.
         */
        virtual PResult OpenStream() = 0;


        /**
         * Closes a stream and releases resources.
         * @return PResult::C_OK on success, PResult::Error otherwise.
         */
        virtual PResult CloseStream() = 0;


        /**
         * Returns true if output stream is open, false otherwise
         * @return true if the stream is open, false otherwise
         */
        bool IsOpen()
        {
            return m_isOpen;
        }


        /**
         * Returns true for networked streams.
         * @return true if this is a networked stream, false for a file stream.
         */
        bool IsStreaming()
        {
            return m_streamType == PFFmpegStreamHandler::StreamType::E_STREAM_NETWORK;
        }


        /**
         * Returns true if this is an input stream.
         */
        bool IsInputStream() const
        {
            return m_streamDirection == PFFmpegStreamHandler::StreamDirection::E_STREAM_INPUT;
        }


        /**
         * Returns true if this is an output stream.
         */
        bool IsOutputStream() const
        {
            return m_streamDirection == PFFmpegStreamHandler::StreamDirection::E_STREAM_OUTPUT;
        }

    protected:
        PFFmpegStreamHandler::StreamType      m_streamType;
        PFFmpegStreamHandler::StreamDirection m_streamDirection;
        PUri                                  m_uri;
        AVCodecID                             m_codecID;
        mutable PMutex                        m_mutex;
        bool                                  m_isOpen;
};

#endif /* SDK_SRC_PLUGINS_WRAPPERS_FFMPEG_PFFMPEGSTREAMHANDLER_H_ */
