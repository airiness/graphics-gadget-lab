#pragma once
#include "GGLabTestCore/SelfTest.h"

#include <string_view>

namespace gglab
{
	void RunRGObjectArenaSelfTests(SelfTestContext& context) noexcept;
	// Private death-test infrastructure; always run by the rendering-contracts suite.
	int RunRGObjectArenaChild(std::string_view scenario) noexcept;
}
