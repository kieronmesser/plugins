/*
 * Copyright (C) 2015 Digital Barriers plc. All rights reserved.
 * Contact: http://www.digitalbarriers.com/
 *
 * This file is part of the Papillon SDK.
 *
 * You can't use, modify or distribute any part of this file without
 * the explicit written agreements of Digital Barriers plc.
 */

#ifndef SDK_SRC_PLUGINS_WRAPPERS_FFMPEG_PFFMPEGOUTPUTSTREAMHANDLER_H_
#define SDK_SRC_PLUGINS_WRAPPERS_FFMPEG_PFFMPEGOUTPUTSTREAMHANDLER_H_

#include <PFFmpegStreamHandler.h>

/**
 * @file PFFmpegOutputStreamHandler.h
 * @brief Class PFFmpegOutputStreamHandler Provides a high-level handler for an file/network output stream using FFmpeg.
 */
class PFFmpegOutputStreamHandler : public PFFmpegStreamHandler
{
    public:
        PFFmpegOutputStreamHandler()
            : m_formatContext     (0)
            , m_outputFormat      (0)
            , m_stream            (0)
            , m_codec             (0)
            , m_codecContext      (0)
            , m_codecOptions      (0)
            , m_conversionContext (0)
            , m_fps               (0)
            , m_gopLength         (12)
        {
            m_streamDirection = PFFmpegStreamHandler::StreamDirection::E_STREAM_OUTPUT;
        }


        ~PFFmpegOutputStreamHandler()
        {
            CloseStream().LogErrorIfAny();

            if (IsStreaming())
            {
                if (avformat_network_deinit() < 0)
                    P_LOG_WARNING << "libavformat had problems deinitialising the network";
            }

            avcodec_close(m_codecContext);
            avformat_free_context(m_formatContext);
            av_dict_free(&m_codecOptions);

            m_isOpen = false;
        }


