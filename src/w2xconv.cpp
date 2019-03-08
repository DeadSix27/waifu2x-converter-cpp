#define W2XCONV_IMPL
#define _WIN32_WINNT 0x0600

#define ENABLE_AVX 1

#include <thread>

#ifdef X86OPT
//#if (defined __GNUC__) || (defined __clang__)
#ifndef _WIN32
#include <cpuid.h>
#endif
#endif // X86OPT

#ifdef ARMOPT
#if defined __ANDROID__
#include <cpu-features.h>
#elif (defined(__linux))
#include <sys/auxv.h>
#endif
#endif

#include <limits.h>
#include <sstream>

#if defined(WIN32) && defined(UNICODE)
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

#endif

#include "w2xconv.h"
#include "sec.hpp"
#include "Buffer.hpp"
#include "modelHandler.hpp"
#include "convertRoutine.hpp"
#include "filters.hpp"
#include "cvwrap.hpp"

struct W2XConvImpl {
	std::string dev_name;

	ComputeEnv env;

	std::vector<std::unique_ptr<w2xc::Model> > noise0_models;
	std::vector<std::unique_ptr<w2xc::Model> > noise1_models;
	std::vector<std::unique_ptr<w2xc::Model> > noise2_models;
	std::vector<std::unique_ptr<w2xc::Model> > noise3_models;
	std::vector<std::unique_ptr<w2xc::Model> > scale2_models;
};

static std::vector<struct W2XConvProcessor> processor_list;

static void
global_init2(void)
{
	{
		W2XConvProcessor host;
		host.type = W2XCONV_PROC_HOST;
		host.sub_type = W2XCONV_PROC_HOST_OPENCV;
		host.dev_id = 0;
		host.dev_name = "Generic";
		host.num_core = std::thread::hardware_concurrency();

#ifdef X86OPT
#ifdef _WIN32
#define x_cpuid(p,eax) __cpuid(p, eax)
		typedef int cpuid_t;
#else
#define x_cpuid(p,eax) __get_cpuid(eax, &(p)[0], &(p)[1], &(p)[2], &(p)[3]);
		typedef unsigned int cpuid_t;
#endif
		cpuid_t v[4];
		cpuid_t data[4*3+1];

		x_cpuid(v, 0x80000000);
		if ((unsigned int)v[0] >= 0x80000004) {
			x_cpuid(data+4*0, 0x80000002);
			x_cpuid(data+4*1, 0x80000003);
			x_cpuid(data+4*2, 0x80000004);
			data[12] = 0;

			host.dev_name = strdup((char*)data);
		} else {
			x_cpuid(data, 0x0);
			data[4] = 0;
			host.dev_name = strdup((char*)(data + 1));
		}

		x_cpuid(v, 1);

		if (ENABLE_AVX && (v[2] & 0x18000000) == 0x18000000) {
			if (v[2] & (1<<12)) {
				host.sub_type = W2XCONV_PROC_HOST_FMA;
			} else {
				host.sub_type = W2XCONV_PROC_HOST_AVX;
			}
		} else if (v[2] & (1<<0)) {
			host.sub_type = W2XCONV_PROC_HOST_SSE3;
		}
#endif // X86OPT

#ifdef ARMOPT
		bool have_neon = false;
#if defined(__ARM_NEON)
// armv8 or -march=armv7-a for all files
		have_neon = true;
#elif defined(__ANDROID__)
		int hwcap = android_getCpuFeatures();
		if (hwcap & ANDROID_CPU_ARM_FEATURE_NEON) {
			have_neon = true;
		}
#elif defined(__linux)
		int hwcap = getauxval(AT_HWCAP);
		if (hwcap & HWCAP_ARM_NEON) {
			have_neon = true;
		}
#endif
		if (have_neon) {
			host.dev_name = "ARM NEON";
			host.sub_type = W2XCONV_PROC_HOST_NEON;
		}
#endif

		processor_list.push_back(host);
	}

	w2xc::initOpenCLGlobal(&processor_list);
	w2xc::initCUDAGlobal(&processor_list);


	/*
	 * <priority>
	 * 1: NV CUDA
	 * 2: OCL GPU
	 * 3: host AVX
	 * 4: OCL GPU (intel gen)
	 * 5: host
	 * 6: other
	 *
	 * && orderd by num_core
	 */
	std::sort(processor_list.begin(),
		  processor_list.end(),
		  [&](W2XConvProcessor const &p0,
		      W2XConvProcessor const &p1)
		  {
			  bool p0_is_opencl_gpu =
				  (p0.type == W2XCONV_PROC_OPENCL) &&
				  ((p0.sub_type&W2XCONV_PROC_OPENCL_DEVICE_MASK) == W2XCONV_PROC_OPENCL_DEVICE_GPU)
				  ;

			  bool p1_is_opencl_gpu =
				  (p1.type == W2XCONV_PROC_OPENCL) &&
				  ((p1.sub_type&W2XCONV_PROC_OPENCL_DEVICE_MASK) == W2XCONV_PROC_OPENCL_DEVICE_GPU)
				  ;


			  bool p0_is_opencl_intel_gpu =
				  (p0.type == W2XCONV_PROC_OPENCL) &&
				  (p0.sub_type == W2XCONV_PROC_OPENCL_INTEL_GPU)
				  ;

			  bool p1_is_opencl_intel_gpu =
				  (p1.type == W2XCONV_PROC_OPENCL) &&
				  (p1.sub_type == W2XCONV_PROC_OPENCL_INTEL_GPU)
				  ;

			  bool p0_host_avx =
				  (p0.type == W2XCONV_PROC_HOST) &&
				  (p0.sub_type >= W2XCONV_PROC_HOST_AVX)
				  ;

			  bool p1_host_avx =
				  (p1.type == W2XCONV_PROC_HOST) &&
				  (p1.sub_type >= W2XCONV_PROC_HOST_AVX)
				  ;

			  if (p0.type == p1.type) {
				  if (p0.type == W2XCONV_PROC_OPENCL) {
					  if (p0.sub_type != p1.sub_type) {
						  if (p0_is_opencl_gpu) {
							  return true;
						  }

						  if (p1_is_opencl_gpu) {
							  return false;
						  }
					  }
				  }

				  if (p0.num_core != p1.num_core) {
					  return p0.num_core > p1.num_core;
				  }
			  } else {
				  if (p0.type == W2XCONV_PROC_CUDA) {
					  return true;
				  }

				  if (p1.type == W2XCONV_PROC_CUDA) {
					  return false;
				  }

				  if (p0_is_opencl_intel_gpu) {
					  if (p1_host_avx) {
						  return false;
					  }
				  }

				  if (p1_is_opencl_intel_gpu) {
					  if (p0_host_avx) {
						  return false;
					  }
				  }

				  if (p0_is_opencl_gpu) {
					  return true;
				  }

				  if (p1_is_opencl_gpu) {
					  return false;
				  }
			  }

			  if (p0.type == W2XCONV_PROC_HOST) {
				  return true;
			  }

			  if (p1.type == W2XCONV_PROC_HOST) {
				  return false;
			  }

			  /* ?? */
			  return p0.dev_id < p1.dev_id;
		  });
}

#ifdef _WIN32
#include <windows.h>
static INIT_ONCE global_init_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK
global_init1(PINIT_ONCE initOnce,
	     PVOID Parameter,
	     PVOID *Context)
{
	global_init2();
	return TRUE;
}

static void
global_init(void)
{
	InitOnceExecuteOnce(&global_init_once,
			    global_init1,
			    nullptr, nullptr);
}
#else

#include <pthread.h>

static pthread_once_t global_init_once = PTHREAD_ONCE_INIT;
static void
global_init1()
{
	global_init2();
}
static void
global_init()
{
	pthread_once(&global_init_once, global_init1);
}
#endif


