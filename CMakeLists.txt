cmake_minimum_required(VERSION 3.8)

project(waifu2xcpp)
include("CMakeDependentOption")
include("CheckIncludeFileCXX")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "5.0")
		message(FATAL_ERROR "This software requires GCC 5 or higher")
	endif()
endif()

string(TIMESTAMP TS)
add_definitions(-DBUILD_TS="${TS}")

find_program(FOUND_GIT "git")

if (FOUND_GIT STREQUAL "FOUND_GIT-NOTFOUND")
	set(GIT_BRANCH "null")
	set(GIT_COMMIT_HASH "000000")
	set(GIT_TAG "v0.0.0")
	message(WARNING "Git not found, using placeholder version: ${GIT_TAG} (${GIT_BRANCH}-${GIT_COMMIT_HASH})")
else()
    execute_process(COMMAND git rev-parse --abbrev-ref HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)
	execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_COMMIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE)
	execute_process(COMMAND git describe --tags --abbrev=0 HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE GIT_TAG OUTPUT_STRIP_TRAILING_WHITESPACE)
	message(STATUS "Found git, set version to: ${GIT_TAG} (${GIT_BRANCH}-${GIT_COMMIT_HASH})")
endif()

add_definitions(-DGIT_BRANCH="${GIT_BRANCH}")
add_definitions(-DGIT_COMMIT_HASH="${GIT_COMMIT_HASH}")
add_definitions(-DGIT_TAG="${GIT_TAG}")

CMAKE_DEPENDENT_OPTION(FORCE_AMD "Whether to force detection of AMDSDK" OFF "NOT FORCE_CUDA" OFF) # When you have CUDASDK and AMDSDK installed but want to detect AMD primarly, set this ON
CMAKE_DEPENDENT_OPTION(FORCE_CUDA "Whether to force detection of CUDASDK" OFF "NOT FORCE_AMD" OFF) # When you have CUDASDK and AMDSDK installed but want to detect CUDA primarly, set this ON
CMAKE_DEPENDENT_OPTION(FORCE_DUAL "Whether to build for both AMD and NVidia" OFF "NOT FORCE_AMD;NOT FORCE_CUDA" OFF)

set(LOCAL_SYS_TYPE "unknown")
if (MINGW)
	set(LOCAL_SYS_TYPE "MinGW")
endif()
if (UNIX)
	set(LOCAL_SYS_TYPE "Linux")
endif()
if (APPLE)
	set(LOCAL_SYS_TYPE "Apple")
endif()
if (MSVC)
	set(LOCAL_SYS_TYPE "MSVC")
endif()
message(STATUS "System is: ${CMAKE_SYSTEM_NAME} (${LOCAL_SYS_TYPE})")

if(APPLE)
	set(CMAKE_C_COMPILER "/usr/local/opt/llvm/bin/clang")
	set(CMAKE_CXX_COMPILER "/usr/local/opt/llvm/bin/clang++")
	set(CMAKE_EXE_LINKER_FLAGS "-L/usr/local/opt/llvm/lib -Wl,-rpath,/usr/local/opt/llvm/lib")
	set(CMAKE_SHARED_LINKER_FLAGS "-L/usr/local/opt/llvm/lib -Wl,-rpath,/usr/local/opt/llvm/lib")
	set(FILE_SYSTEM_LIB "c++fs")
elseif(UNIX OR MINGW)
	set(FILE_SYSTEM_LIB "stdc++fs")
endif()

add_executable(conv conv.c)
if(UNIX)
	set(OPENCV_PREFIX "/usr" CACHE FILEPATH "OpenCV path")
	find_package(OpenCV)
	if(NOT OPENCV_FOUND)
		find_package(PkgConfig REQUIRED)
		pkg_check_modules(OPENCV opencv)
	endif()
	if(OPENCV_FOUND)
		set(HAVE_OPENCV TRUE)
		set(CMAKE_REQUIRED_INCLUDES ${OpenCV_INCLUDE_DIR})
		include_directories(${OpenCV_INCLUDE_DIR})
		CHECK_INCLUDE_FILE_CXX("${OPENCV_PREFIX}/include/opencv2/opencv.hpp" HAVE_OPENCV)
		if (HAVE_OPENCV)
			link_directories(${OPENCV_PREFIX}/lib)
		endif()
	endif()
