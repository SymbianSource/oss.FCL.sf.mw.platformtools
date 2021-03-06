/*
* Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies). 
* All rights reserved.
* This component and the accompanying materials are made available
* under the terms of "Eclipse Public License v1.0"
* which accompanies this distribution, and is available
* at the URL "http://www.eclipse.org/legal/epl-v10.html".
*
* Initial Contributors:
* Nokia Corporation - initial contribution.
*
* Contributors:
*
* Description:
*
*/

#include "BMCONV.H"
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>

extern TRgb* color256Palette;
extern char* color256InversePalette;

static inline bool CanCopy16bppData(const char* aDest, const char* aDestEnd, int aDataSize)
	{
	const char* aDestNew = 	aDest + (aDataSize / 128) * (256 + 1) + //(2 * 128) bytes data + 1 leading byte
		                            (aDataSize % 128 ? (aDataSize % 128) * 2 + 1 : 0);
	return aDestNew <= aDestEnd;
	}

static inline bool CanWrite16bppValue(const char* aDest, const char* aDestEnd, int aDataSize)
	{
	const char* aDestNew = 	aDest + (aDataSize / 128) * (2 + 1) + //2 bytes data + 1 leading byte
		                            (aDataSize % 128 ? 2 + 1 : 0);
	return aDestNew <= aDestEnd;
	}

static inline bool CanCopy8bppData(const char* aDest, const char* aDestEnd, int aDataSize)
	{
	const char* aDestNew = 	aDest + (aDataSize / 128) * (128 + 1) + //128 bytes data + 1 leading byte
		                            (aDataSize % 128 ? (aDataSize % 128) + 1 : 0);
	return aDestNew <= aDestEnd;
	}

static inline bool CanWrite8bppValue(const char* aDest, const char* aDestEnd, int aDataSize)
	{
	const char* aDestNew = 	aDest + (aDataSize / 128) * (1 + 1) + //1 byte data + 1 leading byte
		                            (aDataSize % 128 ? 1 + 1 : 0);
	return aDestNew <= aDestEnd;
	}

static inline bool CanCopy24bppData(const char* aDest, const char* aDestEnd, int aDataSize)
	{
	const char* aDestNew = 	aDest + (aDataSize / 128) * (384 + 1) + //(128 * 3) bytes data + 1 leading byte
		                            (aDataSize % 128 ? (aDataSize % 128) * 3 + 1 : 0);
	return aDestNew <= aDestEnd;
	}

static inline bool CanWrite24bppValue(const char* aDest, const char* aDestEnd, int aDataSize)
	{
	const char* aDestNew = 	aDest + (aDataSize / 128) * (3 + 1) + //3 bytes data + 1 leading byte
		                            (aDataSize % 128 ? 3 + 1 : 0);
	return aDestNew <= aDestEnd;
	}

static inline bool CanCopy32bppData(const char* aDest, const char* aDestEnd, int aDataSize)
	{
	const char* aDestNew = 	aDest + (aDataSize / 128) * (512 + 1) + //(128 * 4) bytes data + 1 leading byte
		                            (aDataSize % 128 ? (aDataSize % 128) * 4 + 1 : 0);
	return aDestNew <= aDestEnd;
	}

static inline bool CanWrite32bppValue(const char* aDest, const char* aDestEnd, int aDataSize)
	{
	const char* aDestNew = 	aDest + (aDataSize / 128) * (4 + 1) + //4 bytes data + 1 leading byte
		                            (aDataSize % 128 ? 4 + 1 : 0);
	return aDestNew <= aDestEnd;
	}
BitmapCompiler::BitmapCompiler(TSourceFile* aSourcefilenames,int aNumSources):
	iSourcefilenames(aSourcefilenames),
	iPbmSources(NULL),
	iNumSources(aNumSources)
	{}

BitmapCompiler::~BitmapCompiler()
	{
	iDestFile.close();
	if(iPbmSources)
		for(int count=0;count<iNumSources;count++)
			delete iPbmSources[count];
	delete [] iPbmSources;
	iPbmSources = NULL;
	}

int BitmapCompiler::Compile(TStoreType aSt,int aCompress,char* aDestfilename,char* aHeaderFilename,char* aPaletteFilename)
	{
	int ret = LoadPalette(aPaletteFilename);
	if (ret)
		return ret;

	iDestFile.open(aDestfilename, ios::out | ios::binary);
	if(iDestFile.is_open()==0)
		return DestFile;
	if (iNumSources < 1)
		return SourceFile;

	if (iNumSources==1 && (iSourcefilenames[0].FileName()[0]==OPTCHAR || iSourcefilenames[0].FileName()[0]==ALTERNATE_OPTCHAR) && iSourcefilenames[0].FileName()[1]=='m')
		{
		// Source is an existing MBM file, presumably because we want to convert
		// a filestore into a ROM image.
		if (aHeaderFilename)
			{
			cout << "Header file generation is not permitted with /m or -m\n";
			aHeaderFilename = NULL;
			}
		iSourcefilenames[0].FileName()+=2;
		EpocLoader pl;
		int romFormat;
		ret = pl.EpocBitmapCount(iSourcefilenames[0].FileName(), iNumSources, romFormat);
		if (!ret)
			ret = LoadPbmFile(iSourcefilenames[0].FileName());
		}
	else
		{
		// Read the usual list of [OPT]bmp_n source bitmaps.
		ret = LoadSourcefiles();
		}
	if (ret)
		return ret;

	ret = CreateHeader(aHeaderFilename);
	if (ret)
		return ret;

	if (aSt == ERomStore || aSt == ECompressedRomStore)
		ret = RomImage(aSt == ECompressedRomStore);
	else
		ret = FileImage(aCompress);
	return ret;
	};

