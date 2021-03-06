/*
	NDI Receiver

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

	08.07.16 - Uses ofxNDIreceive class
	11.07.18 - Add ReceiveImage for a texture
			 - Change to always create an RGBA receiver.
			   Allow the SDK to do the conversion from other formats. 
			   Openframeworks is RGBA so any other format would need
			   to be converted in any case. 
	16.07.18 - Add GetFrameType
	06.08.18 - Add receive to ofFbo
			 - Check for receiver creation in ReceiveImage to unsigned char array

	New functions and changes for 3.5 update:

			bool ReceiverCreated()
			bool ReceiveImage(ofFbo &fbo)
			bool ReceiveImage(ofTexture &texture)
			bool ReceiveImage(ofImage &image)
			bool ReceiveImage(ofPixels &pixels)
			NDIlib_frame_type_e GetFrameType()
			bool GetSenderName(char *sendername, int maxsize, int index = -1)
			std::string GetSenderName(int index = -1)
			unsigned int GetSenderWidth()
			unsigned int GetSenderHeight()
			double GetFps()

*/
#include "ofxNDIreceiver.h"


ofxNDIreceiver::ofxNDIreceiver()
{

}

ofxNDIreceiver::~ofxNDIreceiver()
{
	
}

// Create a receiver
bool ofxNDIreceiver::CreateReceiver(int userindex)
{
	return NDIreceiver.CreateReceiver(userindex);
}

// Create a receiver with preferred colour format
bool ofxNDIreceiver::CreateReceiver(NDIlib_recv_color_format_e color_format, int userindex)
{
	return NDIreceiver.CreateReceiver(color_format, userindex);

}

// Open the receiver to receive
bool ofxNDIreceiver::OpenReceiver()
{
	// Update the NDI sender list to find new senders
	// There is no delay if no new senders are found
	NDIreceiver.FindSenders();
	// Check the sender count
	int nSenders = GetSenderCount();
	if (nSenders > 0) {

		// Has the user changed the sender index ?
		if (NDIreceiver.SenderSelected()) {
			// Retain the last sender in case of network delay
			// Wait for the network to come back up or for the
			// user to select another sender when it does
			if (nSenders == 1)
				return false;
			// Release the current receiver.
			// A new one is then created from the selected sender index.
			NDIreceiver.ReleaseReceiver();
			return false;
		}

		// Receiver already created
		if (NDIreceiver.ReceiverCreated())
			return true;

		// Create a new receiver if one does not exist.
		// A receiver is created from an index into a list of sender names.
		// The current user selected index is saved in the NDIreceiver class
		// and is used to create the receiver unless you specify a particular index.
		return NDIreceiver.CreateReceiver();

	}

	// No senders
	return false;

}

// Return whether the receiver has been created
bool ofxNDIreceiver::ReceiverCreated()
{
	return NDIreceiver.ReceiverCreated();
}

// Close receiver and release resources
void ofxNDIreceiver::ReleaseReceiver()
{
	NDIreceiver.ReleaseReceiver();
}

// Receive an fbo
// Fbo re-allocated to changed sender dimensions
// For false return, check for metadata using IsMetadata()
bool ofxNDIreceiver::ReceiveImage(ofFbo &fbo)
{
	if (!fbo.isAllocated())
		return false;

	// Check for receiver creation
	if (!OpenReceiver())
		return false;

	unsigned int width = (unsigned int)fbo.getWidth();
	unsigned int height = (unsigned int)fbo.getHeight();

	// Receive a pixel image first
	if (NDIreceiver.ReceiveImage(width, height)) {

		// Get the video frame buffer pointer
		unsigned char *videoData = NDIreceiver.GetVideoData();
		if (!videoData) {
			printf("ReceiveTexture : No video data\n");
			return false;
		}

		// Check for changed sender dimensions
		// to re-allocate the receiving texture
		if (width != (unsigned int)fbo.getWidth() || height != (unsigned int)fbo.getHeight())
			fbo.allocate(width, height, GL_RGBA);

		// Get the NDI frame pixel data into the fbo texture
		fbo.getTexture().loadData((const unsigned char *)videoData, width, height, GL_RGBA);

		// Free the NDI video buffer
		NDIreceiver.FreeVideoData();

		return true;
	}

	return false;

}


