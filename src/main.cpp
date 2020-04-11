/*
* The MIT License (MIT)
* This file is part of waifu2x-converter-cpp
* 
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, ana to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
* 
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#include <cassert>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <string>
#include <cmath>
#include <deque>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

// Support ancient versions of GCC still used in stubborn distros.
#if defined(__GNUC__) && !__has_include(<filesystem>)
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include "tclap/CmdLine.h"
#include "sec.hpp"

#if defined(_WIN32) && defined(_UNICODE)
#include <Windows.h>
#pragma comment ( linker, "/entry:\"wmainCRTStartup\"" )
#endif


#include "w2xconv.h"
#include "tstring.hpp"

#ifndef DEFAULT_MODELS_DIRECTORY
#define DEFAULT_MODELS_DIRECTORY "models_rgb"
#define DEFAULT_MODELS_DIRECTORYW L"models_rgb"
#endif

#ifdef HAVE_OPENCV
#include <opencv2/opencv.hpp>
#endif


class CustomFailOutput : public TCLAP::StdOutput
{
	public:
		virtual void failure( TCLAP::CmdLineInterface& _cmd, TCLAP::ArgException& e ) override
		{
			std::string progName = _cmd.getProgramName();

			std::cerr << "PARSE ERROR: " << e.argId() << std::endl
					  << "			 " << e.error() << std::endl << std::endl;

			if (_cmd.hasHelpAndVersion())
			{
				std::cerr << "Brief USAGE: " << std::endl;

				_shortUsage( _cmd, std::cerr );	

				std::cerr << std::endl
					<< "For complete USAGE and HELP type: " 
					<< std::endl << "   " << progName << " --help" 
					<< std::endl << std::endl;

				std::cerr << "Waifu2x OpenCV - Version " << GIT_TAG << " (" << GIT_COMMIT_HASH << ") - https://github.com/DeadSix27/waifu2x-converter-cpp" << std::endl << std::endl;
				std::cerr << "If you find issues or need help, visit: https://github.com/DeadSix27/waifu2x-converter-cpp/issues" << std::endl << std::endl;
			}
			else
			{
				usage(_cmd);
			}

			throw TCLAP::ExitException(1);
		}
};

static void dump_procs()
{
	const W2XConvProcessor *procs;
	size_t num_proc;
	procs = w2xconv_get_processor_list(&num_proc);

	for (int i = 0; i < num_proc; i++)
	{
		const W2XConvProcessor *p = &procs[i];
		const char *type;
		switch (p->type)
		{
			case W2XCONV_PROC_HOST:
			{
				switch (p->sub_type)
				{
					case W2XCONV_PROC_HOST_AVX:
					{
						type = "AVX";
						break;
					}
					case W2XCONV_PROC_HOST_FMA:
					{
						type = "FMA";
						break;
					}
					case W2XCONV_PROC_HOST_SSE3:
					{
						type = "SSE3";
						break;
					}
					case W2XCONV_PROC_HOST_NEON:
					{
						type = "NEON";
						break;
					}
					default:
					{
						type = "OpenCV";
						break;
					}
				}
				break;
			}
			case W2XCONV_PROC_CUDA:
			{
				type = "CUDA";
				break;
			}
			case W2XCONV_PROC_OPENCL:
			{
				type = "OpenCL";
				break;
			}
			default:
			{
				type = "??";
				break;
			}
		}

		printf("%4d: %-45s(%-10s): num_core=%d\n", i, p->dev_name, type, p->num_core);
	}
}

void check_for_errors(W2XConv* converter, int error)
{
	if (error)
	{
		char *err = w2xconv_strerror(&converter->last_error);
		std::string errorMessage(err);
		w2xconv_free(err);
		throw std::runtime_error(errorMessage);
	}
}



std::map<std::string,bool> supported_formats =
{
	// Windows Bitmaps
	{"BMP",  true},
	{"DIB",  true},
	
	// JPEG Files
	{"JPEG", true},
	{"JPG",  true},
	{"JPE",  true},
	
	// JPEG 2000 Files
	{"JP2",  false},
	
	// Portable Network Graphics
	{"PNG",  true},
	
	// WebP
	{"WEBP", true},
	
	// Portable Image Format
	{"PBM",  true},
	{"PGM",  true},
	{"PPM",  true},
	{"PXM",  true},
	{"PNM",  true},
	
	// Sun Rasters
	{"SR",   true},
	{"RAS",  true},
	
	// TIFF Files
	{"TIF",  true},
	{"TIFF", true},
	
	// OpenEXR Image Files
	{"EXR",  true},
	
	// Radiance HDR
	{"HDR",  true},
	{"PIC",  true}
};

bool validate_format_extension(std::string ext)
{
	if(ext.length() == 0)
		return false;
	
	if(ext.at(0) == '.')
		ext=ext.substr(1);
	
	std::transform(ext.begin(), ext.end(), ext.begin(), toupper);
	
	auto index = supported_formats.find(ext);
	if (index != supported_formats.end())
	{
		return index->second;
	}
	return false;
}

#if defined(_WIN32) && defined(_UNICODE)
bool validate_format_extension(std::wstring ext_w)
{
	if(ext_w.length() == 0)
		return false;
	
	if(ext_w.at(0) == L'.')
		ext_w=ext_w.substr(1);
	
	std::string ext = wstr2str(ext_w);
	return validate_format_extension(ext);
}
#endif


#if defined(HAVE_OPENCV) && defined(HAVE_OPENCV_3_X)
	#define w2xHaveImageWriter(x) cvHaveImageWriter(x)
#else
	#define w2xHaveImageWriter(x) cv::haveImageWriter(x)
#endif
	
//check for supported formats
void check_supported_formats()
{
	#ifndef HAVE_OPENCV
		// Only default formats are supported
		return;
	#else
	// Portable Network Graphics
	if (!w2xHaveImageWriter(".png"))
	{
		supported_formats["PNG"] = false;
	}
	
	// JPEG Files
	if (!w2xHaveImageWriter(".jpg"))
	{
		supported_formats["JPEG"] = false;
		supported_formats["JPG"] = false;
		supported_formats["JPE"] = false;
	}
	
	/* 
	Disabled due to vulnerabilities in Jasper codec, see: https://github.com/opencv/opencv/issues/14058
	// JPEG 2000 Files
	if (!w2xHaveImageWriter(".jp2"))
	{
		supported_formats["JP2"] = false;
	}
	*/
	
	// WebP Files
	if (!w2xHaveImageWriter(".webp"))
	{
		supported_formats["WEBP"] = false;
	}
	
	// TIFF Files
	if (!w2xHaveImageWriter(".tif"))
	{
		supported_formats["TIF"] = false;
		supported_formats["TIFF"] = false;
	}
	
	// OpenEXR Image Files
	if (!w2xHaveImageWriter(".exr"))
	{
		supported_formats["EXR"] = false;
	}

	/* These formats are always available.
	// Windows Bitmaps (Always Supported)
	supported_formats["BMP"] = true;
	supported_formats["DIB"] = true;
	
	// Portable Image Format (Always Supported)
	supported_formats["PBM"] = true;
	supported_formats["PGM"] = true;
	supported_formats["PPM"] = true;
	supported_formats["PXM"] = true;
	supported_formats["PNM"] = true;
	
	// Sun Rasters (Always Supported)
	supported_formats["SR"] = true;
	supported_formats["RAS"] = true;
	
	// Radiance HDR (Always Supported)
	supported_formats["HDR"] = true;
	supported_formats["PIC"] = true;
	*/
	#endif
}

