/*
	NDI Receive

	using the NDI SDK to receive frames from the network

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

	20.06.16 - Added RefreshSenders for Max receiver
			 - Added GetSenderIndex(char *sendername, int &index)
	01.07.16 - Added UNREFERENCED_PARAMETER as required
	02.07.16 - Minor changes to RefreshSenders for NDIwebcam
	07.07.16 - Duplicate of global video_frame in ReceiveImage removed
			   Rebuild with VS2015
	25.07.16 - Timeout removed for NDIlib_recv_capture
	10.10.16 - Updated SSE2 memcpy with intrinsics for 64bit compatibility
			   (NDI applications require minimum SSE3)
			   Added "rgba_bgra_sse2" for rgba <> bgra conversion
			   and bSwapRB and bInvert options to RecieveImage and CopyImage
			   Removed ReceiveTexture - not working
	11.10.16 - Changed CreateReceiver
			     . use only an index rather than return a name as well
				 . do not change the current user selected index
			   Changed GetSenderName to use an optional sender index
	07.11.16 - Added CPU support check
	09.02.17 - include changes by Harvey Buchan for NDI SDK version 2
			 - Added Metadata
			 - Added option to specify low bandwidth NDI receiving mode
			 - Removed bSwapRB option from ReceiveImage - now done internally
			 - Replacement function for deprecated NDIlib_find_get_sources
	17.02.17 - Added GetNDIversion - NDIlib_version
	22.02.17 - cleanup
	31.03.18 - Update to NDI SDK Version 3 - search on "Vers 3"
	         - change functions to _v2
			 - change variable types
	11.06.18 - remove messageboxes and replace with cout
			 - correct strcpy_s in GetSenderName for string length
			   (see https://github.com/ThomasLengeling/ofxNDI/commit/216d8d90f811ba73c02bceed60d9deb3f09b02ef)
			 - change ReceiveImage to use the video frame pointer externally
			   rather than copy to a buffer
			   Added GetVideoData, FreeVideoData
	08.07.18 - Change GetSenderName to include size of char buffer
	         - Change class name to ofxDNIreceive
			 - Set allow_video_fields flag FALSE for CreateReceiver
	12.07.18 - Add senderName
			 - Set sender name in CreateReceiver
			 - Update sender name list only if sender number changes
			 - Update sender index after find sources
	13.07.18 - Add 
				string GetSenderName
				GetSenderWidth
				GetSenderHeight
				GetFps
	16.07.18 - Add GetFrameType
			 - Use existing sender name for GetSenderName(-1)
	30.07.18 - const char for GetSenderIndex(const char *sendername, ..
			 - Added GetSenderIndex(std::string sendername, int &index)
	06.08.18 - SetSenderIndex return false for the same sender

	New functions and changes for 3.5 uodate:

				bool GetSenderName(char *sendername, int maxsize, int index = -1)
				bool ReceiveImage(unsigned int &width, unsigned int &height)
				NDIlib_FourCC_type_e GetVideoType()
				unsigned char *GetVideoData()
				void FreeVideoData()
				NDIlib_frame_type_e GetFrameType()
				std::string GetSenderName(int index = -1)
				unsigned int GetSenderWidth()
				unsigned int GetSenderHeight()
				double GetFps()


*/
#include "ofxNDIreceive.h"


ofxNDIreceive::ofxNDIreceive()
{
	pNDI_find = NULL;
	pNDI_recv = NULL;
	p_sources = NULL;
	no_sources = 0;
	bNDIinitialized = false;
	bReceiverCreated = false;
	bSenderSelected = false;
	m_FrameType = NDIlib_frame_type_none;
	nsenders = 0;
	m_Width = 0;
	m_Height = 0;
	senderIndex = 0;
	senderName = "";
	
	// For received frame fps calculations
	frameTime = 0.0;
	fps = frameRate = 1.0; // starting value
	startTime = lastTime = (double)timeGetTime();

	m_bandWidth = NDIlib_recv_bandwidth_highest;

	if(!NDIlib_is_supported_CPU() ) {
		std::cout << "CPU does not support NDI NDILib requires SSE4.1 NDIreceiver" << std::endl;
	}
	else {
		bNDIinitialized = NDIlib_initialize();
		if(!bNDIinitialized) {
			std::cout << "Cannot run NDI - NDILib initialization failed" << std::endl;
		}

	}

}