// Receive a texture
// Texture re-allocated to changed sender dimensions
// For false return, check for metadata using IsMetadata()
bool ofxNDIreceiver::ReceiveImage(ofTexture &texture)
{
	if (!texture.isAllocated())
		return false;

	// Check for receiver creation
	if (!OpenReceiver())
		return false;

	unsigned int width = (unsigned int)texture.getWidth();
	unsigned int height = (unsigned int)texture.getHeight();

	// Receive a pixel image first
	if (NDIreceiver.ReceiveImage(width, height)) {

		// Get the video frame buffer pointer
		unsigned char *videoData = NDIreceiver.GetVideoData();
		if (!videoData) {
			printf("ReceiveTexture : No video data\n");
			return false;
		}

		// Check for changed sender dimensions
		// to re-allocate the receiving texture
		if (width != (unsigned int)texture.getWidth() || height != (unsigned int)texture.getHeight())
			texture.allocate(width, height, GL_RGBA);

		// Get the NDI frame pixel data into the texture
		texture.loadData((const unsigned char *)videoData, width, height, GL_RGBA);

		// Free the NDI video buffer
		NDIreceiver.FreeVideoData();

		return true;
	}

	return false;

}

// Receive an image
// Image re-allocated to changed sender dimensions
// For false return, check for metadata using IsMetadata()
bool ofxNDIreceiver::ReceiveImage(ofImage &image)
{
	if (!image.isAllocated())
		return false;

	// Check for receiver creation
	if (!OpenReceiver())
		return false;

	unsigned int width = (unsigned int)image.getWidth();
	unsigned int height = (unsigned int)image.getHeight();

	// Receive a pixel image first
	if (NDIreceiver.ReceiveImage(width, height)) {

		// Get the video frame buffer pointer
		unsigned char *videoData = NDIreceiver.GetVideoData();
		if (!videoData)
			return false;

		// Check for changed sender dimensions
		// to re-allocate the receiving image
		if (width != (unsigned int)image.getWidth() || height != (unsigned int)image.getHeight())
			image.allocate(width, height, OF_IMAGE_COLOR_ALPHA);

		// Get the NDI frame pixel data into the image texture
		image.getTexture().loadData((const unsigned char *)videoData, width, height, GL_RGBA);

		// Free the NDI video buffer
		NDIreceiver.FreeVideoData();

		return true;
	}

	return false;

}

// Receive a pixel buffer
// Buffer re-allocated with changed sender dimensions
// For false return, check for metadata using IsMetadata()
bool ofxNDIreceiver::ReceiveImage(ofPixels &buffer)
{
	if (!buffer.isAllocated())
		return false;

	// Check for receiver creation
	if (!OpenReceiver())
		return false;

	unsigned int width = (unsigned int)buffer.getWidth();
	unsigned int height = (unsigned int)buffer.getHeight();

	// Receive a pixel image first
	if (NDIreceiver.ReceiveImage(width, height)) {

		// Get the video frame buffer pointer
		unsigned char *videoData = NDIreceiver.GetVideoData();
		if (!videoData)
			return false;

		// Check for changed sender dimensions
		// to re-allocate the receiving image
		if (width != (unsigned int)buffer.getWidth() || height != (unsigned int)buffer.getHeight())
			buffer.allocate(width, height, OF_IMAGE_COLOR_ALPHA);

		// Get the NDI frame pixel data into the pixel buffer
		buffer.setFromExternalPixels((unsigned char *)videoData, width, height, OF_PIXELS_RGBA);

		// Free the NDI video buffer
		NDIreceiver.FreeVideoData();

		return true;
	}

	return false;

}

