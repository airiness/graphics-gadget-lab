#include "GGLabRuntime/Graphics/CameraRenderViewQueryBase.h"
#include "GGLabRuntime/Graphics/EnvironmentAssetController.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingSettings.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/EnvironmentSelectionControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentTextureSource.h"
#include "GGLabRuntime/Graphics/IBLBakeConfig.h"
#include "GGLabRuntime/Graphics/IBLPreviewControlBase.h"
#include "GGLabRuntime/Graphics/IBLPreviewDiagnostics.h"
#include "GGLabRuntime/Graphics/IBLPreviewTypes.h"
#include "GGLabRuntime/Graphics/IBLPreviewViewBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewControlBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewDiagnostics.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewViewBase.h"
#include "GGLabRuntime/Graphics/RHI/DX12/DX12ContextFactory.h"
#include "GGLabRuntime/Graphics/RHI/DX12/DX12ResourceLifecycleTools.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#if GGLAB_ENABLE_VULKAN
#include "GGLabRuntime/Graphics/RHI/Vulkan/VulkanWin32ContextFactory.h"
#endif
#include "GGLabRuntime/Graphics/RenderCompositionAccess.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "GGLabRuntime/Graphics/ShadowPreviewViewBase.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RHI/DX12/DX12CommandContext.h"
#include "Graphics/RHI/DX12/DX12RootSignature.h"
#include "Graphics/RHI/DX12/Utility/DX12BindingLayoutUtils.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/VulkanCommandContext.h"
#endif
#include "Graphics/Renderer.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "RGObjectArenaSelfTests.h"
#include "RenderServicesViewSelfTests.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"