int BitmapCompiler::AllocatePbmSourcesArray()
	{
	iPbmSources = new SEpocBitmapHeader*[iNumSources];
	if(iPbmSources == NULL)
		return NoMemory;

	memset(iPbmSources,0xff,sizeof(SEpocBitmapHeader*)*iNumSources);
	int count;
	for(count=0;count<iNumSources;count++)
		iPbmSources[count]=NULL;
	return NoError;
	}

int BitmapCompiler::LoadSourcefiles()
	{
	int ret = AllocatePbmSourcesArray();
	if (ret)
		return ret;
	for(int count=0;count<iNumSources;count++)
		{
		bool useAlpha = false;
		int bpp = 2;
		TBitmapColor color = EMonochromeBitmap;
		BitmapLoader bmpload;
		if(iSourcefilenames[count].FileName()[0]==OPTCHAR || iSourcefilenames[count].FileName()[0]==ALTERNATE_OPTCHAR)
			{
			iSourcefilenames[count].FileName()++;
			if(iSourcefilenames[count].FileName()[0]=='c')
				{
				color = EColorBitmap;
				iSourcefilenames[count].FileName()++;
				}
			bpp=iSourcefilenames[count].FileName()[0]-'0';
			iSourcefilenames[count].FileName()++;
			int next=iSourcefilenames[count].FileName()[0]-'0';
			if(next==2 || next==6 || next==4)
				{
				bpp=bpp*10+next;
				iSourcefilenames[count].FileName()++;
				}
			if(iSourcefilenames[count].FileName()[0]=='a')
				{
				useAlpha = true;
				iSourcefilenames[count].FileName()++;
				if (color!=EColorBitmap || bpp!=32)
					return Bpp;
				}
			if (color == EColorBitmap && bpp!=4 && bpp!=8 && bpp!=12 && bpp!=16 && bpp!=24 && bpp!=32)
				return Bpp;
			if (color == EMonochromeBitmap && bpp!=1 && bpp!=2 && bpp!=4 && bpp!=8)
				return Bpp;
			}
		if (color == EColorBitmap && useAlpha)
			color = EColorBitmapAlpha;
		ret = bmpload.LoadBitmap(iSourcefilenames[count],bpp,color,iPbmSources[count]);
		if (ret)
			return ret;
		}
	return NoError;
	}

int BitmapCompiler::LoadPbmFile(char* aPbmFilename)
	{
	int ret = AllocatePbmSourcesArray();
	if (ret)
		return ret;
	for(int count=0;count<iNumSources;count++)
		{
		EpocLoader pl;
		ret = pl.LoadEpocBitmap(aPbmFilename,count);
		if (!ret)
			ret = pl.DupBitmap(iPbmSources[count]);
		if (ret)
			return ret;
		}
	return NoError;
	}

int BitmapCompiler::RomImage(bool aCompress)
	{
	int count=0;

	if (aCompress)
		{
		for(; count<iNumSources; count++)
			{
			int ret = CompressBitmap(iPbmSources[count]);
			if (ret)
				return ret;
			}
		}

	int ret = WriteRomheader();
	if (ret)
		return ret;

	for(count=0;count<iNumSources;count++)
		{
		ret = WriteRombitmap(iPbmSources[count]);
		if (ret)
			return ret;
		}
	return NoError;
	}

int BitmapCompiler::FileImage(int aCompress)
	{
	int count = 0;
	if(aCompress)
		{
		for(;count<iNumSources;count++)
			{
			int ret = CompressBitmap(iPbmSources[count]);
			if(ret)
				return ret;
			}
		}
	int ret = WriteFileheader();
	if (ret)
		return ret;
	for (count=0;count<iNumSources;count++)
		{
		ret = WriteFilebitmap(iPbmSources[count]);
		if (ret)
			return ret;
		}
	ret = WriteHeadStream();
	return ret;
	}

int BitmapCompiler::WriteRomheader()
	{
	iDestFile.write((char*)&KMultiBitmapRomImageUid,4);
	iDestFile.write((char*)&iNumSources,4);
	int byteswritten=8+(iNumSources*4);
	for (int count=0;count<iNumSources;count++)
		{
		iDestFile.write((char*)&byteswritten,4);
		byteswritten+=(((iPbmSources[count]->iBitmapSize)+3)/4*4)-sizeof(SEpocBitmapHeader)+sizeof(Bitmap);
		}
	return NoError;
	}

