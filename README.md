Vulkan-ReSTIR friendly to NVIDIA Vulkan Wrapper
================

![Bistro ](doc/bistro_night.gif)

## 1. Explanation
This project is the implementation of [Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting](https://research.nvidia.com/sites/default/files/pubs/2020-07_Spatiotemporal-reservoir-resampling/ReSTIR.pdf) on Vulkan API with [NVIDIA Wrapper](https://github.com/nvpro-samples)

## 2. Building the Project

 0. Requirements on Windows:
    - Visual Studio 2019
    - Vulkan SDK above version 1.2.162.0.
    - NVIDIA graphics board that supports `VK_KHR_ray_tracing_pipeline` extension and corresponding drivers (latest recommended)
    - CMake above version 3.9.6.

    The code has not been tested on other platforms.

 1. Clone this repository with the `--recurse-submodules` flag.

 2. Build the project using the standard CMake building process.

## 3. Other Demos
### Sponza
![Sponza](doc/sponza.jpg)
### Cornell Box
![Cornell Box](doc/cornell.jpg)
### Bistro (with Triangle Lights)
![Bistro (with Triangle Lights)](doc/bistro_night.jpg)
### Bistro (with IBL)
![Bistro (with IBL)](doc/bistro_day.jpg)
### Sun Tample (with Triangle Lights)
![Sun Tample (with Triangle Lights)](doc/tample_night.jpg)
### Sun Tample  (with IBL)
![Sun Tample (with IBL)](doc/tample_day.jpg)

## 4. Implementation
- [x] Triangle Light
- [ ] (Beta) IBL 