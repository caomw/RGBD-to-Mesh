#include "FileUtils.h"


void saveRGBDFrameImagesToFiles(string filename, RGBDFramePtr frame)
{
	if(frame->hasColor())
	{
		//Write color file
		ofstream rgbfile (filename+".rgb", ios::out|ios::binary);
		if (rgbfile.is_open())
		{
			char* rgbData = (char*)frame->getColorArray().get();
			int memSize = frame->getXRes()*frame->getYRes()*sizeof(ColorPixel);
			rgbfile.write(rgbData, memSize);
			rgbfile.close();
		}
	}

	if(frame->hasDepth())
	{
		//write depth file//Write color file
		ofstream depthfile (filename+".depth", ios::out|ios::binary);
		if (depthfile.is_open())
		{
			char* depthData = (char*)frame->getDepthArray().get();
			int memSize = frame->getXRes()*frame->getYRes()*sizeof(DPixel);
			depthfile.write(depthData, memSize);
			depthfile.close();
		}
	}
}


void loadRGBDFrameImagesFromFiles(string filename, RGBDFramePtr frame)
{
		//Write color file
		ifstream rgbfile (filename+".rgb", ios::out|ios::binary);
		if (rgbfile.is_open())
		{
			char* rgbData = (char*)frame->getColorArray().get();
			int memSize = frame->getXRes()*frame->getYRes()*sizeof(ColorPixel);
			rgbfile.read(rgbData, memSize);
			rgbfile.close();
			frame->setHasColor(true);
		}

		//write depth file//Write color file
		ifstream depthfile (filename+".depth", ios::out|ios::binary);
		if (depthfile.is_open())
		{
			char* depthData = (char*)frame->getDepthArray().get();
			int memSize = frame->getXRes()*frame->getYRes()*sizeof(DPixel);
			depthfile.read(depthData, memSize);
			depthfile.close();
			frame->setHasDepth(true);
		}
	
}