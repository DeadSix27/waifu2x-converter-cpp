#ifndef W2XCONV_H
#define W2XCONV_H

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32

#ifdef W2XCONV_IMPL
#define W2XCONV_EXPORT __declspec(dllexport)
#else
#define W2XCONV_EXPORT __declspec(dllimport)
#endif


#else

#ifdef W2XCONV_IMPL
#define W2XCONV_EXPORT __attribute__((visibility("default")))
#else
#define W2XCONV_EXPORT
#endif

#endif

enum W2XConvGPUMode {
	W2XCONV_GPU_DISABLE = 0,
	W2XCONV_GPU_AUTO = 1,
	W2XCONV_GPU_FORCE_OPENCL = 2
};

enum W2XConvErrorCode {
	W2XCONV_NOERROR,
	W2XCONV_ERROR_WIN32_ERROR,	/* errno_ = GetLastError() */
	W2XCONV_ERROR_WIN32_ERROR_PATH, /* u.win32_path */
	W2XCONV_ERROR_LIBC_ERROR,	/* errno_ */
	W2XCONV_ERROR_LIBC_ERROR_PATH,	/* libc_path */

	W2XCONV_ERROR_MODEL_LOAD_FAILED, /* u.path */

	W2XCONV_ERROR_IMREAD_FAILED,	/* u.path */
	W2XCONV_ERROR_IMWRITE_FAILED,	/* u.path */

	W2XCONV_ERROR_RGB_MODEL_MISMATCH_TO_Y,
	W2XCONV_ERROR_Y_MODEL_MISMATCH_TO_RGB_F32,

	W2XCONV_ERROR_OPENCL,	/* u.cl_error */
};

struct W2XConvError {
	enum W2XConvErrorCode code;

	union {
		char *path;
		unsigned int errno_;

		struct {
			unsigned int errno_;
			char *path;
		} win32_path;

		struct {
			int errno_;
			char *path;
		} libc_path;

		struct {
			int error_code;
			int dev_id;
		} cl_error;
	}u;
};

W2XCONV_EXPORT char *w2xconv_strerror(struct W2XConvError *e); /* should be free by w2xcvonv_free() */
W2XCONV_EXPORT void w2xconv_free(void *p);

struct W2XConvFlopsCounter {
	double flop;
	double filter_sec;
	double process_sec;
};

enum W2XConvProcessorType {
	W2XCONV_PROC_HOST,
	W2XCONV_PROC_CUDA,
	W2XCONV_PROC_OPENCL
};

enum W2XConvFilterType {
	W2XCONV_FILTER_DENOISE1,
	W2XCONV_FILTER_DENOISE2,
	W2XCONV_FILTER_DENOISE3,
	W2XCONV_FILTER_SCALE2x
};

/* W2XConvProcessor::sub_type */
#define W2XCONV_PROC_HOST_OPENCV 0x0000
#define W2XCONV_PROC_HOST_SSE3 0x0001
#define W2XCONV_PROC_HOST_AVX 0x0002
#define W2XCONV_PROC_HOST_FMA 0x0003

#define W2XCONV_PROC_HOST_NEON 0x0104

#define W2XCONV_PROC_CUDA_NVIDIA 0

#define W2XCONV_PROC_OPENCL_PLATFORM_MASK 0x00ff
#define W2XCONV_PROC_OPENCL_DEVICE_MASK   0xff00

#define W2XCONV_PROC_OPENCL_PLATFORM_NVIDIA 0
#define W2XCONV_PROC_OPENCL_PLATFORM_AMD 1
#define W2XCONV_PROC_OPENCL_PLATFORM_INTEL 2
#define W2XCONV_PROC_OPENCL_PLATFORM_UNKNOWN 0xff

#define W2XCONV_PROC_OPENCL_DEVICE_CPU (1<<8)
#define W2XCONV_PROC_OPENCL_DEVICE_GPU (2<<8)
#define W2XCONV_PROC_OPENCL_DEVICE_UNKNOWN (0xff<<8)

#define W2XCONV_PROC_OPENCL_NVIDIA_GPU (W2XCONV_PROC_OPENCL_PLATFORM_NVIDIA | W2XCONV_PROC_OPENCL_DEVICE_GPU)
#define W2XCONV_PROC_OPENCL_AMD_GPU (W2XCONV_PROC_OPENCL_PLATFORM_AMD | W2XCONV_PROC_OPENCL_DEVICE_GPU)
#define W2XCONV_PROC_OPENCL_INTEL_GPU (W2XCONV_PROC_OPENCL_PLATFORM_INTEL | W2XCONV_PROC_OPENCL_DEVICE_GPU)
#define W2XCONV_PROC_OPENCL_UNKNOWN_GPU (W2XCONV_PROC_OPENCL_PLATFORM_UNKNOWN | W2XCONV_PROC_OPENCL_DEVICE_GPU)

