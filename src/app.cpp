#include <sstream>
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE


#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

extern std::vector<std::string> defaultSearchPaths;
extern bool IgnorePointLight;


#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION


#include "app.h"
#include "nvh/cameramanipulator.hpp"
#include "nvh/fileoperations.hpp"
#include "nvh/gltfscene.hpp"
#include "nvh/nvprint.hpp"
#include "nvvk/commands_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/pipeline_vk.hpp"
#include "nvvk/renderpasses_vk.hpp"
#include "nvvk/shaders_vk.hpp"
#include "nvvk/context_vk.hpp"

#include "nvh/alignment.hpp"
#include "shaders/headers/binding.glsl"


void App::setup(const vk::Instance& instance,
	const vk::Device& device,
	const vk::PhysicalDevice& physicalDevice,
	uint32_t                  queueFamily)
{
	AppBase::setup(instance, device, physicalDevice, queueFamily);
	m_alloc.init(device, physicalDevice);
	m_debug.setup(m_device);
}

void App::createScene(std::string scene) {
	std::string filename = nvh::findFile(scene, defaultSearchPaths);
	_loadScene(filename);
	if (IgnorePointLight) {
		m_gltfScene.m_lights.clear();
	}
	_createDescriptorPool();

	m_sceneBuffers.create(
		m_gltfScene,
		m_tmodel, &m_alloc, m_device, m_physicalDevice,
		m_graphicsQueueIndex
	);

	m_sceneBuffers.createDescriptorSet(m_descStaticPool);

	for (std::size_t i = 0; i < numGBuffers; i++) {
		m_gBuffers[i].create(&m_alloc, m_device, m_graphicsQueueIndex, m_size, m_renderPass);
		//m_gBuffers[i].transitionLayout();
	}

	const float aspectRatio = m_size.width / static_cast<float>(m_size.height);
	m_sceneUniforms.prevFrameProjectionViewMatrix = CameraManip.getMatrix() * nvmath::perspectiveVK(CameraManip.getFov(), aspectRatio, 0.1f, 1000.0f);

	_createUniformBuffer();
	_createDescriptorSet();

	LOGI("Create Restir Pass\n");

	m_restirPass.setup(m_device, m_physicalDevice, m_graphicsQueueIndex, &m_alloc);
	m_restirPass.createRenderPass(m_size);
	m_restirPass.createPipeline(m_sceneSetLayout, m_sceneBuffers.getDescLayout(), m_lightSetLayout, m_restirSetLayout);


	LOGI("Create SpatialReuse Pass\n");

	m_spatialReusePass.setup(m_device, m_physicalDevice, m_graphicsQueueIndex, &m_alloc);
	m_spatialReusePass.createRenderPass(m_size);
	m_spatialReusePass.createPipeline(m_sceneSetLayout, m_lightSetLayout, m_restirSetLayout);


	createDepthBuffer();
	createRenderPass();
	initGUI(0);
	createFrameBuffers();
	_createPostPipeline();

	_updateRestirDescriptorSet();

	m_pushC.initialize = 1;
	_createMainCommandBuffer();

	m_device.waitIdle();
	LOGI("Prepared\n");


}

