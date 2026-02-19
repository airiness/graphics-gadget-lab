#pragma once

namespace gglab
{
	enum class RenderResourceTag : uint8_t
	{
		IBL
	};

	/*
	* Management runtime generated GPU Textures ro Buffers
	*/
	class RenderResourceRegistry
	{
	public:
		RenderResourceRegistry() noexcept;
		
	};
}