void display_supported_formats()
{
	std::cout
	#ifdef HAVE_OPENCV
			 << " [With OpenCV] This list depends on which formats opencv has been built with."
	#else
			 << " [Without OpenCV] Only the default formats can be used (recompile to use OpenCV)."
	#endif
			 << std::endl;
			 
	for (auto const& x : supported_formats)
	{
		std::cout << "\t" << std::setw(4) << x.first << " -> " << (x.second ? "Yes" : "No") << std::endl;
	}
}

// convert mode
#define CONV_NONE 0
#define CONV_NOISE 1
#define CONV_SCALE 2
#define CONV_NOISE_SCALE 3

// output option
#define OUTPUT_NORMAL 0
#define OUTPUT_RECURSIVE 1
#define OUTPUT_SUBDIR 2

struct ConvInfo {
	int convMode;
	int NRLevel;
	double scaleRatio;
	int blockSize;
	W2XConv* converter;
	int* imwrite_params;
	_tstring postfix;
	_tstring origPath;
	_tstring outputFormat;
	int outputOption;
	ConvInfo(
		int convMode,
		int NRLevel,
		double scaleRatio,
		int blockSize,
		W2XConv* converter,
		int* imwrite_params,
		_tstring origPath,
		_tstring outputFormat,
		int outputOption
	):
		convMode(convMode),
		NRLevel(NRLevel),
		scaleRatio(scaleRatio),
		blockSize(blockSize),
		converter(converter),
		imwrite_params(imwrite_params),
		origPath(origPath),
		outputFormat(outputFormat),
		outputOption(outputOption) {
			postfix = _T("_");
			
			if (convMode & CONV_NOISE)
			{
				postfix = postfix + _T("[L") + std::_to_tstring(NRLevel) + _T("]");
			}
			if (convMode & CONV_SCALE)
			{
				_tstringstream tss;
				tss << _T("[x") << std::fixed << std::setprecision(2) << scaleRatio << _T("]");
				postfix = postfix + tss.str();
			}
			
			if (converter->tta_mode)
			{
				postfix = postfix + _T("[T]");
			}
		};
};


