#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabRuntime/Core/Math/Color.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Core/StringId.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Graphics/Asset/AssetCacheStatistics.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingSettings.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/EnvironmentTextureSource.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Graphics/IBLBakeConfig.h"
#include "GGLabRuntime/Graphics/IBLBakeTypes.h"
#include "GGLabRuntime/Graphics/IBLPreviewTypes.h"
#include "GGLabRuntime/Graphics/LightingMutationResult.h"
#include "GGLabRuntime/Graphics/LightingProfile.h"
#include "GGLabRuntime/Graphics/MaterialTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHISwapChain.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResourceUtils.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraphTypes.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassInfo.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/Resource/RenderTextureIndex.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/SamplerTypes.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/ViewRenderSettingsPatch.h"
#include "GGLabRuntime/Graphics/WorldLighting.h"
#include "GGLabRuntime/Graphics/WorldSun.h"
#include "GGLabRuntime/Scene/Components.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/AtmosphereSystem.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/RHI/RHIUploadPool.h"
#include "Graphics/RenderPass/AerialPerspectiveGraphResources.h"
#include "Graphics/RenderPass/AtmosphereGraphResources.h"
#include "Graphics/RenderPass/RenderPassAerialPerspective.h"
#include "Graphics/RenderPass/RenderPassAtmosphere.h"
#include "Graphics/RenderPass/RenderPassIBLEnvironment.h"
#include "Graphics/RenderPass/RenderPassIBLEnvironmentMipChain.h"
#include "Graphics/RenderPass/RenderPassIBLPrefilteredSpecular.h"
#include "Graphics/RenderPass/RenderPassPostProcessPreview.h"
#include "Graphics/RenderSceneBuilder.h"
#include "Graphics/RenderWorldExtractor.h"
#include "Graphics/Resource/PersistentTexturePool.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace gglab::rendering_test
{
	void RunEnvironmentLightingOverrideTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		device.m_CreateValidDescriptors = true;
		TransientResourcePool pool(&device);
		SamplerRegistry samplers({ .m_Device = &device });
		RenderResourceRegistry registry({ .m_Device = &device,
			.m_TransientResourcePool = &pool, .m_SamplerRegistry = &samplers });
		EnvironmentLightingSystem environment({ .m_RenderResourceRegistry = &registry });
		environment.SetPrefilteredSpecularMaxSampleLuminance(123.0f);
		const auto baseline = environment.GetSettings();
		const auto generation = environment.GetBakeRequestGeneration();
		auto scope = environment.AcquireOverride();
		context.Check(scope && !environment.AcquireOverride() && !scope->OwnsBakeConfiguration() &&
			scope->GetInheritedSettings().m_BakeConfig == baseline.m_BakeConfig,
			"An exclusive lighting scope snapshots the entered baseline without claiming inherited fields");
		scope.reset();
		context.Check(environment.GetBakeRequestGeneration() == generation,
			"Releasing an unused lighting scope does not request a bake");

		scope = environment.AcquireOverride();
		scope->Apply({ .m_Intensity = 2.0f, .m_RotationRadians = 0.5f,
			.m_EnableSkybox = false, .m_BackgroundMode = EnvironmentBackgroundMode::PhysicalSky,
			.m_PrefilteredSpecularSampleCount = 64 });
		context.Check(scope->OwnsBakeConfiguration() && environment.GetSettings().m_Intensity == 2.0f &&
			environment.GetSettings().m_BakeConfig.m_PrefilteredSpecularSampleCount == 64 &&
			environment.GetBakeRequestGeneration() == generation + 1,
			"Lighting overrides claim only the typed fields they write and request one actual bake");
		environment.SetIntensity(3.0f);
		environment.SetBackgroundMode(EnvironmentBackgroundMode::PhysicalAtmospherePreview);
		scope.reset();
		const auto restored = environment.GetSettings();
		context.Check(restored.m_Intensity == 3.0f &&
			restored.m_BackgroundMode == EnvironmentBackgroundMode::PhysicalAtmospherePreview &&
			restored.m_RotationRadians == baseline.m_RotationRadians &&
			restored.m_EnableSkybox == baseline.m_EnableSkybox &&
			restored.m_QualityPreset == baseline.m_QualityPreset &&
			restored.m_BakeConfig == baseline.m_BakeConfig &&
			environment.GetBakeRequestGeneration() == generation + 2,
			"Release preserves later writers and restores the complete Custom bake recipe once");

		scope = environment.AcquireOverride();
		scope->Apply({ .m_Intensity = 4.0f, .m_PrefilteredSpecularSampleCount = 64 });
		const auto beforeSameValue = environment.GetBakeRequestGeneration();
		const auto beforeOwnership = environment.GetLightingControlRevision();
		environment.SetIntensity(4.0f);
		environment.SetPrefilteredSpecularSampleCount(64);
		context.Check(environment.GetLightingControlRevision() > beforeOwnership &&
			environment.GetBakeRequestGeneration() == beforeSameValue,
			"Equal writes by another lighting owner advance control identity without another bake");
		context.Check(!scope->OwnsBakeConfiguration() &&
			environment.GetBakeRequestGeneration() == beforeSameValue,
			"A same-value ordinary write takes ownership without scheduling a redundant bake");
		scope.reset();
		context.Check(environment.GetSettings().m_Intensity == 4.0f &&
			environment.GetSettings().m_BakeConfig.m_PrefilteredSpecularSampleCount == 64 &&
			environment.GetBakeRequestGeneration() == beforeSameValue,
			"Release never undoes a same-value write by another owner");
		const auto stableControl = environment.GetLightingControlRevision();
		environment.SetIntensity(4.0f);
		environment.SetPrefilteredSpecularSampleCount(64);
		context.Check(environment.GetLightingControlRevision() == stableControl,
			"Repeated writes by the same owner with equal settings are control NoChange");
		environment.SetIntensity(5.0f);
		environment.SetIntensity(4.0f);
		context.Check(environment.GetLightingControlRevision() > stableControl &&
			environment.GetBakeRequestGeneration() == beforeSameValue,
			"Lighting setting ABA advances control identity independently of effect generation");

		scope = environment.AcquireOverride();
		scope->Apply({ .m_Intensity = 5.0f, .m_PrefilteredSpecularSampleCount = 128 });
		environment.SetIntensity(6.0f);
		environment.SetPrefilteredSpecularMaxSampleLuminance(456.0f);
		const auto external = environment.GetSettings();
		scope->Apply({ .m_Intensity = 7.0f, .m_PrefilteredSpecularSampleCount = 256 });
		const auto beforeRelease = environment.GetBakeRequestGeneration();
		scope.reset();
		context.Check(environment.GetSettings().m_Intensity == external.m_Intensity &&
			environment.GetSettings().m_BakeConfig == external.m_BakeConfig &&
			environment.GetBakeRequestGeneration() == beforeRelease + 1,
			"Reclaiming an override restores the intervening writer's latest state on release");

		environment.SetQualityPreset(IBLQualityPreset::Low);
		const auto preset = environment.GetSettings();
		scope = environment.AcquireOverride();
		scope->Apply({ .m_Intensity = 8.0f, .m_PrefilteredSpecularSampleCount = 128 });
		environment.SetIntensity(std::numeric_limits<float>::quiet_NaN());
		environment.SetQualityPreset(static_cast<IBLQualityPreset>(255));
		environment.SetPrefilteredSpecularMaxSampleLuminance(std::numeric_limits<float>::infinity());
		context.Check(scope->OwnsBakeConfiguration(),
			"Rejected ordinary edits do not steal a lighting scope's ownership");
		scope.reset();
		context.Check(environment.GetSettings().m_QualityPreset == preset.m_QualityPreset &&
			environment.GetSettings().m_BakeConfig == preset.m_BakeConfig &&
			environment.GetSettings().m_Intensity == preset.m_Intensity,
			"Release restores the original preset as well as its full recipe");

		scope = environment.AcquireOverride();
		const auto beforeOperation = environment.GetBakeRequestGeneration();
		const auto requested = scope->ApplyPrefilteredSpecularSampleCount(13);
		context.Check(requested.m_Outcome == LightingMutationOutcome::Requested &&
			requested.m_PreviousGeneration == beforeOperation &&
			requested.m_Generation == environment.GetBakeRequestGeneration() &&
			requested.m_Generation > beforeOperation,
			"A scoped sample operation returns its actual newly requested target directly");
		const auto existing = scope->ApplyPrefilteredSpecularSampleCount(13);
		context.Check(existing.m_Outcome == LightingMutationOutcome::Unchanged &&
			existing.m_PreviousGeneration == requested.m_Generation &&
			existing.m_Generation == requested.m_Generation,
			"A matching scoped operation binds the existing target instead of replaying old request evidence");
		environment.SetPrefilteredSpecularSampleCount(13);
		const auto reclaimed = scope->ApplyPrefilteredSpecularSampleCount(13);
		context.Check(scope->OwnsBakeConfiguration() && reclaimed.m_Outcome == LightingMutationOutcome::Unchanged &&
			reclaimed.m_Generation == existing.m_Generation,
			"Reclaiming an equal target returns this operation's unchanged identity without an unnecessary bake");
		const auto clamped = scope->ApplyPrefilteredSpecularSampleCount(0);
		context.Check(clamped.m_Outcome == LightingMutationOutcome::Requested &&
			clamped.m_PreviousGeneration == existing.m_Generation &&
			clamped.m_Generation == environment.GetBakeRequestGeneration() &&
			environment.GetSettings().m_BakeConfig.m_PrefilteredSpecularSampleCount == 1 &&
			existing.m_Generation == requested.m_Generation,
			"The domain sanitizes samples consistently and earlier returned evidence remains an owned value");
		scope.reset();

		auto temporary = std::make_unique<EnvironmentLightingSystem>(
			EnvironmentLightingSystem::CreateInfo{ .m_RenderResourceRegistry = &registry });
		scope = temporary->AcquireOverride();
		scope->Apply({ .m_Intensity = 2.0f });
		temporary.reset();
		scope->Apply({ .m_Intensity = 3.0f });
		const auto expired = scope->ApplyPrefilteredSpecularSampleCount(64);
		context.Check(expired.m_Outcome == LightingMutationOutcome::Rejected &&
			expired.m_PreviousGeneration == 0 && expired.m_Generation == 0,
			"An expired lighting scope cannot return a current target or replay earlier evidence");
		context.Check(!scope->OwnsBakeConfiguration(),
			"A lighting scope may outlive its Runtime owner without accessing destroyed state");
		scope.reset();
	}

	void RunEnvironmentLightingSettingsTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		device.m_CreateValidDescriptors = true;
		TransientResourcePool pool(&device);
		SamplerRegistry samplers({ .m_Device = &device });
		RenderResourceRegistry registry({
			.m_Device = &device,
			.m_TransientResourcePool = &pool,
			.m_SamplerRegistry = &samplers,
		});
		EnvironmentLightingSystem environment({ .m_RenderResourceRegistry = &registry });
		const EnvironmentLightingViewBase& view = environment;
		EnvironmentLightingControlBase& control = environment;
		const auto initial = view.GetEnvironmentLightingSettings();
		context.Check(initial.m_Intensity == 1.0f && initial.m_RotationRadians == 0.0f &&
			initial.m_EnableSkybox && initial.m_BackgroundMode == EnvironmentBackgroundMode::TextureEnvironment &&
			initial.m_QualityPreset == IBLQualityPreset::Medium &&
			initial.m_BakeConfig == GetIBLBakeConfig(IBLQualityPreset::Medium) &&
			environment.GetBakeRequestGeneration() == 0,
			"Environment query preserves initial settings without requesting a bake");

		constexpr auto PreviewTypes = std::array{
			IBLPreviewType::Environment,
			IBLPreviewType::Irradiance,
			IBLPreviewType::PrefilteredSpecular,
		};
		const auto clearPreviews = [&]()
		{
			for (auto type : PreviewTypes)
			{
				registry.PublishIBLPreview(type, registry.ObserveIBLPreviewRequest(type), IBLPreviewLayout::Cross, 0);
			}
		};
		const auto allPreviewsDirty = [&]()
		{
			return std::ranges::all_of(PreviewTypes,
				[&](auto type) { return registry.IsIBLPreviewDirty(type); });
		};
		clearPreviews();
		control.SetIntensity(2.0f);
		context.Check(view.GetEnvironmentLightingSettings().m_Intensity == 2.0f &&
			initial.m_Intensity == 1.0f && allPreviewsDirty() &&
			environment.GetBakeRequestGeneration() == 0,
			"Environment intensity invalidates previews but not captured settings or bake generation");
		clearPreviews();
		control.SetIntensity(2.0f);
		control.SetIntensity(std::numeric_limits<float>::quiet_NaN());
		control.SetIntensity(std::numeric_limits<float>::infinity());
		context.Check(view.GetEnvironmentLightingSettings().m_Intensity == 2.0f &&
			std::ranges::none_of(PreviewTypes,
				[&](auto type) { return registry.IsIBLPreviewDirty(type); }),
			"Unchanged and non-finite intensity inputs do not invalidate previews");
		control.SetIntensity(-2.0f);
		context.Check(view.GetEnvironmentLightingSettings().m_Intensity == 0.0f &&
			allPreviewsDirty(), "Environment intensity remains clamped to nonnegative values");
		clearPreviews();
		constexpr float Rotation = 2.5f * std::numbers::pi_v<float>;
		control.SetRotationRadians(Rotation);
		const auto rotated = view.GetEnvironmentLightingSettings();
		context.Check(std::abs(rotated.m_RotationRadians - std::remainder(Rotation, 2.0f * std::numbers::pi_v<float>)) < 1.0e-6f &&
			allPreviewsDirty() && environment.GetBakeRequestGeneration() == 0,
			"Environment yaw wraps and invalidates previews without scheduling a bake");
		clearPreviews();
		control.SetRotationRadians(rotated.m_RotationRadians);
		control.SetRotationRadians(std::numeric_limits<float>::quiet_NaN());
		control.SetRotationRadians(std::numeric_limits<float>::infinity());
		control.SetSkyboxEnabled(false);
		control.SetBackgroundMode(EnvironmentBackgroundMode::PhysicalAtmospherePreview);
		control.SetBackgroundMode(static_cast<EnvironmentBackgroundMode>(255));
		context.Check(view.GetEnvironmentLightingSettings().m_RotationRadians ==
			rotated.m_RotationRadians && !view.GetEnvironmentLightingSettings().m_EnableSkybox &&
			view.GetEnvironmentLightingSettings().m_BackgroundMode == EnvironmentBackgroundMode::PhysicalAtmospherePreview &&
			environment.GetBakeRequestGeneration() == 0 &&
			std::ranges::none_of(PreviewTypes,
				[&](auto type) { return registry.IsIBLPreviewDirty(type); }),
			"Background preview selection does not alter texture IBL bake or preview state");

		control.SetQualityPreset(IBLQualityPreset::Low);
		const auto low = view.GetEnvironmentLightingSettings();
		context.Check(low.m_QualityPreset == IBLQualityPreset::Low &&
			low.m_BakeConfig == GetIBLBakeConfig(IBLQualityPreset::Low) &&
			environment.GetBakeRequestGeneration() == 1,
			"A concrete environment preset requests one bake with its unchanged configuration");
		control.SetQualityPreset(IBLQualityPreset::Low);
		control.SetQualityPreset(IBLQualityPreset::Custom);
		control.SetQualityPreset(IBLQualityPreset::Count);
		control.SetQualityPreset(static_cast<IBLQualityPreset>(255));
		context.Check(environment.GetBakeRequestGeneration() == 1 &&
			view.GetEnvironmentLightingSettings().m_BakeConfig == low.m_BakeConfig,
			"Repeated and invalid environment presets leave the request unchanged");
		control.SetPrefilteredSpecularSampleCount(0);
		const auto custom = view.GetEnvironmentLightingSettings();
		context.Check(custom.m_QualityPreset == IBLQualityPreset::Custom &&
			custom.m_BakeConfig.m_PrefilteredSpecularSampleCount == 1 &&
			environment.GetBakeRequestGeneration() == 2,
			"Custom environment samples clamp at the lower bound and request a bake");
		control.SetPrefilteredSpecularSampleCount(1);
		context.Check(environment.GetBakeRequestGeneration() == 2,
			"Repeated environment sample counts do not request redundant bakes");
		control.SetPrefilteredSpecularSampleCount(std::numeric_limits<uint32_t>::max());
		const auto maxSamples = view.GetEnvironmentLightingSettings().m_BakeConfig;
		context.Check(maxSamples.m_PrefilteredSpecularSampleCount == 4096 &&
			environment.GetBakeRequestGeneration() == 3,
			"Custom environment samples preserve the upper bound");
		control.SetPrefilteredSpecularMaxSampleLuminance(std::numeric_limits<float>::quiet_NaN());
		control.SetPrefilteredSpecularMaxSampleLuminance(std::numeric_limits<float>::infinity());
		const auto nonFiniteLuminance = view.GetEnvironmentLightingSettings().m_BakeConfig;
		context.Check(environment.GetBakeRequestGeneration() == 3 &&
			nonFiniteLuminance.m_PrefilteredSpecularMaxSampleLuminance == 1000.0f,
			"Non-finite environment luminance inputs do not change requested settings");
		control.SetPrefilteredSpecularMaxSampleLuminance(0.0f);
		control.SetPrefilteredSpecularMaxSampleLuminance(1.0f);
		const auto minLuminance = view.GetEnvironmentLightingSettings().m_BakeConfig;
		context.Check(minLuminance.m_PrefilteredSpecularMaxSampleLuminance == 1.0f &&
			environment.GetBakeRequestGeneration() == 4,
			"Environment luminance clamps low and repeated effective values do not rebake");
		control.SetPrefilteredSpecularMaxSampleLuminance(100000.0f);
		const auto maxLuminance = view.GetEnvironmentLightingSettings().m_BakeConfig;
		context.Check(maxLuminance.m_PrefilteredSpecularMaxSampleLuminance == 65000.0f &&
			environment.GetBakeRequestGeneration() == 5 &&
			low.m_BakeConfig.m_PrefilteredSpecularSampleCount == 128,
			"Environment luminance clamps high while older copied settings remain unchanged");
		control.RequestRebake(true);
		context.Check(environment.GetBakeRequestGeneration() == 6 &&
			environment.ShouldIgnoreCache(6),
			"Forced environment rebuild bypasses the cache for its requested generation");
		control.RequestRebake();
		control.RequestRebake();
		context.Check(environment.GetBakeRequestGeneration() == 8 &&
			!environment.ShouldIgnoreCache(8) &&
			device.m_CreateTextureCount == 0 && device.m_RecordTextureUseCount == 0,
			"Explicit rebuild requests advance independently without allocating or submitting GPU work");

		control.SetBackgroundMode(EnvironmentBackgroundMode::PhysicalSky);
		const AtmosphereSettings earth{};
		auto sun = ResolveWorldSun(WorldSunSettings{}, -Vector3::UnitY);
		environment.ResolveWorldLighting(earth, sun, 42);
		const auto firstPhysical = environment.GetRequestedPhysicalSky();
		const uint64_t firstGeneration = environment.GetBakeRequestGeneration();
		context.Check(firstPhysical && !environment.GetActivePhysicalSky() &&
			std::abs(firstPhysical->m_ReferenceObserverWorld.m_Y - 1.0f) < 0.01f,
			"Physical sky captures a fixed one-meter observer without fabricating texture provenance");
		environment.ResolveWorldLighting(earth, sun, 42);
		context.Check(environment.GetBakeRequestGeneration() == firstGeneration,
			"Unchanged physical parameters do not rebake; camera pose and exposure are absent from the request");
		context.Check(environment.PublishWorldLighting(firstPhysical, firstGeneration),
			"A complete current world-lighting snapshot can be published");
		sun = ResolveWorldSun(WorldSunSettings{ .m_PerpendicularIlluminanceLux = 60000.0f }, Vector3(1.0f, -1.0f, 0.0f));
		environment.ResolveWorldLighting(earth, sun, 42);
		const auto pending = environment.GetRequestedPhysicalSky();
		const auto& active = environment.GetActivePhysicalSky();
		context.Check(pending && active && pending->m_Sun.m_PerpendicularIlluminanceLux == 60000.0f &&
			active->m_Sun.m_PerpendicularIlluminanceLux == 120000.0f &&
			environment.GetRenderSettings().m_Intensity == 1.0f &&
			environment.GetRenderSettings().m_RotationRadians == 0.0f,
			"Sun edits remain pending while physical IBL retains the active sun, neutral intensity and world alignment");
		context.Check(!environment.PublishWorldLighting(firstPhysical, firstGeneration) &&
			environment.GetActivePhysicalSky()->m_Sun.m_PerpendicularIlluminanceLux == 120000.0f,
			"Superseded world-lighting publications cannot change the active generation");
		context.Check(environment.PublishWorldLighting(pending, environment.GetBakeRequestGeneration()),
			"The replacement snapshot switches after the publication gate");
		const uint64_t beforeSessionChange = environment.GetBakeRequestGeneration();
		environment.ResolveWorldLighting(earth, sun, 42, 1);
		context.Check(environment.GetBakeRequestGeneration() == beforeSessionChange + 1 &&
			environment.GetRequestedPhysicalSky()->m_SessionIdentity == 1 &&
			environment.GetActivePhysicalSky()->m_SessionIdentity == 0,
			"A replacement Lab session cannot reuse the old sun entity identity while its lighting remains pending");
		control.SetBackgroundMode(EnvironmentBackgroundMode::TextureEnvironment);
		environment.ResolveWorldLighting(earth, sun, 42);
		context.Check(!environment.GetRequestedPhysicalSky() && environment.GetActivePhysicalSky() &&
			environment.GetRenderSettings().m_BackgroundMode == EnvironmentBackgroundMode::PhysicalSky,
			"A source-mode switch retains the complete physical generation while texture IBL is pending");
		context.Check(environment.PublishWorldLighting(std::nullopt, environment.GetBakeRequestGeneration()),
			"Texture IBL completion clears the linked physical source");
		environment.ResolveWorldLighting(earth, sun, 42);
		context.Check(!environment.GetActivePhysicalSky() &&
			environment.GetRenderSettings().m_Intensity == view.GetEnvironmentLightingSettings().m_Intensity,
			"Texture mode restores its authored artistic intensity only after publication");
	}

	void RunLightingProfileContractTests(SelfTestContext& context) noexcept
	{
		struct DirectionRotationCase
		{
			Vector3 m_From;
			Vector3 m_To;
		};
		const std::array rotations
		{
			DirectionRotationCase{ Vector3::Forward, Vector3::Forward },
			DirectionRotationCase{ Vector3::Forward, Vector3(0.0f, -0.0001745329f, 1.0f) },
			DirectionRotationCase{ Vector3::Forward, Vector3(0.0f, 0.0001745329f, 1.0f) },
			DirectionRotationCase{ Vector3::Forward, Vector3(0.00001f, 0.0f, 1.0f) },
			DirectionRotationCase{ Vector3::Forward, Vector3(0.0001745329f, 0.0f, -1.0f) },
			DirectionRotationCase{ Vector3::Forward, Vector3(-0.001745329f, 0.0f, -1.0f) },
			DirectionRotationCase{ Vector3::Forward, Vector3::Backward },
			DirectionRotationCase{ Vector3::UnitX, -Vector3::UnitX },
			DirectionRotationCase{ Vector3::UnitY, Vector3(0.0001f, 1.0f, 0.0f) },
			DirectionRotationCase{ Vector3(2.0f, 0.0f, 0.0f), Vector3(-3.0f, 0.0003f, 0.0f) },
		};
		for (const auto& test : rotations)
		{
			const auto rotation = math::RotationFromTo(test.m_From, test.m_To);
			const auto actual = math::TransformDirection(test.m_From.Normalized(), math::CreateFromQuaternion(rotation));
			context.Check(math::IsFinite(rotation) && (actual - test.m_To.Normalized()).Length() < 1.0e-6f,
				"Direction rotations preserve small parallel and antiparallel angles on every axis");
		}
		WorldLightingProfile profile{ .m_Id = "test.daylight" };
		const auto hash = HashLightingProfile(profile);
		context.Check(ValidateLightingProfile(profile) && hash.IsValid() &&
			Sha256DigestToHex(hash) ==
				"04dc4b18a002e5a66f77d4b4558c3f7246b09bd9c4cb7e6899ceb52a403ad9d7",
			std::format("Physical Sky profiles preserve the v2 explicit-field SHA-256 fixture ({})",
				Sha256DigestToHex(hash)));
		auto alias = profile;
		alias.m_Id = "test.alias";
		alias.m_RecommendedEV100 = 17.0f;
		alias.m_Inputs.m_IBLRotationRadians = -0.0f;
		context.Check(HashLightingProfile(alias) == hash,
			"Profile identity excludes labels/exposure advice and canonicalizes signed zero");
		alias.m_Inputs.m_Atmosphere.m_MieScattering *= 0.5f;
		context.Check(HashLightingProfile(alias) != hash,
			"Atmosphere transport inputs participate in the profile hash");
		alias = profile;
		alias.m_Inputs.m_Sun.m_PerpendicularIlluminanceLux = 1000001.0f;
		context.Check(!ValidateLightingProfile(alias), "Profiles reject illuminance that Runtime would clamp");
		alias = profile;
		alias.m_Inputs.m_Atmosphere.m_MieAnisotropy = 0.99f;
		context.Check(!ValidateLightingProfile(alias), "Profiles reject atmosphere inputs that Runtime would clamp");
		alias = profile;
		alias.m_Inputs.m_IBLIntensity = 2.0f;
		context.Check(!ValidateLightingProfile(alias), "Physical Sky profiles reject unsupported artistic intensity");
		World world;
		context.Check(!ApplyWorldLightingInputsToWorld(world, profile.m_Inputs) && !world.m_Atmosphere,
			"A profile cannot fabricate a missing designated sun");
		auto& registry = world.GetRegistry();
		const auto entity = registry.create();
		registry.emplace<components::TransformComponent>(entity);
		components::LightComponent light;
		light.m_Type = LightType::Directional;
		light.m_WorldSun.emplace();
		registry.emplace<components::LightComponent>(entity, light);
		RecordingDevice device;
		device.m_CreateValidDescriptors = true;
		TransientResourcePool pool(&device);
		SamplerRegistry samplers({ .m_Device = &device });
		RenderResourceRegistry resources({ .m_Device = &device,
			.m_TransientResourcePool = &pool, .m_SamplerRegistry = &samplers });
		EnvironmentLightingSystem environment({ .m_RenderResourceRegistry = &resources });
		environment.SetBackgroundMode(EnvironmentBackgroundMode::PhysicalSky);
		context.Check(ApplyWorldLightingInputsToWorld(world, profile.m_Inputs),
			"Profile input application uses the production designated-sun selection");
		environment.SynchronizeWorldLighting(world, 17);
		const auto original = environment.GetWorldLightingPublication().m_Requested;
		const auto prepared = PrepareWorldLightingInputs(profile.m_Inputs);
		context.Check(original && EqualWorldLightingAuthoring(*original, prepared) &&
			EqualWorldLightingEffect(*original, prepared) && original->m_SessionIdentity == 17,
			"Actual requests retain authored inputs independently of profile metadata");
		context.Check(environment.PublishWorldLighting(environment.GetRequestedPhysicalSky(), original->m_Generation),
			"Profile provenance crosses the existing Runtime publication gate");

		std::string preparationError;
		auto pendingInputs = profile.m_Inputs;
		pendingInputs.m_Sun.m_PerpendicularIlluminanceLux *= 0.5f;
		auto plan = environment.PrepareWorldLightingInputs(world, 17, pendingInputs, preparationError);
		context.Check(plan && plan->HasChanges() && preparationError.empty() &&
			environment.GetBakeRequestGeneration() == original->m_Generation &&
			registry.get<components::LightComponent>(entity).m_WorldSun->m_PerpendicularIlluminanceLux == 120000.0f,
			"Preparing a lighting mutation creates neither live authoring nor a Runtime request");
		plan.reset();
		context.Check(environment.GetBakeRequestGeneration() == original->m_Generation &&
			EqualWorldLightingAuthoring(*environment.GetWorldLightingPublication().m_Requested, prepared),
			"Discarding a prepared lighting plan preserves request ownership and authored provenance");
		World missingSun;
		context.Check(!environment.PrepareWorldLightingInputs(missingSun, 17, pendingInputs, preparationError) &&
			!preparationError.empty() && environment.GetBakeRequestGeneration() == original->m_Generation,
			"Missing current World targets reject during preparation without mutation");

		auto& rotation = registry.get<components::TransformComponent>(entity).m_Rotation;
		rotation = Quaternion(-rotation.m_X, -rotation.m_Y, -rotation.m_Z, -rotation.m_W);
		const auto unchanged = environment.ApplyWorldLightingInputs(world, 17, profile.m_Inputs);
		context.Check(unchanged.m_Outcome == LightingMutationOutcome::Unchanged &&
			unchanged.m_Generation == original->m_Generation,
			"Quaternion sign equivalence neither requests a bake nor changes attribution");
		auto equivalent = profile;
		equivalent.m_Inputs.m_SunDirection *= 2.0f;
		context.Check(HashLightingProfile(equivalent) == hash &&
			EqualWorldLightingEffect(prepared, PrepareWorldLightingInputs(equivalent.m_Inputs)),
			"Hash v2 and effect identity agree on producer-equivalent direction representations");
		equivalent = profile;
		equivalent.m_Inputs.m_Sun.m_Chromaticity *= 0.5f;
		const auto gray = PrepareWorldLightingInputs(equivalent.m_Inputs);
		context.Check(EqualWorldLightingEffect(prepared, gray) && !EqualWorldLightingAuthoring(prepared, gray),
			"Chromaticity normalization does not conflate effective and authored identity");
		registry.get<components::LightComponent>(entity).m_WorldSun->m_Chromaticity *= 0.5f;
		environment.SynchronizeWorldLighting(world, 17);
		const auto deduplicated = environment.GetWorldLightingPublication();
		context.Check(deduplicated.m_Requested && deduplicated.m_Active &&
			EqualWorldLightingAuthoring(*deduplicated.m_Requested, prepared) &&
			!EqualWorldLightingAuthoring(*deduplicated.m_Requested, gray) &&
			EqualWorldLightingAuthoring(*deduplicated.m_Active, prepared),
			"An effective-equivalent World edit cannot relabel an older requested or published generation");
		const auto grayResult = environment.ApplyWorldLightingInputs(world, 17, equivalent.m_Inputs);
		context.Check(grayResult.m_Outcome == LightingMutationOutcome::Unchanged &&
			grayResult.m_Generation == original->m_Generation,
			"An effective-equivalent selection reports Unchanged without a forced rebake");

		world.m_Atmosphere->m_RayleighScaleHeightMeters *= 2.0f;
		context.Check(!environment.PrepareWorldLightingInputs(world, 17, profile.m_Inputs, preparationError) &&
			environment.GetBakeRequestGeneration() == original->m_Generation &&
			world.m_Atmosphere->m_RayleighScaleHeightMeters == 16000.0f,
			"Preparation does not silently settle or overwrite a pre-existing unresolved edit");
		const auto replacement = environment.ApplyWorldLightingInputs(world, 17, profile.m_Inputs);
		context.Check(replacement.m_Outcome == LightingMutationOutcome::Requested &&
			replacement.m_PreviousGeneration == original->m_Generation + 1 &&
			replacement.m_Generation == replacement.m_PreviousGeneration + 1 &&
			environment.GetWorldLightingPublication().m_Active->m_Generation == original->m_Generation,
			"Pre-existing unresolved edits settle before a separately attributed profile mutation");
		context.Check(!ApplyWorldLightingInputsToWorld(world, alias.m_Inputs) &&
			world.m_Atmosphere->m_RayleighScaleHeightMeters == 8000.0f,
			"Invalid profile inputs reject before mutating the World");

		environment.SetBackgroundMode(EnvironmentBackgroundMode::TextureEnvironment);
		auto scope = environment.AcquireOverride();
		scope->Apply({ .m_BackgroundMode = EnvironmentBackgroundMode::PhysicalSky });
		environment.SynchronizeWorldLighting(world, 17);
		const auto sameProfile = environment.ApplyWorldLightingInputs(world, 17, profile.m_Inputs);
		scope.reset();
		context.Check(sameProfile.m_Outcome == LightingMutationOutcome::Unchanged &&
			environment.GetSettings().m_BackgroundMode == EnvironmentBackgroundMode::TextureEnvironment,
			"A NoChange profile does not steal the entered owner's background override");
		scope = environment.AcquireOverride();
		scope->Apply({ .m_BackgroundMode = EnvironmentBackgroundMode::PhysicalSky });
		environment.SynchronizeWorldLighting(world, 17);
		const auto changedProfile = environment.ApplyWorldLightingInputs(world, 17, pendingInputs);
		scope.reset();
		context.Check(changedProfile.m_Outcome == LightingMutationOutcome::Requested &&
			environment.GetSettings().m_BackgroundMode == EnvironmentBackgroundMode::PhysicalSky,
			"A committed profile mutation owns its background and survives the previous Lab's release");

		environment.SetBackgroundMode(EnvironmentBackgroundMode::TextureEnvironment);
		scope = environment.AcquireOverride();
		scope->Apply({ .m_BackgroundMode = EnvironmentBackgroundMode::PhysicalSky });
		environment.SynchronizeWorldLighting(world, 17);
		const auto beforeSun = environment.GetBakeRequestGeneration();
		const auto beforeSunSettings = environment.GetSettings();
		// Equivalent authoring is not substituted by an older request's provenance.
		auto& authoredSun = *registry.get<components::LightComponent>(entity).m_WorldSun;
		authoredSun.m_Chromaticity *= 0.5f;
		environment.SynchronizeWorldLighting(world, 17);
		const Vector3 smallDirection(0.0f, -0.0001745329f, 1.0f);
		context.Check(!environment.PrepareWorldSunDirection(world, 18, smallDirection, preparationError) &&
			!environment.PrepareWorldSunDirection(world, 17, Vector3::Zero, preparationError) &&
			environment.GetBakeRequestGeneration() == beforeSun,
			"Sun direction preparation rejects foreign sessions and invalid directions without mutation");
		auto sunPlan = environment.PrepareWorldSunDirection(world, 17, smallDirection, preparationError);
		context.Check(sunPlan && sunPlan->HasChanges() && preparationError.empty() &&
			environment.GetBakeRequestGeneration() == beforeSun &&
			ResolveWorldSunDirection(rotation).m_Y < -0.99f,
			"Preparing a sun-only plan preserves the World and existing request");
		sunPlan.reset();
		sunPlan = environment.PrepareWorldSunDirection(world, 17, smallDirection, preparationError);
		const auto sunResult = sunPlan ? sunPlan->Commit() : LightingMutationResult{};
		const auto sunRequest = environment.GetWorldLightingPublication().m_Requested;
		context.Check(sunResult.m_Outcome == LightingMutationOutcome::Requested &&
			sunResult.m_PreviousGeneration == beforeSun && sunResult.m_Generation == beforeSun + 1 &&
			(ResolveWorldSunDirection(rotation) - smallDirection.Normalized()).Length() < 1.0e-7f &&
			authoredSun.m_Chromaticity.m_X == 0.5f && authoredSun.m_PerpendicularIlluminanceLux == 60000.0f &&
			world.m_Atmosphere->m_RayleighScaleHeightMeters == 8000.0f && sunRequest &&
			sunRequest->m_Inputs.m_Sun.m_Chromaticity.m_X == 0.5f &&
			environment.GetSettings().m_BackgroundMode == beforeSunSettings.m_BackgroundMode &&
			environment.GetSettings().m_BakeConfig == beforeSunSettings.m_BakeConfig,
			"A sun-only commit preserves unrelated authoring and settings while requesting its actual direction");
		sunPlan = environment.PrepareWorldSunDirection(world, 17, smallDirection, preparationError);
		const auto sameSun = sunPlan ? sunPlan->Commit() : LightingMutationResult{};
		context.Check(sameSun.m_Outcome == LightingMutationOutcome::Unchanged &&
			sameSun.m_Generation == sunResult.m_Generation,
			"Repeating a small sun direction binds the same target without another bake");
		scope.reset();
		context.Check(environment.GetSettings().m_BackgroundMode == EnvironmentBackgroundMode::TextureEnvironment,
			"A sun-only mutation preserves the entered lease's background restoration on exit");

		scope = environment.AcquireOverride();
		scope->Apply({ .m_BackgroundMode = EnvironmentBackgroundMode::PhysicalSky });
		environment.SynchronizeWorldLighting(world, 17);
		sunPlan = environment.PrepareWorldSunDirection(world, 17, Vector3(1.0f, -1.0f, 0.0f), preparationError);
		const auto otherSun = sunPlan ? sunPlan->Commit() : LightingMutationResult{};
		environment.SetBackgroundMode(EnvironmentBackgroundMode::PhysicalSky);
		scope.reset();
		context.Check(otherSun.m_Outcome == LightingMutationOutcome::Requested &&
			environment.GetSettings().m_BackgroundMode == EnvironmentBackgroundMode::PhysicalSky,
			"A same-value ordinary background writer still takes ownership after a sun-only mutation");

		auto invalid = profile;
		invalid.m_Inputs.m_SunDirection = Vector3(std::numeric_limits<float>::max(), 1.0f, 0.0f);
		context.Check(!ValidateLightingProfile(invalid),
			"Finite direction components still reject squared-length overflow");
		invalid = profile;
		invalid.m_RecommendedEV100 = 25.0f;
		context.Check(!ValidateLightingProfile(invalid) && !Camera::IsValidManualEV100(25.0f),
			"Exposure advice uses the Camera's strict supported EV100 range");
		invalid = profile;
		invalid.m_Inputs.m_Sun.m_PerpendicularIlluminanceLux = 0.0f;
		context.Check(ValidateWorldSunSettings(invalid.m_Inputs.m_Sun) && !ValidateLightingProfile(invalid),
			"Zero illuminance is valid in WorldSun but excluded by the named-profile policy");
		invalid.m_Inputs.m_Atmosphere.m_MieExtinction = 0.0f;
		context.Check(!ValidateAtmosphereSettings(invalid.m_Inputs.m_Atmosphere),
			"Atmosphere owns the extinction/scattering relationship");
		context.Check(math::IsInClosedRange(1.0f, 0.0f, 1.0f) &&
			!math::IsInClosedRange(std::numeric_limits<float>::infinity(), 0.0f, 1.0f),
			"Math's finite closed interval helper contains no domain policy");
	}

	void RunAtmosphereContractTests(SelfTestContext& context) noexcept
	{
		World authoredWorld;
		authoredWorld.m_Atmosphere = AtmosphereSettings{};
		authoredWorld.m_Atmosphere->m_MieAnisotropy = 0.6f;
		World movedWorld(std::move(authoredWorld));
		context.Check(movedWorld.m_Atmosphere && movedWorld.m_Atmosphere->m_MieAnisotropy == 0.6f &&
			!authoredWorld.m_Atmosphere, "World movement preserves authored atmosphere and clears the moved-away owner");
		AtmosphereSettings settings{};
		const auto sun = ResolveWorldSun(WorldSunSettings{}, -Vector3::UnitY);
		const auto a = ResolveAtmosphere(settings, sun, Vector3(0.0f, 1000.0f, 0.0f));
		context.Check(std::abs(a.m_Observer.m_X - a.m_Radii.m_X - 1.0f) < 0.001f &&
			std::abs(a.m_Rayleigh.m_X - 0.005802f) < 1e-7f && a.m_Rayleigh.m_W == 8.0f,
			"Atmosphere boundary converts meters and inverse meters to kilometers and resolves radial altitude");
		const auto shortPath = EvaluateAtmosphereTransmittance(a, 1000.0f, 1.0f, 1000.0f);
		const auto longPath = EvaluateAtmosphereTransmittance(a, 1000.0f, 1.0f, 50000.0f);
		context.Check(shortPath.m_X <= 1.0f && longPath.m_X > 0.0f && longPath.m_X < shortPath.m_X &&
			longPath.m_Y < shortPath.m_Y && longPath.m_Z < shortPath.m_Z &&
			EvaluateAtmosphereTransmittance(a, 1000.0f, 1.0f, 0.0f).m_X == 1.0f &&
			EvaluateAtmosphereTransmittance(a, 1000.0f, -1.0f, 2000.0f).m_X == 0.0f,
			"Earth transmittance is finite, bounded, decreases with path length, and rejects planet-occluded sun paths");
		settings.m_WorldUnitsToMeters = 1000.0f;
		const auto scaled = ResolveAtmosphere(settings, sun, Vector3(0.0f, 1.0f, 0.0f));
		context.Check(scaled.m_Observer.m_X == a.m_Observer.m_X && AtmosphereDirtyMask(a, scaled) == 0,
			"Equivalent meter and kilometer worlds resolve identical atmosphere transport");
		context.Check(a.m_World.m_W == 0.001f && scaled.m_World.m_W == 1.0f &&
			std::abs(a.m_World.m_Y + 6360.0f) < 0.001f,
			"Physical sky and direct sunlight use the same resolved world-to-atmosphere transform");
		auto changed = a;
		changed.m_Sun.m_X *= 2.0f;
		bool invalidation = AtmosphereDirtyMask(a, changed) == 4;
		changed = a;
		changed.m_Observer.m_X += 1.0f;
		invalidation &= AtmosphereDirtyMask(a, changed) == 4;
		changed = a;
		changed.m_Ground.m_X += 0.1f;
		invalidation &= AtmosphereDirtyMask(a, changed) == 6;
		changed = a;
		changed.m_Mie.m_W = 0.5f;
		invalidation &= AtmosphereDirtyMask(a, changed) == 4;
		changed = a;
		changed.m_Rayleigh.m_X *= 2.0f;
		invalidation &= AtmosphereDirtyMask(a, changed) == 7;
		context.Check(invalidation, "Atmosphere dirty masks preserve transmittance and unit-sun scattering when only the sun or observer changes");
		settings.m_MieExtinction = -1.0f;
		settings.m_MieAnisotropy = std::numeric_limits<float>::quiet_NaN();
		settings.m_RayleighScaleHeightMeters = 0.0f;
		const auto sanitized = ResolveAtmosphere(settings, sun, Vector3::Zero);
		context.Check(sanitized.m_Mie.m_Y >= sanitized.m_Mie.m_X && std::isfinite(sanitized.m_Mie.m_W) &&
			sanitized.m_Rayleigh.m_W > 0.0f, "Atmosphere sanitization preserves nonnegative absorption and finite density scales");

		RecordingDevice device;
		device.m_PassConstantsEnabled = true;
		device.m_CreateValidDescriptors = true;
		device.m_TextureViewsSupported = true;
		device.m_UseControlledFenceCompletion = true;
		PersistentTexturePool pool(&device);
		AtmosphereSystem system(&device, &pool);
		class ShaderAccess final : public RenderShaderProgramAccess
		{
		public:
			ShaderID LoadProgram(const ShaderProgramRef&) noexcept override { return ShaderID{ 1 }; }
			uint64_t GetGeneration(ShaderID) const noexcept override { return 1; }
		} shaders;
		class BindingAccess final : public RenderBindingLayoutAccess
		{
		public:
			RHIBindingLayoutHandle GetCommonBindingLayout() const noexcept override { return {}; }
			RHIBindingLayoutDesc GetCommonBindingLayoutDesc() const noexcept override { return {}; }
			const ComposedLayout* GetCommonLayoutContract() const noexcept override
			{
				return &scene_bindings::GetCommonLayoutContract();
			}
		} bindings;
		class PipelineAccess final : public RenderPipelineResolver
		{
		public:
			RHIPipelineHandle Resolve(GraphicsPipelineSlot&, const GraphicsPhysicalPipelineKey&, const RenderPassInfo&) noexcept override
			{
				return {};
			}
			RHIPipelineHandle Resolve(ComputePipelineSlot&, const ComputePipelineRecipe&, const RenderPassInfo&) noexcept override
			{
				return RHIPipelineHandle{ 1, 1 };
			}
			void GetPipelineUsages(RHIPipelineHandle, std::vector<RenderPassInfo>&) const noexcept override {}
		} pipelines;
		class SamplerAccess final : public RenderSamplerAccess
		{
		public:
			SamplerID GetOrCreateSampler(const SamplerKey&) noexcept override { return {}; }
			SamplerID GetPresetSamplerId(SamplerPreset) const noexcept override { return {}; }
			uint32_t GetSamplerIndex(SamplerPreset) const noexcept override { return 0; }
			uint32_t GetSamplerIndex(const SamplerID&) const noexcept override { return 0; }
			uint32_t ResolveSamplerIndex(SamplerID, SamplerPreset) const noexcept override { return 0; }
		} samplers;
		BindingTestPresentation bindingPresentation;
		const RenderServices services{ .m_PipelineResolver = &pipelines, .m_ShaderPrograms = &shaders,
			.m_Samplers = &samplers, .m_Presentation = &bindingPresentation,
			.m_BindingLayout = &bindings, .m_Atmosphere = &system };
		RenderFrameBuildResult frame{};
		RHIUploadPool uploads(device);
		RHIUploadWriter writer;
		auto uploadBatch = uploads.BeginBatch();
		uploadBatch->Attach(writer);
		frame.m_Uploads = writer.GetFrameUploads();
		frame.m_RenderScene.m_Atmosphere = AtmosphereSettings{};
		frame.m_RenderScene.m_WorldSun = sun;
		frame.m_RenderScene.m_WorldSunLightIndex = 0;
		frame.m_RenderViews.resize(1);
		RenderGraph graph({ .m_Device = &device,
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }) });
		RenderPassAtmosphere pass;
		pass.AddPass(graph, frame.MakeRenderFrameContext(), services);
		pass.AddFinishPass(graph);
		const bool compiled = graph.Compile();
		RGSnapshot snapshot;
		BuildRenderGraphSnapshot(graph, snapshot);
		bool dependencies = compiled && snapshot.m_Passes.size() == 5;
		for (size_t i = 1; dependencies && i < 4; ++i)
		{
			dependencies &= !snapshot.m_Passes[i].m_Culled && snapshot.m_Passes[i].m_Accesses.size() == i &&
				snapshot.m_Passes[i].m_ExecutionOrder < snapshot.m_Passes[i + 1].m_ExecutionOrder;
		}
		context.Check(dependencies, "Production atmosphere graph retains ordered transmittance, multiple scattering, sky and export dependencies");
		system.EndFrame(false, {});
		device.m_AtmosphereDestroyedBufferCount = 0;
		const bool first = system.Begin(a, { 1,1,1 });
		context.Check(first && system.GetDiagnostics().m_DirtyMask == 7, "Cold atmosphere allocation requires all three LUT producers");
		if (first)
		{
			for (uint32_t i = 0; i < 3; ++i)
			{
				system.NotifyExecuted(i);
			}
		}
		system.EndFrame(true, { RHIFenceHandle{ 1,1 },10 });
		const bool cached = system.Begin(a, { 1,1,1 });
		context.Check(cached && system.GetDiagnostics().m_DirtyMask == 0 &&
			system.GetDiagnostics().m_Generations[0] == 1,
			"Cached atmosphere keeps its LUT generations without owning a constants buffer");
		system.EndFrame(true, { RHIFenceHandle{ 1,1 },11 });
		changed = a;
		changed.m_Observer.m_X += 1.0f;
		const bool updated = system.Begin(changed, { 1,1,1 });
		context.Check(updated && system.GetDiagnostics().m_DirtyMask == 4, "Observer edits schedule only the sky-view producer");
		if (updated)
		{
			system.NotifyExecuted(2);
		}
		system.EndFrame(false, { RHIFenceHandle{ 1,1 },12 });
		context.Check(!system.GetDiagnostics().m_Available && system.GetDiagnostics().m_Generations[2] == 1 &&
			pool.GetDiagnostics().m_PendingRetirementTextureCount == 3 && device.m_AtmosphereDestroyedBufferCount == 0,
			"Cancelled submitted atmosphere work publishes no generation and retains textures until its fence");
		device.m_CompletedFenceValue = 12;
		pool.Tick();
		context.Check(device.m_AtmosphereDestroyedBufferCount == 0 &&
			pool.GetDiagnostics().m_PendingRetirementTextureCount == 0,
			"Atmosphere textures retire after GPU completion without private constants buffers");
		const bool retry = system.Begin(a, { 1,1,1 });
		if (retry)
		{
			system.NotifyExecuted(0);
		}
		system.EndFrame(false, {});
		device.m_CompletedFenceValue = 12;
		system.Shutdown();
		pool.Tick();
		context.Check(pool.GetDiagnostics().m_ActiveTextureCount == 0 &&
			device.m_AtmosphereDestroyedBufferCount == 0,
			"Unsubmitted atmosphere cancellation leaves no active LUT or private constants resources");

		// Exercise the actual inspector consumer between LUT production and final export.
		// A producer-only graph cannot detect reads declared after an early Export.
		device.m_CreateValidDescriptors = true;
		TransientResourcePool previewPool(&device);
		SamplerRegistry previewSamplers({ .m_Device = &device });
		RenderResourceRegistry registry({ .m_Device = &device,
			.m_TransientResourcePool = &previewPool, .m_SamplerRegistry = &previewSamplers });
		class PresentationAccess final : public RenderPresentationAccess
		{
		public:
			RHIContext* GetRHIContext() const noexcept override { return &m_Context; }
			RHIDevice* GetDevice() const noexcept override { return nullptr; }
			RHISwapChain* GetSwapChain() const noexcept override { return nullptr; }
			const std::array<float, 4>& GetBackBufferClearColor() const noexcept override { return m_Clear; }
			RHIFencePoint GetLastSubmittedFencePoint() const noexcept override { return {}; }
		private:
			mutable BindingTestRHIContext m_Context;
			std::array<float, 4> m_Clear{};
		} presentation;
		AtmosphereSystem previewAtmosphere(&device, &pool);
		auto previewServices = services;
		previewServices.m_Resources = &registry;
		previewServices.m_Presentation = &presentation;
		previewServices.m_Atmosphere = &previewAtmosphere;
		frame.m_RenderViews[0].m_Width = 1024;
		frame.m_RenderViews[0].m_Height = 512;
		const std::array previewTaps{ PostProcessDebugTap::AtmosphereTransmittance,
			PostProcessDebugTap::AtmosphereMultipleScattering, PostProcessDebugTap::AtmosphereSkyView };
		uint64_t fenceValue = 20;
		for (size_t tap = 0; tap < previewTaps.size(); ++tap)
		{
			frame.m_RenderViews[0].m_CameraPosition = Vector3::Zero;
			for (uint32_t phase = 0; phase < 3; ++phase)
			{
				if (phase == 2)
				{
					frame.m_RenderViews[0].m_CameraPosition = Vector3(0.0f, 1000.0f, 0.0f);
				}
				RenderGraph previewGraph({ .m_Device = &device, .m_TransientResourcePool = &previewPool });
				const auto previewContext = frame.MakeRenderFrameContext();
				pass.AddPass(previewGraph, previewContext, previewServices);
				const uint32_t expectedMask = phase == 0 ? 7u : phase == 1 ? 0u : 4u;
				const bool expectedUpdate = previewAtmosphere.GetDiagnostics().m_DirtyMask == expectedMask;
				registry.SetPostProcessPreviewSelection({ previewTaps[tap], 0 }, PostProcessPreviewChannel::Atmosphere);
				registry.RequestPostProcessPreview(PostProcessPreviewChannel::Atmosphere);
				RenderPassPostProcessPreview preview;
				preview.AddPass(previewGraph, previewContext, previewServices);
				pass.AddFinishPass(previewGraph);
				const bool previewCompiled = previewGraph.Compile();
				RGSnapshot previewSnapshot;
				BuildRenderGraphSnapshot(previewGraph, previewSnapshot);
				const auto consumer = std::ranges::find(previewSnapshot.m_Passes, "Atmosphere.Preview", &RGSnapshotPassInfo::m_Name);
				const auto finish = std::ranges::find(previewSnapshot.m_Passes, "Atmosphere.Export", &RGSnapshotPassInfo::m_Name);
				bool valid = previewCompiled && expectedUpdate && consumer != previewSnapshot.m_Passes.end() &&
					finish != previewSnapshot.m_Passes.end() && !consumer->m_Culled && !finish->m_Culled &&
					consumer->m_ExecutionOrder < finish->m_ExecutionOrder;
				valid &= std::ranges::any_of(previewSnapshot.m_DependencyEdges,
					[tap](const RGSnapshotDependencyEdge& edge)
					{
						return edge.m_FromPassName == "Atmosphere.Preview" && edge.m_ToPassName == "Atmosphere.Export" &&
							edge.m_ResourceName == AtmosphereLutNames[tap] && edge.m_Reason == RGDependencyReason::ExportReaderToExport;
					});
				for (const auto name : AtmosphereLutNames)
				{
					const auto resource = std::ranges::find(previewSnapshot.m_Resources, name, &RGSnapshotResourceInfo::m_Name);
					valid &= resource != previewSnapshot.m_Resources.end() && resource->m_HasFinalBarrierState &&
						resource->m_FinalBarrierState == CommonRHIResourceState();
				}
				if (valid)
				{
					valid &= std::ranges::any_of(finish->m_PostBarriers,
						[tap](const RGSnapshotBarrierInfo& barrier)
						{
							return barrier.m_ResourceName == AtmosphereLutNames[tap] &&
								barrier.m_After == CommonRHIResourceState();
						});
				}
				context.Check(valid, std::format("Atmosphere preview {} reads before export with dirty mask {} and returns to Common",
					AtmosphereLutNames[tap], expectedMask));
				// Simulate a successful submission to exercise cache reuse in the next graph.
				for (uint32_t i = 0; i < 3; ++i)
				{
					if (expectedMask & (1u << i))
					{
						previewAtmosphere.NotifyExecuted(i);
					}
				}
				previewAtmosphere.EndFrame(previewCompiled, { RHIFenceHandle{ 1, 1 }, ++fenceValue });
			}
			previewAtmosphere.Disable();
		}
		frame.m_RenderScene.m_Atmosphere.reset();
		RenderGraph disabledGraph({ .m_Device = &device, .m_TransientResourcePool = &previewPool });
		const auto disabledContext = frame.MakeRenderFrameContext();
		pass.AddPass(disabledGraph, disabledContext, previewServices);
		registry.PublishPostProcessPreview({ previewTaps[0], 0 }, PostProcessPreviewChannel::Atmosphere, 42,
			registry.ObservePostProcessPreviewRequest(PostProcessPreviewChannel::Atmosphere));
		registry.SetPostProcessPreviewSelection({ previewTaps[0], 0 }, PostProcessPreviewChannel::Atmosphere);
		registry.RequestPostProcessPreview(PostProcessPreviewChannel::Atmosphere);
		RenderPassPostProcessPreview unavailablePreview;
		unavailablePreview.AddPass(disabledGraph, disabledContext, previewServices);
		pass.AddFinishPass(disabledGraph);
		RGSnapshot disabledSnapshot;
		BuildRenderGraphSnapshot(disabledGraph, disabledSnapshot);
		context.Check(disabledSnapshot.m_Passes.empty() &&
			!registry.HasPublishedPostProcessPreview(PostProcessPreviewChannel::Atmosphere),
			"Disabled atmosphere creates no LUT exports and invalidates the unavailable preview");

		class BakeEnvironmentAccess final : public RenderEnvironmentAccess
		{
		public:
			AtmosphereGPU m_Parameters{};
			EnvironmentLightingSettings m_Settings{};
			IBLBakeStatus m_Status{};
			EnvironmentTextureSource m_TextureSource{};
			const EnvironmentLightingSettings& GetEnvironmentLightingSettings() const noexcept override { return m_Settings; }
			bool ShouldInitializeBakeResources() const noexcept override { return false; }
			uint64_t GetBakingGeneration() const noexcept override { return 1; }
			const IBLBakeConfig& GetBakingConfig() const noexcept override { return m_Settings.m_BakeConfig; }
			const IBLBakeStatus& GetBakingStatus() const noexcept override { return m_Status; }
			uint64_t GetBakeRequestGeneration() const noexcept override { return m_Status.m_RequestedGeneration; }
			IBLBakeStage GetStageForRecording() const noexcept override { return IBLBakeStage::Environment; }
			void NotifyStageExecuted(IBLBakeStage, uint64_t) noexcept override {}
			void NotifyBakeResourcesInitialized(uint64_t) noexcept override {}
			const EnvironmentTextureSource& GetBakingSource() const noexcept override { return m_TextureSource; }
			const EnvironmentTextureSource& GetCommittedEnvironmentSource() const noexcept override { return m_TextureSource; }
			ArtifactCacheCoreStatistics GetArtifactCacheStatistics() const noexcept override { return {}; }
			LocalDerivedDataStoreStatistics GetDerivedDataStoreStatistics() const noexcept override { return {}; }
			const AtmosphereGPU* GetBakingAtmosphereParameters() const noexcept override { return &m_Parameters; }
		} bakeEnvironment;
		bakeEnvironment.m_Parameters = a;
		registry.EnsureIBLBakeResources(bakeEnvironment.m_Settings.m_BakeConfig);
		AtmosphereSystem bakeAtmosphere(&device, &pool);
		auto bakeServices = services;
		bakeServices.m_Environment = &bakeEnvironment;
		bakeServices.m_BakeAtmosphere = &bakeAtmosphere;
		bakeServices.m_Resources = &registry;
		RenderGraph bakeGraph({ .m_Device = &device, .m_TransientResourcePool = &previewPool });
		struct BakeImportData {};
		bakeGraph.AddPass<BakeImportData>("IBL.Import.Contract", [&registry](RenderGraph::RGBuilder& builder, BakeImportData&)
			{
				auto& resources = builder.GetBlackboard().Create<RGIBLResources>(IBLResourcesName);
				resources.m_BakeEnvironmentCubemap = builder.ImportTexture("IBL.Bake.Environment",
					registry.GetIBLBakeTextureHandle(RenderTextureIndex::IBL_EnvironmentCubemap),
					*registry.GetIBLBakeTextureDesc(RenderTextureIndex::IBL_EnvironmentCubemap),
					UndefinedRHITextureState(), RGContentValidity::Undefined);
				resources.m_BakePrefilteredSpecularCubemap = builder.ImportTexture("IBL.Bake.Specular",
					registry.GetIBLBakeTextureHandle(RenderTextureIndex::IBL_PrefilteredSpecularCubemap),
					*registry.GetIBLBakeTextureDesc(RenderTextureIndex::IBL_PrefilteredSpecularCubemap),
					UndefinedRHITextureState(), RGContentValidity::Undefined);
			});
		pass.AddBakePass(bakeGraph, disabledContext, bakeServices);
		RenderPassIBLEnvironment environmentPass;
		environmentPass.AddPass(bakeGraph, disabledContext, bakeServices);
		RenderPassIBLEnvironmentMipChain environmentMipPass;
		environmentMipPass.AddPass(bakeGraph, disabledContext, bakeServices);
		RenderPassIBLPrefilteredSpecular specularPass;
		specularPass.AddPass(bakeGraph, disabledContext, bakeServices);
		pass.AddFinishPass(bakeGraph);
		const bool bakeCompiled = bakeGraph.Compile();
		RGSnapshot bakeSnapshot;
		BuildRenderGraphSnapshot(bakeGraph, bakeSnapshot);
		bool importanceDependenciesMatch = bakeCompiled;
		for (uint32_t mip = 0u; mip < 7u; ++mip)
		{
			const std::string from = mip == 0u ? "IBL.EnvironmentMipChain.9" : std::format("IBL.PrefilteredSpecular.Importance.{}", mip - 1u);
			const std::string to = std::format("IBL.PrefilteredSpecular.Importance.{}", mip);
			importanceDependenciesMatch &= std::ranges::any_of(bakeSnapshot.m_DependencyEdges,
				[&from, &to](const RGSnapshotDependencyEdge& edge)
				{
					return edge.m_FromPassName == from && edge.m_ToPassName == to;
				});
		}
		importanceDependenciesMatch &= std::ranges::any_of(bakeSnapshot.m_DependencyEdges,
			[](const RGSnapshotDependencyEdge& edge)
			{
				return edge.m_FromPassName == "IBL.PrefilteredSpecular.Importance.6" && edge.m_ToPassName == "IBL.PrefilteredSpecular";
			});
		context.Check(importanceDependenciesMatch,
			"Production specular importance mips depend on the baked environment and preceding mip before convolution");
		const auto importanceResource = std::ranges::find(bakeSnapshot.m_Resources, "IBL.SpecularImportance", &RGSnapshotResourceInfo::m_Name);
		const auto specularConsumer = std::ranges::find(bakeSnapshot.m_Passes, "IBL.PrefilteredSpecular", &RGSnapshotPassInfo::m_Name);
		context.Check(importanceResource != bakeSnapshot.m_Resources.end() &&
			specularConsumer != bakeSnapshot.m_Passes.end() && !importanceResource->m_Imported &&
			importanceResource->m_TextureFormat == RHIFormat::R32Float &&
			importanceResource->m_TextureExtent.m_Width == 64u &&
			importanceResource->m_LastUserPassIndex == static_cast<int32_t>(specularConsumer->m_Index),
			"Specular importance remains a RenderGraph transient retained through its last convolution consumer");
		context.Check(bakeCompiled && std::ranges::any_of(bakeSnapshot.m_DependencyEdges,
			[](const RGSnapshotDependencyEdge& edge)
			{
				return edge.m_FromPassName == BakeAtmosphereLutNames[2] && edge.m_ToPassName == "IBL.Environment";
			}), "Production procedural IBL depends on the pending sky LUT with no texture asset service");
		context.Check(std::ranges::any_of(bakeSnapshot.m_DependencyEdges,
			[](const RGSnapshotDependencyEdge& edge)
			{
				return edge.m_FromPassName == "IBL.Environment" && edge.m_ToPassName == "IBL.Atmosphere.Export" &&
					edge.m_Reason == RGDependencyReason::ExportReaderToExport;
			}), "Pending atmosphere export follows its IBL consumer and restores the persistent state contract");
		for (uint32_t i = 0; i < 3; ++i)
		{
			bakeAtmosphere.NotifyExecuted(i);
		}
		bakeAtmosphere.EndFrame(bakeCompiled, { RHIFenceHandle{ 1, 1 }, 100 });
		device.m_CompletedFenceValue = 99;
		context.Check(!bakeAtmosphere.CanPublish(), "A submitted physical sky remains unpublished until its GPU fence completes");
		device.m_CompletedFenceValue = 100;
		context.Check(bakeAtmosphere.CanPublish(), "GPU completion unlocks a complete pending atmosphere generation");
		AtmosphereSystem activeAtmosphere(&device, &pool);
		const bool activeBegun = activeAtmosphere.Begin(a, { 1,1,1 });
		if (activeBegun)
		{
			for (uint32_t i = 0; i < 3; ++i)
			{
				activeAtmosphere.NotifyExecuted(i);
			}
		}
		activeAtmosphere.EndFrame(activeBegun, { RHIFenceHandle{ 1, 1 }, 200 });
		const auto oldTexture = activeAtmosphere.GetTexture(0);
		const auto newTexture = bakeAtmosphere.GetTexture(0);
		activeAtmosphere.SwapPublished(bakeAtmosphere);
		bakeAtmosphere.Disable();
		pool.Tick();
		context.Check(activeAtmosphere.GetTexture(0) == newTexture && oldTexture != newTexture &&
			pool.GetDiagnostics().m_PendingRetirementTextureCount == 3,
			"Atomic LUT replacement retains the old set behind its last-use fence, independent of the bake fence");
		frame.m_RenderScene.m_Atmosphere = AtmosphereSettings{};
		frame.m_RenderSceneStatus = RenderSceneBuildStatus::Ready;
		frame.m_RenderViews[0].m_Width = 1024;
		frame.m_RenderViews[0].m_Height = 512;
		frame.m_RenderViews[0].m_FovRadians = 1.0f;
		frame.m_RenderViews[0].m_Aspect = 2.0f;
		frame.m_RenderViews[0].m_Far = 10000.0f;
		bakeEnvironment.m_Settings.m_EnableSkybox = true;
		bakeEnvironment.m_Settings.m_BackgroundMode = EnvironmentBackgroundMode::PhysicalAtmospherePreview;
		auto aerialServices = previewServices;
		aerialServices.m_Atmosphere = &activeAtmosphere;
		aerialServices.m_Environment = &bakeEnvironment;
		RenderGraph aerialGraph({ .m_Device = &device, .m_TransientResourcePool = &previewPool });
		struct AerialTargetData {};
		aerialGraph.AddPass<AerialTargetData>("AerialTest.Opaque", [](RenderGraph::RGBuilder& builder, AerialTargetData&)
			{
				builder.SideEffect();
				const RHIExtent3D extent{ 1024, 512, 1 };
				RHITextureDesc colorDesc{};
				colorDesc.m_Format = RHIFormat::R16G16B16A16Float;
				colorDesc.m_Extent = extent;
				auto& targets = builder.GetBlackboard().Create<RGViewTargetsTable>(ViewTargetsTableName).GetViewTargets(RenderViewID::Main);
				targets.m_RenderWidth = extent.m_Width;
				targets.m_RenderHeight = extent.m_Height;
				targets.m_DisplayWidth = extent.m_Width;
				targets.m_DisplayHeight = extent.m_Height;
				targets.m_SceneColor = builder.CreateTexture("AerialTest.SceneColor", colorDesc);
				builder.WriteInPlace(targets.m_SceneColor, RGTextureAccess::RenderTarget);
				RHITextureDesc depthDesc{};
				depthDesc.m_Format = RHIFormat::R32Typeless;
				depthDesc.m_Extent = extent;
				auto& depth = builder.GetBlackboard().Create<RGSceneDepthResources>(SceneDepthResourcesName);
				depth.m_Texture = builder.CreateTexture("AerialTest.Depth", depthDesc);
				depth.m_SrvDesc = MakeRHITexture2DViewDesc(RHIFormat::R32Float, 0, 1, RHITextureAspect::Depth);
				builder.WriteInPlace(depth.m_Texture, RGTextureAccess::DepthStencilWrite);
			});
		const auto aerialContext = frame.MakeRenderFrameContext();
		pass.AddPass(aerialGraph, aerialContext, aerialServices);
		registry.SetPostProcessPreviewSelection({ PostProcessDebugTap::AtmosphereAerialTransmittance, 0 }, PostProcessPreviewChannel::Atmosphere);
		registry.RequestPostProcessPreview(PostProcessPreviewChannel::Atmosphere);
		RenderPassAerialPerspective aerialPass;
		const auto baselineColor = aerialGraph.GetBlackboard().Get<RGViewTargetsTable>(
			ViewTargetsTableName).GetViewTargets(RenderViewID::Main).m_SceneColor;
		frame.m_ViewRenderSettings[0].m_Lighting.m_EnableAerialPerspective = false;
		aerialPass.AddPass(aerialGraph, aerialContext, aerialServices);
		context.Check(!aerialGraph.GetBlackboard().TryGet<RGAerialPerspectiveResources>(AerialPerspectiveResourcesName) &&
			aerialGraph.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName).
			GetViewTargets(RenderViewID::Main).m_SceneColor == baselineColor &&
			activeAtmosphere.GetTexture(0) == newTexture && bakeEnvironment.m_Settings.m_EnableSkybox,
			"Disabling aerial transport preserves surface scene color, active atmosphere and sky settings");
		frame.m_ViewRenderSettings[0].m_Lighting.m_EnableAerialPerspective = true;
		aerialPass.AddPass(aerialGraph, aerialContext, aerialServices);
		RenderPassPostProcessPreview aerialPreview;
		aerialPreview.AddPass(aerialGraph, aerialContext, aerialServices);
		aerialGraph.AddPass<AerialTargetData>("AerialTest.TransparentLoad", [](RenderGraph::RGBuilder& builder, AerialTargetData&)
			{
				builder.SideEffect();
				auto& targets = builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName).GetViewTargets(RenderViewID::Main);
				builder.ReadWriteInPlace(targets.m_SceneColor, RGTextureAccess::RenderTarget);
			});
		aerialGraph.AddPass<AerialTargetData>("AerialTest.Consumer", [](RenderGraph::RGBuilder& builder, AerialTargetData&)
			{
				builder.SideEffect();
				const auto& targets = builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName).GetViewTargets(RenderViewID::Main);
				builder.Read(targets.m_SceneColor, RGTextureAccess::Sample);
			});
		pass.AddFinishPass(aerialGraph);
		const bool aerialCompiled = aerialGraph.Compile();
		RGSnapshot aerialSnapshot;
		BuildRenderGraphSnapshot(aerialGraph, aerialSnapshot);
		const auto aerialEdge = [&aerialSnapshot](std::string_view from, std::string_view to) noexcept
		{
			return std::ranges::any_of(aerialSnapshot.m_DependencyEdges,
				[from, to](const RGSnapshotDependencyEdge& edge)
				{
					return edge.m_FromPassName == from && edge.m_ToPassName == to;
				});
		};
		context.Check(aerialCompiled, "Aerial opaque graph compiles with sampled depth and LUT inputs");
		const auto aerialBuild = std::ranges::find(aerialSnapshot.m_Passes, "Atmosphere.AerialPerspective.Build", &RGSnapshotPassInfo::m_Name);
		context.Check(aerialBuild != aerialSnapshot.m_Passes.end() && !aerialBuild->m_Culled &&
			std::ranges::any_of(aerialBuild->m_Accesses,
				[](const RGSnapshotAccessInfo& access)
				{
					return access.m_ResourceName == AtmosphereLutNames[0] &&
						access.m_DependencyAccess == RGDependencyAccess::Read;
				}) &&
			std::ranges::any_of(aerialBuild->m_Accesses,
				[](const RGSnapshotAccessInfo& access)
				{
					return access.m_ResourceName == AtmosphereLutNames[1] &&
						access.m_DependencyAccess == RGDependencyAccess::Read;
				}), "Aerial froxel build samples both active atmosphere transport LUTs");
		context.Check(aerialEdge("Atmosphere.AerialPerspective.Build", "Atmosphere.AerialPerspective.Composite"),
			"Aerial composite reads the froxel output");
		context.Check(aerialEdge("Atmosphere.AerialPerspective.Composite", "AerialTest.TransparentLoad") &&
			aerialEdge("AerialTest.TransparentLoad", "AerialTest.Consumer"),
			"Downstream rendering loads aerial scene color before final sampled consumption");
		context.Check(aerialEdge("Atmosphere.AerialPerspective.Composite", "Atmosphere.Preview"),
			"Aerial diagnostic preview reads the current composite result");
		const auto aerialInitialize = std::ranges::find(aerialSnapshot.m_Passes,
			"Atmosphere.AerialPerspective.InitializeSceneColor", &RGSnapshotPassInfo::m_Name);
		const auto aerialComposite = std::ranges::find(aerialSnapshot.m_Passes,
			"Atmosphere.AerialPerspective.Composite", &RGSnapshotPassInfo::m_Name);
		const auto aerialTransparent = std::ranges::find(aerialSnapshot.m_Passes, "AerialTest.TransparentLoad", &RGSnapshotPassInfo::m_Name);
		const auto aerialColor = std::ranges::find(aerialSnapshot.m_Resources, "Atmosphere.AerialSceneColor", &RGSnapshotResourceInfo::m_Name);
		context.Check(aerialInitialize != aerialSnapshot.m_Passes.end() &&
			aerialComposite != aerialSnapshot.m_Passes.end() &&
			aerialTransparent != aerialSnapshot.m_Passes.end() &&
			!aerialInitialize->m_Culled && aerialInitialize->m_SideEffect &&
			aerialInitialize->m_EncoderType == RGPassEncoderType::Graphics &&
			aerialInitialize->m_ExecutionOrder >= 0 &&
			aerialInitialize->m_ExecutionOrder < aerialComposite->m_ExecutionOrder &&
			aerialComposite->m_ExecutionOrder < aerialTransparent->m_ExecutionOrder &&
			aerialEdge("Atmosphere.AerialPerspective.InitializeSceneColor",
				"Atmosphere.AerialPerspective.Composite"),
			"Physical aerial color initialization survives full-write culling and precedes compute and rendering");
		const auto aerialColorUsage = RHITextureUsage::RenderTarget |
			RHITextureUsage::UnorderedAccess | RHITextureUsage::Sampled;
		context.Check(aerialColor != aerialSnapshot.m_Resources.end() &&
			aerialInitialize != aerialSnapshot.m_Passes.end() &&
			aerialColor->m_FirstUserPassIndex == static_cast<int32_t>(aerialInitialize->m_Index) &&
			aerialColor->m_UsageBits == static_cast<uint64_t>(aerialColorUsage),
			"Aerial scene color is allocated at its clear pass with combined RTV, UAV and SRV usage");
		const auto aerialColorTransition = [&aerialSnapshot](std::string_view passName,
			const RHIResourceState& before, const RHIResourceState& after) noexcept
		{
			const auto passInfo = std::ranges::find(aerialSnapshot.m_Passes, passName, &RGSnapshotPassInfo::m_Name);
			return passInfo != aerialSnapshot.m_Passes.end() &&
				std::ranges::any_of(passInfo->m_PreBarriers,
					[&](const RGSnapshotBarrierInfo& barrier)
					{
						return barrier.m_ResourceName == "Atmosphere.AerialSceneColor" &&
							barrier.m_Kind == RGBarrierKind::Transition &&
							barrier.m_Before == before && barrier.m_After == after;
					});
		};
		const auto aerialRenderTargetState = ToRHIResourceState(RGTextureAccess::RenderTarget);
		const auto aerialStorageState = ToRHIResourceState(RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
		context.Check(aerialColorTransition("Atmosphere.AerialPerspective.InitializeSceneColor",
			UndefinedRHITextureState(), aerialRenderTargetState) &&
			aerialColorTransition("Atmosphere.AerialPerspective.Composite", aerialRenderTargetState, aerialStorageState) &&
			aerialColorTransition("AerialTest.TransparentLoad", aerialStorageState, aerialRenderTargetState),
			"RenderGraph orders aerial initialization, compute writes and render target loads with exact transitions");
		activeAtmosphere.EndFrame(false, {});
		device.m_CompletedFenceValue = 200;
		activeAtmosphere.Shutdown();
		bakeAtmosphere.Shutdown();
		device.m_CompletedFenceValue = std::max(fenceValue, uint64_t{ 200 });
		registry.ReleaseAll({ RHIFenceHandle{ 1, 1 }, fenceValue });
		previewAtmosphere.Shutdown();
		previewPool.Tick();
		pool.Tick();
		uploadBatch->Detach(writer);
		uploadBatch->Cancel();

		AtmosphereSystem snapshotLive(&device, &pool);
		AtmosphereSystem snapshotBake(&device, &pool);
		BakeEnvironmentAccess snapshotEnvironment;
		snapshotEnvironment.m_Parameters = a;
		snapshotEnvironment.m_Parameters.m_Sun.m_X *= 0.5f;
		auto snapshotServices = services;
		snapshotServices.m_Atmosphere = &snapshotLive;
		snapshotServices.m_BakeAtmosphere = &snapshotBake;
		snapshotServices.m_Environment = &snapshotEnvironment;
		RenderPassAtmosphere snapshotPass;
		uploadBatch = uploads.BeginBatch();
		uploadBatch->Attach(writer);
		frame.m_Uploads = writer.GetFrameUploads();
		const auto snapshotContext = frame.MakeRenderFrameContext();
		RenderGraph snapshotGraph({ .m_Device = &device, .m_TransientResourcePool = &previewPool });
		snapshotPass.AddPass(snapshotGraph, snapshotContext, snapshotServices);
		snapshotPass.AddBakePass(snapshotGraph, snapshotContext, snapshotServices);
		snapshotPass.AddFinishPass(snapshotGraph);
		const auto liveSnapshot = snapshotGraph.GetBlackboard().Get<RGAtmosphereResources>(
			AtmosphereResourcesName);
		const auto bakeSnapshotResources = snapshotGraph.GetBlackboard().Get<RGAtmosphereResources>(
			BakeAtmosphereResourcesName);
		const auto matchesPayload = [&device](const RGAtmosphereResources& resources)
			{
				const auto& binding = resources.m_Constants;
				return binding.IsValid() && binding.m_SizeInBytes == sizeof(AtmosphereGPU) &&
					std::memcmp(device.m_PassConstantStorage[binding.m_Buffer.Index() - 1].data() +
						binding.m_OffsetInBytes, &resources.m_Diagnostics.m_Parameters,
						sizeof(AtmosphereGPU)) == 0;
			};
		context.Check(writer.Owns(liveSnapshot.m_Constants) && writer.Owns(bakeSnapshotResources.m_Constants) &&
			liveSnapshot.m_Constants.m_Buffer == bakeSnapshotResources.m_Constants.m_Buffer &&
			liveSnapshot.m_Constants.m_OffsetInBytes != bakeSnapshotResources.m_Constants.m_OffsetInBytes &&
			matchesPayload(liveSnapshot) && matchesPayload(bakeSnapshotResources),
			"Live and bake atmosphere snapshots have independent exact payloads in the same frame batch");
		// Mutating the producer after composition must not change either recorded snapshot.
		snapshotEnvironment.m_Parameters = {};
		RecordingGraphicsCommandContext snapshotGraphics;
		RecordingComputeCommandContext snapshotCompute;
		snapshotGraphics.BeginRecordingScope(writer);
		snapshotCompute.BeginRecordingScope(writer);
		const bool snapshotCompiled = snapshotGraph.Compile();
		if (snapshotCompiled)
		{
			RGDeferredCommitList executeCommits;
			RGExecuteContext execute(executeCommits, { &snapshotGraphics, &snapshotCompute, nullptr });
			snapshotGraph.Execute(execute);
			executeCommits.Discard();
		}
		const auto atmosphereContract = scene_bindings::ComposeAtmosphereLayout(
			scene_bindings::GetCommonLayoutContract());
		const auto atmosphereParameter = atmosphereContract.IsSuccess() ?
			atmosphereContract.m_Layout->FindParameterIndex(scene_bindings::AtmosphereConstants) :
			std::nullopt;
		const auto countBindings = [&snapshotCompute, atmosphereParameter](const RHIConstantBufferBinding& expected)
			{
				return std::ranges::count_if(snapshotCompute.m_ConstantBindings,
					[&expected, atmosphereParameter](const RecordedConstantBinding& binding)
					{
						return atmosphereParameter && binding.m_ParameterIndex == *atmosphereParameter &&
							binding.m_Buffer == expected.m_Buffer && binding.m_Offset == expected.m_OffsetInBytes;
					});
			};
		context.Check(snapshotCompiled && countBindings(liveSnapshot.m_Constants) == 3 &&
			countBindings(bakeSnapshotResources.m_Constants) == 3 && matchesPayload(bakeSnapshotResources) &&
			!snapshotCompute.HasRecordingError(),
			"Every live and bake LUT stage binds its composition snapshot without re-uploading or observing later edits");
		snapshotGraphics.EndRecordingScope();
		snapshotCompute.EndRecordingScope();
		uploadBatch->Detach(writer);
		const RHIFencePoint snapshotFence{ RHIFenceHandle{ 1, 1 }, 300 };
		const bool published = uploadBatch->Publish();
		uploadBatch->RecordAcceptedSubmission();
		const bool submitted = uploadBatch->Submitted(snapshotFence);
		snapshotLive.EndFrame(snapshotCompiled, snapshotFence);
		snapshotBake.EndFrame(snapshotCompiled, snapshotFence);
		context.Check(published && submitted && uploads.GetStatistics().m_LastBatch.m_UploadCount == 8 &&
			device.m_UploadBufferDescs.size() == 1 &&
			Test(device.m_UploadBufferDescs[0].m_CreateFlags, RHIBufferCreateFlags::UploadStorage),
			"Two atmosphere instances upload once each plus six PassCBs, with no private constants allocation");
		device.m_CompletedFenceValue = 300;
		uploads.ReclaimCompleted();

		// Cached LUTs still need fresh constants for sky and aerial consumers each frame.
		snapshotEnvironment.m_Parameters = bakeSnapshotResources.m_Diagnostics.m_Parameters;
		uploadBatch = uploads.BeginBatch();
		uploadBatch->Attach(writer);
		frame.m_Uploads = writer.GetFrameUploads();
		const auto cachedContext = frame.MakeRenderFrameContext();
		RenderGraph cachedGraph({ .m_Device = &device, .m_TransientResourcePool = &previewPool });
		snapshotPass.AddPass(cachedGraph, cachedContext, snapshotServices);
		snapshotPass.AddBakePass(cachedGraph, cachedContext, snapshotServices);
		const auto& cachedLive = cachedGraph.GetBlackboard().Get<RGAtmosphereResources>(AtmosphereResourcesName);
		context.Check(cachedLive.m_Constants.m_UploadBatchId != liveSnapshot.m_Constants.m_UploadBatchId &&
			writer.Owns(cachedLive.m_Constants) && matchesPayload(cachedLive) &&
			snapshotLive.GetDiagnostics().m_DirtyMask == 0 && snapshotBake.GetDiagnostics().m_DirtyMask == 0,
			"Cached live and bake LUTs receive current-frame constants without scheduling new LUT producers");
		snapshotLive.EndFrame(true, snapshotFence);
		snapshotBake.EndFrame(true, snapshotFence);
		uploadBatch->Detach(writer);
		uploadBatch->Cancel();
		context.Check(uploads.GetStatistics().m_LastBatch.m_UploadCount == 2,
			"Cached atmosphere composition uploads exactly one constants snapshot per instance");

		const auto& cachedForward = cachedGraph.GetBlackboard().Get<RGForwardAtmosphereConstants>();
		context.Check(cachedForward.m_AttenuationEnabled &&
			cachedForward.m_Snapshot.GetBinding().m_Buffer == cachedLive.m_Constants.m_Buffer &&
			cachedForward.m_Snapshot.GetBinding().m_OffsetInBytes == cachedLive.m_Constants.m_OffsetInBytes &&
			cachedForward.m_Snapshot.GetBinding().m_UploadBatchId == cachedLive.m_Constants.m_UploadBatchId,
			"Forward reuses the live current-frame snapshot even when all LUTs are clean");
		const auto neutral = MakeNeutralAtmosphere();
		bool neutralFinite = true;
		const auto neutralWords = std::bit_cast<std::array<float, sizeof(AtmosphereGPU) / sizeof(float)>>(neutral);
		for (const auto value : neutralWords)
		{
			neutralFinite &= std::isfinite(value);
		}
		const auto neutralTransmittance = EvaluateAtmosphereTransmittance(neutral, 1000.0f, 1.0f, 10000.0f);
		context.Check(neutralFinite && neutral.m_Radii.m_X > 0.0f &&
			neutral.m_Radii.m_Y > neutral.m_Radii.m_X && neutral.m_Rayleigh.m_W > 0.0f &&
			neutral.m_Mie.m_Z > 0.0f && neutral.m_Ground.m_W > 0.0f &&
			neutralTransmittance.m_X == 1.0f && neutralTransmittance.m_Y == 1.0f &&
			neutralTransmittance.m_Z == 1.0f,
			"Disabled attenuation has finite vacuum coefficients with valid helper denominators");
		uploadBatch = uploads.BeginBatch();
		uploadBatch->Attach(writer);
		frame.m_Uploads = writer.GetFrameUploads();
		const auto authoredAtmosphere = frame.m_RenderScene.m_Atmosphere;
		frame.m_RenderScene.m_Atmosphere.reset();
		auto neutralServices = snapshotServices;
		neutralServices.m_Atmosphere = nullptr;
		RenderGraph neutralGraph({ .m_Device = &device, .m_TransientResourcePool = &previewPool });
		snapshotPass.AddPass(neutralGraph, frame.MakeRenderFrameContext(), neutralServices);
		const auto& disabledForward = neutralGraph.GetBlackboard().Get<RGForwardAtmosphereConstants>();
		context.Check(!disabledForward.m_AttenuationEnabled && disabledForward.m_Snapshot.IsValid() &&
			writer.Owns(disabledForward.m_Snapshot.GetBinding()) &&
			!neutralGraph.GetBlackboard().TryGet<RGAtmosphereResources>(AtmosphereResourcesName),
			"Disabled Forward publishes one neutral frame snapshot without live or bake substitution");
		uploadBatch->Detach(writer);
		uploadBatch->Cancel();
		context.Check(uploads.GetStatistics().m_LastBatch.m_UploadCount == 1,
			"Every disabled frame uploads exactly one shared neutral snapshot");
		frame.m_RenderScene.m_Atmosphere = authoredAtmosphere;
		uploadBatch = uploads.BeginBatch();
		uploadBatch->Attach(writer);
		frame.m_Uploads = writer.GetFrameUploads();
		auto unavailableServices = snapshotServices;
		unavailableServices.m_Atmosphere = nullptr;
		RenderGraph unavailableGraph({ .m_Device = &device, .m_TransientResourcePool = &previewPool });
		snapshotPass.AddPass(unavailableGraph, frame.MakeRenderFrameContext(), unavailableServices);
		const auto& unavailable = unavailableGraph.GetBlackboard().Get<RGForwardAtmosphereConstants>();
		context.Check(unavailable.m_AttenuationEnabled && !unavailable.m_Snapshot.IsValid(),
			"Enabled attenuation with missing live data never receives a neutral or bake fallback");
		RecordingGraphicsCommandContext missingGraphics;
		RecordingComputeCommandContext missingCompute;
		missingGraphics.BeginRecordingScope(writer);
		missingCompute.BeginRecordingScope(writer);
		const bool missingCompiled = unavailableGraph.Compile();
		if (missingCompiled)
		{
			RGDeferredCommitList executeCommits;
			RGExecuteContext execute(executeCommits, { &missingGraphics, &missingCompute, nullptr });
			unavailableGraph.Execute(execute);
			executeCommits.Discard();
		}
		context.Check(missingCompiled && missingGraphics.HasRecordingError() && uploadBatch->HasFailed(),
			"Missing enabled live data fails the frame even when no Forward draw is emitted");
		missingGraphics.EndRecordingScope();
		missingCompute.EndRecordingScope();
		uploadBatch->Detach(writer);
		uploadBatch->Cancel();
		const auto validTexture = snapshotLive.GetTexture(0);
		const auto validBakeTexture = snapshotBake.GetTexture(0);
		const auto validGenerations = snapshotLive.GetDiagnostics().m_Generations;
		const auto validBakeGenerations = snapshotBake.GetDiagnostics().m_Generations;
		const auto limits = device.m_UploadLimits;
		device.m_UploadLimits.m_MaxConstantRangeInBytes = sizeof(AtmosphereGPU) - 4;
		uploadBatch = uploads.BeginBatch();
		uploadBatch->Attach(writer);
		frame.m_Uploads = writer.GetFrameUploads();
		RenderGraph failedGraph({ .m_Device = &device, .m_TransientResourcePool = &previewPool });
		const auto failedContext = frame.MakeRenderFrameContext();
		snapshotPass.AddPass(failedGraph, failedContext, snapshotServices);
		snapshotPass.AddBakePass(failedGraph, failedContext, snapshotServices);
		context.Check(uploadBatch->HasFailed() &&
			!failedGraph.GetBlackboard().TryGet<RGAtmosphereResources>(AtmosphereResourcesName) &&
			!failedGraph.GetBlackboard().TryGet<RGAtmosphereResources>(BakeAtmosphereResourcesName) &&
			snapshotLive.GetTexture(0) == validTexture && snapshotLive.IsInitialized(0) &&
			snapshotBake.GetTexture(0) == validBakeTexture && snapshotBake.IsInitialized(0) &&
			snapshotLive.GetDiagnostics().m_Generations == validGenerations &&
			snapshotBake.GetDiagnostics().m_Generations == validBakeGenerations && snapshotBake.CanPublish(),
			"Upload failure fails the frame before Begin and preserves both valid atmosphere instances");
		uploadBatch->Detach(writer);
		uploadBatch->Cancel();
		device.m_UploadLimits = limits;

		uploadBatch = uploads.BeginBatch();
		uploadBatch->Attach(writer);
		frame.m_Uploads = writer.GetFrameUploads();
		frame.m_RenderViews[0].m_CameraPosition += Vector3(0.0f, 1000.0f, 0.0f);
		RenderGraph unexecutedGraph({ .m_Device = &device, .m_TransientResourcePool = &previewPool });
		snapshotPass.AddPass(unexecutedGraph, frame.MakeRenderFrameContext(), snapshotServices);
		const bool unexecutedCompiled = unexecutedGraph.Compile();
		RGSnapshot unexecutedSnapshot;
		BuildRenderGraphSnapshot(unexecutedGraph, unexecutedSnapshot);
		context.Check(unexecutedCompiled && std::ranges::any_of(unexecutedSnapshot.m_Passes,
			[](const RGSnapshotPassInfo& info)
			{
				return info.m_Name == AtmosphereLutNames[2] && info.m_SideEffect && !info.m_Culled;
			}), "Imported atmosphere LUT writes remain live even without an export or sky consumer");
		// Do not execute: composition and even a frame fence cannot certify unwritten LUTs.
		snapshotLive.EndFrame(true, snapshotFence);
		context.Check(!snapshotLive.GetDiagnostics().m_Available &&
			snapshotLive.GetDiagnostics().m_Generations == validGenerations,
			"Unexecuted LUT work cannot publish a generation even when the surrounding frame submits");
		uploadBatch->Detach(writer);
		uploadBatch->Cancel();
		snapshotLive.Shutdown();
		snapshotBake.Shutdown();
		pool.Tick();
	}

	void RunWorldSunContractTests(SelfTestContext& context) noexcept
	{
		const auto luminance = [](const Vector3& rgb)
		{
			return 0.2126 * rgb.m_X + 0.7152 * rgb.m_Y + 0.0722 * rgb.m_Z;
		};
		WorldSunSettings settings{};
		settings.m_Chromaticity = Vector3(1.0f, 0.8f, 0.6f);
		const auto sun = ResolveWorldSun(settings, Vector3(0.0f, -2.0f, 0.0f));
		context.Check(std::abs(luminance(sun.m_Chromaticity) - 1.0) < 1.0e-6 &&
			sun.m_Direction.m_Y == -1.0f && sun.m_PerpendicularIlluminanceLux == 120000.0f,
			"World sun resolves unit direction and luminance-normalized chromaticity independently of exposure");
		bool integratedLuxMatches = true;
		for (float radius : { 0.01f, 0.2666f, 1.0f, 5.0f })
		{
			settings.m_AngularRadiusDegrees = radius;
			const auto disk = ResolveWorldSun(settings, -Vector3::UnitY);
			double projectedIntegral = 0.0;
			constexpr int samples = 4096;
			const double step = disk.m_AngularRadiusRadians / static_cast<double>(samples);
			for (int i = 0; i < samples; ++i)
			{
				const double theta = (i + 0.5) * step;
				projectedIntegral += std::cos(theta) * std::sin(theta) * step * 2.0 * std::numbers::pi;
			}
			const double integratedLux = luminance(disk.m_DiskRadiance) * projectedIntegral;
			integratedLuxMatches &= std::abs(integratedLux / settings.m_PerpendicularIlluminanceLux - 1.0) < 1.0e-5;
		}
		context.Check(integratedLuxMatches,
			"Numerical RGB solar disk integration recovers configured perpendicular lux across angular radii");
		const auto whiteSun = ResolveWorldSun(WorldSunSettings{}, -Vector3::UnitY);
		components::TransformComponent transform{};
		components::LightComponent light{};
		light.m_Color = Color::Red;
		light.m_Intensity = 3.0f;
		RenderDirectionalLight selected{};
		selected.m_EntityKey = 42;
		selected.m_Direction = sun.m_Direction;
		selected.m_WorldSun = sun;
		const auto physicalGpu = RenderSceneBuilder::BuildLightData(42, transform, light, selected);
		const auto legacyGpu = RenderSceneBuilder::BuildLightData(43, transform, light, selected);
		light.m_Type = LightType::Point;
		const auto localGpu = RenderSceneBuilder::BuildLightData(42, transform, light, selected);
		context.Check(physicalGpu.Intensity == 120000.0f && physicalGpu.Direction.m_Y == -1.0f &&
			physicalGpu.Color.m_Y == sun.m_Chromaticity.m_Y && legacyGpu.Intensity == 3.0f &&
			legacyGpu.Color.m_Y == 0.0f && localGpu.Intensity == 3.0f && localGpu.Color.m_Y == 0.0f,
			"GPU light encoding applies physical units only to the designated directional sun");
		const float radiance = whiteSun.m_DirectIlluminance.m_X * 0.18f / std::numbers::pi_v<float>;
		const auto ev15 = ResolveManualExposureSettings(15.0f, 0.0f, true);
		const auto ev16 = ResolveManualExposureSettings(16.0f, 0.0f, true);
		context.Check(std::abs(radiance - 6875.4935f) < 0.01f &&
			std::abs(radiance * ev15.m_ExposureScale - 2.0f * radiance * ev16.m_ExposureScale) < 1.0e-6f,
			"Neutral 18 percent Lambert reference is rho E over pi and one exposure stop halves exposed radiance");
		WorldSunSettings invalid{};
		invalid.m_PerpendicularIlluminanceLux = std::numeric_limits<float>::infinity();
		invalid.m_Chromaticity = Vector3::Zero;
		invalid.m_AngularRadiusDegrees = std::numeric_limits<float>::quiet_NaN();
		const auto sanitized = ResolveWorldSun(invalid, Vector3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f));
		context.Check(sanitized.m_PerpendicularIlluminanceLux == 0.0f &&
			sanitized.m_Direction.m_Y == -1.0f && sanitized.m_Chromaticity.m_X == 1.0f &&
			std::isfinite(sanitized.m_DiskRadiance.m_X) && sanitized.m_ProjectedSolidAngle > 0.0f,
			"Invalid sun input resolves finite darkness, neutral chromaticity and a valid angular radius");
	}

	void RunViewSettingsPatchTests(SelfTestContext& context) noexcept
	{
		TemporalAASettings content;
		content.m_Enabled = true;
		content.m_DepthAbsoluteThreshold = 0.2f;
		content.m_DepthRelativeThreshold = 0.3f;
		content.m_HistoryRelaxation = 1.5f;
		context.Check(TemporalAASettingsPatch{}.IsEmpty() &&
			ApplyTemporalAASettingsPatch({}, content) == content,
			"An empty typed TAA patch retains the resolved content settings");
		const auto checkTAAField = [&](auto patchMember, auto settingsMember,
			auto value, auto expectedValue)
			{
				TemporalAASettingsPatch patch;
				patch.*patchMember = value;
				auto expected = content;
				expected.*settingsMember = expectedValue;
				context.Check(!patch.IsEmpty() &&
					ApplyTemporalAASettingsPatch(patch, content) == expected,
					"A typed TAA field resolves its value without changing inherited fields");
			};
		checkTAAField(&TemporalAASettingsPatch::m_Enabled, &TemporalAASettings::m_Enabled,
			false, false);
		struct FloatField
		{
			std::optional<float> TemporalAASettingsPatch::* m_Patch;
			float TemporalAASettings::* m_Settings;
			float m_Min;
			float m_Max;
		};
		const std::array floats
		{
			FloatField{ &TemporalAASettingsPatch::m_MaxHistoryFeedback, &TemporalAASettings::m_MaxHistoryFeedback, 0.0f, TemporalAAMaxHistoryFeedbackCeiling },
			FloatField{ &TemporalAASettingsPatch::m_DepthAbsoluteThreshold, &TemporalAASettings::m_DepthAbsoluteThreshold, 0.0f, TemporalAAMaxDepthThreshold },
			FloatField{ &TemporalAASettingsPatch::m_DepthRelativeThreshold, &TemporalAASettings::m_DepthRelativeThreshold, 0.0f, TemporalAAMaxDepthThreshold },
			FloatField{ &TemporalAASettingsPatch::m_VelocityWeightScale, &TemporalAASettings::m_VelocityWeightScale, 0.0f, TemporalAAMaxVelocityWeightScale },
			FloatField{ &TemporalAASettingsPatch::m_LuminanceWeightScale, &TemporalAASettings::m_LuminanceWeightScale, 0.0f, TemporalAAMaxLuminanceWeightScale },
			FloatField{ &TemporalAASettingsPatch::m_NeighborhoodClampExpansion, &TemporalAASettings::m_NeighborhoodClampExpansion, 0.0f, TemporalAAMaxNeighborhoodClampExpansion },
			FloatField{ &TemporalAASettingsPatch::m_HistoryRelaxation, &TemporalAASettings::m_HistoryRelaxation, 0.0f, TemporalAAMaxHistoryRelaxation },
			FloatField{ &TemporalAASettingsPatch::m_VarianceClipGamma, &TemporalAASettings::m_VarianceClipGamma, TemporalAAMinVarianceClipGamma, TemporalAAMaxVarianceClipGamma },
			FloatField{ &TemporalAASettingsPatch::m_TextureLodBiasOffset, &TemporalAASettings::m_TextureLodBiasOffset, TemporalAAMinTextureLodBiasOffset, TemporalAAMaxTextureLodBiasOffset },
		};
		const TemporalAASettings defaults;
		for (const auto& field : floats)
		{
			for (const float value : { 0.0f, -99.0f, 99.0f,
				std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
			{
				const float expected = std::isfinite(value) ?
					std::clamp(value, field.m_Min, field.m_Max) :
					defaults.*field.m_Settings;
				checkTAAField(field.m_Patch, field.m_Settings, value, expected);
			}
		}
		const auto checkTAAEnum = [&](auto patchMember, auto settingsMember, auto values)
			{
				for (const auto value : values)
				{
					checkTAAField(patchMember, settingsMember, value, value);
				}
				using T = std::remove_cvref_t<decltype(content.*settingsMember)>;
				checkTAAField(patchMember, settingsMember, static_cast<T>(255), defaults.*settingsMember);
			};
		checkTAAEnum(&TemporalAASettingsPatch::m_HistoryAccumulation,
			&TemporalAASettings::m_HistoryAccumulation,
			std::array{ TemporalAAHistoryAccumulation::CompatibilityAge, TemporalAAHistoryAccumulation::EffectiveSamples });
		checkTAAEnum(&TemporalAASettingsPatch::m_HistoryRectification,
			&TemporalAASettings::m_HistoryRectification,
			std::array{ TemporalAAHistoryRectification::MinMaxClamp, TemporalAAHistoryRectification::VarianceClip, TemporalAAHistoryRectification::BoundedVarianceClip });
		checkTAAEnum(&TemporalAASettingsPatch::m_HistoryFilter, &TemporalAASettings::m_HistoryFilter,
			std::array{ TemporalAAHistoryFilter::Bilinear, TemporalAAHistoryFilter::CatmullRomClamped });
		checkTAAEnum(&TemporalAASettingsPatch::m_CurrentFilter, &TemporalAASettings::m_CurrentFilter,
			std::array{ TemporalAACurrentFilter::Point, TemporalAACurrentFilter::Gaussian });
		checkTAAEnum(&TemporalAASettingsPatch::m_MotionSelection, &TemporalAASettings::m_MotionSelection,
			std::array{ TemporalAAMotionSelection::Center, TemporalAAMotionSelection::ClosestDepth });
		checkTAAEnum(&TemporalAASettingsPatch::m_PostTemporalView, &TemporalAASettings::m_PostTemporalView,
			std::array{ TemporalAAPostTemporalView::Jittered, TemporalAAPostTemporalView::Unjittered });
		checkTAAEnum(&TemporalAASettingsPatch::m_ResolutionPreset, &TemporalAASettings::m_ResolutionPreset,
			std::array{ TemporalAAResolutionPreset::Native, TemporalAAResolutionPreset::Quality });

		GTAOSettings gtao;
		gtao.m_Power = 2.0f;
		gtao.m_DenoiseRadius = 4;
		gtao.m_FinalAOFormatPreference = GTAOFinalAOFormatPreference::ForceR16Float;
		const auto patchedGTAO = ApplyGTAOSettingsPatch({ .m_Enabled = false,
			.m_Radius = 0.5f, .m_DirectionCount = 0, .m_StepCount = 999,
			.m_TemporalAccumulation = false, .m_TemporalMaxSamples = 999 }, gtao);
		context.Check(!patchedGTAO.m_Enabled && patchedGTAO.m_Radius == 0.5f &&
			patchedGTAO.m_FalloffStart == 0.5f && patchedGTAO.m_FalloffEnd == 0.5f &&
			patchedGTAO.m_DirectionCount == 1 && patchedGTAO.m_StepCount == GTAOMaxStepCount &&
			!patchedGTAO.m_TemporalAccumulation &&
			patchedGTAO.m_TemporalMaxSamples == GTAOMaxTemporalSamples &&
			patchedGTAO.m_Power == 2.0f && patchedGTAO.m_DenoiseRadius == 4 &&
			patchedGTAO.m_FinalAOFormatPreference == GTAOFinalAOFormatPreference::ForceR16Float,
			"Typed GTAO patches clamp sampling and dependent falloff while preserving unrelated fields");
		const auto minimumGTAO = ApplyGTAOSettingsPatch({ .m_Radius = -1.0f,
			.m_DirectionCount = 999, .m_StepCount = 0, .m_TemporalMaxSamples = 0 }, gtao);
		context.Check(minimumGTAO.m_Radius == GTAOMinRadius &&
			minimumGTAO.m_DirectionCount == GTAOMaxDirectionCount && minimumGTAO.m_StepCount == 1 &&
			minimumGTAO.m_TemporalMaxSamples == 1 &&
			ApplyGTAOSettingsPatch({ .m_Radius = 99.0f }, gtao).m_Radius == GTAOMaxRadius,
			"Typed GTAO patches preserve both sampling bounds and the radius limits");

		ResolvedViewRenderSettings view;
		view.m_TemporalAA = content;
		view.m_Lighting.m_GTAO = gtao;
		view.m_Lighting.m_EnableAerialPerspective = false;
		view.m_PostProcess.m_Bloom.m_Intensity = 0.2f;
		view.m_Exposure = ResolveManualExposureSettings(5.0f, 2.0f, true);
		ApplyViewRenderSettingsPatch({ .m_Exposure = { .m_CompensationEV = 0.0f } }, view, true);
		context.Check(view.m_Exposure.m_ManualEV100 == 5.0f && view.m_Exposure.m_CompensationEV == 0.0f &&
			view.m_Exposure.m_EffectiveEV100 == 5.0f &&
			NearlyEqual(view.m_Exposure.m_ExposureScale, 1.0f / (1.2f * 32.0f)) &&
			view.m_Exposure.m_PreExposure == view.m_Exposure.m_ExposureScale &&
			view.m_TemporalAA == content && view.m_Lighting.m_GTAO.m_Power == 2.0f &&
			!view.m_Lighting.m_EnableAerialPerspective && view.m_PostProcess.m_Bloom.m_Intensity == 0.2f,
			"A compensation-only patch derives exposure together and preserves the other view settings");
		view.m_Exposure = ResolveManualExposureSettings(5.0f, 2.0f, true);
		ApplyViewRenderSettingsPatch({ .m_Exposure = { .m_ManualEV100 = 0.0f } }, view, false);
		context.Check(view.m_Exposure.m_ManualEV100 == 0.0f && view.m_Exposure.m_CompensationEV == 2.0f &&
			view.m_Exposure.m_EffectiveEV100 == -2.0f && view.m_Exposure.m_PreExposure == 1.0f &&
			NearlyEqual(view.m_Exposure.m_ExposureScale, 4.0f / 1.2f),
			"A manual-only zero patch retains compensation and resolves disabled pre-exposure");
	}

	void RunExposureContractTests(SelfTestContext& context) noexcept
	{
		Camera camera(Camera::CreateInfo{});
		const ResolvedExposureSettings defaultExposure = ResolveViewRenderSettings(ViewRenderProfile{}, camera).m_Exposure;
		context.Check(NearlyEqual(defaultExposure.m_ExposureScale, 1.0f / ManualExposureSaturationNormalization, 0.000001f) &&
			defaultExposure.m_ManualEV100 == 0.0f &&
			defaultExposure.m_CompensationEV == 0.0f &&
			defaultExposure.m_PreExposure == defaultExposure.m_ExposureScale,
			"Zero manual EV100 uses saturation-normalized default exposure");

		bool compensationResponseMatches = true;
		for (float compensation : { -10.0f, -2.0f, 0.0f, 2.0f, 10.0f })
		{
			camera.SetExposureCompensationEV(compensation);
			const float exposureScale = ResolveViewRenderSettings(ViewRenderProfile{}, camera).m_Exposure.m_ExposureScale;
			compensationResponseMatches &= NearlyEqual(exposureScale / std::exp2(compensation), defaultExposure.m_ExposureScale, 0.000001f);
		}
		context.Check(compensationResponseMatches,
			"Exposure compensation retains its stop response at zero manual EV100");

		camera.SetManualEV100(16.0f);
		camera.SetExposureCompensationEV(0.0f);
		ViewRenderProfile preExposedProfile{};
		preExposedProfile.m_EnableScenePreExposure = true;
		const auto preExposedSettings = ResolveViewRenderSettings(preExposedProfile, camera);
		preExposedProfile.m_TemporalAA.m_Enabled = true;
		const auto temporalSettings = ResolveViewRenderSettings(preExposedProfile, camera);
		context.Check(preExposedSettings.m_Exposure.m_PreExposure == preExposedSettings.m_Exposure.m_ExposureScale &&
			preExposedSettings.m_Exposure.m_PreExposure < 0.0001f &&
			temporalSettings.m_Exposure.m_PreExposure == temporalSettings.m_Exposure.m_ExposureScale &&
			temporalSettings.m_Exposure.m_ExposureScale == preExposedSettings.m_Exposure.m_ExposureScale,
			"Scene pre-exposure remains enabled with a TAA request under the V2 ABI");
		const ResolvedTemporalFramePlan noTemporalFrame{};
		const RenderView preExposedView = RenderViewBuildTraits<RenderViewID::Main>::Build({
			.m_Camera = camera,
			.m_RenderSettings = preExposedSettings,
			.m_TemporalFramePlan = noTemporalFrame,
			.m_Resolution = ResolveNativeViewResolution({ 64, 64 }),
		});
		const auto upload = RenderSceneBuilder::BuildViewData(std::span<const RenderView>(&preExposedView, 1), {});
		context.Check(upload.m_Views.size() == 1 &&
			preExposedView.m_ScenePreExposure == preExposedSettings.m_Exposure.m_PreExposure &&
			upload.m_Views[0].ScenePreExposure == preExposedView.m_ScenePreExposure &&
			upload.m_Views[0].ExposureMultiplier == preExposedSettings.m_Exposure.m_ExposureScale,
			"The resolved view uploads one unchanged scene storage scale for every scene writer");

		const ResolvedExposureSettings base = ResolveManualExposureSettings(12.0f, 0.0f);
		const ResolvedExposureSettings plusStop = ResolveManualExposureSettings(13.0f, 0.0f);
		const ResolvedExposureSettings minusStop = ResolveManualExposureSettings(11.0f, 0.0f);
		const ResolvedExposureSettings plusCompensation = ResolveManualExposureSettings(12.0f, 1.0f);
		context.Check(NearlyEqual(plusStop.m_ExposureScale / base.m_ExposureScale, 0.5f) &&
			NearlyEqual(minusStop.m_ExposureScale / base.m_ExposureScale, 2.0f) &&
			NearlyEqual(plusCompensation.m_ExposureScale / base.m_ExposureScale, 2.0f) &&
			base.m_EffectiveEV100 == 12.0f &&
			plusCompensation.m_EffectiveEV100 == 11.0f,
			"Manual exposure and compensation move exposed-linear brightness by exact stops");

		const float nan = std::numeric_limits<float>::quiet_NaN();
		const ResolvedExposureSettings sanitized = ResolveManualExposureSettings(nan, std::numeric_limits<float>::infinity());
		const ResolvedExposureSettings clamped = ResolveManualExposureSettings(1000.0f, -1000.0f);
		camera.SetManualEV100(nan);
		context.Check(sanitized.m_ManualEV100 == 0.0f &&
			sanitized.m_CompensationEV == 0.0f &&
			std::isfinite(sanitized.m_ExposureScale) && sanitized.m_ExposureScale > 0.0f &&
			clamped.m_ManualEV100 == Camera::ClampManualEV100(1000.0f) &&
			clamped.m_CompensationEV == Camera::ClampExposureCompensationEV(-1000.0f) &&
			std::isfinite(clamped.m_ExposureScale) && clamped.m_ExposureScale > 0.0f &&
			camera.GetManualEV100() == 16.0f,
			"Exposure resolution clamps finite ranges and rejects non-finite camera input");

		TemporalViewHistory viewHistory;
		TemporalObjectHistory objectHistory;
		TemporalFrameTransaction disabledTransaction;
		disabledTransaction.Begin(viewHistory, objectHistory, {}, ResolveNativeViewResolution({ 64, 64 }), nullptr, 0.5f);
		const bool retainedDisabledScale = disabledTransaction.GetScenePreExposure() == 0.5f;
		disabledTransaction.Abort();
		context.Check(retainedDisabledScale &&
			disabledTransaction.GetState() == TemporalFrameTransactionState::Aborted,
			"A TAA-disabled frame can carry a non-unit storage scale without committing history");
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingLightingSelfTests(SelfTestContext& context) noexcept
	{
		RunEnvironmentLightingSettingsTests(context);
		RunEnvironmentLightingOverrideTests(context);
		RunLightingProfileContractTests(context);
		RunAtmosphereContractTests(context);
		RunWorldSunContractTests(context);
		RunViewSettingsPatchTests(context);
		RunExposureContractTests(context);
	}
}
