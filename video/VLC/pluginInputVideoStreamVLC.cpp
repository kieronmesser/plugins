/*
 * Copyright (C) 2014 Digital Barriers plc. All rights reserved.
 * Contact: http://www.digitalbarriers.com/
 *
 * This file is part of the Papillon SDK.
 *
 * You can't use, modify or distribute any part of this file without
 * the explicit written agreements of Digital Barriers plc.
 */

// ****************************************************************************
// Description:  this is the source code of a plugin for Papillon which is
//               able to create an input video stream (a PInputVideoStream object)
//               based on libvlc.
//               With this plugin, you can for example read RTSP video streams,
//               HTTP/HTTPS streams (e.g. youtube) or local video files.
//
// About LibVLC: - Documentation is available here: 
//                 https://www.videolan.org/developers/vlc/doc/doxygen/html/group__libvlc__media__player.html
//               - To get latest version of  libvlc, just download and install VLC,
//                 then go to /sdk folder to get header files and libs
// ****************************************************************************

#define PAPILLON_EXPORT_CORE_PLUGIN
#include <PapillonCore.h>
#include <PPluginInterface.h>
// libvlc
#include <vlc/vlc.h>
// STL
#include <cassert>
#include <list>
#ifdef PAPILLON_LINUX
#   include <string.h> // for memcpy
#endif

#define DODEBUG 0
#if DODEBUG
#define ONDEBUG(x) x
#else
#define ONDEBUG(x)
#endif

using namespace papillon;

const PString PRODUCT_NAME        = "VLCInputVideoStream";
const PString PRODUCT_DESCRIPTION = "read any video stream supported by VLC";
const PString PRODUCT_VERSION     = "1.0";
const PGuid   PRODUCT_GUID         ("{D2855F0D-0035-4DD0-BFB0-A7692FA6255E}");
const PString PRODUCT_LOG         = PString("%1 (%2) plugin: ").Arg(PRODUCT_NAME).Arg(PRODUCT_VERSION);


const int32   DEFAULT_WIDTH                 = 720;
const int32   DEFAULT_HEIGHT                = 576;
const int32   MAX_PENDING_IMAGES            = 1;
const int32   DEFAULT_NETWORK_CACHING_IN_MS = 1000;
PString       DEFAULT_PROTOCOL              = "no-rtsp-tcp"; // other options are "rtsp-tcp" "rtsp-http" or "rtsp-http-port=80"


libvlc_instance_t* g_libvlc_instance;


void PPlugin_OnLoad(PResult& ret)
{
    try
    {
        g_libvlc_instance = libvlc_new(0, NULL);

        if (g_libvlc_instance == NULL)
            ret = PResult::Error("unable to create libvlc");
        else
            ret = PResult::C_OK;
    }
    catch (...)
    {
        ret = PResult::Error("unable to create libvlc");
    }
}


void PPlugin_OnUnload(PResult& ret)
{
    libvlc_release(g_libvlc_instance);
    ret = PResult::C_OK;
}


void PPlugin_About(PResult& result, PString& productName, PVersion& productVersion, PGuid& productGuid, PString& briefDescription)
{
    productName      = PRODUCT_NAME;
    productVersion   = PVersion(PRODUCT_VERSION);
    productGuid      = PRODUCT_GUID;
    briefDescription = PRODUCT_DESCRIPTION;
    result           = PResult::C_OK;
}


struct SInputStream
{
public:
    SInputStream()
        : m_uri                                     ()
        , m_isOpened                                (false)
        , m_isFirstFrame                            (true)
        , m_isAutoResolution                        (true)
        , m_needsResolutionUpdate                   (true)
        , m_libvlc_media                            (NULL)
        , m_libvlc_media_player                     (NULL)
        , m_libvlc_event_manager                    (NULL)
        , m_libvlc_media_list                       (NULL)
        , m_mutexPixelBuffer                        ()
        , m_pixelBuffer                             ()
        , m_queue                                   (MAX_PENDING_IMAGES, 0)
        , m_imgWidth                                (DEFAULT_WIDTH)
        , m_imgHeight                               (DEFAULT_HEIGHT)
        , m_networkCachingInMs                      (DEFAULT_NETWORK_CACHING_IN_MS)
        , m_protocol                                (DEFAULT_PROTOCOL)
        , m_isRGBSwapped                            (false)
        , m_libvlc_event_mediaPlayerEndReached      (false)
        , m_libvlc_event_mediaPlayerEncounteredError(false)
        , m_libvlc_event_mediaPlayerPlaying         (false)
        , m_frameNumber                             (0)
    {
    }

