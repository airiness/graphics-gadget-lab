#pragma once
#include "NapaVoxelTestFramework.h"

namespace napa::voxel::testing
{
	void RunNapaVoxelCoordinateSelfTests(TestContext& context) noexcept;
	void RunNapaVoxelDamageSelfTests(TestContext& context) noexcept;
	void RunNapaVoxelEditSelfTests(TestContext& context) noexcept;
	void RunNapaVoxelHashSelfTests(TestContext& context) noexcept;
	void RunNapaVoxelMesherSelfTests(TestContext& context) noexcept;
	void RunNapaVoxelMultiChunkSelfTests(TestContext& context) noexcept;
	void RunNapaVoxelMutationSelfTests(TestContext& context) noexcept;
	void RunNapaVoxelPrimitiveSelfTests(TestContext& context) noexcept;
	void RunNapaVoxelRestoreSelfTests(TestContext& context) noexcept;
	void RunNapaVoxelStorageSelfTests(TestContext& context) noexcept;
}
