/*
	NDI sender

	using the NDI SDK to send the frames via network

	http://NDI.NewTek.com

	Copyright (C) 2016-2018 Lynn Jarvis.

	http://www.spout.zeal.co

	=========================================================================
	This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
	=========================================================================

	13-06-16	- removed internal buffer
				- used in-place buffer flip function so that internal buffer is not needed
	27.07.16	- restored FlipBuffer with additional temporary buffer
				- used optimised assembler memcpy in FlipBuffer - 4-5 fps increase at 2560x1440
				  FlipBuffer should be avoided if a GPU texture copy/invert is possible
	10.10.16	- updated SSE2 memcpy with intrinsics for 64bit compatibility
	12.10.16	- Included a bgra conversion option for SendImage
	05.11.16	- Added SetClockVideo
	07.11.16	- Added CPU support check
	12.11.16	- Fix MessageBox \N to \nN
	13.11.16	- Do not clock the video for async sending
	15.11.16	- add audio support
	09.02.17	- include changes by Harvey Buchan for NDI SDK version 2
				  (RGBA sender option)
				- Added Metadata
	17.02.17	- Added MetaData functions
				- Added GetNDIversion - NDIlib_version
	22.02.17	- corrected DWORD cast to int in NDI_connection_type
	// Changes for NDI Version 3
	04.11.17	- const char for SendImage
				- change functions to _v2
				- change variable types
	31.03.18	- Update to NDI SDK Version 3 - search on "Vers 3"
				- change functions to _v2
				- change variable types

	Changes with update to 3.5
	11.06.18	- SendImage - release p_frame for invert on size change
				- remove messagebox and replace with cout
				- Incremented version number in Sender meta-data registration to "1.001.000"
	08.07.18	- Change class name to ofxDNIsend
	11.07.18	- Add SetFrameRate - single integer or double
	14.07.18	- Add Sender dimensions m_Width, m_Height and bSenderInitialized
				- Add GetWidth and GetHeight
				- Add SenderCreated


*/
#include "ofxNDIsend.h"


ofxNDIsend::ofxNDIsend()
{
	pNDI_send = NULL;
	p_frame = NULL;
	m_frame_rate_N = 60000; // 60 fps default : 30000 - 29.97 fps
	m_frame_rate_D = 1000; // 1001 - 29.97 fps
	m_horizontal_aspect = 1; // source aspect ratio by default
	m_vertical_aspect = 1;
	m_picture_aspect_ratio = 16.0f/9.0f; // Re-calculated from source aspect ratio
	m_bProgressive = true; // progressive default
	m_bClockVideo = true; // clock video default
	m_bAsync = false;
	m_bNDIinitialized = false;
	m_Width = m_Height = 0;
	bSenderInitialized = false;
	m_ColorFormat = NDIlib_FourCC_type_RGBA; // default rgba output format

	// Audio
	m_bAudio = false; // No audio default
	m_AudioSampleRate = 48000; // 48kHz
	m_AudioChannels = 1; // Default mono
	m_AudioSamples = 1602; // There can be up to 1602 samples, can be changed on the fly
	m_AudioTimecode = NDIlib_send_timecode_synthesize; // Timecode (synthesized for us !)
	m_AudioData = NULL; // Audio buffer

	if(!NDIlib_is_supported_CPU() ) {
		std::cout << "CPU does not support NDI NDILib requires SSE4.1 NDIsender" << std::endl;
		m_bNDIinitialized = false;
	}
	else {
		m_bNDIinitialized = NDIlib_initialize();
		if(!m_bNDIinitialized) {
			std::cout << "Cannot run NDI - NDILib initialization failed" << std::endl;
		}
	}
}


ofxNDIsend::~ofxNDIsend()
{
	// Release a sender if created
	if (bSenderInitialized)
		ReleaseSender();
	bSenderInitialized = false;

	// Release the library
	NDIlib_destroy();
	m_bNDIinitialized = false;

}

// Create an RGBA sender
bool ofxNDIsend::CreateSender(const char *sendername, unsigned int width, unsigned int height)
{
	return CreateSender(sendername, width, height, NDIlib_FourCC_type_RGBA);
}

