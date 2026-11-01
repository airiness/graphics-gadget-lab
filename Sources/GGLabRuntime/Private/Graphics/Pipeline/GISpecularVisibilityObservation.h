#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIObservationLimits.h"
#include "GGLabRuntime/Graphics/Pipeline/GISpecularVisibility.h"
#include "Graphics/GIShaderConstants.h"
#include "Graphics/Pipeline/GIObservationSceneIdentity.h"
#include "Graphics/Pipeline/GITiledObservation.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gglab
{
	struct GISpecularVisibilityKey
	{
		GIObservationSceneIdentity m_Scene;
		uint64_t m_SceneRevision = 0;
		RHITextureHandle m_BrdfLut;
		uint64_t m_BrdfLutRevision = 0;
		uint64_t m_ShaderGeneration = 0;
		uint64_t m_PositionShaderGeneration = 0;
		uint64_t m_ReceiverShaderGeneration = 0;
		uint64_t m_CoverageShaderGeneration = 0;
		std::array<float, 16> m_ViewProjection{};
		std::array<float, 3> m_CameraPosition{};
		float m_MaximumDistance = 0;
		float m_TextureLodBias = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Samples = 0;
		uint32_t m_Seed = 0;
		bool operator==(const GISpecularVisibilityKey&) const noexcept = default;
	};

	// Renderer-owned transaction: base/coat numerator, denominator, below-face and
	// invalid sums plus their compensation. Counter RNG has no mutable state.
	// A pending spatial sweep never overwrites the complete estimate.
	class GISpecularVisibilityObservation final
	{
	public:
		enum class SurfaceSlot : size_t
		{
			BaseSum = gi_shader::GIVisibilityBaseSum,
			CoatSum = gi_shader::GIVisibilityCoatSum,
			BaseCompensation = gi_shader::GIVisibilityBaseCompensation,
			CoatCompensation = gi_shader::GIVisibilityCoatCompensation,
			Count = gi_shader::GIVisibilitySurfaceCount,
		};
		static constexpr size_t SurfaceCount = static_cast<size_t>(SurfaceSlot::Count);
		using Surface = GITiledObservation<SurfaceCount>::Surface;
		using Tile = GITiledObservation<SurfaceCount>::Tile;
		GISpecularVisibilityObservation(RHIDevice& device, PersistentTexturePool& pool) noexcept;
		void BeginFrame(uint64_t serial) noexcept { m_Execution.BeginFrame(serial); }
		[[nodiscard]] bool Prepare(const GISpecularVisibilityKey& key,
			uint32_t samplesPerBatch, uint32_t queryWorkBudget,
			uint32_t maximumTilePixels = 0,
			uint32_t receiverQueryBudget = GIObservationLimits::MaximumQueryWorkPerSubmission) noexcept;
		[[nodiscard]] Tile GetTile() const noexcept { return m_Execution.GetTile(); }
		[[nodiscard]] std::array<Surface, SurfaceCount> Import(RenderGraph::RGBuilder& builder) noexcept;
		void Export(RenderGraph::RGBuilder& builder,
			const std::array<Surface, SurfaceCount>& surfaces) noexcept
		{
			m_Execution.Export(builder, surfaces);
		}
		[[nodiscard]] GIReceiverCache& GetReceiverCache() noexcept { return m_Execution.GetReceiverCache(); }
		void MarkRecorded() noexcept { m_Execution.MarkRecorded(); }
		void Settle(const RenderFrameOutcome& outcome) noexcept;
		void Cancel() noexcept;
		void Shutdown() noexcept;
		[[nodiscard]] GISpecularVisibilityProgress GetDiagnostics() const noexcept
		{
			return m_Execution.ProjectProgress<GISpecularVisibilityProgress>();
		}

	private:
		GITiledObservation<SurfaceCount> m_Execution;
		GISpecularVisibilityKey m_Key{};
	};
}
