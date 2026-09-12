#include "Graphics/LegacyRenderHostAccess.h"
#include "Graphics/Renderer.h"

namespace gglab
{
	Renderer* GetLegacyRenderer(RenderHost* host) noexcept
	{
		return static_cast<Renderer*>(host);
	}

	const Renderer* GetLegacyRenderer(const RenderHost* host) noexcept
	{
		return static_cast<const Renderer*>(host);
	}

	Renderer& GetLegacyRenderer(RenderHost& host) noexcept
	{
		return static_cast<Renderer&>(host);
	}
}