ofxNDIreceive::~ofxNDIreceive()
{
	if(pNDI_recv) NDIlib_recv_destroy(pNDI_recv);
	if(pNDI_find) NDIlib_find_destroy(pNDI_find);
	if(bNDIinitialized)	NDIlib_destroy();
}

// Create a finder to look for a sources on the network
void ofxNDIreceive::CreateFinder()
{
	if(!bNDIinitialized) return;

	if(pNDI_find) NDIlib_find_destroy(pNDI_find);
	const NDIlib_find_create_t NDI_find_create_desc = { TRUE, NULL, NULL }; // Version 2
	// pNDI_find = NDIlib_find_create2(&NDI_find_create_desc);
	// Vers 3
	pNDI_find = NDIlib_find_create_v2(&NDI_find_create_desc);
	p_sources = NULL;
	no_sources = 0;
	nsenders = 0;

}

// Release the current finder
void ofxNDIreceive::ReleaseFinder()
{
	if(!bNDIinitialized) return;

	if(pNDI_find) NDIlib_find_destroy(pNDI_find);
	pNDI_find = NULL;
	p_sources = NULL;
	no_sources = 0;

}


// Find all current NDI senders
int ofxNDIreceive::FindSenders()
{
	std::string name;
	uint32_t nsources = 0; // New number of sources

	if(!bNDIinitialized) {
		printf("FindSenders : NDI not initialized\n");
		return 0;
	}

	// If a finder was created, use it to find senders on the network
	if (pNDI_find) {
		
		//
		// This may be called for every frame so has to be fast.
		//

		// Specify a delay so that p_sources is returned only for a network change.
		// If there was no network change, p_sources is NULL and no_sources = 0 
		// and can't be used for other functions, so the sender names as well as 
		// the sender count need to be saved locally.
		p_sources = FindGetSources(pNDI_find, &nsources, 1);

		// If there are new sources and the number has changed
		if (p_sources && nsources != no_sources) {

			// Rebuild the sender name list
			no_sources = nsources;
			NDIsenders.clear();

			if (no_sources > 0) {
				for (int i = 0; i<(int)no_sources; i++) {
					if (p_sources[i].p_ndi_name && p_sources[i].p_ndi_name[0]) {
						NDIsenders.push_back(p_sources[i].p_ndi_name);
					}
				}
			}

			// Update the current sender index
			// because it's position may have changed
			if (!senderName.empty()) {

				// If there are no senders left, close the current receiver
				if (NDIsenders.size() == 0) {
					ReleaseReceiver();
					senderName.clear();
					senderIndex = 0;
					return 0;
				}

				// Reset the current sender index
				if (NDIsenders.size() > 0) {
					senderIndex = 0;
					for (int i = 0; i < (int)NDIsenders.size(); i++) {
						if (senderName == NDIsenders.at(i))
							senderIndex = i;
					}
				}

				// printf("ofxNDIreceive::FindSenders - new sender index = %d\n", senderIndex);

				// Signal a new sender if it is not the same one
				// The calling application can then query this
				if (senderName != NDIsenders.at(senderIndex)) {
					// printf("ofxNDIreceive::FindSenders - new sender\n");
					bSenderSelected = true;
				}

			}
		}
	}
	else {
		CreateFinder();
	}

	return (int)NDIsenders.size();
}

// Refresh NDI sender list with the current network snapshot
// No longer used
int ofxNDIreceive::RefreshSenders(uint32_t timeout)
{
	std::string name;
	uint32_t nsources = 0;

	if(!bNDIinitialized) return 0;

	// Release the current finder
	if(pNDI_find) ReleaseFinder();
	if(!pNDI_find) CreateFinder();

	// If a finder was created, use it to find senders on the network
	// Give it a timeout in case of connection trouble.
	if(pNDI_find) {

		dwStartTime = timeGetTime();
		dwElapsedTime = 0;
		do {
			p_sources = NDIlib_find_get_current_sources(pNDI_find, &nsources);
			dwElapsedTime = timeGetTime() - dwStartTime;
		} while(nsources == 0 && (uint32_t)dwElapsedTime < timeout);
		return nsources;

	}

	return 0;
}

