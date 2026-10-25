#include "NapaVoxelSelfTests.h"
#include "NapaVoxelSelfTestCases.h"

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
