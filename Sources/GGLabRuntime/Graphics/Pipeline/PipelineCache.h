#pragma once
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Pipeline/DepthCoverage.h"
#include "Graphics/Pipeline/PipelinePresets.h"
#include "Graphics/RHI/RHIPipeline.h"
#include "Graphics/RenderPass/RenderPassInfo.h"
#include "Graphics/Shader/ShaderPipelineSnapshot.h"

#include <shared_mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace gglab
{
	class RHIPipelineSystem;
	class ShaderManager;

	struct GraphicsPipelineFormats
	{
		std::array<RHIFormat, RHIGraphicsPipelineDesc::MaxRenderTargets> m_RenderTargetFormats{};
		uint32_t m_RenderTargetCount = 0;
		RHIFormat m_DepthStencilFormat = RHIFormat::Unknown;
		uint32_t m_SampleCount = 1;
		uint32_t m_SampleQuality = 0;

		constexpr bool operator==(const GraphicsPipelineFormats&) const noexcept = default;
	};

	struct GraphicsPhysicalPipelineKey
	{
		RHIBindingLayoutHandle m_BindingLayout{};
		InputLayoutID m_InputLayoutId{};

		ShaderID m_VSId{};
		ShaderID m_PSId{};
		ShaderID m_DSId{};
		ShaderID m_HSId{};
		ShaderID m_GSId{};

		GraphicsPipelineFormats m_Formats{};

		RHIPrimitiveTopologyType m_TopologyType = RHIPrimitiveTopologyType::Triangle;
		RHIPrimitiveTopology m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
		uint32_t m_SampleMask = std::numeric_limits<uint32_t>::max();

		RasterizerPreset m_RasterizerPreset = RasterizerPreset::Default;
		DepthPreset m_DepthPreset = DepthPreset::StandardZWrite;
		BlendPreset m_BlendPreset = BlendPreset::Default;

		int32_t m_DepthBias = 0;
		float m_DepthBiasClamp = 0.0f;
		float m_SlopeScaledDepthBias = 0.0f;

		constexpr bool operator==(const GraphicsPhysicalPipelineKey&) const noexcept = default;
	};

	struct GraphicsLogicalPipelineMetadata
	{
		std::optional<DepthCoveragePipelineSignature> m_DepthCoveragePipelineSignature =
			std::nullopt;

		constexpr bool operator==(const GraphicsLogicalPipelineMetadata&) const noexcept = default;
	};

	struct GraphicsPipelineDescription
	{
		GraphicsPhysicalPipelineKey m_PhysicalKey{};
		GraphicsLogicalPipelineMetadata m_LogicalMetadata{};

		constexpr bool operator==(const GraphicsPipelineDescription&) const noexcept = default;
	};

	struct ComputePipelineRecipe
	{
		RHIBindingLayoutHandle m_BindingLayout{};
		ShaderID m_CSId{};

		constexpr bool operator==(const ComputePipelineRecipe&) const noexcept = default;
	};

	struct GraphicsPipelineSlot
	{
	public:
		void Reset() noexcept { *this = {}; }
		const GraphicsPhysicalPipelineKey& GetPhysicalKey() const noexcept { return m_PhysicalKey; }
		RHIPipelineHandle GetPipeline() const noexcept { return m_Pipeline; }

	private:
		friend class PipelineCache;

		GraphicsPhysicalPipelineKey m_PhysicalKey{};
		ShaderHash128 m_ShaderDependencyFingerprint{};
		RHIPipelineHandle m_Pipeline{};
		uint64_t m_PipelineSystemRevision = 0;
	};

	struct ComputePipelineSlot
	{
	public:
		void Reset() noexcept { *this = {}; }

	private:
		friend class PipelineCache;

		ComputePipelineRecipe m_Recipe{};
		ShaderHash128 m_ShaderDependencyFingerprint{};
		RHIPipelineHandle m_Pipeline{};
		uint64_t m_PipelineSystemRevision = 0;
	};

	class PipelineCache
	{
	public:
		struct CreateInfo
		{
			RHIPipelineSystem* m_PipelineSystem = nullptr;
			ShaderManager* m_ShaderManager = nullptr;
		};

		explicit PipelineCache(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(PipelineCache);
		~PipelineCache() = default;

		RHIPipelineHandle Resolve(GraphicsPipelineSlot& slot,
			const GraphicsPhysicalPipelineKey& physicalKey,
			const RenderPassInfo& renderPassInfo) noexcept;
		RHIPipelineHandle Resolve(ComputePipelineSlot& slot, const ComputePipelineRecipe& recipe,
			const RenderPassInfo& renderPassInfo) noexcept;
		ShaderManager* GetShaderManager() const noexcept { return m_ShaderManager; }
		void GetPipelineUsages(
			RHIPipelineHandle pipeline, std::vector<RenderPassInfo>& outUsages) const noexcept;

	private:
		void RecordPipelineUsage(
			RHIPipelineHandle pipeline, const RenderPassInfo& renderPassInfo) noexcept;

		ShaderManager* m_ShaderManager = nullptr;
		RHIPipelineSystem* m_PipelineSystem = nullptr;
		mutable std::shared_mutex m_UsageMutex;
		std::unordered_map<RHIPipelineHandle, std::vector<RenderPassInfo>> m_PipelineUsages;
		uint64_t m_UsagePipelineSystemRevision = 0;
	};
}
