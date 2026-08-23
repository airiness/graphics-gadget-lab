#include "Application/SelfTest/ApplicationContentRegistrationSelfTests.h"
#include "Application/Content/DesktopApplicationContent.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/Shader/ShaderProgramCatalog.h"

namespace gglab
{
	void RunApplicationContentRegistrationSelfTests(SelfTestContext& context) noexcept
	{
		const ApplicationContentRegistration desktop = CreateDesktopApplicationContent();
		const ApplicationContentSelection desktopSelection = ResolveApplicationContentSelection(
			desktop, DesktopLabHostDemoId, DesktopDefaultLabId);
		context.Check(desktop.IsValid() && desktop.m_Demos.size() == 3 &&
			desktop.m_Labs.size() == 16 && desktopSelection.Succeeded(),
			"Windows desktop composition reproduces the complete three-Demo, sixteen-Lab catalog");

		const auto checkSelectedDemand = [&context, &desktop](
			std::string_view labId, size_t expectedCount, std::string_view message) noexcept
			{
				const ApplicationContentSelection selection = ResolveApplicationContentSelection(
					desktop, DesktopLabHostDemoId, labId);
				ShaderProgramDemandSet demands;
				const bool succeeded = selection.Succeeded() &&
					demands.AddRange(shader_programs::GetRendererInitialShaderProgramDemand()) &&
					AppendSelectedContentShaderProgramDemand(selection, demands);
				context.Check(succeeded && demands.GetPrograms().size() == expectedCount, message);
			};
		checkSelectedDemand("gglab.lab.render_graph_compute", 34,
			"Render-graph compute selection contributes four stable shader demands");
		checkSelectedDemand("gglab.lab.coordinate_conformance", 34,
			"Coordinate conformance selection contributes four stable shader demands");
		checkSelectedDemand("gglab.lab.napa_voxel", 32,
			"Napa voxel selection contributes two stable shader demands");
	}
}
