/**
 * @page plugin_inputVideoStreamVLC PPluginInputVideoStreamVLC
 *
 * \section plugin_inputVideoStreamVLC_description Description
 * Read video streams using VLC media player (support RTSP, RTMP, HTTP, HTTPS or local video file)
 * - Product name: <b>VLCInputVideoStream</b>
 * - Version: <b>1.0</b>
 *
 * \section Getting the Plugin
 *
 * This plugin is not provided in our installation.  However the source code can be downloaded from our git-hub page.
 *
 * \section plugin_inputVideoStreamVLC_create How to read a stream using the VLC plugin?
 * To create a PInputVideoStream to retrieve images from a VLC input stream:
 * \code{.cpp}
 * PInputVideoStream ivs;
 * PResult ret = PInputVideoStream::Open("rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mov?width=800&height=600", ivs);
 * // other example:
 * // PResult ret = PInputVideoStream::Open("https://www.youtube.com/watch?v=tXjVOFgNd5M?width=800&height=600", ivs);
 * if (ret.Failed()) ...
 * \endcode
 *
 * <b>Expected scheme of URI is:</b>
 * - rtsp://...
 * - rtmp://...
 * - http://...
 * - https://...
 * - file:...
 *
 * \section plugin_inputVideoStreamVLC_query Options on query string
 * - <b>width=W</b>: width of the stream to retrieve
 * - <b>height=H</b>: height of the stream to retrieve
 * - <b>protocol=P</b>: protocol to be used; for example, P can be "rtsp-tcp", "rtsp-http" or "rtsp-http-port=80"
 * - <b>rgbSwapped</b>: swap red and blue channels of the video stream
 *
 * \section plugin_inputVideoStreamVLC_input_properties Get properties
 * None
 *
 * \section plugin_inputVideoStreamVLC_output_properties Set properties
 * None
 */
