#pragma once
#include "Application/SelfTest/SelfTest.h"

namespace gglab
{
	void RunNapaVoxelCoordinateSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelEditSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelGGLabAdapterSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelHashSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelMesherSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelMultiChunkSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelMutationSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelPrimitiveSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelPublicationSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelRestoreSelfTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelStorageSelfTests(SelfTestContext& context) noexcept;
}
