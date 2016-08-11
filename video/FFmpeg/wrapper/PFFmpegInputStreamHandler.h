/*
 * Copyright (C) 2015 Digital Barriers plc. All rights reserved.
 * Contact: http://www.digitalbarriers.com/
 *
 * This file is part of the Papillon SDK.
 *
 * You can't use, modify or distribute any part of this file without
 * the explicit written agreements of Digital Barriers plc.
 */

#ifndef SDK_SRC_PLUGINS_WRAPPERS_FFMPEG_PFFMPEGINPUTSTREAMHANDLER_H_
#define SDK_SRC_PLUGINS_WRAPPERS_FFMPEG_PFFMPEGINPUTSTREAMHANDLER_H_

#include "PFFmpegStreamHandler.h"

/**
 * @file PFFmpegInputStreamHandler.h
 * @brief Class PFFmpegInputStreamHandler Provides a high-level handler for an input stream using FFmpeg.
 */
class PFFmpegInputStreamHandler : public PFFmpegStreamHandler
{
    public:
        PFFmpegInputStreamHandler()
            : m_codecContext         (0)
            , m_conversionContext    (0)
            , m_fps                  (0)
            , m_dictionary           (0)
            , m_formatContext        (0)
            , m_stream               (0)
            , m_codec                (0)
            , m_videoStreamIndex     (0)
        {
            m_streamDirection = PFFmpegStreamHandler::StreamDirection::E_STREAM_INPUT;
        }


        ~PFFmpegInputStreamHandler()
        {
            avcodec_close(m_codecContext);
            avformat_close_input(&m_formatContext);
            avformat_free_context(m_formatContext);
            av_dict_free(&m_dictionary);

            CloseStream().LogErrorIfAny();
        }


        /*
         * Creates and initialises a stream.
         * @return PResult::C_OK on success, PResult::Error otherwise.
         */
        PResult CreateStream()
        {
            PMutexLocker lock(&m_mutex);

            // Status of some methods called
            int result = -1;
            // Actual stream path, path without scheme for files, entire thing for network streams
            PString streamPath((IsStreaming() ? m_uri.ToString() : m_uri.GetPath().ToString()));

            // Open input stream
            if ((result = avformat_open_input(&m_formatContext, streamPath.c_str(), NULL, NULL)) < 0)
            {
                return PResult::Error(PString("Cannot open input URI \"%1\": %2").Arg(m_uri.ToString()).Arg(FFmpegUtility::GetErrorString(result)));
            }

            // Retrieve stream information
            if ((result = avformat_find_stream_info(m_formatContext, NULL)) < 0)
            {
                return PResult::Error(PString("Cannot find a stream: %1").Arg(FFmpegUtility::GetErrorString(result)));
            }

#if FFMPEG_DEBUG
            av_dump_format(m_formatContext, 0, streamPath.c_str(), 0);
#endif

            // Find the first video stream that is relevant with our configuration
            m_videoStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

            if (m_videoStreamIndex < 0)
            {
                switch (m_videoStreamIndex)
                {
                    case AVERROR_DECODER_NOT_FOUND:
                        return PResult::Error("Found a stream but not a decoder");

                    case AVERROR_STREAM_NOT_FOUND:
                    default:
                        return PResult::Error("Cannot find a video stream");
                }
            }

            // Get a pointer to the video stream
            m_stream = m_formatContext->streams[m_videoStreamIndex];

            return PResult::C_OK;
        }


        /**
         * Opens the codec for this stream and writes the stream header.
         * @return PResult::C_OK on success, PResult::Error otherwise
         */
        PResult OpenStream()
        {
            PMutexLocker lock(&m_mutex);

            // Get a pointer to the codec context for the video stream
            m_codecContext = m_formatContext->streams[m_videoStreamIndex]->codec;

            // Find the decoder for the video stream
            m_codec = avcodec_find_decoder(m_codecContext->codec_id);
            if (m_codec == NULL)
            {
                return PResult::Error("Cannot find a decoder for this type of codec");
            }

            int result = -1;

            // Open codec
            if ((result = avcodec_open2(m_codecContext, m_codec, &m_dictionary)) < 0)
            {
                return PResult::Error(PString("Cannot open codec").Arg(FFmpegUtility::GetErrorString(result)));
            }

            // Dump some information regarding the video stream
#if FFMPEG_DEBUG
            FFmpegUtility::PrintDictionary(m_formatContext->metadata);
            FFmpegUtility::PrintStreams(m_formatContext);
#endif

            return PResult::C_OK;
        }


        /**
         * Marks the stream as closed, resource release is handled in the destructor.
         * @return PResult::C_OK on success, PResult::Error otherwise
         */
        PResult CloseStream()
        {
            PMutexLocker lock(&m_mutex);

            m_isOpen = false;
            return PResult::C_OK;
        }


