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

#include <PFFmpegInputStreamHandler.h>

namespace papillon
{
    const PString   PRODUCT_NAME = "FFmpegInputVideoStream";
    const PString   PRODUCT_VERSION = "1.0";
    const PGuid     PRODUCT_GUID("{AA0B7F5E-B593-4E91-B83E-FE947B4615AD}");
    const int       DEFAULT_MAX_PENDING_FRAMES = 1;

    class PInputVideoStreamFFmpeg: public PInputVideoStreamInterface, public PRunnable
    {
        public:

            PInputVideoStreamFFmpeg(const PUri& uri)
                : PInputVideoStreamInterface    (uri)
                , m_isOpened                    (false)
                , m_loop                        (false)
                , m_streamHandler               (0)
                , m_threadRetrieveImages        ()
                , m_isThreadRunning             (false)
                , m_concurrentQueueFrames       (DEFAULT_MAX_PENDING_FRAMES)
            {
            }


            virtual ~PInputVideoStreamFFmpeg()
            {
                if (IsOpened())
                    Release();
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
                {
                    return PResult::Error("Video stream is already opened");
                }

                // First things first, check if we have a license
                if (PLicensing::GetInstance().CheckOutLicense(PRODUCT_NAME, PRODUCT_VERSION).Failed())
                {
                    return PResult::ErrorFailedToCheckOutLicense(PRODUCT_NAME, PRODUCT_VERSION);
                }

                // Validate given URI
                if (!m_uri.IsValid())
                    return PResult::Error(PString("Invalid URI: \"%1\"").Arg(m_uri.ToString()));

                // If URI points to a file, check if it exists
                PString uri = m_uri.ToString();
                if (m_uri.IsFile())
                {
                    uri = m_uri.GetPath();
                    if (!PFile::Exists(uri))
                    {
                        return PResult::ErrorFileNotFound(uri);
                    }
                }
                // if not, then encode it so we don't have special characters like '+' in it
                else
                {
                    m_uri.Encode();
                }

                // Create the stream handler
                m_streamHandler = new PFFmpegInputStreamHandler();
                if (!m_streamHandler)
                    return PResult::Error("Cannot allocate a stream handler");

                // Initialise encoder according to the URI
                PResult initialiseResult = m_streamHandler->Initialise(m_uri);
                if (initialiseResult.Failed())
                {
                    return initialiseResult.Push(PResult::Error("Cannot initialise FFmpeg stream with this URI"));
                }

                PResult createStream = m_streamHandler->CreateStream();
                if (createStream.Failed())
                {
                    return createStream.Push(PResult::Error(PString("Cannot create a stream to file '%1'").Arg(m_uri.GetPath())));
                }

                PResult openStream = m_streamHandler->OpenStream();
                if (openStream.Failed())
                {
                    return openStream.Push(PResult::Error(PString("Cannot open a stream to file '%1'").Arg(m_uri.GetPath())));
                }

                // If a valid 'startFrame' query string is given along with the URI
                // seek to it if the stream is not a network stream
                if (m_streamHandler->IsStreaming() == false)
                {
                    int32 startFrame = 0;
                    if (m_uri.GetQueryValue("startFrame", startFrame))
                    {
                        if (startFrame > 0)
                        {
                            PResult seekToFrame = m_streamHandler->SeekToFrame(startFrame);
                            if (seekToFrame.Failed())
                            {
                                return PResult::Error(PString("Cannot seek to frame '%1'").Arg(startFrame)).Push(seekToFrame);
                            }
                        }
                    }
                }

                // Set loop flag, if it's true GetFrame() will seek to the beginning after hitting EOS
                PString loop;
                m_uri.GetQueryValue("loop", loop);
                m_loop = (loop.ToUpper() == "TRUE");
                if (m_loop)
                    P_LOG_WARNING << "Mind that you'll not have a useful video file with 'loop' option unless you call POutputVideoStream::Release() at some point.";

                if (m_uri.GetScheme() == "rtsp")
                {
                    // start background thread to retrieve images at max speed
                    m_isThreadRunning = true;
                    m_threadRetrieveImages = PThread(*this);
                }

                m_isOpened = true;
                return PResult::C_OK;
            }


            virtual PResult Release()
            {
                if (m_streamHandler)
                {
                    delete m_streamHandler;
                    m_streamHandler = NULL;
                }

                if (!m_isOpened)
                    return PResult::Error("video stream not opened yet");

                // first, stop background thread which uses 0-mq socket 
                // (0-mq is not thread-safe; calling m_socket.Close() may crash the app)
                m_isThreadRunning = false;
                m_threadRetrieveImages.Join();

                m_isOpened = false;
                return PLicensing::GetInstance().CheckInLicense(PRODUCT_NAME);
            }


