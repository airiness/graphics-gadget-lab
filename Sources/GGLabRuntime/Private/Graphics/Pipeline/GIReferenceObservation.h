#pragma once
#include "Graphics/Pipeline/GIObservationSceneIdentity.h"
#include "Graphics/Pipeline/GITiledObservation.h"
#include "GGLabRuntime/Graphics/Pipeline/GIObservationLimits.h"
#include "GGLabRuntime/Graphics/Pipeline/GIReference.h"
#include "GGLabRuntime/Graphics/Pipeline/GIReferenceTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gglab
{
	struct GIReferenceObservationKey
	{
		GIObservationSceneIdentity m_Scene;
		uint64_t m_SceneRevision = 0;
		uint64_t m_LightingGeneration = 0;
		uint64_t m_EnvironmentGeneration = 0;
		uint64_t m_ShaderGeneration = 0;
		uint64_t m_ReceiverShaderGeneration = 0;
		uint64_t m_CoverageShaderGeneration = 0;
		uint64_t m_PositionShaderGeneration = 0;
		std::array<float, 16> m_ViewProjection{};
		std::array<float, 3> m_CameraPosition{};
		std::array<float, 3> m_ConstantRadiance{};
		float m_MaximumDistance = 0;
		float m_TextureLodBias = 0;
		float m_EnvironmentIntensity = 0;
		float m_EnvironmentRotation = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Samples = 0;
		uint32_t m_Seed = 0;
		uint32_t m_MaximumDepth = 1;
		GIReferenceEnvironmentFilter m_Filter = GIReferenceEnvironmentFilter::Face32;
		bool m_ConstantEnvironment = true;

		bool operator==(const GIReferenceObservationKey&) const noexcept = default;
	};

	// Renderer-owned, independent of temporal history and of the Lab's lifetime.
	// Pending tiles advance on Submitted; all six surfaces publish only when a whole
	// sweep is submitted. Only a Completed fence permits scheduling another tile.
	class GIReferenceObservation final
	{
	public:
		enum class SurfaceSlot : size_t
		{
			SunSum,
			EnvironmentSum,
			OutcomeSum,
			RandomState,
			SunCompensation,
			EnvironmentCompensation,
			Count,
		};
		static constexpr size_t SurfaceCount = static_cast<size_t>(SurfaceSlot::Count);
		using Surface = GITiledObservation<SurfaceCount>::Surface;
		using Tile = GITiledObservation<SurfaceCount>::Tile;
		GIReferenceObservation(RHIDevice& device, PersistentTexturePool& pool) noexcept;
		void BeginFrame(uint64_t serial) noexcept { m_Execution.BeginFrame(serial); }
		[[nodiscard]] bool Prepare(const GIReferenceObservationKey& key,
			uint32_t samplesPerBatch, uint32_t sampleWorkBudget,
			uint32_t queryWorkBudget = GIObservationLimits::MaximumQueryWorkPerSubmission,
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
		[[nodiscard]] GIReferenceObservationDiagnostics GetDiagnostics() const noexcept
		{
			auto result = m_Execution.ProjectProgress<GIReferenceObservationDiagnostics>();
			result.m_MaximumDepth = m_MaximumDepth;
			return result;
		}

	private:
		GITiledObservation<SurfaceCount> m_Execution;
		GIReferenceObservationKey m_Key{};
		uint32_t m_MaximumDepth = 1;
	};
}
