#include "DevTools/DevelopGui/Panels/CameraInspectorPanel.h"
#include "DevTools/EnumText/EnumTextGraphics.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiMathWidgets.h"
#include "GGLabRuntime/Graphics/CameraTooling.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include <imgui.h>

namespace gglab
{
	namespace
	{
		struct CameraPanelState
		{
			// UI behavior
			bool m_AutoApply = true;
			bool m_SyncFromCamera = true;
			bool m_SyncFromController = true;
			bool m_ShowBasis = true;
			bool m_ShowMatrices = false;
			uint64_t m_SelectedCameraId = 0;
			uint64_t m_LastCameraId = 0;

			// cached edit values
			float m_Pos[3] = { 0.0f, 0.0f, 0.0f };
			float m_YawDegree = 0.0f;
			float m_PitchDegree = 0.0f;

			float m_FovDegree = 60.0f;
			float m_NearZ = 0.01f;
			float m_FarZ = 1000.0f;
			float m_ExposureCompensationEV = 0.0f;

			// controller params
			CameraControllerSettings m_CtrlParams{};

			// first time initialize
			bool m_Initialized = false;
		};

		static void PullFromCamera(CameraPanelState& state, const CameraToolingObservation& camera) noexcept
		{
			const Vector3 p = camera.m_Settings.m_Position;
			state.m_Pos[0] = p.m_X;
			state.m_Pos[1] = p.m_Y;
			state.m_Pos[2] = p.m_Z;

			state.m_YawDegree = math::ToDegrees(camera.m_Settings.m_Yaw);
			state.m_PitchDegree = math::ToDegrees(camera.m_Settings.m_Pitch);

			state.m_FovDegree = camera.m_Settings.m_Fov;
			state.m_NearZ = camera.m_Settings.m_Near;
			state.m_FarZ = camera.m_Settings.m_Far;
			state.m_ExposureCompensationEV = camera.m_Settings.m_ExposureCompensationEV;
		}


		static void PushToCamera(CameraPanelState& state, const CameraToolingViewBase& view,
			CameraToolingControlBase& control, uint64_t id) noexcept
		{
			const CameraEditSettings settings{
				Vector3(state.m_Pos[0], state.m_Pos[1], state.m_Pos[2]),
				math::ToRadians(state.m_YawDegree), math::ToRadians(state.m_PitchDegree),
				state.m_FovDegree, state.m_NearZ, state.m_FarZ, state.m_ExposureCompensationEV
			};
			if (control.SetCamera(id, settings))
			{
				const auto updated = view.GetCameras();
				if (const auto* camera = updated.FindCamera(id)) PullFromCamera(state, *camera);
			}
		}