_tstring generate_output_location(
	const _tstring origPath,
	const _tstring inputFileName,
	_tstring outputFileName,
	const _tstring postfix,
	const _tstring outputFormat,
	int outputOption
)
{
	size_t lastSlashPos = outputFileName.find_last_of(_T("/\\"));
	size_t lastDotPos = outputFileName.find_last_of(_T('.'));

	if (_tcscmp(outputFileName.c_str(), _T("auto")) == 0)
	{
		outputFileName = inputFileName;
		
		size_t tailDot = outputFileName.find_last_of(_T('.'));
		if (tailDot != _tstring::npos)
		{
			outputFileName.erase(tailDot, outputFileName.length());
		}
		
		if (outputOption & OUTPUT_RECURSIVE)
		{
			outputFileName = outputFileName + postfix;
		}
		
		outputFileName = outputFileName + _T(".") + outputFormat;
	}	
	else if (outputFileName.back() == _T('/') || outputFileName.back() == _T('\\'))
	{
		if (outputOption & OUTPUT_SUBDIR && inputFileName.find(origPath) != _tstring::npos)
		{
			_tstring relative = inputFileName.substr(origPath.length()+1);
			outputFileName += relative.substr(0, relative.find_last_of(_T("/\\"))+1);
		}
		
		//outputFileName = output folder or "auto/"
		if (!fs::is_directory(outputFileName))
		{
			fs::create_directories(outputFileName);
		}
		
		//We pass tmp into generate_output_location because we will use the default way of naming processed files.
		//We will remove everything, in the tmp string, prior to the last slash to get the filename.
		//This removes all contextual information about where a file originated from if "recursive_directory" was enabled.
		_tstring tmp;
		if (outputOption & OUTPUT_RECURSIVE)
		{
			tmp = generate_output_location(origPath, inputFileName, _T("auto"), postfix, outputFormat, outputOption);
		}
		else
		{
			tmp = inputFileName;
			size_t tailDot = tmp.find_last_of(_T('.'));
			if (tailDot != _tstring::npos)
			{
				tmp.erase(tailDot, tmp.length());
			}
			tmp = tmp + _T(".") + outputFormat;
		}
	
		//tmp = full formatted output file path
		size_t lastSlash = tmp.find_last_of(_T("/\\"));
		if (lastSlash != _tstring::npos)
		{
			tmp.erase(0, lastSlash+1);
		}
		
		outputFileName += tmp;
	}
	else if (lastDotPos == _tstring::npos || lastSlashPos != _tstring::npos && lastDotPos < lastSlashPos)
	{
		//e.g. ./test.d/out needs to be changed to ./test.d/out.png
		outputFileName += _T(".") + outputFormat;
	}
	else if (lastSlashPos == _tstring::npos || lastDotPos > lastSlashPos)
	{
		//We may have a regular output file here or something went wrong.
		//outputFileName is already what it should be thus nothing needs to be done.
	}
	else
	{
		throw std::runtime_error("An unknown 'outputFileName' has been inserted into generate_output_location.");
	}
	return outputFileName;
}

