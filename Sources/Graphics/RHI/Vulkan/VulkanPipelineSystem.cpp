#include "Core/Precompiled.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"
#include "Graphics/RHI/Vulkan/VulkanDescriptorManager.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanShaderBindingABI.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace gglab
{
	namespace
	{
		[[nodiscard]] VkShaderStageFlags ToVulkanShaderStages(RHIShaderStage stages) noexcept
		{
			VkShaderStageFlags result = 0;
			if (Test(stages, RHIShaderStage::Vertex))
			{
				result |= VK_SHADER_STAGE_VERTEX_BIT;
			}
			if (Test(stages, RHIShaderStage::Pixel))
			{
				result |= VK_SHADER_STAGE_FRAGMENT_BIT;
			}
			if (Test(stages, RHIShaderStage::Compute))
			{
				result |= VK_SHADER_STAGE_COMPUTE_BIT;
			}
			return result;
		}

		[[nodiscard]] VulkanShaderRegisterClass ToVulkanRegisterClass(
			RHIBindingType type) noexcept
		{
			switch (type)
			{
			case RHIBindingType::ConstantBuffer:
			case RHIBindingType::PushConstants:
				return VulkanShaderRegisterClass::ConstantBuffer;
			case RHIBindingType::ReadOnlyStorageBuffer:
			case RHIBindingType::SampledTexture:
				return VulkanShaderRegisterClass::ShaderResource;
			case RHIBindingType::ReadWriteStorageBuffer:
			case RHIBindingType::StorageTexture:
				return VulkanShaderRegisterClass::UnorderedAccess;
			case RHIBindingType::Sampler:
				return VulkanShaderRegisterClass::Sampler;
			default:
				return VulkanShaderRegisterClass::ConstantBuffer;
			}
		}

		[[nodiscard]] VkDescriptorType ToVulkanDescriptorType(RHIBindingType type) noexcept
		{
			switch (type)
			{
			case RHIBindingType::ConstantBuffer:
				return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			case RHIBindingType::ReadOnlyStorageBuffer:
			case RHIBindingType::ReadWriteStorageBuffer:
				return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			case RHIBindingType::SampledTexture:
				return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			case RHIBindingType::StorageTexture:
				return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			case RHIBindingType::Sampler:
				return VK_DESCRIPTOR_TYPE_SAMPLER;
			case RHIBindingType::PushConstants:
				return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			default:
				return VK_DESCRIPTOR_TYPE_MAX_ENUM;
			}
		}
	}

	VulkanBindingLayoutPlan BuildVulkanBindingLayoutPlan(
		const RHIBindingLayoutDesc& desc, uint32_t maxUniformBufferRange) noexcept
	{
		VulkanBindingLayoutPlan plan{};
		plan.m_ParameterToDynamicOffsetSlot.fill(UINT32_MAX);
		if (desc.m_SlotCount > RHIBindingLayoutDesc::MaxSlots)
		{
			plan.m_Error = VulkanBindingLayoutError::TooManySlots;
			return plan;
		}

		for (uint32_t parameterIndex = 0; parameterIndex < desc.m_SlotCount; ++parameterIndex)
		{
			const RHIBindingSlotDesc& slot = desc.m_Slots[parameterIndex];
			if (slot.m_Type == RHIBindingType::Unknown)
			{
				plan.m_Error = VulkanBindingLayoutError::UnknownBindingType;
				return plan;
			}
			if (IsBindlessBindingType(slot.m_Type))
			{
				if (slot.m_Count != 0)
				{
					plan.m_Error = VulkanBindingLayoutError::InvalidDescriptorCount;
					return plan;
				}
				continue;
			}
			if (slot.m_Count == 0)
			{
				plan.m_Error = VulkanBindingLayoutError::InvalidDescriptorCount;
				return plan;
			}
			if (slot.m_Type == RHIBindingType::PushConstants &&
				(slot.m_Count != 1 || slot.m_SizeInBytes == 0 ||
					slot.m_SizeInBytes % sizeof(uint32_t) != 0 ||
					slot.m_SizeInBytes > maxUniformBufferRange))
			{
				plan.m_Error = VulkanBindingLayoutError::InvalidPushConstantSize;
				return plan;
			}

			const VulkanShaderBindingResult location = EvaluateVulkanFixedShaderBinding(
				ToVulkanRegisterClass(slot.m_Type), slot.m_Binding, slot.m_Space);
			if (!location.IsSupported())
			{
				plan.m_Error = VulkanBindingLayoutError::UnsupportedRegisterSpace;
				return plan;
			}
			VulkanSet0BindingPlan& binding = plan.m_Set0Bindings[plan.m_Set0BindingCount++];
			binding.m_LogicalParameterIndex = parameterIndex;
			binding.m_Binding = location.m_Location.m_Binding;
			binding.m_DescriptorType = ToVulkanDescriptorType(slot.m_Type);
			binding.m_DescriptorCount = slot.m_Count;
			binding.m_StageFlags = ToVulkanShaderStages(slot.m_Visibility);
			binding.m_SizeInBytes = slot.m_SizeInBytes;
			if (binding.m_DescriptorType == VK_DESCRIPTOR_TYPE_MAX_ENUM ||
				binding.m_StageFlags == 0)
			{
				plan.m_Error = VulkanBindingLayoutError::UnknownBindingType;
				return plan;
			}
		}

		std::sort(plan.m_Set0Bindings.begin(),
			plan.m_Set0Bindings.begin() + plan.m_Set0BindingCount,
			[](const VulkanSet0BindingPlan& left, const VulkanSet0BindingPlan& right) noexcept
			{
				return left.m_Binding < right.m_Binding;
			});
		for (uint32_t bindingIndex = 0; bindingIndex < plan.m_Set0BindingCount; ++bindingIndex)
		{
			VulkanSet0BindingPlan& binding = plan.m_Set0Bindings[bindingIndex];
			if (bindingIndex > 0 &&
				plan.m_Set0Bindings[bindingIndex - 1].m_Binding == binding.m_Binding)
			{
				plan.m_Error = VulkanBindingLayoutError::DuplicateNativeBinding;
				return plan;
			}
			if (binding.m_DescriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
			{
				binding.m_DynamicOffsetSlot = plan.m_DynamicOffsetCount++;
				plan.m_ParameterToDynamicOffsetSlot[binding.m_LogicalParameterIndex] =
					binding.m_DynamicOffsetSlot;
			}
		}
		return plan;
	}

	VulkanBindingLayout::~VulkanBindingLayout() noexcept
	{
		Release();
	}

	bool VulkanBindingLayout::Create(VulkanDevice* device, const RHIBindingLayoutDesc& desc,
		VkDescriptorSetLayout globalSetLayout) noexcept
	{
		if (m_Device != nullptr || device == nullptr || globalSetLayout == VK_NULL_HANDLE ||
			!device->RequireOwnerThread("VulkanBindingLayout::Create"))
		{
			return false;
		}
		m_Device = device;
		m_Plan = BuildVulkanBindingLayoutPlan(
			desc, device->GetPhysicalDeviceLimits().maxUniformBufferRange);
		if (!m_Plan.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Vulkan binding layout lowering failed with error {}.",
				static_cast<uint32_t>(m_Plan.m_Error));
			Release();
			return false;
		}

		std::array<VkDescriptorSetLayoutBinding, RHIBindingLayoutDesc::MaxSlots> bindings{};
		for (uint32_t index = 0; index < m_Plan.m_Set0BindingCount; ++index)
		{
			const VulkanSet0BindingPlan& source = m_Plan.m_Set0Bindings[index];
			bindings[index] = {
				.binding = source.m_Binding,
				.descriptorType = source.m_DescriptorType,
				.descriptorCount = source.m_DescriptorCount,
				.stageFlags = source.m_StageFlags,
			};
		}
		VkDescriptorSetLayoutCreateInfo set0Info{};
		set0Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		set0Info.bindingCount = m_Plan.m_Set0BindingCount;
		set0Info.pBindings = bindings.data();
		VkDescriptorSetLayoutSupport support{};
		support.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT;
		vkGetDescriptorSetLayoutSupport(device->Get(), &set0Info, &support);
		if (support.supported != VK_TRUE)
		{
			GGLAB_LOG_GRAPHICS_ERROR("Vulkan set-0 descriptor layout is not supported.");
			Release();
			return false;
		}

		VkResult result =
			vkCreateDescriptorSetLayout(device->Get(), &set0Info, nullptr, &m_Set0Layout);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"vkCreateDescriptorSetLayout(set 0) failed with {}.", ToString(result));
			Release();
			return false;
		}

		const std::array<VkDescriptorSetLayout, 2> setLayouts{
			m_Set0Layout,
			globalSetLayout,
		};
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		pipelineLayoutInfo.pSetLayouts = setLayouts.data();
		result =
			vkCreatePipelineLayout(device->Get(), &pipelineLayoutInfo, nullptr, &m_PipelineLayout);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"vkCreatePipelineLayout failed with {}.", ToString(result));
			Release();
			return false;
		}

		const char* debugName = desc.m_DebugName ? desc.m_DebugName : "Vulkan.BindingLayout";
		SetVulkanObjectDebugName(device->Get(), VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
			reinterpret_cast<uint64_t>(m_Set0Layout), debugName);
		SetVulkanObjectDebugName(device->Get(), VK_OBJECT_TYPE_PIPELINE_LAYOUT,
			reinterpret_cast<uint64_t>(m_PipelineLayout), debugName);
		return true;
	}

	void VulkanBindingLayout::Release() noexcept
	{
		if (m_Device == nullptr)
		{
			return;
		}
		if (m_PipelineLayout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(m_Device->Get(), m_PipelineLayout, nullptr);
			m_PipelineLayout = VK_NULL_HANDLE;
		}
		if (m_Set0Layout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(m_Device->Get(), m_Set0Layout, nullptr);
			m_Set0Layout = VK_NULL_HANDLE;
		}
		m_Plan = {};
		m_Device = nullptr;
	}

	VulkanPipelineSystem::VulkanPipelineSystem(VulkanDevice* device) noexcept : m_Device(device)
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
	}

	VulkanPipelineSystem::~VulkanPipelineSystem() = default;

	RHIBindingLayoutHandle VulkanPipelineSystem::CreateBindingLayout(
		const RHIBindingLayoutDesc& desc) noexcept
	{
		if (!m_Device->RequireOwnerThread("VulkanPipelineSystem::CreateBindingLayout"))
		{
			return {};
		}
		auto layout = std::make_unique<VulkanBindingLayout>();
		if (!layout->Create(m_Device, desc,
			m_Device->GetDescriptorManager().GetGlobalSetLayout()))
		{
			return {};
		}
		const auto index =
			static_cast<RHIBindingLayoutHandle::IndexType>(m_BindingLayouts.size());
		m_BindingLayouts.push_back({
			.m_Layout = std::move(layout),
			.m_DebugName = desc.m_DebugName ? desc.m_DebugName : "",
			});
		return RHIBindingLayoutHandle(index, m_BindingLayoutGeneration);
	}

	RHIPipelineHandle VulkanPipelineSystem::CreateGraphicsPipeline(
		const RHIGraphicsPipelineCreateInfo& createInfo) noexcept
	{
		GGLAB_UNUSED(createInfo);
		GGLAB_LOG_GRAPHICS_WARN(
			"Vulkan graphics pipeline creation is not available in the active backend path.");
		return {};
	}

	RHIPipelineHandle VulkanPipelineSystem::CreateComputePipeline(
		const RHIComputePipelineCreateInfo& createInfo) noexcept
	{
		GGLAB_UNUSED(createInfo);
		GGLAB_LOG_GRAPHICS_WARN(
			"Vulkan compute pipeline creation is not available in the active backend path.");
		return {};
	}

	bool VulkanPipelineSystem::IsAlive(RHIBindingLayoutHandle layout) const noexcept
	{
		return ResolveBindingLayout(layout) != nullptr;
	}

	bool VulkanPipelineSystem::IsAlive(RHIPipelineHandle pipeline) const noexcept
	{
		GGLAB_UNUSED(pipeline);
		return false;
	}

	void VulkanPipelineSystem::Clear() noexcept
	{
		++m_PipelineGeneration;
		if (m_PipelineGeneration == RHIPipelineHandle::InvalidGeneration)
		{
			++m_PipelineGeneration;
		}
		++m_Revision;
	}

	VkShaderModule VulkanPipelineSystem::CreateShaderModule(
		const ShaderBytecode& bytecode, const char* debugName) noexcept
	{
		constexpr uint32_t SpirVMagic = 0x07230203u;
		if (!m_Device->RequireOwnerThread("VulkanPipelineSystem::CreateShaderModule") ||
			!bytecode.IsValid() || bytecode.m_Format != ShaderBinaryFormat::SpirV ||
			bytecode.m_SizeInBytes % sizeof(uint32_t) != 0)
		{
			return VK_NULL_HANDLE;
		}
		uint32_t magic = 0;
		std::memcpy(&magic, bytecode.m_Data, sizeof(magic));
		if (magic != SpirVMagic)
		{
			GGLAB_LOG_GRAPHICS_ERROR("Vulkan shader module rejected invalid SPIR-V magic.");
			return VK_NULL_HANDLE;
		}

		std::vector<uint32_t> alignedCode(bytecode.m_SizeInBytes / sizeof(uint32_t));
		std::memcpy(alignedCode.data(), bytecode.m_Data, bytecode.m_SizeInBytes);
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = bytecode.m_SizeInBytes;
		createInfo.pCode = alignedCode.data();
		VkShaderModule shaderModule = VK_NULL_HANDLE;
		const VkResult result =
			vkCreateShaderModule(m_Device->Get(), &createInfo, nullptr, &shaderModule);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"vkCreateShaderModule failed with {}.", ToString(result));
			return VK_NULL_HANDLE;
		}
		SetVulkanObjectDebugName(m_Device->Get(), VK_OBJECT_TYPE_SHADER_MODULE,
			reinterpret_cast<uint64_t>(shaderModule),
			debugName ? debugName : "Vulkan.ShaderModule");
		return shaderModule;
	}

	void VulkanPipelineSystem::DestroyShaderModule(VkShaderModule shaderModule) noexcept
	{
		if (shaderModule != VK_NULL_HANDLE &&
			m_Device->RequireOwnerThread("VulkanPipelineSystem::DestroyShaderModule"))
		{
			vkDestroyShaderModule(m_Device->Get(), shaderModule, nullptr);
		}
	}

	VulkanBindingLayout* VulkanPipelineSystem::ResolveBindingLayout(
		RHIBindingLayoutHandle layout) noexcept
	{
		return const_cast<VulkanBindingLayout*>(std::as_const(*this).ResolveBindingLayout(layout));
	}

	const VulkanBindingLayout* VulkanPipelineSystem::ResolveBindingLayout(
		RHIBindingLayoutHandle layout) const noexcept
	{
		if (!layout.IsValid() || layout.Generation() != m_BindingLayoutGeneration ||
			layout.Index() >= m_BindingLayouts.size())
		{
			return nullptr;
		}
		return m_BindingLayouts[layout.Index()].m_Layout.get();
	}
}