// Receive image pixels to a char buffer
// Retained for compatibility with previous version of ofxNDI
// Return sender width and height
// Test width and height for change with true return
// For false return, check for metadata using IsMetadata()
bool ofxNDIreceiver::ReceiveImage(unsigned char *pixels,
	unsigned int &width, unsigned int &height, bool bInvert)
{

	if (!pixels)
		return false;

	// Check for receiver creation
	if (!OpenReceiver())
		return false;

	return NDIreceiver.ReceiveImage(pixels, width, height, bInvert);
}

// Create a finder to look for a sources on the network
void ofxNDIreceiver::CreateFinder()
{
	NDIreceiver.CreateFinder();
}

// Release the current finder
void ofxNDIreceiver::ReleaseFinder()
{
	NDIreceiver.ReleaseFinder();
}

// Find all current NDI senders
int ofxNDIreceiver::FindSenders()
{
	return NDIreceiver.FindSenders();
}

// Refresh sender list with the current network snapshot
int ofxNDIreceiver::RefreshSenders(uint32_t timeout)
{
	return NDIreceiver.RefreshSenders(timeout);
}

// Set the sender list index variable
bool ofxNDIreceiver::SetSenderIndex(int index)
{
	return NDIreceiver.SetSenderIndex(index);
}

// Return the index of the current sender
int ofxNDIreceiver::GetSenderIndex()
{
	return NDIreceiver.GetSenderIndex();
}

// Return the index of a sender name
bool ofxNDIreceiver::GetSenderIndex(char *sendername, int &index)
{
	return NDIreceiver.GetSenderIndex(sendername, index);
}

// Has the user changed the sender index ?
bool ofxNDIreceiver::SenderSelected()
{
	return NDIreceiver.SenderSelected();
}

// Return the number of senders
int ofxNDIreceiver::GetSenderCount()
{
	return NDIreceiver.GetSenderCount();
}

// Return the name characters of a sender index
// For back-compatibility only
// Char functions replaced with string version
bool ofxNDIreceiver::GetSenderName(char *sendername)
{
	// Length of user name string is not known
	int index = -1;
	return GetSenderName(sendername, 128, index);
}

bool ofxNDIreceiver::GetSenderName(char *sendername, int index)
{
	// Length of user name string is not known
	return GetSenderName(sendername, 128, index);
}

bool ofxNDIreceiver::GetSenderName(char *sendername, int maxsize, int userindex)
{
	return NDIreceiver.GetSenderName(sendername, maxsize, userindex);
}

// Return the name string of a sender index
std::string ofxNDIreceiver::GetSenderName(int userindex)
{
	return NDIreceiver.GetSenderName(userindex);
}

// Return the current sender width
unsigned int ofxNDIreceiver::GetSenderWidth() {
	return NDIreceiver.GetSenderWidth();
}

// Return the current sender height
unsigned int ofxNDIreceiver::GetSenderHeight() {
	return NDIreceiver.GetSenderHeight();
}

//
// Bandwidth
//
// NDIlib_recv_bandwidth_lowest will provide a medium quality stream that takes almost no bandwidth,
// this is normally of about 640 pixels in size on it is longest side and is a progressive video stream.
// NDIlib_recv_bandwidth_highest will result in the same stream that is being sent from the up-stream source
//
void ofxNDIreceiver::SetLowBandwidth(bool bLow)
{
	NDIreceiver.SetLowBandwidth(bLow);
}

// Return the received frame type
NDIlib_frame_type_e ofxNDIreceiver::GetFrameType()
{
	return NDIreceiver.GetFrameType();
}

// Is the current frame MetaData ?
bool ofxNDIreceiver::IsMetadata()
{
	return NDIreceiver.IsMetadata();
}

// Return the current MetaData string
std::string ofxNDIreceiver::GetMetadataString()
{
	return NDIreceiver.GetMetadataString();
}

// Return the NDI dll version number
std::string ofxNDIreceiver::GetNDIversion()
{
	return NDIreceiver.GetNDIversion();
}

// Return the received frame rate
double ofxNDIreceiver::GetFps()
{
	return NDIreceiver.GetFps();
}


