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
* Description:  Mifconv icon binary converters class.
*
*/


#include "mifconv.h"
#include "mifconv_iconbinaryconverter.h"
#include "mifconv_util.h"
#include "mifconv_exception.h"
#include "mifconv_convertermanager.h"
#include "mifconv_argumentmanager.h"
#include <errno.h>
#include <algorithm> 

const MifConvString SVGTBINENCODE_DEFAULT_PATH(EPOC_TOOLS_PATH);

/**
 *
 */
MifConvIconBinaryConverter::MifConvIconBinaryConverter()
{
}

/**
 *
 */
MifConvIconBinaryConverter::~MifConvIconBinaryConverter()
{
}

/**
 *
 */
void MifConvIconBinaryConverter::Init()
{
}

/**
 *
 */
void MifConvIconBinaryConverter::CleanupTargetFiles()
{
}

/**
 *
 */
void MifConvIconBinaryConverter::AppendFile( const MifConvSourceFile& sourcefile )
{   
	if( MifConvUtil::FileExtension( sourcefile.Filename() ) == SVG_FILE_EXTENSION &&
        MifConvArgumentManager::Instance()->BooleanValue(MifConvDisableSvgCompression) == false)
	{ 
		iSourceFiles.push_back( sourcefile );

        // Create temp directory:
        if( iTempDir.length() == 0 )
        {
            InitTempFile();
        }

        // External SVGTBINENCODE converts .svg files to .svgb files. However, .svgb files
        // shall be given to mif-converter to get them in mif-file:      
                
        // Create new string for .svgb file name: 
        MifConvString tmpFile(sourcefile.Filename());
        ConvertToBinaryFilename(tmpFile);
        MifConvString tempBinFilename(iTempDir + MifConvUtil::FilenameWithoutExtension(tmpFile) + "." + SVGB_BINARY_FILE_EXTENSION);

        // Get converters for .svgb files:
        MifConvFileConverterList& additionalConverters = MifConvConverterManager::Instance()->GetConverters(tempBinFilename);

        // Converters for .svg files:
        MifConvFileConverterList& thisFilesConverters = MifConvConverterManager::Instance()->GetConverters( sourcefile.Filename() );

        // Save temporary binary filename for later deleting:
        iTempFilenames.push_back(tempBinFilename);

        // Add temporary file to converters:
        for( MifConvFileConverterList::iterator c = additionalConverters.begin(); c != additionalConverters.end(); ++c )
        {
            // We have to make sure that we don't add same file twice to same converter. So, let's take first a list of
            // .svg file converters and compare them to the .svgb file converters. Don't add temporary file to converters
            // that are found from both of the lists.
            MifConvFileConverterList::iterator c2 = std::find(thisFilesConverters.begin(), thisFilesConverters.end(), *c );
            if( c2 == thisFilesConverters.end() )
            {
                // .svgb converter not found from .svg converters -> add temporary file to .svgb converter:
                MifConvSourceFile svgbFile(sourcefile);
                svgbFile.SetFilename(tempBinFilename);
                (*c)->AppendFile(svgbFile);                
            }            
        }
	}
}

/**
 *
 */
void MifConvIconBinaryConverter::Convert()
{    
    if( iSourceFiles.size() > 0 && MifConvArgumentManager::Instance()->BooleanValue(MifConvDisableSvgCompression) == false )
    {
	    ConvertToSvgb();
    }
}

/**
 *
 */
void MifConvIconBinaryConverter::Cleanup(bool err)
{    
	CleanupTempFiles();
	if( err )
	{
	    CleanupTargetFiles();
	}
}

/**
 *
 */
void MifConvIconBinaryConverter::ConvertToSvgb()
{    
    RunExtConverter();
}

/**
 *
 */
void MifConvIconBinaryConverter::InitTempFile()
{    
    MifConvArgumentManager* argMgr = MifConvArgumentManager::Instance();
    // Construct temp file name
    // If temp directory is given in command line arguments, use it:    
    iTempDir = MifConvUtil::DefaultTempDirectory();
    const MifConvString& tempDirArg = argMgr->StringValue(MifConvTempPathArg);
    if( tempDirArg.length() > 0 )
    {
        iTempDir = tempDirArg;
    }

    if( iTempDir.length() > 0 && iTempDir.at(iTempDir.length()-1) != DIR_SEPARATOR2 )
    {
        iTempDir.append(DIR_SEPARATOR);
    }

    // Generate new temp-filename:
    iTempDir.append(MifConvUtil::TemporaryFilename());

    // append tmp at as postfix
    // this is needed because the generated name can contain a single period '.'
    // character as the last character which is eaten away when the directory created.
    iTempDir.append(MifConvString("tmp"));

    MifConvUtil::EnsurePathExists(iTempDir);

    iTempDir.append(DIR_SEPARATOR);
}

/**
 *
 */
void MifConvIconBinaryConverter::ConvertToBinaryFilename( MifConvString& input )
{
    MifConvUtil::ReplaceChar(input, DIR_SEPARATOR2, '_');
    MifConvUtil::ReplaceChar(input, INCORRECT_DIR_SEPARATOR2, '_');
    MifConvUtil::ReplaceChar(input, ':', '_');
    MifConvUtil::ReplaceChar(input, ' ', '_');
}

