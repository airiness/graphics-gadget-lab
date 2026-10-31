#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIReference.h"
#include "GGLabRuntime/Graphics/Pipeline/GIReferenceTypes.h"
#include "Graphics/Pipeline/TemporalHistoryTextures.h"
#include "Graphics/RenderFrameOutcome.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace gglab
{
	class RHIDevice;

	// Observation identity must not prolong a Lab's asset interests into AssetManager
	// shutdown. Query submission leases own snapshots; this weak control-block key
	// distinguishes recycled addresses even after a scene has expired.
	struct GIReferenceSceneIdentity
	{
		std::weak_ptr<RayScene> m_Owner;
		uintptr_t m_Address = 0;
		GIReferenceSceneIdentity() noexcept = default;
		GIReferenceSceneIdentity(const std::shared_ptr<RayScene>& owner) noexcept :
			m_Owner(owner), m_Address(reinterpret_cast<uintptr_t>(owner.get())) {}
		bool operator==(const GIReferenceSceneIdentity& other) const noexcept
		{
			return m_Address == other.m_Address && !m_Owner.owner_before(other.m_Owner) &&
				!other.m_Owner.owner_before(m_Owner);
		}
	};

	struct GIReferenceObservationKey
	{
		GIReferenceSceneIdentity m_Scene;
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
		static constexpr size_t SurfaceCount = 6;
		using Surface = TemporalHistoryTextures<SurfaceCount>::RenderGraphSurface;
		struct Tile
		{
			uint32_t m_X = 0;
			uint32_t m_Y = 0;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			bool m_BeginSweep = false;
			bool m_EndSweep = false;
		};
		GIReferenceObservation(RHIDevice& device, PersistentTexturePool& pool) noexcept;
		~GIReferenceObservation();
		void BeginFrame(uint64_t serial) noexcept;
		[[nodiscard]] bool Prepare(const GIReferenceObservationKey& key,
			uint32_t samplesPerBatch, uint32_t sampleWorkBudget,
			uint32_t queryWorkBudget = 16'777'216, uint32_t maximumTilePixels = 0) noexcept;
		[[nodiscard]] Tile GetTile() const noexcept { return m_Tile; }
		[[nodiscard]] std::array<Surface, SurfaceCount> Import(RenderGraph::RGBuilder& builder) noexcept;
		void Export(RenderGraph::RGBuilder& builder, const std::array<Surface, SurfaceCount>& surfaces) noexcept;
		void MarkRecorded() noexcept { m_Recorded = true; }
		void Settle(const RenderFrameOutcome& outcome) noexcept;
		void Cancel() noexcept;
		void Shutdown() noexcept;
		[[nodiscard]] GIReferenceObservationDiagnostics GetDiagnostics() const noexcept
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
		GIReferenceObservationKey m_Key{};
		GIReferenceObservationDiagnostics m_Diagnostics{};
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