void App::_renderUI() {
	bool changed = false;
	if (showGui())
	{
		using GuiH = ImGuiH::Control;

		ImGuiH::Panel::Begin();

		const char* debugModes[]{
			"None",
			"Albedo",
			"Emission",
			"Normal",
			"Roughness",
			"Metallic",
			"WorldPosition",
			"Point Light Visualization"
		};
		changed |= ImGui::Combo("Debug Mode", &m_sceneUniforms.debugMode, debugModes, 8);
		changed |= ImGui::SliderFloat("Gamma", &m_sceneUniforms.gamma, 1.0f, 5.0f);

		changed |= ImGui::SliderInt("Initial Light Samples (log2)", &m_log2InitialLightSamples, 0, 10);

		changed |= ImGui::Checkbox("Use Temporal Reuse", &m_enableTemporalReuse);
		if (m_enableTemporalReuse) {
			changed |= ImGui::SliderInt("Temporal Sample Count Clamping", &m_sceneUniforms.temporalSampleCountMultiplier, 0, 100);
		}
		changed |= ImGui::Checkbox("Use Spatial Reuse", &m_enableSpatialReuse);
		if (m_enableSpatialReuse) {
			changed |= ImGui::SliderFloat("Spatial Radius", &m_sceneUniforms.spatialRadius, 0, 50);
		}
		changed |= ImGui::Checkbox("Use Visible Test", &m_enableVisibleTest);
		changed |= ImGui::Checkbox("Use Environment", &m_enableEnvironment);
		if (m_enableEnvironment) {
			changed |= ImGui::SliderFloat("FireFly Clamp Threshold", &m_sceneUniforms.fireflyClampThreshold, 0.0, 5.0);
			changed |= ImGui::SliderFloat("Environmental Suppression", &m_sceneUniforms.environmentalPower, 1.0, 10, "%.3f", 2.0);

		}

		if (ImGui::CollapsingHeader("Camera"))
		{
			nvmath::vec3f eye, center, up;
			CameraManip.getLookat(eye, center, up);
			float fov(CameraManip.getFov());
			changed |= GuiH::Drag("Position", "", &eye);
			changed |= GuiH::Drag("Center", "", &center);
			changed |= GuiH::Drag("Up", "", &up, nullptr, GuiH::Flags::Normal, vec3f(0.f), vec3f(1.f), 0.01f);
			changed |= GuiH::Slider("FOV", "", &fov, nullptr, GuiH::Flags::Normal, 1.0f, 150.f, "%.3f deg");
			if (changed)
			{
				CameraManip.setLookat(eye, center, up);
				CameraManip.setFov(fov);

			}
		}
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
			1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGuiH::Control::Info("", "", "(F10) Toggle Pane", ImGuiH::Control::Flags::Disabled);
		ImGuiH::Panel::End();
	}
	ImGui::Render();
	if (changed) {
		_resetFrame();
	}
}


void App::render() {
	_updateFrame();
	m_queue.waitIdle();

	{
		const vk::CommandBuffer& cmdBuf = m_mainCommandBuffer;
		cmdBuf.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
		_updateUniformBuffer(cmdBuf);
		m_restirPass.run(cmdBuf, m_sceneSet, m_sceneBuffers.getDescSet(), m_lightSet, m_restirSets[m_currentGBufferFrame]);
		m_spatialReusePass.run(cmdBuf, m_sceneSet, m_lightSet, m_restirSets[m_currentGBufferFrame]);
		cmdBuf.end();
		_submitMainCommand();

	}

	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	_renderUI();
	prepareFrame();

	auto                     curFrame = getCurFrame();
	const vk::CommandBuffer& cmdBuf = getCommandBuffers()[curFrame];
	cmdBuf.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	{

		vk::ClearValue clearValues[2];
		clearValues[0].setColor(
			std::array<float, 4>({ 0.0,0.0,0.0,0.0 }));
		clearValues[1].setDepthStencil({ 1.0f, 0 });

		vk::RenderPassBeginInfo postRenderPassBeginInfo;
		postRenderPassBeginInfo.setClearValueCount(2);
		postRenderPassBeginInfo.setPClearValues(clearValues);
		postRenderPassBeginInfo.setRenderPass(getRenderPass());
		postRenderPassBeginInfo.setFramebuffer(getFramebuffers()[curFrame]);
		postRenderPassBeginInfo.setRenderArea({ {}, getSize() });

		cmdBuf.beginRenderPass(postRenderPassBeginInfo, vk::SubpassContents::eInline);
		cmdBuf.pushConstants<shader::PushConstant>(m_postPipelineLayout,
			vk::ShaderStageFlagBits::eFragment,
			0, m_pushC);
		// Rendering tonemapper
		_drawPost(cmdBuf, m_currentGBufferFrame);
		// Rendering UI
		ImGui::Render();
		ImGui::RenderDrawDataVK(cmdBuf, ImGui::GetDrawData());
		ImGui::EndFrame();
		cmdBuf.endRenderPass();
	}
	// Submit for display
	cmdBuf.end();
	submitFrame();
	//m_device.waitIdle();

	m_currentGBufferFrame = (m_currentGBufferFrame + 1) % numGBuffers;
	if (m_pushC.frame > 10) {
		m_pushC.initialize = 0;
	}
}

