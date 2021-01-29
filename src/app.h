#pragma once
#include <vulkan/vulkan.hpp>

#define NVVK_ALLOC_DEDICATED
#include "nvvk/allocator_vk.hpp"
#include "nvvk/appbase_vkpp.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/context_vk.hpp"
#include "nvvk/profiler_vk.hpp"

// #VKRay
#include "nvh/gltfscene.hpp"
#include "nvvk/raytraceKHR_vk.hpp"

#include "sceneBuffers.h"
#include "GBuffer.hpp"
#include "util.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"

#include "passes/restirPass.h"
#include "passes/spatialReusePass.h"

class App : public nvvk::AppBase
{
public:
	constexpr static std::size_t numGBuffers = 2;
	App() {};
	~App() {};
	void setup(const vk::Instance& instance,
		const vk::Device& device,
		const vk::PhysicalDevice& physicalDevice,
		uint32_t                  queueFamily) override;
	void createScene(std::string scene);
	void render();
	void destroyResources();

private:
	void _loadScene(const std::string& filename);

	void _createDescriptorPool();
	void _createUniformBuffer();
	void _createDescriptorSet();
	void _createPostPipeline();
	void _createMainCommandBuffer();
	void _updateRestirDescriptorSet();

	void _updateUniformBuffer(const vk::CommandBuffer& cmdBuf);

	void _drawPost(vk::CommandBuffer cmdBuf, uint32_t currentGFrame);
	void _renderUI();
	void _submitMainCommand();

	void _updateFrame();
	void _resetFrame();

	void onResize(int /*w*/, int /*h*/) override;

	nvvk::AllocatorDedicated m_alloc;
	nvvk::DebugUtil          m_debug;


	//Constants
	bool m_enableTemporalReuse = true;
	bool m_enableSpatialReuse = true;
	bool m_enableVisibleTest = true;
	bool m_enableEnvironment = false;

	int m_log2InitialLightSamples = 5;
	int m_temporalReuseSampleMultiplier = 20;




	uint32_t m_currentGBufferFrame = 0;
	shader::PushConstant m_pushC;
	shader::SceneUniforms m_sceneUniforms;
	nvvk::Buffer               m_sceneUniformBuffer;


	//Resources
	nvh::GltfScene m_gltfScene;
	tinygltf::Model m_tmodel;
	SceneBuffers m_sceneBuffers;
	GBuffer m_gBuffers[2];


	vk::DeviceSize m_reservoirBufferSize;
	std::vector<nvvk::Texture>              m_reservoirInfoBuffers;
	std::vector<nvvk::Texture>              m_reservoirWeightBuffers;
	nvvk::Texture             m_reservoirTmpInfoBuffer;
	nvvk::Texture             m_reservoirTmpWeightBuffer;
	nvvk::Texture m_storageImage;

	//Descriptors
	vk::DescriptorPool          m_descStaticPool;
	vk::DescriptorPool          m_descTexturePool;

	nvvk::DescriptorSetBindings m_sceneSetLayoutBind;
	vk::DescriptorSetLayout     m_sceneSetLayout;
	vk::DescriptorSet           m_sceneSet;

	nvvk::DescriptorSetBindings m_lightSetLayoutBind;
	vk::DescriptorSetLayout     m_lightSetLayout;
	vk::DescriptorSet           m_lightSet;

	nvvk::DescriptorSetBindings m_restirSetLayoutBind;
	vk::DescriptorSetLayout     m_restirSetLayout;
	std::vector<vk::DescriptorSet>           m_restirSets;

	//Pipeline
	vk::Pipeline                m_postPipeline;
	vk::PipelineLayout          m_postPipelineLayout;

	vk::CommandBuffer m_mainCommandBuffer;
	vk::Fence m_mainFence;

	//Pass
	RestirPass m_restirPass;
	SpatialReusePass m_spatialReusePass;

	void _initReservior(shader::Reservoir& reseovir) {
		reseovir.numStreamSamples = 0;
			reseovir.lightIndex = 0;
			reseovir.pHat = 0.0;
			reseovir.sumWeights = 0.0;
			reseovir.w = 0.0;
		
	}

};
