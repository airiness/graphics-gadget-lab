#pragma once
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/DepthCoverage.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelinePresets.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"
#include "GGLabRuntime/Graphics/Shader/ShaderPipelineSnapshot.h"
#include "GGLabRuntime/Graphics/Shader/ShaderTypes.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace gglab
{
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

	// Opaque cache slots filled by the pipeline resolver. The internal
	// dependency and pipeline state stay owned by the Runtime resolver.
	struct GraphicsPipelineSlot
	{
	public:
		void Reset() noexcept { *this = {}; }
		const GraphicsPhysicalPipelineKey& GetPhysicalKey() const noexcept { return m_PhysicalKey; }
		RHIPipelineHandle GetPipeline() const noexcept { return m_Pipeline; }

	private:
		friend class PipelineCache;

		GraphicsPhysicalPipelineKey m_PhysicalKey{};
		std::shared_ptr<const std::array<ShaderPipelineDependencyIdentity, 5>>
			m_ShaderDependencies;
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
		ShaderPipelineDependencyIdentity m_ShaderDependency{};
		RHIPipelineHandle m_Pipeline{};
		uint64_t m_PipelineSystemRevision = 0;
	};
}
