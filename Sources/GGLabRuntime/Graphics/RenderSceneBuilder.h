#pragma once
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/RenderScene.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "Graphics/Buffer/PersistentStructuredBuffer.h"
#include "Graphics/Buffer/PersistentStructuredBufferTable.h"

#include <cstdint>
#include <optional>
#include <span>

namespace gglab
{
	class EnvironmentLightingSystem;
	class World;
	class AssetManager;
	class SamplerRegistry;
	class TransferManager;
	class RenderResourceRegistry;
	class TemporalFrameTransaction;

	struct RenderSceneGpuAllocations
	{
		DynamicStructuredBufferAllocator<ViewGPU>::Allocation m_Views{};
		DynamicBufferAllocation m_SceneConstants{};

		bool IsEmpty() const noexcept { return !m_Views.IsValid() && !m_SceneConstants.IsValid(); }
	};

	class RenderSceneBuilder
	{
	public:
		struct BuildInfo
		{
			const World& m_World;
			AssetManager& m_AssetManager;
			SamplerRegistry& m_SamplerRegistry;
			TransferManager& m_TransferManager;
			RenderResourceRegistry& m_RenderResourceRegistry;
			EnvironmentLightingSystem& m_EnvironmentLightingSystem;

			std::span<RenderView> m_RenderViews;

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

		struct BuildResult
		{
			RenderScene m_RenderScene{};
			RenderSceneGpuAllocations m_GpuAllocations{};
			RHIFencePoint m_UploadFencePoint{};
			RenderSceneBuildStatus m_Status = RenderSceneBuildStatus::GpuUploadFailed;
		};

	public:
		BuildResult Build(const BuildInfo& info) noexcept;
	};
}