// Set current sender index in the sender list
bool ofxNDIreceive::SetSenderIndex(int index)
{
	if(!bNDIinitialized) 
		return false;

	if (NDIsenders.empty() || NDIsenders.size() == 0)
		return false;

	senderIndex = index;
	if (senderIndex > (int)NDIsenders.size())
		senderIndex = 0;

	// Return for the same sender
	if (NDIsenders.at(senderIndex) == senderName)
		return false;

	// Update the class sender name
	senderName = NDIsenders.at(senderIndex);

	// Set selected flag to indicate that the user has changed sender index
	bSenderSelected = true; 

	return true;

}

// Return the index of the current sender
int ofxNDIreceive::GetSenderIndex()
{
	return senderIndex;
}

// Get the index of a sender name
bool ofxNDIreceive::GetSenderIndex(const char *sendername, int &index)
{
	std::string name = sendername;
	return GetSenderIndex(name, index);
}

// Has the user changed the sender index ?
bool ofxNDIreceive::SenderSelected()
{
	bool bSelected = bSenderSelected;
	bSenderSelected = false; // one off - the user has to select again

	return bSelected;
}

// Return the number of senders
int ofxNDIreceive::GetSenderCount()
{
	return (int)NDIsenders.size();
}

// Return the name characters of a sender index
// For back-compatibility only
// Char functions replaced with string versions
bool ofxNDIreceive::GetSenderName(char *sendername)
{
	// Length of user name string is not known
	// assume 128 characters maximum
	int idx = -1;
	int index = GetSenderIndex(sendername, idx);
	return GetSenderName(sendername, 128, index);
}

bool ofxNDIreceive::GetSenderName(char *sendername, int index)
{
	// Length of user name string is not known
	// assume 128 characters maximum
	return GetSenderName(sendername, 128, index);
}

bool ofxNDIreceive::GetSenderName(char *sendername, int maxsize, int userindex)
{
	int index = userindex;

	if (index > (int)NDIsenders.size() - 1)
		return false;

	// If no index has been specified, use the currently selected index
	if (userindex < 0) {
		// If there is an existing name, return it
		if (!senderName.empty()) {
			strcpy_s(sendername, maxsize, senderName.c_str());
			return true;
		}
		// Otherwise use the existing index
		index = senderIndex;
	}

	if (NDIsenders.size() > 0
		&& (unsigned int)index < NDIsenders.size()
		&& !NDIsenders.empty()
		&& NDIsenders.at(index).size() > 0) {
		strcpy_s(sendername, maxsize, NDIsenders.at(index).c_str());
		return true;
	}

	return false;
}


// Get the index of a sender name string
bool ofxNDIreceive::GetSenderIndex(std::string sendername, int &index)
{
	if (sendername.empty()) return false;

	if (NDIsenders.size() > 0) {
		for (int i = 0; i<(int)NDIsenders.size(); i++) {
			if (sendername == NDIsenders.at(i)) {
				index = i;
				return true;
			}
		}
	}
	return false;
}


// Get the name string of a sender index
std::string ofxNDIreceive::GetSenderName(int userindex)
{
	int index = userindex;

	if (index > (int)NDIsenders.size() - 1)
		return senderName;

	// If no index has been specified, use the currently selected index
	if (userindex < 0) {
		// If there is an existing name, return it
		if (!senderName.empty())
			return senderName;
		// Otherwise use the existing index
		index = senderIndex;
	}

	if (NDIsenders.size() > 0
		&& (unsigned int)index < NDIsenders.size()
		&& !NDIsenders.empty()
		&& NDIsenders.at(index).size() > 0) {
		return NDIsenders.at(index);
	}

	return NULL;
}

// Return current sender width
unsigned int ofxNDIreceive::GetSenderWidth()
{
	return m_Width;
}

// Return current sender height
unsigned int ofxNDIreceive::GetSenderHeight()
{
	return m_Height;
}

