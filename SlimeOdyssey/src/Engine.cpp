//
// Created by alexm on 3/07/24.
//

#include "Engine.h"

#include <spdlog/spdlog.h>
#include "SlimeWindow.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <GLFW/glfw3.h>

#include <string>

#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3
#define VMA_IMPLEMENTATION
#define VMA_DEBUG_LOG(format, ...) do { \
spdlog::debug(format, ##__VA_ARGS__); \
} while(0)

#include <vk_mem_alloc.h>

#include <glm/glm.hpp>
#include <chrono>

#define MAX_FRAMES_IN_FLIGHT 2

Engine::Engine(const char* name, int width, int height, bool resizable) : m_window(name, width, height, resizable)
{
	spdlog::set_level(spdlog::level::trace);
	spdlog::stdout_color_mt("console");
}

Engine::~Engine()
{
	Cleanup();
}

int Engine::CreateEngine()
{
	if (DeviceInit() != 0)
		return -1;
	if (CreateCommandPool() != 0)
		return -1;
	if (GetQueues() != 0)
		return -1;
	if (CreateSwapchain() != 0)
		return -1;
	if (CreateRenderCommandBuffers() != 0)
		return -1;
	if (InitSyncObjects() != 0)
		return -1;
	if (SetupManagers() != 0)
		return -1;

	return 0;
}

int Engine::SetupManagers()
{
	m_shaderManager     = ShaderManager(m_device); // TODO Fins a better place to set this up maybe a setup managers func
	m_modelManager      = ModelManager(this, m_device, m_allocator, m_pathManager);
	m_descriptorManager = DescriptorManager(m_device);
	m_inputManager	  = InputManager(m_window.GetWindow());

	// TODO move lights outta here
	for (int i = 0; i < MAX_LIGHTS; i++)
	{
		LightObject& lightObject = m_lights.emplace_back();

		Light& light           = lightObject.light;
		light.lightPos         = glm::vec3(0.0f, 0.0f, 3.0f);
		light.lightColor       = glm::vec3(1.0f, 0.85f, 0.9f);
		light.viewPos          = glm::vec3(0.0f, 0.0f, 3.0f);
		light.ambientStrength  = 0.1f;
		light.specularStrength = 0.5f;
		light.shininess        = 10.0f;

		// Create buffer for light
		CreateBuffer(sizeof(Light), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, lightObject.buffer, lightObject.allocation);
		CopyStructToBuffer(light, lightObject.buffer, lightObject.allocation);
	}

	return 0;
}

VkCommandBuffer Engine::BeginSingleTimeCommands()
{
	VkCommandBufferAllocateInfo alloc_info = {};
	alloc_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandPool                 = data.commandPool; // Assuming you have a command pool created
	alloc_info.commandBufferCount          = 1;

	VkCommandBuffer command_buffer;
	if (vkAllocateCommandBuffers(m_device, &alloc_info, &command_buffer) != VK_SUCCESS)
	{
		spdlog::error("Failed to allocate command buffer!");
		return VK_NULL_HANDLE;
	}

	VkCommandBufferBeginInfo begin_info = {};
	begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS)
	{
		spdlog::error("Failed to begin recording command buffer!");
		return VK_NULL_HANDLE;
	}

	return command_buffer;
}

void Engine::EndSingleTimeCommands(VkCommandBuffer command_buffer)
{
	if (command_buffer == VK_NULL_HANDLE)
	{
		return;
	}

	if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS)
	{
		spdlog::error("Failed to record command buffer!");
		return;
	}

	VkSubmitInfo submit_info       = {};
	submit_info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &command_buffer;

	VkQueue graphics_queue = data.graphicsQueue; // Assuming you have a graphics queue defined

	if (vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS)
	{
		spdlog::error("Failed to submit command buffer!");
		return;
	}

	// Wait for the command buffer to finish execution
	if (vkQueueWaitIdle(graphics_queue) != VK_SUCCESS)
	{
		spdlog::error("Failed to wait for queue to finish!");
		return;
	}

	// Free the command buffer
	vkFreeCommandBuffers(m_device, data.commandPool, 1, &command_buffer);
}

