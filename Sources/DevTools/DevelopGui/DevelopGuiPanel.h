#pragma once

#include <cstdint>
#include <string_view>

namespace gglab
{
	struct DevelopGuiContext;

	class DevelopGuiPanelBase
	{
	public:
		virtual ~DevelopGuiPanelBase() = default;

		virtual std::string_view GetPath() const noexcept = 0;
		virtual std::string_view GetTitle() const noexcept = 0;
		virtual void Draw(DevelopGuiContext& context) noexcept = 0;

		virtual int32_t GetOrder() const noexcept { return 0; }
		virtual bool IsDefaultOpen() const noexcept { return false; }
	};
}