int BitmapCompiler::WriteRombitmap(SEpocBitmapHeader* aPbm)
	{
	if (aPbm->iPaletteEntries != 0)
	    return PaletteSupportNotImplemented;

	int bitmapsize = aPbm->iBitmapSize-sizeof(SEpocBitmapHeader)+sizeof(Bitmap);
	bitmapsize = ((bitmapsize+3)/4)*4;

	char* buffer = new char[bitmapsize];
	if (buffer == NULL)
		return NoMemory;
	memset(buffer,0xff,bitmapsize);

	Bitmap* bmp = (Bitmap*)buffer;
	bmp->iUid=KCBitwiseBitmapUid;
	TBitmapColor color = aPbm->iColor;

	switch(aPbm->iBitsPerPixel)
		{ // for corresponding enums, see TDisplayMode in <gdi.h>
	case 1:
		bmp->iDispMode=1;
		break;
	case 2:
		bmp->iDispMode=2;
		break;
	case 4:
		if (color == EMonochromeBitmap)
			bmp->iDispMode=3;
		else
			bmp->iDispMode=5;
		break;
	case 8:
		if (color == EMonochromeBitmap)
			bmp->iDispMode=4;
		else
			bmp->iDispMode=6;
		break;
	case 12:
		bmp->iDispMode=10;
		break;
	case 16:
		bmp->iDispMode=7;
		break;
	case 24:
		bmp->iDispMode=8;
		break;
	case 32:
		if (color == EColorBitmapAlpha)
			bmp->iDispMode=12; // Color16MA
		else
			bmp->iDispMode=11; // Color16MU
		break;
	default:
		delete [] buffer;
		return SourceFile;
		}

	bmp->iHeap = NULL;
	bmp->iPile = NULL;
	bmp->iHeader = *aPbm;
	bmp->iByteWidth = BitmapUtils::ByteWidth(bmp->iHeader.iWidthInPixels,bmp->iHeader.iBitsPerPixel);
	bmp->iDataOffset = sizeof(Bitmap);

	CopyTail(buffer + sizeof(Bitmap), aPbm, aPbm->iBitmapSize, sizeof(SEpocBitmapHeader));
	iDestFile.write(buffer,bitmapsize);

	delete [] buffer;

	return NoError;
	}

int BitmapCompiler::WriteFileheader()
	{
	int zero=0;
	iDestFile.write((char*)&KWriteOnceFileStoreUid,4);
	iDestFile.write((char*)&KMultiBitmapFileImageUid,4);
	iDestFile.write((char*)&zero,4);
	iDestFile.write((char*)&KMultiBitmapFileImageChecksum,4);
	int byteswritten=16;
	for(int count=0;count<iNumSources;count++)
		{
		byteswritten+=iPbmSources[count]->iBitmapSize;
		}
	byteswritten+=4;
	iDestFile.write((char*)&byteswritten,4);
	return NoError;
	}

int BitmapCompiler::WriteHeadStream()
	{
	iDestFile.write((char*)&iNumSources,4);
	int byteswritten=20;
	for(int count=0;count<iNumSources;count++)
		{
		iDestFile.write((char*)&byteswritten,4);
		byteswritten+=iPbmSources[count]->iBitmapSize;
		}
	return NoError;
	}

int BitmapCompiler::WriteFilebitmap(SEpocBitmapHeader* aPbm)
	{
	if (aPbm->iPaletteEntries != 0)
		return PaletteSupportNotImplemented;

	int dataSize = aPbm->iBitmapSize - sizeof(SEpocBitmapHeader);

	iDestFile.write((char*)(aPbm),sizeof(SEpocBitmapHeader));
	// WritePalette()
	iDestFile.write(((char*)(aPbm)) + sizeof(SEpocBitmapHeader),dataSize);

	return NoError;
	}

void BitmapCompiler::WritePalette()
	{
	for (int index = 0; index < 256; index++)
		{
		iDestFile.write((char*)(&iPalette[index]),3);
		}
	}

int BitmapCompiler::LoadPalette(char* aPaletteFilename)
	{
	if (!aPaletteFilename)
		{
		iDefaultPalette = 1;
		return NoError;
		}

	iDefaultPalette = 0;
	color256Palette = iPalette;
	color256InversePalette = iInversePalette;

	struct stat fileinfo;
	if (stat(aPaletteFilename,&fileinfo)==-1)
		return CommandFile;

	int filesize = fileinfo.st_size;
	if (filesize == 0)
		return PaletteFile;

#if defined(__MSVCDOTNET__) || defined(__TOOLS2__) || defined(__CW32__)
	fstream paletteFile(aPaletteFilename, ios::in | ios::binary);
#else
	fstream paletteFile(aPaletteFilename, ios::in | ios::binary | ios::nocreate);
#endif

	if(!paletteFile.is_open())
		return PaletteFile;

	char* paletteData = new char[filesize];
	if (!paletteData)
		return NoMemory;

	memset(paletteData,0,filesize);
	paletteFile.read(paletteData,filesize);
	paletteFile.close();

	char* dataPtr = (char*)paletteData;
	char* dataPtrLimit = dataPtr + filesize;

	for (int index = 0; index < 256; index++)
		{
		char hexBuf[10];
		int ret = ReadHexString(hexBuf,dataPtr,dataPtrLimit);
		if (ret)
			{
			delete paletteData;
			return ret;
			}

		int red = HexToInt(hexBuf[8],hexBuf[9]);
		int green = HexToInt(hexBuf[6],hexBuf[7]);
		int blue = HexToInt(hexBuf[4],hexBuf[5]);

		iPalette[index] = TRgb(red,green,blue);
		}

	delete paletteData;
	CalculateInversePalette();
	return NoError;
	}

