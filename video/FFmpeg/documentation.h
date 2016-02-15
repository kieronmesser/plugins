/**
 * @page plugin_inputVideoStreamFFmpeg PPluginInputVideoStreamFFmpeg
 *
 * \section plugin_inputVideoStreamFFmpeg_description Description
 * Read video streams from local video files.
 * - Product name: <b>FFmpegInputVideoStream</b>
 * - Version: <b>1.0</b>
 *
 * \section plugin_inputVideoStreamFFmpeg_create How to read H264 video files?
 * To create a PInputVideoStream to retrieve images from a video file:
 * \code{.cpp}
 * PInputVideoStream ivs;
 * PResult ret = PInputVideoStream::Open("file:myVideo.mp4", ivs); // or simply Open("myVideo.mp4", ivs)
 * if (ret.Failed()) ...
 * \endcode
 *
 * <b>Expected format of URI is:</b>
 * - "file:filename"
 *
 * \section plugin_inputVideoStreamFFmpeg_query Options on query string
 * - <b>loop=true</b>: to make the stream loop
 * - <b>startFrame=N</b>: where N is a frame number
 * \code{.cpp}
 * // Example
 * PInputVideoStream ivs;
 * PResult ret = PInputVideoStream::Open("file:myVideo.mp4?loop=true&startFrame=100&decode_with=ffmpeg", ivs);
 * if (ret.Failed()) ...
 * \endcode
 * Note that the query string 'decode_with=ffmpeg' is necessary to pick this particular plugin.
 *
 * \section plugin_inputVideoStreamFFmpeg_input_properties Get properties
 * - <b>Get("WIDTH", int32)</b>: get image width
 * - <b>Get("HEIGHT", int32)</b>: get image height
 * - <b>Get("FRAME_COUNT", int32)</b>: get number of frames in the video file
 * - <b>Get("FRAME_NUMBER", int32)</b>: get number of frames decoded so far
 * - <b>Get("FPS, double)</b>: get frame per seconds
 *
 * \section plugin_inputVideoStreamFFmpeg_output_properties Set properties
 * - <b>Set("GO_TO_FRAME", int32)</b>: jump to the specified frame number
 * - <b>Set("GO_TO_TIME", double)</b>: jump to the specified timestamp in seconds
 * - <b>Set("RESET", int32)</b>: reset codec status after a seek operation, given integer value is ignored
 */