//--------------------------------------------------------------------------------------------------
// Destroying all allocations
//
void App::destroyResources()
{

	m_device.destroy(m_sceneSetLayout);
	m_alloc.destroy(m_sceneUniformBuffer);

	m_device.destroy(m_descStaticPool);
	m_device.destroy(m_descTexturePool);
	m_device.destroy(m_lightSetLayout);
	m_device.destroy(m_restirSetLayout);


	for (auto& t : m_reservoirInfoBuffers) {
		m_alloc.destroy(t);
	}
	for (auto& t : m_reservoirWeightBuffers) {
		m_alloc.destroy(t);
	}
	m_alloc.destroy(m_storageImage);
	m_alloc.destroy(m_reservoirTmpInfoBuffer);
	m_alloc.destroy(m_reservoirTmpWeightBuffer);
	//#Post
	m_device.destroy(m_postPipeline);
	m_device.destroy(m_postPipelineLayout);

	m_device.destroy(m_mainFence);

	m_sceneBuffers.destroy();

	m_restirPass.destroy();
	m_spatialReusePass.destroy();

	for (auto& gBuf : m_gBuffers) {
		gBuf.destroy();
	}
	m_alloc.deinit();

}

//--------------------------------------------------------------------------------------------------
// Loading the glTF file and setting up all buffers
//
void App::_loadScene(const std::string& filename)
{
	using vkBU = vk::BufferUsageFlagBits;
	tinygltf::TinyGLTF tcontext;
	std::string        warn, error;

	LOGI("Loading file: %s", filename.c_str());
	if (!tcontext.LoadASCIIFromFile(&m_tmodel, &error, &warn, filename))
	{
		assert(!"Error while loading scene");
	}
	LOGW(warn.c_str());
	LOGE(error.c_str());


	m_gltfScene.importMaterials(m_tmodel);
	m_gltfScene.importDrawableNodes(m_tmodel,
		nvh::GltfAttributes::Normal | nvh::GltfAttributes::Texcoord_0 | nvh::GltfAttributes::Color_0 | nvh::GltfAttributes::Tangent);

	ImGuiH::SetCameraJsonFile(fs::path(filename).stem().string());
	if (!m_gltfScene.m_cameras.empty())
	{
		auto& c = m_gltfScene.m_cameras[0];
		CameraManip.setCamera({ c.eye, c.center, c.up, (float)rad2deg(c.cam.perspective.yfov) });
		ImGuiH::SetHomeCamera({ c.eye, c.center, c.up, (float)rad2deg(c.cam.perspective.yfov) });

		for (auto& c : m_gltfScene.m_cameras)
		{
			ImGuiH::AddCamera({ c.eye, c.center, c.up, (float)rad2deg(c.cam.perspective.yfov) });
		}
	}
	else
	{
		// Re-adjusting camera to fit the new scene
		CameraManip.fit(m_gltfScene.m_dimensions.min, m_gltfScene.m_dimensions.max, true);
	}
	// Show gltf scene info
	std::cout << "Show gltf scene info" << std::endl;
	std::cout << "scene center:[" << m_gltfScene.m_dimensions.center.x << ", "
		<< m_gltfScene.m_dimensions.center.y << ", "
		<< m_gltfScene.m_dimensions.center.z << "]" << std::endl;

	std::cout << "max:[" << m_gltfScene.m_dimensions.max.x << ", "
		<< m_gltfScene.m_dimensions.max.y << ", "
		<< m_gltfScene.m_dimensions.max.z << "]" << std::endl;

	std::cout << "min:[" << m_gltfScene.m_dimensions.min.x << ", "
		<< m_gltfScene.m_dimensions.min.y << ", "
		<< m_gltfScene.m_dimensions.min.z << "]" << std::endl;

	std::cout << "radius:" << m_gltfScene.m_dimensions.radius << std::endl;

	std::cout << "size:[" << m_gltfScene.m_dimensions.size.x << ", "
		<< m_gltfScene.m_dimensions.size.y << ", "
		<< m_gltfScene.m_dimensions.size.z << "]" << std::endl;

	std::cout << "vertex num:" << m_gltfScene.m_positions.size() << std::endl;
}