const struct W2XConvProcessor *
w2xconv_get_processor_list(size_t *ret_num)
{
	global_init();

	*ret_num = processor_list.size();
	return &processor_list[0];
}

static int
select_device(enum W2XConvGPUMode gpu)
{
	size_t n = processor_list.size();
	if (gpu == W2XCONV_GPU_FORCE_OPENCL) {
		for (size_t i=0; i<n; i++) {
			if (processor_list[i].type == W2XCONV_PROC_OPENCL) {
				return (int) i;
			}
		}
	}

	int host_proc = 0;
	for (int i=0; i<n; i++) {
		if (processor_list[i].type == W2XCONV_PROC_HOST) {
			host_proc = i;
			break;
		}
	}

	if (gpu == W2XCONV_GPU_AUTO) {
		/* 1. CUDA
		 * 2. AMD GPU OpenCL
		 * 3. FMA
		 * 4. AVX
		 * 5. Intel GPU OpenCL
		 */

		for (int i=0; i<n; i++) {
			if (processor_list[i].type == W2XCONV_PROC_CUDA) {
				return i;
			}
		}

		for (int i=0; i<n; i++) {
			if ((processor_list[i].type == W2XCONV_PROC_OPENCL) &&
			    (processor_list[i].sub_type == W2XCONV_PROC_OPENCL_AMD_GPU))
			{
				return i;
			}
		}

		if (processor_list[host_proc].sub_type == W2XCONV_PROC_HOST_FMA ||
		    processor_list[host_proc].sub_type == W2XCONV_PROC_HOST_AVX)
		{
			return host_proc;
		}

		for (int i=0; i<n; i++) {
			if ((processor_list[i].type == W2XCONV_PROC_OPENCL) &&
			    (processor_list[i].sub_type == W2XCONV_PROC_OPENCL_INTEL_GPU))
			{
				return i;
			}
		}

		return host_proc;
	}

	/* (gpu == GPU_DISABLE) */
	for (int i=0; i<n; i++) {
		if (processor_list[i].type == W2XCONV_PROC_HOST) {
			return i;
		}
	}

	return 0;		// ??
}

W2XConv *
w2xconv_init(enum W2XConvGPUMode gpu,
             int nJob,
	     int enable_log)
{
	global_init();

	int proc_idx = select_device(gpu);
	return w2xconv_init_with_processor(proc_idx, nJob, enable_log);
}

struct W2XConv *
w2xconv_init_with_processor(int processor_idx,
			    int nJob,
			    int enable_log)
{
	global_init();

	struct W2XConv *c = new struct W2XConv;
	struct W2XConvImpl *impl = new W2XConvImpl;
	struct W2XConvProcessor *proc = &processor_list[processor_idx];

	if (nJob == 0) {
		nJob = std::thread::hardware_concurrency();
	}

	bool r;

	switch (proc->type) {
	case W2XCONV_PROC_CUDA:
		w2xc::initCUDA(&impl->env, proc->dev_id);
		break;

	case W2XCONV_PROC_OPENCL:
		r = w2xc::initOpenCL(c, &impl->env, proc);
		if (!r) {
			return NULL;
		}
		break;

	default:
	case W2XCONV_PROC_HOST:
		break;
	}

#if defined(_WIN32) || defined(__linux)
	impl->env.tpool = w2xc::initThreadPool(nJob);
#endif

	w2xc::modelUtility::getInstance().setNumberOfJobs(nJob);

	c->impl = impl;
	c->enable_log = enable_log;
	c->target_processor = proc;
	c->last_error.code = W2XCONV_NOERROR;
	c->flops.flop = 0;
	c->flops.filter_sec = 0;
	c->flops.process_sec = 0;

	return c;
}

void
clearError(W2XConv *conv)
{
	switch (conv->last_error.code) {
	case W2XCONV_NOERROR:
	case W2XCONV_ERROR_Y_MODEL_MISMATCH_TO_RGB_F32:
	case W2XCONV_ERROR_WIN32_ERROR:
	case W2XCONV_ERROR_LIBC_ERROR:
	case W2XCONV_ERROR_RGB_MODEL_MISMATCH_TO_Y:
		break;

	case W2XCONV_ERROR_WIN32_ERROR_PATH:
		free(conv->last_error.u.win32_path.path);
		break;

	case W2XCONV_ERROR_LIBC_ERROR_PATH:
		free(conv->last_error.u.libc_path.path);
		break;

	case W2XCONV_ERROR_MODEL_LOAD_FAILED:
	case W2XCONV_ERROR_IMREAD_FAILED:
	case W2XCONV_ERROR_IMWRITE_FAILED:
		free(conv->last_error.u.path);
		break;
	default:
		break;
	}
}

char *
w2xconv_strerror(W2XConvError *e)
{
	std::ostringstream oss;
	char *str;

	switch (e->code) {
	case W2XCONV_NOERROR:
		oss << "no error";
		break;

	case W2XCONV_ERROR_OPENCL:
		oss << "opencl_err: " << e->u.errno_;
		break;

	case W2XCONV_ERROR_WIN32_ERROR:
		oss << "win32_err: " << e->u.errno_;
		break;

	case W2XCONV_ERROR_WIN32_ERROR_PATH:
		oss << "win32_err: " << e->u.win32_path.errno_ << "(" << e->u.win32_path.path << ")";
		break;

	case W2XCONV_ERROR_LIBC_ERROR:
		oss << strerror(e->u.errno_);
		break;

	case W2XCONV_ERROR_LIBC_ERROR_PATH:
		str = strerror(e->u.libc_path.errno_);
		oss << str << "(" << e->u.libc_path.path << ")";
		break;

	case W2XCONV_ERROR_MODEL_LOAD_FAILED:
		oss << "model load failed: " << e->u.path;
		break;

	case W2XCONV_ERROR_IMREAD_FAILED:
		oss << "cv::imread(\"" << e->u.path << "\") failed";
		break;

	case W2XCONV_ERROR_IMWRITE_FAILED:
		oss << "cv::imwrite(\"" << e->u.path << "\") failed";
		break;

	case W2XCONV_ERROR_RGB_MODEL_MISMATCH_TO_Y:
		oss << "cannot apply rgb model to yuv.";
		break;

	case W2XCONV_ERROR_Y_MODEL_MISMATCH_TO_RGB_F32:
		oss << "cannot apply y model to rgb_f32.";
		break;
	}

	return strdup(oss.str().c_str());
}

void
w2xconv_free(void *p)
{
	free(p);
}


static void
setPathError(W2XConv *conv,
             enum W2XConvErrorCode code,
             std::string const &path)
{
	clearError(conv);

	conv->last_error.code = code;
	conv->last_error.u.path = strdup(path.c_str());
}

#if defined(WIN32) && defined(UNICODE)
static void
setPathError(W2XConv *conv,
             enum W2XConvErrorCode code,
             std::wstring const &path)
{
	fs::path fspath = path;
	clearError(conv);

	conv->last_error.code = code;
	conv->last_error.u.path = strdup(fspath.string().c_str());
}
#endif

static void
setError(W2XConv *conv,
	 enum W2XConvErrorCode code)
{
	clearError(conv);
	conv->last_error.code = code;
}


