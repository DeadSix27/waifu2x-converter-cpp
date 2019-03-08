#define CLLIB_EXTERN

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <libgen.h>
#include <sys/stat.h>
#endif
#include <vector>
#include <stdio.h>
#include <string>
#include <string.h>
#include "filters.hpp"
#include "sec.hpp"
#include "CLlib.h"
#include "params.h"
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include "CL/cl.h"
#endif
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;



#ifdef __linux
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

static const char prog[] =
#include "modelHandler_OpenCL.cl.h"
;

#define S_(a) #a
#define S(a) S_(a)

namespace w2xc {


#ifdef _WIN32
	static HMODULE handle;
#else
	static void *handle;
#endif

	struct OpenCLDevListEntry {
		cl_platform_id plt_id;
		cl_device_id dev;
	};

	static std::vector<OpenCLDevListEntry> dev_list;

	void
		initOpenCLGlobal(std::vector<W2XConvProcessor> *proc_list)
	{
#ifdef _WIN32
		handle = LoadLibraryA("OpenCL.dll");
#elif defined __APPLE__
		handle = dlopen("/System/Library/Frameworks/OpenCL.framework/Versions/A/OpenCL", RTLD_LAZY);
#define GetProcAddress dlsym
#define FreeLibrary dlclose

#else
        handle = dlopen("libOpenCL.so.2.0.0", RTLD_LAZY);
        if (handle == nullptr) {
			handle = dlopen("libOpenCL.so.1", RTLD_LAZY);
        }
		if (handle == nullptr) {
			handle = dlopen("libOpenCL.so.1.0.0", RTLD_LAZY);
        }
		if (handle == nullptr) {
			handle = dlopen("libOpenCL.so", RTLD_LAZY);
        }
        if (handle == nullptr) {
			handle = dlopen("/system/vendor/lib/libOpenCL.so", RTLD_LAZY);
        }
        if (handle == nullptr) {
			handle = dlopen("/system/vendor/lib/libOpenCL.so", RTLD_LAZY);
        }
		if (handle == nullptr) {
			handle = dlopen("/system/vendor/lib/libPVROCL.so", RTLD_LAZY);
		}

#define GetProcAddress dlsym
#define FreeLibrary dlclose

#endif

		if (!handle) {
			printf("No openCL handle found, is libOpenCL installed\n");
			return;
		}

#define LOAD(name)                              \
        p_##name = (decltype(p_##name)) GetProcAddress(handle, #name); \
        if (p_##name == NULL) {                 \
                FreeLibrary(handle);            \
                handle = NULL;                  \
                return;                         \
        }

		LOAD(clGetDeviceInfo);
		LOAD(clGetPlatformIDs);
		LOAD(clGetDeviceIDs);
		LOAD(clGetPlatformInfo);
		LOAD(clCreateProgramWithSource);
		LOAD(clCreateProgramWithBinary);
		LOAD(clBuildProgram);
		LOAD(clGetProgramBuildInfo);
		LOAD(clGetProgramInfo);
		LOAD(clReleaseProgram);
		LOAD(clCreateKernel);
		LOAD(clCreateBuffer);
		LOAD(clEnqueueWriteBuffer);
		LOAD(clFlush);
		LOAD(clReleaseMemObject);
		LOAD(clEnqueueReadBuffer);
		LOAD(clFinish);
		LOAD(clEnqueueNDRangeKernel);
		LOAD(clReleaseKernel);
		LOAD(clSetKernelArg);
		LOAD(clCreateCommandQueue);
		LOAD(clCreateContext);
		LOAD(clReleaseCommandQueue);
		LOAD(clReleaseContext);
		LOAD(clWaitForEvents);
		LOAD(clReleaseEvent);

		cl_platform_id plts[16];
		cl_uint num_plt;
		clGetPlatformIDs(16, plts, &num_plt);

		struct OpenCLDevListEntry entry;
		struct W2XConvProcessor proc;


		int cur_dev_id = 0;
		proc.type = W2XCONV_PROC_OPENCL;

		for (unsigned int i = 0; i < num_plt; i++) {
			size_t sz;
			clGetPlatformInfo(plts[i], CL_PLATFORM_NAME, 0, nullptr, &sz);
			std::vector<char> name(sz);
			clGetPlatformInfo(plts[i], CL_PLATFORM_NAME, sz, &name[0], &sz);

			bool is_amd = strstr(&name[0], "AMD") != NULL;
			bool is_apple = strstr(&name[0], "Apple") != NULL;
			bool is_intel = strstr(&name[0], "Intel") != NULL;
			bool is_nvidia = strstr(&name[0], "NVIDIA") != NULL;

			cl_uint num_dev;
			cl_int ret = clGetDeviceIDs(plts[i], CL_DEVICE_TYPE_ALL, 0, nullptr, &num_dev);
			if ((num_dev == 0) || (ret != CL_SUCCESS)) {
				continue;
			}

			std::vector<cl_device_id> devs(num_dev);
			clGetDeviceIDs(plts[i], CL_DEVICE_TYPE_ALL, num_dev, &devs[0], &num_dev);

			for (unsigned int di = 0; di < num_dev; di++) {
				cl_device_id dev = devs[di];
				cl_device_type dtype;

				clGetDeviceInfo(dev, CL_DEVICE_TYPE, sizeof(dtype), &dtype, NULL);
				int sub_type = 0;

				if (is_amd) {
					sub_type = W2XCONV_PROC_OPENCL_PLATFORM_AMD;
				}
				else if (is_nvidia) {
					sub_type = W2XCONV_PROC_OPENCL_PLATFORM_NVIDIA;
				}
				else if (is_intel) {
					sub_type = W2XCONV_PROC_OPENCL_PLATFORM_INTEL;
				}
				else {
					sub_type = W2XCONV_PROC_OPENCL_PLATFORM_UNKNOWN;
				}

				if (dtype == CL_DEVICE_TYPE_GPU) {
					sub_type |= W2XCONV_PROC_OPENCL_DEVICE_GPU;
				}
				else if (dtype == CL_DEVICE_TYPE_CPU) {
					sub_type |= W2XCONV_PROC_OPENCL_DEVICE_CPU;
				}
				else {
					sub_type |= W2XCONV_PROC_OPENCL_DEVICE_UNKNOWN;
				}

				proc.sub_type = sub_type;

				size_t dev_name_len;
				clGetDeviceInfo(dev, CL_DEVICE_NAME, 0, nullptr, &dev_name_len);
				std::vector<char> dev_name(dev_name_len + 1);
				clGetDeviceInfo(dev, CL_DEVICE_NAME, dev_name_len, &dev_name[0], &dev_name_len);
				
				proc.dev_name = strdup(&dev_name[0]);
				proc.dev_id = cur_dev_id++;

				cl_uint num_cu;
				clGetDeviceInfo(dev, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(num_cu), &num_cu, nullptr);

				proc.num_core = num_cu;

				proc_list->push_back(proc);
				entry.plt_id = plts[i];
				entry.dev = dev;
				dev_list.push_back(entry);
			}
		}

		return;
	}

