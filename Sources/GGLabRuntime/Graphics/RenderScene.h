#pragma once
#include "Core/Math/BoundingVolumes.h"
#include "Core/Math/Vector.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/RenderView.h"
#include "Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "Graphics/Buffer/PersistentStructuredBuffer.h"
#include "Graphics/Buffer/PersistentStructuredBufferTable.h"
#include "Graphics/RHI/RHIFence.h"

#include <array>

namespace gglab
{
	class EnvironmentLightingSystem;
	class World;
	class AssetManager;
	class SamplerRegistry;
	class TransferManager;
	class RenderResourceRegistry;
	class TemporalFrameTransaction;

	struct RenderInstance
	{
		MeshID m_MeshId{};
		RenderMaterialKey m_MaterialKey{};
		MaterialFlags m_MaterialFlags = MaterialFlags::None;
		AlphaMode m_AlphaMode = AlphaMode::Opaque;

		uint32_t m_ObjectOffset = 0;
		uint32_t m_MaterialOffset = 0;

		Vector3 m_WorldCenterPos = Vector3::Zero;
		math::Sphere m_WorldBounds{};
		bool m_HasWorldBounds = false;
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
		uint32_t m_DirectionalShadowLightIndex = std::numeric_limits<uint32_t>::max();
		uint32_t m_DirectionalLightCount = 0;
		uint32_t m_LocalLightCount = 0;
		std::array<uint32_t, MaxLightCapacity> m_LightTypesByIndex{};
		std::vector<uint32_t> m_GlobalLightIndices;

		uint64_t m_SceneConstantBufferOffset = 0;

		std::vector<RenderInstance> m_RenderInstances;
	};

	struct RenderSceneGpuAllocations
	{
		DynamicStructuredBufferAllocator<ViewGPU>::Allocation m_Views{};
		DynamicBufferAllocation m_SceneConstants{};

		bool IsEmpty() const noexcept { return !m_Views.IsValid() && !m_SceneConstants.IsValid(); }
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
