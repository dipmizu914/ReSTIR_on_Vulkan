#pragma once

#include <array>

#include <vulkan/vulkan.hpp>
#include <nvmath/nvmath.h>
#include <nvmath/nvmath_glsltypes.h>

struct Vertex {
	nvmath::vec3 position;
	nvmath::vec3 normal;
	nvmath::vec4 tangent;
	nvmath::vec4 color;
	nvmath::vec2 uv;
};