//
// Bandwidth
//
// NDIlib_recv_bandwidth_lowest will provide a medium quality stream that takes almost no bandwidth,
// this is normally of about 640 pixels in size on it is longest side and is a progressive video stream.
// NDIlib_recv_bandwidth_highest will result in the same stream that is being sent from the up-stream source
//
void ofxNDIreceive::SetLowBandwidth(bool bLow)
{
	if(bLow)
		m_bandWidth = NDIlib_recv_bandwidth_lowest; // Low bandwidth receive option
	else
		m_bandWidth = NDIlib_recv_bandwidth_highest;

}

// Return the received frame type
NDIlib_frame_type_e ofxNDIreceive::GetFrameType()
{
	return m_FrameType;
}

// Is the current frame MetaData ?
bool ofxNDIreceive::IsMetadata()
{
	return m_bMetadata;
}

// Return the current MetaData string
std::string ofxNDIreceive::GetMetadataString()
{
	return m_metadataString;
}

// Create an RGBA receiver
bool ofxNDIreceive::CreateReceiver(int userindex)
{
	return CreateReceiver(NDIlib_recv_color_format_e_RGBX_RGBA, userindex);
}

// Create a receiver with preferred colour format
bool ofxNDIreceive::CreateReceiver(NDIlib_recv_color_format_e colorFormat , int userindex)
{
	std::string name;
	int nsources = 0;

	if (!bNDIinitialized) 
		return false;

	int index = userindex;

	// printf("ofxNDIreceive::CreateReceiver - format = %d, user index (%d)\n", colorFormat, userindex);

	if (!pNDI_recv) {

		// The continued check in FindSenders is for a network change and
		// p_sources is returned NULL, so we need to find all the sources
		// again to get a pointer to the selected sender.
		// Give it a timeout in case of connection trouble.
		if (pNDI_find) {
			dwStartTime = timeGetTime();
			do {
				p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
				dwElapsedTime = timeGetTime() - dwStartTime;
			} while (no_sources == 0 && dwElapsedTime < 4000);
		}

		if (p_sources && no_sources > 0) {

			// Quit if the user index is greater than the number of sources
			if (userindex > (int)no_sources - 1)
				return false;

			// If no index has been specified (-1), use the currently set index
			if (userindex < 0)
				index = senderIndex;

			// Rebuild the name list
			NDIsenders.clear();
			if (no_sources > 0) {
				for (int i = 0; i<(int)no_sources; i++) {
					if (p_sources[i].p_ndi_name && p_sources[i].p_ndi_name[0]) {
						NDIsenders.push_back(p_sources[i].p_ndi_name);
					}
				}
			}

			// Release the receiver if not done already
			if (bReceiverCreated) 
				ReleaseReceiver();

			// We tell it that we prefer the passed format
			// NDIlib_recv_create_t NDI_recv_create_desc = {
			// Vers 3.5
			NDIlib_recv_create_v3_t NDI_recv_create_desc = {
				p_sources[index],
				colorFormat,
				m_bandWidth, // Changed by SetLowBandwidth, default NDIlib_recv_bandwidth_highest
				FALSE }; // TRUE }; // allow_video_fields FALSE : TODO - test

			// Create the receiver
			// Deprecated version sets bandwidth to highest and allow fields to true.
			// pNDI_recv = NDIlib_recv_create2(&NDI_recv_create_desc);
			// Vers 3
			// pNDI_recv = NDIlib_recv_create_v2(&NDI_recv_create_desc);
			// Vers 3.5
			pNDI_recv = NDIlib_recv_create_v3(&NDI_recv_create_desc);
			if (!pNDI_recv) {
				printf("CreateReceiver : NDIlib_recv_create_v3 error\n");
				return false;
			}

			// Reset the current sender name
			senderName = NDIsenders.at(index);

			// Reset the sender index
			senderIndex = index;

			// Start counter for frame fps calculations
			StartCounter();

			// on_program = TRUE, on_preview = FALSE
			const NDIlib_tally_t tally_state = { TRUE, FALSE };
			NDIlib_recv_set_tally(pNDI_recv, &tally_state);

			// Set class flag that a receiver has been created
			bReceiverCreated = true;

			return true;

		}
	} // end create receiver

	return false;
}

// Return whether the receiver has been created
bool ofxNDIreceive::ReceiverCreated()
{
	return bReceiverCreated;
}

