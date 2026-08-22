#include "Application/SelfTest/ApplicationContentRegistrationSelfTests.h"
#include "Application/Content/DesktopApplicationContent.h"
#include "GGLabTestCore/SelfTest.h"

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
	}
}
