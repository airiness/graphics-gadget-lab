#pragma once
#include "Graphics/GraphicsTypes.h"
#include "Graphics/PipelinePresets.h"
#include "Graphics/RHI/RHIPipeline.h"

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

	struct GraphicsPipelineRecipe
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
		DepthPreset m_DepthPreset = DepthPreset::Default;
		BlendPreset m_BlendPreset = BlendPreset::Default;

		int32_t m_DepthBias = 0;
		float m_DepthBiasClamp = 0.0f;
		float m_SlopeScaledDepthBias = 0.0f;

		constexpr bool operator==(const GraphicsPipelineRecipe&) const noexcept = default;
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

	private:
		friend class PipelineCache;

		GraphicsPipelineRecipe m_Recipe{};
		uint64_t m_ShaderRevision = 0;
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
		uint64_t m_ShaderRevision = 0;
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

		RHIPipelineHandle Resolve(
			GraphicsPipelineSlot& slot,
			const GraphicsPipelineRecipe& recipe) noexcept;
		RHIPipelineHandle Resolve(
			ComputePipelineSlot& slot,
			const ComputePipelineRecipe& recipe) noexcept;

	private:
		ShaderManager* m_ShaderManager = nullptr;
		RHIPipelineSystem* m_PipelineSystem = nullptr;
	};
}
