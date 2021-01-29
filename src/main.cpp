#include "app.h"


std::vector<std::string> defaultSearchPaths;


static int const SAMPLE_WIDTH = 1920;
static int const SAMPLE_HEIGHT = 1080;

static std::string cornellBox = "media/cornellBox/cornellBox.gltf";
static std::string sponza = "media/Sponza/glTF/Sponza.gltf";

// Since these files are too heavy, please download them yourself
// And make .gltf file by using Blender, etc.
// https://developer.nvidia.com/ue4-sun-temple
static std::string tample = "media/sun_tample/tample.gltf";
// https://www.dropbox.com/s/ka378kmu62i669m/Bistro_Godot.glb?dl=0
static std::string bistro = "media/bistro/bistro.gltf";
static std::string loadScene = tample;

//If scene has no light, point lights will be generated randomly
bool GenerateWhiteLight = true;
bool IgnorePointLight = true;
uint32_t numPointLightGenerates = 100;

std::string environmentalTextureFile = "media/daytime.hdr";

static void onErrorCallback(int error, const char* description)
{
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}


int main(int argc, char** argv)
{
	glfwSetErrorCallback(onErrorCallback);
	if (!glfwInit())
	{
		return -1;
	}
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window =
		glfwCreateWindow(SAMPLE_WIDTH, SAMPLE_HEIGHT, PROJECT_NAME, nullptr, nullptr);
	// Setup camera
	CameraManip.setWindowSize(SAMPLE_WIDTH, SAMPLE_HEIGHT);
	CameraManip.setLookat(nvmath::vec3f(0.0, 0.5, -0.1), nvmath::vec3f(0.0, 0.5, 0.0), nvmath::vec3f(0, 1, 0));

	// Setup Vulkan
	if (!glfwVulkanSupported())
	{
		printf("GLFW: Vulkan Not Supported\n");
		return -1;
	}
	// setup some basic things for the sample, logging file for example
	NVPSystem system(argv[0], PROJECT_NAME);

	// Search path for shaders and other media
	defaultSearchPaths = {
		PROJECT_ABSDIRECTORY,
		PROJECT_ABSDIRECTORY "..",
		NVPSystem::exePath(),
		NVPSystem::exePath() + std::string(PROJECT_NAME),
	};
	nvvk::Context vkctx;
	// Requesting Vulkan extensions and layers

	nvvk::ContextCreateInfo contextInfo(true);

	contextInfo.setVersion(1, 2);
	contextInfo.addInstanceLayer("VK_LAYER_LUNARG_monitor", true);
	contextInfo.addInstanceExtension(VK_KHR_SURFACE_EXTENSION_NAME);
	contextInfo.addInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, true);
#ifdef WIN32
	contextInfo.addInstanceExtension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
	contextInfo.addInstanceExtension(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
	contextInfo.addInstanceExtension(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif
	contextInfo.addInstanceExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	contextInfo.addDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	contextInfo.addDeviceExtension(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
	contextInfo.addDeviceExtension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	// #VKRay: Activate the ray tracing extension
	contextInfo.addDeviceExtension(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
	contextInfo.addDeviceExtension(VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME);
	contextInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	contextInfo.addDeviceExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
	contextInfo.addDeviceExtension(VK_KHR_SHADER_CLOCK_EXTENSION_NAME);
	vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelFeature;
	contextInfo.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false,
		&accelFeature);
	vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature;
	contextInfo.addDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, false,
		&rtPipelineFeature);


	// Creating Vulkan base application
	vkctx.ignoreDebugMessage(0x99fb7dfd);  // dstAccelerationStructure
	vkctx.ignoreDebugMessage(0x45e8716f);  // dstAccelerationStructure
	vkctx.initInstance(contextInfo);
	// Find all compatible devices
	auto compatibleDevices = vkctx.getCompatibleDevices(contextInfo);
	assert(!compatibleDevices.empty());
	// Use a compatible device
	vkctx.initDevice(compatibleDevices[0], contextInfo);


	App app;
	const vk::SurfaceKHR surface = app.getVkSurface(vkctx.m_instance, window);
	vkctx.setGCTQueueWithPresent(surface);

	app.setup(vkctx.m_instance, vkctx.m_device, vkctx.m_physicalDevice,
		vkctx.m_queueGCT.familyIndex);
	app.createSwapchain(surface, SAMPLE_WIDTH, SAMPLE_HEIGHT);

	app.createScene(loadScene);

	app.setupGlfwCallbacks(window);
	ImGui_ImplGlfw_InitForVulkan(window, true);

	CameraManip.setLookat(nvmath::vec3f(5, 3, -20), nvmath::vec3f(-1, -2, -5), nvmath::vec3f(0, 1, 0));
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		if (app.isMinimized())
			continue;
		app.render();
	}

	// Cleanup
	app.getDevice().waitIdle();
	app.destroyResources();
	app.destroy();

	vkctx.deinit();

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