    ~SInputStream()
    {
    }

    void* LockPixelBuffer()
    {
        m_mutexPixelBuffer.Lock();
        return m_pixelBuffer.AsPtr<void>();
    }

    void UnlockPixelBuffer()
    {
        m_mutexPixelBuffer.Unlock();
    }

    void SetResolution(int width, int height)
    {
        if ((width == 0) || (height == 0))
        {
            P_LOG_FATAL << "bad resolution (width and height must not be 0)";
            return;
        }

        P_LOG_INFO << PRODUCT_NAME << ": setting resolution to " << width << "x" << height;

        m_imgWidth  = width%16 == 0 ? width : width + 16 - width%16;
        m_imgHeight = height;

        const int32 sizeOfBuffer = m_imgWidth * m_imgHeight * 3;
        m_queue.ResizeBuffers(sizeOfBuffer);

        LockPixelBuffer();
        m_pixelBuffer.Resize(sizeOfBuffer);
        UnlockPixelBuffer();
    }

    PResult GetFirstFrame(PFrame& frame)
    {
        PImage image(m_imgWidth, m_imgHeight, PImage::E_BGR8U);
        bool isValidImage = false;

        for (int i=0; i<50; ++i)
        {
            if (!m_queue.TryDequeue(image.GetDataPtr(), 100))
            {
                P_LOG_DEBUG << PRODUCT_NAME << ": no image available";

                P_LOG_TRACE << PRODUCT_NAME << ": try to play sub-item...";
                if (m_libvlc_event_mediaPlayerEndReached && TryToPlaySubItem().Failed())
                    return PResult::Error("no image available");
            }
            else 
            {
                isValidImage = true;
                break;
            }
        }

        if (isValidImage)
        {
            m_isFirstFrame = false;
            return BuildFrameFromImage(frame, image);
        }

        return PResult::Error("no image available");
    }

    PResult BuildFrameFromImage(PFrame& frame, PImage image)
    {
        if (m_isRGBSwapped)
            image.SwapRGB(image);

        frame.SetNewImage(image, PGuid::CreateUniqueId(), PRODUCT_GUID);
        frame.SetSourceFrameNumber(m_frameNumber++);
        frame.SetTimestampToCurrentUTC();
        m_isFirstFrame = false;
        return PResult::C_OK;
    }

    PResult TryToPlaySubItem()
    {
        if (m_libvlc_event_mediaPlayerEndReached)
        {
            P_LOG_INFO << PRODUCT_NAME << ": check for sub-items...";

            // check if there is a subitem
            m_libvlc_media_list = libvlc_media_subitems(m_libvlc_media);
            if (m_libvlc_media_list == NULL)
            {
                P_LOG_INFO << PRODUCT_NAME << ": no sub-item found";
                return PResult::Error("reach end-of stream");
            }
            else
            {
                P_LOG_INFO << PRODUCT_NAME << ": found " << libvlc_media_list_count(m_libvlc_media_list) << " sub-items";
                m_libvlc_media = libvlc_media_list_item_at_index(m_libvlc_media_list, 0);
                P_LOG_INFO << PRODUCT_NAME << ": stop playing...";
                libvlc_media_player_set_media(m_libvlc_media_player, m_libvlc_media);
                P_LOG_INFO << PRODUCT_NAME << ": start playing...";
                if (libvlc_media_player_play(m_libvlc_media_player) != 0)
                {
                    P_LOG_ERROR << PRODUCT_NAME << ": failed to play sub-item";
                    return PResult::Error("failed to play sub-item");
                }
                m_libvlc_event_mediaPlayerEndReached = false;
                return PResult::C_OK;
            }
        }
        else
        {
            return PResult::Error("end-of stream not reached");
        }
    }