		static void DrawCameraControls(CameraPanelState& state, const CameraToolingSnapshot& snapshot,
			CameraToolingControlBase* control) noexcept
		{
			const CameraToolingObservation* display = nullptr;
			for (const auto& camera : snapshot.m_Cameras)
			{
				if (camera.m_RenderViewId == snapshot.m_DisplayViewId) display = &camera;
			}
			const std::string fallback = devtools::EnumText(snapshot.m_DisplayViewId);
			ImGui::BeginDisabled(!control);
			if (ImGui::BeginCombo("Display View", display ? display->m_Name.c_str() : fallback.c_str()))
			{
				for (const auto& camera : snapshot.m_Cameras)
				{
					if (!camera.m_EnableRenderView && camera.m_RenderViewId != RenderViewID::Main) continue;
					const bool selected = camera.m_RenderViewId == snapshot.m_DisplayViewId;
					if (ImGui::Selectable(camera.m_Name.c_str(), selected) && control)
						control->SetDisplayCamera(camera.m_Id);
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			const auto* selectedCamera = snapshot.FindCamera(state.m_SelectedCameraId);
			if (ImGui::BeginCombo("Edit Camera", selectedCamera ? selectedCamera->m_Name.c_str() : "Camera"))
			{
				for (const auto& camera : snapshot.m_Cameras)
				{
					const bool selected = camera.m_Id == state.m_SelectedCameraId;
					if (ImGui::Selectable(camera.m_Name.c_str(), selected) && control &&
						control->SetActiveCamera(camera.m_Id))
					{
						state.m_SelectedCameraId = camera.m_Id;
						state.m_Initialized = false;
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (ImGui::Button("Add Debug Camera") && control)
			{
				state.m_SelectedCameraId = control->AddDebugCamera();
				state.m_Initialized = false;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!selectedCamera || !selectedCamera->m_IsDebug);
			if (ImGui::Button("Remove Camera") && control &&
				control->RemoveCamera(state.m_SelectedCameraId))
			{
				state.m_SelectedCameraId = 0;
				state.m_Initialized = false;
			}
			ImGui::EndDisabled();
			ImGui::EndDisabled();
		}
	}

	void CameraInspectorPanel::Draw(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<CameraPanelState>();
		if (!context.m_Cameras)
		{
			ImGui::TextUnformatted("Camera tooling query is not available.");
			state.m_Initialized = false;
			state.m_SelectedCameraId = 0;
			return;
		}
		const auto& view = *context.m_Cameras;
		auto* control = context.m_CameraControl;
		auto snapshot = view.GetCameras();
		if (!snapshot.FindCamera(state.m_SelectedCameraId))
			state.m_SelectedCameraId = snapshot.m_ActiveCameraId;
		DrawCameraControls(state, snapshot, control);
		snapshot = view.GetCameras();
		if (!snapshot.FindCamera(state.m_SelectedCameraId))
			state.m_SelectedCameraId = snapshot.m_ActiveCameraId;
		const auto* selected = snapshot.FindCamera(state.m_SelectedCameraId);
		if (!selected)
		{
			ImGui::TextUnformatted("No cameras registered.");
			state.m_Initialized = false;
			return;
		}
		auto camera = *selected;
		const uint64_t id = camera.m_Id;
		const bool cameraCtrl = camera.m_Controller.has_value();
		if (!state.m_Initialized || state.m_LastCameraId != id)
		{
			PullFromCamera(state, camera);
			if (cameraCtrl) state.m_CtrlParams = *camera.m_Controller;
			state.m_LastCameraId = id;
			state.m_Initialized = true;
		}
		if (state.m_SyncFromCamera && state.m_AutoApply) PullFromCamera(state, camera);
		if (cameraCtrl && state.m_SyncFromController && state.m_AutoApply)
			state.m_CtrlParams = *camera.m_Controller;

		ImGui::TextUnformatted(camera.m_Name.c_str());
		ImGui::Separator();
		ImGui::Checkbox("Auto Apply", &state.m_AutoApply);
		ImGui::SameLine();
		ImGui::Checkbox("Sync Camera", &state.m_SyncFromCamera);
		ImGui::SameLine();
		ImGui::Checkbox("Sync Controller", &state.m_SyncFromController);
		ImGui::Checkbox("Show Basis", &state.m_ShowBasis);
		ImGui::SameLine();
		ImGui::Checkbox("Show Matrices", &state.m_ShowMatrices);

		ImGui::BeginDisabled(!control);
		bool frustumChanged = ImGui::Checkbox("Draw Frustum", &camera.m_ShowFrustum);
		if (camera.m_ShowFrustum)
			frustumChanged |= ImGui::ColorEdit4("Frustum Color", &camera.m_FrustumColor.m_R);
		if (frustumChanged && control) control->SetFrustum(id, camera.m_ShowFrustum, camera.m_FrustumColor);
		if (camera.m_IsDebug)
		{
			bool enabled = camera.m_EnableRenderView;
			if (ImGui::Checkbox("Build RenderView", &enabled) && control)
			{
				control->SetRenderViewEnabled(id, enabled);
				const auto updated = view.GetCameras();
				if (const auto* current = updated.FindCamera(id)) camera = *current;
			}
			if (camera.m_EnableRenderView && IsDebugCameraRenderViewID(camera.m_RenderViewId))
			{
				ImGui::Text("RenderView: %s", devtools::EnumText(camera.m_RenderViewId).c_str());
				const std::string modeText = devtools::EnumText(camera.m_VisibilityMode);
				if (ImGui::BeginCombo("Visibility Mode", modeText.c_str()))
				{
					constexpr std::array modes = { RenderViewVisibilityMode::Self,
						RenderViewVisibilityMode::MainCamera, RenderViewVisibilityMode::IntersectionWithMainCamera,
						RenderViewVisibilityMode::None };
					for (const auto mode : modes)
					{
						const bool selectedMode = mode == camera.m_VisibilityMode;
						if (ImGui::Selectable(devtools::EnumText(mode).c_str(), selectedMode) && control)
							control->SetVisibilityMode(id, mode);
						if (selectedMode) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
			else ImGui::TextDisabled("RenderView: None");
		}
		ImGui::Spacing();

		bool camChanged = false;
		bool ctrlChanged = false;

		ImGui::SeparatorText("Transform");
		camChanged |= ImGui::DragFloat3("Position", state.m_Pos, 0.05f);

		camChanged |= ImGui::DragFloat("Yaw (degree)", &state.m_YawDegree, 0.1f);
		camChanged |= ImGui::DragFloat("Pitch (degree)", &state.m_PitchDegree, 0.1f);

		ImGui::SeparatorText("Projection");
		camChanged |= ImGui::DragFloat("FOV (degree)", &state.m_FovDegree, 0.1f, 1.0f, 179.0f);
		camChanged |= ImGui::DragFloat("Near", &state.m_NearZ, 0.001f, 0.0001f, 100.0f);
		camChanged |= ImGui::DragFloat("Far", &state.m_FarZ, 1.0f, 0.1f, 100000.0f);

		ImGui::SeparatorText("Exposure");
		camChanged |= ImGui::SliderFloat("Exposure Compensation", &state.m_ExposureCompensationEV,
			-10.0f, 10.0f, "%+.2f EV", ImGuiSliderFlags_AlwaysClamp);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		{
			ImGui::SetTooltip("Adjusts image brightness in photographic stops.\n"
				"+1 EV doubles exposure; -1 EV halves it.");
		}
		ImGui::Text("Exposure Multiplier: %.4fx", std::exp2(state.m_ExposureCompensationEV));
		if (ImGui::Button("Reset Exposure"))
		{
			state.m_ExposureCompensationEV = 0.0f;
			camChanged = true;
		}

		// Controller params
		ImGui::SeparatorText("Controller");
		if (cameraCtrl)
		{
			ctrlChanged |= ImGui::DragFloat(
				"Movement Speed", &state.m_CtrlParams.m_MovementSpeed, 0.1f, 0.0f, 1000.0f);
			ctrlChanged |= ImGui::DragFloat("Sensitivity(Rad/Count)",
				&state.m_CtrlParams.m_MouseSensitivityRadPerCount, 0.00001f, 0.0001f, 0.005f,
				"%.4f");
			ctrlChanged |= ImGui::DragFloat("Accelerate Multiplier",
				&state.m_CtrlParams.m_AccelerateMultiplier, 0.05f, 1.0f, 20.0f);
			ctrlChanged |=
				ImGui::SliderFloat("SmoothStep T", &state.m_CtrlParams.m_SmoothStepT, 0.0f, 1.0f);

			if (ImGui::Button("Reset Velocity"))
			{
				if (control) control->ResetVelocity(id);
			}
		}
		else
		{
			ImGui::TextUnformatted("No controller bound.");
		}

		ImGui::Spacing();

		// Behavior
		if (state.m_AutoApply)
		{
			if (camChanged)
			{
				if (control) PushToCamera(state, view, *control, id);
			}
			if (cameraCtrl && ctrlChanged)
			{
				if (control) control->SetController(id, state.m_CtrlParams);
			}
		}
		else
		{
			ImGui::Separator();
			if (ImGui::Button("Apply"))
			{
				if (control) PushToCamera(state, view, *control, id);
				if (cameraCtrl)
				{
					if (control) control->SetController(id, state.m_CtrlParams);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Revert From Runtime"))
			{
				PullFromCamera(state, camera);
				if (cameraCtrl)
				{
					state.m_CtrlParams = *camera.m_Controller;
				}
			}
		}

		ImGui::EndDisabled();
		const auto refreshed = view.GetCameras();
		if (const auto* current = refreshed.FindCamera(id)) camera = *current;

		// Read only infos
		ImGui::SeparatorText("Runtime Info");
		ImGui::Text("Aspect: %.4f", camera.m_Aspect);

		if (state.m_ShowBasis)
		{
			devtools::DrawVector3Text("Forward", camera.m_Forward);
			devtools::DrawVector3Text("Right", camera.m_Right);
			devtools::DrawVector3Text("Up", camera.m_Up);
		}

		if (state.m_ShowMatrices)
		{
			devtools::DrawMatrix4x4Tree("View Matrix", camera.m_ViewMatrix);
			devtools::DrawMatrix4x4Tree("Proj Matrix", camera.m_ProjMatrix);
		}
	}
}