// Close receiver and release resources
void ofxNDIreceive::ReleaseReceiver()
{
	if(!bNDIinitialized) return;

	if(pNDI_recv) 
		NDIlib_recv_destroy(pNDI_recv);

	m_Width = 0;
	m_Height = 0;
	senderName.empty();
	pNDI_recv = NULL;
	bReceiverCreated = false;
	bSenderSelected = false;
	FreeVideoData();

}

// Receive RGBA image pixels to a buffer
bool ofxNDIreceive::ReceiveImage(unsigned char *pixels,
								  unsigned int &width, unsigned int &height, bool bInvert)
{
	NDIlib_frame_type_e NDI_frame_type;
	NDIlib_metadata_frame_t metadata_frame;
	m_FrameType = NDIlib_frame_type_none;

	if (pNDI_recv) {

		NDI_frame_type = NDIlib_recv_capture_v2(pNDI_recv, &video_frame, NULL, &metadata_frame, 0);
		
		// Is no data received or the connection lost ?
		if (NDI_frame_type == NDIlib_frame_type_none)
			return false;

		if (NDI_frame_type == NDIlib_frame_type_error) {
			printf("ReceiveImage : NDI_frame_type_error\n");
			return false;
		}

		// Set frame type for external access
		m_FrameType = NDI_frame_type;

		// Metadata
		if (NDI_frame_type == NDIlib_frame_type_metadata) {
			if (metadata_frame.p_data) {
				m_bMetadata = true;
				m_metadataString = metadata_frame.p_data;
				// ReceiveImage will return false
				// Use IsMetadata() to determine whether metadata has been received
			}
		}
		else {
			m_bMetadata = false;
			if (!m_metadataString.empty())
				m_metadataString.clear();
		}

		// TODO - receive Audio

		if (video_frame.p_data && NDI_frame_type == NDIlib_frame_type_video) {

			if (m_Width != (unsigned int)video_frame.xres || m_Height != (unsigned int)video_frame.yres) {

				m_Width = (unsigned int)video_frame.xres;
				m_Height = (unsigned int)video_frame.yres;

				// Update the caller dimensions and return received OK
				// for the app to handle changed dimensions
				width = m_Width;
				height = m_Height;

				return true;
			}

			// Otherwise sizes are current - copy the received frame data to the local buffer
			if (video_frame.p_data && (uint8_t*)pixels) {

				// Video frame type
				switch (video_frame.FourCC) {

				// Note :
				// The receiver is set up to prefer RGBA format
				// so other formats should be converted to RGBA by the API
				// and the conversion functions never used.
				// They are here as a backup only.
				
				// NDIlib_FourCC_type_UYVA not supported
				// Alpha copied as received
				case NDIlib_FourCC_type_UYVY: // YCbCr color space
					ofxNDIutils::YUV422_to_RGBA((const unsigned char *)video_frame.p_data, pixels, m_Width, m_Height, (unsigned int)video_frame.line_stride_in_bytes);
					break;

				case NDIlib_FourCC_type_BGRA: // BGRA
				case NDIlib_FourCC_type_BGRX: // BGRX
					ofxNDIutils::CopyImage((const unsigned char *)video_frame.p_data, pixels, m_Width, m_Height, (unsigned int)video_frame.line_stride_in_bytes, true, bInvert);
					break;

				case NDIlib_FourCC_type_RGBA: // RGBA
				case NDIlib_FourCC_type_RGBX: // RGBX
				default: // RGBA
					ofxNDIutils::CopyImage((const unsigned char *)video_frame.p_data, pixels, m_Width, m_Height, (unsigned int)video_frame.line_stride_in_bytes, false, bInvert);
					break;

				} // end switch received format

				// Buffers captured must be freed
				NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);

				// The caller always checks the received dimensions
				width = m_Width;
				height = m_Height;

				// Update received frame counter
				UpdateFps();

				return true;

			} // endif video frame data

			return false;

		} // endif NDIlib_frame_type_video
	} // endif pNDI_recv

	return false;
}