    PUri                      m_uri                                     ;
    bool                      m_isOpened                                ;
    bool                      m_isFirstFrame                            ;//!< FIXME(AK) we have frame number which we can check if ==0
    bool                      m_isAutoResolution                        ;
    bool                      m_needsResolutionUpdate                   ;
    libvlc_media_t*           m_libvlc_media                            ;
    libvlc_media_player_t*    m_libvlc_media_player                     ;
    libvlc_event_manager_t*   m_libvlc_event_manager                    ;
    libvlc_media_list_t*      m_libvlc_media_list                       ;
    PMutex                    m_mutexPixelBuffer                        ;
    PByteArray                m_pixelBuffer                             ;
    PConcurrentRawBufferQueue m_queue                                   ;
    int32                     m_imgWidth                                ;
    int32                     m_imgHeight                               ;
    int32                     m_networkCachingInMs                      ;
    PString                   m_protocol                                ;
    bool                      m_isRGBSwapped                            ;
    bool                      m_libvlc_event_mediaPlayerEndReached      ;
    bool                      m_libvlc_event_mediaPlayerEncounteredError;
    bool                      m_libvlc_event_mediaPlayerPlaying         ;
    int32                     m_frameNumber                             ;
};


void PPlugin_CreateInstance(PResult& result, void** instance, const PProperties& /*parameters*/)
{
    if (PLicensing::GetInstance().CheckOutLicense(PRODUCT_NAME, PRODUCT_VERSION).Failed())
    {
        result = PResult::ErrorFailedToCheckOutLicense(PRODUCT_NAME, PRODUCT_VERSION);
        return;
    }

    *instance = new SInputStream();

    result = PResult::C_OK;
}


void PPlugin_DestroyInstance(papillon::PResult& result, void** instance)
{
    if (*instance != NULL)
    {
        PPlugin_VideoStream_Close(result, *instance);

        SInputStream* is = static_cast<SInputStream*>(*instance);
        delete is; *instance = NULL;

        result = PLicensing::GetInstance().CheckInLicense(PRODUCT_NAME).PrependErrorMessage(PRODUCT_LOG);
    }
    else
        result = PResult::C_OK;
}


void PPlugin_VideoStream_AcceptURI(PResult& result, const PUri& uri)
{
    if (uri.IsFile())
        result = PResult::C_OK;
    else {
#ifdef _MSC_VER        
        const std::string str("dshow");
#else
        const std::string str("v4l");
#endif   
        std::string strUri(uri.ToString().c_str());
        if(strUri.find(str) == 0)
            result = PResult::C_OK;
        else
        result = uri.CheckScheme(PStringList("rtsp", "http", "https", "rtmp"));
        if(result.Failed())
            P_LOG_DEBUG << "result:" << result;
    }
}


static void CallbackLoggingVLC(void* /*data*/, int level, const libvlc_log_t *ctx, const char *fmt, va_list args)
{
    // note: this function is thread-safe because the Papillon logging system is thread-safe
    const int MAX_BUFFER_SIZE = 8192;
    char buffer[MAX_BUFFER_SIZE];
    vsnprintf(buffer, MAX_BUFFER_SIZE, fmt, args); // clamp buffer larger than MAX_BUFFER_SIZE
    P_LOG_TRACE << PRODUCT_NAME << ": libVLC log: " << PString(buffer);
}


static void* CallbackLockVideoMemory(void* data, void** p_pixels)
{
    P_LOG_TRACE << PRODUCT_NAME << ": CallbackLockVideoMemory()";
    ONDEBUG(std::cerr << ": CallbackLockVideoMemory()\n");
    
    SInputStream* is = reinterpret_cast<SInputStream*>(data);
    if (is != NULL)
    {
        if (is->m_needsResolutionUpdate)
        {
            unsigned int w = 0, h = 0;
            libvlc_video_get_size(is->m_libvlc_media_player, 0, &w, &h);
            P_LOG_INFO << PRODUCT_NAME << ": auto resolution set from stream; video size is " << w << "x" << h;

            if (w*h != 0 && w*h != is->m_imgWidth*is->m_imgHeight)
            {
                is->m_needsResolutionUpdate = false;
                is->m_imgWidth = w;
                is->m_imgHeight = h;
                is->SetResolution(is->m_imgWidth, is->m_imgHeight);
            }
        }

        *p_pixels = is->LockPixelBuffer();
    }

    return NULL;
}


static void CallbackUnlockVideoMemory(void* data, void* id, void* const* p_pixels)
{
    P_LOG_TRACE << PRODUCT_NAME << ": CallbackUnlockVideoMemory()";
    ONDEBUG(std::cerr << "CallbackUnlockVideoMemory:"  << "\n");

    SInputStream* is = reinterpret_cast<SInputStream*>(data);
    if (is != NULL)
    {
        is->m_queue.Enqueue(is->m_pixelBuffer.AsConstPtr<void>());
        is->UnlockPixelBuffer();
    }
}


