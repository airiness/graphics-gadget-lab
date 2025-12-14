#pragma once
#include "RenderScene.h"
#include "StructuredBuffer.h"

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

			StructuredBuffer<ObjectGPU>& m_Objects;
			StructuredBuffer<MaterialGPU>& m_Materials;
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