void BitmapCompiler::CalculateInversePalette()
	{
	for (int index = 0; index < 4096; index++)
		{
		TRgb color = TRgb((index & 0x00f) * 17,((index & 0x0f0) >> 4) * 17,((index & 0xf00) >> 8) * 17);

		int nearest = 0;
		int distance = iPalette[0].Difference(color);

		for (int paletteIndex = 0; paletteIndex < 256; paletteIndex++)
			{
			TRgb paletteColor = iPalette[paletteIndex];

			if (paletteColor == color)
				{
				nearest = paletteIndex;
				break;
				}

			int paletteDistance = paletteColor.Difference(color);
			if (paletteDistance < distance)
				{
				nearest = paletteIndex;
				distance = paletteDistance;
				}
			}

		iInversePalette[index] = (char)nearest;
		TRgb palColor = iPalette[nearest];
		color = palColor;
		}
	}

int BitmapCompiler::CreateHeader(char* aHeaderFilename)
	{
	if (!aHeaderFilename)
		return NoError;

	fstream hf(aHeaderFilename,ios::out);
	if(!hf.is_open())
		return DestFile;

	char* basename = strrchr(aHeaderFilename,'\\');
	if (basename==0)
		basename = aHeaderFilename;
	else
		basename++;	// skip directory separator

	char unadornedFile[256];
	strcpy(unadornedFile, UnadornedName(aHeaderFilename));

	hf << "// " << basename << "\n";
	hf << "// Generated by BitmapCompiler\n";
	hf << "// Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies). All rights reserved.\n";
	hf << "//\n\n";
	hf << "enum TMbm" << unadornedFile << "\n";
	hf << "\t{\n";

	for (int count=0;count<iNumSources;count++)
		{
        char* unadornedSourceFileName = UnadornedName(iSourcefilenames[count].FileName());
        // Strip "_mask_soft" suffix to plain "_mask"
        if( (strlen(unadornedSourceFileName)>10) && 
            (strcmp( unadornedSourceFileName + strlen(unadornedSourceFileName) - 10,
            "_mask_soft" ) == 0) )
            {
            unadornedSourceFileName[ strlen(unadornedSourceFileName) - 5 ] = '\0';
            }
		hf << "\tEMbm" << unadornedFile << unadornedSourceFileName;
		if(count<iNumSources-1)
			hf << ',';
		hf << '\n';
		}
	hf << "\t};\n";
	hf.close();
	return NoError;
	}

char* BitmapCompiler::UnadornedName(char* aName)
	{
	int len = strlen(aName);

	char* foundDir = strrchr(aName,'\\');
	char* foundUrl = strrchr(aName,'/');
	char* foundExt = strrchr(aName,'.');

	char* foundPath = (foundDir > foundUrl) ? foundDir : foundUrl;
	char* firstchar = (foundPath > (char*)0) ? (char*)(foundPath + 1) : aName;
	char* lastchar = (foundExt > firstchar) ? foundExt : aName+len;

	static char result[256];
	if (lastchar-firstchar > 255)
		{
		strcpy(result,"NameTooLong");
		return result;
		}
	int i=0;
	result[0] = (char)toupper(*firstchar);
	firstchar+=1;
	for (i=1; firstchar<lastchar; i++,firstchar++)
		{
		result[i] = (char)tolower(*firstchar);
		}
	result[i] = '\0';
	return result;
	}

int BitmapCompiler::CompressBitmap(SEpocBitmapHeader*& aPbm)
	{
	int originalsize = aPbm->iBitmapSize-sizeof(SEpocBitmapHeader);
	char* newbits=new char[originalsize+sizeof(SEpocBitmapHeader)];
	if(!newbits)
		return NoMemory;

	memset(newbits,0xff,sizeof(SEpocBitmapHeader)+originalsize);
	memcpy(newbits,aPbm,sizeof(SEpocBitmapHeader));
	char* newbitsptr=newbits+sizeof(SEpocBitmapHeader);
	char* oldbits=((char*)(aPbm))+sizeof(SEpocBitmapHeader);

	TBitmapfileCompression compression = ENoBitmapCompression;
	int ret = NoCompression;

	if (aPbm->iBitsPerPixel <= 8)
		{
		compression = EByteRLECompression;
		ret = CompressByteData(newbitsptr,oldbits,originalsize);
		}
	else if (aPbm->iBitsPerPixel == 12)
		{
		compression = ETwelveBitRLECompression;
		ret = CompressTwelveBitData(newbitsptr,oldbits,originalsize);
		}
	else if (aPbm->iBitsPerPixel == 16)
		{
		compression = ESixteenBitRLECompression;
		ret = CompressSixteenBitData(newbitsptr,oldbits,originalsize);
		}
	else if (aPbm->iBitsPerPixel == 24)
		{
		compression = ETwentyFourBitRLECompression;
		ret = CompressTwentyFourBitData(newbitsptr,oldbits,originalsize);
		}
	else if (aPbm->iBitsPerPixel == 32 && (aPbm->iColor == EColorBitmap))
		{
		compression = EThirtyTwoUBitRLECompression;
		ret = CompressThirtyTwoUBitData(newbitsptr,oldbits,originalsize);
		}
	else if (aPbm->iBitsPerPixel == 32 && (aPbm->iColor == EColorBitmapAlpha))
		{
		compression = EThirtyTwoABitRLECompression;
		ret = CompressThirtyTwoABitData(newbitsptr,oldbits,originalsize);
		}

	if(ret)
		{
		delete [] newbits;
		if(ret>0)
			return ret;
		return NoError;
		}

	delete aPbm;
	aPbm = (SEpocBitmapHeader*)newbits;
	aPbm->iBitmapSize = newbitsptr-(newbits+sizeof(SEpocBitmapHeader))+sizeof(SEpocBitmapHeader);
	aPbm->iCompression = compression;
	return NoError;
	}

