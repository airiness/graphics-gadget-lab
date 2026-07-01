#pragma once
#include "Graphics/GPUStructures.h"
#include "Graphics/RenderView.h"
#include "Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "Graphics/Buffer/PersistentStructuredBuffer.h"
#include "Graphics/Buffer/PersistentStructuredBufferTable.h"
#include "Graphics/RHI/RHIFence.h"

namespace gglab
{
	class World;
	class AssetManager;
	class TransferManager;
	class RenderResourceRegistry;

	struct RenderInstance
	{
		MeshID m_MeshId{};
		MaterialID m_MaterialId{};

		uint32_t m_ObjectOffset = 0;

		Vector3 m_WorldCenterPos = Vector3::Zero;
	};

	struct RenderScene
	{
		uint32_t m_ObjectBaseIndex = 0;
		uint32_t m_ObjectCount = 0;

		uint32_t m_MaterialBaseIndex = 0;
		uint32_t m_MaterialCount = 0;

		uint32_t m_ViewBaseIndex = 0;
		uint32_t m_ViewCount = 0;

		uint32_t m_LightBaseIndex = 0;
		uint32_t m_LightCount = 0;

		uint64_t m_SceneConstantBufferOffset = 0;

		std::vector<RenderInstance> m_RenderInstances;
	};

	struct RenderSceneGpuAllocations
	{
		DynamicStructuredBufferAllocator<ViewGPU>::Allocation m_Views{};
		DynamicBufferAllocation m_SceneConstants{};

		bool IsEmpty() const noexcept
		{
			return !m_Views.IsValid() &&
				!m_SceneConstants.IsValid();
		}
	};

	enum class RenderSceneBuildStatus : uint8_t
	{
		Ready,
		GpuUploadFailed,
	};

	class RenderSceneBuilder
	{
	public:
		struct BuildInfo
		{
			const World& m_World;
			AssetManager& m_AssetManager;
			TransferManager& m_TransferManager;
			RenderResourceRegistry& m_RenderResourceRegistry;

			std::span<RenderView> m_RenderViews;

			DynamicConstantBufferAllocator& m_SceneCB;
			PersistentStructuredBuffer<ObjectGPU>& m_ObjectsSB;
			PersistentStructuredBuffer<MaterialGPU>& m_MaterialsSB;
			PersistentStructuredBuffer<LightGPU>& m_LightsSB;
			PersistentStructuredBufferTable<uint64_t, ObjectGPU>& m_ObjectTable;
			PersistentStructuredBufferTable<MaterialID, MaterialGPU>& m_MaterialTable;
			PersistentStructuredBufferTable<uint64_t, LightGPU>& m_LightTable;
			DynamicStructuredBufferAllocator<ViewGPU>& m_ViewsSB;
			uint32_t m_CurrentBackBufferIndex = 0;
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
