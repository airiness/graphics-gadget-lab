#include "Application/Content/DesktopApplicationContent.h"
#include "Application/Demo/DemoLabHost.h"
#include "Application/Demo/DemoPlayground.h"
#include "Application/Demo/StartDemo.h"
#include "Application/Lab/Sessions/AlphaTestLabSession.h"
#include "Application/Lab/Sessions/AssetPublicationLabSession.h"
#include "Application/Lab/Sessions/AssetResidencyLabSession.h"
#include "Application/Lab/Sessions/CoordinateConformanceLabSession.h"
#include "Application/Lab/Sessions/CullingLabSession.h"
#include "Application/Lab/Sessions/EnvironmentAssetLabSession.h"
#include "Application/Lab/Sessions/ForwardPlusLabSession.h"
#include "Application/Lab/Sessions/GTAOLabSession.h"
#include "Application/Lab/Sessions/MathFoundationLabSession.h"
#include "Application/Lab/Sessions/MiniPBRGridLabSession.h"
#include "Application/Lab/Sessions/NapaVoxelLabSession.h"
#include "Application/Lab/Sessions/PostProcessLabSession.h"
#include "Application/Lab/Sessions/RenderGraphComputeLabSession.h"
#include "Application/Lab/Sessions/SampleableDepthLabSession.h"
#include "Application/Lab/Sessions/SurfaceProbeLabSession.h"
#include "Application/Lab/Sessions/TaskSystemLabSession.h"
#include "Graphics/Shader/ShaderProgramCatalog.h"

#include <memory>
#include <span>
#include <utility>

namespace gglab
{
	namespace
	{
		std::unique_ptr<DemoBase> CreateStartDemo(const DemoCreateInfo& createInfo,
			const LabId&, std::span<const LabRegistration>) noexcept
		{
			return std::make_unique<StartDemo>(createInfo);
		}

		std::unique_ptr<DemoBase> CreatePlaygroundDemo(const DemoCreateInfo& createInfo,
			const LabId&, std::span<const LabRegistration>) noexcept
		{
			return std::make_unique<DemoPlayground>(createInfo);
		}

		std::unique_ptr<DemoBase> CreateLabHostDemo(const DemoCreateInfo& createInfo,
			const LabId& startupLab,
			std::span<const LabRegistration> labRegistrations) noexcept
		{
			auto labHost =
				std::make_unique<DemoLabHost>(createInfo, startupLab, labRegistrations);
			if (!labHost->IsValid())
			{
				return nullptr;
			}
			return labHost;
		}
	}

	ApplicationContentRegistration CreateDesktopApplicationContent() noexcept
	{
		ApplicationContentRegistration registration{};
		registration.m_Demos = {
			{
				.m_Id = std::string(DesktopStartDemoId),
				.m_Factory = &CreateStartDemo,
			},
			{
				.m_Id = std::string(DesktopPlaygroundDemoId),
				.m_Factory = &CreatePlaygroundDemo,
			},
			{
				.m_Id = std::string(DesktopLabHostDemoId),
				.m_Factory = &CreateLabHostDemo,
				.m_ProvidesLabRuntime = true,
			},
		};
		registration.m_Labs = {
			{ CullingLabSession::GetDescriptor(), &CullingLabSession::Create },
			{ MiniPBRGridLabSession::GetDescriptor(), &MiniPBRGridLabSession::Create },
			{ PostProcessLabSession::GetDescriptor(), &PostProcessLabSession::Create },
			{ RenderGraphComputeLabSession::GetDescriptor(),
				&RenderGraphComputeLabSession::Create,
				{ shader_programs::RenderGraphComputeWrite,
					shader_programs::RenderGraphComputeReadWrite,
					shader_programs::RenderGraphComputePreviewVertex,
					shader_programs::RenderGraphComputePreviewPixel } },
			{ CoordinateConformanceLabSession::GetDescriptor(),
				&CoordinateConformanceLabSession::Create,
				{ shader_programs::CoordinateGeometryVertex,
					shader_programs::CoordinateFullscreenVertex,
					shader_programs::CoordinateMarkerPixel,
					shader_programs::CoordinateConformancePixel } },
			{ SampleableDepthLabSession::GetDescriptor(), &SampleableDepthLabSession::Create },
			{ SurfaceProbeLabSession::GetDescriptor(), &SurfaceProbeLabSession::Create },
			{ GTAOLabSession::GetDescriptor(), &GTAOLabSession::Create },
			{ ForwardPlusLabSession::GetDescriptor(), &ForwardPlusLabSession::Create },
			{ AlphaTestLabSession::GetDescriptor(), &AlphaTestLabSession::Create },
			{ MathFoundationLabSession::GetDescriptor(), &MathFoundationLabSession::Create },
			{ TaskSystemLabSession::GetDescriptor(), &TaskSystemLabSession::Create },
			{ AssetPublicationLabSession::GetDescriptor(), &AssetPublicationLabSession::Create },
			{ AssetResidencyLabSession::GetDescriptor(), &AssetResidencyLabSession::Create },
			{ EnvironmentAssetLabSession::GetDescriptor(), &EnvironmentAssetLabSession::Create },
			{ NapaVoxelLabSession::GetDescriptor(), &NapaVoxelLabSession::Create,
				{ shader_programs::NapaVoxelVertex, shader_programs::NapaVoxelPixel } },
		};
		return registration;
	}
}