int BitmapCompiler::CompressByteData(char*& aDest,char* aSrce,int aSize)
	{
	const char* destEnd = aDest + aSize;
	char* bytepointer=aSrce;
	char* limitpointer=bytepointer+aSize;
	int margin = (aSize>>6);
	char* limitmargin = limitpointer - ((margin > 4) ? margin : 4);
	char* cutoffpointer=aDest+(aSize>>1)+(aSize>>2);
	int ret=NoError;
	char* oldbytepointer=NULL;
	while(bytepointer<limitmargin)
		{
		char value=*bytepointer;
		if(*(bytepointer+1)==value && *(bytepointer+2)==value)
			{
			oldbytepointer=bytepointer;
			bytepointer+=3;
			while ( ( bytepointer < limitmargin ) && ( *bytepointer == value ) )
				bytepointer++;
			ret = WriteCompressedByteValues(aDest,value,bytepointer-oldbytepointer, destEnd);
			if(ret) return ret;
			}
		else
			{
			oldbytepointer=bytepointer;
			while((bytepointer<limitmargin) && (*(bytepointer+1)!=value || *(bytepointer+2)!=value))
				{
				bytepointer++;
				value=*bytepointer;
				}
			ret = WriteCompressedByteData(aDest,oldbytepointer,bytepointer-oldbytepointer, destEnd);
			if(ret) return ret;
			}
		if(aDest>cutoffpointer)
			return NoCompression;
		}

	int remaining = limitpointer-bytepointer;
	if(remaining > 0)
		{
		if (aDest + remaining > cutoffpointer)
			return NoCompression;
		ret=WriteCompressedByteData(aDest,bytepointer,remaining, destEnd);
		if(ret) return ret;
		}
	return NoError;
	}

int BitmapCompiler::WriteCompressedByteData(char*& aDest,char* aData,int aLength, const char* aDestEnd)
	{
	if(!CanCopy8bppData(aDest, aDestEnd, aLength))
		return NoCompression;
	while(aLength>128)
		{
		*aDest++=-128;
		for(int count=0;count<128;count++)
			*aDest++=*aData++;
		aLength-=128;
		}
	if(aLength>128 || aLength<1) return CompressionError;
	*aDest++=char(-aLength);
	for(int count=0;count<aLength;count++)
		*aDest++=*aData++;
	return NoError;
	}

int BitmapCompiler::WriteCompressedByteValues(char*& aDest,char aValue,int aLength, const char* aDestEnd)
	{
	if (aLength < 1)
		return CompressionError;

	if(!CanWrite8bppValue(aDest, aDestEnd, aLength))
		return NoCompression;

	while (aLength > 128)
		{
		*aDest++ = 127;
		*aDest++ = aValue;
		aLength -= 128;
		}

	*aDest++ = char(aLength-1);
	*aDest++ = aValue;

	return NoError;
	}

int BitmapCompiler::CompressTwelveBitData(char*& aDest,char* aSrce,int aSizeInBytes)
	{
	unsigned short* srcePtr = (unsigned short*)aSrce;
	unsigned short* srceLimitPtr = srcePtr + (aSizeInBytes / 2);
	unsigned short* dest = (unsigned short*)aDest;

	while (srcePtr < srceLimitPtr)
		{
		unsigned short* runStartPtr = srcePtr;
		unsigned short value = *srcePtr;
		do
			srcePtr++;
		while (srcePtr < srceLimitPtr && *srcePtr == value);
		WriteCompressedTwelveBitData(dest,*runStartPtr,srcePtr - runStartPtr); // Ignore error return as 12bpp compression never fails
		}

	aDest = (char*)dest;
	return NoError;
	}

int BitmapCompiler::WriteCompressedTwelveBitData(unsigned short*& aDest,unsigned short aData,int aLength)
	{
	// The run length l is stored as l-1 in the top 4 bits of each 16-bit word (between 1 and 16)
	aData &= 0x0fff;
	unsigned short maxLengthData = (unsigned short)(aData | 0xf000);

	while(aLength>16)
		{
		*aDest++ = maxLengthData;
		aLength -= 16;
		}

	if (aLength > 0)
		*aDest++ = (unsigned short)(aData | ((aLength - 1) << 12));

	return NoError;
	}

int BitmapCompiler::CompressSixteenBitData(char*& aDest,char* aSrce,int aSizeInBytes)
	{
	char* destEnd = aDest + aSizeInBytes;
	unsigned short* srcePtr = (unsigned short*)aSrce;
	unsigned short* srceLimitPtr = srcePtr + (aSizeInBytes / 2);
	unsigned short* srceLimitPtrMinusOne = srceLimitPtr - 1;
	char* destCompressedLimitPtr = aDest + (aSizeInBytes * 7 / 8);
	int ret = NoError;

	while (srcePtr < srceLimitPtrMinusOne)
		{
		unsigned short value = *srcePtr;
		unsigned short* runStartPtr = srcePtr++;

		if(*srcePtr == value)
			{
			do
				srcePtr++;
			while ( ( srcePtr < srceLimitPtr ) && ( *srcePtr == value ) );

			ret = WriteCompressedSixteenBitValues(aDest,value,srcePtr-runStartPtr, destEnd);
			if (ret)
				return ret;
			}
		else
			{
			value = *srcePtr;
			while (srcePtr < srceLimitPtrMinusOne && *(srcePtr + 1) != value)
				{
				srcePtr++;
				value = *srcePtr;
				}

			ret = WriteCompressedSixteenBitData(aDest,runStartPtr,srcePtr - runStartPtr, destEnd);
			if (ret)
				return ret;
			}
		if (aDest > destCompressedLimitPtr)
			return NoCompression;
		}

	if (srcePtr < srceLimitPtr)
		{
		ret = WriteCompressedSixteenBitData(aDest,srcePtr,srceLimitPtr - srcePtr, destEnd);
		if (ret)
			return ret;
		}

	if (aDest > destCompressedLimitPtr)
		return NoCompression;
	return NoError;
	}

