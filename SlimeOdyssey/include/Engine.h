//
// Created by alexm on 3/07/24.
//

#pragma once
#include "PipelineGenerator.h"
#include "ShaderManager.h"
#include "SlimeWindow.h"
#include <vector>

#include <VkBootstrap.h>
#include <map>
#include <memory>
#include <ModelManager.h>
#include <ResourcePathManager.h>
#include <vk_mem_alloc.h>

struct GLFWwindow;

class Engine
{
public:
	Engine(const char* name, int width, int height, bool resizable = false);
	~Engine();

	struct RenderData
	{
		VkQueue graphicsQueue;
		VkQueue presentQueue;

		std::vector<VkImage> swapchainImages;
		std::vector<VkImageView> swapchainImageViews;

		std::map<const char*, std::unique_ptr<PipelineGenerator>> pipelines;

		VkCommandPool commandPool;
		std::vector<VkCommandBuffer> renderCommandBuffers;

		std::vector<VkSemaphore> availableSemaphores;
		std::vector<VkSemaphore> finishedSemaphore;
		std::vector<VkFence> inFlightFences;
		std::vector<VkFence> imageInFlight;
		size_t currentFrame = 0;
	};

	VkCommandBuffer BeginSingleTimeCommands();
	void EndSingleTimeCommands(VkCommandBuffer command_buffer);
	int DeviceInit();
	int CreateImages();
	int CreateSwapchain();
	int GetQueues();
	VkShaderModule CreateShaderModule(const std::vector<char>& code);
	int CreateCommandPool();
	int CreateRenderCommandBuffers();
	int InitSyncObjects();
	int Draw(VkCommandBuffer& cmd, int imageIndex);
	int RenderFrame();

	int Cleanup();

	Window& GetWindow() { return m_window; }
	ShaderManager& GetShaderManager() { return m_shaderManager; }
	ModelManager& GetModelManager() { return m_modelManager; }
	std::map<const char*, std::unique_ptr<PipelineGenerator>>& GetPipelines() { return data.pipelines; }
	VkDevice GetDevice() { return m_device.device; }
	VkQueue GetGraphicsQueue() { return data.graphicsQueue; }
	VkQueue GetPresentQueue() { return data.presentQueue; }
	VkCommandPool GetCommandPool() { return data.commandPool; }
	VmaAllocator GetAllocator() { return m_allocator; }

	RenderData data; // Needs to be removed from here

private:
	Window m_window;

	vkb::Instance m_instance;
	vkb::InstanceDispatchTable m_instDisp;
	VkSurfaceKHR m_surface{};
	vkb::Device m_device;
	vkb::DispatchTable m_disp;
	vkb::Swapchain m_swapchain;
	VmaAllocator m_allocator{};
	ShaderManager m_shaderManager;
	ModelManager m_modelManager;
	ResourcePathManager m_pathManager;
};