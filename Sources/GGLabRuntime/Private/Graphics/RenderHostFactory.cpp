#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadScheduling.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/Renderer.h"
#include "Graphics/AtmosphereSystem.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"

namespace gglab
{
	RenderHostInstance CreateRenderHost(const RenderHostCreateInfo& createInfo) noexcept
	{
		RenderHostInstance instance{};
		if (createInfo.m_RHIContextFactory == nullptr ||
			createInfo.m_ShaderManager == nullptr ||
			!createInfo.HasRequiredRuntimePaths())
		{
			return instance;
		}

		auto renderer = std::make_unique<Renderer>();
		const Renderer::CreateInfo rendererCreateInfo{
			.m_RHIContextFactory = createInfo.m_RHIContextFactory,
			.m_ShaderManager = createInfo.m_ShaderManager,
			.m_TaskSystem = createInfo.m_TaskSystem,
			.m_IblDerivedDataCacheDirectory = createInfo.m_IblDerivedDataCacheDirectory,
			.m_Width = createInfo.m_Width,
			.m_Height = createInfo.m_Height,
			.m_AdapterSelector = createInfo.m_AdapterSelector,
			.m_EnableDebugValidation = createInfo.m_EnableDebugValidation,
			.m_EnableGpuBasedValidation = createInfo.m_EnableGpuBasedValidation,
		};
		if (!renderer->Initialize(rendererCreateInfo))
		{
			return instance;
		}

		instance.m_CompositionBindings.m_PipelineResolver = renderer->GetPipelineCache();
		instance.m_CompositionBindings.m_ShaderPrograms = createInfo.m_ShaderManager;
		instance.m_CompositionBindings.m_Samplers = renderer->GetSamplerRegistry();
		instance.m_CompositionBindings.m_Resources = renderer->GetRenderResourceRegistry();
		instance.m_CompositionBindings.m_FrameBuffers = renderer.get();
		instance.m_CompositionBindings.m_Environment = renderer.get();
		instance.m_CompositionBindings.m_Presentation = renderer.get();
		instance.m_CompositionBindings.m_BindingLayout = renderer.get();
		instance.m_CompositionBindings.m_Temporal = renderer.get();
		instance.m_CompositionBindings.m_Atmosphere = renderer->GetAtmosphereSystem();
		instance.m_CompositionBindings.m_BakeAtmosphere = renderer->GetBakeAtmosphereSystem();
		instance.m_CompositionBindings.m_AssetUpload = renderer->GetAssetUploadScheduler();
		instance.m_CompositionBindings.m_FrameCapture = renderer->GetFrameCaptureAccess();
		instance.m_CompositionBindings.m_GIReferenceObservation = renderer->GetGIReferenceObservation();
		instance.m_Composition = renderer.get();
		instance.m_ServiceView = renderer->PublishRenderServices(instance.m_CompositionBindings);
		instance.m_Host = std::move(renderer);
		return instance;
	}
}
