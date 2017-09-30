/*
 * main.cpp
 *   (ここにファイルの簡易説明を記入)
 *
 *  Created on: 2015/05/24
 *      Author: wlamigo
 * 
 *   (ここにファイルの説明を記入)
 */

#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <string>
#include <cmath>
#include "tclap/CmdLine.h"
#include "sec.hpp"

#include "w2xconv.h"

#ifndef DEFAULT_MODELS_DIRECTORY
#define DEFAULT_MODELS_DIRECTORY "models_rgb"
#endif

static void
dump_procs()
{
	const W2XConvProcessor *procs;
	int num_proc;
	procs = w2xconv_get_processor_list(&num_proc);

	for (int i=0; i<num_proc; i++) {
		const W2XConvProcessor *p = &procs[i];
		const char *type;
		switch (p->type) {
		case W2XCONV_PROC_HOST:
			switch (p->sub_type) {
			case W2XCONV_PROC_HOST_AVX:
				type = "AVX";
				break;
			case W2XCONV_PROC_HOST_FMA:
				type = "FMA";
				break;
			case W2XCONV_PROC_HOST_SSE3:
				type = "SSE3";
				break;
			case W2XCONV_PROC_HOST_NEON:
				type = "NEON";
				break;
			default:
				type = "OpenCV";
				break;
			}
			break;

		case W2XCONV_PROC_CUDA:
			type = "CUDA";
			break;

		case W2XCONV_PROC_OPENCL:
			type = "OpenCL";
			break;

		default:
			type = "??";
			break;
		}

		printf("%4d: %-45s(%-10s): num_core=%d\n",
		       i,
		       p->dev_name,
		       type,
		       p->num_core);
	}
}

