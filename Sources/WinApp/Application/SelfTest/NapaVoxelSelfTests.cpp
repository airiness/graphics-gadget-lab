#include "Application/SelfTest/NapaVoxelSelfTests.h"
#include "Application/SelfTest/NapaVoxelSelfTestCases.h"

namespace gglab
{
	void RunNapaVoxelSelfTests(SelfTestContext& context) noexcept
	{
		RunNapaVoxelCommandSelfTests(context);
		RunNapaVoxelGGLabAdapterSelfTests(context);
		RunNapaVoxelRaycastEditSelfTests(context);
		RunNapaVoxelPublicationSelfTests(context);
	}
}