        /**
         * Creates a stream in given dimensions and at given FPS.
         * @param dimensions PSizei holding desired dimensions
         * @param fps Desired FPS value
         * @return PResult::C_OK on success, PResult::Error otherwise
         */
        PResult CreateStream(const PSizei& dimensions, uint32 fps)
        {
            PMutexLocker lock(&m_mutex);

            if (dimensions.IsValid() == false)
                return PResult::ErrorBadArgument(PString("Given dimensions are invalid: %1").Arg(dimensions.ToString()));
            if (fps == 0)
                return PResult::Error("FPS cannot be 0");

            int result = -1;

            m_dimensions = dimensions;
            m_fps = fps;

            // We'll want to pass a URI without the queries if it's a network one
            PString filename((m_uri.IsNetwork() ? m_networkURI : m_uri.ToString()));
            // If this is an RTP stream then we also need to use an RTP muxer, but if not,
            // we better pass FOURCC to libavformat so a proper container is picked
            PString formatName;
            if (m_uri.GetScheme() == "rtp")
                formatName = "rtp";
            else
            {
                // Default container to use is MP4
                // todo Here av_guess_format() returns 'H264' even though an extension .mp4 is passed, and it
                // causes to dump h264 frames without a container, why?
                // Find a way to pick a container based on either file extension or given 'fourcc' query string
                // On a side note, this also means that other extensions like .m4v will have no effect for now
#if 0
                PString fourcc;
                m_uri.GetQueryValue("fourcc", fourcc);
                formatName = av_guess_format(fourcc.c_str(), ((!IsStreaming()) ? m_uri.GetPath().c_str() : ""), NULL)->name;
#endif
                formatName = "mp4";
            }

            // Allocate a proper context for format
            if ((result = avformat_alloc_output_context2(&m_formatContext, NULL, formatName.c_str(), filename.c_str())) < 0)
            {
                return PResult::Error(PString("Cannot allocate output context: %1").Arg(FFmpegUtility::GetErrorString(errno)));
            }

            // Have a shortcut to the output format
            m_outputFormat = m_formatContext->oformat;

            // Find the encoder for given FOURCC
            m_codec = avcodec_find_encoder(m_codecID);
            if (!m_codec)
            {
                return PResult::Error(PString("Cannot find an encoder for \"%1\": %2").Arg(avcodec_get_name(m_codecID)).Arg(FFmpegUtility::GetErrorString(errno)));
            }

            // Create a new stream
            m_stream = avformat_new_stream(m_formatContext, m_codec);
            if (!m_stream)
            {
                return PResult::Error(PString("Cannot create a new stream: %1").Arg(FFmpegUtility::GetErrorString(errno)));
            }

            // Initialise codec context with default values
            if ((result = avcodec_get_context_defaults3(m_stream->codec, m_codec)) < 0)
            {
                return PResult::Error(PString("Cannot grab default values for the codec context: %1").Arg(FFmpegUtility::GetErrorString(result)));
            }

            // This is just a pointer for easy access
            m_stream->id = m_formatContext->nb_streams - 1;
            m_codecContext = m_stream->codec;
            m_stream->time_base = av_make_q(1, m_fps);
            m_codecContext->codec_id              = m_codecID;
            m_codecContext->time_base             = m_stream->time_base;

            // fixme color space should change according to FOURCC, this is the default value for h264
            m_codecContext->pix_fmt               = AV_PIX_FMT_YUV420P;
            m_codecContext->width                 = m_dimensions.GetWidth();
            m_codecContext->height                = m_dimensions.GetHeight();
            m_codecContext->coder_type            = AVMEDIA_TYPE_VIDEO;
            m_codecContext->strict_std_compliance = FF_COMPLIANCE_STRICT;
            m_codecContext->workaround_bugs       = FF_BUG_AUTODETECT;
            m_codecContext->error_concealment     = 3;
            // todo This 'tune' parameter needs more attention/research, when not set to 'zerolatency' 
            // av_interleaved_write_frame() always fails (with 'invalid parameter', without any more 
            // details, of course) (with av_write_frame() encoding is fine, but file is not playable and 
            // x264 complains 'pts has no value' and 'pts non-monotonous), which is not a must by the way,
            // if we find a correct way of setting PTS values ourselves, but until then..
            // 'zerolatency' brings in following parameters,
            // --bframes 0 --force-cfr --no-mbtree --sync-lookahead 0 --sliced-threads --rc-lookahead 0
            // (So setting it means lots of duplication in ConfigureLowLatency()) and one/more of these 
            // doesn't get along with av_interleaved_write_frame()
            // see https://trac.ffmpeg.org/wiki/StreamingGuide#Latency
            av_opt_set(m_codecContext->priv_data, "tune", "zerolatency", 0);

            // In case of network streaming, decrease latency
            if (IsStreaming())
            {
                ConfigureLowLatency();
                P_LOG_DEBUG << "Streaming mode is on, configured encoder to low-latency mode";
            }

            // Tune output quality
            if (!SetOutputQuality())
            {
                P_LOG_WARNING << "Trouble tuning output video quality";
            }

            // Tune GOP length
            if (!SetGroupOfPicturesLength())
            {
                P_LOG_WARNING << "Trouble setting GOP length value";
            }

            // Check if stream needs a stream header
            if (m_formatContext->oformat->flags & AVFMT_GLOBALHEADER)
            {
                m_codecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;
            }

            /*
             * Prepare a copy of the URI without the query strings if it's a network URI
             * libavformat expects 'udp://127.0.0.1:4444' than the full 'udp://127.0.0.1:4444&width=..'
             */
            if (IsStreaming())
            {
                m_networkURI += m_uri.GetScheme();
                m_networkURI += "://";
                m_networkURI += m_uri.GetAuthority();
                m_networkURI += m_uri.GetPath();
            }

            return PResult::C_OK;
        }


        /**
         * Opens the codec and the stream, and writes stream header.
         * @return PResult::C_OK on success, PResult::Error otherwise
         */
        PResult OpenStream()
        {
            PMutexLocker lock(&m_mutex);

            if (m_stream == NULL)
                return PResult::Error("Cannot open a stream that has not been CreateStream()ed");
            if (m_isOpen)
                return PResult::Error("Stream is already open");

            int result = -1;

            // Open the codec
            if ((result = avcodec_open2(m_codecContext, m_codec, (m_codecOptions) ? &m_codecOptions : NULL)) < 0)
            {
                return PResult::Error(PString("Cannot open codec: %1").Arg(FFmpegUtility::GetErrorString(result)));
            }

            // We'll want to open a URI without the queries if it's a network type, if not,
            // then just the file path of it
            PString filename((m_uri.IsNetwork() ? m_networkURI : m_uri.GetPath().ToString()));

#if FFMPEG_DEBUG
            av_dump_format(m_formatContext, 0, filename.c_str(), 1);
#endif

            // Open the output medium if there is one
            if (!(m_formatContext->oformat->flags & AVFMT_NOFILE))
            {
                if ((result = avio_open(&m_formatContext->pb, filename.c_str(), AVIO_FLAG_WRITE)) < 0)
                {
                    return PResult::Error(PString("Could not open '%1': %2").Arg(filename).Arg(FFmpegUtility::GetErrorString(result).c_str()));
                }
            }

            // Finally, open the stream and write the stream header
            if ((result = avformat_write_header(m_formatContext, NULL)) < 0)
            {
                return PResult::Error(PString("Cannot write stream header of \"%1\": %2").Arg(m_uri.ToString()).Arg(FFmpegUtility::GetErrorString(result)));
            }

            // libav does not have an is_open() so a successful avformat_write_header() call
            // is the only case that flags a successful opening of a stream
            m_isOpen = true;
            return PResult::C_OK;
        }