int
w2xconv_load_models(W2XConv *conv, const char *model_dir)
{
	struct W2XConvImpl *impl = conv->impl;

	std::string modelFileName(model_dir);

	impl->noise0_models.clear();
	impl->noise1_models.clear();
	impl->noise2_models.clear();
	impl->noise3_models.clear();
	impl->scale2_models.clear();

	if (!w2xc::modelUtility::generateModelFromJSON(modelFileName + "/noise0_model.json", impl->noise0_models)) {
		setPathError(conv,
			     W2XCONV_ERROR_MODEL_LOAD_FAILED,
			     modelFileName + "/noise0_model.json");
		return -1;
	}

	if (!w2xc::modelUtility::generateModelFromJSON(modelFileName + "/noise1_model.json", impl->noise1_models)) {
		setPathError(conv,
			     W2XCONV_ERROR_MODEL_LOAD_FAILED,
			     modelFileName + "/noise1_model.json");
		return -1;
	}
	if (!w2xc::modelUtility::generateModelFromJSON(modelFileName + "/noise2_model.json", impl->noise2_models)) {
		setPathError(conv,
			     W2XCONV_ERROR_MODEL_LOAD_FAILED,
			     modelFileName + "/noise2_model.json");
		return -1;
	}
	if (!w2xc::modelUtility::generateModelFromJSON(modelFileName + "/noise3_model.json", impl->noise3_models)) {
		setPathError(conv,
			     W2XCONV_ERROR_MODEL_LOAD_FAILED,
			     modelFileName + "/noise3_model.json");
		return -1;
	}
	if (!w2xc::modelUtility::generateModelFromJSON(modelFileName + "/scale2.0x_model.json", impl->scale2_models)) {
		setPathError(conv,
			     W2XCONV_ERROR_MODEL_LOAD_FAILED,
			     modelFileName + "/scale2.0x_model.json");
		return -1;

	}

	return 0;
}

void
w2xconv_set_model_3x3(struct W2XConv *conv,
		      enum W2XConvFilterType m,
		      int layer_depth,
		      int num_input_plane,
		      const int *num_map, // num_map[layer_depth]
		      const float *coef_list, // coef_list[layer_depth][num_map][3x3]
		      const float *bias // bias[layer_depth][num_map]
	)
{
	struct W2XConvImpl *impl = conv->impl;
	std::vector<std::unique_ptr<w2xc::Model> > *models = nullptr;

	switch (m) {
	case W2XCONV_FILTER_DENOISE0:
		models = &impl->noise0_models;
		break;
	case W2XCONV_FILTER_DENOISE1:
		models = &impl->noise1_models;
		break;
	case W2XCONV_FILTER_DENOISE2:
		models = &impl->noise2_models;
		break;
	case W2XCONV_FILTER_DENOISE3:
		models = &impl->noise3_models;
		break;
	case W2XCONV_FILTER_SCALE2x:
		models = &impl->scale2_models;
		break;
	}

	models->clear();
	w2xc::modelUtility::generateModelFromMEM(layer_depth,
						 num_input_plane,
						 num_map,
						 coef_list,
						 bias,
						 *models);
}

void
w2xconv_fini(struct W2XConv *conv)
{
	struct W2XConvImpl *impl = conv->impl;
	clearError(conv);

	w2xc::finiCUDA(&impl->env);
	w2xc::finiOpenCL(&impl->env);
#if defined(_WIN32) || defined(__linux)
	w2xc::finiThreadPool(impl->env.tpool);
#endif

	delete impl;
	delete conv;
}

#ifdef HAVE_OPENCV
static void
apply_denoise(struct W2XConv *conv,
	      cv::Mat &image,
	      int denoise_level,
	      int blockSize,
	      enum w2xc::image_format fmt)
{
	struct W2XConvImpl *impl = conv->impl;
	ComputeEnv *env = &impl->env;

	std::vector<cv::Mat> imageSplit;
	cv::Mat *input;
	cv::Mat *output;
	cv::Mat imageY;

	if (IS_3CHANNEL(fmt)) {
		input = &image;
		output = &image;
	} else {
		cv::split(image, imageSplit);
		imageSplit[0].copyTo(imageY);
		input = &imageY;
		output = &imageSplit[0];
	}

	W2Mat output_2;
	W2Mat input_2(extract_view_from_cvmat(*input));

	if (denoise_level == 0) {
		w2xc::convertWithModels(conv, env, input_2, output_2,
					impl->noise0_models,
					&conv->flops, blockSize, fmt, conv->enable_log);
	}
	else if (denoise_level == 1) {
		w2xc::convertWithModels(conv, env, input_2, output_2,
					impl->noise1_models,
					&conv->flops, blockSize, fmt, conv->enable_log);
	}
	else if (denoise_level == 2) {
		w2xc::convertWithModels(conv, env, input_2, output_2,
					impl->noise2_models,
					&conv->flops, blockSize, fmt, conv->enable_log);
	}
	else if (denoise_level == 3) {
		w2xc::convertWithModels(conv, env, input_2, output_2,
					impl->noise3_models,
					&conv->flops, blockSize, fmt, conv->enable_log);
	}

	*output = copy_to_cvmat(output_2);

	if (! IS_3CHANNEL(fmt)) {
		cv::merge(imageSplit, image);
	}
}

static void
apply_scale(struct W2XConv *conv,
	    cv::Mat &image,
	    int iterTimesTwiceScaling,
	    int blockSize,
	    enum w2xc::image_format fmt)
{
	struct W2XConvImpl *impl = conv->impl;
	ComputeEnv *env = &impl->env;

	if (conv->enable_log) {
		std::cout << "start scaling" << std::endl;
	}

	// 2x scaling
	for (int nIteration = 0; nIteration < iterTimesTwiceScaling;
	     nIteration++) {

		if (conv->enable_log) {
			std::cout << "#" << std::to_string(nIteration + 1)
				  << " 2x scaling..." << std::endl;
		}

		cv::Size imageSize = image.size();
		imageSize.width *= 2;
		imageSize.height *= 2;
		cv::Mat image2xNearest;
		cv::Mat imageY;
		std::vector<cv::Mat> imageSplit;
		cv::Mat image2xBicubic;
		cv::Mat *input, *output;

		cv::resize(image, image2xNearest, imageSize, 0, 0, cv::INTER_NEAREST);

		if (IS_3CHANNEL(fmt)) {
			input = &image2xNearest;
			output = &image;
		} else {
			cv::split(image2xNearest, imageSplit);
			imageSplit[0].copyTo(imageY);
			// generate bicubic scaled image and
			// convert RGB -> YUV and split
			imageSplit.clear();
			cv::resize(image,image2xBicubic,imageSize,0,0,cv::INTER_CUBIC);
			cv::split(image2xBicubic, imageSplit);
			input = &imageY;
			output = &imageSplit[0];
		}

		W2Mat output_2;
		W2Mat input_2(extract_view_from_cvmat(*input));

		if(!w2xc::convertWithModels(conv,
					    env,
					    input_2,
					    output_2,
					    impl->scale2_models,
					    &conv->flops, blockSize, fmt,
					    conv->enable_log))
		{
			std::cerr << "w2xc::convertWithModels : something error has occured.\n"
				"stop." << std::endl;
			std::exit(1);
		}

		*output = copy_to_cvmat(output_2);

		if (!IS_3CHANNEL(fmt)) {
			cv::merge(imageSplit, image);
		}

	} // 2x scaling : end
}

static inline float
clipf(float min, float v, float max)
{
	v = std::max(min,v);
	v = std::min(max,v);

	return v;
}

template <typename SRC_TYPE, int src_max, int ridx, int bidx>
static void
preproc_rgb2yuv(cv::Mat *dst,
		cv::Mat *src)
{
	int w = src->size().width;
	int h = src->size().height;

	float div = 1.0f / src_max;

	for (int yi=0; yi<h; yi++) {
		const SRC_TYPE *src_line = (SRC_TYPE*)src->ptr(yi);
		float *dst_line = (float*)dst->ptr(yi);

		for (int xi=0; xi<w; xi++) {
			float b = src_line[xi*3 + bidx] * div;
			float g = src_line[xi*3 + 1] * div;
			float r = src_line[xi*3 + ridx] * div;

			float Y = clipf(0.0f, b*0.114f + g*0.587f + r*0.299f, 1.0f);
			float U = clipf(0.0f, (b-Y) * 0.492f + 0.5f,          1.0f);
			float V = clipf(0.0f, (r-Y) * 0.877f + 0.5f,          1.0f);

			dst_line[xi*3 + 0] = Y;
			dst_line[xi*3 + 1] = U;
			dst_line[xi*3 + 2] = V;
		}
	}
}

