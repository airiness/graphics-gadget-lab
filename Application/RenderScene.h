#pragma once
#include "GPUStructures.h"
#include "RenderView.h"
#include "DX12RingStructuredBuffer.h"

namespace gglab
{
	class World;
	class AssetManager;
	class TransferManager;

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

	class RenderSceneBuilder
	{
	public:
		struct BuildInfo
		{
			const World& m_World;
			AssetManager& m_AssetManager;
			TransferManager& m_TransferManager;

			std::span<RenderView> m_RenderViews;

			DX12RingStructuredBuffer<ObjectGPU>& m_ObjectsSB;
			DX12RingStructuredBuffer<MaterialGPU>& m_MaterialsSB;
			DX12RingStructuredBuffer<ViewGPU>& m_ViewsSB;
		};

		struct BuildResult
		{
			RenderScene m_RenderScene{};
			DX12FencePoint m_UploadFencePoint{};
		};

	public:
		BuildResult Build(const BuildInfo& info) noexcept;
	};
}