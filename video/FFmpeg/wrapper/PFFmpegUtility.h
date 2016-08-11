/*
 * Copyright (C) 2016 Digital Barriers plc. All rights reserved.
 * Contact: http://www.digitalbarriers.com/
 *
 * This file is part of the Papillon SDK.
 *
 * You can't use, modify or distribute any part of this file without
 * the explicit written agreements of Digital Barriers plc.
 */

#ifndef SDK_SRC_PLUGINS_WRAPPERS_FFMPEG_PFFMPEGUTILITY_H_
#define SDK_SRC_PLUGINS_WRAPPERS_FFMPEG_PFFMPEGUTILITY_H_

#include <PResult.h>
#include <PGuid.h>
#include <PLog.h>
using namespace papillon;

#include <QtCore/qscopedpointer.h>

// FFmpeg includes
extern "C"
{
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
    #include <libavformat/avformat.h>
    #include <libavutil/imgutils.h>
    #include <libavutil/opt.h>
    #include <libavutil/error.h>
}

#define FFMPEG_DEBUG 0

// This is a custom deleter to have QScopedPointer work for AVFrame* type
struct AVFrameDeleter
{
    static inline void cleanup(AVFrame* frame)
    {
        av_freep(frame->data);
        av_frame_free(&frame);
    }
};

/**
 * @file PFFmpegUtility.h
 * @brief Class FFmpegUtility Provides utility methods for video streams by wrapping libavutil and others.
 */
class FFmpegUtility
{
    public:
        /**
         * Prints parameter,value pairs in given AVDictionary (which is a property of an AVFormatContext).
         * @param AVDictionary to grab parameter,value pairs from.
         * @return none.
         */
        static void PrintDictionary(const AVDictionary* const dictionary)
        {
            AVDictionaryEntry *dictionaryItem = NULL;

            P_LOG_INFO << "Metadata dictionary has " << av_dict_count(dictionary) << " item(s):";
            while ((dictionaryItem = av_dict_get(dictionary, "", dictionaryItem, AV_DICT_IGNORE_SUFFIX)) != NULL)
                P_LOG_INFO << PString(" [%1=%2]").Arg(dictionaryItem->key).Arg(dictionaryItem->value);
        }


        /**
         * Prints stream information of given AVFormatContext.
         * @param AVFormatContext to grab stream information from.
         * @return none.
         */
        static void PrintStreams(const AVFormatContext* const formatContext)
        {
            P_LOG_INFO << "There are " << formatContext->nb_streams << " stream(s)";
            for (unsigned int i = 0; i < formatContext->nb_streams; i++)
                P_LOG_INFO << "Stream[index=" << i
                           << ", codec= " << av_get_media_type_string(formatContext->streams[i]->codec->codec_type)
                           << "]";
        }


        /**
         * Returns the duration of the video stream in seconds.
         * @param AVFormatContext to grab video stream from.
         * @return Duration of the video stream in seconds, a negative value on error.
         */
        static int64_t GetDuration(const AVFormatContext* const formatContext)
        {
            // Validate `value' against video's duration
            if (formatContext->duration != AV_NOPTS_VALUE)
            {
                return (formatContext->duration + 5000) / AV_TIME_BASE;
            }

            return -1;
        }


        /**
         * Returns FPS of given video stream.
         * @param AVStream of relevant video stream.
         * @return FPS value as a floating point value.
         */
        static double GetFPS(const AVStream* const stream)
        {
            return av_rescale(static_cast<double>(stream->time_base.den),
                              static_cast<double>(stream->time_base.num),
                              1000);
        }


        /**
         * Seeks to given timestamp in seconds on given video stream. A call to Reset() should follow a
         * call to this function to reset internal codec state.
         * @param formatContext AVFormatContext to grab video stream from.
         * @param videoStreamIndex Index of the video stream, i.e. AVFormatContext->streams[index].
         * @param timeStampInSeconds Self-explanatory, e.g. 13.75.
         * @return PResult::C_OK on success, PResult::Error otherwise.
         * @see Reset()
         */
        static PResult SeekTo(AVFormatContext* const formatContext, int videoStreamIndex, double timeStampInSeconds)
        {
            // Convert timestamp into AVStream's time_base
            int64_t timeStampToSeek = av_rescale(timeStampInSeconds,
                                                 formatContext->streams[videoStreamIndex]->time_base.den,
                                                 formatContext->streams[videoStreamIndex]->time_base.num);

#if FFMPEG_DEBUG
            P_LOG_DEBUG << "Requested timestamp to seek to is '" << timeStampInSeconds << "'";
            P_LOG_DEBUG << "Same timestamp in 3rd-party decoder's time_base is '" << timeStampToSeek << "'";
#endif

            int result = -1;
            if ((result = av_seek_frame(formatContext, videoStreamIndex, timeStampToSeek, AVSEEK_FLAG_ANY)) < 0)
            {
                return PResult::Error(PString("Failed to seek Video: %1").Arg(GetErrorString(result)));
            }

            return PResult::C_OK;
        }


        /**
         * Resets internal decoder state and buffers. This should be called after a SeekTo() call.
         * @param codecContext AVCodecContext to reset internal state.
         * @return PResult::C_OK on success PResult::Error otherwise.
         * @see SeekTo()
         */
        static PResult Reset(AVCodecContext* const codecContext)
        {
            if (codecContext)
            {
                avcodec_flush_buffers(codecContext);
                return PResult::C_OK;
            }

            return PResult::Error("Incoming codec context is not valid");
        }