template <typename SRC_TYPE, int ridx, int bidx>
static bool
set_nearest_nontransparent(float *r, float *g, float *b,
			   const SRC_TYPE *s,
			   int xi)
{
	SRC_TYPE a = s[xi*4+3];
	if (a == 0) {
		return false;
	}

	*r = (float)s[xi*4+ridx];
	*g = (float)s[xi*4+1];
	*b = (float)s[xi*4+bidx];

	return true;
}


template <typename SRC_TYPE, int src_max, int ridx, int bidx>
static void
preproc_rgba2yuv(cv::Mat *dst_yuv,
		 cv::Mat *dst_alpha,
		 cv::Mat *src,
		 float bkgd_r,
		 float bkgd_g,
		 float bkgd_b)
{
	int w = src->size().width;
	int h = src->size().height;

	float div = 1.0f / src_max;
	float alpha_coef = 1.0f / src_max;

	for (int yi=0; yi<h; yi++) {
		const SRC_TYPE *src_line = (SRC_TYPE*)src->ptr(yi);
		const SRC_TYPE *src_line0 = NULL, *src_line2 = NULL;

		if (yi != 0) {
			src_line0 = (SRC_TYPE*)src->ptr(yi-1);
		}

		if (yi != h-1) {
			src_line2 = (SRC_TYPE*)src->ptr(yi+1);
		}

		float *dst_yuv_line = (float*)dst_yuv->ptr(yi);
		float *dst_alpha_line = (float*)dst_alpha->ptr(yi);

		for (int xi=0; xi<w; xi++) {
			float r = src_line[xi*4 + ridx] * div;
			float g = src_line[xi*4 + 1] * div;
			float b = src_line[xi*4 + bidx] * div;
			SRC_TYPE a = src_line[xi*4 + 3];
			if (a == 0) {
				r = bkgd_r;
				g = bkgd_g;
				b = bkgd_b;

#if 0
				if (yi == 0 || yi == h-1 || xi == 0 || xi == w-1) {
					/* xx */
					r = bkgd_r;
					g = bkgd_g;
					b = bkgd_b;
				} else {
					/* set nearest non-transparental color */
					SRC_TYPE near_a;
					bool set = false;

					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line0, xi-1);
					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line0, xi);
					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line0, xi+1);

					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line,  xi-1);
					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line,  xi+1);

					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line2, xi-1);
					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line2, xi);
					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line2, xi+1);

					if (set) {
						r *= div;
						g *= div;
						b *= div;
					} else {
						r = bkgd_r;
						g = bkgd_g;
						b = bkgd_b;
					}
				}
#endif

			} else {
				SRC_TYPE ra = src_max - a;
				r = r * (a * alpha_coef) + bkgd_r * (ra * alpha_coef);
				g = g * (a * alpha_coef) + bkgd_g * (ra * alpha_coef);
				b = b * (a * alpha_coef) + bkgd_b * (ra * alpha_coef);
			}
			float Y = clipf(0.0f, b*0.114f + g*0.587f + r*0.299f, 1.0f);
			float U = clipf(0.0f, (b-Y) * 0.492f + 0.5f,          1.0f);
			float V = clipf(0.0f, (r-Y) * 0.877f + 0.5f,          1.0f);

			dst_yuv_line[xi*3 + 0] = Y;
			dst_yuv_line[xi*3 + 1] = U;
			dst_yuv_line[xi*3 + 2] = V;
			dst_alpha_line[xi] = a * div;
		}
	}
}

template <typename SRC_TYPE, int src_max, int ridx, int bidx>
static void
preproc_rgb2rgb(cv::Mat *dst,
		cv::Mat *src)
{
	int w = src->size().width;
	int h = src->size().height;

	float div = 1.0f / src_max;

	for (int yi=0; yi<h; yi++) {
		const SRC_TYPE *src_line = (SRC_TYPE*)src->ptr(yi);
		float *dst_line = (float*)dst->ptr(yi);

		for (int xi=0; xi<w; xi++) {
			float r = src_line[xi*3 + ridx] * div;
			float g = src_line[xi*3 + 1] * div;
			float b = src_line[xi*3 + bidx] * div;

			dst_line[xi*3 + 0] = r;
			dst_line[xi*3 + 1] = g;
			dst_line[xi*3 + 2] = b;
		}
	}
}
template <typename SRC_TYPE, int src_max, int ridx, int bidx>
static void
preproc_rgba2rgb(cv::Mat *dst_rgb,
		 cv::Mat *dst_alpha,
		 cv::Mat *src,
		 float bkgd_r,
		 float bkgd_g,
		 float bkgd_b)
{
	int w = src->size().width;
	int h = src->size().height;

	float div = 1.0f / src_max;
	float alpha_coef = 1.0f / src_max;

	for (int yi=0; yi<h; yi++) {
		const SRC_TYPE *src_line = (SRC_TYPE*)src->ptr(yi);
		const SRC_TYPE *src_line0 = NULL, *src_line2 = NULL;

		if (yi != 0) {
			src_line0 = (SRC_TYPE*)src->ptr(yi-1);
		}

		if (yi != h-1) {
			src_line2 = (SRC_TYPE*)src->ptr(yi+1);
		}

		float *dst_rgb_line = (float*)dst_rgb->ptr(yi);
		float *dst_alpha_line = (float*)dst_alpha->ptr(yi);

		for (int xi=0; xi<w; xi++) {
			float r = src_line[xi*4 + ridx] * div;
			float g = src_line[xi*4 + 1] * div;
			float b = src_line[xi*4 + bidx] * div;
			SRC_TYPE a = src_line[xi*4 + 3];
			if (a == 0) {
				r = bkgd_r;
				g = bkgd_g;
				b = bkgd_b;

#if 0
				if (yi == 0 || yi == h-1 || xi == 0 || xi == w-1) {
					/* xx */
					r = bkgd_r;
					g = bkgd_g;
					b = bkgd_b;
				} else {
					/* set nearest non-transparental color */
					SRC_TYPE near_a;
					bool set = false;

					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line0, xi-1);
					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line0, xi);
					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line0, xi+1);

					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line,  xi-1);
					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line,  xi+1);

					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line2, xi-1);
					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line2, xi);
					if (!set) set = set_nearest_nontransparent<SRC_TYPE,ridx,bidx>(&r, &g, &b, src_line2, xi+1);

					if (set) {
						r *= div;
						g *= div;
						b *= div;
					} else {
						r = bkgd_r;
						g = bkgd_g;
						b = bkgd_b;
					}
				}
#endif
			} else {
				SRC_TYPE ra = src_max - a;
				r = r * (a * alpha_coef) + bkgd_r * (ra * alpha_coef);
				g = g * (a * alpha_coef) + bkgd_g * (ra * alpha_coef);
				b = b * (a * alpha_coef) + bkgd_b * (ra * alpha_coef);

				r = std::min(1.0f, r);
				g = std::min(1.0f, g);
				b = std::min(1.0f, b);
			}
			dst_rgb_line[xi*3 + 0] = r;
			dst_rgb_line[xi*3 + 1] = g;
			dst_rgb_line[xi*3 + 2] = b;
			dst_alpha_line[xi] = a * div;
		}
	}
}


