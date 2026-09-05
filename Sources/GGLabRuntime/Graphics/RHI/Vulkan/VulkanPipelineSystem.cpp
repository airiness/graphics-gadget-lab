#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "Graphics/RHI/Vulkan/VulkanDescriptorManager.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanConversions.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineState.h"
#include "Graphics/RHI/Vulkan/VulkanShaderBindingABI.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool IsVulkanShaderBytecode(const ShaderBytecode& bytecode) noexcept
		{
			return !bytecode.IsValid() || bytecode.m_Format == ShaderBinaryFormat::SpirV;
		}

		[[nodiscard]] bool AreVertexInputsEqual(
			const RHIVertexInputLayoutDesc& left, const RHIVertexInputLayoutDesc& right) noexcept
		{
			if (left.m_AttributeCount != right.m_AttributeCount ||
				left.m_VertexBufferCount != right.m_VertexBufferCount)
			{
				return false;
			}
			for (uint32_t index = 0; index < left.m_AttributeCount; ++index)
			{
				const auto& a = left.m_Attributes[index];
				const auto& b = right.m_Attributes[index];
				if (a.m_SemanticIndex != b.m_SemanticIndex || a.m_Location != b.m_Location ||
					a.m_Format != b.m_Format || a.m_InputSlot != b.m_InputSlot ||
					a.m_AlignedByteOffset != b.m_AlignedByteOffset)
				{
					return false;
				}
			}
			for (uint32_t index = 0; index < left.m_VertexBufferCount; ++index)
			{
				const auto& a = left.m_VertexBuffers[index];
				const auto& b = right.m_VertexBuffers[index];
				if (a.m_InputSlot != b.m_InputSlot || a.m_StrideInBytes != b.m_StrideInBytes ||
					a.m_InputRate != b.m_InputRate || a.m_InstanceStepRate != b.m_InstanceStepRate)
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool AreGraphicsPipelineDescriptionsEqual(
			const RHIGraphicsPipelineDesc& left, const RHIGraphicsPipelineDesc& right) noexcept
		{
			if (left.m_BindingLayout != right.m_BindingLayout ||
				left.m_TopologyType != right.m_TopologyType ||
				left.m_PrimitiveTopology != right.m_PrimitiveTopology ||
				left.m_RenderTargetCount != right.m_RenderTargetCount ||
				left.m_DepthStencilFormat != right.m_DepthStencilFormat ||
				left.m_SampleCount != right.m_SampleCount ||
				left.m_SampleQuality != right.m_SampleQuality ||
				left.m_SampleMask != right.m_SampleMask ||
				left.m_Rasterizer.m_FillMode != right.m_Rasterizer.m_FillMode ||
				left.m_Rasterizer.m_CullMode != right.m_Rasterizer.m_CullMode ||
				left.m_Rasterizer.m_FrontCounterClockwise !=
				right.m_Rasterizer.m_FrontCounterClockwise ||
				left.m_Rasterizer.m_DepthBias != right.m_Rasterizer.m_DepthBias ||
				left.m_Rasterizer.m_DepthBiasClamp != right.m_Rasterizer.m_DepthBiasClamp ||
				left.m_Rasterizer.m_SlopeScaledDepthBias !=
				right.m_Rasterizer.m_SlopeScaledDepthBias ||
				left.m_Rasterizer.m_DepthClipEnable != right.m_Rasterizer.m_DepthClipEnable ||
				left.m_DepthStencil.m_DepthTestEnable != right.m_DepthStencil.m_DepthTestEnable ||
				left.m_DepthStencil.m_DepthWriteEnable != right.m_DepthStencil.m_DepthWriteEnable ||
				left.m_DepthStencil.m_DepthCompareOp != right.m_DepthStencil.m_DepthCompareOp ||
				left.m_DepthStencil.m_StencilEnable != right.m_DepthStencil.m_StencilEnable ||
				left.m_Blend.m_AlphaToCoverageEnable != right.m_Blend.m_AlphaToCoverageEnable ||
				!AreVertexInputsEqual(left.m_VertexInput, right.m_VertexInput))
			{
				return false;
			}
			for (uint32_t index = 0; index < RHIGraphicsPipelineDesc::MaxRenderTargets; ++index)
			{
				if (left.m_RenderTargetFormats[index] != right.m_RenderTargetFormats[index] ||
					left.m_Blend.m_RenderTargets[index] != right.m_Blend.m_RenderTargets[index])
				{
					return false;
				}
			}
			return true;
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

		uint32_t logicalParameterIndex = 0;
		for (uint32_t slotIndex = 0; slotIndex < desc.m_SlotCount; ++slotIndex)
		{
			const RHIBindingSlotDesc& slot = desc.m_Slots[slotIndex];
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
				ToVulkanShaderRegisterClass(slot.m_Type), slot.m_Binding, slot.m_Space);
			if (!location.IsSupported())
			{
				plan.m_Error = VulkanBindingLayoutError::UnsupportedRegisterSpace;
				return plan;
			}
			VulkanSet0BindingPlan& binding = plan.m_Set0Bindings[plan.m_Set0BindingCount++];
			binding.m_LogicalParameterIndex = logicalParameterIndex++;
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

	VulkanPipelineSystem::~VulkanPipelineSystem()
	{
		Clear();
	}

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
		BindingLayoutSlot& slot = m_BindingLayouts.emplace_back();
		slot.m_Layout = std::move(layout);
		slot.m_DebugName = desc.m_DebugName ? desc.m_DebugName : "";
		slot.m_SlotCount = desc.m_SlotCount;
		for (uint32_t slotIndex = 0; slotIndex < desc.m_SlotCount; ++slotIndex)
		{
			slot.m_Slots[slotIndex] = desc.m_Slots[slotIndex];
			slot.m_SlotDebugNames[slotIndex] =
				desc.m_Slots[slotIndex].m_DebugName ? desc.m_Slots[slotIndex].m_DebugName : "";
			slot.m_Slots[slotIndex].m_DebugName = nullptr;
		}
		return RHIBindingLayoutHandle(index, m_BindingLayoutGeneration);
	}

	RHIPipelineHandle VulkanPipelineSystem::CreateGraphicsPipeline(
		const RHIGraphicsPipelineCreateInfo& createInfo) noexcept
	{
		if (!m_Device->RequireOwnerThread("VulkanPipelineSystem::CreateGraphicsPipeline") ||
			!ValidateRHIGraphicsPipelinePortability(createInfo.m_Desc,
				m_Device->GetEnabledPortabilityCapabilities()).IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanPipelineSystem::CreateGraphicsPipeline rejected a non-portable description.");
			return {};
		}
		VulkanBindingLayout* bindingLayout = ResolveBindingLayout(createInfo.m_Desc.m_BindingLayout);
		const bool requiresPixelShader = createInfo.m_Desc.m_RenderTargetCount != 0;
		const bool hasPixelShader = createInfo.m_PixelShader.IsValid();
		if (bindingLayout == nullptr || !createInfo.m_VertexShader.IsValid() ||
			createInfo.m_VertexShader.m_EntryPoint.empty() ||
			(requiresPixelShader && !hasPixelShader) ||
			(hasPixelShader && createInfo.m_PixelShader.m_EntryPoint.empty()) ||
			!IsVulkanShaderBytecode(createInfo.m_VertexShader) ||
			!IsVulkanShaderBytecode(createInfo.m_PixelShader) ||
			!IsVulkanShaderBytecode(createInfo.m_DomainShader) ||
			!IsVulkanShaderBytecode(createInfo.m_HullShader) ||
			!IsVulkanShaderBytecode(createInfo.m_GeometryShader) ||
			createInfo.m_DomainShader.IsValid() || createInfo.m_HullShader.IsValid() ||
			createInfo.m_GeometryShader.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanPipelineSystem::CreateGraphicsPipeline received invalid or unsupported inputs.");
			return {};
		}

		std::string vertexEntry;
		std::string pixelEntry;
		if (!IsValidShaderRuntimeEntryPoint(createInfo.m_VertexShader.m_EntryPoint) ||
			(hasPixelShader && !IsValidShaderRuntimeEntryPoint(
				createInfo.m_PixelShader.m_EntryPoint)))
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Vulkan pipeline entry points must be non-empty ASCII HLSL identifiers.");
			return {};
		}
		vertexEntry = createInfo.m_VertexShader.m_EntryPoint;
		pixelEntry = createInfo.m_PixelShader.m_EntryPoint;

		const std::array shaderHashes{
			createInfo.m_VertexShader.m_Hash,
			createInfo.m_PixelShader.m_Hash,
			createInfo.m_DomainShader.m_Hash,
			createInfo.m_HullShader.m_Hash,
			createInfo.m_GeometryShader.m_Hash,
		};
		for (uint32_t index = 0; index < m_Pipelines.size(); ++index)
		{
			const PipelineSlot& slot = m_Pipelines[index];
			if (slot.m_Pipeline && slot.m_Type == PipelineType::Graphics &&
				slot.m_ShaderHashes == shaderHashes &&
				AreGraphicsPipelineDescriptionsEqual(slot.m_GraphicsDesc, createInfo.m_Desc))
			{
				return RHIPipelineHandle(index, m_PipelineGeneration);
			}
		}

		VkShaderModule vertexModule =
			CreateShaderModule(createInfo.m_VertexShader, "Vulkan.GraphicsPipeline.VS");
		VkShaderModule pixelModule = hasPixelShader
			? CreateShaderModule(createInfo.m_PixelShader, "Vulkan.GraphicsPipeline.PS")
			: VK_NULL_HANDLE;
		if (vertexModule == VK_NULL_HANDLE || (hasPixelShader && pixelModule == VK_NULL_HANDLE))
		{
			DestroyShaderModule(pixelModule);
			DestroyShaderModule(vertexModule);
			return {};
		}

		std::array<VulkanShaderStageModule, 2> stages{
			VulkanShaderStageModule{ VK_SHADER_STAGE_VERTEX_BIT, vertexModule, vertexEntry.c_str() },
			VulkanShaderStageModule{ VK_SHADER_STAGE_FRAGMENT_BIT, pixelModule, pixelEntry.c_str() },
		};
		const uint32_t stageCount = hasPixelShader ? 2u : 1u;
		auto pipeline = std::make_unique<VulkanPipelineState>();
		const bool created = pipeline->Create(m_Device, bindingLayout, createInfo.m_Desc,
			std::span<const VulkanShaderStageModule>(stages.data(), stageCount));
		DestroyShaderModule(pixelModule);
		DestroyShaderModule(vertexModule);
		if (!created)
		{
			return {};
		}

		const auto index = static_cast<RHIPipelineHandle::IndexType>(m_Pipelines.size());
		PipelineSlot& slot = m_Pipelines.emplace_back();
		slot.m_Pipeline = std::move(pipeline);
		slot.m_Type = PipelineType::Graphics;
		slot.m_GraphicsDesc = createInfo.m_Desc;
		for (uint32_t attributeIndex = 0;
			attributeIndex < slot.m_GraphicsDesc.m_VertexInput.m_AttributeCount; ++attributeIndex)
		{
			slot.m_SemanticNames[attributeIndex] =
				createInfo.m_Desc.m_VertexInput.m_Attributes[attributeIndex].m_SemanticName
				? createInfo.m_Desc.m_VertexInput.m_Attributes[attributeIndex].m_SemanticName
				: "";
			slot.m_GraphicsDesc.m_VertexInput.m_Attributes[attributeIndex].m_SemanticName = nullptr;
		}
		slot.m_ShaderHashes = shaderHashes;
		return RHIPipelineHandle(index, m_PipelineGeneration);
	}

	RHIPipelineHandle VulkanPipelineSystem::CreateComputePipeline(
		const RHIComputePipelineCreateInfo& createInfo) noexcept
	{
		if (!m_Device->RequireOwnerThread("VulkanPipelineSystem::CreateComputePipeline"))
		{
			return {};
		}
		VulkanBindingLayout* bindingLayout =
			ResolveBindingLayout(createInfo.m_Desc.m_BindingLayout);
		if (bindingLayout == nullptr || !createInfo.m_ComputeShader.IsValid() ||
			createInfo.m_ComputeShader.m_EntryPoint.empty() ||
			!IsVulkanShaderBytecode(createInfo.m_ComputeShader))
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"VulkanPipelineSystem::CreateComputePipeline received invalid inputs.");
			return {};
		}
		std::string entryPoint;
		if (!IsValidShaderRuntimeEntryPoint(createInfo.m_ComputeShader.m_EntryPoint))
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Vulkan pipeline entry points must be non-empty ASCII HLSL identifiers.");
			return {};
		}
		entryPoint = createInfo.m_ComputeShader.m_EntryPoint;

		for (uint32_t index = 0; index < m_Pipelines.size(); ++index)
		{
			const PipelineSlot& slot = m_Pipelines[index];
			if (slot.m_Pipeline && slot.m_Type == PipelineType::Compute &&
				slot.m_ComputeDesc.m_BindingLayout == createInfo.m_Desc.m_BindingLayout &&
				slot.m_ShaderHashes[0] == createInfo.m_ComputeShader.m_Hash)
			{
				return RHIPipelineHandle(index, m_PipelineGeneration);
			}
		}

		VkShaderModule shaderModule =
			CreateShaderModule(createInfo.m_ComputeShader, "Vulkan.ComputePipeline.CS");
		if (shaderModule == VK_NULL_HANDLE)
		{
			return {};
		}
		auto pipeline = std::make_unique<VulkanPipelineState>();
		const bool created = pipeline->CreateCompute(
			m_Device, bindingLayout, shaderModule, entryPoint.c_str());
		DestroyShaderModule(shaderModule);
		if (!created)
		{
			return {};
		}

		const auto index = static_cast<RHIPipelineHandle::IndexType>(m_Pipelines.size());
		PipelineSlot& slot = m_Pipelines.emplace_back();
		slot.m_Pipeline = std::move(pipeline);
		slot.m_Type = PipelineType::Compute;
		slot.m_ComputeDesc = createInfo.m_Desc;
		slot.m_ShaderHashes[0] = createInfo.m_ComputeShader.m_Hash;
		return RHIPipelineHandle(index, m_PipelineGeneration);
	}

	bool VulkanPipelineSystem::IsAlive(RHIBindingLayoutHandle layout) const noexcept
	{
		return ResolveBindingLayout(layout) != nullptr;
	}

	bool VulkanPipelineSystem::IsAlive(RHIPipelineHandle pipeline) const noexcept
	{
		return pipeline.IsValid() && pipeline.Generation() == m_PipelineGeneration &&
			pipeline.Index() < m_Pipelines.size() && m_Pipelines[pipeline.Index()].m_Pipeline &&
			m_Pipelines[pipeline.Index()].m_Pipeline->Get() != VK_NULL_HANDLE;
	}

	void VulkanPipelineSystem::Clear() noexcept
	{
		if (!m_Device->RequireOwnerThread("VulkanPipelineSystem::Clear"))
		{
			return;
		}
		if (!m_Pipelines.empty())
		{
			const VkResult result = vkQueueWaitIdle(m_Device->GetGraphicsQueue());
			if (result != VK_SUCCESS)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"VulkanPipelineSystem::Clear failed to quiesce the graphics queue with {}.",
					ToString(result));
			}
		}
		m_Pipelines.clear();
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

	bool VulkanPipelineSystem::ResolveGraphicsPipeline(RHIPipelineHandle pipeline,
		VulkanPipelineState*& outPipelineState, VulkanBindingLayout*& outBindingLayout,
		RHIGraphicsPipelineDesc& outDesc) const noexcept
	{
		outPipelineState = nullptr;
		outBindingLayout = nullptr;
		if (!IsAlive(pipeline))
		{
			return false;
		}
		const PipelineSlot& slot = m_Pipelines[pipeline.Index()];
		if (slot.m_Type != PipelineType::Graphics)
		{
			return false;
		}
		outPipelineState = slot.m_Pipeline.get();
		outBindingLayout = slot.m_Pipeline->GetBindingLayout();
		outDesc = slot.m_GraphicsDesc;
		return outBindingLayout != nullptr;
	}

	bool VulkanPipelineSystem::ResolveComputePipeline(RHIPipelineHandle pipeline,
		VulkanPipelineState*& outPipelineState,
		VulkanBindingLayout*& outBindingLayout) const noexcept
	{
		outPipelineState = nullptr;
		outBindingLayout = nullptr;
		if (!IsAlive(pipeline))
		{
			return false;
		}
		const PipelineSlot& slot = m_Pipelines[pipeline.Index()];
		if (slot.m_Type != PipelineType::Compute)
		{
			return false;
		}
		outPipelineState = slot.m_Pipeline.get();
		outBindingLayout = slot.m_Pipeline->GetBindingLayout();
		return outBindingLayout != nullptr;
	}
}