        /**
         * Returns the error log string for the given FFmpeg error code.
         * @param errorCode libav error code.
         * @return Error log string as a PString.
         */
        static PString GetErrorString(int errorCode)
        {
            const unsigned errorMessageLength = 256;
            char errorMessage[errorMessageLength] = { 0 };
            av_make_error_string(errorMessage, errorMessageLength, errorCode);
            return PString(const_cast<const char*>(errorMessage));
        }


        /**
         * Creates a PFrame from an AVFrame as if by magic.
         * @codecContext AVCodecContext that has been initialised previously.
         * @param avFrame A frame produced by avcodec_decode_video2(), for example.
         * @pFrame a papillon:PFrame object to fill with AVFrame's data.
         * @return PResult::C_OK on success, PResult::Error otherwise.
         */
        static PResult CreatePImageFromAVFrame(const AVCodecContext* codecContext, const AVFrame* avFrame, PFrame& pFrame)
        {
            AVFrame *outputFrameScaled    = NULL;  // Frame that sws_scale()'s output is stored
            uint8_t *outputFrameBuffer    = NULL;  // Size of the inner buffer of output AVFrame
            int outputFrameSize           = 0;     // Size of the final frame that sws_scale() outputs
            SwsContext *conversionContext = NULL;
            int result                    = -1;    // Keeps return value of various FFmpeg API methods

            // Allocate frame that will keep the output of sws_scale()
            outputFrameScaled = av_frame_alloc();
            if (!outputFrameScaled)
            {
                return PResult::Error("Cannot allocate a frame");
            }

            // Determine and allocate required size for conversion buffer
            if ((outputFrameSize = avpicture_get_size(AV_PIX_FMT_BGR24,
                                                      codecContext->width, codecContext->height)) < 0)
            {
                return PResult::Error(PString("Cannot calculate size for a picture: %1").Arg(GetErrorString(outputFrameSize)));
            }

            outputFrameBuffer = (uint8_t *) av_malloc(outputFrameSize * sizeof(uint8_t));

            /**
             * Here we need to check if source pixel format is one of the deprecated ones
             * in pixfmt.h. This happens only when reading MJPEG files, and I don't know why
             * libavcodec is initialising the codec with a deprecated value. It is apparently
             * handled in libswscale/utils.c:handle_jpeg() but it might be buggy. This suppresses
             * the warning "Deprecated pixel format used, make sure you did set range correctly" anyway.
             */
            AVPixelFormat sourcePixelFormat;
            switch (codecContext->pix_fmt) {
                case AV_PIX_FMT_YUVJ420P: sourcePixelFormat = AV_PIX_FMT_YUV420P; break;
                case AV_PIX_FMT_YUVJ422P: sourcePixelFormat = AV_PIX_FMT_YUV422P; break;
                case AV_PIX_FMT_YUVJ444P: sourcePixelFormat = AV_PIX_FMT_YUV444P; break;
                case AV_PIX_FMT_YUVJ440P: sourcePixelFormat = AV_PIX_FMT_YUV440P; break;
                default: sourcePixelFormat = codecContext->pix_fmt; break;
            }

            // Retrieve a conversion context
            conversionContext = sws_getContext(codecContext->width, codecContext->height,
                                               sourcePixelFormat,
                                               codecContext->width, codecContext->height,
                                               AV_PIX_FMT_BGR24,
                                               SWS_BILINEAR, NULL, NULL, NULL);

            // Associate picture buffer with picture struct
            if ((result = avpicture_fill((AVPicture *) outputFrameScaled, outputFrameBuffer,
                                         AV_PIX_FMT_BGR24,
                                         codecContext->width, codecContext->height)) < 0)
            {
                return PResult::Error(PString("Cannot attach a buffer to the frame: %1").Arg(GetErrorString(result)));
            }

            // Scale or convert the image if a change in size or pixel format is requested
            sws_scale(conversionContext,
                      (uint8_t const * const *)avFrame->data,
                      avFrame->linesize,
                      0,
                      codecContext->height,
                      outputFrameScaled->data,
                      outputFrameScaled->linesize);
            sws_freeContext(conversionContext);

            // Copy AVFrame data into a PImage buffer
            PImage image(codecContext->width, codecContext->height, PImage::E_BGR8U);
            for (int32 h = 0; h < image.GetHeight(); h++)
            {
                memcpy(&(image.GetDataPtr()[h * image.GetWidth() * image.GetSizeOfPixel()]),
                       &(outputFrameScaled->data[0][h * outputFrameScaled->linesize[0]]),
                       image.GetWidth() * 3);
            }

            // FIXME Following is a temporary fix, perhaps we should get the current time
            // at the beginning of each decoding, and set this timestamp by adding pFrame.pts to it
            pFrame.SetTimestampToCurrentUTC();
            pFrame.SetNewImage(image, PGuid::CreateUniqueId(), PGuid::CreateUniqueId());
            // FIXME inputFrame->coded_picture_number will diverge in case of seeking
            pFrame.SetFrameNumber(static_cast<int32>(avFrame->coded_picture_number));
            pFrame.SetTimestampToCurrentUTC();

            // Cleanup
            av_free(outputFrameBuffer);
            av_free(outputFrameScaled);

            return PResult::C_OK;
        }
};

#endif /* SDK_SRC_PLUGINS_WRAPPERS_FFMPEG_PFFMPEGUTILITY_H_ */
