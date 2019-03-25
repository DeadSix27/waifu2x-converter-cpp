[![Discord](https://img.shields.io/badge/Discord-Join-blue.svg)](https://discord.gg/gAvufS2) :: [![Downloads522](https://img.shields.io/github/downloads/DeadSix27/waifu2x-converter-cpp/latest/total.svg)](https://github.com/DeadSix27/waifu2x-converter-cpp/releases) :: [![TotalDownloads](https://img.shields.io/github/downloads/DeadSix27/waifu2x-converter-cpp/total.svg)](https://github.com/DeadSix27/waifu2x-converter-cpp/releases)

# waifu2x (converter only version)

This is a reimplementation of waifu2x ([original](https://github.com/nagadomi/waifu2x)) converter function, in C++, using OpenCV.
This is also a reimplementation of [waifu2x python version](https://marcan.st/transf/waifu2x.py) by [Hector Martin](https://marcan.st/blog/).
You can use this as command-line tool of image noise reduction or/and scaling.

This software was originally made by @WL-Amigo and has been improved a lot over the years, see [FORK_CHANGES.md](FORK_CHANGES.md) for more info on that.

## I also have a Discord at:

https://discord.gg/gAvufS2 It's often easier to chat in real time, so if you want feel free to join.

## Obtain It Here

- **Windows downloads**
  - https://github.com/DeadSix27/waifu2x-converter-cpp/releases

- **AUR (Arch)**
  - [waifu2x-converter-cpp-git](https://aur.archlinux.org/packages/waifu2x-converter-cpp-git/) (git master)
  - [waifu2x-converter-cpp](https://aur.archlinux.org/packages/waifu2x-converter-cpp/) (releaes)
  - These are maintained by [nfnty](https://aur.archlinux.org/account/nfnty). If you have issues with the AUR packages, please contact him.

- **Other Linux**
  - Please build from source. See [BUILDING.md](BUILDING.md) for help.

## Supported platforms

- Linux
- LInux (ARM)
- Windows 7+  
- MacOS?
  - This is not officially supported, but see here for more information: [#20](https://github.com/DeadSix27/waifu2x-converter-cpp/issues/20)

## Build dependencies

- [GCC 5](https://gcc.gnu.org/) (Linux)
- [Visual Studio 2017](https://visualstudio.microsoft.com/downloads/) (Windows)
- [picojson](https://github.com/kazuho/picojson) (included)
- [TCLAP(Templatized C++ Command Line Parser Library)](http://tclap.sourceforge.net/) (included)
- [OpenCV 3+](https://opencv.org/releases.html)

## How to build

See [BUILDING.md](BUILDING.md) for more information.

## Usage

You may also view the help page via executing `waifu2x-converter-cpp.exe --help`.

### --list-opencv-formats

    dump opencv supported format list

### -l,  --list-processor

    dump processor list

### --block_size <integer>

    block size

### --disable-gpu

    disable GPU

### --force-OpenCL

    force to use OpenCL on Intel Platform

### -p <integer>,  --processor <integer>

    set target processor

### -j <integer>,  --jobs <integer>

    number of threads launching at the same time

### --model_dir <string>

    path to custom model directory (don't append last / )

### --scale_ratio <double>

    custom scale ratio

### --noise_level <0|1|2|3>

    noise reduction level

### -m <noise|scale|noise_scale>,  --mode <noise|scale|noise_scale>

    image processing mode

### -q,  --quiet

    Enable quiet mode.

### -r <bool>,  --recursive_directory <bool>

    Search recursively through directories to find more images to process.

    If this is set to 0 it will only check in the directory specified if
    the input is a directory instead of an image.

    You mustn't supply this argument with something other than 0 or 1.

### -o <string>,  --output <string>

    path to output image file or directory  (you should use the full path)

### -i <string>,  --input <string>

    (required)  path to input image file or directory (you should use the
  full path)

### --,  --ignore_rest

    Ignores the rest of the labeled arguments following this flag.

### --version

    Displays version information and exits.

### -h,  --help

    Displays usage information and exits.

## Notes

I'd appreciate any help on this project, I do not want yet another fork... so if you have improvement ideas or find bugs, please make a pull request or open an issue :)!

## A big thanks to these people helping me maintain this fork

- @YukihoAA
- @iame6162013
- https://github.com/DeadSix27/waifu2x-converter-cpp/graphs/contributors
