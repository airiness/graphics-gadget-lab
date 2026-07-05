#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/HelloLabSession.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

namespace gglab
{
	HelloLabSession::HelloLabSession(const LabSessionCreateInfo& createInfo) noexcept :
		LabSession(
			GetDescriptor(),
			createInfo,
			std::make_unique<RenderPipelineForwardPBR>())
	{}

	void HelloLabSession::Update() noexcept
	{
		UpdateCamera();
	}

	LabId HelloLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.hello");
	}

	LabDescriptor HelloLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Hello Lab",
			.m_Category = "Foundation",
			.m_Description = "An empty scene used to validate the Lab runtime lifecycle.",
			.m_Kind = LabKind::Scene,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSession> HelloLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<HelloLabSession>(createInfo);
	}
}