int Engine::DeviceInit()
{
	spdlog::info("Initializing Vulkan...");

	// set up the debug messenger to use spdlog
	auto debugCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) -> VkBool32 {
		switch (messageSeverity)
		{
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			spdlog::debug("{}", pCallbackData->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			spdlog::info("{}", pCallbackData->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			spdlog::warn("{}\n", pCallbackData->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			spdlog::error("{}\n", pCallbackData->pMessage);
			break;
		default:
			spdlog::error("Unknown message severity: {}", pCallbackData->pMessage);
		}

		return VK_FALSE;
	};

	// Create instance with VK_KHR_dynamic_rendering extension
	spdlog::info("Creating Vulkan instance...");
	vkb::InstanceBuilder instance_builder;
	auto instance_ret = instance_builder
		.request_validation_layers(true)
		.set_debug_messenger_severity(
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		.set_debug_callback(debugCallback)
		.require_api_version(1, 3, 0)
		.build();

	if (!instance_ret)
	{
		spdlog::error("Failed to create instance: {}", instance_ret.error().message());
		return -1;
	}
	spdlog::info("Vulkan instance created.");

	m_instance = instance_ret.value();
	m_instDisp = m_instance.make_table();

	m_surface = m_window.CreateSurface(m_instance.instance);

	// Select physical device //
	spdlog::info("Selecting physical device...");
	VkPhysicalDeviceVulkan13Features features13 = {};
	features13.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering                 = VK_TRUE;
	features13.synchronization2                 = VK_TRUE;

	VkPhysicalDeviceVulkan12Features features12 = {};
	features12.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.bufferDeviceAddress              = VK_TRUE;
	features12.descriptorIndexing               = VK_TRUE;

	VkPhysicalDeviceFeatures features = {};
	features.fillModeNonSolid         = VK_TRUE;
	features.wideLines                = VK_TRUE;

	vkb::PhysicalDeviceSelector phys_device_selector(m_instance);

	auto phys_device_ret = phys_device_selector
		.set_minimum_version(1, 3)
		.set_required_features_13(features13)
		.set_required_features_12(features12)
		.set_required_features(features)
		.set_surface(m_surface)
		.select();

	if (!phys_device_ret)
	{
		spdlog::error("Failed to select physical device: {}", phys_device_ret.error().message());
		return -1;
	}
	spdlog::info("Physical device selected.");
	spdlog::info("Physical device: {}", phys_device_ret.value().properties.deviceName);

	vkb::PhysicalDevice physical_device = phys_device_ret.value();

	// Create device (extension is automatically enabled as it was required in PhysicalDeviceSelector)
	spdlog::info("Creating logical device...");
	vkb::DeviceBuilder device_builder{ physical_device };
	auto device_ret = device_builder.build();

	if (!device_ret)
	{
		spdlog::error("Failed to create logical device: {}", device_ret.error().message());
		return -1;
	}
	spdlog::info("Logical device created.");
	m_device = device_ret.value();

	m_disp = m_device.make_table();

	// Setup the VMA allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice         = physical_device.physical_device;
	allocatorInfo.device                 = m_device.device;
	allocatorInfo.instance               = m_instance.instance;

	if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create VMA allocator");
	}

	m_debugUtils = VulkanDebugUtils(m_instance, m_device);

	return 0;
}

int Engine::CreateSwapchain()
{
	spdlog::info("Creating swapchain...");
	vkDeviceWaitIdle(m_device);

	vkb::SwapchainBuilder swapchain_builder(m_device, m_surface);

	auto swap_ret = swapchain_builder
		.use_default_format_selection()
		.set_desired_format(VkSurfaceFormatKHR{
			.format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
		})
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // Use vsync present mode
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.set_old_swapchain(m_swapchain)
		.build();

	if (!swap_ret)
	{
		spdlog::error("Failed to create swapchain: {}", swap_ret.error().message());
		return -1;
	}

	vkb::destroy_swapchain(m_swapchain);
	m_swapchain = swap_ret.value();

	// Delete old image views
	for (auto& image_view : data.swapchainImageViews)
	{
		m_disp.destroyImageView(image_view, nullptr);
	}

	data.swapchainImages     = m_swapchain.get_images().value();
	data.swapchainImageViews = m_swapchain.get_image_views().value();

	// Clean up old depth image and image view
	vmaDestroyImage(m_allocator, data.depthImage, data.depthImageAllocation);
	m_disp.destroyImageView(data.depthImageView, nullptr);

	// Create the depth image
	VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
	VkImageCreateInfo depthImageInfo = {};
	depthImageInfo.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	depthImageInfo.imageType         = VK_IMAGE_TYPE_2D;
	depthImageInfo.extent.width      = m_swapchain.extent.width;
	depthImageInfo.extent.height     = m_swapchain.extent.height;
	depthImageInfo.extent.depth      = 1;
	depthImageInfo.mipLevels         = 1;
	depthImageInfo.arrayLayers       = 1;
	depthImageInfo.format            = depthFormat;
	depthImageInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
	depthImageInfo.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
	depthImageInfo.usage             = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthImageInfo.samples           = VK_SAMPLE_COUNT_1_BIT;
	depthImageInfo.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo depthAllocInfo = {};
	depthAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	vmaCreateImage(m_allocator, &depthImageInfo, &depthAllocInfo, &data.depthImage, &data.depthImageAllocation, nullptr);

	// Create the depth image view
	VkImageViewCreateInfo depthImageViewInfo = {};
	depthImageViewInfo.sType                 = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	depthImageViewInfo.image                 = data.depthImage;
	depthImageViewInfo.viewType              = VK_IMAGE_VIEW_TYPE_2D;
	depthImageViewInfo.format                = depthFormat;
	depthImageViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthImageViewInfo.subresourceRange.baseMipLevel   = 0;
	depthImageViewInfo.subresourceRange.levelCount     = 1;
	depthImageViewInfo.subresourceRange.baseArrayLayer = 0;
	depthImageViewInfo.subresourceRange.layerCount     = 1;

	m_disp.createImageView(&depthImageViewInfo, nullptr, &data.depthImageView);

	return 0;
}

int Engine::GetQueues()
{
	auto gq = m_device.get_queue(vkb::QueueType::graphics);
	if (!gq.has_value())
	{
		spdlog::error("failed to get graphics queue: {}", gq.error().message());
		return -1;
	}
	data.graphicsQueue = gq.value();

	auto pq = m_device.get_queue(vkb::QueueType::present);
	if (!pq.has_value())
	{
		spdlog::error("failed to get present queue: {}", pq.error().message());
		return -1;
	}
	data.presentQueue = pq.value();
	return 0;
}

VkShaderModule Engine::CreateShaderModule(const std::vector<char>& code)
{
	spdlog::info("Creating shader module...");
	VkShaderModuleCreateInfo create_info = {};
	create_info.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create_info.codeSize                 = code.size();
	create_info.pCode                    = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule shaderModule;
	if (m_disp.createShaderModule(&create_info, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return VK_NULL_HANDLE; // failed to create shader module
	}

	return shaderModule;
}

int Engine::CreateCommandPool()
{
	spdlog::info("Creating command pool...");
	VkCommandPoolCreateInfo pool_info = {};
	pool_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.queueFamilyIndex        = m_device.get_queue_index(vkb::QueueType::graphics).value();
	// Assuming graphics queue family
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	if (m_disp.createCommandPool(&pool_info, nullptr, &data.commandPool) != VK_SUCCESS)
	{
		spdlog::error("Failed to create command pool!");
		return -1;
	}

	return 0;
}

int Engine::CreateRenderCommandBuffers()
{
	spdlog::info("Recording command buffers...");
	data.renderCommandBuffers.resize(m_swapchain.image_count);

	VkCommandBufferAllocateInfo alloc_info = {};
	alloc_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.commandPool                 = data.commandPool;
	alloc_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount          = static_cast<uint32_t>(data.renderCommandBuffers.size());

	if (m_disp.allocateCommandBuffers(&alloc_info, data.renderCommandBuffers.data()) != VK_SUCCESS)
	{
		spdlog::error("Failed to allocate command buffers!");
		return -1;
	}

	return 0;
}

void Engine::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkBuffer& buffer, VmaAllocation& allocation)
{
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size        = size;
	bufferInfo.usage       = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = memoryUsage;

	if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create buffer!");
	}
}

template <typename T> void Engine::CopyStructToBuffer(T& data, VkBuffer buffer, VmaAllocation allocation)
{
	void* mappedData;
	vmaMapMemory(m_allocator, allocation, &mappedData);
	memcpy(mappedData, &data, sizeof(T));
	vmaUnmapMemory(m_allocator, allocation);
}

void Engine::SetupViewportAndScissor(VkCommandBuffer& cmd)
{
	VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = static_cast<float>(m_swapchain.extent.width),
		.height = static_cast<float>(m_swapchain.extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};

	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = m_swapchain.extent
	};

	m_disp.cmdSetViewport(cmd, 0, 1, &viewport);
	m_disp.cmdSetScissor(cmd, 0, 1, &scissor);
}