int BitmapCompiler::WriteCompressedSixteenBitData(char*& aDest,unsigned short* aData,
												  int aLength, const char* aDestEnd)
	{
	if (aLength < 1)
		return CompressionError;

	if(!CanCopy16bppData(aDest, aDestEnd, aLength))
		return NoCompression;

	char* srcePtr = (char*)aData;

	while (aLength > 128)
		{
		*aDest++ = -128;
		memcpy(aDest,srcePtr,256);
		aDest += 256;
		srcePtr += 256;
		aLength -= 128;
		}

	*aDest++ = char(-aLength);

	int remainingBytes = aLength * 2;
	memcpy(aDest,srcePtr,remainingBytes);
	aDest += remainingBytes;

	return NoError;
	}

int BitmapCompiler::WriteCompressedSixteenBitValues(char*& aDest,unsigned short aValue,
													int aLength, const char* aDestEnd)
	{
	if (aLength < 1)
		return CompressionError;

	if(!CanWrite16bppValue(aDest, aDestEnd, aLength))
		return NoCompression;

	char lowByte = char(aValue);
	char highByte = char(aValue >> 8);

	while (aLength > 128)
		{
		*aDest++ = 127;
		*aDest++ = lowByte;
		*aDest++ = highByte;
		aLength -= 128;
		}

	*aDest++ = char(aLength-1);
	*aDest++ = lowByte;
	*aDest++ = highByte;

	return NoError;
	}

int BitmapCompiler::CompressTwentyFourBitData(char*& aDest,char* aSrce,int aSizeInBytes)
	{
	const char* destEnd = aDest + aSizeInBytes; 
	char* srceLimitPtr = aSrce + aSizeInBytes;
	char* srceLimitPtrMinusThree = srceLimitPtr - 3; // three bytes == one pixel
	char* destCompressedLimitPtr = aDest + (aSizeInBytes * 7 / 8);
	int ret = NoError;

	while (aSrce < srceLimitPtrMinusThree)
		{
		char* runStartPtr = aSrce;
		char component1 = *aSrce++;
		char component2 = *aSrce++;
		char component3 = *aSrce++;

		if (TrueColorPointerCompare(aSrce,component1,component2,component3))
			{
			do
				aSrce += 3;
			while (aSrce < srceLimitPtr && TrueColorPointerCompare(aSrce,component1,component2,component3));

			ret = WriteCompressedTwentyFourBitValues(aDest,component1,component2,component3,(aSrce - runStartPtr) / 3, destEnd);
			if (ret)
				return ret;
			}
		else
			{
			bool more  = true;
			bool eqRun = false;
			do
				{
				component1 = *aSrce++;
				component2 = *aSrce++;
				component3 = *aSrce++;
				more = (aSrce < srceLimitPtr);
				eqRun = more && TrueColorPointerCompare(aSrce,component1,component2,component3);
				}
			while (more && !eqRun);
			if (eqRun)
				aSrce -= 3;
			int pixelLength = (aSrce - runStartPtr) / 3;
			ret = WriteCompressedTwentyFourBitData(aDest,runStartPtr,pixelLength,destEnd);
			if (ret)
				return ret;
			}
		if (aDest > destCompressedLimitPtr)
			return NoCompression;
		}

	if (aSrce < srceLimitPtr)
		{
		ret = WriteCompressedTwentyFourBitData(aDest,aSrce,(srceLimitPtr - aSrce) / 3, destEnd);
		if (ret)
			return ret;
		}

	if (aDest > destCompressedLimitPtr)
		return NoCompression;
	return NoError;
	}

int BitmapCompiler::WriteCompressedTwentyFourBitData(char*& aDest,char* aData,
													 int aLength, const char* aDestEnd)
	{
	if (aLength < 1)
		return CompressionError;

	if(!CanCopy24bppData(aDest, aDestEnd, aLength))
		return NoCompression;

	while (aLength > 128)
		{
		*aDest++ = -128;
		memcpy(aDest,aData,384);
		aDest += 384;
		aData += 384;
		aLength -= 128;
		}

	*aDest++ = char(-aLength);

	int remainingBytes = aLength * 3;
	memcpy(aDest,aData,remainingBytes);
	aDest += remainingBytes;

	return NoError;
	}

int BitmapCompiler::WriteCompressedTwentyFourBitValues(char*& aDest,char aComponent1,
													   char aComponent2,char aComponent3,
													   int aLength, const char* aDestEnd)
	{
	if (aLength < 1)
		return CompressionError;

	if(!CanWrite24bppValue(aDest, aDestEnd, aLength))
		return NoCompression;

	while (aLength > 128)
		{
		*aDest++ = 127;
		*aDest++ = aComponent1;
		*aDest++ = aComponent2;
		*aDest++ = aComponent3;
		aLength -= 128;
		}

	*aDest++ = char(aLength-1);
	*aDest++ = aComponent1;
	*aDest++ = aComponent2;
	*aDest++ = aComponent3;

	return NoError;
	}