// Create a sender of specified colour format
// Formats supported are RGBA, BGRA and UVYV
bool ofxNDIsend::CreateSender(const char *sendername, unsigned int width, unsigned int height, NDIlib_FourCC_type_e colorFormat)
{
	// printf("ofxNDIsender::CreateSender(%s, %d, %d, (%d)\n", sendername, width, height, colorFormat);

	// Create an NDI source that is clocked to the video.
	// unless async sending has been selected.
	NDI_send_create_desc.p_ndi_name = sendername;
	NDI_send_create_desc.p_groups = NULL;
	// Do not clock the video for async sending
	if (m_bAsync)
		NDI_send_create_desc.clock_video = false;
	else
		NDI_send_create_desc.clock_video = m_bClockVideo;
	NDI_send_create_desc.clock_audio = false;

	// Calulate aspect ratio
	// Source (1:1)
	// Normal 4:3
	// Widescreen 16:9

	// 1:1 means use the source aspect ratio
	if(m_horizontal_aspect == 1 && m_vertical_aspect == 1) 
		m_picture_aspect_ratio = (float)width/(float)height;
	else
		m_picture_aspect_ratio = (float)m_horizontal_aspect/(float)m_vertical_aspect;

	// We create the NDI sender
	pNDI_send = NDIlib_send_create(&NDI_send_create_desc);

	if (pNDI_send) {

		// Provide a meta-data registration that allows people to know what we are. Note that this is optional.
		// Note that it is possible for senders to also register their preferred video formats.
		char* p_connection_string = "<ndi_product long_name=\"ofxNDI sender\" "
												 "             short_name=\"ofxNDI Sender\" "
												 "             manufacturer=\"spout@zeal.co\" "
												 "             version=\"1.001.000\" "
												 "             session=\"default\" "
												 "             model_name=\"none\" "
												 "             serial=\"none\"/>";
		
		const NDIlib_metadata_frame_t NDI_connection_type = {
			// The length
			(int)::strlen(p_connection_string),
			// Timecode (synthesized for us !)
			NDIlib_send_timecode_synthesize,
			// The string
			p_connection_string
		};

		NDIlib_send_add_connection_metadata(pNDI_send, &NDI_connection_type);
		
		// We are going to create an non-interlaced frame at 60fps
		if(p_frame) free((void *)p_frame);
		p_frame = NULL; // invert  buffer

		video_frame.xres = (int)width;
		video_frame.yres = (int)height;
		video_frame.FourCC = colorFormat;
		video_frame.frame_rate_N = m_frame_rate_N; // clock the frame (default 60fps)
		video_frame.frame_rate_D = m_frame_rate_D;
		video_frame.picture_aspect_ratio = m_picture_aspect_ratio; // default source (width/height)

		// 24-1-17 SDK Change to NDI v2
		//video_frame.is_progressive = m_bProgressive; // progressive of interlaced (default progressive)
		if (m_bProgressive) video_frame.frame_format_type = NDIlib_frame_format_type_progressive;
		else video_frame.frame_format_type = NDIlib_frame_format_type_interleaved;
		// The timecode of this frame in 100ns intervals
		video_frame.timecode = NDIlib_send_timecode_synthesize; // 0LL; // Let the API fill in the timecodes for us.
		video_frame.p_data = NULL;
		video_frame.line_stride_in_bytes = (int)width*4; // The stride of a line BGRA

		// Keep the sender dimensions locally
		m_Width = width;
		m_Height = height;
		bSenderInitialized = true;
		m_ColorFormat = colorFormat;

		if(m_bAudio) {
			// Create an audio buffer
			m_audio_frame.sample_rate = m_AudioSampleRate;
			m_audio_frame.no_channels = m_AudioChannels;
			m_audio_frame.no_samples  = m_AudioSamples;
			m_audio_frame.timecode    = m_AudioTimecode;
			m_audio_frame.p_data      = m_AudioData;
			// mono/stereo inter channel stride
			m_audio_frame.channel_stride_in_bytes = (m_AudioChannels-1)*m_AudioSamples*sizeof(FLOAT);
		}

		return true;
	}

	return false;
}

// Update sender dimensions with the existing colour format
bool ofxNDIsend::UpdateSender(unsigned int width, unsigned int height)
{
	return UpdateSender(width, height, m_ColorFormat);
}

