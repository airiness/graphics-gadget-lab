#pragma once
#include "Application/SelfTest/SelfTest.h"

namespace gglab
{
	void RunNapaVoxelCoordinateSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelHashSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelStorageSelfTests(SelfTestContext& context) noexcept;
}
