#include "Graphics/RHI/Vulkan/VulkanPipelineState.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanFormat.h"
#include "Graphics/RHI/Vulkan/VulkanConversions.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] const RHIVertexBufferLayoutDesc* FindVertexBinding(
			const RHIVertexInputLayoutDesc& input, uint32_t slot) noexcept
		{
			for (uint32_t index = 0; index < input.m_VertexBufferCount; ++index)
			{
				if (input.m_VertexBuffers[index].m_InputSlot == slot)
				{
					return &input.m_VertexBuffers[index];
				}
			}
			return nullptr;
		}
	}

	VulkanGraphicsPipelinePlan BuildVulkanGraphicsPipelinePlan(
		const RHIGraphicsPipelineDesc& desc) noexcept
	{
		VulkanGraphicsPipelinePlan plan{};
		if (!IsRHIPrimitiveTopologyCompatible(desc.m_TopologyType, desc.m_PrimitiveTopology))
		{
			plan.m_Error = VulkanGraphicsPipelineError::InvalidTopology;
			return plan;
		}
		plan.m_Topology = ToVulkanPrimitiveTopology(desc.m_PrimitiveTopology);
		if (plan.m_Topology == VK_PRIMITIVE_TOPOLOGY_MAX_ENUM)
		{
			plan.m_Error = VulkanGraphicsPipelineError::InvalidTopology;
			return plan;
		}

		if (desc.m_VertexInput.m_VertexBufferCount > plan.m_VertexBindings.size() ||
			desc.m_VertexInput.m_AttributeCount > plan.m_VertexAttributes.size())
		{
			plan.m_Error = VulkanGraphicsPipelineError::InvalidVertexBinding;
			return plan;
		}
		plan.m_VertexBindingCount = desc.m_VertexInput.m_VertexBufferCount;
		for (uint32_t index = 0; index < plan.m_VertexBindingCount; ++index)
		{
			const RHIVertexBufferLayoutDesc& source = desc.m_VertexInput.m_VertexBuffers[index];
			if (source.m_StrideInBytes == 0 ||
				(source.m_InputRate == RHIVertexInputRate::PerVertex &&
					source.m_InstanceStepRate != 0) ||
				(source.m_InputRate == RHIVertexInputRate::PerInstance &&
					source.m_InstanceStepRate != 1))
			{
				plan.m_Error = VulkanGraphicsPipelineError::InvalidVertexBinding;
				return plan;
			}
			for (uint32_t previous = 0; previous < index; ++previous)
			{
				if (desc.m_VertexInput.m_VertexBuffers[previous].m_InputSlot == source.m_InputSlot)
				{
					plan.m_Error = VulkanGraphicsPipelineError::InvalidVertexBinding;
					return plan;
				}
			}
			plan.m_VertexBindings[index] = {
				.binding = source.m_InputSlot,
				.stride = source.m_StrideInBytes,
				.inputRate = source.m_InputRate == RHIVertexInputRate::PerInstance
					? VK_VERTEX_INPUT_RATE_INSTANCE
					: VK_VERTEX_INPUT_RATE_VERTEX,
			};
		}

		plan.m_VertexAttributeCount = desc.m_VertexInput.m_AttributeCount;
		for (uint32_t index = 0; index < plan.m_VertexAttributeCount; ++index)
		{
			const RHIVertexAttributeDesc& source = desc.m_VertexInput.m_Attributes[index];
			const RHIVertexBufferLayoutDesc* binding =
				FindVertexBinding(desc.m_VertexInput, source.m_InputSlot);
			const VkFormat format = ToVulkanFormat(source.m_Format);
			const RHIFormatInfo& formatInfo = GetRHIFormatInfo(source.m_Format);
			if (binding == nullptr || format == VK_FORMAT_UNDEFINED ||
				GetVulkanFormatInfo(source.m_Format).m_IsTypeless ||
				GetVulkanFormatInfo(source.m_Format).m_IsDepthStencil ||
				source.m_AlignedByteOffset > binding->m_StrideInBytes ||
				formatInfo.m_BytesPerBlock > binding->m_StrideInBytes - source.m_AlignedByteOffset)
			{
				plan.m_Error = binding == nullptr
					? VulkanGraphicsPipelineError::InvalidVertexBinding
					: VulkanGraphicsPipelineError::InvalidVertexFormat;
				return plan;
			}
			for (uint32_t previous = 0; previous < index; ++previous)
			{
				if (desc.m_VertexInput.m_Attributes[previous].m_Location == source.m_Location)
				{
					plan.m_Error = VulkanGraphicsPipelineError::InvalidVertexBinding;
					return plan;
				}
			}
			plan.m_VertexAttributes[index] = {
				.location = source.m_Location,
				.binding = source.m_InputSlot,
				.format = format,
				.offset = source.m_AlignedByteOffset,
			};
		}

		if (desc.m_RenderTargetCount > plan.m_ColorFormats.size())
		{
			plan.m_Error = VulkanGraphicsPipelineError::InvalidRenderTargetFormat;
			return plan;
		}
		plan.m_ColorAttachmentCount = desc.m_RenderTargetCount;
		for (uint32_t index = 0; index < plan.m_ColorAttachmentCount; ++index)
		{
			const VulkanFormatInfo& formatInfo =
				GetVulkanFormatInfo(desc.m_RenderTargetFormats[index]);
			if (formatInfo.m_ResourceFormat == VK_FORMAT_UNDEFINED || formatInfo.m_IsTypeless ||
				formatInfo.m_IsDepthStencil)
			{
				plan.m_Error = VulkanGraphicsPipelineError::InvalidRenderTargetFormat;
				return plan;
			}
			plan.m_ColorFormats[index] = formatInfo.m_ResourceFormat;
			const RHIRenderTargetBlendDesc& blend = desc.m_Blend.m_RenderTargets[index];
			plan.m_BlendAttachments[index] = {
				.blendEnable = blend.m_BlendEnable,
				.srcColorBlendFactor = ToVulkanBlendFactor(blend.m_SrcColor),
				.dstColorBlendFactor = ToVulkanBlendFactor(blend.m_DstColor),
				.colorBlendOp = ToVulkanBlendOp(blend.m_ColorOp),
				.srcAlphaBlendFactor = ToVulkanBlendFactor(blend.m_SrcAlpha),
				.dstAlphaBlendFactor = ToVulkanBlendFactor(blend.m_DstAlpha),
				.alphaBlendOp = ToVulkanBlendOp(blend.m_AlphaOp),
				.colorWriteMask = ToVulkanColorWriteMask(blend.m_WriteMask),
			};
		}

		if (desc.m_DepthStencilFormat != RHIFormat::Unknown)
		{
			const VulkanFormatInfo& formatInfo =
				GetVulkanFormatInfo(desc.m_DepthStencilFormat);
			if (!formatInfo.m_IsDepthStencil || formatInfo.m_ResourceFormat == VK_FORMAT_UNDEFINED)
			{
				plan.m_Error = VulkanGraphicsPipelineError::InvalidDepthStencilFormat;
				return plan;
			}
			plan.m_DepthFormat = formatInfo.m_ResourceFormat;
			if ((formatInfo.m_Aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
			{
				plan.m_StencilFormat = formatInfo.m_ResourceFormat;
			}
			else if (desc.m_DepthStencil.m_StencilEnable)
			{
				plan.m_Error = VulkanGraphicsPipelineError::InvalidDepthStencilFormat;
				return plan;
			}
		}
		else if (desc.m_DepthStencil.m_DepthTestEnable || desc.m_DepthStencil.m_StencilEnable)
		{
			plan.m_Error = VulkanGraphicsPipelineError::InvalidDepthStencilFormat;
			return plan;
		}

		if (desc.m_SampleCount == 0 || (desc.m_SampleCount & (desc.m_SampleCount - 1)) != 0 ||
			desc.m_SampleCount > 64)
		{
			plan.m_Error = VulkanGraphicsPipelineError::InvalidSampleCount;
			return plan;
		}
		plan.m_SampleCount = ToVulkanSampleCount(desc.m_SampleCount);
		plan.m_PolygonMode = ToVulkanPolygonMode(desc.m_Rasterizer.m_FillMode);
		plan.m_CullMode = ToVulkanCullMode(desc.m_Rasterizer.m_CullMode);
		plan.m_FrontFace = ToVulkanFrontFace(desc.m_Rasterizer.m_FrontCounterClockwise);
		plan.m_DepthCompareOp = ToVulkanCompareOp(desc.m_DepthStencil.m_DepthCompareOp);
		return plan;
	}

	VulkanPipelineState::~VulkanPipelineState() noexcept
	{
		Release();
	}

	bool VulkanPipelineState::Create(VulkanDevice* device, VulkanBindingLayout* bindingLayout,
		const RHIGraphicsPipelineDesc& desc,
		std::span<const VulkanShaderStageModule> shaders) noexcept
	{
		if (m_Device != nullptr || device == nullptr || bindingLayout == nullptr ||
			!bindingLayout->IsValid() || shaders.empty() ||
			!device->RequireOwnerThread("VulkanPipelineState::Create"))
		{
			return false;
		}
		const VulkanGraphicsPipelinePlan plan = BuildVulkanGraphicsPipelinePlan(desc);
		if (!plan.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR("Vulkan graphics pipeline lowering failed with error {}.",
				static_cast<uint32_t>(plan.m_Error));
			return false;
		}

		std::array<VkPipelineShaderStageCreateInfo, 5> shaderStages{};
		if (shaders.size() > shaderStages.size())
		{
			return false;
		}
		for (uint32_t index = 0; index < shaders.size(); ++index)
		{
			const VulkanShaderStageModule& shader = shaders[index];
			if (shader.m_Module == VK_NULL_HANDLE || shader.m_EntryPoint == nullptr ||
				shader.m_EntryPoint[0] == '\0')
			{
				return false;
			}
			shaderStages[index] = {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = shader.m_Stage,
				.module = shader.m_Module,
				.pName = shader.m_EntryPoint,
			};
		}

		VkPipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInput.vertexBindingDescriptionCount = plan.m_VertexBindingCount;
		vertexInput.pVertexBindingDescriptions = plan.m_VertexBindings.data();
		vertexInput.vertexAttributeDescriptionCount = plan.m_VertexAttributeCount;
		vertexInput.pVertexAttributeDescriptions = plan.m_VertexAttributes.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = plan.m_Topology;

		VkPipelineViewportStateCreateInfo viewport{};
		viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport.viewportCount = 1;
		viewport.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = !desc.m_Rasterizer.m_DepthClipEnable;
		rasterizer.polygonMode = plan.m_PolygonMode;
		rasterizer.cullMode = plan.m_CullMode;
		rasterizer.frontFace = plan.m_FrontFace;
		rasterizer.depthBiasEnable = desc.m_Rasterizer.m_DepthBias != 0 ||
			desc.m_Rasterizer.m_DepthBiasClamp != 0.0f ||
			desc.m_Rasterizer.m_SlopeScaledDepthBias != 0.0f;
		rasterizer.depthBiasConstantFactor = static_cast<float>(desc.m_Rasterizer.m_DepthBias);
		rasterizer.depthBiasClamp = desc.m_Rasterizer.m_DepthBiasClamp;
		rasterizer.depthBiasSlopeFactor = desc.m_Rasterizer.m_SlopeScaledDepthBias;
		rasterizer.lineWidth = 1.0f;

		const std::array<uint32_t, 2> sampleMasks{
			desc.m_SampleMask, std::numeric_limits<uint32_t>::max()
		};
		VkPipelineMultisampleStateCreateInfo multisample{};
		multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample.rasterizationSamples = plan.m_SampleCount;
		multisample.sampleShadingEnable = VK_FALSE;
		multisample.pSampleMask = sampleMasks.data();
		multisample.alphaToCoverageEnable = desc.m_Blend.m_AlphaToCoverageEnable;

		const VkStencilOpState stencilState{
			.failOp = VK_STENCIL_OP_KEEP,
			.passOp = VK_STENCIL_OP_KEEP,
			.depthFailOp = VK_STENCIL_OP_KEEP,
			.compareOp = VK_COMPARE_OP_ALWAYS,
			.compareMask = 0xff,
			.writeMask = 0xff,
		};
		VkPipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = desc.m_DepthStencil.m_DepthTestEnable;
		depthStencil.depthWriteEnable = desc.m_DepthStencil.m_DepthWriteEnable;
		depthStencil.depthCompareOp = plan.m_DepthCompareOp;
		depthStencil.stencilTestEnable = desc.m_DepthStencil.m_StencilEnable;
		depthStencil.front = stencilState;
		depthStencil.back = stencilState;

		VkPipelineColorBlendStateCreateInfo colorBlend{};
		colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlend.attachmentCount = plan.m_ColorAttachmentCount;
		colorBlend.pAttachments = plan.m_BlendAttachments.data();

		const std::array dynamicStates{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		};
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		VkPipelineRenderingCreateInfo rendering{};
		rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		rendering.colorAttachmentCount = plan.m_ColorAttachmentCount;
		rendering.pColorAttachmentFormats = plan.m_ColorFormats.data();
		rendering.depthAttachmentFormat = plan.m_DepthFormat;
		rendering.stencilAttachmentFormat = plan.m_StencilFormat;

		VkGraphicsPipelineCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		createInfo.pNext = &rendering;
		createInfo.stageCount = static_cast<uint32_t>(shaders.size());
		createInfo.pStages = shaderStages.data();
		createInfo.pVertexInputState = &vertexInput;
		createInfo.pInputAssemblyState = &inputAssembly;
		createInfo.pViewportState = &viewport;
		createInfo.pRasterizationState = &rasterizer;
		createInfo.pMultisampleState = &multisample;
		createInfo.pDepthStencilState = &depthStencil;
		createInfo.pColorBlendState = &colorBlend;
		createInfo.pDynamicState = &dynamicState;
		createInfo.layout = bindingLayout->GetPipelineLayout();

		const VkResult result =
			vkCreateGraphicsPipelines(device->Get(), VK_NULL_HANDLE, 1, &createInfo, nullptr,
				&m_Pipeline);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR("vkCreateGraphicsPipelines failed with {}.", ToString(result));
			m_Pipeline = VK_NULL_HANDLE;
			return false;
		}

		m_Device = device;
		m_BindingLayout = bindingLayout;
		m_Desc = desc;
		for (uint32_t index = 0; index < m_Desc.m_VertexInput.m_AttributeCount; ++index)
		{
			m_Desc.m_VertexInput.m_Attributes[index].m_SemanticName = nullptr;
		}
		SetVulkanObjectDebugName(device->Get(), VK_OBJECT_TYPE_PIPELINE,
			reinterpret_cast<uint64_t>(m_Pipeline), "Vulkan.GraphicsPipeline");
		return true;
	}

	bool VulkanPipelineState::CreateCompute(VulkanDevice* device,
		VulkanBindingLayout* bindingLayout, VkShaderModule shaderModule,
		const char* entryPoint) noexcept
	{
		if (m_Device != nullptr || device == nullptr || bindingLayout == nullptr ||
			!bindingLayout->IsValid() || shaderModule == VK_NULL_HANDLE || entryPoint == nullptr ||
			entryPoint[0] == '\0' ||
			!device->RequireOwnerThread("VulkanPipelineState::CreateCompute"))
		{
			return false;
		}

		const VkPipelineShaderStageCreateInfo stage{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = shaderModule,
			.pName = entryPoint,
		};
		const VkComputePipelineCreateInfo createInfo{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = stage,
			.layout = bindingLayout->GetPipelineLayout(),
		};
		const VkResult result = vkCreateComputePipelines(
			device->Get(), VK_NULL_HANDLE, 1, &createInfo, nullptr, &m_Pipeline);
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR("vkCreateComputePipelines failed with {}.", ToString(result));
			m_Pipeline = VK_NULL_HANDLE;
			return false;
		}

		m_Device = device;
		m_BindingLayout = bindingLayout;
		SetVulkanObjectDebugName(device->Get(), VK_OBJECT_TYPE_PIPELINE,
			reinterpret_cast<uint64_t>(m_Pipeline), "Vulkan.ComputePipeline");
		return true;
	}

	void VulkanPipelineState::Release() noexcept
	{
		if (m_Device != nullptr && m_Pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(m_Device->Get(), m_Pipeline, nullptr);
		}
		m_Desc = {};
		m_Pipeline = VK_NULL_HANDLE;
		m_BindingLayout = nullptr;
		m_Device = nullptr;
	}
}