        /**
         * Closes a stream.
         * @return PResult::C_OK on success, PResult::Error otherwise
         */
        PResult CloseStream()
        {
            PMutexLocker lock(&m_mutex);

            if (m_isOpen)
            {
                // Write the stream header if we're dealing with an RTSP stream
                int result = -1;
                if ((result = av_write_trailer(m_formatContext)) < 0)
                {
                    return PResult::Error(PString("Cannot write stream trailer of '%1': %2").Arg(m_uri.ToString()).Arg(FFmpegUtility::GetErrorString(result)));
                }

                avio_flush(m_formatContext->pb);

                if ((result = avio_close(m_formatContext->pb)) < 0)
                {
                    return PResult::Error(PString("Cannot close output file: %1").Arg(FFmpegUtility::GetErrorString(result)));
                }

                m_isOpen = false;
            }

            return PResult::C_OK;
        }


        /**
         * Encodes given PImage and puts encoded data into incoming AVPacket*
         * @param image PImage object to encode
         * @param encodedPacket AVPacket to hold the encoded image
         * @return PResult::C_OK on success, PResult::Error otherwise
         */
        PResult EncodeFrame(const PImage& image, AVPacket* encodedPacket)
        {
            PMutexLocker lock(&m_mutex);
            int result = -1;

            // Map incoming PImage pixel format to libavutil's corresponding pixel format
            enum AVPixelFormat sourcePixelFormat = AV_PIX_FMT_NONE;
            switch (image.GetPixelFormat())
            {
                case PImage::E_BGR8U: sourcePixelFormat = AV_PIX_FMT_BGR24; break;
                case PImage::E_RGB8U: sourcePixelFormat = AV_PIX_FMT_RGB24; break;
                default: return PResult::Error("This pixel format is not supported by this video output plugin");
            }

            // A conversion between pixel formats (and resolutions as well..). Here incoming
            // pixel format will be converted to one that is used by H.264, i.e. AV_PIX_FMT_YUV420P.
            if ((m_conversionContext = sws_getContext(image.GetWidth(), image.GetHeight(), sourcePixelFormat,
                                                      m_dimensions.GetWidth(), m_dimensions.GetHeight(),
                                                      AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL)) == NULL)
                return PResult::Error("Cannot create a conversion context");

            // Allocate frames: source frame is the one that we copy data from incoming PImage's
            // internal buffer. This frame is fed into sws_scale() and its output is stored in
            // scaled frame. None of these two are the final encoded frame yet.
            QScopedPointer<AVFrame, AVFrameDeleter> sourceFrame(av_frame_alloc());
            QScopedPointer<AVFrame, AVFrameDeleter> scaledFrame(av_frame_alloc());

            if (!sourceFrame || !scaledFrame)
            {
                return PResult::Error("Cannot allocate source and/or scaled frame");
            }

            // Allocate source frame, i.e. the input to sws_scale()
            if ((result = av_image_alloc(sourceFrame->data, sourceFrame->linesize, image.GetWidth(), image.GetHeight(), sourcePixelFormat, 1)) < 0)
            {
                return PResult::Error(PString("Cannot allocate source AVFrame's buffers: %1").Arg(FFmpegUtility::GetErrorString(result)));
            }

            // Copy image data into source AVFrame from PImage
            for (int32 h = 0; h < image.GetHeight(); h++)
                memcpy(&(sourceFrame->data[0][h * sourceFrame->linesize[0]]),
                       &(image.GetDataPtr()[h * image.GetWidth() * image.GetSizeOfPixel()]),
                       image.GetWidth()*3);

            // Allocate destination frame, i.e. output from sws_scale()
            if ((result = av_image_alloc(scaledFrame->data, scaledFrame->linesize,
                                         m_dimensions.GetWidth(), m_dimensions.GetHeight(),
                                         AV_PIX_FMT_YUV420P, 1)) < 0)
            {
                return PResult::Error(PString("Cannot allocate destination AVFrame's buffers: %1").Arg(FFmpegUtility::GetErrorString(result)));
            }

            // Perform the conversion
            sws_scale(m_conversionContext, sourceFrame->data, sourceFrame->linesize,
                      0, image.GetHeight(), scaledFrame->data, scaledFrame->linesize);
            sws_freeContext(m_conversionContext);

            // Set dimensions and pixel format of sws_scale() output
            scaledFrame->format = AV_PIX_FMT_YUV420P;
            scaledFrame->width = m_dimensions.GetWidth();
            scaledFrame->height = m_dimensions.GetHeight();

            // Perform the encoding
            int gotFrame; // Set by encoder if there is an encoded frame
            if ((result = avcodec_encode_video2(m_codecContext, encodedPacket, scaledFrame.data(), &gotFrame)) < 0)
            {
                return PResult::Error(PString("Cannot encode frame \"%1\": %2").Arg(image.ToString()).Arg(FFmpegUtility::GetErrorString(result)));
            }

            if (gotFrame)
                return PResult::C_OK;

            return PResult::Error("No frames were encoded");
        }


