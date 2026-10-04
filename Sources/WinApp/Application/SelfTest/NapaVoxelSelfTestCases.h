#pragma once
#include "GGLabTestCore/SelfTest.h"

namespace gglab
{
	void RunNapaVoxelCommandSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelGGLabAdapterSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelPublicationSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelRaycastEditSelfTests(SelfTestContext& context) noexcept;
}