int main(int argc, char** argv) {
	int ret = 1;
	for (int ai=1; ai<argc; ai++) {
		if (strcmp(argv[ai], "--list-processor") == 0) {
			dump_procs();
			return 0;
		}
	}

	// definition of command line arguments
	TCLAP::CmdLine cmd("waifu2x reimplementation using OpenCV", ' ', w2xconv_version());

	TCLAP::ValueArg<std::string> cmdInputFile("i", "input_file",
			"path to input image file (you should input full path)", true, "",
			"string", cmd);

	TCLAP::ValueArg<std::string> cmdOutputFile("o", "output_file",
			"path to output image file (you should input full path)", false,
			"(auto)", "string", cmd);

	std::vector<std::string> cmdModeConstraintV;
	cmdModeConstraintV.push_back("noise");
	cmdModeConstraintV.push_back("scale");
	cmdModeConstraintV.push_back("noise_scale");
	TCLAP::ValuesConstraint<std::string> cmdModeConstraint(cmdModeConstraintV);
	TCLAP::ValueArg<std::string> cmdMode("m", "mode", "image processing mode",
			false, "noise_scale", &cmdModeConstraint, cmd);

	std::vector<int> cmdNRLConstraintV;
	cmdNRLConstraintV.push_back(1);
	cmdNRLConstraintV.push_back(2);
	cmdNRLConstraintV.push_back(3);
	TCLAP::ValuesConstraint<int> cmdNRLConstraint(cmdNRLConstraintV);
	TCLAP::ValueArg<int> cmdNRLevel("", "noise_level", "noise reduction level",
			false, 1, &cmdNRLConstraint, cmd);

	TCLAP::ValueArg<double> cmdScaleRatio("", "scale_ratio",
			"custom scale ratio", false, 2.0, "double", cmd);

	TCLAP::ValueArg<std::string> cmdModelPath("", "model_dir",
			"path to custom model directory (don't append last / )", false,
			DEFAULT_MODELS_DIRECTORY, "string", cmd);

	TCLAP::ValueArg<int> cmdNumberOfJobs("j", "jobs",
			"number of threads launching at the same time", false, 0, "integer",
			cmd);

	TCLAP::ValueArg<int> cmdTargetProcessor("", "processor",
						"set target processor", false, -1, "integer",
						cmd);

	TCLAP::SwitchArg cmdForceOpenCL("", "force-OpenCL",
					"force to use OpenCL on Intel Platform",
					cmd, false);

	TCLAP::SwitchArg cmdDisableGPU("", "disable-gpu", "disable GPU", cmd, false);

	TCLAP::ValueArg<int> cmdBlockSize("", "block_size", "block size",
					  false, 0, "integer", cmd);
	TCLAP::SwitchArg cmdListProcessor("", "list-processor", "dump processor list", cmd, false);

	// definition of command line argument : end

	// parse command line arguments
	try {
		cmd.parse(argc, argv);
	} catch (std::exception &e) {
		std::cerr << e.what() << std::endl;
		std::cerr << "Error : cmd.parse() threw exception" << std::endl;
		std::exit(-1);
	}

	if (cmdListProcessor.getValue()) {
		dump_procs();
	}

	std::string outputFileName = cmdOutputFile.getValue();
	if (outputFileName == "(auto)") {
		outputFileName = cmdInputFile.getValue();
		int tailDot = outputFileName.find_last_of('.');
		if(tailDot != std::string::npos)
			outputFileName.erase(tailDot, outputFileName.length());
		outputFileName = outputFileName + "(" + cmdMode.getValue() + ")";
		std::string &mode = cmdMode.getValue();
		if(mode.find("noise") != mode.npos){
			outputFileName = outputFileName + "(Level" + std::to_string(cmdNRLevel.getValue())
			+ ")";
		}
		if(mode.find("scale") != mode.npos){
			outputFileName = outputFileName + "(x" + std::to_string(cmdScaleRatio.getValue())
			+ ")";
		}
		outputFileName += ".png";
	}
	else if (outputFileName.find_last_of('.') == std::string::npos)
		outputFileName += ".png";

	enum W2XConvGPUMode gpu = W2XCONV_GPU_AUTO;

	if (cmdDisableGPU.getValue()) {
		gpu = W2XCONV_GPU_DISABLE;
	} else if (cmdForceOpenCL.getValue()) {
		gpu = W2XCONV_GPU_FORCE_OPENCL;
	}

	W2XConv *converter;
	int num_proc;
	w2xconv_get_processor_list(&num_proc);
	int proc = cmdTargetProcessor.getValue();

	if (proc != -1 && proc < num_proc) {
		converter = w2xconv_init_with_processor(proc,
							cmdNumberOfJobs.getValue(),
							1);
	} else {
		converter = w2xconv_init(gpu,
					 cmdNumberOfJobs.getValue(), 1);
	}

	double time_start = getsec();

	switch (converter->target_processor->type) {
	case W2XCONV_PROC_HOST:
		printf("CPU: %s\n",
		       converter->target_processor->dev_name);
		break;

	case W2XCONV_PROC_CUDA:
		printf("CUDA: %s\n",
		       converter->target_processor->dev_name);
		break;

	case W2XCONV_PROC_OPENCL:
		printf("OpenCL: %s\n",
		       converter->target_processor->dev_name);
		break;
	}

	int bs = cmdBlockSize.getValue();

	int r = w2xconv_load_models(converter, cmdModelPath.getValue().c_str());
	if (r < 0) {
		goto error;
	}


	{
		int nrLevel = 0;
		if (cmdMode.getValue() == "noise" || cmdMode.getValue() == "noise_scale") {
			nrLevel = cmdNRLevel.getValue();
		}

		double scaleRatio = 1;
		if (cmdMode.getValue() == "scale" || cmdMode.getValue() == "noise_scale") {
			scaleRatio = cmdScaleRatio.getValue();
		}

		r = w2xconv_convert_file(converter,
					 outputFileName.c_str(),
					 cmdInputFile.getValue().c_str(),
					 nrLevel,
					 scaleRatio, bs);
	}

	if (r < 0) {
		goto error;
	}

	{
		double time_end = getsec();

		double gflops_proc = (converter->flops.flop/(1000.0*1000.0*1000.0)) / converter->flops.filter_sec;
		double gflops_all = (converter->flops.flop/(1000.0*1000.0*1000.0)) / (time_end-time_start);

		std::cout << "process successfully done! (all:"
			  << (time_end - time_start)
			  << "[sec], " << gflops_all << "[GFLOPS], filter:"
			  << converter->flops.filter_sec
			  << "[sec], " << gflops_proc << "[GFLOPS])" << std::endl;
	}

	ret = 0;

error:
	if (ret != 0) {
		char *err = w2xconv_strerror(&converter->last_error);
		puts(err);
		w2xconv_free(err);
	}

	w2xconv_fini(converter);

	return ret;
}