        /**
         * Writes incoming PImage to the stream.
         * @return PResult::C_OK on success, PResult::Error otherwise
         */
        PResult WriteFrame(const PImage& image)
        {
            PMutexLocker lock(&m_mutex);

            if (!IsOpen())
                return PResult::Error("Incoming stream is not opened, cannot write frame..");

            if (IsStreaming())
                // If this is a networked stream we'll count how much it takes to send this frame
                // See the other comment below in this method for further info
                m_timer.Start();

            // Here we initialise fields of an AVPacket with default values, internal buffer
            // will be allocated by the encoder
            AVPacket* encodedPacket = new AVPacket();
            if (!encodedPacket)
                return PResult::ErrorNullPointer("Cannot allocate a packet");

            av_init_packet(encodedPacket);
            encodedPacket->data = NULL;
            encodedPacket->size = 0;
            encodedPacket->dts = AV_NOPTS_VALUE;
            encodedPacket->pts = AV_NOPTS_VALUE;
            encodedPacket->duration = 0;

            if (EncodeFrame(image, encodedPacket).Ok())
            {
                encodedPacket->stream_index = m_stream->index;
                encodedPacket->pts = av_rescale_q_rnd(encodedPacket->pts,
                                                      m_stream->codec->time_base,
                                                      m_stream->time_base,
                                                      AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                encodedPacket->dts = av_rescale_q_rnd(encodedPacket->dts,
                                                      m_stream->codec->time_base,
                                                      m_stream->time_base,
                                                      AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

                int result = -1;
                if ((result = av_interleaved_write_frame(m_formatContext, encodedPacket)) < 0)
                {
                    P_LOG_ERROR << "Error while writing video frame: " << FFmpegUtility::GetErrorString(result);
                    if (result == AVERROR(EPIPE))
                    {
                        return PResult::ErrorStreamClosed(PString("Client is not connected anymore, streaming will stop: %1").Arg(FFmpegUtility::GetErrorString(result)));
                    }
                }

                if (IsStreaming())
                {
                    // We are supposed to send a frame every 1000 / FPS milliseconds, so if a
                    // frame's transfer takes less than this, we should idle around until it's time to send next frame
                    PThread::Sleep((1000 / m_fps) - m_timer.ElapsedMs());
                }
            }

            // Cleanup
            av_packet_free_side_data(encodedPacket);
            av_packet_unref(encodedPacket);

            return PResult::C_OK;
        }


        /**
         * Decreases encoding latency by tuning some encoder parameters
         * @return none
         */
        void ConfigureLowLatency()
        {
            // B-type frames delay the encoding by max_b_frames+1 relative to the input
            m_codecContext->max_b_frames = 0;
            av_dict_set(&m_codecOptions, "bframes", "0", 0);

            // Set codec delay to zero
            m_codecContext->delay = 0;
            // Set a low level of reference frames window
            m_codecContext->refs = 3;
            av_opt_set(m_codecContext->priv_data, "tune", "zerolatency", 0);
            av_opt_set(m_codecContext->priv_data, "rc_lookahead", "0", 0);

            av_dict_set(&m_codecOptions, "sync-lookahead", "0", 0);
            av_dict_set(&m_codecOptions, "rc-lookahead", "0", 0);
            av_opt_set(m_codecContext->priv_data, "x264opts", "no-mbtree", 0);
            // Set options to reduce memory overhead of x264 codec
            // Note: This option may change the number of I-frames, hence altering gop_length
            av_dict_set(&m_codecOptions, "scenecut", "-1", 0);
        }

        /**
         * Sets quality-related properties of the codec with respect to incoming query string 'quality'
         * @return true on success, false otherwise
         */
        bool SetOutputQuality()
        {
            if (!m_uri.GetQueryValue("quality", m_quality))
            {
                P_LOG_DEBUG << "No specific output quality requested, defaulting to 'medium'";
                // Let's have 'medium' by default
                m_quality = "medium";
            }
            else if (m_quality != "high" && m_quality != "medium" && m_quality != "low")
            {
                P_LOG_WARNING << "Value '" << m_quality << "' for query string 'quality' is not valid, defaulting to 'medium'";
                m_quality = "medium";
            }

            if (!m_codecContext)
            {
                P_LOG_WARNING << "Cannot set quality before codec context is initialised";
                return false;
            }

            long highQualityBitRate = m_dimensions.GetHeight() * m_dimensions.GetWidth();

            m_codecContext->flags                   |= CODEC_FLAG_BITEXACT;
            m_codecContext->flags                   |= CODEC_FLAG_PASS1;
            m_codecContext->flags                   |= CODEC_FLAG_LOOP_FILTER;

            if (m_quality == "high")
            {
                m_codecContext->bit_rate             = highQualityBitRate;
                m_codecContext->bit_rate_tolerance   = m_codecContext->bit_rate / 8;
                m_codecContext->qmin                 = 10;
                m_codecContext->qmax                 = 20;
            }
            else if (m_quality == "medium")
            {
                m_codecContext->bit_rate             = highQualityBitRate / 2;
                m_codecContext->bit_rate_tolerance   = m_codecContext->bit_rate / 4;
                m_codecContext->qmin                 = 20;
                m_codecContext->qmax                 = 30;
            }
            else if (m_quality == "low")
            {
                m_codecContext->bit_rate             = highQualityBitRate / 4;
                m_codecContext->bit_rate_tolerance   = m_codecContext->bit_rate / 2;
                m_codecContext->qmin                 = 30;
                m_codecContext->qmax                 = 35;
            }

            P_LOG_DEBUG << PString("Output video quality set to '%1'").Arg(m_quality);

            return true;
        }

        /**
         * Sets Group of Pictures value with respect to incoming query string 'gop_length'
         * @return true on success, false otherwise
         */
        bool SetGroupOfPicturesLength()
        {
            if (!m_codecContext)
            {
                P_LOG_WARNING << "Cannot set quality before codec context is initialised";
                return false;
            }

            if (!m_uri.GetQueryValue("gop_length", m_gopLength))
            {
                P_LOG_DEBUG << "No specific GOP length was requested, defaulting to FPS/2 if FPS is know, to 12 if not";
                if (m_fps > 2)
                    m_gopLength = m_fps / 2;
                else
                    m_gopLength = 12;
            }

            m_codecContext->gop_size = m_gopLength;

            P_LOG_DEBUG << PString("GOP length is set to '%1', %2").
                    Arg(m_gopLength).
                    Arg(((m_gopLength) ? PString("there will be an I-frame encoded in every %1 frame(s)").Arg(m_gopLength) : "there will be only I-frames"));

            return true;
        }

    private:
        AVFormatContext*    m_formatContext;
        AVOutputFormat*     m_outputFormat;      // Shortcut for AVFormatContext->oformat
        AVStream*           m_stream;
        AVCodec*            m_codec;
        AVCodecContext*     m_codecContext;      // Shortcut for AVStream->codec
        AVDictionary*       m_codecOptions;      // Parameters for the encoding
        PString             m_networkURI;        // This is the network related part of a longer URI, i.e. without queries
        PTimer              m_timer;             // This is used to ensure a certain FPS while streaming
        PSizei              m_dimensions;
        SwsContext*         m_conversionContext;
        uint32              m_fps;
        PString             m_quality;           // Can be 'high', 'medium', 'low'
        int32               m_gopLength;         // Can be 0 to INT_MAX
};

#endif /* SDK_SRC_PLUGINS_WRAPPERS_FFMPEG_PFFMPEGOUTPUTSTREAMHANDLER_H_ */