#include <Windows.h>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace gglab::rendering_test
{
	template <typename T>
	concept PublicInlineConstantWords = requires(T& value, std::span<const uint32_t> words)
	{
		value.SetInlineConstantWords(0, words, 0);
	};

	static_assert(!PublicInlineConstantWords<RHIGraphicsCommandContext>);

	static_assert(!PublicInlineConstantWords<RHIComputeCommandContext>);

	static_assert(!PublicInlineConstantWords<DX12GraphicsCommandContext>);

	static_assert(!PublicInlineConstantWords<DX12ComputeCommandContext>);

#if GGLAB_ENABLE_VULKAN
	static_assert(!PublicInlineConstantWords<VulkanGraphicsCommandContext>);
	static_assert(!PublicInlineConstantWords<VulkanComputeCommandContext>);
#endif

	static_assert(std::is_same_v<decltype(std::declval<const DX12RootSignature&>().GetParameterPlan()),
		const DX12RootParameterPlan&>);

	template <typename T>
	concept CameraRoutingMutation = requires(T& value) { value.SetDisplayViewId(RenderViewID::Main); };

	template <typename T>
	concept CameraRoutingSlotAccess = requires(const T& value) { value.FindRenderViewSlot(RenderViewID::Main); };

	static_assert(!CameraRoutingMutation<CameraRenderViewQueryBase>);

	static_assert(!CameraRoutingSlotAccess<CameraRenderViewQueryBase>);

	template <typename T>
	concept ResourceLifecycleQuery = requires(const T& value)
	{
		{ value.GetSnapshot() } -> std::same_as<DX12ResourceLifecycleSnapshot>;
	};

	template <typename T>
	concept ResourceLifecycleMutation = requires(T& value) { value.AddTexture(); value.DestroyAll(); };

	template <typename T>
	concept ResourceLifecycleNativeAccess = requires(T& value) { value.GetDX12Device(); };

	static_assert(ResourceLifecycleQuery<DX12ResourceLifecycleViewBase>);

	static_assert(!ResourceLifecycleMutation<DX12ResourceLifecycleViewBase>);

	static_assert(ResourceLifecycleMutation<DX12ResourceLifecycleControlBase>);

	static_assert(!ResourceLifecycleQuery<DX12ResourceLifecycleControlBase>);

	static_assert(!ResourceLifecycleNativeAccess<DX12ResourceLifecycleToolsBase>);

	template <typename T>
	concept EnvironmentCatalogAccess = requires(const T& value) { value.GetEntries(); };

	template <typename T>
	concept EnvironmentLifecycleControl = requires(T& value) { value.Tick(); value.Reset(); };

	template <typename T>
	concept EnvironmentFileSelection = requires(T& value, const std::filesystem::path& path)
	{
		value.SelectEnvironmentFile(path);
	};

	static_assert(std::derived_from<EnvironmentAssetController, EnvironmentSelectionControlBase>);

	static_assert(!std::is_abstract_v<EnvironmentAssetController>);

	static_assert(requires(EnvironmentSelectionControlBase& control)
	{
		{ control.SelectEnvironment(0) } noexcept -> std::same_as<bool>;
	});

	static_assert(!EnvironmentCatalogAccess<EnvironmentSelectionControlBase>);

	static_assert(!EnvironmentLifecycleControl<EnvironmentSelectionControlBase>);

	static_assert(!EnvironmentFileSelection<EnvironmentSelectionControlBase>);

	template <typename T>
	concept PostProcessPreviewQuery = requires(const T& value)
	{
		{ value.GetPostProcessPreviewDiagnostics() } ->
			std::same_as<PostProcessPreviewDiagnostics>;
	};

	template <typename T>
	concept PostProcessPreviewRequest = requires(T& value)
	{
		value.SetPostProcessPreviewSelection(PostProcessDebugSelection{});
		value.SetPostProcessPreviewExposureEV(0.0f);
		value.RequestPostProcessPreview();
	};

	template <typename T>
	concept PostProcessPreviewSettlement = requires(T& value)
	{
		value.PublishPostProcessPreview({}, PostProcessPreviewChannel::PostProcessing, 1, {});
	};

	template <typename T>
	concept IBLPreviewQuery = requires(const T& value)
	{
		{ value.GetIBLPreviewResourcesDiagnostics() } ->
			std::same_as<IBLPreviewResourcesDiagnostics>;
	};

	template <typename T>
	concept IBLPreviewRequest = requires(T& value)
	{
		value.RequestIBLPreview(IBLPreviewType::Environment);
		value.SetIBLEnvironmentPreviewMip(1);
	};

	template <typename T>
	concept IBLPreviewSettlement = requires(T& value)
	{
		value.PublishIBLPreview(IBLPreviewType::Environment, {}, IBLPreviewLayout::Cross, 0);
	};

	template <typename T>
	concept IBLPreviewAllocation = requires(T& value)
	{
		value.EnsureIblResources();
	};

	static_assert(IBLPreviewQuery<IBLPreviewViewBase>);

	static_assert(!IBLPreviewRequest<IBLPreviewViewBase>);

	static_assert(IBLPreviewRequest<IBLPreviewControlBase>);

	static_assert(!IBLPreviewQuery<IBLPreviewControlBase>);

	static_assert(!IBLPreviewSettlement<IBLPreviewViewBase>);

	static_assert(!IBLPreviewSettlement<IBLPreviewControlBase>);

	static_assert(!IBLPreviewAllocation<IBLPreviewViewBase>);

	static_assert(!IBLPreviewAllocation<IBLPreviewControlBase>);

	static_assert(PostProcessPreviewQuery<PostProcessPreviewViewBase>);

	static_assert(!PostProcessPreviewRequest<PostProcessPreviewViewBase>);

	static_assert(PostProcessPreviewRequest<PostProcessPreviewControlBase>);

	static_assert(!PostProcessPreviewQuery<PostProcessPreviewControlBase>);

	static_assert(!PostProcessPreviewSettlement<PostProcessPreviewViewBase>);

	static_assert(!PostProcessPreviewSettlement<PostProcessPreviewControlBase>);

	template <typename T>
	concept ShadowPreviewAllocation = requires(T& value)
	{
		value.EnsureShadowPreviewResources();
	};

	static_assert(requires(const ShadowPreviewViewBase& view)
	{
		{ view.GetShadowPreviewDiagnostics() } noexcept -> std::same_as<ShadowPreviewDiagnostics>;
	});

	static_assert(!ShadowPreviewAllocation<ShadowPreviewViewBase>);

	static_assert(ShadowPreviewAllocation<RenderResourceRegistry>);

	template <typename T>
	concept EnvironmentSettingsQuery = requires(const T& value)
	{
		{ value.GetEnvironmentLightingSettings() } noexcept ->
			std::same_as<EnvironmentLightingSettings>;
	};

	template <typename T>
	concept EnvironmentSettingsControl = requires(T& value)
	{
		value.SetIntensity(1.0f);
		value.SetRotationRadians(0.0f);
		value.SetQualityPreset(IBLQualityPreset::Medium);
		value.SetPrefilteredSpecularSampleCount(512);
		value.SetPrefilteredSpecularMaxSampleLuminance(1000.0f);
		value.SetSkyboxEnabled(true);
		value.SetBackgroundMode(EnvironmentBackgroundMode::TextureEnvironment);
		value.RequestRebake();
	};

	template <typename T>
	concept EnvironmentSourceCommit = requires(T& value)
	{
		value.CommitEnvironmentSource(EnvironmentTextureSource{});
	};

	static_assert(EnvironmentSettingsQuery<EnvironmentLightingViewBase>);

	static_assert(!EnvironmentSettingsControl<EnvironmentLightingViewBase>);

	static_assert(EnvironmentSettingsControl<EnvironmentLightingControlBase>);

	static_assert(!EnvironmentSettingsQuery<EnvironmentLightingControlBase>);

	static_assert(!EnvironmentSourceCommit<EnvironmentLightingViewBase>);

	static_assert(!EnvironmentSourceCommit<EnvironmentLightingControlBase>);

	void RunRuntimePathConfigurationContractTests(SelfTestContext& context) noexcept
	{
		class RecordingContextFactory final : public RHIContextFactoryBase
		{
		public:
			std::unique_ptr<RHIContext> CreateContext(const RHIContextDesc& desc) const noexcept override
			{
				m_CreateCalled = true;
				m_Desc = desc;
				return {};
			}

			mutable bool m_CreateCalled = false;
			mutable RHIContextDesc m_Desc{};
		};

		static_assert(std::is_abstract_v<RHIContextFactoryBase>);
		static_assert(std::has_virtual_destructor_v<RHIContextFactoryBase>);
		static_assert(std::is_same_v<decltype(Renderer::CreateInfo::m_RHIContextFactory), const RHIContextFactoryBase*>);

		Renderer::CreateInfo createInfo{};
		context.Check(!createInfo.HasRequiredRuntimePaths(),
			"Renderer configuration rejects missing host-supplied runtime paths");
		createInfo.m_IblDerivedDataCacheDirectory = "DerivedDataCache/IBL";
		context.Check(createInfo.HasRequiredRuntimePaths(),
			"Renderer configuration accepts the required host-supplied IBL cache root");

		Renderer missingFactoryRenderer;
		context.Check(!missingFactoryRenderer.Initialize(createInfo),
			"Renderer rejects a missing host-supplied RHI context factory");

		RecordingContextFactory factory;
		createInfo.m_RHIContextFactory = &factory;
		createInfo.m_Width = 1280;
		createInfo.m_Height = 720;
		Renderer renderer;
		context.Check(!renderer.Initialize(createInfo) && factory.m_CreateCalled &&
			factory.m_Desc.m_Width == 1280 && factory.m_Desc.m_Height == 720,
			"Renderer borrows the host factory and forwards only neutral context settings");
	}

	void RunRenderHostContractTests(SelfTestContext& context) noexcept
	{
		static_assert(std::is_abstract_v<RenderHost>,
			"the Public render host contract stays an interface");
		static_assert(std::is_base_of_v<RenderHost, Renderer>,
			"the concrete renderer implements the Public render host contract");
		static_assert(!std::is_copy_constructible_v<RenderFrame> &&
			std::is_move_constructible_v<RenderFrame>,
			"the Public frame handle is a move-only RAII owner");
		static_assert(std::is_base_of_v<RenderPipelineResolver, PipelineCache>,
			"the pipeline cache implements the explicit pipeline resolver contract");
		static_assert(std::is_base_of_v<RenderShaderProgramAccess, ShaderManager>,
			"the shader manager implements the explicit program access contract");
		static_assert(std::is_base_of_v<RenderSamplerAccess, SamplerRegistry>,
			"the sampler registry implements the explicit sampler access contract");
		static_assert(
			std::is_base_of_v<RenderResourceRegistryAccess, RenderResourceRegistry>,
			"the render resource registry implements the explicit resource contract");
		static_assert(std::is_base_of_v<RenderFrameBufferAccess, Renderer> &&
			std::is_base_of_v<RenderEnvironmentAccess, Renderer> &&
				std::is_base_of_v<RenderPresentationAccess, Renderer>,
			"the renderer implements the explicit frame, environment and presentation contracts");

		const RenderFrame unavailable(RHIFrameBeginStatus::Unavailable);
		const RenderFrame fatal(RHIFrameBeginStatus::Fatal);
		context.Check(!unavailable.IsValid() && !unavailable.IsReady() &&
			unavailable.GetBeginStatus() == RHIFrameBeginStatus::Unavailable &&
			unavailable.IsUnavailable() && !fatal.IsValid() && fatal.IsFatal(),
			"non-ready begin results carry no active render frame");

		RenderFrame source(RHIFrameBeginStatus::Unavailable);
		RenderFrame moved(std::move(source));
		context.Check(!source.IsValid() && !source.IsReady() && moved.IsUnavailable(),
			"moving a frame handle transfers the abort obligation");

		RenderFrame assigned(RHIFrameBeginStatus::Fatal);
		assigned = std::move(moved);
		context.Check(!moved.IsValid() && !assigned.IsReady() && assigned.IsUnavailable(),
			"assigning a frame handle transfers the abort obligation");

		const RenderHostInstance missingFactory = CreateRenderHost({
			.m_Diagnostics = DiagnosticsSessionCreateInfo{} });
		context.Check(!missingFactory.m_Host &&
			!missingFactory.m_Diagnostics &&
			!missingFactory.m_CompositionBindings.m_PipelineResolver &&
			!missingFactory.m_CompositionBindings.m_Presentation,
			"render host factory rejects missing context factories before composing diagnostics");
		RenderHostCreateInfo missingPaths{};
		missingPaths.m_RHIContextFactory = reinterpret_cast<const RHIContextFactoryBase*>(1);
		const RenderHostInstance missingPathsInstance = CreateRenderHost(missingPaths);
		context.Check(!missingPathsInstance.m_Host &&
			!missingPathsInstance.m_CompositionBindings.m_PipelineResolver,
			"render host factory rejects missing runtime paths");

		RenderHostCreateInfo missingShaderManager{};
		missingShaderManager.m_RHIContextFactory = reinterpret_cast<const RHIContextFactoryBase*>(1);
		missingShaderManager.m_IblDerivedDataCacheDirectory = "cache";
		const RenderHostInstance missingShaderManagerInstance = CreateRenderHost(missingShaderManager);
		context.Check(!missingShaderManagerInstance.m_Host &&
			!missingShaderManagerInstance.m_CompositionBindings.m_PipelineResolver,
			"render host factory rejects a missing host shader manager");

		RenderHostInstance defaultHostInstance{};
		context.Check(!defaultHostInstance.m_Host && !defaultHostInstance.m_Composition,
			"render host instances default to no host and no composition access");
		RenderHostInstance hostInstance{};
		hostInstance.m_Composition = reinterpret_cast<RenderCompositionAccess*>(1);
		context.Check(hostInstance.m_Composition == reinterpret_cast<RenderCompositionAccess*>(1),
			"render host instances carry the composition access alongside the host and services");
	}

	void RunNativeContextCompositionTests(SelfTestContext& context) noexcept
	{
		RHIContextDesc nativeContextDesc{ .m_Width = 64, .m_Height = 64 };
		context.Check(!CreateDX12Context(nativeContextDesc, nullptr),
			"DX12 composition rejects a missing window before creating backend objects");
#if GGLAB_ENABLE_VULKAN
		context.Check(!CreateVulkanWin32Context(nativeContextDesc, nullptr, nullptr, true),
			"Vulkan Win32 composition rejects missing native handles before bootstrap");
		// Sentinel handles must never reach WSI when host ABI suitability is false.
		context.Check(!CreateVulkanWin32Context(nativeContextDesc, reinterpret_cast<HINSTANCE>(1), reinterpret_cast<HWND>(1), false),
			"Vulkan Win32 composition preserves the host ABI rejection before loader or WSI access");
#endif
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingServicesSelfTests(SelfTestContext& context) noexcept
	{
		RunRenderServicesViewSelfTests(context);
		RunRGObjectArenaSelfTests(context);
		RunNativeContextCompositionTests(context);
		RunRuntimePathConfigurationContractTests(context);
		RunRenderHostContractTests(context);
	}
}