void Engine::SetupDepthTestingAndLineWidth(VkCommandBuffer& cmd)
{
	m_disp.cmdSetDepthTestEnable(cmd, VK_TRUE);
	m_disp.cmdSetDepthWriteEnable(cmd, VK_TRUE);
	m_disp.cmdSetDepthCompareOp(cmd, VK_COMPARE_OP_LESS);
	m_disp.cmdSetLineWidth(cmd, 10.0f);
}

void Engine::DrawModels(VkCommandBuffer& cmd)
{
	m_debugUtils.BeginDebugMarker(cmd, "Draw Models", debugUtil_BeginColour);

	// Update light buffer once
	m_debugUtils.InsertDebugMarker(cmd, "Update Light Buffer", debugUtil_UpdateLightBufferColour);
	CopyStructToBuffer(m_lights.at(0).light, m_lights.at(0).buffer, m_lights.at(0).allocation);

	// Store the currently bound descriptor set
	VkDescriptorSet boundDescriptorSet = VK_NULL_HANDLE;

	for (const auto& [name, model] : m_modelManager)
	{
		if (!model.isActive)
			continue;

		std::string pipelineName = model.pipeLineName;
		auto pipelineIt          = data.pipelines.find(pipelineName);

		if (pipelineIt == data.pipelines.end())
		{
			spdlog::error("Pipeline not found: {}", pipelineName);
			continue;
		}

		m_debugUtils.BeginDebugMarker(cmd, ("Draw Model: " + name).c_str(), debugUtil_StartDrawColour);

		PipelineGenerator* pipeline = pipelineIt->second.get();
		m_disp.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetPipeline());

		auto descSets = pipeline->GetDescriptorSets();
		if (descSets.empty() || descSets[0] == VK_NULL_HANDLE)
		{
			spdlog::error("Invalid descriptor set for pipeline: {}", pipelineName);
			m_debugUtils.EndDebugMarker(cmd);
			continue;
		}

		// Bind descriptor set
		if (descSets[0] != boundDescriptorSet)
		{
			boundDescriptorSet = descSets[0];
			m_debugUtils.InsertDebugMarker(cmd, "Bind Descriptor Set", debugUtil_BindDescriptorSetColour);
			m_disp.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetPipelineLayout(), 0, 1, descSets.data(), 0, nullptr);

			// Bind light buffer only when descriptor set changes
			m_descriptorManager.BindBuffer(descSets[0], 0, m_lights.at(0).buffer, 0, sizeof(Light));
		}

		// Push Constants
		m_mvp.view  = m_camera.getViewMatrix();
		m_mvp.proj  = m_camera.getProjectionMatrix();
		m_mvp.model = model.model;

		m_debugUtils.InsertDebugMarker(cmd, "Push Constant MVP", debugUtil_PushConstantsColour);
		m_disp.cmdPushConstants(cmd, pipeline->GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(m_mvp), &m_mvp);

		m_modelManager.DrawModel(cmd, model);

		m_debugUtils.EndDebugMarker(cmd);
	}

	m_debugUtils.EndDebugMarker(cmd);
}