            virtual PResult GetFrame(PFrame& frame, int32 timeOutMs)
            {
                if (!m_isOpened)
                    return PResult::ErrorInvalidState("video stream not opened");

                if (m_isThreadRunning)
                {
                    if (!m_concurrentQueueFrames.TryDequeue(frame, timeOutMs))
                        return PResult::Error(PString("Failed to retrieve a frame: time-out (more than %1 ms elapsed)").Arg(timeOutMs));
                }
                else
                {
                    // Read a frame from the stream
                    PResult result = m_streamHandler->ReadFrame(frame);
                    if (result.Failed())
                    {
                        if (result.GetCode() == PResult::E_ERROR_END_OF_STREAM)
                        {
                            // Failed, but if it's EOS and 'loop' flag is set then we'll seek to the beginning and keep reading
                            if (m_loop)
                            {
                                // Reset() the stream in case a seek has already taken place, and seek to the beginning
                                if (m_streamHandler->Reset().Ok() && m_streamHandler->SeekToFrame(0).Ok())
                                {
                                    result = m_streamHandler->ReadFrame(frame);
                                    if (result.Failed())
                                        return PResult::Error("Cannot read a frame after seeking to the beginning for looping the video");
                                }
                                else
                                    return PResult::Error("Tried to loop the video yet failed to seek to the beginning of the file");
                            }
                            else
                                return result.Push(PResult::Error("End of stream reached"));
                        }
                        else
                            // It's another error than hitting EOS, so just return the error
                            return result.Push(PResult::Error("Cannot read a frame from stream"));
                    }
                }

                frame.SetSourceId(PRODUCT_GUID);
                return PResult::C_OK;
            }


            virtual bool HasLength() const
            {
                return true;
            }


            virtual bool CanSeek() const
            {
                return true;
            }


            virtual PResult Get(const PString& propertyName, int32& value) const
            {
                if (!m_isOpened)
                    return PResult::Error("Cannot get properties of a closed stream");

                if (propertyName.ToUpper() == "WIDTH")
                {
                    value = m_streamHandler->GetWidth();
                    return PResult::C_OK;
                }
                else if (propertyName.ToUpper() == "HEIGHT")
                {
                    value = m_streamHandler->GetHeight();
                    return PResult::C_OK;
                }
                else if (propertyName.ToUpper() == "FRAME_COUNT")
                {
                    value = m_streamHandler->GetDuration();
                    return PResult::C_OK;
                }
                else if (propertyName.ToUpper() == "FRAME_NUMBER")
                {
                    value = m_streamHandler->GetFrameNumber();
                    return PResult::C_OK;
                }

                return PResult::ErrorBadArgument(PString("Unexpected propertyName (%1)").Arg(propertyName));
            }


            virtual void Run()
            {
                while (m_isThreadRunning)
                {
                    PFrame frame;
                    PResult readFrame = m_streamHandler->ReadFrame(frame).LogErrorIfAny(PString("Reading frame from %1").Arg(m_uri.ToString()));
                    if (readFrame.Ok())
                    {
                        // push the new image to the queue; oldest images are dropped (=> keep only the N most recent images)
                        m_concurrentQueueFrames.Enqueue(frame);
                    }
                    PThread::Sleep(1);
                }
            }


            virtual PResult Get(const PString& property, double& value) const
            {
                if (!m_isOpened)
                    return PResult::Error("Cannot query properties on a closed stream");

                value = PMath::NaNd;

                if (property.ToUpper() == "FPS")
                {
                    value = m_streamHandler->GetFPS();
                    return PResult::C_OK;
                }

                return PResult::ErrorBadArgument(PString("Unsupported property (%1)").Arg(property));
            }


            virtual PResult Set(const PString& property, int32 value)
            {
                if (!m_isOpened)
                    return PResult::Error("Cannot set properties on a closed stream");

                if (property.ToUpper() == "GO_TO_FRAME")
                {
                    if (!m_uri.IsFile())
                        return PResult::Error("Seeking is available only for file streams");

                    return m_streamHandler->SeekToFrame(value);
                }
                else if (property.ToUpper() == "RESET")
                {
                    return m_streamHandler->Reset();
                }

                return PResult::ErrorBadArgument(PString("Unsupported property (%1)").Arg(property));
            }


            virtual PResult Set(const PString& property, double value)
            {
                if (!m_isOpened)
                    return PResult::Error("Cannot set properties on a closed stream");

                if (property.ToUpper() == "GO_TO_TIME")
                {
                    if (!m_uri.IsFile())
                        return PResult::Error("Seeking is available only for file streams");

                    return m_streamHandler->SeekToFrame(value * m_streamHandler->GetFPS());
                }

                return PResult::ErrorBadArgument(PString("Unsupported property (%1)").Arg(property));
            }

        private:
            PFFmpegInputStreamHandler*  m_streamHandler;
            bool                        m_isOpened;
            bool                        m_loop;
            PThread                     m_threadRetrieveImages;
            bool                        m_isThreadRunning;
            PConcurrentQueue<PFrame>    m_concurrentQueueFrames;
    };


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
        productName = PRODUCT_NAME;
        productVersion = PVersion(PRODUCT_VERSION);
        productGuid = PRODUCT_GUID;
        briefDescription = "Read a video stream from file (H264)";
    }


    void CreateInputVideoStreamImpl(const PUri& uri, PInputVideoStreamInterface** inputVideoStreamImpl, PResult& ret)
    {
        P_LOG_DEBUG<<PRODUCT_NAME << ": try to open video stream using " << PRODUCT_NAME << " v" << PRODUCT_VERSION << ", source is \"" << uri << "\"";

        if (!uri.IsValid())
        {
            ret = PResult::ErrorBadURIFormat(PString("unexpected URI scheme (should be 'file'): \"%1\"").Arg(uri.ToString()));
            return;
        }
        *inputVideoStreamImpl = new PInputVideoStreamFFmpeg(uri);
        ret = PResult::C_OK;
    }
}

// namespace papillon
