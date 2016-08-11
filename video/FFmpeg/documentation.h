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
 *
 * @page plugin_outputVideoStreamFFmpeg PPluginOutputVideoStreamFFmpeg
 *
 * \section plugin_outputVideoStreamFFmpeg_description Description
 * Write a video stream to a file or to an RTSP stream
 * - Product name: <b>FFmpegOutputVideoStream</b>
 * - Version: <b>1.0</b>
 *
 * \section plugin_outputVideoStreamFFmpeg_create How to write a video file?
 * To create a POutputVideoStream to write images:
 * \code{.cpp}
 * POutputVideoStream ovs;
 * PResult ret = POutputVideoStream::Open("file:myVideo.out?fourcc=H264&width=800&height=600&fps=25", ovs);
 * if (ret.Failed()) ...
 * ...
 * ovs.PutImage(image);
 * \endcode
 *
 * \section plugin_outputVideoStreamFFmpeg_create How to generate an RTSP stream?
 * To create a POutputVideoStream to write images:
 * \code{.cpp}
 * POutputVideoStream ovs;
 * PResult ret = POutputVideoStream::Open("rtsp://127.0.0.1:1935/ffmpeg/0?fourcc=H264&width=800height=600&fps=25&encode_with=ffmpeg", ovs);
 * if (ret.Failed()) ...
 * ...
 * ovs.PutImage(image);
 * \endcode
 * Note that you need to specify the encode_with=ffmpeg parameter to pick this particular plugin as currently it's the only one that supports RTSP streaming.
 *
 * <b>Expected format of URI is either</b>
 * - "file:path?parameters"
 * <b>or</b>
 * - "rtsp://<host>:<port>/<path>"
 *
 * \section plugin_outputVideoStreamFFmpeg_query Options on query string
 * - <b>width=W</b>: width of the output stream
 * - <b>height=H</b>: height of the output stream
 * - <b>fps=N</b>: frame rate of the output stream (frames per second)
 * - <b>fourcc=XXXX</b>: codec used to encode video stream ('H264' is the default value if not given)
 * - <b>encode_with=XXXX</b>: 3rd-party tool to use for encoding, possible values: tvi, opencv, ffmpeg, mainconcept, network, and screen
 * - <b>quality=(high|medium|low)</b>: Encoding quality
 * - <b>gop_length=[0, INT_MAX]</b>: Group of pictures length, i.e. frequency of I-type frames in a stream, 0 for I-type frames only
 *
 * \section plugin_outputVideoStreamFFmpeg_input_properties Get properties
 * None
 *
 * \section plugin_outputVideoStreamFFmpeg_output_properties Set properties
 * None
 */