	static void
		setCLError(W2XConv *c,
			int dev_id,
			int error_code)
	{
		clearError(c);
		c->last_error.code = W2XCONV_ERROR_OPENCL;
		c->last_error.u.cl_error.dev_id = dev_id;
		c->last_error.u.cl_error.error_code = error_code;
	}

	void removeForbiddenChar(std::string* s)
	{
		std::string::iterator it;
		std::string illegalChars = "\\/:?\"<>|, ";
		for (it = s->begin(); it < s->end(); ++it) {
			bool found = illegalChars.find(*it) != std::string::npos;
			if (found) {
				*it = '_';
			}
		}
	}

	bool
		initOpenCL(W2XConv *c, ComputeEnv *env, W2XConvProcessor *proc)
	{
		int dev_id = proc->dev_id;
		env->num_cl_dev = 1;
		env->cl_dev_list = new OpenCLDev[1];
		const OpenCLDevListEntry *de = &dev_list[dev_id];
		cl_int err;
		cl_device_id dev = de->dev;
		cl_context_properties props[] =
		{ CL_CONTEXT_PLATFORM, (cl_context_properties)(de->plt_id), 0 };
		cl_context context = clCreateContext(props, 1, &dev, NULL, NULL, &err);
		if (err != CL_SUCCESS) {
			setCLError(c, dev_id, err);
			return false;
		}

		if (proc->sub_type == W2XCONV_PROC_OPENCL_INTEL_GPU) {
			env->pref_block_size = 256;
		}

		cl_command_queue queue;
		cl_kernel ker_filter, ker_filter_in1_out32, ker_filter_in128_out1;
		cl_kernel ker_filter_in3_out32, ker_filter_in128_out3;
		cl_program program = 0;

		const char *dev_name = proc->dev_name;
		bool bin_avaiable = false;

#if ((defined __linux) && !(defined __ANDROID__)) || _WIN32
#define GENERATE_BINARY
#endif


#ifdef GENERATE_BINARY
#ifdef __linux
		ssize_t path_len = 4;
		char *self_path = (char*)malloc(path_len + 1);
		while (1) {
			ssize_t r = readlink("/proc/self/exe", self_path, path_len);
			if (r < path_len) {
				self_path[r] = '\0';
				break;
			}

			path_len *= 2;
			self_path = (char*)realloc(self_path, path_len + 1);
		}

		struct stat self_st;
		stat(self_path, &self_st);
		self_path = dirname(self_path);
#else
		size_t path_len = 4;
		char *self_path = (char*)malloc(path_len + 1);
		DWORD len;
		while (1) {
			len = GetModuleFileNameA(NULL, self_path, (DWORD) path_len);
			if (len > 0 && len != path_len) {
				break;
			}

			path_len *= 2;
			self_path = (char*)realloc(self_path, path_len + 1);
		}
		WIN32_FIND_DATAA self_st;
		HANDLE finder = FindFirstFileA(self_path, &self_st);
		FindClose(finder);

		for (int si = len - 1; si >= 0; si--) {
			if (self_path[si] == '\\') {
				self_path[si] = '\0';
				break;
			}
		}
#endif

		std::string dev_nameStr = &dev_name[0];
		removeForbiddenChar(&dev_nameStr);
		
		std::string bin_path = std::string(self_path) + "/" + dev_nameStr + ".bin";

		FILE *binfp = fopen(bin_path.c_str(), "rb");
		if (binfp) {
#if (defined __linux)
			struct stat bin_st;
			stat(bin_path.c_str(), &bin_st);

			bool old = false;
			if (bin_st.st_mtim.tv_sec < self_st.st_mtim.tv_sec) {
				old = true;
			}

			if (bin_st.st_mtim.tv_sec == self_st.st_mtim.tv_sec) {
				if (bin_st.st_mtim.tv_nsec < self_st.st_mtim.tv_nsec) {
					old = true;
				}
			}
			size_t bin_sz = bin_st.st_size;
#else
			WIN32_FIND_DATAA bin_st;
			HANDLE finder = FindFirstFileA(bin_path.c_str(), &bin_st);
			FindClose(finder);

			bool old = false;
			uint64_t self_time = (((uint64_t)self_st.ftLastWriteTime.dwHighDateTime) << 32) |
				((uint64_t)self_st.ftLastWriteTime.dwLowDateTime);
			uint64_t bin_time = (((uint64_t)bin_st.ftLastWriteTime.dwHighDateTime) << 32) |
				((uint64_t)bin_st.ftLastWriteTime.dwLowDateTime);

			if (bin_time < self_time) {
				old = true;
			}

			size_t bin_sz = bin_st.nFileSizeLow;
#endif

			if (!old) {
				unsigned char *bin = (unsigned char*)malloc(bin_sz);

				size_t rem = bin_sz;
				unsigned char *p = bin;
				while (rem) {
					size_t rsz = fread(p, 1, rem, binfp);
					if (rsz <= 0) {
						break;
					}

					rem -= rsz;
					p += rsz;
				}

				if (rem == 0) {
					cl_int err;
					program = clCreateProgramWithBinary(context, 1, &dev, &bin_sz,
						(const unsigned char**)&bin, NULL, &err);

					if (err == CL_SUCCESS) {
						bin_avaiable = true;
					}
				}

				free(bin);
			}

			fclose(binfp);
		}
#endif

		if (!bin_avaiable) {
			const char *source[1] = { prog };
			size_t src_len[1] = { sizeof(prog) - 1 };

			program = clCreateProgramWithSource(context, 1, source, src_len, &err);
			if (err != CL_SUCCESS) {
				clReleaseContext(context);
				setCLError(c, dev_id, err);
				return false;
			}

		}

#ifdef GENERATE_BINARY
		free(self_path);
#endif

		err = clBuildProgram(program, 1, &dev, "", nullptr, nullptr);
		if (err != CL_SUCCESS) {
			size_t log_len;
			clGetProgramBuildInfo(program, dev, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_len);

			std::vector<char> log(log_len + 1);
			clGetProgramBuildInfo(program, dev, CL_PROGRAM_BUILD_LOG, log_len, &log[0], &log_len);
			log[log_len] = '\0';

			puts(&log[0]);

			clReleaseProgram(program);
			clReleaseContext(context);
			setCLError(c, dev_id, err);
			return false;
		}



#ifdef GENERATE_BINARY
		if (!bin_avaiable) {
			size_t binsz;
			size_t ret_len;
			clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES, sizeof(binsz), &binsz, &ret_len);

			char *buffer = new char[binsz];
			char *ptrs[1];
			ptrs[0] = buffer;

			clGetProgramInfo(program, CL_PROGRAM_BINARIES, sizeof(ptrs), ptrs, &ret_len);
			
			FILE *fp = NULL;
			while (fp == NULL)
			{
				fp = fopen(bin_path.c_str(), "wb");
				if (fp == NULL) {
					#if (defined __linux)
						if (errno == 13) {
							std::string user_folder("/tmp/.waifu2x");
							char *home_dir = getenv ("HOME");
							if (home_dir != NULL) {
								user_folder = std::string(home_dir) + "/.waifu2x";
							}
							
							if (!fs::exists(user_folder)) {
								try { fs::create_directory(user_folder); }
								catch (fs::filesystem_error& e) {
									printf("ERROR: %s\n",e.what());
									exit(EXIT_FAILURE);
								}
							}
							bin_path = user_folder + "/" + dev_nameStr + ".bin";
							fp = fopen(bin_path.c_str(), "wb");
							printf("Writing OpenCL-Binary to: %s\n",bin_path.c_str());
						}
						else {
							printf("Error opening file %s: [%d] %s\n",bin_path.c_str(),errno,strerror(errno));
							exit (EXIT_FAILURE);
						}
					#else
						printf("Error opening file %s: [%d] %s\n",bin_path.c_str(),errno,strerror(errno));
						exit (EXIT_FAILURE);
					#endif
				}
				else { printf("Writing OpenCL-Binary to: %s\n",bin_path.c_str()); }
			}
			

			size_t rem = binsz;
			char *p = buffer;

			while (rem) {
				size_t wsz = fwrite(p, 1, rem, fp);
				if (wsz <= 0) {
					fclose(fp);
					unlink(bin_path.c_str());
					fp = NULL;
					break;
				}
				rem -= wsz;
				p += wsz;
			}

			if (fp) {
				fclose(fp);
			}

			delete[] buffer;
		}
#endif



