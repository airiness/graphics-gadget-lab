#pragma once
#include "GraphicsTypes.h"
#include "RenderScene.h"

namespace gglab
{
	struct DrawItem
	{
		MeshID m_MeshId{};
		MaterialID m_MaterialId{};
		uint32_t m_ObjectOffset = 0;

		RenderBucket m_Bucket = RenderBucket::Opaque;
		uint64_t m_SortKey = 0;
	};

	struct DrawRange
	{
		uint32_t m_Start = 0;
		uint32_t m_Count = 0;
	};

	struct RenderQueue
	{
		std::vector<DrawItem> m_DrawItems;
	};

	class RenderQueueBuilder
	{
	public:
		struct BuildInfo
		{
			const RenderScene& m_RenderScene;
		};

		RenderQueue Build(const BuildInfo& info) noexcept;
	};
}