static void CallbackMediaPlayer(const libvlc_event_t* event, void* data)
{
    ONDEBUG(std::cerr << "CallbackMediaPlayer type:" << int(event->type) << "\n");

    SInputStream* is = reinterpret_cast<SInputStream*>(data);
    if (is == NULL)
    {
        P_LOG_ERROR << PRODUCT_NAME << ": unexpected NULL SInputStream in CallbackMediaPlayer";
        return;
    }

    switch (event->type)
    {
    case libvlc_MediaPlayerMediaChanged      : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerMediaChanged"; break;
    case libvlc_MediaPlayerNothingSpecial    : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerNothingSpecial"; break;
    case libvlc_MediaPlayerOpening           : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerOpening"; break;
    case libvlc_MediaPlayerBuffering         : {
        ONDEBUG(P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerBuffering");
        is->m_libvlc_event_mediaPlayerPlaying = true;
        break;
                                               }
    case libvlc_MediaPlayerPlaying           : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerPlaying"; break;
    case libvlc_MediaPlayerPaused            : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerPaused"; break;
    case libvlc_MediaPlayerStopped           : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerStopped"; break;
    case libvlc_MediaPlayerForward           : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerForward"; break;
    case libvlc_MediaPlayerBackward          : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerBackward"; break;
    case libvlc_MediaPlayerEndReached        : 
        P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerEndReached";
        is->m_libvlc_event_mediaPlayerEndReached = true;
        break;
    case libvlc_MediaPlayerEncounteredError  : 
        P_LOG_ERROR << PRODUCT_NAME << ": callback media player: MediaPlayerEncounteredError";
        is->m_libvlc_event_mediaPlayerEncounteredError = true;
        break;
    case libvlc_MediaPlayerTimeChanged       : {
        ONDEBUG(libvlc_time_t time = libvlc_media_player_get_time(is->m_libvlc_media_player);
        P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerTimeChanged:" << time);
        break;}
    case libvlc_MediaPlayerPositionChanged   : {
        ONDEBUG(float position = libvlc_media_player_get_rate(is->m_libvlc_media_player);
        P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerPositionChanged:" << position);
        break; }
    case libvlc_MediaPlayerSeekableChanged   : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerSeekableChanged"; break;
    case libvlc_MediaPlayerPausableChanged   : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerPausableChanged"; break;
    case libvlc_MediaPlayerTitleChanged      : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerTitleChanged"; break;
    case libvlc_MediaPlayerSnapshotTaken     : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerSnapshotTaken"; break;
    case libvlc_MediaPlayerLengthChanged     : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerLengthChanged"; break;
    case libvlc_MediaPlayerVout:
        {
            if (!is->m_isOpened)
            {
                if (is->m_isAutoResolution)
                {
                    // and we can set the size
                    unsigned int width = 0, height = 0, i = 0;
                    while (width * height == 0 && ++i < 100)
                    {
                        libvlc_video_get_size(is->m_libvlc_media_player, 0, &width, &height);
                        PConcurrentThread::Sleep(10);
                    }
                    char* ar = libvlc_video_get_aspect_ratio(is->m_libvlc_media_player);
                    if (ar != NULL)
                        P_LOG_INFO << PRODUCT_NAME << ": aspect ratio " << PString(ar);
                    else
                        P_LOG_INFO << PRODUCT_NAME << ": aspect ratio not specified";
                    P_LOG_INFO << PRODUCT_NAME << ": auto resolution enabled (video size seems to be " << width << "x" << height << " after " << i << " tries)";
                    if ((width == 0) || (height == 0))
                    {
                        is->m_needsResolutionUpdate = true;
                        P_LOG_INFO << PRODUCT_NAME << ": failed to retrieve resolution... use default resolution: " << is->m_imgWidth << "x" << is->m_imgHeight;
                    }
                    else
                    {
                        is->m_needsResolutionUpdate = false;
                        is->m_imgWidth = width;
                        is->m_imgHeight = height;
                    }
                }

                is->SetResolution(is->m_imgWidth, is->m_imgHeight);
            }
            P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: MediaPlayerVout";
            is->m_isOpened = true;
            break;
        }
    default                                  : P_LOG_DEBUG << PRODUCT_NAME << ": callback media player: unknown"; break;
    }
}


// This callback is used to receive the size of buffers used by VLC to receive images. i.e. it is adapted to the REAL size of images coming in
static unsigned int CallbackFormat(void** data, char* chroma, unsigned int* width, unsigned int* height, unsigned int* pitches, unsigned int* lines)
{
    P_LOG_TRACE << PRODUCT_NAME << ": CallbackFormat()";

    SInputStream* is = reinterpret_cast<SInputStream*>(*data);
    strcpy(chroma, "RV24");
    pitches[0] = pitches[1] = pitches[2] = *width * 3;
    lines[0] = lines[1] = lines[2] = *height;

    // Memory protection : default img size is incoming buffer size when auto resolution is set
    if (is->m_isAutoResolution)
    {
        is->m_imgWidth = *width;
        is->m_imgHeight = *height;
    }

    const int32 sizeOfBuffer = (*width) * (*height) * 3;
    is->m_queue.ResizeBuffers(sizeOfBuffer);

    is->LockPixelBuffer();
    is->m_pixelBuffer.Resize(sizeOfBuffer);
    is->UnlockPixelBuffer();

    return 1;
}


void PPlugin_VideoStream_Open(PResult& result, void* instance, const PUri& uri)
{
    P_LOG_INFO << PRODUCT_NAME << " " << PVersion(PRODUCT_VERSION) << ": try to open " << uri.ToString().Quote();

    if (instance == NULL)
    {
        result = PResult::ErrorNullPointer("unexpected NULL instance");
        return;
    }

    SInputStream* is = static_cast<SInputStream*>(instance);

    if (is->m_isOpened)
    {
        result = PResult::Error("video stream already opened");
        return;
    }

    // here, m_isOpened is false...
    is->m_uri = uri;

    try
    {
        if (!is->m_uri.GetQueryValue("width", is->m_imgWidth) || !is->m_uri.GetQueryValue("height", is->m_imgHeight))
        {
            is->m_imgWidth              = DEFAULT_WIDTH;
            is->m_imgHeight             = DEFAULT_HEIGHT;
            is->m_isAutoResolution      = true;
            is->m_needsResolutionUpdate = true;
        }
        else
        {
            is->m_isAutoResolution      = false;
            is->m_needsResolutionUpdate = false;
        }

        if (!is->m_uri.GetQueryValue("protocol", is->m_protocol))
            is->m_protocol = DEFAULT_PROTOCOL;

        is->m_isRGBSwapped = is->m_uri.HasQueryItem("rgbSwapped");

        P_LOG_INFO << PRODUCT_NAME << ": Open: parameter \"uri\"         = " << is->m_uri.ToString();
        P_LOG_INFO << PRODUCT_NAME << ": Open: parameter \"resolution\"  = " << is->m_imgWidth << "x" << is->m_imgHeight;
        P_LOG_INFO << PRODUCT_NAME << ": Open: parameter \"protocol\"    = " << is->m_protocol;
        P_LOG_INFO << PRODUCT_NAME << ": Open: parameter \"swapRedBlue\" = " << is->m_isRGBSwapped;

        libvlc_log_set(g_libvlc_instance, CallbackLoggingVLC, is);

        if (is->m_uri.IsFile())
        {
            PString filename = is->m_uri.GetPath();
            if (PFile::CheckExistsAndIsReadable(filename).Failed())
            {
                result = PResult::ErrorFileNotFound(PString("video file not found: \"%1\"").Arg(filename));
                return;
            }

            is->m_libvlc_media = libvlc_media_new_path(g_libvlc_instance, filename.c_str());
        }
        else
        {
            is->m_libvlc_media = libvlc_media_new_location(g_libvlc_instance, is->m_uri.ToString().c_str());
        }

        if (is->m_libvlc_media == NULL)
        {
            result = PResult::ErrorNullPointer("m_libvlc_media");
            return;
        }

        libvlc_media_add_option(is->m_libvlc_media, is->m_protocol.c_str());

        int32 networkCaching;
        if (is->m_uri.GetQueryValue("network-caching", networkCaching))
            is->m_networkCachingInMs = networkCaching;
        P_LOG_INFO << PRODUCT_NAME << ": Open: network caching set to " << is->m_networkCachingInMs << " ms";
        libvlc_media_add_option(is->m_libvlc_media, PString("network-caching=%1").Arg(is->m_networkCachingInMs).c_str());

        is->m_libvlc_media_player = libvlc_media_player_new_from_media(is->m_libvlc_media);


        P_LOG_DEBUG << PRODUCT_NAME << ": Open: register callback to retrieve images";
        libvlc_video_set_callbacks(is->m_libvlc_media_player, CallbackLockVideoMemory, CallbackUnlockVideoMemory, NULL, is);

        // Call back format is necessary to let VLC determine size of video received rather than forcing size
        P_LOG_DEBUG << PRODUCT_NAME << ": Open: register callback to get image format";
        libvlc_video_set_format_callbacks(is->m_libvlc_media_player, CallbackFormat, NULL);

        P_LOG_DEBUG << PRODUCT_NAME << ": Open: set event manager";
        is->m_libvlc_event_manager = libvlc_media_player_event_manager(is->m_libvlc_media_player);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerMediaChanged      , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerNothingSpecial    , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerOpening           , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerBuffering         , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerPlaying           , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerPaused            , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerStopped           , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerForward           , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerBackward          , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerEndReached        , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerEncounteredError  , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerTimeChanged       , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerPositionChanged   , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerSeekableChanged   , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerPausableChanged   , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerTitleChanged      , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerSnapshotTaken     , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerLengthChanged     , CallbackMediaPlayer, is);
        libvlc_event_attach(is->m_libvlc_event_manager, libvlc_MediaPlayerVout              , CallbackMediaPlayer, is);

        if (PLicensing::GetInstance().CheckOutLicense(PRODUCT_NAME, PRODUCT_VERSION).Failed())
        {
            result = PResult::ErrorFailedToCheckOutLicense(PRODUCT_NAME, PRODUCT_VERSION);
            return;
        }

        P_LOG_INFO << PRODUCT_NAME << ": Open: start playing...";
        if (libvlc_media_player_play(is->m_libvlc_media_player) != 0)
        {
            result = PResult::Error("failed to open video source");
            return;
        }

        // try to play the video stream; waits at most 10 seconds
        PTimer timer;
        while (!is->m_libvlc_event_mediaPlayerPlaying && !is->m_libvlc_event_mediaPlayerEncounteredError && (timer.ElapsedSec() < 10.0))
            PConcurrentThread::Sleep(1);

        if (!is->m_libvlc_event_mediaPlayerPlaying)
        {
            PPlugin_VideoStream_Close(result, instance);
            P_LOG_ERROR << PRODUCT_NAME << ": Open: unable to play the stream";
            result = PResult::Error("unable to play the stream");
            return;
        }

        // wait until we get some video - m_isOpened is received on first video frame so no need to wait for is parsed
        // https://forum.videolan.org/viewtopic.php?t=95728
        while (/*!libvlc_media_is_parsed(m_libvlc_media) || */!is->m_isOpened && !is->m_libvlc_event_mediaPlayerEncounteredError)
            PConcurrentThread::Sleep(10);

        if (is->m_libvlc_event_mediaPlayerEncounteredError)
        {
            PPlugin_VideoStream_Close(result, instance);
            P_LOG_ERROR << PRODUCT_NAME << ": Open: unable to play the stream - unexpected error";
            result = PResult::Error("unable to play the stream");
            return;
        }

        P_LOG_INFO << PRODUCT_NAME << ": Open: success, " << uri.ToString().Quote() << " opened, ready to get frames...";

        is->m_isOpened = true;
        result = PResult::C_OK;
    }
    catch (std::exception&)
    {
        result = PResult::Error("failed to open video stream...");
        return;
    }
    catch (...)
    {
        result = PResult::Error("failed to open video stream: unknown exception");
        return;
    }
}


void PPlugin_VideoStream_Close(PResult& result, void* instance)
{
    P_LOG_TRACE << PRODUCT_NAME << ": Close()";

    if (instance == NULL)
    {
        result = PResult::ErrorNullPointer("unexpected NULL instance");
        return;
    }

    SInputStream* is = static_cast<SInputStream*>(instance);

    if (!is->m_isOpened)
    {
        result = PResult::Error("video stream not opened");
        return;
    }
    // here, m_isOpened is true...

    try
    {
        // is->m_libvlc_media is not NULL

        P_LOG_INFO << PRODUCT_NAME << ": Close: unregister callback to retrieve images";
        libvlc_video_set_callbacks(is->m_libvlc_media_player, NULL, NULL, NULL, is);

        P_LOG_INFO << PRODUCT_NAME << ": Close: detach event manager";
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerMediaChanged      , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerNothingSpecial    , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerOpening           , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerBuffering         , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerPlaying           , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerPaused            , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerStopped           , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerForward           , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerBackward          , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerEndReached        , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerEncounteredError  , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerTimeChanged       , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerPositionChanged   , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerSeekableChanged   , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerPausableChanged   , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerTitleChanged      , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerSnapshotTaken     , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerLengthChanged     , CallbackMediaPlayer, is);
        libvlc_event_detach(is->m_libvlc_event_manager, libvlc_MediaPlayerVout              , CallbackMediaPlayer, is);

        P_LOG_INFO << PRODUCT_NAME << ": Close: stop playing...";
        libvlc_media_player_stop(is->m_libvlc_media_player);

        if (is->m_libvlc_media_list != NULL)
        {
            libvlc_media_list_release(is->m_libvlc_media_list);
            is->m_libvlc_media_list = NULL;
        }

        libvlc_media_player_release(is->m_libvlc_media_player);
        is->m_libvlc_media_player = NULL;

        libvlc_media_release(is->m_libvlc_media);
        is->m_libvlc_media = NULL;

        libvlc_log_unset(g_libvlc_instance);
        P_LOG_INFO << PRODUCT_NAME << ": Close: Ok";
    }
    catch (...)
    {
        result = PResult::Error("failed to release the video stream").LogIfError();
        return;
    }    

    // everything seems to be ok: the video stream has been successfully closed
    is->m_isOpened = false;
    result = PResult::C_OK;
}


void PPlugin_VideoStream_IsOpened(PResult& result, void* instance, bool& isOpened)
{
    if (instance == NULL)
    {
        result = PResult::ErrorNullPointer("unexpected NULL instance");
        return;
    }

    SInputStream* is = static_cast<SInputStream*>(instance);
    isOpened = is->m_isOpened;
    result = PResult::C_OK;
}


void PPlugin_VideoStream_CanSeek(PResult& result, void* instance, bool& canSeek)
{
    if (instance == NULL)
    {
        result = PResult::ErrorNullPointer("unexpected NULL instance");
        return;
    }

    SInputStream* is = static_cast<SInputStream*>(instance);
    canSeek = is->m_uri.IsFile();
    result = PResult::C_OK;
}


void PPlugin_VideoStream_GetFrame(PResult& result, void* instance, PFrame& frame, int32 timeOutMs)
{
    P_LOG_TRACE << PRODUCT_NAME << ": GetFrame()";

    if (instance == NULL)
    {
        result = PResult::ErrorNullPointer("unexpected NULL instance");
        return;
    }

    ONDEBUG(std::cerr << "GetFrame begin\n");
    SInputStream* is = static_cast<SInputStream*>(instance);

    if (!is->m_isOpened)
    {
        result = PResult::ErrorInvalidState("video stream not opened");
        ONDEBUG(std::cerr << "GetFrame not open\n");
        return;
    }
    // here, m_isOpened is true...

    try
    {
        PImage image(is->m_imgWidth, is->m_imgHeight, PImage::E_BGR8U);

        if (is->m_isFirstFrame)
        {
            result = is->GetFirstFrame(frame);
            ONDEBUG(std::cerr << "GetFrame first frame\n");
            return;
        }

        if (!is->m_queue.TryDequeue(image.GetDataPtr(), timeOutMs))
        {
            P_LOG_DEBUG << PRODUCT_NAME << ": no image available";
            {
                result = PResult::Error("no image available");
                ONDEBUG(std::cerr << "GetFrame no image available\n");
                return;
            }            
        }

        result = is->BuildFrameFromImage(frame, image);
    }
    catch (...)
    {
        result = PResult::C_ERROR_UNKNOWN;
    }
    ONDEBUG(std::cerr << "GetFrame end\n");
}


void PPlugin_Get(PResult& result, void* /*instance*/, const PString& /*property*/, PObject& /*object*/)
{
    result = PResult::C_ERROR_NOT_SUPPORTED;
}


void PPlugin_Set(PResult& result, void* /*instance*/, const PString& /*property*/, const PObject& /*object*/)
{
    result = PResult::C_ERROR_NOT_SUPPORTED;
}


