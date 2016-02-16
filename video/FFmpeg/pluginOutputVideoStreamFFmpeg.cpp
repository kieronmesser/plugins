/*
 * Copyright (C) 2015 Digital Barriers plc. All rights reserved.
 * Contact: http://www.digitalbarriers.com/
 *
 * This file is part of the Papillon SDK.
 *
 * You can't use, modify or distribute any part of this file without
 * the explicit written agreements of Digital Barriers plc.
 */
#define PAPILLON_EXPORT_CORE_PLUGIN

#include <PapillonCore.h>
#include <PPluginInterface.h>
#include <POutputVideoStreamInterface.h>

#include "PFFmpegOutputStreamHandler.h"

const PString PRODUCT_NAME         = "FFmpegOutputVideoStream";
const PString PRODUCT_VERSION      = "1.0";
const PGuid   PRODUCT_GUID("{7BA82022-E5DA-4898-84A4-F772F549F35E}");

const int32   DEFAULT_WIDTH        = 720;
const int32   DEFAULT_HEIGHT       = 576;
const double  DEFAULT_FPS          = 24.0;
const PString DEFAULT_CODEC_FOURCC = "H264";

class POutputVideoStreamFFmpeg : public POutputVideoStreamInterface
{
public:
    POutputVideoStreamFFmpeg(const PUri& uri)
        : POutputVideoStreamInterface(uri)
        , m_isOpened                 (false)
        , m_fps                      (0.0)
        , m_outputDimensions         (DEFAULT_WIDTH, DEFAULT_HEIGHT)
        , m_codecFourcc              (DEFAULT_CODEC_FOURCC)
        , m_encoder                  (0)
    {}


    virtual ~POutputVideoStreamFFmpeg()
    {
        if (IsOpened())
            Release();

        delete m_encoder;
    }


    virtual PString GetName() const
    {
        return PRODUCT_NAME;
    }


    virtual bool IsOpened() const
    {
        return m_isOpened;
    }


    virtual PResult Open()
    {
        if (m_isOpened)
            return PResult::Error("Open() called on an already opened stream");

        // Validate URI
        if (!m_uri.IsValid())
            return PResult::ErrorBadArgument(PString("Invalid URI: '%1'").Arg(m_uri.ToString()));

        // Instantiate a stream handler
        m_encoder = new PFFmpegOutputStreamHandler();
        if (!m_encoder)
            return PResult::ErrorNullPointer("Cannot allocate a stream handler");

        // Initialise encoder according to the URI
        PResult initialiseResult = m_encoder->Initialise(m_uri);
        if (initialiseResult.Failed())
        {
            return initialiseResult.Push(PResult::Error(PString("Cannot initialise FFmpeg stream with the URI '%1'").Arg(m_uri.ToString())));
        }

        // Only H264 is supported for now
        if (m_codecFourcc.Compare("H264"))
        {
            return PResult::Error(PString("Requested FOURCC '%1' is not supported").Arg(m_codecFourcc));
        }

        m_fps = DEFAULT_FPS;
        if (!m_uri.GetQueryValue("fps", m_fps))
        {
            m_fps = DEFAULT_FPS;
        }

        // Retrieve requested dimensions from the query string
        int width, height;
        if (!m_uri.GetQueryValue("width", width) || !m_uri.GetQueryValue("height", height))
        {
            m_outputDimensions.SetWidth(DEFAULT_WIDTH);
            m_outputDimensions.SetHeight(DEFAULT_HEIGHT);
            P_LOG_WARNING << "Query strings 'width' or 'height', or both are missing, they are defaulted to " << m_outputDimensions.ToString();
        }
        else
        {
            m_outputDimensions.SetWidth(width);
            m_outputDimensions.SetHeight(height);
            m_outputDimensions.Clamp(16, 4096);
        }

        // Create a video stream
        PResult streamResult = m_encoder->CreateStream(m_outputDimensions, m_fps);
        if (streamResult.Failed())
        {
            return streamResult.Push(PResult::ErrorStreamNotAvailable("Cannot create a stream"));
        }

        // Open the video stream
        PResult encoderResult = m_encoder->OpenStream();
        if (encoderResult.Failed())
        {
            return encoderResult.Push(PResult::ErrorStreamNotAvailable("Either file or RTSP stream is not available"));
        }

        m_isOpened = true;
        return PResult::C_OK;
    }


    virtual PResult Release()
    {
        if (!m_isOpened)
            return PResult::Error("output video stream is not opened");

        m_isOpened = false;
        m_encoder->CloseStream();

        return PLicensing::GetInstance().CheckInLicense(PRODUCT_NAME);
    }


    virtual PResult PutImage(const PImage& image)
    {
        if (!m_isOpened)
            return PResult::Error("output video stream is not opened");

        PResult result = m_encoder->WriteFrame(image);
        if (result.Failed())
            return PResult::Error("Cannot write image to the output stream").Push(result);

        return result;
    }

private:
    bool                        m_isOpened;
    double                      m_fps;
    PSizei                      m_outputDimensions;
    PString                     m_codecFourcc;
    PFFmpegOutputStreamHandler* m_encoder;
};

namespace papillon
{
    void OnLoad(PResult& ret)
    {
        ret = PResult::C_OK;
    }


    void OnUnload(PResult& ret)
    {
        ret = PResult::C_OK;
    }


    void About(PString& productName, PVersion& productVersion, PGuid& productGuid, PString& briefDescription)
    {
        productName      = PRODUCT_NAME;
        productVersion   = PVersion(PRODUCT_VERSION);
        productGuid      = PRODUCT_GUID;
        briefDescription = "Write H.264 video stream to file or an RTSP stream";
    }


    void CreateOutputVideoStreamImpl(const PUri& uri, POutputVideoStreamInterface** outputVideoStreamImpl, PResult& ret)
    {
        P_LOG_DEBUG << PRODUCT_NAME << ": try to open video stream using " << PRODUCT_NAME << " v" << PRODUCT_VERSION << ", source is \"" << uri << "\"";
        if (!uri.IsValid())
        {
            ret = PResult::ErrorBadURIFormat(PString("Invalid URI scheme: \"%1\"").Arg(uri.ToString()));
            return;
        }
        *outputVideoStreamImpl = new POutputVideoStreamFFmpeg(uri);
        ret = PResult::C_OK;
    }
}