// Receive image pixels without a receiving buffer
// The received video frame is held in ofxReceive class.
// Use the video frame data pointer externally with GetVideoData()
// For success, the video frame must be freed with FreeVideoData().
bool ofxNDIreceive::ReceiveImage(unsigned int &width, unsigned int &height)
{
	NDIlib_frame_type_e NDI_frame_type;
	NDIlib_metadata_frame_t metadata_frame;
	m_FrameType = NDIlib_frame_type_none;

	if (pNDI_recv) {

		NDI_frame_type = NDIlib_recv_capture_v2(pNDI_recv, &video_frame, NULL, &metadata_frame, 0);

		// Is no data received or the connection lost ?
		if (NDI_frame_type == NDIlib_frame_type_none)
			return false;
		
		if (NDI_frame_type == NDIlib_frame_type_error) {
			printf("ReceiveImage : NDI_frame_type_error\n");
			return false;
		}

		// Set frame type for external access
		m_FrameType = NDI_frame_type;

		// Metadata
		if (NDI_frame_type == NDIlib_frame_type_metadata) {
			if (metadata_frame.p_data) {
				m_bMetadata = true;
				m_metadataString = metadata_frame.p_data;
				// ReceiveImage will return false
				// Use IsMetadata() to determine whether metadata has been received
			}
		}
		else {
			m_bMetadata = false;
			if (!m_metadataString.empty())
				m_metadataString.clear();
		}

		if (video_frame.p_data && NDI_frame_type == NDIlib_frame_type_video) {

			if (m_Width != (unsigned int)video_frame.xres || m_Height != (unsigned int)video_frame.yres) {
				m_Width = (unsigned int)video_frame.xres;
				m_Height = (unsigned int)video_frame.yres;
			}
			
			// Retain the video frame pointer for external access.
			// Buffers captured must then be freed using FreeVideoData.
			// Update the caller dimensions and return received OK
			// for the app to handle changed dimensions
			width = m_Width;
			height = m_Height;

			// Update received frame counter
			UpdateFps();

			return true;
		} // endif NDIlib_frame_type_video

	} // endif pNDI_recv

	return false;
}

// Get the video type received
NDIlib_FourCC_type_e ofxNDIreceive::GetVideoType()
{
	return video_frame.FourCC;
}

// Get a pointer to the current video frame data
unsigned char *ofxNDIreceive::GetVideoData()
{
	return video_frame.p_data;
}

// Free NDI video frame buffers
void ofxNDIreceive::FreeVideoData()
{
	if (video_frame.p_data) NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
}

// Get NDI dll version number
std::string ofxNDIreceive::GetNDIversion()
{
	return NDIlib_version();
}

// Get the received frame rate
double ofxNDIreceive::GetFps()
{
	return fps;
}

//
// Private functions
//

// Version 2
// Replacement for deprecated NDIlib_find_get_sources
// If no timeout specified, return the sources that exist right now
// For a timeout, wait for that timeout and return the sources that exist then
// If that fails, return NULL
const NDIlib_source_t* ofxNDIreceive::FindGetSources(NDIlib_find_instance_t p_instance,
	uint32_t* p_no_sources,
	uint32_t timeout_in_ms)
{
	if (!p_instance)
		return NULL;

	if ((!timeout_in_ms) || (NDIlib_find_wait_for_sources(p_instance, timeout_in_ms))) {
		// Recover the current set of sources (i.e. the ones that exist right this second)
		return NDIlib_find_get_current_sources(p_instance, p_no_sources);
	}

	return NULL;

}

// Received fps is independent of the application draw rate
void ofxNDIreceive::UpdateFps() {

	unsigned int width = 0;
	unsigned int height = 0;

	// Calculate received frame fps
	lastTime = startTime;
	startTime = GetCounter();
	frameTime = (startTime - lastTime) / 1000.0; // in seconds

	if (frameTime  > 0.000001) {
		frameRate = floor(1.0 / frameTime + 0.5);
		// damping from a starting fps value
		fps *= 0.95;
		fps += 0.05*frameRate;
	}
}

void ofxNDIreceive::StartCounter()
{
	LARGE_INTEGER li;
	if (!QueryPerformanceFrequency(&li)) {
		printf("QueryPerformanceFrequency failed!\n");
		return;
	}

	PCFreq = double(li.QuadPart) / 1000.0;
	QueryPerformanceCounter(&li);
	CounterStart = li.QuadPart;

	// Reset starting frame rate value
	fps = frameRate = 1.0;
}

double ofxNDIreceive::GetCounter()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return double(li.QuadPart - CounterStart) / PCFreq;
}
