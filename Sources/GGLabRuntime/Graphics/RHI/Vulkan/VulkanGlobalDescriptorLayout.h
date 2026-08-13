#pragma once
#include "Graphics/RHI/Vulkan/VulkanShaderBindingABI.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace gglab
{
	[[nodiscard]] constexpr VkDescriptorType ToVkDescriptorType(
		VulkanDescriptorType type) noexcept
	{
		switch (type)
		{
		case VulkanDescriptorType::Mutable:
			return VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
		case VulkanDescriptorType::SampledImage:
			return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case VulkanDescriptorType::StorageImage:
			return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		case VulkanDescriptorType::Sampler:
			return VK_DESCRIPTOR_TYPE_SAMPLER;
		}
		return VK_DESCRIPTOR_TYPE_MAX_ENUM;
	}

	// Materializes the native global descriptor layout and pool declarations
	// from the shader ABI. The structure is intentionally non-copyable because
	// Vulkan create-info members point into its owned arrays.
	class VulkanGlobalDescriptorLayoutPlan
	{
	public:
		explicit VulkanGlobalDescriptorLayoutPlan(
			const VulkanShaderBindingABI& abi = GGLabVulkanShaderBindingABI) noexcept
		{
			for (uint32_t index = 0; index < m_MutableTypes.size(); ++index)
			{
				m_MutableTypes[index] = ToVkDescriptorType(
					abi.m_ResourceHeapMutableAllowedTypes[index]);
			}
			m_MutableLists[0] = {
				.descriptorTypeCount = static_cast<uint32_t>(m_MutableTypes.size()),
				.pDescriptorTypes = m_MutableTypes.data(),
			};
			m_MutableInfo = {
				.sType = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT,
				.mutableDescriptorTypeListCount = static_cast<uint32_t>(m_MutableLists.size()),
				.pMutableDescriptorTypeLists = m_MutableLists.data(),
			};

			VkDescriptorBindingFlags flags = 0;
			flags |= abi.m_PartiallyBound ? VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT : 0;
			flags |= abi.m_UpdateAfterBind ? VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT : 0;
			flags |= abi.m_UpdateUnusedWhilePending
				? VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT : 0;
			m_BindingFlags.fill(flags);
			m_BindingFlagsInfo = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
				.pNext = &m_MutableInfo,
				.bindingCount = static_cast<uint32_t>(m_BindingFlags.size()),
				.pBindingFlags = m_BindingFlags.data(),
			};

			m_Bindings = {
				VkDescriptorSetLayoutBinding{
					.binding = abi.m_ResourceHeapBinding,
					.descriptorType = ToVkDescriptorType(abi.m_ResourceHeapDescriptorType),
					.descriptorCount = abi.m_DescriptorCapacity.m_ResourceDescriptorCount,
					.stageFlags = VK_SHADER_STAGE_ALL,
				},
				VkDescriptorSetLayoutBinding{
					.binding = abi.m_SamplerHeapBinding,
					.descriptorType = ToVkDescriptorType(abi.m_SamplerHeapDescriptorType),
					.descriptorCount = abi.m_DescriptorCapacity.m_SamplerDescriptorCount,
					.stageFlags = VK_SHADER_STAGE_ALL,
				},
			};
			m_LayoutInfo = {
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.pNext = &m_BindingFlagsInfo,
				.flags = static_cast<VkDescriptorSetLayoutCreateFlags>(abi.m_UpdateAfterBind
					? VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT : 0),
				.bindingCount = static_cast<uint32_t>(m_Bindings.size()),
				.pBindings = m_Bindings.data(),
			};
			m_PoolSizes = {
				VkDescriptorPoolSize{
					.type = ToVkDescriptorType(abi.m_ResourceHeapDescriptorType),
					.descriptorCount = abi.m_DescriptorCapacity.m_ResourceDescriptorCount,
				},
				VkDescriptorPoolSize{
					.type = ToVkDescriptorType(abi.m_SamplerHeapDescriptorType),
					.descriptorCount = abi.m_DescriptorCapacity.m_SamplerDescriptorCount,
				},
			};
			m_PoolCreateFlags = static_cast<VkDescriptorPoolCreateFlags>(abi.m_UpdateAfterBind
				? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT : 0);
		}

		VulkanGlobalDescriptorLayoutPlan(const VulkanGlobalDescriptorLayoutPlan&) = delete;
		VulkanGlobalDescriptorLayoutPlan& operator=(const VulkanGlobalDescriptorLayoutPlan&) = delete;
		VulkanGlobalDescriptorLayoutPlan(VulkanGlobalDescriptorLayoutPlan&&) = delete;
		VulkanGlobalDescriptorLayoutPlan& operator=(VulkanGlobalDescriptorLayoutPlan&&) = delete;

		[[nodiscard]] const VkDescriptorSetLayoutCreateInfo& GetLayoutInfo() const noexcept
		{
			return m_LayoutInfo;
		}
		[[nodiscard]] const VkMutableDescriptorTypeCreateInfoEXT& GetMutableInfo() const noexcept
		{
			return m_MutableInfo;
		}
		[[nodiscard]] const std::array<VkDescriptorPoolSize, 2>& GetPoolSizes() const noexcept
		{
			return m_PoolSizes;
		}
		[[nodiscard]] VkDescriptorPoolCreateFlags GetPoolCreateFlags() const noexcept
		{
			return m_PoolCreateFlags;
		}

	private:
		std::array<VkDescriptorType, 2> m_MutableTypes{};
		std::array<VkMutableDescriptorTypeListEXT, 2> m_MutableLists{};
		VkMutableDescriptorTypeCreateInfoEXT m_MutableInfo{};
		std::array<VkDescriptorBindingFlags, 2> m_BindingFlags{};
		VkDescriptorSetLayoutBindingFlagsCreateInfo m_BindingFlagsInfo{};
		std::array<VkDescriptorSetLayoutBinding, 2> m_Bindings{};
		VkDescriptorSetLayoutCreateInfo m_LayoutInfo{};
		std::array<VkDescriptorPoolSize, 2> m_PoolSizes{};
		VkDescriptorPoolCreateFlags m_PoolCreateFlags = 0;
	};
}