/**
 *
 */
void MifConvIconBinaryConverter::RunExtConverter()
{      
    MifConvArgumentManager* argMgr = MifConvArgumentManager::Instance();
   
    // Build svgtbinencode command    
    MifConvString extConverterCommand("\""); // Open the " mark
    MifConvString versionArgument;
    MifConvString sourceArgument;    
        
    const MifConvString& extConverterPath = argMgr->StringValue(MifConvSvgencodePathArg);
    const MifConvString& defaultExtConverterPath = GetDefaultExtConverterPath();
    if( extConverterPath.length() > 0 )
    {
        extConverterCommand += extConverterPath; // If the path is given, use it.
    }
    else
    {
        extConverterCommand += defaultExtConverterPath; // Use default path
    }

    // Ensure that the last char of the path is dir-separator:
    if( extConverterCommand.length() > 1 && extConverterCommand.at(extConverterCommand.length()-1) != DIR_SEPARATOR2 )
        extConverterCommand += DIR_SEPARATOR;

    // Then add SVGTBINENCODE executable call and close the " mark
    extConverterCommand += SVGTBINENCODE_EXECUTABLE_NAME + MifConvString("\" ");
   
    // If SVGTBINENCODE version is given, use it also:
    const MifConvString& extConverterVersion = argMgr->StringValue(MifConvSvgtVersionArg);
    if( extConverterVersion.length() > 0 )
    {        
        versionArgument = SVGTBINENCODE_OPTION_PREFIX +
            MifConvString(SVGTBINENCODE_VERSION_PARAMETER) + " " + extConverterVersion;
        extConverterCommand += versionArgument + " ";
    }
   
    // Run converter for each of the source files:
    for( MifConvSourceFileList::iterator i = iSourceFiles.begin(); i != iSourceFiles.end(); ++i )
    {        
        // Build temp filename by replacing dir separator and ':' chars with '_':
        MifConvString tmpFileName(i->Filename());
        ConvertToBinaryFilename(tmpFileName);

        // Copy source file to temp directory:
        MifConvString to(iTempDir + tmpFileName);        
        if( MifConvUtil::CopyFile(i->Filename(), to) == false )
        {
            THROW_ERROR_COMMON("File copy failed: " + to, MifConvString(__FILE__), __LINE__ );
        }
        iTempFilenames.push_back(to);
        // It seems that system() function does not work if the command consists of two separate parts 
        // enclosed with quotation marks. If the whole string is enclosed with quotation marks then it works...
        // For example: command '"\epoc32\tools\bmconv" "somefile"' does not work while command
        // '""\epoc32\tools\bmconv" "somefile""' does.
        cout << "Mifconvdebugging - executing" << MifConvString("\""+extConverterCommand+"\""+to+"\"\"").c_str() << endl;

        if( system(MifConvString("\""+extConverterCommand+"\""+to+"\"\"").c_str()) < 0 )
        {
            cout << "Mifconvdebugging - failed to execute" << MifConvString("\""+extConverterCommand+"\""+to+"\"\"").c_str() << endl;
            int ernro = errno;  // The error number must check straight away before any next system command
            
            MifConvString errStr("Executing SVGTBINENCODE failed");
            if( ernro )
            {
                errStr += ", system error = " + MifConvUtil::ToString(ernro);      // Possible system error.
            }            
            THROW_ERROR_COMMON(errStr, MifConvString(__FILE__), __LINE__ );
        }
    }
}

/**
 *
 */
void MifConvIconBinaryConverter::CleanupTempFiles()
{
    for( MifConvStringList::iterator i = iTempFilenames.begin(); i != iTempFilenames.end(); ++i )
    {        
        if( remove( i->c_str() ) != 0 )
        {            
            perror( "Error deleting file (svg conversion)" );
        }
    }
    if( iTempDir.length() > 0 && MifConvUtil::RemoveDirectory( iTempDir ) != 0 )
    {        
        perror( "Error deleting temporary directory (svg conversion)" );
    }
}

/**
 *
 */
const MifConvString& MifConvIconBinaryConverter::GetDefaultExtConverterPath()
{   
    if( iDefaultExtConverterPath.length() == 0 )
    {        
        // Check if the EPOCROOT is given
        MifConvString epocRoot(MifConvArgumentManager::Instance()->EpocRoot());
        if( epocRoot.length() > 0 )
        {
            iDefaultExtConverterPath += epocRoot;
        }

        // Ensure that the last char of the path is dir-separator:
        if( iDefaultExtConverterPath.length() > 0 )
        {
            if( iDefaultExtConverterPath.at(iDefaultExtConverterPath.length()-1) != DIR_SEPARATOR2 )
            {
                iDefaultExtConverterPath += DIR_SEPARATOR;
            }        
            iDefaultExtConverterPath += SVGTBINENCODE_DEFAULT_PATH;
        }
    }
        
    return iDefaultExtConverterPath;
}



