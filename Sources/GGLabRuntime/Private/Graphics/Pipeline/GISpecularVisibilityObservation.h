#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GISpecularVisibility.h"
#include "Graphics/Pipeline/GIReferenceObservation.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gglab
{
	struct GISpecularVisibilityKey
	{
		GIReferenceSceneIdentity m_Scene;
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
		using Surface = TemporalHistoryTextures<SurfaceCount>::RenderGraphSurface;
		using Tile = GIObservationTile;
		GISpecularVisibilityObservation(RHIDevice& device, PersistentTexturePool& pool) noexcept;
		~GISpecularVisibilityObservation();
		void BeginFrame(uint64_t serial) noexcept;
		[[nodiscard]] bool Prepare(const GISpecularVisibilityKey& key,
			uint32_t samplesPerBatch, uint32_t queryWorkBudget,
			uint32_t maximumTilePixels = 0) noexcept;
		[[nodiscard]] Tile GetTile() const noexcept { return m_Tile; }
		[[nodiscard]] std::array<Surface, SurfaceCount> Import(RenderGraph::RGBuilder& builder) noexcept;
		void Export(RenderGraph::RGBuilder& builder,
			const std::array<Surface, SurfaceCount>& surfaces) noexcept;
		void MarkRecorded() noexcept { m_Recorded = true; }
		void Settle(const RenderFrameOutcome& outcome) noexcept;
		void Cancel() noexcept;
		void Shutdown() noexcept;
		[[nodiscard]] GISpecularVisibilityProgress GetDiagnostics() const noexcept
		{
			auto result = m_Diagnostics;
			result.m_Active = m_Visited;
			return result;
		}

	private:
		void Release() noexcept;
		RHIDevice& m_Device;
		PersistentTexturePool& m_Pool;
		TemporalHistoryTextures<SurfaceCount> m_Textures;
		GISpecularVisibilityKey m_Key{};
		GISpecularVisibilityProgress m_Diagnostics{};
		RHIFencePoint m_LastUse{};
		RHIFencePoint m_PendingFence{};
		uint64_t m_FrameSerial = 0;
		bool m_Visited = false;
		bool m_Exported = false;
		bool m_Recorded = false;
		bool m_UnknownCompletion = false;
		bool m_Settled = false;
		bool m_SweepActive = false;
		bool m_PendingInitialized = false;
		uint32_t m_TileWidth = 0;
		uint32_t m_TileHeight = 0;
		Tile m_Tile{};
	};
}