		ker_filter = clCreateKernel(program, "filter", &err);
		if (err != CL_SUCCESS) {
			clReleaseProgram(program);
			clReleaseContext(context);
			setCLError(c, dev_id, err);
			return false;
		}

		ker_filter_in1_out32 = clCreateKernel(program, "filter_in1_out32", &err);
		if (err != CL_SUCCESS) {
			clReleaseProgram(program);
			clReleaseContext(context);
			clReleaseKernel(ker_filter);
			setCLError(c, dev_id, err);
			return false;
		}

		ker_filter_in3_out32 = clCreateKernel(program, "filter_in3_out32", &err);
		if (err != CL_SUCCESS) {
			clReleaseProgram(program);
			clReleaseContext(context);
			clReleaseKernel(ker_filter);
			clReleaseKernel(ker_filter_in1_out32);
			setCLError(c, dev_id, err);
			return false;
		}

		ker_filter_in128_out1 = clCreateKernel(program, "filter_in128_out1", &err);
		if (err != CL_SUCCESS) {
			clReleaseProgram(program);
			clReleaseContext(context);
			clReleaseKernel(ker_filter);
			clReleaseKernel(ker_filter_in1_out32);
			setCLError(c, dev_id, err);
			return false;
		}

		ker_filter_in128_out3 = clCreateKernel(program, "filter_in128_out3", &err);
		if (err != CL_SUCCESS) {
			clReleaseProgram(program);
			clReleaseContext(context);
			clReleaseKernel(ker_filter);
			clReleaseKernel(ker_filter_in1_out32);
			setCLError(c, dev_id, err);
			return false;
		}