void convert_file(ConvInfo info, fs::path inputName, fs::path output)
{
	//std::cout << "Operating on: " << fs::absolute(inputName).string() << std::endl;

	_tstring outputName = generate_output_location(info.origPath, fs::absolute(inputName).TSTRING_METHOD(), output.TSTRING_METHOD(), info.postfix, info.outputFormat, info.outputOption);

	int _nrLevel = -1;
	if (info.convMode & CONV_NOISE)
	{
		_nrLevel = info.NRLevel;
	}

	double _scaleRatio = 1;
	if (info.convMode & CONV_SCALE)
	{
		_scaleRatio = info.scaleRatio;
	}

	int error = w2xconv_convert_file(info.converter,
			outputName.c_str(),
			fs::absolute(inputName).TSTRING_METHOD().c_str(),
			_nrLevel,
			_scaleRatio,
			info.blockSize,
			info.imwrite_params
	);

	check_for_errors(info.converter, error);
}



#if defined(_WIN32) && defined(_UNICODE)
//CommandLineToArgvA source from: http://alter.org.ua/en/docs/win/args/
char** CommandLineToArgvA(char* CmdLine, int* _argc)
{
	char** argv;
	char*  _argv;
	size_t   len;
	int   argc;
	char   a;
	size_t   i, j;
	bool  in_QM;
	bool  in_TEXT;
	bool  in_SPACE;
	len = strlen(CmdLine);
	i = ((len+2)/2)*sizeof(void*) + sizeof(void*);
	argv = (char**)malloc(i + (len+2)*sizeof(char));
	_argv = (char*)(((unsigned char*)argv)+i);
	argc = 0;
	argv[argc] = _argv;
	in_QM = false;
	in_TEXT = false;
	in_SPACE = true;
	i = 0;
	j = 0;
	while(a = CmdLine[i])
	{
		if (in_QM)
		{
			if (a == '\"')
			{
				in_QM = false;
			}
			else
			{
				_argv[j] = a;
				j++;
			}
		}
		else
		{
			switch(a)
			{
				case '\"':
				{
					in_QM = true;
					in_TEXT = true;
					if (in_SPACE) {
						argv[argc] = _argv+j;
						argc++;
					}
					in_SPACE = false;
					break;
				}
				case ' ':
				case '\t':
				case '\n':
				case '\r':
				{
					if(in_TEXT) {
						_argv[j] = '\0';
						j++;
					}
					in_TEXT = false;
					in_SPACE = true;
					break;
				}
				default:
				{
					in_TEXT = true;
					if (in_SPACE) {
						argv[argc] = _argv+j;
						argc++;
					}
					_argv[j] = a;
					j++;
					in_SPACE = false;
					break;
				}
			}
		}
		i++;
	}
	_argv[j] = '\0';
	argv[argc] = nullptr;
	
	(*_argc) = argc;
	return argv;
}

#endif


