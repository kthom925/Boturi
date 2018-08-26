#include "Boturi.h"
#include "Vulkan.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

bool Boturi::debugMode;
bool Boturi::resizedWindow;

const int Boturi::MAX_FRAMES_IN_FLIGHT = 2;
std::map<int, VkSampler> Boturi::textureSamplers;

// Constants across the environment
GLFWwindow * Boturi::window;

VkInstance Boturi::instance;
VkDebugReportCallbackEXT Boturi::debugger;
VkSurfaceKHR Boturi::surface;
VkPhysicalDevice Boturi::physicalDevice;
VkDevice Boturi::device;
VkSwapchainKHR Boturi::swapChain;
VkRenderPass Boturi::renderPass;
VkCommandPool Boturi::commandPool;

std::vector<VkImage> Boturi::swapChainImages;
std::vector<VkImageView> Boturi::swapChainImageViews;
std::vector<VkFramebuffer> Boturi::frameBuffers;

int Boturi::graphicsQueueIndex = -1;
int Boturi::presentQueueIndex = -1;

VkQueue Boturi::graphicsQueue;
VkQueue Boturi::presentQueue;

uint32_t Boturi::numImages;
VkSampleCountFlagBits Boturi::msaaSamples;

VkFormat Boturi::depthFormat;

SwapChainSupportDetails Boturi::swapChainDetails;
VkFormat Boturi::imageFormat;
VkExtent2D Boturi::extent;

Image Boturi::colorAttachment;
Image Boturi::depthAttachment;

std::vector<VkSemaphore> Boturi::imageAvailableSemaphores;
std::vector<VkSemaphore> Boturi::renderFinishedSemaphores;
std::vector<VkFence> Boturi::inFlightFences;

// Dynamically created

std::vector<Descriptor> Boturi::descriptors;

float Boturi::aspectRatio;

Descriptor d;
Pipeline p;
Texture t;
Mesh m;
UniformBuffer u;
MVPMatrix mvp;


std::vector<const char *> Boturi::deviceExtensions = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

std::vector<const char *> Boturi::validationLayers = {
	"VK_LAYER_LUNARG_standard_validation" 
};

// Used when creating GLFW window
static void framebufferResizeCallback(GLFWwindow * window, int width, int height) {
	Boturi::resizedWindow = true;
}

void makeGlfwWindow(
	GameConfiguration config, VkSurfaceKHR & surface, GLFWwindow ** window)
{
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	 *window = glfwCreateWindow(config.width, config.height, config.title, nullptr, nullptr);
	glfwSetFramebufferSizeCallback(*window, framebufferResizeCallback);

	glfwCreateWindowSurface(Boturi::instance, *window, nullptr, &surface);
}

void Boturi::init(GameConfiguration config)
{
	glfwInit();

	debugMode = config.debugMode;

	if (makeVulkanInstance(config, instance) != VK_SUCCESS)
		printError("Unable to create vulkan instance");

	makeGlfwWindow(config, surface, &window);

	if (debugMode)
		makeVulkanDebugger(debugger);

	selectPhysicalDevice(physicalDevice, &graphicsQueueIndex, &presentQueueIndex, &swapChainDetails);
	makeVulkanDevice(device);

	vkGetDeviceQueue(device, graphicsQueueIndex, 0, &graphicsQueue);
	vkGetDeviceQueue(device, presentQueueIndex, 0, &presentQueue);

	makeSyncObjects(imageAvailableSemaphores, renderFinishedSemaphores, inFlightFences);


	addDynamics();
	// Temporary testing code
	// TODO: Descriptor sets
	t = Texture("textures/texture.jpg");
	m = Mesh("models/cube.obj");

	std::vector<BindingType> def = { UNIFORM_BUFFER, TEXTURE_SAMPLER };
	d = Descriptor(def);
	p = Pipeline("shaders/vert.spv", "shaders/frag.spv", d);
	u = UniformBuffer(MVP_MATRIX);

	std::vector<UniformBuffer> us = { u };
	std::vector<Texture> ts = { t };

	d.makeDescriptorSets(def, us, ts);

	mvp = {};
	mvp.model = glm::mat4(1.0f);
	mvp.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	mvp.projection = glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 10.0f);
	mvp.projection[1][1] *= -1;

	// end temp

	while (!glfwWindowShouldClose(window))
		glfwPollEvents();
}

void Boturi::printError(const char* message)
{
	std::cout << "BOTURI ERROR: " << message << std::endl;
	std::getchar();
	std::exit(EXIT_FAILURE);
}

void Boturi::addDynamics()
{
	makeSwapChain(swapChain, numImages, imageFormat, extent);
	fillSwapChain();

	makeRenderPass(renderPass);
	CommandBuffer::makeCommandPool(commandPool);

	colorAttachment = Image(extent, imageFormat, 1, Boturi::msaaSamples, 
		VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	depthAttachment = Image(extent, depthFormat, 1, Boturi::msaaSamples,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

	makeFrameBuffers(frameBuffers);
}

void Boturi::removeDynamics()
{
	colorAttachment.cleanup();
	depthAttachment.cleanup();

	for (auto frameBuffer : frameBuffers)
		vkDestroyFramebuffer(device, frameBuffer, nullptr);

	

	vkDestroyCommandPool(device, commandPool, nullptr);
	vkDestroyRenderPass(device, renderPass, nullptr);

	// Temp code

	d.cleanup();
	p.cleanup();

	// end temp

	for (auto imageView : swapChainImageViews)
		vkDestroyImageView(device, imageView, nullptr);

	vkDestroySwapchainKHR(device, swapChain, nullptr);
}

void Boturi::refresh()
{
	int width = 0, height = 0;
	while (width == 0 || height == 0) {
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(device);

	removeDynamics();

	addDynamics();
}

void Boturi::exit()
{
	removeDynamics();

	for (auto& pair : textureSamplers)
		vkDestroySampler(device, pair.second, nullptr);

	t.cleanup();
	m.cleanup();
	u.cleanup();

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
		vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
		vkDestroyFence(device, inFlightFences[i], nullptr);
	}

	vkDestroyDevice(device, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);

	if (debugMode)
		destroyVulkanDebugger(debugger);

	vkDestroyInstance(instance, nullptr);

	glfwDestroyWindow(window);
	glfwTerminate();
}

VkSampler Boturi::getTextureSampler(int mipLevel)
{
	if (textureSamplers.find(mipLevel) == textureSamplers.end())
		textureSamplers[mipLevel] = Texture::makeTextureSampler(mipLevel);
	return textureSamplers[mipLevel];
}

size_t Boturi::getUniformSize(UniformType type)
{
	switch (type)
	{
	case MVP_MATRIX:
		return sizeof(MVPMatrix);
		break;
	default:
		Boturi::printError("The given uniform type does not exist");
		return 0;
		break;
	}
}