elseif(WIN32)
	 if (DEFINED OPENCV_PREFIX)
	 	set(OpenCV_DIR "${OPENCV_PREFIX}" CACHE FILEPATH "OpenCV 3.x path")
	 endif()
	 find_package(OpenCV)
	 if(${OPENCV_FOUND})
	 	set(OPENCV_WINDOWS_LIBRARY "opencv_world${OpenCV_VERSION_MAJOR}${OpenCV_VERSION_MINOR}${OpenCV_VERSION_PATCH}") # Kind of dirty, but works (as long they don't change their naming that is..)
		include_directories(${OPENCV_PREFIX}/include)
		link_directories(${OPENCV_PREFIX}/lib)
		set(HAVE_OPENCV TRUE)
	 else()
		set(OPENCV_PREFIX "opencv" CACHE FILEPATH "OpenCV 3.x path") # No real default path for windows, assume subfolder in source folder
		include_directories(${OPENCV_PREFIX}/include)
		link_directories(${OPENCV_PREFIX}/lib)
		#set(CMAKE_REQUIRED_INCLUDES ${OPENCV_PREFIX}/include)
		#CHECK_INCLUDE_FILE_CXX("${OPENCV_PREFIX}/include/opencv2/opencv.hpp" HAVE_OPENCV)
		if(EXISTS "${OPENCV_PREFIX}/include/opencv2/opencv.hpp")
			set(HAVE_OPENCV TRUE)
			message(STATUS "Found OpenCV")
			set(OPENCV_WINDOWS_LIBRARY "opencv_world340")
		else()
			message(FATAL_ERROR "Could not find OpenCV")
		endif()
	endif()
endif()

find_library(IMGCODECS_LIBRARY NAMES opencv_imgcodecs opencv_highgui HINTS ${OPENCV_PREFIX}/lib)

if(HAVE_OPENCV)
	add_definitions(-DHAVE_OPENCV)
	set(OPENCV_LIBRARIES opencv_core opencv_imgproc ${IMGCODECS_LIBRARY} opencv_features2d)
endif()

if(FORCE_AMD)
	message(STATUS "CUDA Disabled with -DFORCE-AMD=ON")
else()
	find_package(CUDA)
endif()

if(BUILD_UNICODE)
	add_definitions(-DUNICODE)
	add_definitions(-D_UNICODE)
	message(STATUS "Building with UNICODE")
endif()

if(WIN32)
	if(FORCE_AMD)
		set(FindAMDCL_DIR ".")
		find_package(FindAMDCL REQUIRED)
		message(STATUS "OpenCL: Use AMD: ${OpenCL_INCLUDE_DIRS} ${OpenCL_LIBRARIES}")
	elseif(FORCE_CUDA)
		set(FindCUDACL_DIR ".")
		find_package(FindCUDACL REQUIRED)
		message(STATUS "OpenCL: Use nVidia: ${OpenCL_INCLUDE_DIRS} ${OpenCL_LIBRARIES}")
	elseif(FORCE_DUAL)
		set(FindAMDCL_DIR ".")
		find_package(FindAMDCL REQUIRED)
		message(STATUS "OpenCL: Use dual mode: ${OpenCL_INCLUDE_DIRS} ${OpenCL_LIBRARIES}")
	else()
		find_package(OpenCL REQUIRED)
	endif()
	include_directories(${OpenCL_INCLUDE_DIRS})
endif()

if(CUDA_FOUND)
	add_definitions(-DHAVE_CUDA)
	include_directories(${CUDA_TOOLKIT_INCLUDE})
