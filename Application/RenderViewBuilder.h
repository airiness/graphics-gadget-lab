#pragma once
#include "RenderView.h"

namespace gglab
{
	class Camera;
	class RenderViewBuilder
	{
	public:
		struct BuildInfo
		{
			const Camera& m_Camera;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};

	public:
		RenderView Build(const BuildInfo& info) noexcept;
	};
}