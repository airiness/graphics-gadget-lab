#include "Application/SelfTest/NapaVoxelCoreSelfTests.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

namespace gglab
{
	void RunNapaVoxelCoreSelfTests(SelfTestContext& context) noexcept
	{
		RunNapaVoxelCommandSelfTests(context);
		RunNapaVoxelGGLabAdapterSelfTests(context);
		RunNapaVoxelRaycastEditSelfTests(context);
		RunNapaVoxelPublicationSelfTests(context);
	}
}
