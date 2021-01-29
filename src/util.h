#pragma once
#define NVVK_ALLOC_DEDICATED

#include <vulkan/vulkan.hpp>
#include <nvmath/nvmath.h>
#include <nvmath/nvmath_glsltypes.h>
#include "nvvk/allocator_vk.hpp"
#include "nvvk/appbase_vkpp.hpp"
#include "nvh/gltfscene.hpp"
#include "nvvk/descriptorsets_vk.hpp"

#include "shaderIncludes.h"
#include "nvh/gltfscene.hpp"
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <iostream>
#include <optional>
#include <random>


[[nodiscard]] std::vector<shader::pointLight> collectPointLightsFromScene(const nvh::GltfScene&);
[[nodiscard]] std::vector<shader::pointLight> generateRandomPointLights(
	nvmath::vec3 min, nvmath::vec3 max, 
	std::uniform_real_distribution<float> distR = std::uniform_real_distribution<float>(0.0f, 1.0f),
	std::uniform_real_distribution<float> distG = std::uniform_real_distribution<float>(0.0f, 1.0f),
	std::uniform_real_distribution<float> distB = std::uniform_real_distribution<float>(0.0f, 1.0f)
);

[[nodiscard]] std::vector<shader::triangleLight> collectTriangleLightsFromScene(const nvh::GltfScene&);

[[nodiscard]] std::vector<shader::aliasTableCell> createAliasTable(std::vector<float>&);
[[nodiscard]] vk::Format findSupportedFormat(
	const std::vector<vk::Format>& candidates, vk::PhysicalDevice, vk::ImageTiling, vk::FormatFeatureFlags
);