void App::_createDescriptorPool() {
	// create  D Pool

	uint32_t maxSets = 100;
	using vkDT = vk::DescriptorType;
	using vkDP = vk::DescriptorPoolSize;

	std::vector<vkDP> staticPoolSizes{
		vkDP(vkDT::eCombinedImageSampler, maxSets),
		vkDP(vkDT::eStorageBuffer, maxSets),
		vkDP(vkDT::eUniformBuffer, maxSets),
		vkDP(vkDT::eUniformBufferDynamic, maxSets),
		vkDP(vkDT::eAccelerationStructureKHR, maxSets),
		vkDP(vkDT::eStorageImage, maxSets)
	};
	m_descStaticPool = nvvk::createDescriptorPool(m_device, staticPoolSizes, maxSets);

	std::vector<vkDP> texturePoolSizes{
		vkDP(vkDT::eCombinedImageSampler, static_cast<uint32_t>(4 * m_gltfScene.m_materials.size()))
	};
	m_descTexturePool = nvvk::createDescriptorPool(m_device, texturePoolSizes, static_cast<uint32_t>(m_gltfScene.m_materials.size()));
}

void App::_createUniformBuffer()
{
	using vkBU = vk::BufferUsageFlagBits;
	using vkMP = vk::MemoryPropertyFlagBits;


	m_sceneUniforms.debugMode = 0;
	m_sceneUniforms.gamma = 2.2;
	m_sceneUniforms.screenSize = nvmath::uvec2(m_size.width, m_size.height);
	m_sceneUniforms.flags = RESTIR_VISIBILITY_REUSE_FLAG | RESTIR_TEMPORAL_REUSE_FLAG | RESTIR_SPATIAL_REUSE_FLAG;
	//if (_enableTemporalReuse) {
	//	m_sceneUniforms.flags |= RESTIR_TEMPORAL_REUSE_FLAG;
	//}
	m_sceneUniforms.spatialNeighbors = 4;
	m_sceneUniforms.spatialRadius = 30.0f;
	m_sceneUniforms.initialLightSampleCount = 1 << m_log2InitialLightSamples;
	m_sceneUniforms.temporalSampleCountMultiplier = m_temporalReuseSampleMultiplier;

	m_sceneUniforms.pointLightCount = m_sceneBuffers.getPtLightsCount();
	m_sceneUniforms.triangleLightCount = m_sceneBuffers.getTriLightsCount();
	m_sceneUniforms.aliasTableCount = m_sceneBuffers.getAliasTableCount();

	m_sceneUniforms.environmentalPower = 1.0;
	m_sceneUniforms.fireflyClampThreshold = 2.0;



	m_sceneUniformBuffer = m_alloc.createBuffer(sizeof(shader::SceneUniforms),
		vkBU::eUniformBuffer | vkBU::eTransferDst, vkMP::eDeviceLocal);
	m_debug.setObjectName(m_sceneUniformBuffer.buffer, "sceneBuffer");

	m_reservoirInfoBuffers.resize(numGBuffers);
	m_reservoirWeightBuffers.resize(numGBuffers);


	nvvk::CommandPool cmdBufGet(m_device, m_graphicsQueueIndex);
	vk::CommandBuffer cmdBuf = cmdBufGet.createCommandBuffer();



	_updateUniformBuffer(cmdBuf);
	auto colorCreateInfo = nvvk::makeImage2DCreateInfo(m_size, vk::Format::eR32G32B32A32Sfloat,
		vk::ImageUsageFlagBits::eColorAttachment
		| vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage
	);
	vk::SamplerCreateInfo samplerCreateInfo{ {}, vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest };

	for (std::size_t i = 0; i < numGBuffers; ++i) {
		{
			nvvk::Image             image = m_alloc.createImage(colorCreateInfo);
			vk::ImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, colorCreateInfo);
			m_reservoirInfoBuffers[i] = m_alloc.createTexture(image, ivInfo, samplerCreateInfo);
			m_reservoirInfoBuffers[i].descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			nvvk::cmdBarrierImageLayout(cmdBuf, m_reservoirInfoBuffers[i].image, vk::ImageLayout::eUndefined,
				vk::ImageLayout::eGeneral);
		}
		{
			nvvk::Image             image = m_alloc.createImage(colorCreateInfo);
			vk::ImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, colorCreateInfo);
			m_reservoirWeightBuffers[i] = m_alloc.createTexture(image, ivInfo, samplerCreateInfo);
			m_reservoirWeightBuffers[i].descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			nvvk::cmdBarrierImageLayout(cmdBuf, m_reservoirWeightBuffers[i].image, vk::ImageLayout::eUndefined,
				vk::ImageLayout::eGeneral);
		}
	}
	{
		{
			nvvk::Image             image = m_alloc.createImage(colorCreateInfo);
			vk::ImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, colorCreateInfo);
			m_reservoirTmpInfoBuffer = m_alloc.createTexture(image, ivInfo, samplerCreateInfo);
			m_reservoirTmpInfoBuffer.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			nvvk::cmdBarrierImageLayout(cmdBuf, m_reservoirTmpInfoBuffer.image, vk::ImageLayout::eUndefined,
				vk::ImageLayout::eGeneral);
		}
		{
			nvvk::Image             image = m_alloc.createImage(colorCreateInfo);
			vk::ImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, colorCreateInfo);
			m_reservoirTmpWeightBuffer = m_alloc.createTexture(image, ivInfo, samplerCreateInfo);
			m_reservoirTmpWeightBuffer.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			nvvk::cmdBarrierImageLayout(cmdBuf, m_reservoirTmpWeightBuffer.image, vk::ImageLayout::eUndefined,
				vk::ImageLayout::eGeneral);
		}
	}

	nvvk::Image             image = m_alloc.createImage(colorCreateInfo);
	vk::ImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, colorCreateInfo);
	m_storageImage = m_alloc.createTexture(image, ivInfo, samplerCreateInfo);
	m_storageImage.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	nvvk::cmdBarrierImageLayout(cmdBuf, m_storageImage.image, vk::ImageLayout::eUndefined,
		vk::ImageLayout::eGeneral);

	cmdBufGet.submitAndWait(cmdBuf);
	m_alloc.finalizeAndReleaseStaging();

}
//--------------------------------------------------------------------------------------------------
// Describing the layout pushed when rendering
//
void App::_createDescriptorSet()
{
	using vkDS = vk::DescriptorSetLayoutBinding;
	using vkDT = vk::DescriptorType;
	using vkSS = vk::ShaderStageFlagBits;
	std::vector<vk::WriteDescriptorSet> writes;

	m_sceneSetLayoutBind.addBinding(vkDS(B_SCENE, vkDT::eUniformBuffer, 1, vkSS::eVertex | vkSS::eFragment | vkSS::eRaygenKHR | vkSS::eCompute | vkSS::eMissKHR));
	m_sceneSetLayout = m_sceneSetLayoutBind.createLayout(m_device);
	m_sceneSet = nvvk::allocateDescriptorSet(m_device, m_descStaticPool, m_sceneSetLayout);
	vk::DescriptorBufferInfo dbiUnif{ m_sceneUniformBuffer.buffer, 0, VK_WHOLE_SIZE };
	writes.emplace_back(m_sceneSetLayoutBind.makeWrite(m_sceneSet, B_SCENE, &dbiUnif));


	m_lightSetLayoutBind.addBinding(vkDS(B_ALIAS_TABLE, vkDT::eStorageBuffer, 1, vkSS::eFragment | vkSS::eRaygenKHR | vkSS::eCompute));
	m_lightSetLayoutBind.addBinding(vkDS(B_POINT_LIGHTS, vkDT::eStorageBuffer, 1, vkSS::eFragment | vkSS::eRaygenKHR | vkSS::eCompute));
	m_lightSetLayoutBind.addBinding(vkDS(B_TRIANGLE_LIGHTS, vkDT::eStorageBuffer, 1, vkSS::eFragment | vkSS::eRaygenKHR | vkSS::eCompute));
	m_lightSetLayoutBind.addBinding(vkDS(B_ENVIRONMENTAL_MAP, vkDT::eCombinedImageSampler, 1, vkSS::eFragment | vkSS::eRaygenKHR | vkSS::eCompute | vkSS::eMissKHR));
	m_lightSetLayoutBind.addBinding(vkDS(B_ENVIRONMENTAL_ALIAS_MAP, vkDT::eCombinedImageSampler, 1, vkSS::eFragment | vkSS::eRaygenKHR | vkSS::eCompute));

	m_lightSetLayout = m_lightSetLayoutBind.createLayout(m_device);
	m_lightSet = nvvk::allocateDescriptorSet(m_device, m_descStaticPool, m_lightSetLayout);

	vk::DescriptorBufferInfo pointLightUnif{ m_sceneBuffers.getPtLights().buffer, 0, VK_WHOLE_SIZE };
	vk::DescriptorBufferInfo trialgleLightUnif{ m_sceneBuffers.getTriLights().buffer, 0, VK_WHOLE_SIZE };
	vk::DescriptorBufferInfo aliasTableUnif{ m_sceneBuffers.getAliasTable().buffer, 0, VK_WHOLE_SIZE };
	const vk::DescriptorImageInfo& environmentalUnif = m_sceneBuffers.getEnvironmentalTexture().descriptor;
	const vk::DescriptorImageInfo& environmentalAliasUnif = m_sceneBuffers.getEnvironmentalAliasMap().descriptor;

	writes.emplace_back(m_lightSetLayoutBind.makeWrite(m_lightSet, B_ALIAS_TABLE, &aliasTableUnif));
	writes.emplace_back(m_lightSetLayoutBind.makeWrite(m_lightSet, B_POINT_LIGHTS, &pointLightUnif));
	writes.emplace_back(m_lightSetLayoutBind.makeWrite(m_lightSet, B_TRIANGLE_LIGHTS, &trialgleLightUnif));
	writes.emplace_back(m_lightSetLayoutBind.makeWrite(m_lightSet, B_ENVIRONMENTAL_MAP, &environmentalUnif));
	writes.emplace_back(m_lightSetLayoutBind.makeWrite(m_lightSet, B_ENVIRONMENTAL_ALIAS_MAP, &environmentalAliasUnif));

	m_restirSetLayoutBind.addBinding(vkDS(B_FRAME_WORLD_POSITION, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_FRAME_ALBEDO, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_FRAME_NORMAL, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_FRAME_MATERIAL_PROPS, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_PERV_FRAME_WORLD_POSITION, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_PERV_FRAME_ALBEDO, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_PERV_FRAME_NORMAL, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_PREV_FRAME_MATERIAL_PROPS, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));

	m_restirSetLayoutBind.addBinding(vkDS(B_RESERVIORS_INFO, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_RESERVIORS_WEIGHT, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_PREV_RESERVIORS_INFO, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_PREV_RESERVIORS_WEIGHT, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_TMP_RESERVIORS_INFO, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_TMP_RESERVIORS_WEIGHT, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayoutBind.addBinding(vkDS(B_STORAGE_IMAGE, vkDT::eStorageImage, 1, vkSS::eRaygenKHR | vkSS::eFragment | vkSS::eCompute));
	m_restirSetLayout = m_restirSetLayoutBind.createLayout(m_device);
	m_restirSets.resize(numGBuffers);
	nvvk::allocateDescriptorSets(m_device, m_descStaticPool, m_restirSetLayout, numGBuffers, m_restirSets);


	m_device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

}

//--------------------------------------------------------------------------------------------------
// The pipeline is how things are rendered, which shaders, type of primitives, depth test and more
//
void App::_createPostPipeline()
{
	// Push constants in the fragment shader
	vk::PushConstantRange pushConstantRanges = { vk::ShaderStageFlagBits::eFragment, 0, sizeof(shader::PushConstant) };

	// Creating the pipeline layout
	std::vector<vk::DescriptorSetLayout> layouts = { m_sceneSetLayout, m_lightSetLayout ,m_restirSetLayout };
	vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	//pipelineLayoutCreateInfo.setSetLayoutCount(1);
	pipelineLayoutCreateInfo.setSetLayouts(layouts);
	pipelineLayoutCreateInfo.setPushConstantRangeCount(1);
	pipelineLayoutCreateInfo.setPPushConstantRanges(&pushConstantRanges);
	m_postPipelineLayout = m_device.createPipelineLayout(pipelineLayoutCreateInfo);

	// Pipeline: completely generic, no vertices
	std::vector<std::string> paths = defaultSearchPaths;

	nvvk::GraphicsPipelineGeneratorCombined pipelineGenerator(m_device, m_postPipelineLayout,
		m_renderPass);
	pipelineGenerator.addShader(nvh::loadFile("src/shaders/quad.vert.spv", true, paths, true),
		vk::ShaderStageFlagBits::eVertex);
	pipelineGenerator.addShader(nvh::loadFile("src/shaders/post.frag.spv", true, paths, true),
		vk::ShaderStageFlagBits::eFragment);
	pipelineGenerator.rasterizationState.setCullMode(vk::CullModeFlagBits::eNone);
	m_postPipeline = pipelineGenerator.createPipeline();
	m_debug.setObjectName(m_postPipeline, "post");
}

void App::_createMainCommandBuffer() {
	m_mainCommandBuffer =
		m_device.allocateCommandBuffers({ m_cmdPool, vk::CommandBufferLevel::ePrimary, 1 })[0];
	vk::FenceCreateInfo fenceInfo;
	fenceInfo
		.setFlags(vk::FenceCreateFlagBits::eSignaled);
	m_mainFence = m_device.createFence(fenceInfo);
}

void App::_updateRestirDescriptorSet()
{
	std::vector<vk::WriteDescriptorSet> writes;

	for (uint32_t i = 0; i < numGBuffers; i++) {
		vk::DescriptorSet& set = m_restirSets[i];
		const GBuffer& buf = m_gBuffers[i];
		const GBuffer& bufprev = m_gBuffers[(numGBuffers + i - 1) % numGBuffers];

		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_FRAME_WORLD_POSITION, &buf.getWorldPosTexture().descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_FRAME_ALBEDO, &buf.getAlbedoTexture().descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_FRAME_NORMAL, &buf.getNormalTexture().descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_FRAME_MATERIAL_PROPS, &buf.getMaterialPropertiesTexture().descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_PERV_FRAME_WORLD_POSITION, &bufprev.getWorldPosTexture().descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_PERV_FRAME_ALBEDO, &bufprev.getAlbedoTexture().descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_PERV_FRAME_NORMAL, &bufprev.getNormalTexture().descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_PREV_FRAME_MATERIAL_PROPS, &bufprev.getMaterialPropertiesTexture().descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_STORAGE_IMAGE, &m_storageImage.descriptor));


		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_RESERVIORS_INFO, &m_reservoirInfoBuffers[i].descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_RESERVIORS_WEIGHT, &m_reservoirWeightBuffers[i].descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_PREV_RESERVIORS_INFO, &m_reservoirInfoBuffers[(numGBuffers + i - 1) % numGBuffers].descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_PREV_RESERVIORS_WEIGHT, &m_reservoirWeightBuffers[(numGBuffers + i - 1) % numGBuffers].descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_TMP_RESERVIORS_INFO, &m_reservoirTmpInfoBuffer.descriptor));
		writes.emplace_back(m_restirSetLayoutBind.makeWrite(set, B_TMP_RESERVIORS_WEIGHT, &m_reservoirTmpWeightBuffer.descriptor));


	}
	m_device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