		queue = clCreateCommandQueue(context, dev, 0, &err);
		if (err != CL_SUCCESS) {
			clReleaseProgram(program);
			clReleaseContext(context);
			clReleaseKernel(ker_filter);
			clReleaseKernel(ker_filter_in1_out32);
			setCLError(c, dev_id, err);
			return false;
		}

		env->num_cl_dev = 1;
		env->cl_dev_list = new OpenCLDev[1];

		env->cl_dev_list[0].platform = de->plt_id;
		env->cl_dev_list[0].context = context;
		env->cl_dev_list[0].devid = dev;
		env->cl_dev_list[0].queue = queue;
		env->cl_dev_list[0].program = program;
		env->cl_dev_list[0].ker_filter = ker_filter;
		env->cl_dev_list[0].ker_filter_in1_out32 = ker_filter_in1_out32;
		env->cl_dev_list[0].ker_filter_in128_out1 = ker_filter_in128_out1;
		env->cl_dev_list[0].ker_filter_in3_out32 = ker_filter_in3_out32;
		env->cl_dev_list[0].ker_filter_in128_out3 = ker_filter_in128_out3;
		env->cl_dev_list[0].name = &dev_name[0];

		return true;
	}

	void
		finiOpenCL(ComputeEnv *env)
	{
		for (int di = 0; di < env->num_cl_dev; di++) {
			OpenCLDev *d = &env->cl_dev_list[di];
			clReleaseKernel(d->ker_filter);
			clReleaseKernel(d->ker_filter_in128_out1);
			clReleaseKernel(d->ker_filter_in128_out3);
			clReleaseKernel(d->ker_filter_in1_out32);
			clReleaseKernel(d->ker_filter_in3_out32);
			clReleaseProgram(d->program);
			clReleaseCommandQueue(d->queue);
			clReleaseContext(d->context);
		}

		delete[] env->cl_dev_list;
	}



	void
		filter_OpenCL_impl(ComputeEnv *env,
			Buffer *packed_input_buf,
			Buffer *packed_output_buf,
			int nInputPlanes,
			int nOutputPlanes,
			const float *fbiases,
			const float *weight,
			int w,
			int h,
			int nJob)
	{
		cl_int err;
		int dev_id = 0;

		OpenCLDev *dev = &env->cl_dev_list[dev_id];
		size_t in_size = sizeof(float) * w * h * nInputPlanes;
		cl_context context = dev->context;

		cl_mem cl_packed_input = packed_input_buf->get_read_ptr_cl(env, dev_id, in_size);
		cl_mem cl_packed_output = packed_output_buf->get_write_ptr_cl(env, dev_id);

		cl_mem cl_fbiases = clCreateBuffer(context,
			CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
			sizeof(float) * nOutputPlanes,
			(void*)fbiases, &err
		);
		enum filter_type {
			FILTER_GENERIC,
			FILTER_IN1,
			FILTER_IN3,
			FILTER_OUT1,
			FILTER_OUT3,
		} type = FILTER_GENERIC;

		cl_kernel ker = dev->ker_filter;
		bool static_nplane = false;

		if (nInputPlanes == 1 && nOutputPlanes == 32) {
			type = FILTER_IN1;
			ker = dev->ker_filter_in1_out32;
		}
		else if (nInputPlanes == 3 && nOutputPlanes == 32) {
			type = FILTER_IN3;
			ker = dev->ker_filter_in3_out32;
			static_nplane = true;
		}
		else if (nOutputPlanes == 1 && nInputPlanes == 128) {
			type = FILTER_OUT1;
			ker = dev->ker_filter_in128_out1;
		}
		else if (nOutputPlanes == 3 && nInputPlanes == 128) {
			type = FILTER_OUT3;
			ker = dev->ker_filter_in128_out3;
			static_nplane = true;
		}


		size_t weight_size;

		if (type == FILTER_GENERIC) {
			weight_size = sizeof(float) * GPU_VEC_WIDTH * nInputPlanes * 9;
		}
		else {
			weight_size = sizeof(float) * nOutputPlanes * nInputPlanes * 9;
		}

		cl_mem cl_weight = clCreateBuffer(context,
			CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
			weight_size,
			(void*)weight, &err
		);

		int ai = 0;

		clSetKernelArg(ker, ai++, sizeof(cl_mem), &cl_packed_input);
		if (!static_nplane) {
			clSetKernelArg(ker, ai++, sizeof(cl_int), &nInputPlanes);
		}
		clSetKernelArg(ker, ai++, sizeof(cl_mem), &cl_packed_output);
		if (!static_nplane) {
			clSetKernelArg(ker, ai++, sizeof(cl_int), &nOutputPlanes);
		}
		clSetKernelArg(ker, ai++, sizeof(cl_mem), &cl_fbiases);
		clSetKernelArg(ker, ai++, sizeof(cl_int), &h);
		clSetKernelArg(ker, ai++, sizeof(cl_int), &w);
		clSetKernelArg(ker, ai++, sizeof(cl_mem), &cl_weight);

		cl_event event;

		size_t gws[3] = { 1, 1, 1 };
		size_t lws[3] = { 1, 1, 1 };
		if (type == FILTER_GENERIC) {
			gws[0] = h * nOutputPlanes;
			lws[0] = nOutputPlanes;
		}
		else if (type == FILTER_IN1) {
			gws[0] = h * 256;
			lws[0] = 256;
		}
		else if (type == FILTER_OUT1 || type == FILTER_OUT3) {
			gws[0] = h * 128;
			lws[0] = 128;
		}
		else if (type == FILTER_IN3) {
			gws[0] = h * 192;
			lws[0] = 192;
		}

		err = clEnqueueNDRangeKernel(dev->queue,
			ker,
			3,
			nullptr, gws, lws,
			0, nullptr, &event);
		if (err != CL_SUCCESS) {
			printf("enqueue ndrange error : %d\n", err);
			exit(1);
		}

		err = clWaitForEvents(1, &event);
		if (err != CL_SUCCESS) {
			printf("wait ndrange error : %d\n", err);
			exit(1);
		}

		if (err != CL_SUCCESS) {
			printf("read buffer error : %d\n", err);
			exit(1);
		}

		clReleaseMemObject(cl_fbiases);
		clReleaseMemObject(cl_weight);
		clReleaseEvent(event);
	}

}