// Update sender dimensions and colour format
bool ofxNDIsend::UpdateSender(unsigned int width, unsigned int height, NDIlib_FourCC_type_e colorFormat)
{
	if(pNDI_send && m_bAsync) {
		// Because one buffer is in flight we need to make sure that 
		// there is no chance that we might free it before NDI is done with it. 
		// You can ensure this either by sending another frame, or just by
		// sending a frame with a NULL pointer.
		// NDIlib_send_send_video_async(pNDI_send, NULL);
		NDIlib_send_send_video_async_v2(pNDI_send, NULL);
	}

	// Free the local buffer, it is re-created in SendImage if invert is needed
	if(p_frame) free((void *)p_frame);
	p_frame = NULL;
	video_frame.p_data = NULL;

	// Reset video frame size
	video_frame.xres = (int)width;
	video_frame.yres = (int)height;
	video_frame.line_stride_in_bytes = (int)width * 4;
	video_frame.FourCC = colorFormat;

	// Update the sender dimensions
	m_Width = width;
	m_Height = height;

	return true;
}


// Send image pixels
bool ofxNDIsend::SendImage(const unsigned char * pixels, 
	unsigned int width, unsigned int height,
	bool bSwapRB, bool bInvert)
{
	// printf("SendImage (%x, %d, %d, (%d, %d) )\n", pixels, width, height, bSwapRB, bInvert);

	if (pixels && width > 0 && height > 0) {

		// Allow for forgotten UpdateSender
		if (video_frame.xres != (int)width || video_frame.yres != (int)height) {
			video_frame.xres = (int)width;
			video_frame.yres = (int)height;
			video_frame.line_stride_in_bytes = width * 4;
			// Release pframe for invert because the size is different
			// It will be re-created at the correct size
			if (p_frame) free((void *)p_frame);
			p_frame = NULL;
		}

		if (bSwapRB || bInvert) {
			// printf("bSwapRB = %d, bInvert = %d\n", bSwapRB, bInvert);
			// Local memory buffer is only needed for rgba to bgra or invert
			if (!p_frame) {
				p_frame = (uint8_t*)malloc(width*height * 4 * sizeof(unsigned char));
				if (!p_frame) {
					std::cout << "Out of memory in SendImage" << std::endl;
					return false;
				}
				video_frame.p_data = p_frame;
			}
			ofxNDIutils::CopyImage((const unsigned char *)pixels, (unsigned char *)video_frame.p_data,
				width, height, (unsigned int)video_frame.line_stride_in_bytes, bSwapRB, bInvert);
		}
		else {
			// No bgra conversion or invert, so use the pointer directly
			video_frame.p_data = (uint8_t*)pixels;
		}

		// Submit the audio buffer first.
		// Refer to the NDI SDK example where for 48000 sample rate
		// and 29.97 fps, an alternating sample number is used.
		// Do this in the application using SetAudioSamples(nSamples);
		// General reference : http://jacklinstudios.com/docs/post-primer.html
		if (m_bAudio && m_audio_frame.p_data != NULL)
			NDIlib_send_send_audio_v2(pNDI_send, &m_audio_frame);

		// Metadata
		if (m_bMetadata && !m_metadataString.empty()) {
			metadata_frame.length = (int)m_metadataString.size();
			metadata_frame.timecode = NDIlib_send_timecode_synthesize;
			metadata_frame.p_data = (char *)m_metadataString.c_str(); // XML message format
			NDIlib_send_send_metadata(pNDI_send, &metadata_frame);
			// printf("Metadata\n%s\n", m_metadataString.c_str());
		}

		if (m_bAsync) {
			// Submit the frame asynchronously. This means that this call will return immediately and the 
			// API will "own" the memory location until there is a synchronizing event. A synchronouzing event is 
			// one of : NDIlib_send_send_video_async, NDIlib_send_send_video, NDIlib_send_destroy
			NDIlib_send_send_video_async_v2(pNDI_send, &video_frame);
		}
		else {
			// Submit the frame. Note that this call will be clocked
			// so that we end up submitting at exactly the predetermined fps.
			NDIlib_send_send_video_v2(pNDI_send, &video_frame);
		}
		return true;
	}

	return false;
}

// Close sender and release resources
void ofxNDIsend::ReleaseSender()
{
	// Destroy the NDI sender
	if (pNDI_send) NDIlib_send_destroy(pNDI_send);

	// Release the invert buffer
	if (p_frame) free((void*)p_frame);

	p_frame = NULL;
	pNDI_send = NULL;

	// Reset sender dimensions
	m_Width = m_Height = 0;
	bSenderInitialized = false;

}

// Return whether the sender has been created
bool ofxNDIsend::SenderCreated()
{
	return bSenderInitialized;
}