/** The function encodes 32-bit buffer into 24-bit stream by using RLE compression algorithm*/
int BitmapCompiler::CompressThirtyTwoUBitData(char*& aDest,char* aSrce,int aSizeInBytes)
	{
	const char* destEnd = aDest + aSizeInBytes; 
	char* srceLimitPtr = aSrce + aSizeInBytes;
	char* srceLimitPtrMinusFour = srceLimitPtr - 4; // four bytes == one pixel
	char* destCompressedLimitPtr = aDest + (aSizeInBytes * 7 / 8);
	int ret = NoError;

	while (aSrce < srceLimitPtrMinusFour)
		{
		char* runStartPtr = aSrce;
		char component1 = *aSrce++;
		char component2 = *aSrce++;
		char component3 = *aSrce++;
		aSrce++;

		if (TrueColorPointerCompare(aSrce,component1,component2,component3))
			{
			do
				aSrce += 4;
			while (aSrce < srceLimitPtr && TrueColorPointerCompare(aSrce,component1,component2,component3));

			ret = WriteCompressedThirtyTwoUBitValues(aDest,component1,component2,component3,(aSrce - runStartPtr) / 4, destEnd);
			if (ret)
				return ret;
			}
		else //non repeating pixel buffer
			{
			bool more  = true;
			bool eqRun = false;
			do {
				component1 = *aSrce++;
				component2 = *aSrce++;
				component3 = *aSrce++;
				aSrce++; //last byte is unused (no alpha component)
				more = (aSrce < srceLimitPtr);
				eqRun = more && TrueColorPointerCompare(aSrce,component1,component2,component3);
				} while (more && !eqRun);
			if (eqRun)
				aSrce -= 4;
			int pixelLength = (aSrce - runStartPtr) / 4;
			ret = WriteCompressedThirtyTwoUBitData(aDest,runStartPtr,pixelLength,destEnd);
 			if (ret)
				return ret;
			}
		if (aDest > destCompressedLimitPtr)
			return NoCompression;
		}

	if (aSrce < srceLimitPtr)
		{
		ret = WriteCompressedThirtyTwoUBitData(aDest,aSrce,(srceLimitPtr - aSrce) / 4, destEnd);
		if (ret)
			return ret;
		}

	if (aDest > destCompressedLimitPtr)
		return NoCompression;
	return NoError;
	}

/** The function copies non repeating 32-bit buffer to 24-bit compressed buffer*/
int BitmapCompiler::WriteCompressedThirtyTwoUBitData(char*& aDest,char* aData,
													 int aLength, const char* aDestEnd)
	{
	if (aLength < 1)
		return CompressionError;

	if(!CanCopy24bppData(aDest, aDestEnd, aLength))
		return NoCompression;

	while (aLength > 128)
		{
		*aDest++ = -128;
		for(int ii = 0; ii < 128; ii++)
			{
			memcpy(aDest,aData, 3);
			aDest += 3;
			aData += 4;
			}
		aLength -= 128;
		}

	*aDest++ = char(-aLength);

	while(aLength--)
		{
		memcpy(aDest,aData, 3);
		aDest += 3;
		aData += 4;
		}

	return NoError;
	}

/** The function copies repeating colour to the 24-bit compressed buffer */
int BitmapCompiler::WriteCompressedThirtyTwoUBitValues(char*& aDest,char aComponent1,
													   char aComponent2,char aComponent3,
													   int aLength, const char* aDestEnd)
	{
	if (aLength < 1)
		return CompressionError;

	if(!CanWrite24bppValue(aDest, aDestEnd, aLength))
		return NoCompression;
	while (aLength > 128)
		{
		*aDest++ = 127;
		*aDest++ = aComponent1;
		*aDest++ = aComponent2;
		*aDest++ = aComponent3;
		aLength -= 128;
		}

	*aDest++ = char(aLength-1);
	*aDest++ = aComponent1;
	*aDest++ = aComponent2;
	*aDest++ = aComponent3;

	return NoError;
	}

int BitmapCompiler::TrueColorPointerCompare(char* aColorPointer,char aComponent1,char aComponent2,char aComponent3)
	{
	return (*aColorPointer == aComponent1 && *(aColorPointer + 1) == aComponent2 && *(aColorPointer + 2) == aComponent3);
	}

