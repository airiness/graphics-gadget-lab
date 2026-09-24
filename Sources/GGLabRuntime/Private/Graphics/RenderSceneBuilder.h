#pragma once
#include "GGLabRuntime/Graphics/DirectionalShadowFramePlan.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/RenderScene.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "Graphics/Buffer/PersistentStructuredBufferTable.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace gglab
{
	class EnvironmentLightingSystem;
	class World;
	class AssetManager;
	class RenderSamplerAccess;
	class TransferManager;
	class RenderResourceRegistry;
	class TemporalFrameTransaction;

	struct RenderSceneGpuAllocations
	{
		DynamicStructuredBufferAllocator<ViewGPU>::Allocation m_Views{};
		DynamicBufferAllocation m_SceneConstants{};
		DynamicBufferAllocation m_ShadowConstants{};

		bool IsEmpty() const noexcept
		{
			return !m_Views.IsValid() && !m_SceneConstants.IsValid() && !m_ShadowConstants.IsValid();
		}
	};

	class RenderSceneBuilder
	{
	public:
		struct BuildInfo
		{
			const World& m_World;
			AssetManager& m_AssetManager;
			RenderSamplerAccess& m_SamplerRegistry;
			TransferManager& m_TransferManager;
			RenderResourceRegistry& m_RenderResourceRegistry;
			EnvironmentLightingSystem& m_EnvironmentLightingSystem;

			std::span<RenderView> m_RenderViews;
			const DirectionalShadowFramePlan& m_DirectionalShadowFramePlan;

			DynamicConstantBufferAllocator& m_SceneCB;
			PersistentStructuredBuffer<ObjectGPU>& m_ObjectsSB;
			PersistentStructuredBuffer<MaterialGPU>& m_MaterialsSB;
			PersistentStructuredBuffer<LightGPU>& m_LightsSB;
			PersistentStructuredBufferTable<uint64_t, ObjectGPU>& m_ObjectTable;
			PersistentStructuredBufferTable<RenderMaterialKey, MaterialGPU>& m_MaterialTable;
			PersistentStructuredBufferTable<uint64_t, LightGPU>& m_LightTable;
			TemporalFrameTransaction* m_TemporalFrameTransaction = nullptr;
			std::optional<uint64_t> m_DirectionalShadowLightKey;
			DynamicStructuredBufferAllocator<ViewGPU>& m_ViewsSB;
			uint32_t m_FrameSlotIndex = 0;
		};

		struct ViewUploadData
		{
			std::vector<ViewGPU> m_Views;
			uint32_t m_ShadowViewBaseOffset = DirectionalShadowFramePlan::UnassignedViewBaseOffset;
		};

		struct BuildResult
		{
			uint32_t m_ShadowViewBaseOffset = DirectionalShadowFramePlan::UnassignedViewBaseOffset;
			RenderScene m_RenderScene{};
			RenderSceneGpuAllocations m_GpuAllocations{};
			RHIFencePoint m_UploadFencePoint{};
			RenderSceneBuildStatus m_Status = RenderSceneBuildStatus::GpuUploadFailed;
		};

	public:
		BuildResult Build(const BuildInfo& info) noexcept;

		[[nodiscard]] static ViewUploadData BuildViewData(
			std::span<const RenderView> cameraViews, const DirectionalShadowFramePlan& cascades) noexcept;
	};
}
