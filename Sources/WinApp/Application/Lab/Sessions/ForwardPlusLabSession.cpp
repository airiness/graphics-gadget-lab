#include "Application/Lab/Sessions/ForwardPlusLabSession.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"

#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/Camera.h"
#include "Graphics/Geometry.h"
#include "Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "GGLabRuntime/Graphics/RHI/RHISwapChain.h"
#include "GGLabRuntime/Scene/Components.h"

namespace gglab
{
	namespace
	{
		enum class ForwardPlusFixture : int32_t
		{
			ZeroLocalLights,
			OneLocalLight,
			SixtyFourLocalLights,
			MixedLightTypes,
			NearPlaneLight,
			TileBoundaryLight,
		};

		enum class SelectedTileMode : int32_t
		{
			CenterGeometry,
			BackgroundTopLeft,
		};

		const LabParameterId FixtureId("forward_plus.fixture");
		const LabParameterId LightingModeId("forward_plus.lighting_mode");
		const LabParameterId ValidateHdrDiffId("forward_plus.validate_hdr_diff");
		const LabParameterId SelectedTileModeId("forward_plus.selected_tile");
		const LabParameterId EnableCameraInputId("forward_plus.camera.enable_input");

		components::MaterialInstanceComponent MakeMaterial(
			std::string_view key, const Color& color) noexcept
		{
			components::MaterialInstanceComponent material{};
			material.m_Key = RuntimeMaterialKey(key);
			material.m_Properties.m_BaseColor = color;
			material.m_Properties.m_RoughnessFactor = 0.72f;
			return material;
		}
	}

	ForwardPlusLabSession::ForwardPlusLabSession(const LabSessionCreateInfo& createInfo) noexcept :
		ForwardPlusLabSession(createInfo, std::make_shared<ForwardPlusDebugReadback>())
	{
	}