template <typename DST_TYPE, int dst_max, int ridx, int bidx>
static void
postproc_rgb2rgba(cv::Mat *dst,
		  cv::Mat *src_rgb,
		  cv::Mat *src_alpha,
		  float bkgd_r,
		  float bkgd_g,
		  float bkgd_b)
{
	int w = dst->size().width;
	int h = dst->size().height;

	for (int yi=0; yi<h; yi++) {
		const float *src_rgb_line = (float*)src_rgb->ptr(yi);
		const float *src_alpha_line = (float*)src_alpha->ptr(yi);
		DST_TYPE *dst_line = (DST_TYPE*)dst->ptr(yi);

		for (int xi=0; xi<w; xi++) {
			float r = src_rgb_line[xi*3 + 0];
			float g = src_rgb_line[xi*3 + 1];
			float b = src_rgb_line[xi*3 + 2];
			float a = src_alpha_line[xi];

			/*       data = src*alpha + bkgd*(1-alpha)    */
			/* -src*alpha = bkgd*(1-alpha) - data         */
			/*  src*alpha = data - bkgd*(1-alpha)         */
			/*  src*alpha = data - bkgd + bkgd * alpha    */
			/*        src = (data - bkgd)/alpha+bkgd      */

			r = (r - bkgd_r)/a + bkgd_r;
			g = (g - bkgd_g)/a + bkgd_g;
			b = (b - bkgd_b)/a + bkgd_b;

			r = clipf(0.0f, r * dst_max, dst_max);
			g = clipf(0.0f, g * dst_max, dst_max);
			b = clipf(0.0f, b * dst_max, dst_max);
			a = clipf(0.0f, a * dst_max, dst_max);

			dst_line[xi*4 + ridx] = (DST_TYPE)r;
			dst_line[xi*4 + 1] = (DST_TYPE)g;
			dst_line[xi*4 + bidx] = (DST_TYPE)b;
			dst_line[xi*4 + 3] = (DST_TYPE)a;
		}
	}
}

template <typename DST_TYPE, int dst_max, int ridx, int bidx>
static void
postproc_rgb2rgb(cv::Mat *dst,
		 cv::Mat *src_rgb)
{
	int w = dst->size().width;
	int h = dst->size().height;

	for (int yi=0; yi<h; yi++) {
		const float *src_rgb_line = (float*)src_rgb->ptr(yi);
		DST_TYPE *dst_line = (DST_TYPE*)dst->ptr(yi);

		for (int xi=0; xi<w; xi++) {
			float r = src_rgb_line[xi*3 + 0];
			float g = src_rgb_line[xi*3 + 1];
			float b = src_rgb_line[xi*3 + 2];

			r = clipf(0.0f, r * dst_max, dst_max);
			g = clipf(0.0f, g * dst_max, dst_max);
			b = clipf(0.0f, b * dst_max, dst_max);

			dst_line[xi*3 + ridx] = (DST_TYPE)r;
			dst_line[xi*3 + 1] = (DST_TYPE)g;
			dst_line[xi*3 + bidx] = (DST_TYPE)b;
		}
	}
}

template <typename DST_TYPE, int dst_max, int ridx, int bidx>
static void
postproc_yuv2rgba(cv::Mat *dst,
		  cv::Mat *src_yuv,
		  cv::Mat *src_alpha,
		  float bkgd_r,
		  float bkgd_g,
		  float bkgd_b)
{
	int w = dst->size().width;
	int h = dst->size().height;

	for (int yi=0; yi<h; yi++) {
		const float *src_yuv_line = (float*)src_yuv->ptr(yi);
		const float *src_alpha_line = (float*)src_alpha->ptr(yi);
		DST_TYPE *dst_line = (DST_TYPE*)dst->ptr(yi);

		for (int xi=0; xi<w; xi++) {
			float a = src_alpha_line[xi];
			float y = src_yuv_line[xi*3 + 0];
			float cr = src_yuv_line[xi*3 + 1];
			float cb = src_yuv_line[xi*3 + 2];
			float C0 = 2.032f, C1 = -0.395f, C2 = -0.581f, C3 = 1.140f;

			float b = y + (cb-0.5f)*C3;
			float g = y + (cb-0.5f)*C2 + (cr-0.5f)*C1;
			float r = y + (cr-0.5f)*C0;

			r = (r - bkgd_r)/a + bkgd_r;
			g = (g - bkgd_g)/a + bkgd_g;
			b = (b - bkgd_b)/a + bkgd_b;

			r = clipf(0.0f, r * dst_max, dst_max);
			g = clipf(0.0f, g * dst_max, dst_max);
			b = clipf(0.0f, b * dst_max, dst_max);
			a = clipf(0.0f, a * dst_max, dst_max);

			dst_line[xi*4 + ridx] = (DST_TYPE)r;
			dst_line[xi*4 + 1] = (DST_TYPE)g;
			dst_line[xi*4 + bidx] = (DST_TYPE)b;
			dst_line[xi*4 + 3] = (DST_TYPE)a;
		}
	}
}

template <typename DST_TYPE, int dst_max, int ridx, int bidx>
static void
postproc_yuv2rgb(cv::Mat *dst,
		 cv::Mat *src_yuv)
{
	int w = dst->size().width;
	int h = dst->size().height;

	for (int yi=0; yi<h; yi++) {
		const float *src_yuv_line = (float*)src_yuv->ptr(yi);
		DST_TYPE *dst_line = (DST_TYPE*)dst->ptr(yi);

		for (int xi=0; xi<w; xi++) {
			float y = src_yuv_line[xi*3 + 0];
			float cr = src_yuv_line[xi*3 + 1];
			float cb = src_yuv_line[xi*3 + 2];

			float C0 = 2.032f, C1 = -0.395f, C2 = -0.581f, C3 = 1.140f;

			float b = y + (cb-0.5f)*C3;
			float g = y + (cb-0.5f)*C2 + (cr-0.5f)*C1;
			float r = y + (cr-0.5f)*C0;

			r = clipf(0.0f, r * dst_max, dst_max);
			g = clipf(0.0f, g * dst_max, dst_max);
			b = clipf(0.0f, b * dst_max, dst_max);

			dst_line[xi*3 + ridx] = (DST_TYPE)r;
			dst_line[xi*3 + 1] = (DST_TYPE)g;
			dst_line[xi*3 + bidx] = (DST_TYPE)b;
		}
	}
}



static int
read_int4(FILE *fp) {
    unsigned int c0 = fgetc(fp);
    unsigned int c1 = fgetc(fp);
    unsigned int c2 = fgetc(fp);
    unsigned int c3 = fgetc(fp);

    return (c0<<24) | (c1<<16) | (c2<<8) | (c3);
}
static int
read_int2(FILE *fp) {
    unsigned int c0 = fgetc(fp);
    unsigned int c1 = fgetc(fp);

    return (c0<<8) | (c1);
}


