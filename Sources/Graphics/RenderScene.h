#pragma once
#include "Graphics/GPUStructures.h"
#include "Graphics/RenderView.h"
#include "Graphics/DX12/DX12RingStructuredBuffer.h"

namespace gglab
{
	class World;
	class AssetManager;
	class TransferManager;
	class RenderResourceRegistry;

	template<typename T>
	class DX12ConstantBuffer;

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

		LightGPU m_MainLight{};

		std::vector<RenderInstance> m_RenderInstances;
	};

	struct RenderSceneGpuAllocations
	{
		DX12RingStructuredBuffer<ObjectGPU>::AllocateResult m_Objects{};
		DX12RingStructuredBuffer<MaterialGPU>::AllocateResult m_Materials{};
		DX12RingStructuredBuffer<ViewGPU>::AllocateResult m_Views{};

		bool IsEmpty() const noexcept
		{
			return !m_Objects.IsValid() &&
				!m_Materials.IsValid() &&
				!m_Views.IsValid();
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

			DX12ConstantBuffer<SceneGPU>& m_SceneCB;
			DX12RingStructuredBuffer<ObjectGPU>& m_ObjectsSB;
			DX12RingStructuredBuffer<MaterialGPU>& m_MaterialsSB;
			DX12RingStructuredBuffer<ViewGPU>& m_ViewsSB;
			uint32_t m_CurrentBackBufferIndex = 0;
		};

		struct BuildResult
		{
			RenderScene m_RenderScene{};
			RenderSceneGpuAllocations m_GpuAllocations{};
			DX12FencePoint m_UploadFencePoint{};
			RenderSceneBuildStatus m_Status = RenderSceneBuildStatus::GpuUploadFailed;
		};

	public:
		BuildResult Build(const BuildInfo& info) noexcept;
	};
}