else()
	message(STATUS "CUDA not found. disabled.")
endif()

include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR})

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	if(MINGW)
		set(OCV_COMPILER_DIRNAME "mingw")
	elseif(MSVC11)
		set(OCV_COMPILER_DIRNAME "vc11")
	elseif(MSVC14)
		set(OCV_COMPILER_DIRNAME "vc14")
	else()
		set(OCV_COMPILER_DIRNAME "vc12")
	endif()

	if(MSVC)
		set(CMAKE_CXX_FLAGS_RELEASE "/O2 /MT")
		set(CMAKE_CXX_FLAGS_DEBUG "/Zi /MT")
	endif()

	if(MINGW)
		SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
		SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static")
	endif()

	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(OCV_SYSTEM_DIRNAME "x64")
	else()
		set(OCV_SYSTEM_DIRNAME "x86")
	endif()

	if(MSVC14)
		link_directories(${OPENCV_PREFIX}/${OCV_SYSTEM_DIRNAME}/${OCV_COMPILER_DIRNAME}/lib)
	else()
		link_directories(${OPENCV_PREFIX}/${OCV_SYSTEM_DIRNAME}/${OCV_COMPILER_DIRNAME}/staticlib)
	endif()

endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64" OR
	CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64" OR
	CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR
	CMAKE_SYSTEM_PROCESSOR STREQUAL "x86" OR
	CMAKE_SYSTEM_PROCESSOR MATCHES "^i.86$")
	set(X86_TRUE true)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^aarch|^arm")
	set(ARM_TRUE true)
else()
	set(X86_TRUE false)
endif()

option(X86OPT "Enable X86 SIMD optimizations" ${X86_TRUE})
if(X86OPT)
	add_definitions(-DX86OPT)
	set(OPT_SOURCES src/modelHandler_avx.cpp src/modelHandler_fma.cpp src/modelHandler_sse.cpp)
endif()

option(ARMOPT "Enable ARM SIMD optimizations" ${ARM_TRUE})
if(ARMOPT)
	add_definitions(-DARMOPT)
	set(OPT_SOURCES src/modelHandler_neon.cpp)

	include(CheckCXXCompilerFlag)
	# -mfloat-abi=hard and -mfloat-abi=softfp cannot be mixed
	# -mfpu=neon and -march=armv7-a are not supported on armv8
	foreach(flag -mfloat-abi=hard -mfloat-abi=softfp -mfpu=neon -march=armv7-a -lstdc++fs)
		string(REGEX REPLACE "[-=]" "_" flagp ${flag})
		check_cxx_compiler_flag(${flag} ${flagp})
		if(${flagp} AND NOT ${flag} MATCHES "-march")
			set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}")
		endif()
	endforeach()
endif()


add_library(w2xc SHARED
	src/modelHandler.cpp ${OPT_SOURCES}
	src/modelHandler_OpenCL.cpp src/convertRoutine.cpp src/threadPool.cpp
	src/modelHandler_CUDA.cpp src/w2xconv.cpp src/common.cpp src/wcsfunc.cpp
	src/cvwrap.cpp
	src/Env.cpp src/Buffer.cpp
)

add_dependencies(w2xc gensrcs)