int Engine::BeginCommandBuffer(VkCommandBuffer& cmd)
{
	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
	};

	if (m_disp.beginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
	{
		spdlog::error("Failed to begin recording command buffer!");
		return -1;
	}
	return 0;
}

int Engine::EndCommandBuffer(VkCommandBuffer& cmd)
{
	if (m_disp.endCommandBuffer(cmd) != VK_SUCCESS)
	{
		spdlog::error("Failed to record command buffer!");
		return -1;
	}
	return 0;
}

int Engine::Draw(VkCommandBuffer& cmd, int imageIndex)
{
    if (BeginCommandBuffer(cmd) != 0)
        return -1;

    SetupViewportAndScissor(cmd);
    SetupDepthTestingAndLineWidth(cmd);

    // Transition color image to color attachment optimal
    m_modelManager.TransitionImageLayout(data.swapchainImages[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    // Transition depth image to depth attachment optimal
    m_modelManager.TransitionImageLayout(data.depthImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingAttachmentInfo colorAttachmentInfo = {};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachmentInfo.imageView = data.swapchainImageViews[imageIndex];
    colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentInfo.clearValue = { .color = { { 0.05f, 0.05f, 0.05f, 1.0f } } };

    VkRenderingAttachmentInfo depthAttachmentInfo = {};
    depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachmentInfo.imageView = data.depthImageView;
    depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachmentInfo.clearValue = { .depthStencil = { 1.0f, 0 } };  // Clear to 0 for reverse depth

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { .offset = { 0, 0 }, .extent = m_swapchain.extent };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;

    m_disp.cmdBeginRendering(cmd, &renderingInfo);

    DrawModels(cmd);

    m_disp.cmdEndRendering(cmd);

    // Transition color image to present src layout
    m_modelManager.TransitionImageLayout(data.swapchainImages[imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    return EndCommandBuffer(cmd);
}

int Engine::RenderFrame()
{
	if (m_window.WindowSuspended())
	{
		return 0;
	}

	// Wait for the frame to be finished
	if (m_disp.waitForFences(1, &data.inFlightFences[data.currentFrame], VK_TRUE, UINT64_MAX) != VK_SUCCESS)
	{
		spdlog::error("Failed to wait for fence!");
		return -1;
	}

	uint32_t image_index;
	VkResult result = m_disp.acquireNextImageKHR(m_swapchain, UINT64_MAX, data.availableSemaphores[data.currentFrame], VK_NULL_HANDLE, &image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		if (CreateSwapchain() != 0)
			return -1;
		return 0;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		spdlog::error("Failed to acquire swapchain image!");
		return -1;
	}

	// Begin command buffer recording
	VkCommandBuffer cmd = data.renderCommandBuffers[image_index];

	if (Draw(cmd, image_index) != 0)
		return -1;

	// Mark the image as now being in use by this frame
	data.imageInFlight[image_index] = data.inFlightFences[data.currentFrame];

	VkSubmitInfo submit_info = {};
	submit_info.sType        = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore wait_semaphores[]      = { data.availableSemaphores[data.currentFrame] };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submit_info.waitSemaphoreCount     = 1;
	submit_info.pWaitSemaphores        = wait_semaphores;
	submit_info.pWaitDstStageMask      = wait_stages;

	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &cmd;

	VkSemaphore signal_semaphores[]  = { data.finishedSemaphore[data.currentFrame] };
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores    = signal_semaphores;

	m_disp.resetFences(1, &data.inFlightFences[data.currentFrame]);

	if (m_disp.queueSubmit(data.graphicsQueue, 1, &submit_info, data.inFlightFences[data.currentFrame]) != VK_SUCCESS)
	{
		spdlog::error("Failed to submit draw command buffer!");
		return -1;
	}

	VkPresentInfoKHR present_info   = {};
	present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores    = signal_semaphores;

	VkSwapchainKHR swapchains[] = { m_swapchain };
	present_info.swapchainCount = 1;
	present_info.pSwapchains    = swapchains;
	present_info.pImageIndices  = &image_index;

	result = m_disp.queuePresentKHR(data.presentQueue, &present_info);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_window.ShouldRecreateSwapchain())
	{
		if (CreateSwapchain() != 0)
			return -1;
	}
	else if (result != VK_SUCCESS)
	{
		spdlog::error("Failed to present swapchain image!");
		return -1;
	}

	data.currentFrame = (data.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

	return 0;
}

int Engine::InitSyncObjects()
{
	spdlog::info("Initializing synchronization objects...");
	// Create synchronization objects
	data.availableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
	data.finishedSemaphore.resize(MAX_FRAMES_IN_FLIGHT);
	data.inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
	data.imageInFlight.resize(m_swapchain.image_count, VK_NULL_HANDLE);

	VkSemaphoreCreateInfo semaphore_info = {};
	semaphore_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fence_info = {};
	fence_info.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags             = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		if (m_disp.createSemaphore(&semaphore_info, nullptr, &data.availableSemaphores[i]) != VK_SUCCESS ||
		    m_disp.createSemaphore(&semaphore_info, nullptr, &data.finishedSemaphore[i]) != VK_SUCCESS ||
		    m_disp.createFence(&fence_info, nullptr, &data.inFlightFences[i]) != VK_SUCCESS)
		{
			spdlog::error("Failed to create synchronization objects!");
			return -1;
		}
	}

	return 0;
}

int Engine::Cleanup()
{
	spdlog::info("Cleaning up...");
	vkDeviceWaitIdle(m_device);

	// Destroy synchronization objects
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		m_disp.destroySemaphore(data.availableSemaphores[i], nullptr);
		m_disp.destroySemaphore(data.finishedSemaphore[i], nullptr);
		m_disp.destroyFence(data.inFlightFences[i], nullptr);
	}

	for (auto& image_view : data.swapchainImageViews)
	{
		m_disp.destroyImageView(image_view, nullptr);
	}

	m_modelManager.UnloadAllResources();

	m_shaderManager.CleanupDescriptorSetLayouts();
	data.pipelines.clear();

	m_descriptorManager.Cleanup();

	// TODO REMOVE THIS Clean up the lights
	for (auto& light : m_lights)
	{
		vmaDestroyBuffer(m_allocator, light.buffer, light.allocation);
	}

	// Clean up old depth image and image view
	vmaDestroyImage(m_allocator, data.depthImage, data.depthImageAllocation);
	m_disp.destroyImageView(data.depthImageView, nullptr);

	m_shaderManager.CleanupShaderModules();

	m_disp.destroyCommandPool(data.commandPool, nullptr);

	vkb::destroy_swapchain(m_swapchain);
	vkb::destroy_surface(m_instance, m_surface);

	// Destroy the allocator
	vmaDestroyAllocator(m_allocator);

	vkb::destroy_device(m_device);
	vkb::destroy_instance(m_instance);

	return 0;
}