        /**
         * Returns a PFrame read from the stream.
         * @return PResult::C_OK on success, PResult::Error otherwise.
         */
        PResult ReadFrame(PFrame& frame)
        {
            PMutexLocker lock(&m_mutex);

            AVFrame *inputFrame = NULL;     // This is the frame that we read in
            AVPacket inputPacket;           // This is the packet read from the decoder
            int frameFinished = 0;          // Set when we've read all the packets of a frame
            int result;

            // Allocate frames
            if ((inputFrame = av_frame_alloc()) == NULL)
                return PResult::Error("Cannot allocate a frame");

            // Read packets until we have an entire frame or there is nothing to read anymore
            while (av_read_frame(m_formatContext, &inputPacket) >= 0)
            {
                // Ignore packets from other streams
                if (inputPacket.stream_index != m_videoStreamIndex)
                    continue;

                // Decode video frame
                result = avcodec_decode_video2(m_codecContext, inputFrame, &frameFinished, &inputPacket);

                // Dump some packet information if all is fine
                if (result <= 0)
                    continue;
#if FFMPEG_DEBUG
                else
                    av_pkt_dump_log2(NULL, 0, &inputPacket, 0, m_stream);
#endif
                /*
                 * A frame may be scattered over several AVPacket objects so
                 * check if we have an entire frame
                 */
                if (frameFinished)
                {
                    PResult conversionResult = FFmpegUtility::CreatePImageFromAVFrame(m_codecContext, inputFrame, frame).LogErrorIfAny();

                    // We have a complete frame so cleanup and return C_OK
                    av_free(inputFrame);
                    av_packet_unref(&inputPacket);
                    return PResult::C_OK;
                }

                av_packet_unref(&inputPacket);
            }

            AVPacket emptyPacket;
            av_init_packet(&emptyPacket);

            /*
             * At this moment, there is no more data (or packets) to read from the file
             * (so av_read_frame() returned a negative value) but there are still packets
             * read previously but not yet decoded, as there is always a delay between reading
             * a frame and decoding it. Here we decode these delayed frames.
             */
            do
            {
                avcodec_decode_video2(m_codecContext, inputFrame, &frameFinished, &emptyPacket);

                if (frameFinished)
                {
                    PResult conversionResult = FFmpegUtility::CreatePImageFromAVFrame(m_codecContext, inputFrame, frame).LogErrorIfAny();

                    // We have a complete delayed frame so cleanup and return C_OK
                    av_free(inputFrame);
                    av_packet_unref(&emptyPacket);

                    return PResult::C_OK;
                }
            } while (frameFinished);

            // EOS reached, cleanup and return a relevant error to let the caller stop
            av_free(inputFrame);
            return PResult::ErrorEndOfStream("Reached the end of video stream");
        }


        /**
         * @return Duration of the video stream in seconds.
         * todo What if it's an RTSP stream?
         * todo Make this floating point
         */
        int32 GetDuration() const
        {
            return FFmpegUtility::GetDuration(m_formatContext);
        }


        /**
         * @return Total number of frames currently open stream has.
         * todo What if it's an RTSP stream?
         */
        int32 GetFrameCount() const
        {
            return FFmpegUtility::GetDuration(m_formatContext) * FFmpegUtility::GetFPS(m_stream);
        }


        /**
         * @return Currently processed frame's index.
         */
        int32 GetFrameNumber() const
        {
            return m_codecContext->frame_number;
        }


        /**
         * @return Width of the video stream.
         */
        int32 GetWidth() const
        {
            return m_codecContext->width;
        }


        /**
         * @return Height of the video stream.
         */
        int32 GetHeight() const
        {
            return m_codecContext->height;
        }


        /**
         * @return FPS of the stream as a floating point value.
         */
        double GetFPS() const
        {
            return FFmpegUtility::GetFPS(m_stream);
        }


        /**
         * Seeks to the frame located at given index with respect to the beginning of the stream.
         * @param frameToSeek Frame index to seek to, converted to time-stamp by dividing it by FPS.
         * @return PResult::C_OK on success, PResult::Error otherwise.
         */
        PResult SeekToFrame(uint32 frameToSeek)
        {
            // At this point the user might have already asked for some
            // frames so reset the codec before seeking to the desired timestamp.
            ReturnIfFailed(Reset());

            int64_t totalNumberOfFrames = GetFPS() * FFmpegUtility::GetDuration(m_formatContext);

            // Validate incoming frame number, it should not be beyond the end of video file
            // But for some files (such as http://gstreamer.freedesktop.org/data/media/large/bullet.vob)
            // libav cannot detect neither duration nor FPS properly, even worse, it returns a negative value
            // for duration, so here we should check if we have a positive value for totalNumberOfFrames
            if (totalNumberOfFrames > 0 && frameToSeek > totalNumberOfFrames)
            {
                return PResult::Error("Cannot seek beyond the end of the file");
            }

            return FFmpegUtility::SeekTo(m_formatContext, m_videoStreamIndex, static_cast<double>(frameToSeek) / GetFPS());
        }


        /**
         * Resets internal state of this stream. It's called each time a seek operation is requested
         * assuming that the user might have already read frames from the stream. It should also be called 
         * after seek operations when reading frames from the beginning of the stream is desired.
         * @return PResult::C_OK on success, PResult::Error otherwise.
         */
        PResult Reset()
        {
            return FFmpegUtility::Reset(m_codecContext);
        }

    protected:
        PSizei             m_dimensions;
        AVCodecContext*    m_codecContext;
        SwsContext*        m_conversionContext;
        uint32             m_fps;
        AVDictionary*      m_dictionary;
        AVFormatContext*   m_formatContext;
        AVStream*          m_stream;
        AVCodec*           m_codec;
        int                m_videoStreamIndex;
};

#endif /* SDK_SRC_PLUGINS_WRAPPERS_FFMPEG_PFFMPEGINPUTSTREAMHANDLER_H_ */