#define W2XCONV_PROC_OPENCL_AMD_CPU (W2XCONV_PROC_OPENCL_PLATFORM_AMD | W2XCONV_PROC_OPENCL_DEVICE_CPU)
#define W2XCONV_PROC_OPENCL_INTEL_CPU (W2XCONV_PROC_OPENCL_PLATFORM_INTEL | W2XCONV_PROC_OPENCL_DEVICE_CPU)
#define W2XCONV_PROC_OPENCL_UNKNOWN_CPU (W2XCONV_PROC_OPENCL_PLATFORM_UNKNOWN | W2XCONV_PROC_OPENCL_DEVICE_CPU)

#define W2XCONV_PROC_OPENCL_UNKNOWN (W2XCONV_PROC_OPENCL_PLATFORM_UNKNOWN | W2XCONV_PROC_OPENCL_DEVICE_UNKNOWN)

struct W2XConvProcessor {
	enum W2XConvProcessorType type;
	int sub_type;
	int dev_id;
	int num_core;
	const char *dev_name;
};

struct W2XConvThreadPool;

struct W2XConv {
	/* public */
	struct W2XConvError last_error;
	struct W2XConvFlopsCounter flops;
	const struct W2XConvProcessor *target_processor;
	int enable_log;

	/* internal */
	struct W2XConvImpl *impl;
};

struct w2xconv_rgb_float3 {
	float r;
	float g;
	float b;
	//float3(){}
	//float3(float r, float g, float b):r(r), g(g), b(b){}
};

//TODO: what does W2XCONV_EXPORT do?
W2XCONV_EXPORT	void get_png_background_colour(FILE *png_fp, bool *png_rgb, struct w2xconv_rgb_float3 *bkgd_colour);

W2XCONV_EXPORT const struct W2XConvProcessor *w2xconv_get_processor_list(int *ret_num);

W2XCONV_EXPORT struct W2XConv *w2xconv_init(enum W2XConvGPUMode gpu,
					    int njob /* 0 = auto */,
					    int enable_log);

W2XCONV_EXPORT struct W2XConv *w2xconv_init_with_processor(int processor_idx,
							   int njob,
							   int enable_log);

/* return negative if failed */
W2XCONV_EXPORT int w2xconv_load_models(struct W2XConv *conv,
				       const char *model_dir);

W2XCONV_EXPORT void w2xconv_set_model_3x3(struct W2XConv *conv,
					  enum W2XConvFilterType m,
					  int layer_depth,
					  int num_input_plane,
					  const int *num_map, // num_map[layer_depth]
					  const float *coef_list, // coef_list[layer_depth][num_map][3x3]
					  const float *bias // bias[layer_depth][num_map]
	);


W2XCONV_EXPORT void w2xconv_fini(struct W2XConv *conv);


W2XCONV_EXPORT int w2xconv_convert_file(struct W2XConv *conv,
					const char *dst_path,
					const char *src_path,
					int denoise_level, /* 0:none, 1:L1 denoise, other:L2 denoise  */
					double scale,
					int block_size);

W2XCONV_EXPORT int w2xconv_convert_rgb(struct W2XConv *conv,
				       unsigned char *dst, size_t dst_step_byte, /* rgb24 (src_w*ratio, src_h*ratio) */
				       unsigned char *src, size_t src_step_byte, /* rgb24 (src_w, src_h) */
				       int src_w, int src_h,
				       int denoise_level, /* 0:none, 1:L1 denoise, other:L2 denoise  */
				       double scale,
				       int block_size);

W2XCONV_EXPORT int w2xconv_convert_rgb_f32(struct W2XConv *conv,
					   unsigned char *dst, size_t dst_step_byte, /* rgb float32x3 normalized[0-1] (src_w*ratio, src_h*ratio) */
					   unsigned char *src, size_t src_step_byte, /* rgb float32x3 normalized[0-1] (src_w, src_h) */
					   int src_w, int src_h,
					   int denoise_level, /* 0:none, 1:L1 denoise, other:L2 denoise  */
					   double scale,
					   int block_size);

W2XCONV_EXPORT int w2xconv_convert_yuv(struct W2XConv *conv,
				       unsigned char *dst, size_t dst_step_byte, /* float32x3 normalized[0-1] (src_w*ratio, src_h*ratio) */
				       unsigned char *src, size_t src_step_byte, /* float32x3 normalized[0-1] (src_w, src_h) */
				       int denoise_level, /* 0:none, 1:L1 denoise, other:L2 denoise  */
				       double scale,
				       int block_size);

W2XCONV_EXPORT int w2xconv_apply_filter_y(struct W2XConv *conv,
					  enum W2XConvFilterType type,
					  unsigned char *dst, size_t dst_step_byte, /* float32x1 normalized[0-1] (src_w, src_h) */
					  unsigned char *src, size_t src_step_byte, /* float32x1 normalized[0-1] (src_w, src_h) */
					  int src_w, int src_h,
					  int block_size);

W2XCONV_EXPORT int w2xconv_test(struct W2XConv *conv, int block_size);

#ifdef __cplusplus
}
#endif

#endif