#if defined(_WIN32) && defined(_UNICODE)
int wmain(int argc_w, WCHAR** argv_w)
#else
int main(int argc, char** argv)
#endif
{
	int ret = 1;
	_tstring modelDir;
#if defined(_WIN32) && defined(_UNICODE)
	int argc = 0;
	char **argv = CommandLineToArgvA(GetCommandLineA(), &argc);
	std::wstring inputFileName, outputFileName=L"auto";
	
	modelDir = DEFAULT_MODELS_DIRECTORYW;
	
	for (int ai = 1; ai < argc_w; ai++)
	{
		if ((wcscmp(argv_w[ai], L"-i") == 0) || (wcscmp(argv_w[ai], L"--input") == 0))
		{
			if (ai+1 < argc_w)
			{
				inputFileName = std::wstring(argv_w[ai+1]);
			}
			continue;
		}
		else if ((wcscmp(argv_w[ai], L"-o") == 0) || (wcscmp(argv_w[ai], L"--output") == 0))
		{
			if (ai+1 < argc_w)
			{
				outputFileName = std::wstring(argv_w[ai+1]);
			}
			continue;
		}
		else if (wcscmp(argv_w[ai], L"--model-dir") == 0)
		{
			if (ai+1 < argc_w)
			{
				modelDir = std::wstring(argv_w[ai+1]);
			}
			continue;
		}
		else if ((wcscmp(argv_w[ai], L"-l") == 0) || (wcscmp(argv_w[ai], L"--list-processor") == 0))
		{
			dump_procs();
			return 0;
		}
		else if ((wcscmp(argv_w[ai], L"--list-opencv-formats") == 0) || (wcscmp(argv_w[ai], L"--list-supported-formats") == 0))
		{
			check_supported_formats();
			display_supported_formats();
			return 0;
		}
	}
#else
	for (int ai = 1; ai < argc; ai++)
	{
		if ((strcmp(argv[ai], "--list-processor") == 0) || (strcmp(argv[ai], "-l") == 0))
		{
			dump_procs();
			return 0;
		}
		else if (strcmp(argv[ai], "--list-opencv-formats") == 0 || strcmp(argv[ai], "--list-supported-formats") == 0)
		{
			check_supported_formats();
			display_supported_formats();
			return 0;
		}
	}
#endif

	check_supported_formats();

	// definition of command line arguments
	TCLAP::CmdLine cmd("waifu2x OpenCV Fork - https://github.com/DeadSix27/waifu2x-converter-cpp", ' ', std::string(GIT_TAG) + " (" + GIT_BRANCH + "-" + GIT_COMMIT_HASH + ")", true);
	cmd.setOutput(new CustomFailOutput());

	TCLAP::ValueArg<std::string> cmdInput("i", "input",
		"path to input image file or directory (you should use the full path)", true, "",
		"string", cmd);

	TCLAP::ValueArg<std::string> cmdOutput("o", "output",
		"path to output image file or directory  (you should use the full path)", false,
		"auto", "string", cmd);

	TCLAP::ValueArg<bool> cmdRecursiveDirectoryIterator("r", "recursive-directory",
		"Search recursively through directories to find more images to process.\nIf this is set to 0 it will only check in the directory specified if the input is a directory instead of an image. (0 or 1)", false,
		0, "bool", cmd);

	TCLAP::ValueArg<bool> cmdAutoNaming("a", "auto-naming",
		"Add postfix to output name when output path is not specified.\nSet 0 to disable this. (0 or 1)", false,
		1, "bool", cmd);

	TCLAP::ValueArg<bool> cmdGenerateSubdir("g", "generate-subdir",
		"Generate sub folder when recursive directory is enabled.\nSet 1 to enable this. (0 or 1)", false,
		0, "bool", cmd);

	TCLAP::ValueArg<bool> cmdTTA("t", "tta", "Enable Test-Time Augmentation mode. (0 or 1)", false,
		0, "bool", cmd);

	TCLAP::SwitchArg cmdQuiet("s", "silent", "Enable silent mode. (same as --log-level 1)", cmd, false);
	
	std::vector<int> cmdLogLevelConstraintV;
	cmdLogLevelConstraintV.push_back(0);
	cmdLogLevelConstraintV.push_back(1);
	cmdLogLevelConstraintV.push_back(2);
	cmdLogLevelConstraintV.push_back(3);
	cmdLogLevelConstraintV.push_back(4);
	TCLAP::ValuesConstraint<int> cmdLogLevelConstraint(cmdLogLevelConstraintV);
	TCLAP::ValueArg<int> cmdLogLevel("v", "log-level", "Set log level", false, 3, &cmdLogLevelConstraint, cmd);
	
	std::vector<std::string> cmdModeConstraintV;
	cmdModeConstraintV.push_back("noise");
	cmdModeConstraintV.push_back("scale");
	cmdModeConstraintV.push_back("noise-scale");
	TCLAP::ValuesConstraint<std::string> cmdModeConstraint(cmdModeConstraintV);
	TCLAP::ValueArg<std::string> cmdMode("m", "mode", "image processing mode", false, "noise-scale", &cmdModeConstraint, cmd);

	std::vector<int> cmdNRLConstraintV;
	cmdNRLConstraintV.push_back(0);
	cmdNRLConstraintV.push_back(1);
	cmdNRLConstraintV.push_back(2);
	cmdNRLConstraintV.push_back(3);
	TCLAP::ValuesConstraint<int> cmdNRLConstraint(cmdNRLConstraintV);
	TCLAP::ValueArg<int> cmdNRLevel("", "noise-level", "noise reduction level",
		false, 1, &cmdNRLConstraint, cmd
	);
	
	TCLAP::ValueArg<double> cmdScaleRatio("", "scale-ratio", "custom scale ratio",
		false, 2.0, "double", cmd
	);
	
	TCLAP::ValueArg<std::string> cmdModelPath("", "model-dir", "path to custom model directory (don't append last / )",
		false, DEFAULT_MODELS_DIRECTORY, "string", cmd
	);
	
	TCLAP::ValueArg<int> cmdNumberOfJobs("j", "jobs", "number of threads launching at the same time",
		false, 0, "integer", cmd
	);
	
	TCLAP::ValueArg<int> cmdTargetProcessor("p", "processor", "set target processor",
		false, -1, "integer", cmd
	);	
	TCLAP::SwitchArg cmdForceOpenCL("", "force-OpenCL", "force to use OpenCL on Intel Platform",
		cmd, false
	);
    TCLAP::SwitchArg cmdResume("z", "resume", "Ignores files in input stream that have already been converted",
                                    cmd, false
    );
	TCLAP::SwitchArg cmdDisableGPU("", "disable-gpu", "disable GPU",
		cmd, false
	);
	TCLAP::ValueArg<int> cmdBlockSize("", "block-size", "block size",
		false, 0, "integer", cmd
	);
	TCLAP::ValueArg<int> cmdImgQuality("q", "image-quality", "JPEG & WebP Compression quality (0-101, 0 being smallest size and lowest quality), use 101 for lossless WebP",
		false, -1, "0-101", cmd
	);
	TCLAP::ValueArg<int> cmdPngCompression("c", "png-compression", "Set PNG compression level (0-9), 9 = Max compression (slowest & smallest)",
		false, 5, "0-9", cmd
	);
	TCLAP::ValueArg<std::string> cmdOutputFormat("f", "output-format", "The format used when running in recursive/folder mode\nSee --list-supported-formats for a list of supported formats/extensions.",
		false, "png", "png,jpg,webp,...", cmd
	);
	TCLAP::SwitchArg cmdListProcessor("l", "list-processor", "dump processor list",
		cmd, false
	);
	TCLAP::SwitchArg showOpenCVFormats_deprecated("", "list-opencv-formats", " (deprecated. Use --list-supported-formats) dump opencv supported format list",
		cmd, false
	);
	TCLAP::SwitchArg showOpenCVFormats("", "list-supported-formats", "dump currently supported format list",
		cmd, false
	);

	// definition of command line argument : end
	// parse command line arguments
	try
	{
		cmd.parse(argc, argv);
	}
	catch (std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		std::cerr << "Error : cmd.parse() threw exception" << std::endl;
		std::exit(-1);
	}
	
	//Check scale-ratio is vaild.
	if (cmdScaleRatio.getValue() < 0 || cmdScaleRatio.getValue() > 1024)
	{
		std::cout << "Error: Scale Ratio range is 0-1024" << std::endl;
		std::exit(-1);
	}
	
	//Check Quality/Compression option ranges.
	if (cmdPngCompression.getValue() < 0 || cmdPngCompression.getValue() > 9)
	{
		std::cout << "Error: PNG Compression level range is 0-9, 9 being the slowest and resulting in the smallest file size." << std::endl;
		std::exit(-1);
	}
	if (cmdImgQuality.getValue() < -1 || cmdImgQuality.getValue() > 101)
	{
		std::cout << "Error: JPEG & WebP Compression quality range is 0-101! (0 being smallest size and lowest quality), use 101 for lossless WebP" << std::endl;
		std::exit(-1);
	}
	if (validate_format_extension(cmdOutputFormat.getValue()) == false)
	{
		printf("Unsupported output extension: %s\nUse option --list-supported-formats to see a list of supported formats", cmdOutputFormat.getValue().c_str());
		std::exit(-1);
	}
	
	//We need to do this conversion because using a TCLAP::ValueArg<fs::path> can not handle spaces.
#if defined(_WIN32) && defined(_UNICODE)
	fs::path input = inputFileName;
	_tstring tmpOutput = outputFileName;
#else
	fs::path input = cmdInput.getValue();
	_tstring tmpOutput = cmdOutput.getValue();
	modelDir = cmdModelPath.getValue();
#endif
	if (fs::is_directory(input) && (tmpOutput.back() != _T('/')) && _tcscmp(tmpOutput.c_str(), _T("auto")) != 0)
	{
		tmpOutput += _T("/");
	}
	fs::path output = tmpOutput;
	
	enum W2XConvGPUMode gpu = W2XCONV_GPU_AUTO;

	if (cmdDisableGPU.getValue())
	{
		gpu = W2XCONV_GPU_DISABLE;
	}
	else if (cmdForceOpenCL.getValue())
	{
		gpu = W2XCONV_GPU_FORCE_OPENCL;
	}

	W2XConv *converter;
	size_t num_proc;
	w2xconv_get_processor_list(&num_proc);
	int proc = cmdTargetProcessor.getValue();
	int log_level = 3;

	if (cmdQuiet.getValue())
	{
		log_level = 1;
	}
	else
		log_level = cmdLogLevel.getValue();

	if (proc != -1 && proc < num_proc)
	{
		converter = w2xconv_init_with_processor_and_tta(proc, cmdNumberOfJobs.getValue(), log_level, cmdTTA.getValue());
	}
	else
	{
		converter = w2xconv_init_with_tta(gpu, cmdNumberOfJobs.getValue(), log_level, cmdTTA.getValue());
	}
	
	int jpeg_quality = 90;
	int webp_quality = 101;
	
	if (cmdImgQuality.getValue() != -1)
	{
		jpeg_quality = webp_quality = cmdImgQuality.getValue();
	}
	
	int imwrite_params[] =
	{
		cv::IMWRITE_WEBP_QUALITY,
		webp_quality,
		cv::IMWRITE_JPEG_QUALITY,
		jpeg_quality,
		cv::IMWRITE_PNG_COMPRESSION,
		cmdPngCompression.getValue()
	};

	_tstring outputFormat = _str2tstr(cmdOutputFormat.getValue());
	
	int convMode = CONV_NONE;
	
	if (cmdMode.getValue().find("noise") != _tstring::npos)
	{
		convMode |= CONV_NOISE;
	}
	if (cmdMode.getValue().find("scale") != _tstring::npos)
	{
		convMode |= CONV_SCALE;
	}
	
	int outputOption = cmdAutoNaming.getValue();
	bool recursive_directory_iterator = cmdRecursiveDirectoryIterator.getValue();
	
	if (fs::is_directory(input) && cmdGenerateSubdir.getValue() && recursive_directory_iterator)
	{
		outputOption |= OUTPUT_SUBDIR;
	}
	
	_tstring origPath = fs::absolute(input).TSTRING_METHOD();
	
	if(origPath.back() == _T('\\') || origPath.back() == _T('/')){
		origPath = origPath.substr(0, origPath.length()-1);
	}
	
	ConvInfo convInfo(
		convMode,
		cmdNRLevel.getValue(),
		cmdScaleRatio.getValue(),
		cmdBlockSize.getValue(),
		converter,
		imwrite_params,
		origPath,
		outputFormat,
		outputOption
	);
	
	double time_start = getsec();
	
	if (log_level >= 1)
	{
		switch (converter->target_processor->type)
		{
			case W2XCONV_PROC_HOST:
			{
				printf("CPU: %s\n", converter->target_processor->dev_name);
				break;
			}
			case W2XCONV_PROC_CUDA:
			{
				printf("CUDA: %s\n", converter->target_processor->dev_name);
				break;
			}
			case W2XCONV_PROC_OPENCL:
			{
				printf("OpenCL: %s\n", converter->target_processor->dev_name);
				break;
			}
		}
	}

	int error = w2xconv_load_models(converter, modelDir.c_str());
	check_for_errors(converter, error);

	//This includes errored files.
	int numFilesProcessed = 0;
	int numErrors = 0;
	int numSkipped = 0;
	int numIgnored = 0;
	
	//Build files list
	std::deque<fs::path> files_list;
	
	if (fs::is_directory(input))
	{
		if (log_level >= 1)
		{
			std::cout << "We're going to be operating in a directory. dir:" << fs::absolute(input).string() << std::endl;
		}
		
		if (recursive_directory_iterator)
		{
			for (auto & inputFile : fs::recursive_directory_iterator(input))
			{
				if (!fs::is_directory(inputFile))
				{
					std::string ext = inputFile.path().extension().string();
					if (validate_format_extension(ext))
					{
						files_list.push_back(inputFile);
					}
					else
					{
						if (log_level >= 1)
						{
							std::cout << "Skipping file '" << inputFile.path().filename().string()
								<< "' for having an unsupported file extension (" << ext << ")" << std::endl;
						}
						numSkipped++;
						continue;
					}
				}
			}
		}
		else
		{
			for (auto & inputFile : fs::directory_iterator(input))
			{
				if (!fs::is_directory(inputFile))
				{
					std::string ext = inputFile.path().extension().string();
					if (validate_format_extension(ext))
					{
						files_list.push_back(inputFile);
					}
					else
					{
						if (log_level >= 1)
						{
							std::cout << "Skipping file '" << inputFile.path().filename().string()
								<< "' for having an unsupported file extension (" << ext << ")" << std::endl;
						}
						numSkipped++;
						continue;
					}
				}
			}
		}
	}
	else
		files_list.push_back(input);

	//Proceed by list
	double timeAvg = 0.0;
	int files_count = static_cast<int>(files_list.size());
	for (auto &fn : files_list)
	{
		++numFilesProcessed;
        _tstring outputName = generate_output_location(convInfo.origPath, fs::absolute(fn).TSTRING_METHOD(), output.TSTRING_METHOD(), convInfo.postfix, convInfo.outputFormat, convInfo.outputOption);
        if(cmdResume.getValue() && fs::exists(outputName)){
            if (log_level >= 1) {
                printf("Ignored %s\n", fn.c_str());
            }
            numIgnored++;
            continue;
        }
        double time_file_start = getsec();
			
		if (log_level >= 1)
		{
			std::string file_path = fs::absolute(fn).string();
			if(fs::is_directory(input)){
				std::string orig_path = fs::absolute(input).string();
				if (file_path.find(orig_path) != _tstring::npos)
				{
					file_path = file_path.substr(origPath.length()+1);
				}
			}
			
			printf("Processing file [%d/%d] \"%s\":%s",
				numFilesProcessed,
				files_count,
				file_path.c_str(),
				(log_level >= 2 ? "\n" : " ")
			);
		}

		try
		{
            convert_file(convInfo, fn, output);
		}
		catch (const std::exception& e)
		{
			numErrors++;
			std::cout << e.what() << std::endl;
		}

		if (log_level >= 1)
		{
			//Calculate and out elapsed time
			double time_end = getsec();
			double time_file = time_end - time_file_start;
			double time_all = time_end - time_start;
			if (timeAvg > 0.0)
			{
				timeAvg = time_all / numFilesProcessed;
			}
			else
			{
				timeAvg = time_all;
			}
		
			double elapsed = files_count * timeAvg - time_all;
			int el_h = (int) elapsed / (60 * 60);
			int el_m = (int) (elapsed - el_h * 60 * 60) / 60;
			int el_s = (int) (elapsed - el_h * 60 * 60 - el_m * 60);
			printf("Done, took: ");
			if (el_h)
			{
				printf("%dh", el_h);
			}
			if (el_m)
			{
				printf("%dm", el_h);
			}
			printf("%ds total, file: %.3fs avg: %.3fs\n", el_s, time_file, timeAvg);
		}
	}

	if (log_level >= 1)
	{
		double time_end = getsec();

		double gflops_proc = (converter->flops.flop / (1000.0*1000.0*1000.0)) / converter->flops.filter_sec;
		double gflops_all = (converter->flops.flop / (1000.0*1000.0*1000.0)) / (time_end - time_start);

		printf("Finished processing %d files%s%.3fsecs total, filter: %.3fsecs; %d files ignored, %d files skipped, %d files errored. [GFLOPS: %7.2f, GFLOPS-Filter: %7.2f]\n",
			numFilesProcessed,
			(log_level >=2 ? "\nTook: " : ", took: "),
			(time_end - time_start),
			converter->flops.filter_sec,
			numIgnored,
			numSkipped,
			numErrors,
			gflops_all,
			gflops_proc
		);
	}

	w2xconv_fini(converter);

#if defined(_WIN32) && defined(_UNICODE)
	free(argv);
#endif

	return 0;
}
