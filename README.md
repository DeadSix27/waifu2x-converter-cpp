[![Discord](https://img.shields.io/badge/Discord-Join-blue.svg)](https://discord.gg/jCAFvHM2WK) :: [![Downloads522](https://img.shields.io/github/downloads/DeadSix27/waifu2x-converter-cpp/latest/total.svg)](https://github.com/DeadSix27/waifu2x-converter-cpp/releases) :: [![TotalDownloads](https://img.shields.io/github/downloads/DeadSix27/waifu2x-converter-cpp/total.svg)](https://github.com/DeadSix27/waifu2x-converter-cpp/releases)

# ---------- Work stalled for now ----------
### @YukihoAA has push rights in case anything important comes up, if he finds the time that is.
---

### waifu2x (converter only version)

This is a reimplementation of waifu2x ([original](https://github.com/nagadomi/waifu2x)) converter function, in C++, using OpenCV.
This is also a reimplementation of [waifu2x python version](https://marcan.st/transf/waifu2x.py) by [Hector Martin](https://marcan.st/blog/).
You can use this as command-line tool of image noise reduction or/and scaling.

This software was originally made by @WL-Amigo and has been improved a lot over the years, see [FORK_CHANGES.md](FORK_CHANGES.md) for more info on that.

## Obtain it here:

- #### Windows downloads
  - https://github.com/DeadSix27/waifu2x-converter-cpp/releases
  - Officially supported GUI:
	  - https://github.com/YukihoAA/waifu2x_snowshell/releases

- #### AUR (Arch)
  - [waifu2x-converter-cpp-git](https://aur.archlinux.org/packages/waifu2x-converter-cpp-git/) (git master)
  - [waifu2x-converter-cpp](https://aur.archlinux.org/packages/waifu2x-converter-cpp/) (releaes)
  - These are maintained by [nfnty](https://aur.archlinux.org/account/nfnty). If you have issues with the AUR packages, please contact him.
  
- #### Fedora
  - [waifu2x-converter-cpp](https://apps.fedoraproject.org/packages/waifu2x-converter-cpp)
  - This is maintained by [eclipseo](https://fedoraproject.org/wiki/User:Eclipseo). If you have issues with the Fedora package, please contact him.

- #### NixOS
  - [waifu2x-converter-cpp](https://search.nixos.org/packages?show=waifu2x-converter-cpp&query=waifu2x-converter-cpp)
  - If you have issues with the NixOS package, please create an [issue](https://github.com/NixOS/nixpkgs/issues) on the [nixpkgs repo](https://github.com/NixOS/nixpkgs).

- ####  Other Linux
	 - Please build from source. See [BUILDING.md](BUILDING.md) for help.

## Supported platforms

- Linux
- LInux (ARM)
- Windows 7+
- MacOS?
  - This is not officially supported but see here for more information: [#20](https://github.com/DeadSix27/waifu2x-converter-cpp/issues/20)

## Build dependencies

 - [GCC 5](https://gcc.gnu.org/) (Linux)
 - [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/) (Windows)
 - [picojson](https://github.com/kazuho/picojson) (included)
 - [TCLAP(Templatized C++ Command Line Parser Library)](http://tclap.sourceforge.net/) (included)
 - [OpenCV 3+](https://opencv.org/releases.html)

## How to build

See [BUILDING.md](BUILDING.md) for more information.

## How to Train Own Model

waifu2x-conveter only supports vgg models.
See [nagadomi/waifu2x](https://github.com/nagadomi/waifu2x#train-your-own-model) for more information.

## Usage

Usage of this program can be seen by executing `waifu2x-converter-cpp --help`
If you are on Windows and prefer GUIs, see [here](#windows-downloads).

## Docker

```
docker build -t your-image .
docker run --rm -v $(pwd):/work -it your-image -i /work/noisy.png
```

## Notes:

I'd appreciate any help on this project, I do not want yet another fork... so if you have improvement ideas or find bugs, please make a pull request or open an issue :)!

## A big thanks to these people helping me maintain this fork:

- @YukihoAA
- @iame6162013
- And more: https://github.com/DeadSix27/waifu2x-converter-cpp/graphs/contributors
