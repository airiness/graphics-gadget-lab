#pragma once
#include "RenderScene.h"
#include "DX12RingStructuredBuffer.h"

namespace gglab
{
	struct RenderView;
	class World;
	class AssetManager;
	class TransferManager;
	class RenderSceneBuilder
	{
	public:
		struct BuildInfo
		{
			const World& m_World;
			const RenderView& m_RenderView;

			AssetManager& m_AssetManager;
			TransferManager& m_TransferManager;

			DX12RingStructuredBuffer<ObjectGPU>& m_ObjectsSB;
			DX12RingStructuredBuffer<MaterialGPU>& m_MaterialsSB;
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