//This checks if the file type is png, it defalts to the user inputted bkgd_colour otherwise.
//The returning bool is whether the function excecuted successfully or not.
void get_png_background_colour(FILE *png_fp, bool *png_rgb, struct w2xconv_rgb_float3 *bkgd_colour){
	*png_rgb = false;
	//png file signature
	const static unsigned char png[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
	//byte signature of sub components
	const static unsigned char ihdr[] = {'I','H','D','R'};
	const static unsigned char iend[] = {'I','E','N','D'};
	const static unsigned char bkgd[] = {'b','K','G','D'};

	char sig[8];

	//checks if the file is atleast 8 bytes long(?)
	size_t rdsz = fread(sig, 1, 8, png_fp);
	if (rdsz != 8) {
		return;
	}
	//check if file signatures match
	if (memcmp(png,sig,8) != 0) {
		return;
	}



	int ihdr_size = read_int4(png_fp);
	if (ihdr_size != 13) {
		return;
	}

	rdsz = fread(sig, 1, 4, png_fp);
	if (rdsz != 4) {
		return;
	}
	if (memcmp(ihdr,sig,4) != 0) {
		return;
	}

	int width = read_int4(png_fp);
	int height = read_int4(png_fp);
	int depth = fgetc(png_fp);
	int type = fgetc(png_fp);
	int compress = fgetc(png_fp);
	int filter = fgetc(png_fp);
	int interlace = fgetc(png_fp);

	/* use IMREAD_UNCHANGED
	 * if png && type == RGBA || depth == 16
	 */
	if (type == 6) {
		if (depth == 8 || // RGBA 8bit
		    depth == 16	  // RGBA 16bit
			)
		{
			*png_rgb = true;
		}
	} else if (depth == 16) { // RGB 16bit
		*png_rgb = true;
	}

	if (png_rgb) {
		while (1) {
			int chunk_size = read_int4(png_fp);
			rdsz = fread(sig, 1, 4, png_fp);
			if (rdsz != 4) {
				break;
			}

			if (memcmp(sig,iend,4) == 0) {
				break;
			}

			if (memcmp(sig,bkgd,4) == 0) {
				float r = (float) read_int2(png_fp);
				float g = (float) read_int2(png_fp);
				float b = (float) read_int2(png_fp);

				if (depth == 8) {
					bkgd_colour->r = r / 255.0f;
					bkgd_colour->g = g / 255.0f;
					bkgd_colour->b = b / 255.0f;
				} else {
					bkgd_colour->r = r / 65535.0f;
					bkgd_colour->g = g / 65535.0f;
					bkgd_colour->b = b / 65535.0f;
				}

				break;
			}
		}
	}

	return;
}

void
w2xconv_convert_mat(struct W2XConv *conv, 
			cv::Mat& image_dst, 
					cv::Mat& image_src, 
					int denoise_level, 
					double scale, 
			int blockSize,
			 w2xconv_rgb_float3 background,
			 bool png_rgb,
			 bool dst_png){
				
	bool is_rgb = (conv->impl->scale2_models[0]->getNInputPlanes() == 3);
	enum w2xc::image_format fmt;

	int src_depth = CV_MAT_DEPTH(image_src.type());
	int src_cn = CV_MAT_CN(image_src.type());
	cv::Mat image = cv::Mat(image_src.size(), CV_32FC3);
	cv::Mat alpha;

	if (is_rgb) {
		if (png_rgb) {
			if (src_cn == 4) {
				// save alpha
				alpha = cv::Mat(image_src.size(), CV_32FC1);
				if (src_depth == CV_16U) {
					preproc_rgba2rgb<unsigned short, 65535, 2, 0>(&image, &alpha, &image_src,
										      background.r, background.g, background.b);
				} else {
					preproc_rgba2rgb<unsigned char, 255, 2, 0>(&image, &alpha, &image_src,
										   background.r, background.g, background.b);
				}
			} else {
				preproc_rgb2rgb<unsigned short, 65535, 2, 0>(&image, &image_src);
			}
		} else {
			preproc_rgb2rgb<unsigned char, 255, 2, 0>(&image, &image_src);
		}
		fmt = w2xc::IMAGE_RGB_F32;
	} else {
		if (png_rgb) {
			if (src_cn == 4) {
				// save alpha
				alpha = cv::Mat(image_src.size(), CV_32FC1);
				if (src_depth == CV_16U) {
					preproc_rgba2yuv<unsigned short, 65535, 2, 0>(&image, &alpha, &image_src,
										      background.r, background.g, background.b);
				} else {
					preproc_rgba2yuv<unsigned char, 255, 2, 0>(&image, &alpha, &image_src,
										   background.r, background.g, background.b);
				}
			} else {
				preproc_rgb2yuv<unsigned short, 65535, 2, 0>(&image, &image_src);
			}
		} else {
			preproc_rgb2yuv<unsigned char, 255, 2, 0>(&image, &image_src);
		}

		fmt = w2xc::IMAGE_Y;
	}
	image_src.release();

	if (denoise_level != -1) {
		apply_denoise(conv, image, denoise_level, blockSize, fmt);
	}

	if (scale != 1.0) {
		// calculate iteration times of 2x scaling and shrink ratio which will use at last
		int iterTimesTwiceScaling = static_cast<int>(std::ceil(std::log2(scale)));
		double shrinkRatio = 0.0;
		if (static_cast<int>(scale)
		    != std::pow(2, iterTimesTwiceScaling))
		{
			shrinkRatio = scale / std::pow(2.0, static_cast<double>(iterTimesTwiceScaling));
		}

		apply_scale(conv, image, iterTimesTwiceScaling, blockSize, fmt);

		if (shrinkRatio != 0.0) {
			cv::Size lastImageSize = image.size();
			lastImageSize.width =
				static_cast<int>(static_cast<double>(lastImageSize.width
								     * shrinkRatio));
			lastImageSize.height =
				static_cast<int>(static_cast<double>(lastImageSize.height
								     * shrinkRatio));
			cv::resize(image, image, lastImageSize, 0, 0, cv::INTER_LINEAR);
		}
	}

	if (alpha.empty() || !dst_png) {
		image_dst = cv::Mat(image.size(), CV_MAKETYPE(src_depth,3));

		if (is_rgb) {
			if (src_depth == CV_16U) {
				postproc_rgb2rgb<unsigned short, 65535, 2, 0>(&image_dst, &image);
			} else {
				postproc_rgb2rgb<unsigned char, 255, 2, 0>(&image_dst, &image);
			}
		} else {
			if (src_depth == CV_16U) {
				postproc_yuv2rgb<unsigned short, 65535, 0, 2>(&image_dst, &image);
			} else {
				postproc_yuv2rgb<unsigned char, 255, 0, 2>(&image_dst, &image);
			}
		}
	} else {
		image_dst = cv::Mat(image.size(), CV_MAKETYPE(src_depth,4));

		if (image.size() != alpha.size()) {
			cv::resize(alpha, alpha, image.size(), 0, 0, cv::INTER_LINEAR);
		}

		if (is_rgb) {
			if (src_depth == CV_16U) {
				postproc_rgb2rgba<unsigned short, 65535, 2, 0>(&image_dst, &image, &alpha, background.r, background.g, background.b);
			} else {
				postproc_rgb2rgba<unsigned char, 255, 2, 0>(&image_dst, &image, &alpha, background.r, background.g, background.b);
			}
		} else {
			if (src_depth == CV_16U) {
				postproc_yuv2rgba<unsigned short, 65535, 0, 2>(&image_dst, &image, &alpha, background.r, background.g, background.b);
			} else {
				postproc_yuv2rgba<unsigned char, 255, 0, 2>(&image_dst, &image, &alpha, background.r, background.g, background.b);
			}
		}
	}
}

int
w2xconv_convert_file(struct W2XConv *conv,
		     const char *dst_path,
                     const char *src_path,
                     int denoise_level,
                     double scale,
		     int blockSize)
{
	double time_start = getsec();

	FILE *png_fp = nullptr;
	png_fp = fopen(src_path, "rb");

	if (png_fp == nullptr) {
		setPathError(conv, W2XCONV_ERROR_IMREAD_FAILED, src_path);
		return -1;
	}

	bool png_rgb;
	//Background colour
	//float3 background(1.0f, 1.0f, 1.0f);
	w2xconv_rgb_float3 background;
	background.r = background.g = background.b = 1.0f;
	get_png_background_colour(png_fp, &png_rgb, &background);

	if (png_fp) {
		fclose(png_fp);
		png_fp = nullptr;
	}

	cv::Mat image_src, image_dst;

	/*
	 * IMREAD_COLOR                 : always BGR
	 * IMREAD_UNCHANGED + png       : BGR or BGRA
	 * IMREAD_UNCHANGED + otherwise : ???
	 */
	if (png_rgb) {
		image_src = cv::imread(src_path, cv::IMREAD_UNCHANGED);
	} else {
		image_src = cv::imread(src_path, cv::IMREAD_COLOR);
	}

	bool dst_png = false;
	{
		size_t len = strlen(dst_path);
		if (len >= 4) {
			if (tolower(dst_path[len-4]) == '.' &&
			    tolower(dst_path[len-3]) == 'p' &&
			    tolower(dst_path[len-2]) == 'n' &&
			    tolower(dst_path[len-1]) == 'g')
			{
				dst_png = true;
			}
		}
	}
	
	w2xconv_convert_mat(conv, image_dst, image_src, denoise_level, scale, blockSize, background, png_rgb, dst_png);
	

	if (!cv::imwrite(dst_path, image_dst)) {
		setPathError(conv,
			     W2XCONV_ERROR_IMWRITE_FAILED,
			     dst_path);
		return -1;
	}

	double time_end = getsec();

	conv->flops.process_sec += time_end - time_start;

	//printf("== %f == \n", conv->impl->env.transfer_wait);

	return 0;
}


#if defined(WIN32) && defined(UNICODE)

cv::Mat read_imageW(const WCHAR* filepath, int flags=cv::IMREAD_COLOR) {
	long lSize;
	char* imgBuffer;
    FILE* pFile = _wfopen(filepath, L"rb");
	
    if (!pFile) {
        return cv::Mat::zeros(1, 1, CV_8U);
    }
	
    fseek(pFile, 0, SEEK_END);
    lSize = ftell(pFile);
    fseek(pFile, 0, SEEK_SET);
	
    imgBuffer = new char[lSize];
	
	if(!imgBuffer) {
        return cv::Mat::zeros(1, 1, CV_8U);
	}
	
    fread(imgBuffer, 1, lSize, pFile);
	
    fclose(pFile);
	
    cv::_InputArray arr(imgBuffer, lSize);
    cv::Mat img = cv::imdecode(arr, flags);
	
    delete[] imgBuffer;
    return img;
}

bool write_imageW(const WCHAR* filepath, cv::Mat& img, std::vector<int> param = std::vector<int>(0))
{
	FILE* pFile;
	std::vector<uchar> imageBuffer;
	std::wstring ext = std::wstring(filepath);
	ext = ext.substr(ext.find_last_of(L'.'));
	
	if(!cv::imencode(to_mbs(ext).c_str(),img, imageBuffer, param))
		return false;
	
    pFile = _wfopen(filepath, L"wb+");
    if (!pFile)
        return false;
	
	fwrite(imageBuffer.data(), sizeof(unsigned char), imageBuffer.size(), pFile);
	
    fclose(pFile);
    return true;
}

int
w2xconv_convert_fileW(struct W2XConv *conv,
		     const WCHAR *dst_path,
                     const WCHAR *src_path,
                     int denoise_level,
                     double scale,
		     int blockSize)
{
	double time_start = getsec();

	FILE *png_fp = nullptr;
	png_fp = _wfopen(src_path, L"rb");

	if (png_fp == nullptr) {
		setPathError(conv, W2XCONV_ERROR_IMREAD_FAILED, src_path);
		return -1;
	}

	bool png_rgb;
	//Background colour
	//float3 background(1.0f, 1.0f, 1.0f);
	w2xconv_rgb_float3 background;
	background.r = background.g = background.b = 1.0f;
	get_png_background_colour(png_fp, &png_rgb, &background);

	if (png_fp) {
		fclose(png_fp);
		png_fp = nullptr;
	}

	cv::Mat image_src, image_dst;

	/*
	 * IMREAD_COLOR                 : always BGR
	 * IMREAD_UNCHANGED + png       : BGR or BGRA
	 * IMREAD_UNCHANGED + otherwise : ???
	 */
	if (png_rgb) {
		image_src = read_imageW(src_path, cv::IMREAD_UNCHANGED);
	} else {
		image_src = read_imageW(src_path, cv::IMREAD_COLOR);
	}

	bool dst_png = false;
	{
		size_t len = wcslen(dst_path);
		if (len >= 4) {
			if (towlower(dst_path[len-4]) == L'.' &&
			    towlower(dst_path[len-3]) == L'p' &&
			    towlower(dst_path[len-2]) == L'n' &&
			    towlower(dst_path[len-1]) == L'g')
			{
				dst_png = true;
			}
		}
	}

	w2xconv_convert_mat(conv, image_dst, image_src, denoise_level, scale, blockSize, background, png_rgb, dst_png);

	if (!write_imageW(dst_path, image_dst)) {
		setPathError(conv,
			     W2XCONV_ERROR_IMWRITE_FAILED,
			     dst_path);
		return -1;
	}

	double time_end = getsec();

	conv->flops.process_sec += time_end - time_start;

	//printf("== %f == \n", conv->impl->env.transfer_wait);

	return 0;
}
#endif


static void
convert_mat(struct W2XConv *conv,
	    cv::Mat &image,
	    int denoise_level,
	    double scale,
	    int dst_w, int dst_h,
	    int blockSize,
	    enum w2xc::image_format fmt)
{
	if (denoise_level != -1) {
		apply_denoise(conv, image, denoise_level, blockSize, fmt);
	}

	if (scale != 1.0) {
		// calculate iteration times of 2x scaling and shrink ratio which will use at last
		int iterTimesTwiceScaling = static_cast<int>(std::ceil(std::log2(scale)));
		double shrinkRatio = 0.0;
		if (static_cast<int>(scale)
		    != std::pow(2, iterTimesTwiceScaling))
		{
			shrinkRatio = scale / std::pow(2.0, static_cast<double>(iterTimesTwiceScaling));
		}

		apply_scale(conv, image, iterTimesTwiceScaling, blockSize, fmt);

		if (shrinkRatio != 0.0) {
			cv::Size lastImageSize = image.size();
			lastImageSize.width = dst_w;
			lastImageSize.height = dst_h;
			cv::resize(image, image, lastImageSize, 0, 0, cv::INTER_LINEAR);
		}
	}
}


int
w2xconv_convert_rgb(struct W2XConv *conv,
		    unsigned char *dst, size_t dst_step_byte, /* rgb24 (src_w*ratio, src_h*ratio) */
		    unsigned char *src, size_t src_step_byte, /* rgb24 (src_w, src_h) */
		    int src_w, int src_h,
		    int denoise_level, /* 0:none, 1:L1 denoise, other:L2 denoise  */
		    double scale,
		    int block_size)
{
	int dst_h = (int) (src_h * scale);
	int dst_w = (int) (src_w * scale);

	cv::Mat srci(src_h, src_w, CV_8UC3, src, src_step_byte);
	cv::Mat dsti(dst_h, dst_w, CV_8UC3, dst, dst_step_byte);

	cv::Mat image;
	bool is_rgb = (conv->impl->scale2_models[0]->getNInputPlanes() == 3);

	if (is_rgb) {
		srci.copyTo(image);
		convert_mat(conv, image, denoise_level, scale, dst_w, dst_h, block_size, w2xc::IMAGE_RGB);
		image.copyTo(dsti);
	} else {
		srci.convertTo(image, CV_32F, 1.0 / 255.0);
		cv::cvtColor(image, image, cv::COLOR_RGB2YUV);
		convert_mat(conv, image, denoise_level, scale, dst_w, dst_h, block_size, w2xc::IMAGE_Y);

		cv::cvtColor(image, image, cv::COLOR_YUV2RGB);
		image.convertTo(dsti, CV_8U, 255.0);
	}

	return 0;
}

int
w2xconv_convert_rgb_f32(struct W2XConv *conv,
			unsigned char *dst, size_t dst_step_byte, /* rgb float32x3 normalized[0-1] (src_w*ratio, src_h*ratio) */
			unsigned char *src, size_t src_step_byte, /* rgb float32x3 normalized[0-1] (src_w, src_h) */
			int src_w, int src_h,
			int denoise_level, /* 0:none, 1:L1 denoise, other:L2 denoise  */
			double scale,
			int block_size)
{
	bool is_rgb = (conv->impl->scale2_models[0]->getNInputPlanes() == 3);

	if (!is_rgb) {
		setError(conv, W2XCONV_ERROR_Y_MODEL_MISMATCH_TO_RGB_F32);
		return -1;
	}

	int dst_h = (int) (src_h * scale);
	int dst_w = (int) (src_w * scale);

	cv::Mat srci(src_h, src_w, CV_32FC3, src, src_step_byte);
	cv::Mat dsti(dst_h, dst_w, CV_32FC3, dst, dst_step_byte);

	cv::Mat image;

	srci.copyTo(image);
	convert_mat(conv, image, denoise_level, scale, dst_w, dst_h, block_size, w2xc::IMAGE_RGB_F32);
	image.copyTo(dsti);

	return 0;
}


int
w2xconv_convert_yuv(struct W2XConv *conv,
		    unsigned char *dst, size_t dst_step_byte, /* float32x3 (src_w*ratio, src_h*ratio) */
		    unsigned char *src, size_t src_step_byte, /* float32x3 (src_w, src_h) */
		    int src_w, int src_h,
		    int denoise_level, /* 0:none, 1:L1 denoise, other:L2 denoise  */
		    double scale,
		    int block_size)
{
	int dst_h = (int) (src_h * scale);
	int dst_w = (int) (src_w * scale);

	bool is_rgb = (conv->impl->scale2_models[0]->getNInputPlanes() == 3);
	if (is_rgb) {
		setError(conv, W2XCONV_ERROR_RGB_MODEL_MISMATCH_TO_Y);
		return -1;
	}

	cv::Mat srci(src_h, src_w, CV_32FC3, src, src_step_byte);
	cv::Mat dsti(dst_h, dst_w, CV_32FC3, dst, dst_step_byte);

	cv::Mat image = srci.clone();

	convert_mat(conv, image, denoise_level, scale, dst_w, dst_h, block_size, w2xc::IMAGE_Y);

	image.copyTo(dsti);

	return 0;
}
#endif // HAVE_OPENCV

int
w2xconv_apply_filter_y(struct W2XConv *conv,
		       enum W2XConvFilterType type,
		       unsigned char *dst, size_t dst_step_byte, /* float32x1 (src_w, src_h) */
		       unsigned char *src, size_t src_step_byte, /* float32x1 (src_w, src_h) */
		       int src_w, int src_h,
		       int blockSize)
{
	bool is_rgb = (conv->impl->scale2_models[0]->getNInputPlanes() == 3);
	if (is_rgb) {
		setError(conv, W2XCONV_ERROR_RGB_MODEL_MISMATCH_TO_Y);
		return -1;
	}

	struct W2XConvImpl *impl = conv->impl;
	ComputeEnv *env = &impl->env;

	W2Mat dsti(src_w, src_h, CV_32FC1, dst, (int) dst_step_byte);
	W2Mat srci(src_w, src_h, CV_32FC1, src, (int) src_step_byte);

	std::vector<std::unique_ptr<w2xc::Model> > *mp = NULL;

	switch (type) {
	case W2XCONV_FILTER_DENOISE0:
		mp = &impl->noise0_models;
		break;
		
	case W2XCONV_FILTER_DENOISE1:
		mp = &impl->noise1_models;
		break;

	case W2XCONV_FILTER_DENOISE2:
		mp = &impl->noise2_models;
		break;

	case W2XCONV_FILTER_DENOISE3:
		mp = &impl->noise3_models;
		break;

	case W2XCONV_FILTER_SCALE2x:
		mp = &impl->scale2_models;
		break;

	default:
		return -1;
	}

	W2Mat result;
	w2xc::convertWithModels(conv, env, srci, result,
				*mp,
				&conv->flops, blockSize, w2xc::IMAGE_Y, conv->enable_log);

	for (int yi=0; yi<src_h; yi++) {
		char *d0 = dsti.ptr<char>(yi);
		char *s0 = result.ptr<char>(yi);

		memcpy(d0, s0, src_w * sizeof(float));
	}

	return 0;
}

#ifdef HAVE_OPENCV
int
w2xconv_test(struct W2XConv *conv, int block_size)
{
	int w = 200;
	int h = 100;
	int r;

	cv::Mat src_rgb = cv::Mat::zeros(h, w, CV_8UC3);
	cv::Mat dst = cv::Mat::zeros(h, w, CV_8UC3);

	cv::line(src_rgb,
		 cv::Point(10, 10),
		 cv::Point(20, 20),
		 cv::Scalar(255,0,0), 8);

	cv::line(src_rgb,
		 cv::Point(20, 10),
		 cv::Point(10, 20),
		 cv::Scalar(0,255,0), 8);

	cv::line(src_rgb,
		 cv::Point(50, 30),
		 cv::Point(10, 30),
		 cv::Scalar(0,0,255), 1);

	cv::line(src_rgb,
		 cv::Point(50, 80),
		 cv::Point(10, 80),
		 cv::Scalar(255,255,255), 3);

	cv::Mat src_32fc3;
	cv::Mat src_yuv;

	src_rgb.convertTo(src_32fc3, CV_32F, 1.0 / 255.0);
	cv::cvtColor(src_32fc3, src_yuv, cv::COLOR_RGB2YUV);

	cv::Mat dst_rgb_x2(h*2, w*2, CV_8UC3);
	cv::Mat dst_rgb_f32_x2(h*2, w*2, CV_32FC3);
	cv::Mat dst_yuv_x2(h*2, w*2, CV_32FC3);

	cv::imwrite("test_src.png", src_rgb);

	w2xconv_convert_rgb(conv,
			    dst_rgb_x2.data, dst_rgb_x2.step[0],
			    src_rgb.data, src_rgb.step[0],
			    w, h,
			    1,
			    2.0,
			    block_size);

	cv::imwrite("test_rgb.png", dst_rgb_x2);


	w2xconv_convert_rgb_f32(conv,
				dst_rgb_f32_x2.data, dst_rgb_f32_x2.step[0],
				src_32fc3.data, src_32fc3.step[0],
				w, h,
				1,
				2.0,
				block_size);

	dst_rgb_f32_x2.convertTo(dst_rgb_x2, CV_8U, 255.0);
	cv::imwrite("test_rgb_f32.png", dst_rgb_x2);

	r = w2xconv_convert_yuv(conv,
				dst_yuv_x2.data, dst_yuv_x2.step[0],
				src_yuv.data, src_yuv.step[0],
				w, h,
				1,
				2.0,
				block_size);

	if (r < 0) {
		char *e = w2xconv_strerror(&conv->last_error);
		puts(e);
		w2xconv_free(e);
	} else {
		cv::cvtColor(dst_yuv_x2, dst_yuv_x2, cv::COLOR_YUV2RGB);
		dst_yuv_x2.convertTo(dst_rgb_x2, CV_8U, 255.0);
		cv::imwrite("test_yuv.png", dst_rgb_x2);
	}

	std::vector<cv::Mat> imageSplit;
	cv::split(src_yuv, imageSplit);
	cv::Mat split_src = imageSplit[0].clone();
	cv::Mat split_dst, dst_rgb;
	cv::Mat split_dsty(h, w, CV_32F);

	r = w2xconv_apply_filter_y(conv,
				   W2XCONV_FILTER_DENOISE1,
				   split_dsty.data, split_dsty.step[0],
				   split_src.data, split_src.step[0],
				   w, h, block_size);
	if (r < 0) {
		char *e = w2xconv_strerror(&conv->last_error);
		puts(e);
		w2xconv_free(e);
	} else {
		imageSplit[0] = split_dsty.clone();

		cv::merge(imageSplit, split_dst);

		cv::cvtColor(split_dst, split_dst, cv::COLOR_YUV2RGB);
		split_dst.convertTo(dst_rgb, CV_8U, 255.0);

		cv::imwrite("test_apply.png", dst_rgb);
	}

	return 0;
}
#endif