//--------------------------------------------------------------------------------------------------
// Called at each frame to update the camera matrix
//
void App::_updateUniformBuffer(const vk::CommandBuffer& cmdBuf)
{
	// Prepare new UBO contents on host.
	const float aspectRatio = m_size.width / static_cast<float>(m_size.height);

	m_sceneUniforms.prevFrameProjectionViewMatrix = m_sceneUniforms.projectionViewMatrix;

	m_sceneUniforms.proj = nvmath::perspectiveVK(CameraManip.getFov(), aspectRatio, 0.1f, 1000.0f);
	m_sceneUniforms.view = CameraManip.getMatrix();
	m_sceneUniforms.projInverse = nvmath::invert(m_sceneUniforms.proj);
	m_sceneUniforms.viewInverse = nvmath::invert(m_sceneUniforms.view);
	m_sceneUniforms.projectionViewMatrix = m_sceneUniforms.proj * m_sceneUniforms.view;
	m_sceneUniforms.prevCamPos = m_sceneUniforms.cameraPos;
	m_sceneUniforms.cameraPos = CameraManip.getCamera().eye;
	m_sceneUniforms.initialLightSampleCount = 1 << m_log2InitialLightSamples;

	if (m_enableTemporalReuse) {
		m_sceneUniforms.flags |= RESTIR_TEMPORAL_REUSE_FLAG;
	}
	else {
		m_sceneUniforms.flags &= ~RESTIR_TEMPORAL_REUSE_FLAG;
	}
	if (m_enableVisibleTest) {
		m_sceneUniforms.flags |= RESTIR_VISIBILITY_REUSE_FLAG;
	}
	else {
		m_sceneUniforms.flags &= ~RESTIR_VISIBILITY_REUSE_FLAG;
	}
	if (m_enableSpatialReuse) {
		m_sceneUniforms.flags |= RESTIR_SPATIAL_REUSE_FLAG;
	}
	else {
		m_sceneUniforms.flags &= ~RESTIR_SPATIAL_REUSE_FLAG;
	}
	if (m_enableEnvironment) {
		m_sceneUniforms.flags |= USE_ENVIRONMENT_FLAG;
	}
	else {
		m_sceneUniforms.flags &= ~USE_ENVIRONMENT_FLAG;
	}
	// UBO on the device, and what stages access it.
	vk::Buffer deviceUBO = m_sceneUniformBuffer.buffer;
	auto uboUsageStages = vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader
		| vk::PipelineStageFlagBits::eRayTracingShaderKHR;

	// Ensure that the modified UBO is not visible to previous frames.
	vk::BufferMemoryBarrier beforeBarrier;
	beforeBarrier.setSrcAccessMask(vk::AccessFlagBits::eShaderRead);
	beforeBarrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
	beforeBarrier.setBuffer(deviceUBO);
	beforeBarrier.setOffset(0);
	beforeBarrier.setSize(sizeof m_sceneUniforms);
	cmdBuf.pipelineBarrier(
		uboUsageStages,
		vk::PipelineStageFlagBits::eTransfer,
		vk::DependencyFlagBits::eDeviceGroup, {}, { beforeBarrier }, {});

	// Schedule the host-to-device upload. (hostUBO is copied into the cmd
	// buffer so it is okay to deallocate when the function returns).
	cmdBuf.updateBuffer<shader::SceneUniforms>(m_sceneUniformBuffer.buffer, 0, m_sceneUniforms);

	// Making sure the updated UBO will be visible.
	vk::BufferMemoryBarrier afterBarrier;
	afterBarrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
	afterBarrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
	afterBarrier.setBuffer(deviceUBO);
	afterBarrier.setOffset(0);
	afterBarrier.setSize(sizeof m_sceneUniforms);
	cmdBuf.pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer,
		uboUsageStages,
		vk::DependencyFlagBits::eDeviceGroup, {}, { afterBarrier }, {});
}

