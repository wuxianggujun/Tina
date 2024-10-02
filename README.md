Tina
----------
[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/c%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B#Standardization)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://opensource.org/licenses/MIT)

This is a simple game engine implemented using a large number of third-party libraries. I just want to finish a real project.

## Usage
1.Clone the repository using the following command:`git clone https://github.com/wuxianggujun/Tina.git`

2.Initialize the submodule using `git submodule update --init --recursive`

3.I found that running this example with WSL2+CLion can open the window directly without entering the Ubuntu window program to click on the compiled binary file. But after trying to use Visual Studio, it didn't work. I don't understand if there is a problem?

### Ubuntu Build
Ubuntu requires cmake and ninja to be installed, as well as a C++ development environment
For Ubuntu compilation, you need to install the minimum version of git >=2.35,You can use the command below to install the latest version
```shell
sudo add-apt-repository ppa:git-core/ppa
sudo apt update
sudo apt install git
sudo apt install ninja-build
```

```shell
sudo snap install cmake --classic
sudo ln -s /snap/cmake/current/bin/cmake /usr/bin/cmake
sudo ln -s /snap/cmake/current/bin/ccmake /usr/bin/ccmake
sudo ln -s /snap/cmake/current/bin/cpack /usr/bin/cpack
```

```shell
sudo apt install libgl1-mesa-dev
sudo apt-get install libglfw3-dev
sudo apt-get install pkg-config
sudo apt install libwayland-dev libxkbcommon-dev xorg-dev
sudo apt-get install libx11-dev libxext-dev libxtst-dev libxrender-dev libxmu-dev libxmuu-dev

```
## Build And Run
```mkdir build & cd build
cmake ..
make
./Tina
```

## Dependencies

 * [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake.git) :  Cross-platform, graphics API agnostic, "Bring Your Own Engine/Framework" style rendering library.        
 * [glfw](https://github.com/glfw/glfw.git)   :                 A multi-platform library for OpenGL, OpenGL ES, Vulkan, window and input.       
