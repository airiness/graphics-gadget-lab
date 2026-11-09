#include "RenderGraphDiagnosticsSelfTests.h"
#include "DevTools/EnumText/EnumTextRenderGraph.h"

#include <array>

namespace gglab
{
	void RunRenderGraphDiagnosticsSelfTests(SelfTestContext& context) noexcept
	{
		struct AccessCase
		{
			RGAccelerationStructureAccess m_Access;
			const char* m_Text;
		};
		constexpr std::array cases
		{
			AccessCase{ RGAccelerationStructureAccess::BuildRead, "Build Read" },
			AccessCase{ RGAccelerationStructureAccess::BuildWrite, "Build Write" },
			AccessCase{ RGAccelerationStructureAccess::QueryRead, "Query Read" },
		};
		for (const auto& testCase : cases)
		{
			context.Check(devtools::RenderGraphAccessText(static_cast<uint64_t>(testCase.m_Access),
				RGResourceType::RGAccelerationStructure) == testCase.m_Text,
				"Inspector AS accesses use their typed diagnostic labels instead of buffer aliases");
		}
		context.Check(devtools::RenderGraphAccessText(1, RGResourceType::RGTexture) == "Sample" &&
			devtools::RenderGraphAccessText(1, RGResourceType::RGBuffer) == "Vertex" &&
			devtools::RenderGraphAccessText(1, RGResourceType::RGAccelerationStructure) == "Build Write",
			"Equal access values retain different meanings for texture, buffer and AS resources");
		context.Check(devtools::RenderGraphAccessText(255, RGResourceType::RGAccelerationStructure) == "Unknown" &&
			devtools::RenderGraphAccessText(1, static_cast<RGResourceType>(255)) == "Unknown",
			"Unknown resource and AS access kinds do not fall through to buffer semantics");
	}
}
