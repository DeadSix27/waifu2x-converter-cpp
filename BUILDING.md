
# Waifu2x source building guides

#### You can download pre-built Windows binaries from:

https://github.com/DeadSix27/waifu2x-converter-cpp/releases

#### Index:
- [Windows x64](#windows)
- [Linux](#linux) (ARM is not supported, but could work maybe, open an issue if you get it working.)
- [MacOS](#macos--osx)

---

# Windows

### Requirements:
- [Visual Studio 2017](https://www.visualstudio.com/downloads/) (Community Edition is enough)
  - Make sure to Select these components:
    - `Desktop development with C++`
- CMake 3.13+ [ [cmake-3.13.*-win64-x64.msi](https://cmake.org/download/) ]
  - Select: Add CMake to the system PATH for the current user
- Windows 7 or newer
- [Git for Windows](https://git-scm.com/download/win) (Optional, for the git clone later on)

#### AMD GPUs:
1. Download the AMD APP SDK [ ~~[AMD-SDK-InstallManager-v1.4.87-1.exe](https://developer.amd.com/amd-accelerated-parallel-processing-app-sdk/)~~ ]
	_Note: Currently that link is dead, see [here](https://community.amd.com/thread/228059) for info and [here](https://github.com/DeadSix27/waifu2x-converter-cpp/issues/74) for alternative downloads._

2. Install the AMD SDK  _(you only need `OpenCL` and `OpenCL runtime`)_

#### nVidia GPUs:

1. Download the CUDA SDK 10 [ [cuda_10.*_win10.exe](https://developer.nvidia.com/cuda-downloads?target_os=Windows&target_arch=x86_64&target_version=10&target_type=exelocal) ]

2. Install the CUDA SDK _(you only need to select `CUDA->Development` and `CUDA->Runtime`)_.

#### Building for both GPU brands:

Install both SDK's as shown above, but later add "-DFORCE_DUAL=ON" to the cmake command.

#### Building with UNICODE support:

Add "-DBUILD_UNICODE=ON" to the cmake command.

### Building:
##### We will be using `K:/w2x` as our base folder for this guide.
##### If you want to build for both GPU brands, just install both SDKs (see above).

1. Download OpenCV 4.* [ [opencv-4.*-vc14_vc15.exe](https://opencv.org/releases.html) ]

2. Extract OpenCV to your base folder e.g `K:\w2x\opencv`

3. Open a Command prompt and run the following command: `"C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat" x64`

4. Now run these commands in order, to build w2x:
	```bat
	cd "K:\w2x\"
	git clone "https://github.com/DeadSix27/waifu2x-converter-cpp"
	cd "waifu2x-converter-cpp"
	mkdir out && cd out
	cmake .. -DCMAKE_GENERATOR="Visual Studio 15 2017 Win64" -DOPENCV_PREFIX="K:/w2x/opencv/build/"
	msbuild waifu2xcpp.sln /p:Configuration=Release /p:Platform=x64
	copy K:\w2x\opencv\build\x64\vc15\bin\opencv_world400.dll Release\
	mkdir Release\models_rgb\ && copy ..\models_rgb Release\models_rgb\
	cd ..
	```
5. (Optional) Add to SendTo menu (right click on file in Windows Explorer).

	```bat
	copy /y w32-apps\install.bat out\Release\
	copy /y w32-apps\install.js out\Release\
	copy /y w32-apps\uninstall.bat out\Release\
	copy /y w32-apps\uninstall.js out\Release\
	mkdir out\Release\ExtendedSendTo\ && copy /y w32-apps\ExtendedSendTo\ out\Release\ExtendedSendTo\
	cd out\Release
	install.bat
	cd ExtendedSendTo
	install.wsf
	cd .. && cd .. && cd ..
	```

# Linux

### Requirements:
- GCC 5+
- CMake
- OpenCV 3+

#### Intel GPUs:
- Arch:
  -  sudo pacman -S beignet clinfo
- Ubuntu:
   - sudo apt install beignet-opencl-icd opencl-headers

#### AMD GPUs:
- Arch:
  -  sudo pacman -S opencl-mesa clinfo
- Ubuntu:
   - sudo apt install mesa-opencl-icd opencl-headers

#### nVidia GPUs:
- (?) Open an issue or a PR if you know the exact packages needed for this.

### Building:
##### If you want to build for all GPU brands, just install all packages (untested, see above).
Run these commands in order:
```cmd
git clone "https://github.com/DeadSix27/waifu2x-converter-cpp"
cd waifu2x-converter-cpp
mkdir out && cd out
cmake ..
make -j4
sudo make install
```
If needed run `sudo ldconfig` after the install.

# MacOS / OSX

You need [Homebrew](https://brew.sh) installed, as well as a newer llvm installed (since Xcode's llvm does not have
 filesystem library)

The following has been tested on macOS Mojave 10.14.3:
```
$ brew install llvm opencv
$ git clone https://github.com/DeadSix27/waifu2x-converter-cpp && cd waifu2x-converter-cpp
$ cmake -DOPENCV_PREFIX=/usr/local/Cellar/opencv/<your version here> .
$ make -j4
$ cp -r models_rgb models
```
