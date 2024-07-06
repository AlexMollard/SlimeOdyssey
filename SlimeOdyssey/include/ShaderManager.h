//
// Created by alexm on 3/07/24.
//

#pragma once

#include "spirv_common.hpp"
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <vector>

struct ShaderModule
{
	VkShaderModule handle;
	std::vector<uint32_t> spirvCode;
	VkShaderStageFlagBits stage;

	ShaderModule(VkShaderModule _handle, std::vector<uint32_t> _spirvCode, VkShaderStageFlagBits _stage)
		: handle(_handle), spirvCode(std::move(_spirvCode)), stage(_stage)
	{
	}

	ShaderModule() = default;
};

class ShaderManager
{
public:
	struct ShaderResources
	{
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
		VkVertexInputBindingDescription bindingDescription;
		std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings;
		std::vector<VkPushConstantRange> pushConstantRanges;
	};

	ShaderManager() = default;
	explicit ShaderManager(VkDevice device);
	~ShaderManager();

	ShaderModule LoadShader(const std::string& path, VkShaderStageFlagBits stage);
	ShaderResources ParseShader(const ShaderModule& shaderModule);
	VkDescriptorSetLayout CreateDescriptorSetLayout(const std::vector<VkDescriptorSetLayoutBinding>& bindings);

	void CleanupShaderModules();
	void CleanupDescriptorSetLayouts();

private:
	VkDevice m_device;
	std::unordered_map<std::string, ShaderModule> m_shaderModules;
	std::unordered_map<std::string, VkDescriptorSetLayout> m_descriptorSetLayouts;

	std::vector<uint32_t> ReadFile(const std::string& filename);
	VkShaderModule CreateShaderModule(const std::vector<uint32_t>& code);
	VkFormat GetVkFormat(const spirv_cross::SPIRType& type);
};