#pragma once
#include "Graphics/RHI/RHIPipelineSystem.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gglab
{
	class VulkanDevice;
	class VulkanPipelineState;

	enum class VulkanBindingLayoutError : uint8_t
	{
		None,
		TooManySlots,
		UnknownBindingType,
		UnsupportedRegisterSpace,
		InvalidDescriptorCount,
		InvalidPushConstantSize,
		DuplicateNativeBinding,
	};

	struct VulkanSet0BindingPlan
	{
		uint32_t m_LogicalParameterIndex = 0;
		uint32_t m_Binding = 0;
		VkDescriptorType m_DescriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
		uint32_t m_DescriptorCount = 0;
		VkShaderStageFlags m_StageFlags = 0;
		uint32_t m_SizeInBytes = 0;
		uint32_t m_DynamicOffsetSlot = UINT32_MAX;
	};

	struct VulkanBindingLayoutPlan
	{
		std::array<VulkanSet0BindingPlan, RHIBindingLayoutDesc::MaxSlots> m_Set0Bindings{};
		std::array<uint32_t, RHIBindingLayoutDesc::MaxSlots> m_ParameterToDynamicOffsetSlot{};
		uint32_t m_Set0BindingCount = 0;
		uint32_t m_DynamicOffsetCount = 0;
		VulkanBindingLayoutError m_Error = VulkanBindingLayoutError::None;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Error == VulkanBindingLayoutError::None;
		}
		[[nodiscard]] uint32_t GetDynamicOffsetSlot(uint32_t logicalParameterIndex) const noexcept
		{
			return logicalParameterIndex < m_ParameterToDynamicOffsetSlot.size()
				? m_ParameterToDynamicOffsetSlot[logicalParameterIndex]
				: UINT32_MAX;
		}
	};

	[[nodiscard]] VulkanBindingLayoutPlan BuildVulkanBindingLayoutPlan(
		const RHIBindingLayoutDesc& desc, uint32_t maxUniformBufferRange) noexcept;

	class VulkanBindingLayout
	{
	public:
		VulkanBindingLayout() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanBindingLayout);
		~VulkanBindingLayout() noexcept;

		[[nodiscard]] bool Create(VulkanDevice* device, const RHIBindingLayoutDesc& desc,
			VkDescriptorSetLayout globalSetLayout) noexcept;
		void Release() noexcept;

		[[nodiscard]] VkDescriptorSetLayout GetSet0Layout() const noexcept { return m_Set0Layout; }
		[[nodiscard]] VkPipelineLayout GetPipelineLayout() const noexcept { return m_PipelineLayout; }
		[[nodiscard]] const VulkanBindingLayoutPlan& GetPlan() const noexcept { return m_Plan; }
		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Set0Layout != VK_NULL_HANDLE && m_PipelineLayout != VK_NULL_HANDLE;
		}

	private:
		VulkanDevice* m_Device = nullptr;
		VkDescriptorSetLayout m_Set0Layout = VK_NULL_HANDLE;
		VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
		VulkanBindingLayoutPlan m_Plan{};
	};

	class VulkanPipelineSystem final : public RHIPipelineSystem
	{
	public:
		explicit VulkanPipelineSystem(VulkanDevice* device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanPipelineSystem);
		~VulkanPipelineSystem() override;

		RHIBindingLayoutHandle CreateBindingLayout(
			const RHIBindingLayoutDesc& desc) noexcept override;
		RHIPipelineHandle CreateGraphicsPipeline(
			const RHIGraphicsPipelineCreateInfo& createInfo) noexcept override;
		RHIPipelineHandle CreateComputePipeline(
			const RHIComputePipelineCreateInfo& createInfo) noexcept override;
		bool IsAlive(RHIBindingLayoutHandle layout) const noexcept override;
		bool IsAlive(RHIPipelineHandle pipeline) const noexcept override;
		uint64_t GetRevision() const noexcept override { return m_Revision; }
		void Clear() noexcept override;

		[[nodiscard]] VkShaderModule CreateShaderModule(
			const ShaderBytecode& bytecode, const char* debugName = nullptr) noexcept;
		void DestroyShaderModule(VkShaderModule shaderModule) noexcept;
		[[nodiscard]] VulkanBindingLayout* ResolveBindingLayout(
			RHIBindingLayoutHandle layout) noexcept;
		[[nodiscard]] const VulkanBindingLayout* ResolveBindingLayout(
			RHIBindingLayoutHandle layout) const noexcept;
		[[nodiscard]] bool ResolveGraphicsPipeline(RHIPipelineHandle pipeline,
			VulkanPipelineState*& outPipelineState, VulkanBindingLayout*& outBindingLayout,
			RHIGraphicsPipelineDesc& outDesc) const noexcept;

	private:
		struct BindingLayoutSlot
		{
			std::unique_ptr<VulkanBindingLayout> m_Layout;
			std::string m_DebugName;
		};

		struct PipelineSlot
		{
			std::unique_ptr<VulkanPipelineState> m_Pipeline;
			RHIGraphicsPipelineDesc m_Desc{};
			std::array<ShaderHash128, 5> m_ShaderHashes{};
		};

		VulkanDevice* m_Device = nullptr;
		std::vector<BindingLayoutSlot> m_BindingLayouts;
		std::vector<PipelineSlot> m_Pipelines;
		RHIBindingLayoutHandle::GenerationType m_BindingLayoutGeneration = 1;
		RHIPipelineHandle::GenerationType m_PipelineGeneration = 1;
		uint64_t m_Revision = 1;
	};
}