if(CMAKE_COMPILER_IS_GNUCXX OR ${CMAKE_CXX_COMPILER_ID} MATCHES "^(Apple)?Clang$")
	set(CMAKE_CXX_STANDARD 17)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")

	if (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
		set(REMOVE_SYMBOL "")
	else()
		set(REMOVE_SYMBOL "-s")
	endif()

	set(CMAKE_CXX_FLAGS_RELEASE "-O2 ${REMOVE_SYMBOL}")

	set_source_files_properties(src/modelHandler_avx.cpp PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS} -O2 -mavx ${REMOVE_SYMBOL}")
	set_source_files_properties(src/modelHandler_fma.cpp PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS} -O2 -mfma ${REMOVE_SYMBOL}")
	set_source_files_properties(src/modelHandler_sse.cpp PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS} -O2 -save-temps -msse3 ${REMOVE_SYMBOL}")
	if(_march_armv7_a)
		set_source_files_properties(src/modelHandler_neon.cpp PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS} -O2 -march=armv7-a ${REMOVE_SYMBOL}")
	endif()
elseif(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4819 /wd4996 /wd4800")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4819 /wd4996 /wd4800")
	set_source_files_properties(src/modelHandler_avx.cpp PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS} /O2 /arch:AVX")
	set_source_files_properties(src/modelHandler_fma.cpp PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS} /O2 /arch:AVX2")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
	list(APPEND CUDA_NVCC_FLAGS "-m32")
endif()

if(WIN32)
	if(CUDA_FOUND)
		list(APPEND CUDA_NVCC_FLAGS "-Xcompiler" "/wd 4819")
	endif()
	set_target_properties(w2xc PROPERTIES PREFIX "")
	set_target_properties(w2xc PROPERTIES IMPORT_PREFIX "")
	set_target_properties(w2xc PROPERTIES IMPORT_SUFFIX ".lib")
endif()

if(WIN32) # We build FOR windows.
	if(MINGW) # We build for windows on Linux MinGW (Cross compiling) # not yet working
		target_link_libraries(w2xc opencv_core300 opencv_imgproc300 opencv_imgcodecs300 opencv_features2d300 libjpeg libpng libtiff libjasper opencv_hal300 IlmImf zlib libwebp user32 opencv_core300 opencv_hal300) # xx 300
	elseif(MSVC) # we build natively on Visual studio
		target_link_libraries(w2xc ${OPENCV_WINDOWS_LIBRARY} user32)
	elseif(UNIX) # we build on linux.... on windows.. somehow WSL?/Cygwin? We'll see
		target_link_libraries(w2xc opencv_core300 opencv_imgproc300 opencv_imgcodecs300 opencv_features2d300 ippicvmt libjpeg libpng libtiff libjasper opencv_hal300 IlmImf zlib libwebp user32 opencv_core300 opencv_imgproc300)
	endif()
	add_executable(w2xcr WIN32 w32-apps/w2xcr.c w32-apps/w2xcr.rc)
	target_link_libraries(w2xcr w2xc user32 shlwapi gdi32)
elseif(APPLE)
	target_link_libraries(w2xc ${OpenCV_LIBS} ${CMAKE_DL_LIBS} pthread c++ c++fs)
else() # We are on linux and build for linux
	target_link_libraries(w2xc ${OpenCV_LIBS} ${CMAKE_DL_LIBS} pthread stdc++fs stdc++)
endif()

set(CONV_EXE "$<TARGET_FILE_DIR:conv>/conv")

add_custom_command(
	OUTPUT modelHandler_OpenCL.cl.h
	COMMAND ${CONV_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/src/modelHandler_OpenCL.cl ${CMAKE_CURRENT_BINARY_DIR}/modelHandler_OpenCL.cl.h str
	DEPENDS src/modelHandler_OpenCL.cl conv
)

set(GPU_CODE modelHandler_OpenCL.cl.h)

if(CUDA_FOUND)
    if (CUDA_VERSION_MAJOR LESS 9)
        add_custom_command(
            OUTPUT modelHandler_CUDA.ptx20.h
            COMMAND ${CONV_EXE} modelHandler_CUDA.ptx20 modelHandler_CUDA.ptx20.h str
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/modelHandler_CUDA.ptx20 conv
        )
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/modelHandler_CUDA.ptx20
            COMMAND ${CUDA_NVCC_EXECUTABLE} ${CUDA_NVCC_FLAGS} -arch=sm_20 -ptx -o ${CMAKE_CURRENT_BINARY_DIR}/modelHandler_CUDA.ptx20 ${CMAKE_CURRENT_SOURCE_DIR}/src/modelHandler_CUDA.cu
            DEPENDS src/modelHandler_CUDA.cu
        )
    endif()
    add_custom_command(
        OUTPUT modelHandler_CUDA.ptx30.h
        COMMAND ${CONV_EXE} modelHandler_CUDA.ptx30 modelHandler_CUDA.ptx30.h str
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/modelHandler_CUDA.ptx30 conv
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/modelHandler_CUDA.ptx30
        COMMAND ${CUDA_NVCC_EXECUTABLE} ${CUDA_NVCC_FLAGS} -arch=sm_30 -ptx -o ${CMAKE_CURRENT_BINARY_DIR}/modelHandler_CUDA.ptx30 ${CMAKE_CURRENT_SOURCE_DIR}/src/modelHandler_CUDA.cu
        DEPENDS src/modelHandler_CUDA.cu
    )
    if (CUDA_VERSION_MAJOR LESS 9)
        set(GPU_CODE ${GPU_CODE} modelHandler_CUDA.ptx30.h modelHandler_CUDA.ptx20.h)
    else()
        set(GPU_CODE ${GPU_CODE} modelHandler_CUDA.ptx30.h)
    endif()
endif()

add_custom_target(gensrcs ALL DEPENDS ${GPU_CODE})
enable_testing()

if (${HAVE_OPENCV})
	add_executable(waifu2x-converter-cpp src/main.cpp src/wcsfunc.cpp)

	if(MSVC)
		target_link_libraries(waifu2x-converter-cpp LINK_PUBLIC w2xc)
	elseif(MINGW)
		target_link_libraries(waifu2x-converter-cpp LINK_PUBLIC w2xc ${FILE_SYSTEM_LIB})
	elseif(UNIX)
		target_link_libraries(waifu2x-converter-cpp LINK_PUBLIC w2xc ${FILE_SYSTEM_LIB})
	endif()

	add_executable(runtest w32-apps/runtest.c)
	if(WIN32)
		target_link_libraries(runtest w2xc ${FILE_SYSTEM_LIB})
	elseif(APPLE)
		target_link_libraries(runtest w2xc ${FILE_SYSTEM_LIB} c++)
	else()
		target_link_libraries(runtest w2xc ${FILE_SYSTEM_LIB} stdc++)
	endif()
	add_test(runtest runtest)
	install(TARGETS w2xc waifu2x-converter-cpp RUNTIME DESTINATION bin LIBRARY DESTINATION "lib${LIB_SUFFIX}" ARCHIVE DESTINATION "lib${LIB_SUFFIX}" )
endif()

add_executable(runbench w32-apps/runbench.c)
if(WIN32)
	target_link_libraries(runbench LINK_PUBLIC w2xc ${FILE_SYSTEM_LIB})
elseif(APPLE)
	target_link_libraries(runbench LINK_PUBLIC w2xc ${FILE_SYSTEM_LIB} c++)
else()
	target_link_libraries(runbench LINK_PUBLIC w2xc ${FILE_SYSTEM_LIB} stdc++)
endif()
install(FILES src/w2xconv.h DESTINATION include)

# Better add support for multiple directories where to find models
# with paths separated by : (a la PATH) then install unconditionally
if(WIN32)
	option(INSTALL_MODELS "Install waifu2x RGB models" false)
else()
	option(INSTALL_MODELS "Install waifu2x RGB models" true)
endif()
	
if(INSTALL_MODELS)
	add_definitions(-DDEFAULT_MODELS_DIRECTORY="${CMAKE_INSTALL_PREFIX}/share/waifu2x-converter-cpp")
	install(DIRECTORY models_rgb/ DESTINATION share/waifu2x-converter-cpp)
endif()

option(INSTALL_DOCS "Install developer documentation" false)
if(INSTALL_DOCS)
	install(DIRECTORY appendix docs/internals DESTINATION share/doc/waifu2x-converter-cpp)
endif()
