#pragma once

namespace gglab
{
	enum class ViewType : uint8_t
	{
		RTV,
		DSV,
		// TODO: UAV, SRV, CBV
	};


	

	class ViewCache
	{
	public:
		explicit ViewCache() noexcept;
	};
}