#include "ApplicationFrameworkSelfTests.h"

#include "ApplicationContentRegistration.h"
#include "Lab/LabSessionBase.h"
#include "GGLabTestCore/SelfTest.h"

namespace gglab
{
	namespace
	{
		std::unique_ptr<DemoBase> CreateNullDemo(const DemoCreateInfo&, const LabId&,
			std::span<const LabRegistration>) noexcept
		{
			return nullptr;
		}

		std::unique_ptr<LabSessionBase> CreateNullLab(const LabSessionCreateInfo&) noexcept
		{
			return nullptr;
		}

		LabRegistration MakeLabRegistration(std::string_view id) noexcept
		{
			return {
				.m_Descriptor = {
					.m_Id = LabId(id),
					.m_DisplayName = std::string(id),
					.m_Category = "SelfTest",
					.m_Description = "Registration policy fixture",
				},
				.m_Factory = &CreateNullLab,
			};
		}

		ApplicationDemoRegistration MakeDemoRegistration(
			std::string_view id, bool providesLabRuntime = false) noexcept
		{
			return {
				.m_Id = std::string(id),
				.m_Factory = &CreateNullDemo,
				.m_ProvidesLabRuntime = providesLabRuntime,
			};
		}
	}

	void RunApplicationFrameworkSelfTests(SelfTestContext& context) noexcept
	{
		ApplicationContentRegistration empty;
		context.Check(!empty.IsValid(),
			"A host cannot start with an empty content registration set");

		ApplicationContentRegistration minimal;
		minimal.m_Demos.push_back(MakeDemoRegistration("test.demo.minimal"));
		const ApplicationContentSelection minimalSelection =
			ResolveApplicationContentSelection(minimal, "test.demo.minimal", std::nullopt);
		context.Check(minimal.IsValid() && minimal.m_Labs.empty() &&
			minimalSelection.Succeeded(),
			"A minimal host can register one supported Demo and omit optional Lab content");
		ShaderProgramDemandSet minimalDemands;
		context.Check(AppendSelectedContentShaderProgramDemand(
			minimalSelection, minimalDemands) && minimalDemands.GetPrograms().empty(),
			"Content without shader demand contributes an empty stable snapshot");

		ApplicationContentRegistration invalidShaderDemand = minimal;
		invalidShaderDemand.m_Demos.front().m_ShaderPrograms.push_back({});
		context.Check(!invalidShaderDemand.IsValid(),
			"Content registration rejects invalid shader program identities");

		context.Check(ResolveApplicationContentSelection(
			minimal, "test.demo.unknown", std::nullopt).m_Status ==
			ApplicationContentSelectionStatus::StartupDemoUnavailable,
			"An unavailable startup Demo ID is rejected before runtime startup");

		ApplicationContentRegistration duplicateDemo = minimal;
		duplicateDemo.m_Demos.push_back(MakeDemoRegistration("test.demo.minimal"));
		context.Check(!duplicateDemo.IsValid(),
			"Duplicate Demo registration IDs are rejected");

		ApplicationContentRegistration duplicateLab = minimal;
		duplicateLab.m_Labs.push_back(MakeLabRegistration("test.lab.duplicate"));
		duplicateLab.m_Labs.push_back(MakeLabRegistration("test.lab.duplicate"));
		context.Check(!duplicateLab.IsValid(),
			"Duplicate Lab registration IDs are rejected by the shared Lab catalog contract");

		ApplicationContentRegistration labSubset;
		labSubset.m_Demos.push_back(MakeDemoRegistration("test.demo.lab", true));
		labSubset.m_Labs.push_back(MakeLabRegistration("test.lab.supported"));
		context.Check(ResolveApplicationContentSelection(
			labSubset, "test.demo.lab", "test.lab.supported").Succeeded(),
			"A host-selected Lab subset resolves its supported startup Lab");
		labSubset.m_Labs.front().m_ShaderPrograms.push_back({
			.m_ProgramId = "gglab.shader.test",
			.m_VariantId = "compute",
			});
		const ApplicationContentSelection shaderSelection = ResolveApplicationContentSelection(
			labSubset, "test.demo.lab", "test.lab.supported");
		ShaderProgramDemandSet selectedDemands;
		context.Check(shaderSelection.Succeeded() &&
			AppendSelectedContentShaderProgramDemand(shaderSelection, selectedDemands) &&
			selectedDemands.GetPrograms().size() == 1,
			"Only the selected content contributes its stable shader program demand");
		context.Check(ResolveApplicationContentSelection(
			labSubset, "test.demo.lab", "test.lab.unavailable").m_Status ==
			ApplicationContentSelectionStatus::StartupLabUnavailable,
			"An unavailable startup Lab ID is rejected before Demo transition");
		context.Check(ResolveApplicationContentSelection(
			labSubset, "test.demo.lab", std::nullopt).m_Status ==
			ApplicationContentSelectionStatus::StartupLabUnavailable,
			"A Lab-hosting Demo requires an explicit host-selected startup Lab");
	}
}