/** The function encodes 32-bit buffer into 32-bit stream by using RLE compression algorithm*/
int BitmapCompiler::CompressThirtyTwoABitData(char*& aDest,char* aSrce,int aSizeInBytes)
	{
	const char* destEnd = aDest + aSizeInBytes; 
	char* srceLimitPtr = aSrce + aSizeInBytes;
	char* srceLimitPtrMinusFour = srceLimitPtr - 4; // four bytes == one pixel
	char* destCompressedLimitPtr = aDest + (aSizeInBytes * 7 / 8);
	int ret = NoError;

	while (aSrce < srceLimitPtrMinusFour)
		{
		char* runStartPtr = aSrce;
		char component1 = *aSrce++;
		char component2 = *aSrce++;
		char component3 = *aSrce++;
		char component4 = *aSrce++;

		if (ColorAlphaPointerCompare(aSrce,component1,component2,component3,component4))
			{
			do
				aSrce += 4;
			while (aSrce < srceLimitPtr && ColorAlphaPointerCompare(aSrce,component1,component2,component3,component4));
			int eqPixelLength = (aSrce - runStartPtr) / 4;
			ret = WriteCompressedThirtyTwoABitValues(aDest,component1,component2,component3,component4,eqPixelLength,destEnd);
			if (ret)
				return ret;
			}
		else //non repeating pixel buffer
			{
			bool more  = true;
			bool eqRun = false;
			do {
				component1 = *aSrce++;
				component2 = *aSrce++;
				component3 = *aSrce++;
				component4 = *aSrce++;
				more = (aSrce < srceLimitPtr);
				eqRun = more && ColorAlphaPointerCompare(aSrce,component1,component2,component3,component4);
				} while (more && !eqRun);
			if (eqRun)
				aSrce -= 4;
			int nePixelLength = (aSrce - runStartPtr) / 4;
			ret = WriteCompressedThirtyTwoABitData(aDest,runStartPtr,nePixelLength,destEnd);
			if (ret)
				return ret;
			}
		if (aDest > destCompressedLimitPtr)
			return NoCompression;
		}

	if (aSrce < srceLimitPtr)
		{
		ret = WriteCompressedThirtyTwoABitData(aDest,aSrce,(srceLimitPtr - aSrce) / 4, destEnd);
		if (ret)
			return ret;
		}

	if (aDest > destCompressedLimitPtr)
		return NoCompression;
	return NoError;
	}

/** The function copies non repeating 32-bit buffer to 32-bit compressed buffer*/
int BitmapCompiler::WriteCompressedThirtyTwoABitData(char*& aDest,char* aData,
													 int aLength, const char* aDestEnd)
	{
	if (aLength < 1)
		return CompressionError;

	if(!CanCopy32bppData(aDest, aDestEnd, aLength))
		return NoCompression;

	while (aLength > 128)
		{
		*aDest++ = -128;
		memcpy(aDest,aData,512);
		aDest += 512;
		aData += 512;
		aLength -= 128;
		}

	*aDest++ = char(-aLength);
	memcpy(aDest,aData, aLength*4);
	aDest += aLength*4;
	return NoError;
	}

/** The function copies repeating pixels including alpha to a 32-bit compressed buffer */
int BitmapCompiler::WriteCompressedThirtyTwoABitValues(char*& aDest,char aComponent1,
													   char aComponent2,char aComponent3,
													   char aComponent4,int aLength,
													   const char* aDestEnd)

	{
	if (aLength < 1)
		return CompressionError;

	if(!CanWrite32bppValue(aDest, aDestEnd, aLength))
		return NoCompression;
	while (aLength > 128)
		{
		*aDest++ = 127;
		*aDest++ = aComponent1;
		*aDest++ = aComponent2;
		*aDest++ = aComponent3;
		*aDest++ = aComponent4;
		aLength -= 128;
		}

	*aDest++ = char(aLength-1);
	*aDest++ = aComponent1;
	*aDest++ = aComponent2;
	*aDest++ = aComponent3;
	*aDest++ = aComponent4;

	return NoError;
	}

int BitmapCompiler::ColorAlphaPointerCompare(char* aColorPointer,char aComponent1,char aComponent2,char aComponent3,char aComponent4)
	{
	return (*aColorPointer == aComponent1 && *(aColorPointer + 1) == aComponent2 && *(aColorPointer + 2) == aComponent3 && *(aColorPointer + 3) == aComponent4);
	}

int BitmapCompiler::ReadHexString(char aHexBuf[10],char*& aDataPtr,char* aDataPtrLimit)
	{
	while (aDataPtr < aDataPtrLimit)
		{
		if (aDataPtr[0] == '0')
			{
			if (aDataPtr[1] == 'x' || aDataPtr[1] == 'X')
				{
				memcpy(aHexBuf,aDataPtr,10);
				aDataPtr += 10;
				return NoError;
				}
			}
		aDataPtr++;
		}

	return PaletteFile;
	}

int BitmapCompiler::HexToInt(char aHighNibble,char aLowNibble)
	{
	return (HexToInt(aHighNibble) << 4) + HexToInt(aLowNibble);
	}

int BitmapCompiler::HexToInt(char aNibble)
	{
	int value = 0;

	if (aNibble >= '0' && aNibble <= '9')
		value = aNibble - '0';
	else if (aNibble >= 'a' && aNibble <= 'f')
		value = aNibble - 'a' + 10;
	else if (aNibble >= 'A' && aNibble <= 'F')
		value = aNibble - 'A' + 10;

	return value;
	}

void BitmapCompiler::CopyTail(void* aDst, void* aSrc, int aFullSize, int aSkipped)
	{
	memcpy(aDst, ((char*)aSrc)+aSkipped, aFullSize-aSkipped);
	}



TSourceFile::TSourceFile()
: iFileName(0), iInfo(EFormatNotImportant)
	{
	}

char*& TSourceFile::FileName()
	{
	return iFileName;
	}

void TSourceFile::SetCompileInfo(TCompileInfo aInfo)
	{
	iInfo = aInfo;
	}

void TSourceFile::WriteCompileInfo(ostream& aOut)
	{
	aOut << iFileName;
	if (iInfo == ENokiaBitmap)
		aOut << " (Nokia format)";
	else if (iInfo == EThirdPartyBitmap)
		aOut << " (Third party format)";
	}

