#pragma once
#include "GGLabTestCore/SelfTest.h"

#include <string_view>

namespace gglab
{
	void RunRenderServicesViewSelfTests(SelfTestContext& context) noexcept;
	int RunRenderServicesViewChild(std::string_view scenario) noexcept;
}