// Return current sender width
unsigned int ofxNDIsend::GetWidth()
{
	return m_Width;
}

// Return current sender height
unsigned int ofxNDIsend::GetHeight()
{
	return m_Height;
}

// Return current colour format
NDIlib_FourCC_type_e ofxNDIsend::GetColorFormat()
{
	return m_ColorFormat;
}

// Set frame rate - frames per second whole number
void ofxNDIsend::SetFrameRate(int framerate)
{
	// Keep scales compatible
	m_frame_rate_N = framerate * 1000;
	m_frame_rate_D = 1000;
	// Async sending is now off
	SetAsync(false);
}

// Set frame rate - frames per second decimal number
void ofxNDIsend::SetFrameRate(double framerate)
{
	m_frame_rate_N = int(framerate * 1000.0);
	m_frame_rate_D = 1000;
	SetAsync(false);
}

// Set frame rate - frames per second numerator and denominator
void ofxNDIsend::SetFrameRate(int framerate_N, int framerate_D)
{
	if (framerate_D > 0) {
		m_frame_rate_N = framerate_N;
		m_frame_rate_D = framerate_D;
		SetAsync(false);
	}
	// Aspect ratio is calculated in CreateSender
}

// Get current frame rate as numerator and denominator
void ofxNDIsend::GetFrameRate(int &framerate_N, int &framerate_D)
{
	framerate_N = m_frame_rate_N;
	framerate_D = m_frame_rate_D;
}

// Set aspect ratio
void ofxNDIsend::SetAspectRatio(int horizontal, int vertical)
{
	m_horizontal_aspect = horizontal;
	m_vertical_aspect = vertical;
	// Calculate for return
	m_picture_aspect_ratio = (float)horizontal/(float)vertical;
}

// Get current aspect ratio
void ofxNDIsend::GetAspectRatio(float &aspect)
{
	aspect = m_picture_aspect_ratio;
}

// Set progressive mode
void ofxNDIsend::SetProgressive(bool bProgressive)
{
	m_bProgressive = bProgressive;
}

// Get whether progressive
bool ofxNDIsend::GetProgressive()
{
	return m_bProgressive;
}

// Set clocked
void ofxNDIsend::SetClockVideo(bool bClocked)
{
	m_bClockVideo = bClocked;
}

// Get whether clocked
bool ofxNDIsend::GetClockVideo()
{
	return m_bClockVideo;
}

// Set asynchronous sending mode
void ofxNDIsend::SetAsync(bool bActive)
{
	m_bAsync = bActive;
	/// Do not clock video for async sending
	/// 11.06.18 - do this check in CreateSender
}

// Get whether asynchronous sending mode
bool ofxNDIsend::GetAsync()
{
	return m_bAsync;
}

// Set to send Audio
void ofxNDIsend::SetAudio(bool bAudio)
{
	m_bAudio = bAudio;
}

// Set audio sample rate
void ofxNDIsend::SetAudioSampleRate(int sampleRate)
{
	m_AudioSampleRate = sampleRate;
	m_audio_frame.sample_rate = sampleRate;
}

// Set number of audio channels
void ofxNDIsend::SetAudioChannels(int nChannels)
{
	m_AudioChannels = nChannels;
	m_audio_frame.no_channels = nChannels;
	m_audio_frame.channel_stride_in_bytes = (m_AudioChannels-1)*m_AudioSamples*sizeof(FLOAT);
}

// Set number of audio samples
void ofxNDIsend::SetAudioSamples(int nSamples)
{
	m_AudioSamples = nSamples;
	m_audio_frame.no_samples  = nSamples;
	m_audio_frame.channel_stride_in_bytes = (m_AudioChannels-1)*m_AudioSamples*sizeof(FLOAT);
}

// Set audio timecode
void ofxNDIsend::SetAudioTimecode(int64_t timecode)
{
	m_AudioTimecode = timecode;
	m_audio_frame.timecode = timecode;
}

// Set audio data
void ofxNDIsend::SetAudioData(float *data)
{
	m_AudioData = data;
	m_audio_frame.p_data = data;
}

// Set to send metadata
void ofxNDIsend::SetMetadata(bool bMetadata)
{
	m_bMetadata = bMetadata;
}

// Set metadata
void ofxNDIsend::SetMetadataString(std::string datastring)
{
	m_metadataString = datastring;
}

// Get the current NDI SDK version
std::string ofxNDIsend::GetNDIversion()
{
	return NDIlib_version();
}

