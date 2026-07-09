#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/CameraInspectorPanel.h"
#include "DevTools/EnumText/EnumTextGraphics.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Graphics/Camera.h"
#include "Graphics/CameraController.h"
#include "Graphics/CameraRig.h"
#include "Core/Math/MathFunctions.h"

#include <array>

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
			size_t m_SelectedCameraIndex = 0;
			const Camera* m_LastCamera = nullptr;

			// cached edit values
			float m_Pos[3] = { 0.0f, 0.0f, 0.0f };
			float m_YawDegree = 0.0f;
			float m_PitchDegree = 0.0f;

			float m_FovDegree = 60.0f;
			float m_NearZ = 0.01f;
			float m_FarZ = 1000.0f;

			// controller params
			CameraController::Params m_CtrlParams{};

			// first time initialize
			bool m_Initialized = false;
		};

		struct CameraBinding
		{
			Camera* m_Camera = nullptr;
			CameraController* m_Controller = nullptr;
			CameraRig::CameraSlot* m_Slot = nullptr;
			size_t m_Index = 0;
		};

		static void PullFromCamera(CameraPanelState& state, const Camera& camera) noexcept
		{
			const Vector3 p = camera.GetPosition();
			state.m_Pos[0] = p.m_X; state.m_Pos[1] = p.m_Y; state.m_Pos[2] = p.m_Z;

			state.m_YawDegree = math::ToDegrees(camera.GetYaw());
			state.m_PitchDegree = math::ToDegrees(camera.GetPitch());

			state.m_FovDegree = camera.GetFov();
			state.m_NearZ = camera.GetNear();
			state.m_FarZ = camera.GetFar();
		}

		static void PullFromController(CameraPanelState& state, const CameraController& camCtrl) noexcept
		{
			state.m_CtrlParams = camCtrl.GetParams();
		}

		static void PushToCamera(CameraPanelState& state, Camera& camera) noexcept
		{
			// sanitize
			state.m_FovDegree = Camera::ClampFov(state.m_FovDegree);
			state.m_NearZ = Camera::ClampNear(state.m_NearZ);
			state.m_FarZ = Camera::ClampFar(state.m_NearZ, state.m_FarZ);

			camera.SetPosition(Vector3{ state.m_Pos[0], state.m_Pos[1], state.m_Pos[2] });
			camera.SetYawPitch(math::ToRadians(state.m_YawDegree), math::ToRadians(state.m_PitchDegree));
			camera.SetFov(state.m_FovDegree);
			camera.SetNearFar(state.m_NearZ, state.m_FarZ);

			// Update camera
			camera.Update();
		}

		static void PushToController(CameraPanelState& s, CameraController& ctrl) noexcept
		{
			ctrl.SetParams(s.m_CtrlParams);
		}

		static void DrawVec3(const char* label, const Vector3& vec) noexcept
		{
			ImGui::Text("%s: (%.3f, %.3f, %.3f)", label, vec.m_X, vec.m_Y, vec.m_Z);
		}

		static void DrawMatrix4x4(const char* label, const Matrix& mat) noexcept
		{
			if (!ImGui::TreeNode(label))
			{
				return;
			}

			ImGui::Text("% .4f % .4f % .4f % .4f", mat.m_11, mat.m_12, mat.m_13, mat.m_14);
			ImGui::Text("% .4f % .4f % .4f % .4f", mat.m_21, mat.m_22, mat.m_23, mat.m_24);
			ImGui::Text("% .4f % .4f % .4f % .4f", mat.m_31, mat.m_32, mat.m_33, mat.m_34);
			ImGui::Text("% .4f % .4f % .4f % .4f", mat.m_41, mat.m_42, mat.m_43, mat.m_44);

			ImGui::TreePop();
		}

		static CameraBinding ResolveCameraBinding(
			CameraPanelState& state,
			DevelopGuiContext& context) noexcept
		{
			if (auto* rig = context.m_CameraRig)
			{
				if (rig->GetCameraCount() == 0)
				{
					return {};
				}
				state.m_SelectedCameraIndex = std::min(
					state.m_SelectedCameraIndex,
					rig->GetCameraCount() - 1);
				if (!state.m_Initialized)
				{
					state.m_SelectedCameraIndex = rig->GetActiveCameraIndex();
				}
				auto* slot = rig->GetCameraSlot(state.m_SelectedCameraIndex);
				return {
					.m_Camera = slot ? slot->m_Camera : nullptr,
					.m_Controller = slot ? slot->m_Controller : nullptr,
					.m_Slot = slot,
					.m_Index = state.m_SelectedCameraIndex,
				};
			}

			return {
				.m_Camera = context.m_Camera,
				.m_Controller = context.m_CameraController,
				.m_Slot = nullptr,
				.m_Index = 0,
			};
		}

		static void DrawCameraRigControls(
			CameraPanelState& state,
			CameraRig& rig) noexcept
		{
			if (rig.GetCameraCount() == 0)
			{
				ImGui::TextUnformatted("No cameras registered.");
				return;
			}

			const RenderViewID displayViewId = rig.GetDisplayViewId();
			const CameraRig::CameraSlot* displaySlot = rig.FindRenderViewSlot(displayViewId);
			const std::string displayViewIdText = devtools::EnumText(displayViewId);
			const char* displayPreview = displayViewId == RenderViewID::Main ?
				"Main Camera" :
				(displaySlot ? displaySlot->m_Name.c_str() : displayViewIdText.c_str());
			if (ImGui::BeginCombo("Display View", displayPreview))
			{
				const bool mainSelected = displayViewId == RenderViewID::Main;
				if (ImGui::Selectable("Main Camera", mainSelected))
				{
					GGLAB_UNUSED(rig.SetDisplayViewId(RenderViewID::Main));
				}
				if (mainSelected)
				{
					ImGui::SetItemDefaultFocus();
				}

				for (size_t index = 0; index < rig.GetCameraCount(); ++index)
				{
					const auto* slot = rig.GetCameraSlot(index);
					if (!slot || !slot->m_IsDebug || !slot->m_EnableRenderView ||
						!IsDebugCameraRenderViewID(slot->m_RenderViewId))
					{
						continue;
					}

					const bool selected = displayViewId == slot->m_RenderViewId;
					if (ImGui::Selectable(slot->m_Name.c_str(), selected))
					{
						GGLAB_UNUSED(rig.SetDisplayViewId(slot->m_RenderViewId));
					}
					if (selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			state.m_SelectedCameraIndex = std::min(
				state.m_SelectedCameraIndex,
				rig.GetCameraCount() - 1);

			const auto* selectedSlot = rig.GetCameraSlot(state.m_SelectedCameraIndex);
			const char* preview = selectedSlot ? selectedSlot->m_Name.c_str() : "Camera";
			if (ImGui::BeginCombo("Edit Camera", preview))
			{
				for (size_t index = 0; index < rig.GetCameraCount(); ++index)
				{
					const auto* slot = rig.GetCameraSlot(index);
					if (!slot)
					{
						continue;
					}
					const bool selected = index == state.m_SelectedCameraIndex;
					if (ImGui::Selectable(slot->m_Name.c_str(), selected))
					{
						state.m_SelectedCameraIndex = index;
						rig.SetActiveCameraIndex(index);
						state.m_Initialized = false;
					}
					if (selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			if (ImGui::Button("Add Debug Camera"))
			{
				state.m_SelectedCameraIndex = rig.AddDebugCameraFromActive();
				state.m_Initialized = false;
			}
			ImGui::SameLine();
			const bool canRemove = state.m_SelectedCameraIndex > 0;
			if (!canRemove)
			{
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Remove Camera"))
			{
				GGLAB_UNUSED(rig.RemoveCamera(state.m_SelectedCameraIndex));
				state.m_SelectedCameraIndex = rig.GetActiveCameraIndex();
				state.m_Initialized = false;
			}
			if (!canRemove)
			{
				ImGui::EndDisabled();
			}
		}
	}

	void CameraInspectorPanel::Draw(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<CameraPanelState>();
		if (context.m_CameraRig)
		{
			DrawCameraRigControls(state, *context.m_CameraRig);
			ImGui::Spacing();
		}

		CameraBinding binding = ResolveCameraBinding(state, context);
		Camera* camera = binding.m_Camera;
		CameraController* cameraCtrl = binding.m_Controller;

		if (!camera)
		{
			ImGui::TextUnformatted("No camera bound in DevelopGuiContext.");
			return;
		}

		// Get camera params when first time
		if (!state.m_Initialized || state.m_LastCamera != camera)
		{
			PullFromCamera(state, *camera);
			if (cameraCtrl)
			{
				PullFromController(state, *cameraCtrl);
			}
			state.m_LastCamera = camera;
			state.m_Initialized = true;
		}

		// AutoApply
		const bool allowSync = state.m_AutoApply;

		if (state.m_SyncFromCamera && allowSync)
		{
			PullFromCamera(state, *camera);
		}
		if (cameraCtrl && state.m_SyncFromController && allowSync)
		{
			PullFromController(state, *cameraCtrl);
		}

		ImGui::TextUnformatted(binding.m_Slot ? binding.m_Slot->m_Name.c_str() : "Camera");
		ImGui::Separator();

		ImGui::Checkbox("Auto Apply", &state.m_AutoApply);
		ImGui::SameLine();
		ImGui::Checkbox("Sync Camera", &state.m_SyncFromCamera);
		ImGui::SameLine();
		ImGui::Checkbox("Sync Controller", &state.m_SyncFromController);

		ImGui::Checkbox("Show Basis", &state.m_ShowBasis);
		ImGui::SameLine();
		ImGui::Checkbox("Show Matrices", &state.m_ShowMatrices);

		if (binding.m_Slot)
		{
			ImGui::SameLine();
			ImGui::Checkbox("Draw Frustum", &binding.m_Slot->m_ShowFrustum);
			if (binding.m_Slot->m_ShowFrustum)
			{
				ImGui::ColorEdit4("Frustum Color", &binding.m_Slot->m_FrustumColor.m_R);
			}
			if (binding.m_Slot->m_IsDebug && context.m_CameraRig)
			{
				bool enableRenderView = binding.m_Slot->m_EnableRenderView;
				if (ImGui::Checkbox("Build RenderView", &enableRenderView))
				{
					GGLAB_UNUSED(context.m_CameraRig->SetDebugRenderViewEnabled(
						binding.m_Index,
						enableRenderView));
				}
				if (binding.m_Slot->m_EnableRenderView &&
					IsDebugCameraRenderViewID(binding.m_Slot->m_RenderViewId))
				{
					ImGui::Text("RenderView: %s", devtools::EnumText(binding.m_Slot->m_RenderViewId).c_str());
					RenderViewVisibilityMode visibilityMode = binding.m_Slot->m_VisibilityMode;
					const std::string visibilityModeText = devtools::EnumText(visibilityMode);
					if (ImGui::BeginCombo("Visibility Mode", visibilityModeText.c_str()))
					{
						constexpr std::array modes = {
							RenderViewVisibilityMode::Self,
							RenderViewVisibilityMode::MainCamera,
							RenderViewVisibilityMode::IntersectionWithMainCamera,
							RenderViewVisibilityMode::None,
						};
						for (const RenderViewVisibilityMode mode : modes)
						{
							const bool selected = visibilityMode == mode;
							if (ImGui::Selectable(devtools::EnumText(mode).c_str(), selected))
							{
								binding.m_Slot->m_VisibilityMode = mode;
								visibilityMode = mode;
							}
							if (selected)
							{
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
				}
				else
				{
					ImGui::TextDisabled("RenderView: None");
				}
			}
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

		// Controller params
		ImGui::SeparatorText("Controller");
		if (cameraCtrl)
		{
			ctrlChanged |= ImGui::DragFloat("Movement Speed", &state.m_CtrlParams.m_MovementSpeed, 0.1f, 0.0f, 1000.0f);
			ctrlChanged |= ImGui::DragFloat("Sensitivity(Rad/Count)", &state.m_CtrlParams.m_MouseSensitivityRadPerCount, 0.00001f, 0.0001f, 0.005f, "%.4f");
			ctrlChanged |= ImGui::DragFloat("Accelerate Multiplier", &state.m_CtrlParams.m_AccelerateMultiplier, 0.05f, 1.0f, 20.0f);
			ctrlChanged |= ImGui::SliderFloat("SmoothStep T", &state.m_CtrlParams.m_SmoothStepT, 0.0f, 1.0f);

			if (ImGui::Button("Reset Velocity"))
			{
				cameraCtrl->ResetVelocity();
			}
		}
		else
		{
			ImGui::TextUnformatted("No CameraController bound (read-only camera).");
		}

		ImGui::Spacing();

		// Behavior
		if (state.m_AutoApply)
		{
			if (camChanged)
			{
				PushToCamera(state, *camera);
			}
			if (cameraCtrl && ctrlChanged)
			{
				PushToController(state, *cameraCtrl);
			}
		}
		else
		{
			ImGui::Separator();
			if (ImGui::Button("Apply"))
			{
				PushToCamera(state, *camera);
				if (cameraCtrl)
				{
					PushToController(state, *cameraCtrl);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Revert From Runtime"))
			{
				PullFromCamera(state, *camera);
				if (cameraCtrl)
				{
					PullFromController(state, *cameraCtrl);
				}
			}
		}

		// Read only infos
		ImGui::SeparatorText("Runtime Info");
		ImGui::Text("Aspect: %.4f", camera->GetAspect());

		if (state.m_ShowBasis)
		{
			DrawVec3("Forward", camera->GetForward());
			DrawVec3("Right", camera->GetRight());
			DrawVec3("Up", camera->GetUp());
		}

		if (state.m_ShowMatrices)
		{
			DrawMatrix4x4("View Matrix", camera->GetViewMatrix());
			DrawMatrix4x4("Proj Matrix", camera->GetProjMatrix());
		}
	}
}
