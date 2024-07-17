Tina
----------
[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/c%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B#Standardization)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://opensource.org/licenses/MIT)

This is a simple game engine implemented using a large number of third-party libraries. I just want to finish a real project.

## Building

```
git clone https://github.com/wuxianggujun/Tina.git
git submodule update --init --recursive
```

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
 * [spdlog](https://github.com/gabime/spdlog.git)   :           Fast C++ logging library.
 * [stb-cmake](https://github.com/gracicot/stb-cmake.git) :     Single-file public domain (or MIT licensed) libraries for C/C++.       
 * [entt](https://github.com/skypjack/entt.git)  :              Fast and reliable entity-component system (ECS).   
 * [SDL](https://github.com/libsdl-org/SDL.git) : Simple Directmedia Layer.
 * [glfw](https://github.com/glfw/glfw.git)   :                 A multi-platform library for OpenGL, OpenGL ES, Vulkan, window and input.       
 * [glad](https://github.com/Dav1dde/glad.git) :  |Multi-language GL/GLES/EGL/GLX/WGL Loader-Generator based on the official specs.         
 * [glm](https://github.com/g-truc/glm.git)  :                  OpenGL Mathematics (GLM) is a header only C++ mathematics library for graphics software based on the OpenGL Shading Language (GLSL) specifications.        
 * [Google Test](https://github.com/google/googletest.git) :    Googletest is based on the xUnit testing framework, a popular architecture for unit testing.         
 * [tracy](https://github.com/wolfpld/tracy.git)    :           A real time, nanosecond resolution, remote telemetry, hybrid frame and sampling profiler for games and other applications.         


## Learning Resources

- **[Platformer](https://github.com/Somgonk/Platformer)**
- **[MitchEngine](https://github.com/wobbier/MitchEngine)**
- **[CatDogEngine](https://github.com/CatDogEngine/CatDogEngine)**
- **[wallet](https://github.com/wiimag/wallet)**
- **[ant](https://github.com/ejoy/ant)**
- **[Cross-Platform-Game-Engine](https://github.com/ThomasJowett/Cross-Platform-Game-Engine)**
- **[ReflexEngine](https://github.com/dante1130/ReflexEngine)**
- **[PhysicalEngine](https://github.com/Im-Rises/PhysicalEngine)**
- **[spdlog_wrapper](https://github.com/gqw/spdlog_wrapper)**
- **[klog](https://github.com/KkemChen/klog)**
- **[Spatial.Engine](https://github.com/luizgabriel/Spatial.Engine)**
- **[EraEngine](https://github.com/EldarMuradov/EraEngine)**