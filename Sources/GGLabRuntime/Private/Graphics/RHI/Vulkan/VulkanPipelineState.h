#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <span>

namespace gglab
{
	class VulkanBindingLayout;
	class VulkanDevice;

	enum class VulkanGraphicsPipelineError : uint8_t
	{
		None,
		InvalidTopology,
		UnsupportedShaderStage,
		InvalidVertexBinding,
		InvalidVertexFormat,
		InvalidRenderTargetFormat,
		InvalidDepthStencilFormat,
		InvalidSampleCount,
	};

	struct VulkanGraphicsPipelinePlan
	{
		std::array<VkVertexInputBindingDescription,
			RHIVertexInputLayoutDesc::MaxVertexBuffers> m_VertexBindings{};
		std::array<VkVertexInputAttributeDescription,
			RHIVertexInputLayoutDesc::MaxAttributes> m_VertexAttributes{};
		std::array<VkPipelineColorBlendAttachmentState,
			RHIGraphicsPipelineDesc::MaxRenderTargets> m_BlendAttachments{};
		std::array<VkFormat, RHIGraphicsPipelineDesc::MaxRenderTargets> m_ColorFormats{};
		uint32_t m_VertexBindingCount = 0;
		uint32_t m_VertexAttributeCount = 0;
		uint32_t m_ColorAttachmentCount = 0;
		VkPrimitiveTopology m_Topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
		VkPolygonMode m_PolygonMode = VK_POLYGON_MODE_FILL;
		VkCullModeFlags m_CullMode = VK_CULL_MODE_NONE;
		VkFrontFace m_FrontFace = VK_FRONT_FACE_CLOCKWISE;
		VkSampleCountFlagBits m_SampleCount = VK_SAMPLE_COUNT_1_BIT;
		VkCompareOp m_DepthCompareOp = VK_COMPARE_OP_ALWAYS;
		VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;
		VkFormat m_StencilFormat = VK_FORMAT_UNDEFINED;
		VulkanGraphicsPipelineError m_Error = VulkanGraphicsPipelineError::None;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Error == VulkanGraphicsPipelineError::None;
		}
	};

	[[nodiscard]] VulkanGraphicsPipelinePlan BuildVulkanGraphicsPipelinePlan(
		const RHIGraphicsPipelineDesc& desc) noexcept;

	struct VulkanShaderStageModule
	{
		VkShaderStageFlagBits m_Stage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		VkShaderModule m_Module = VK_NULL_HANDLE;
		const char* m_EntryPoint = nullptr;
	};

	class VulkanPipelineState
	{
	public:
		VulkanPipelineState() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanPipelineState);
		~VulkanPipelineState() noexcept;

		[[nodiscard]] bool Create(VulkanDevice* device, VulkanBindingLayout* bindingLayout,
			const RHIGraphicsPipelineDesc& desc,
			std::span<const VulkanShaderStageModule> shaders) noexcept;
		[[nodiscard]] bool CreateCompute(VulkanDevice* device,
			VulkanBindingLayout* bindingLayout, VkShaderModule shaderModule,
			const char* entryPoint) noexcept;
		void Release() noexcept;

		[[nodiscard]] VkPipeline Get() const noexcept { return m_Pipeline; }
		[[nodiscard]] VulkanBindingLayout* GetBindingLayout() const noexcept
		{
			return m_BindingLayout;
		}
		[[nodiscard]] const RHIGraphicsPipelineDesc& GetDesc() const noexcept { return m_Desc; }

	private:
		VulkanDevice* m_Device = nullptr;
		VulkanBindingLayout* m_BindingLayout = nullptr;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;
		RHIGraphicsPipelineDesc m_Desc{};
	};
}