	ForwardPlusLabSession::ForwardPlusLabSession(const LabSessionCreateInfo& createInfo,
		std::shared_ptr<ForwardPlusDebugReadback> debugReadback) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, std::make_unique<RenderPipelineForwardPBR>(debugReadback)),
		m_DebugReadback(std::move(debugReadback)), m_ViewportWidth(createInfo.m_WindowWidth),
		m_ViewportHeight(createInfo.m_WindowHeight)
	{
		GetMutableViewRenderProfile().m_Lighting.m_ForwardPlus.m_Mode =
			ForwardLightingMode::ForwardPlus;
		GetMutableViewRenderProfile().m_Lighting.m_ForwardPlus.m_EnableHdrDiffValidation = true;

		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = LightingModeId,
			.m_Name = "Lighting Mode",
			.m_Group = "Forward+",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(ForwardLightingMode::ForwardPlus),
			.m_EnumItems =
				{
					{
						.m_Value = int32_t(ForwardLightingMode::Legacy),
						.m_Name = "Legacy",
					},
					{
						.m_Value = int32_t(ForwardLightingMode::ForwardPlus),
						.m_Name = "Forward+",
					},
				},
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ValidateHdrDiffId,
			.m_Name = "Validate HDR Diff",
			.m_Group = "Forward+",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = true,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = FixtureId,
			.m_Name = "Fixture",
			.m_Group = "Forward+",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::RebuildScene,
			.m_DefaultValue = int32_t(ForwardPlusFixture::SixtyFourLocalLights),
			.m_EnumItems =
				{
					{
						.m_Value = int32_t(ForwardPlusFixture::ZeroLocalLights),
						.m_Name = "0 Local Lights",
					},
					{
						.m_Value = int32_t(ForwardPlusFixture::OneLocalLight),
						.m_Name = "1 Local Light",
					},
					{
						.m_Value = int32_t(ForwardPlusFixture::SixtyFourLocalLights),
						.m_Name = "64 Local Lights",
					},
					{
						.m_Value = int32_t(ForwardPlusFixture::MixedLightTypes),
						.m_Name = "Directional + Point + Spot",
					},
					{
						.m_Value = int32_t(ForwardPlusFixture::NearPlaneLight),
						.m_Name = "Near-plane Light",
					},
					{
						.m_Value = int32_t(ForwardPlusFixture::TileBoundaryLight),
						.m_Name = "Tile-boundary Light",
					},
				},
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = SelectedTileModeId,
			.m_Name = "Selected Tile",
			.m_Group = "Readback",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(SelectedTileMode::CenterGeometry),
			.m_EnumItems =
				{
					{
						.m_Value = int32_t(SelectedTileMode::CenterGeometry),
						.m_Name = "Center Geometry",
					},
					{
						.m_Value = int32_t(SelectedTileMode::BackgroundTopLeft),
						.m_Name = "Background Top-left",
					},
				},
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = EnableCameraInputId,
			.m_Name = "Enable Camera Input",
			.m_Group = "Camera",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = false,
			}));
		ApplyImmediateParameters();
	}

	void ForwardPlusLabSession::BeginPrepare() noexcept
	{
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		BuildScene();
		m_LoadingProgress =
			m_AssetPreparation.BuildProgress(*m_Services.m_AssetManager, "Preparing Forward+ Lab");
	}

	void ForwardPlusLabSession::TickPrepare() noexcept
	{
		if (m_LoadingProgress.IsPreparing())
		{
			m_LoadingProgress = m_AssetPreparation.BuildProgress(
				*m_Services.m_AssetManager, "Preparing Forward+ Lab");
		}
	}

	void ForwardPlusLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(m_LoadingProgress.IsReady(),
			"Forward+ Lab committed before its procedural assets were ready.");
	}

	void ForwardPlusLabSession::CancelPrepare() noexcept
	{
		m_DebugReadback->InvalidateResults();
		m_DebugReadback->ResetPerformance();
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		m_World.GetRegistry().clear();
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void ForwardPlusLabSession::OnEnter() noexcept
	{
		auto* gpuProfiler = m_Services.m_Renderer->GetGpuProfiler();
		if (gpuProfiler)
		{
			m_GpuProfilerWasEnabled = gpuProfiler->IsEnabled();
			gpuProfiler->SetEnabled(true);
		}
		ArmGpuTimingCaptureWarmup();
	}

	void ForwardPlusLabSession::OnExit() noexcept
	{
		if (auto* gpuProfiler = m_Services.m_Renderer->GetGpuProfiler())
		{
			gpuProfiler->SetEnabled(m_GpuProfilerWasEnabled);
		}
		m_DebugReadback->InvalidateResults();
	}

	void ForwardPlusLabSession::Update(float deltaTime) noexcept
	{
		if (m_EnableCameraInput)
		{
			UpdateCamera(deltaTime);
		}
		else
		{
			GetCamera().Update();
		}
		UpdateSelectedTile();
		CaptureGpuTimings();
	}

	void ForwardPlusLabSession::OnResize(uint32_t width, uint32_t height) noexcept
	{
		LabSessionBase::OnResize(width, height);
		m_ViewportWidth = width;
		m_ViewportHeight = height;
		m_DebugReadback->InvalidateResults();
		m_DebugReadback->ResetPerformance();
		ArmGpuTimingCaptureWarmup();
		UpdateSelectedTile();
	}

	void ForwardPlusLabSession::ApplyImmediateParameters() noexcept
	{
		auto& forwardPlus = GetMutableViewRenderProfile().m_Lighting.m_ForwardPlus;
		const ForwardLightingMode mode = static_cast<ForwardLightingMode>(GetParameters().Get(
			LightingModeId, int32_t(ForwardLightingMode::ForwardPlus)));
		const bool validateHdrDiff = GetParameters().Get(ValidateHdrDiffId, true);
		if (forwardPlus.m_Mode != mode ||
			forwardPlus.m_EnableHdrDiffValidation != validateHdrDiff)
		{
			m_DebugReadback->InvalidateResults();
			ArmGpuTimingCaptureWarmup();
		}
		forwardPlus.m_Mode = mode;
		forwardPlus.m_EnableHdrDiffValidation = validateHdrDiff;
		m_EnableCameraInput = GetParameters().Get(EnableCameraInputId, false);
		UpdateSelectedTile();
	}

	void ForwardPlusLabSession::RebuildScene() noexcept
	{
		m_AssetPreparation.Reset();
		BuildScene();
	}

	void ForwardPlusLabSession::OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept
	{
		GGLAB_UNUSED(impact);
		ApplyImmediateParameters();
	}

	void ForwardPlusLabSession::BuildScene() noexcept
	{
		m_DebugReadback->InvalidateResults();
		m_DebugReadback->ResetPerformance();
		ArmGpuTimingCaptureWarmup();
		ResetAssetInterests();
		auto& registry = m_World.GetRegistry();
		registry.clear();
		m_FixtureConfigured = false;
		ApplyCameraPreset();

		m_AssetPreparation.TrackModel(ProceduralCubeModelID, "ProceduralCube", 0.6f);
		m_AssetPreparation.TrackModel(ProceduralSphereModelID, "ProceduralSphere", 0.4f);

		components::TransformComponent wallTransform{};
		wallTransform.m_Position = Vector3(0.0f, 1.0f, 9.0f);
		wallTransform.m_Scale = Vector3(7.5f, 4.2f, 0.35f);
		const entt::entity wall = primitive::Cube::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = wallTransform,
			.m_MaterialInstance =
				MakeMaterial("gglab.lab.forward_plus.wall", Color(0.11f, 0.13f, 0.18f, 1.0f)),
			});

		components::TransformComponent sphereTransform{};
		sphereTransform.m_Position = Vector3(0.0f, 0.5f, 6.5f);
		sphereTransform.m_Scale = Vector3::One * 1.4f;
		const entt::entity sphere = primitive::Sphere::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = sphereTransform,
			.m_MaterialInstance =
				MakeMaterial("gglab.lab.forward_plus.sphere", Color(0.22f, 0.48f, 0.92f, 1.0f)),
			});

		m_FixtureConfigured = registry.valid(wall) && registry.valid(sphere);
		BuildLighting();
		ApplyImmediateParameters();
	}

	void ForwardPlusLabSession::BuildLighting() noexcept
	{
		auto& registry = m_World.GetRegistry();
		const auto fixture = static_cast<ForwardPlusFixture>(
			GetParameters().Get(FixtureId, int32_t(ForwardPlusFixture::SixtyFourLocalLights)));
		uint32_t lightCount = 0;
		switch (fixture)
		{
		case ForwardPlusFixture::ZeroLocalLights:
			lightCount = 0;
			break;
		case ForwardPlusFixture::SixtyFourLocalLights:
			lightCount = ForwardPlusTileLightCapacity;
			break;
		case ForwardPlusFixture::MixedLightTypes:
			lightCount = 3;
			break;
		case ForwardPlusFixture::OneLocalLight:
		case ForwardPlusFixture::NearPlaneLight:
		case ForwardPlusFixture::TileBoundaryLight:
			lightCount = 1;
			break;
		}

		for (uint32_t lightIndex = 0; lightIndex < lightCount; ++lightIndex)
		{
			const entt::entity entity = registry.create();
			components::TransformComponent transform{};
			if (fixture == ForwardPlusFixture::MixedLightTypes && lightIndex == 0)
			{
				Vector3 direction(-0.35f, -0.8f, 0.45f);
				direction.Normalize();
				transform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
			}
			else if (fixture == ForwardPlusFixture::MixedLightTypes)
			{
				transform.m_Position = lightIndex == 1 ? Vector3(-1.5f, 1.5f, 5.5f)
					: Vector3(1.8f, 2.0f, 5.8f);
				if (lightIndex == 2)
				{
					Vector3 direction = Vector3(0.0f, 0.5f, 6.5f) - transform.m_Position;
					direction.Normalize();
					transform.m_Rotation =
						math::RotationFromTo(Vector3::Forward, direction);
				}
			}
			else if (fixture == ForwardPlusFixture::NearPlaneLight ||
				(fixture == ForwardPlusFixture::SixtyFourLocalLights && lightIndex == 0))
			{
				transform.m_Position = GetCamera().GetPosition() +
					GetCamera().GetForward() * (GetCamera().GetNear() + 0.05f);
			}
			else if (fixture == ForwardPlusFixture::TileBoundaryLight)
			{
				transform.m_Position = Vector3(0.0f, 0.5f, 6.5f);
			}
			else
			{
				const uint32_t column = lightIndex % 8;
				const uint32_t row = lightIndex / 8;
				transform.m_Position = Vector3((static_cast<float>(column) - 3.5f) * 1.25f,
					(static_cast<float>(row) - 3.5f) * 0.8f + 1.0f,
					6.5f + static_cast<float>(lightIndex % 3) * 0.35f);
			}
			registry.emplace<components::TransformComponent>(entity, transform);

			components::LightComponent light{};
			if (fixture == ForwardPlusFixture::MixedLightTypes)
			{
				light.m_Type = lightIndex == 0 ? LightType::Directional
					: lightIndex == 1 ? LightType::Point
					: LightType::Spot;
			}
			else
			{
				light.m_Type = fixture == ForwardPlusFixture::TileBoundaryLight
					? LightType::Spot
					: LightType::Point;
			}
			light.m_Color =
				Color(0.35f + 0.65f * static_cast<float>((lightIndex * 17u) % 31u) / 30.0f,
					0.3f + 0.7f * static_cast<float>((lightIndex * 11u) % 29u) / 28.0f,
					0.4f + 0.6f * static_cast<float>((lightIndex * 7u) % 23u) / 22.0f, 1.0f);
			light.m_Intensity = fixture == ForwardPlusFixture::MixedLightTypes ? 1.5f : 0.08f;
			light.m_Range = 20.0f;
			light.m_SpotAngle = 55.0f;
			registry.emplace<components::LightComponent>(entity, light);
		}
	}

	void ForwardPlusLabSession::ApplyCameraPreset() noexcept
	{
		GetCamera().LookAt(Vector3(0.0f, 1.0f, -7.0f), Vector3(0.0f, 0.7f, 7.0f));
		GetCamera().SetFov(52.0f);
		GetCamera().SetNearFar(0.1f, 250.0f);
		GetCamera().Update();
	}

	void ForwardPlusLabSession::UpdateSelectedTile() noexcept
	{
		if (!m_DebugReadback)
		{
			return;
		}
		const auto mode = static_cast<SelectedTileMode>(
			GetParameters().Get(SelectedTileModeId, int32_t(SelectedTileMode::CenterGeometry)));
		if (mode == SelectedTileMode::BackgroundTopLeft)
		{
			m_DebugReadback->SetSelectedTile(0, 0);
			return;
		}

		const ForwardPlusTileGrid tileGrid =
			MakeForwardPlusTileGrid(m_ViewportWidth, m_ViewportHeight);
		m_DebugReadback->SetSelectedTile(tileGrid.IsValid() ? tileGrid.m_TileCountX / 2 : 0,
			tileGrid.IsValid() ? tileGrid.m_TileCountY / 2 : 0);
	}

	void ForwardPlusLabSession::CaptureGpuTimings() noexcept
	{
		auto* gpuProfiler = m_Services.m_Renderer->GetGpuProfiler();
		if (!gpuProfiler || !gpuProfiler->IsEnabled())
		{
			return;
		}
		const GpuProfileFrameSnapshot frame = gpuProfiler->GetLatestFrame();
		if (!frame.IsValid() || frame.m_FrameIndex == m_LastGpuProfileFrame)
		{
			return;
		}
		m_LastGpuProfileFrame = frame.m_FrameIndex;
		if (m_GpuTimingWarmupFrames > 0)
		{
			--m_GpuTimingWarmupFrames;
			return;
		}

		double cullMilliseconds = 0.0;
		double opaqueMilliseconds = 0.0;
		bool hasCullSample = false;
		bool hasOpaqueSample = false;
		for (const auto& sample : frame.m_Samples)
		{
			if (sample.m_Name == "Lighting.ForwardPlus.Cull")
			{
				cullMilliseconds += sample.m_Milliseconds;
				hasCullSample = true;
			}
			else if (sample.m_Name == "Geometry.ForwardOpaque")
			{
				opaqueMilliseconds += sample.m_Milliseconds;
				hasOpaqueSample = true;
			}
		}

		const ForwardPlusSettings& settings = GetViewRenderProfile().m_Lighting.m_ForwardPlus;
		if (settings.m_Mode == ForwardLightingMode::Legacy && hasOpaqueSample)
		{
			m_DebugReadback->RecordLegacyGpuTiming(frame.m_FrameIndex, opaqueMilliseconds);
		}
		else if (settings.m_Mode == ForwardLightingMode::ForwardPlus &&
			!settings.m_EnableHdrDiffValidation && hasCullSample && hasOpaqueSample)
		{
			m_DebugReadback->RecordForwardPlusGpuTiming(
				frame.m_FrameIndex, cullMilliseconds, opaqueMilliseconds);
		}
	}

	void ForwardPlusLabSession::ArmGpuTimingCaptureWarmup() noexcept
	{
		const auto* rhiContext = m_Services.m_Renderer
			? m_Services.m_Renderer->GetRHIContext()
			: nullptr;
		m_GpuTimingWarmupFrames = rhiContext ? rhiContext->GetFrameSlotCount() : 3;
	}

	void ForwardPlusLabSession::BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		diagnostics.m_Title = "Fixed-stride Forward+ Cull";
		if (!m_DebugReadback)
		{
			return;
		}

		const uint64_t requestGeneration = m_DebugReadback->GetCurrentGeneration();
		ForwardPlusTileReadback result = m_DebugReadback->GetLatest();
		if (!IsForwardPlusReadbackGenerationCurrent(
			result.m_RequestGeneration, requestGeneration) ||
			result.m_TileGrid.m_Width != m_ViewportWidth ||
			result.m_TileGrid.m_Height != m_ViewportHeight)
		{
			result = {};
		}
		const auto fixture = static_cast<ForwardPlusFixture>(
			GetParameters().Get(FixtureId, int32_t(ForwardPlusFixture::SixtyFourLocalLights)));
		const auto selectedTileMode = static_cast<SelectedTileMode>(
			GetParameters().Get(SelectedTileModeId, int32_t(SelectedTileMode::CenterGeometry)));
		uint32_t expectedLightCount = 1;
		if (selectedTileMode == SelectedTileMode::BackgroundTopLeft ||
			fixture == ForwardPlusFixture::ZeroLocalLights)
		{
			expectedLightCount = 0;
		}
		else if (fixture == ForwardPlusFixture::SixtyFourLocalLights)
		{
			expectedLightCount = ForwardPlusTileLightCapacity;
		}
		else if (fixture == ForwardPlusFixture::MixedLightTypes)
		{
			expectedLightCount = 2;
		}
		ForwardPlusHdrDiffReadback hdrDiff = m_DebugReadback->GetLatestHdrDiff();
		if (!IsForwardPlusReadbackGenerationCurrent(
			hdrDiff.m_RequestGeneration, requestGeneration) ||
			hdrDiff.m_Width != m_ViewportWidth || hdrDiff.m_Height != m_ViewportHeight)
		{
			hdrDiff = {};
		}
		std::shared_ptr<const ForwardPlusGridReadback> grid = m_DebugReadback->GetLatestGrid();
		if (!grid || !grid->m_IsValid || !IsForwardPlusReadbackGenerationCurrent(
			grid->m_RequestGeneration, requestGeneration) ||
			grid->m_TileGrid.m_Width != m_ViewportWidth ||
			grid->m_TileGrid.m_Height != m_ViewportHeight)
		{
			grid.reset();
		}
		const ForwardPlusGridMetrics gridMetrics = grid
			? BuildForwardPlusGridMetrics(grid->m_TileGrid, grid->m_Headers, grid->m_DepthRanges)
			: ForwardPlusGridMetrics{};
		const ForwardPlusPerformanceReadback performance = m_DebugReadback->GetPerformance();
		const ForwardPlusSettings& forwardPlus = GetViewRenderProfile().m_Lighting.m_ForwardPlus;
		const bool hdrDiffRequested =
			forwardPlus.m_Mode == ForwardLightingMode::ForwardPlus &&
			forwardPlus.m_EnableHdrDiffValidation;
		const RHIDevice* device = m_Services.m_Renderer ? m_Services.m_Renderer->GetDevice() : nullptr;
		const RHIShaderWaveCapabilities waveCapabilities =
			device ? device->GetShaderWaveCapabilities() : RHIShaderWaveCapabilities{};
		const std::string waveLaneRange =
			waveCapabilities.IsValid() ? std::format("{} - {}", waveCapabilities.m_MinLaneCount,
				waveCapabilities.m_MaxLaneCount)
			: "not reported";
		const uint32_t tileIndex =
			result.m_IsValid ? result.m_TileY * result.m_TileGrid.m_TileCountX + result.m_TileX : 0;
		const uint32_t lightCount = result.m_Header.GetCount();
		const bool headerValid = result.m_IsValid &&
			result.m_Header.m_Offset == GetForwardPlusTileOffset(tileIndex) &&
			lightCount <= ForwardPlusTileLightCapacity;
		const bool fixtureResultValid = result.m_IsValid && lightCount == expectedLightCount;
		bool sortedIndices = headerValid;
		for (uint32_t index = 0; index < lightCount; ++index)
		{
			sortedIndices &= result.m_LightIndices[index] < MaxLightCapacity;
			if (index > 0)
			{
				sortedIndices &= result.m_LightIndices[index - 1] < result.m_LightIndices[index];
			}
		}

		std::string indexPreview = "none";
		if (lightCount > 0 && lightCount <= ForwardPlusTileLightCapacity)
		{
			indexPreview.clear();
			const uint32_t previewCount = std::min(lightCount, 12u);
			for (uint32_t index = 0; index < previewCount; ++index)
			{
				if (!indexPreview.empty())
				{
					indexPreview += ", ";
				}
				indexPreview += std::to_string(result.m_LightIndices[index]);
			}
			if (previewCount < lightCount)
			{
				indexPreview += ", ...";
			}
		}

		diagnostics.m_Metrics = {
			{
				.m_Name = "Adapter/driver domain",
				.m_Value =
					device ? std::string(device->GetAdapterCompatibilityIdentity()) : "unavailable",
			},
			{
				.m_Name = "Reported wave lanes",
				.m_Value = waveLaneRange,
			},
			{
				.m_Name = "Compaction",
				.m_Value = "64-lane group-shared stable scan",
			},
			{
				.m_Name = "GPU schedules",
				.m_Value = std::to_string(m_DebugReadback->GetScheduledCount()),
			},
			{
				.m_Name = "Request generation",
				.m_Value = std::to_string(requestGeneration),
			},
			{
				.m_Name = "Readback frame",
				.m_Value = result.m_IsValid ? std::to_string(result.m_FrameSerial) : "pending",
			},
			{
				.m_Name = "Tile grid",
				.m_Value = result.m_IsValid ? std::format("{} x {}", result.m_TileGrid.m_TileCountX,
												  result.m_TileGrid.m_TileCountY)
											: "pending",
			},
			{
				.m_Name = "Selected tile",
				.m_Value = result.m_IsValid
							   ? std::format("({}, {})", result.m_TileX, result.m_TileY)
							   : "pending",
			},
			{
				.m_Name = "Header",
				.m_Value = result.m_IsValid ? std::format("offset={}, count={}",
												  result.m_Header.m_Offset, lightCount)
											: "pending",
			},
			{
				.m_Name = "Expected count",
				.m_Value = std::to_string(expectedLightCount),
			},
			{
				.m_Name = "Light indices",
				.m_Value = indexPreview,
			},
			{
				.m_Name = "Non-empty / empty light lists",
				.m_Value = gridMetrics.m_IsValid
					? std::format("{} / {}", gridMetrics.m_NonEmptyLightListTileCount,
						gridMetrics.m_EmptyLightListTileCount)
					: "pending",
			},
			{
				.m_Name = "Light references",
				.m_Value = gridMetrics.m_IsValid
					? std::format("{} total, {:.2f} average, {} max",
						gridMetrics.m_TotalLightReferences,
						gridMetrics.m_AverageLightsPerTile,
						gridMetrics.m_MaxLightsPerTile)
					: "pending",
			},
			{
				.m_Name = "Full-grid View-Z",
				.m_Value = gridMetrics.m_IsValid
					? std::format("{:.4f} - {:.4f}", gridMetrics.m_MinViewZ,
						gridMetrics.m_MaxViewZ)
					: "pending",
			},
			{
				.m_Name = "Latest Legacy / Forward+ GPU sample",
				.m_Value = performance.m_HasLegacySample && performance.m_HasForwardPlusSample
					? std::format("{:.3f} / {:.3f} + {:.3f} ms",
						performance.m_LegacyOpaqueMilliseconds,
						performance.m_ForwardPlusCullMilliseconds,
						performance.m_ForwardPlusOpaqueMilliseconds)
					: "capture both modes",
			},
			{
				.m_Name = "HDR max absolute error",
				.m_Value = hdrDiff.m_IsValid
					? std::format("{:.8f}", hdrDiff.m_MaxAbsoluteError)
					: "pending",
			},
			{
				.m_Name = "HDR max relative luminance error",
				.m_Value = hdrDiff.m_IsValid
					? std::format("{:.8f}", hdrDiff.m_MaxRelativeLuminanceError)
					: "pending",
			},
			{
				.m_Name = "HDR max-error pixel",
				.m_Value = hdrDiff.m_IsValid
					? std::format("({}, {})", hdrDiff.m_MaxErrorPixelX,
						hdrDiff.m_MaxErrorPixelY)
					: "pending",
			},
		};
		diagnostics.m_Checks = {
			{
				.m_Name = "GPU tile readback",
				.m_Status = !result.m_IsValid ? LabDiagnosticCheckStatus::Pending
											  : LabDiagnosticCheckStatus::Passed,
				.m_Detail = "Header and indices are copied from the completed GPU cull frame.",
			},
			{
				.m_Name = "Full-grid diagnostics",
				.m_Status = !gridMetrics.m_IsValid ? LabDiagnosticCheckStatus::Pending
					: LabDiagnosticCheckStatus::Passed,
				.m_Detail =
					"Current-generation headers and depth ranges cover every tile in the viewport.",
			},
			{
				.m_Name = "Tile-list capacity",
				.m_Status = !gridMetrics.m_IsValid ? LabDiagnosticCheckStatus::Pending
					: gridMetrics.m_OverflowTileCount == 0
						? LabDiagnosticCheckStatus::Passed
						: LabDiagnosticCheckStatus::Failed,
				.m_Detail = "No tile may exceed or flag the fixed 64-light capacity.",
			},
			{
				.m_Name = "Fixed-stride address/count",
				.m_Status = !result.m_IsValid ? LabDiagnosticCheckStatus::Pending
							: headerValid ? LabDiagnosticCheckStatus::Passed
											  : LabDiagnosticCheckStatus::Failed,
				.m_Detail = "Offset equals TileIndex * 64 and count does not exceed 64.",
			},
			{
				.m_Name = "Selected fixture result",
				.m_Status = !result.m_IsValid ? LabDiagnosticCheckStatus::Pending
							: fixtureResultValid ? LabDiagnosticCheckStatus::Passed
												 : LabDiagnosticCheckStatus::Failed,
				.m_Detail =
					"GPU count matches the selected 0/1/64-light fixture, or zero for a background-only tile.",
			},
			{
				.m_Name = "Stable global light order",
				.m_Status = !result.m_IsValid ? LabDiagnosticCheckStatus::Pending
							: sortedIndices ? LabDiagnosticCheckStatus::Passed
											  : LabDiagnosticCheckStatus::Failed,
				.m_Detail =
					"Readback light indices are strictly increasing and inside the global 64-light table.",
			},
			{
				.m_Name = "Fixture construction",
				.m_Status = m_LoadingProgress.IsPreparing() ? LabDiagnosticCheckStatus::Pending
							: m_FixtureConfigured ? LabDiagnosticCheckStatus::Passed
															: LabDiagnosticCheckStatus::Failed,
				.m_Detail =
					"The deterministic geometry and selected local-light fixture were created.",
			},
			{
				.m_Name = "Legacy vs Forward+ HDR diff",
				.m_Status = !hdrDiffRequested ? LabDiagnosticCheckStatus::Passed
					: !hdrDiff.m_IsValid ? LabDiagnosticCheckStatus::Pending
					: IsForwardPlusHdrDiffWithinTolerance(hdrDiff)
						? LabDiagnosticCheckStatus::Passed
						: LabDiagnosticCheckStatus::Failed,
				.m_Detail = !hdrDiffRequested
					? "HDR diff validation is disabled for the selected lighting mode."
					: std::format("Compared {} opaque pixels; tolerances are abs <= {} and relative luminance <= {}.",
						hdrDiff.m_ComparedPixelCount, ForwardPlusHdrDiffAbsoluteTolerance,
						ForwardPlusHdrDiffRelativeLuminanceTolerance),
			},
		};
	}

	LabId ForwardPlusLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.forward_plus");
	}

	LabDescriptor ForwardPlusLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Forward+",
			.m_Category = "Rendering",
			.m_Description =
				"Validates fixed-stride 16x16 tile culling, full-grid occupancy and depth "
				"diagnostics, background rejection, near-plane lights, stable light order, HDR "
				"equivalence, and GPU timing.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 2,
		};
	}

	std::unique_ptr<LabSessionBase> ForwardPlusLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<ForwardPlusLabSession>(createInfo);
	}
}
