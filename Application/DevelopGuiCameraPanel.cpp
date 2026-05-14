#include "Precompiled.h"
#include "DevelopGuiCameraPanel.h"
#include "DevelopGuiContext.h"
#include "Camera.h"
#include "CameraController.h"
#include "MathUtils.h"

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

			// cached edit values
			float m_Pos[3] = { 0, 0, 0 };
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

		static void PullFromCamera(CameraPanelState& state, const Camera& camera) noexcept
		{
			const Vector3 p = camera.GetPosition();
			state.m_Pos[0] = p.x; state.m_Pos[1] = p.y; state.m_Pos[2] = p.z;

			state.m_YawDegree = utils::ToDegrees(camera.GetYaw());
			state.m_PitchDegree = utils::ToDegrees(camera.GetPitch());

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
			camera.SetYawPitch(utils::ToRadians(state.m_YawDegree), utils::ToRadians(state.m_PitchDegree));
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
			ImGui::Text("%s: (%.3f, %.3f, %.3f)", label, vec.x, vec.y, vec.z);
		}

		static void DrawMatrix4x4(const char* label, const Matrix& mat) noexcept
		{
			if (!ImGui::TreeNode(label))
			{
				return;
			}

			Matrix fmat{};
			DirectX::XMStoreFloat4x4(&fmat, mat);

			ImGui::Text("% .4f % .4f % .4f % .4f", fmat._11, fmat._12, fmat._13, fmat._14);
			ImGui::Text("% .4f % .4f % .4f % .4f", fmat._21, fmat._22, fmat._23, fmat._24);
			ImGui::Text("% .4f % .4f % .4f % .4f", fmat._31, fmat._32, fmat._33, fmat._34);
			ImGui::Text("% .4f % .4f % .4f % .4f", fmat._41, fmat._42, fmat._43, fmat._44);

			ImGui::TreePop();
		}
	}

	void DevelopGuiCameraPanel(DevelopGuiContext& context) noexcept
	{
		Camera* camera = context.m_Camera;
		CameraController* cameraCtrl = context.m_CameraController;

		if (!camera)
		{
			ImGui::TextUnformatted("No camera bound in DevelopGuiContext.");
			return;
		}

		auto& state = context.PanelState<CameraPanelState>();

		// Get camera params when first time
		if (!state.m_Initialized)
		{
			PullFromCamera(state, *camera);
			if (cameraCtrl)
			{
				PullFromController(state, *cameraCtrl);
			}
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

		ImGui::TextUnformatted("Camera");
		ImGui::Separator();

		ImGui::Checkbox("Auto Apply", &state.m_AutoApply);
		ImGui::SameLine();
		ImGui::Checkbox("Sync Camera", &state.m_SyncFromCamera);
		ImGui::SameLine();
		ImGui::Checkbox("Sync Controller", &state.m_SyncFromController);

		ImGui::Checkbox("Show Basis", &state.m_ShowBasis);
		ImGui::SameLine();
		ImGui::Checkbox("Show Matrices", &state.m_ShowMatrices);

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