//--------------------------------------------------------------------------------------------------
// Draw a full screen quad with the attached image
//
void App::_drawPost(vk::CommandBuffer cmdBuf, uint32_t currentGFrame)
{
	m_debug.beginLabel(cmdBuf, "Post");

	cmdBuf.setViewport(0, { vk::Viewport(0, 0, (float)m_size.width, (float)m_size.height, 0, 1) });
	cmdBuf.setScissor(0, { {{0, 0}, {m_size.width, m_size.height}} });
	cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, m_postPipeline);
	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_postPipelineLayout, 0,
		{
				m_sceneSet,
				m_lightSet,
				m_restirSets[currentGFrame]
		}, {});
	cmdBuf.draw(3, 1, 0, 0);

	m_debug.endLabel(cmdBuf);
}

void App::_submitMainCommand() {
	while (m_device.waitForFences(m_mainFence, VK_TRUE, 10000) == vk::Result::eTimeout) {
	}
	m_device.resetFences(m_mainFence);
	vk::SubmitInfo submitInfo;
	submitInfo
		.setCommandBuffers(m_mainCommandBuffer);
	m_queue.submit(submitInfo, m_mainFence);
}











void App::onResize(int /*w*/, int /*h*/)
{
	/*createOffscreenRender();
	updatePostDescriptorSet();
	updateRtDescriptorSet();*/
	_resetFrame();
}





//--------------------------------------------------------------------------------------------------
// If the camera matrix has changed, resets the frame.
// otherwise, increments frame.
//
void App::_updateFrame()
{
	static nvmath::mat4f refCamMatrix;
	static float         refFov{ CameraManip.getFov() };

	const auto& m = CameraManip.getMatrix();
	const auto  fov = CameraManip.getFov();

	if (memcmp(&refCamMatrix.a00, &m.a00, sizeof(nvmath::mat4f)) != 0 || refFov != fov)
	{
		_resetFrame();
		refCamMatrix = m;
		refFov = fov;
	}
	m_pushC.frame++;
}

void App::_resetFrame()
{
	m_pushC.frame = -1;

}
