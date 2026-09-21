#include "Application/Lab/Sessions/TextureContractLabSession.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Graphics/Asset/AssetLoadProgress.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/CameraReferenceView.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "GGLabRuntime/Scene/Components.h"

namespace gglab
{
	namespace
	{
		constexpr const char* TextureContractModelPath =
			"Assets/Models/GGLabTextureContract/GGLabTextureContract.gltf";
	}

	TextureContractLabSession::TextureContractLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, CreateRenderPipelineForwardPBR())
	{
		auto& profile = GetMutableViewRenderProfile();
		profile.m_TemporalAA.m_Enabled = false;
		profile.m_Lighting.m_GTAO.m_Enabled = false;
		profile.m_PostProcess.m_Bloom.m_Enabled = false;

		auto& cameraRig = GetCameraRig();
		const bool registered = cameraRig.SetReferenceViews({ CameraReferenceView{
			.m_Id = "CAM_TextureContract",
			.m_Name = "Texture Contract",
			.m_Purpose = "Top: UV and sRGB. Middle: tangent normals. Bottom: metallic/roughness.",
			.m_Position = { 0.0f, 2.3f, -12.5f },
			.m_Target = { 0.0f, 2.3f, 0.0f },
			.m_VerticalFovDegrees = math::ToDegrees(0.6509917105f),
			.m_FarPlane = 50.0f,
		} });
		GGLAB_ASSERT_MSG(registered, "Texture contract reference view must be valid.");
		const bool restored = cameraRig.RestoreReferenceView("CAM_TextureContract");
		GGLAB_ASSERT_MSG(restored, "Texture contract must start at its reference view.");
	}

	void TextureContractLabSession::BeginPrepare() noexcept
	{
		ResetAssetInterests();
		m_World.GetRegistry().clear();
		m_PendingModelId = GetAssetOwnerScope().LoadModelAsync(TextureContractModelPath).m_ModelId;
		m_LoadingProgress = {
			.m_Status = m_PendingModelId.IsValid() ? LoadingStatus::Preparing : LoadingStatus::Failed,
			.m_Fraction = 0.05f,
			.m_Stage = m_PendingModelId.IsValid() ? "Loading texture contract board" : "Model request failed",
			.m_Detail = TextureContractModelPath,
		};
	}

	void TextureContractLabSession::TickPrepare() noexcept
	{
		if (!m_LoadingProgress.IsPreparing())
		{
			return;
		}

		const Model* model = m_Services.m_AssetManager->GetModel(m_PendingModelId);
		if (!model)
		{
			m_LoadingProgress.m_Status = LoadingStatus::Failed;
			m_LoadingProgress.m_Stage = "Model request unavailable";
			return;
		}

		LoadingProgressBuilder progress;
		progress.AddCompletedStep(0.05f);
		progress.AddAssetStep(0.95f,
			GetAssetLoadProgress(model->m_State, AssetLoadKind::Model, model->m_LoadProgress),
			TextureContractModelPath);
		m_LoadingProgress = progress.Build();
	}

	void TextureContractLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(m_LoadingProgress.IsReady(), "Texture contract must be ready before commit.");
		auto& registry = m_World.GetRegistry();
		const entt::entity board = registry.create();
		registry.emplace<components::TransformComponent>(board);
		registry.emplace<components::ModelComponent>(board,
			components::ModelComponent{ .m_ModelId = m_PendingModelId });
		m_PendingModelId.Reset();

		const entt::entity lightEntity = registry.create();
		Vector3 direction(-0.45f, -0.65f, 1.0f);
		direction.Normalize();
		components::TransformComponent lightTransform{};
		lightTransform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
		registry.emplace<components::TransformComponent>(lightEntity, lightTransform);

		components::LightComponent light{};
		light.m_Intensity = 3.0f;
		light.m_Color = Color::White;
		light.m_Type = LightType::Directional;
		light.m_Range = 1000.0f;
		// Leave shadows disabled to isolate material inputs from shadow filtering artifacts.
		registry.emplace<components::LightComponent>(lightEntity, light);
	}

	void TextureContractLabSession::CancelPrepare() noexcept
	{
		ResetAssetInterests();
		m_PendingModelId.Reset();
		m_World.GetRegistry().clear();
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void TextureContractLabSession::OnEnter() noexcept
	{
		if (m_HasEnvironmentOverride)
		{
			return;
		}

		// Pending sessions must not change the active Lab's environment while loading.
		const auto previous = m_Services.m_EnvironmentLighting->GetEnvironmentLightingSettings();
		m_PreviousEnvironmentIntensity = previous.m_Intensity;
		m_PreviousSkyboxEnabled = previous.m_EnableSkybox;
		m_HasEnvironmentOverride = true;
		m_Services.m_EnvironmentLightingControl->SetIntensity(0.0f);
		m_Services.m_EnvironmentLightingControl->SetSkyboxEnabled(false);
	}

	void TextureContractLabSession::OnExit() noexcept
	{
		if (m_HasEnvironmentOverride)
		{
			m_Services.m_EnvironmentLightingControl->SetIntensity(m_PreviousEnvironmentIntensity);
			m_Services.m_EnvironmentLightingControl->SetSkyboxEnabled(m_PreviousSkyboxEnabled);
			m_HasEnvironmentOverride = false;
		}
		// The base asset scope survives until LabRuntime retires this session after its GPU fence.
	}

	void TextureContractLabSession::Update(float deltaTime) noexcept
	{
		UpdateCamera(deltaTime);
	}

	LabId TextureContractLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.texture_contract");
	}

	LabDescriptor TextureContractLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Texture Contract",
			.m_Category = "Materials",
			.m_Description = "Checks UVs, sRGB, tangent normals and metallic/roughness texture channels.",
			.m_Kind = LabKind::Scene,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> TextureContractLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<TextureContractLabSession>(createInfo);
	}
}
