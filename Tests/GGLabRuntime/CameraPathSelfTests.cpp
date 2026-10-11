#include "CameraPathSelfTests.h"

#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/CameraController.h"
#include "GGLabRuntime/Graphics/CameraPath.h"
#include "GGLabRuntime/Graphics/CameraRig.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

// Headless contracts for deterministic camera paths: validation, frame-indexed
// evaluation, shot cuts and application to the main camera without unintended
// temporal resets.
namespace gglab
{
	namespace
	{
		[[nodiscard]] bool Near(const Vector3& lhs, const Vector3& rhs) noexcept
		{
			return (lhs - rhs).Length() < 1.0e-5f;
		}

		[[nodiscard]] CameraPathKey MakeKey(
			uint32_t frame, float x, bool cut = false, float fov = 60.0f) noexcept
		{
			return {
				.m_Frame = frame,
				.m_Position = { x, 1.0f, 0.0f },
				.m_Target = { x, 1.0f, 10.0f },
				.m_VerticalFovDegrees = fov,
				.m_Cut = cut,
			};
		}

		[[nodiscard]] CameraPath MakePath(std::vector<CameraPathKey> keys,
			CameraPathInterpolation interpolation = CameraPathInterpolation::Linear)
		{
			return {
				.m_Id = "SEQ_Test",
				.m_Name = "Test",
				.m_Version = 3,
				.m_Interpolation = interpolation,
				.m_NearPlane = 0.2f,
				.m_FarPlane = 500.0f,
				.m_ManualEV100 = 12.0f,
				.m_Keys = std::move(keys),
			};
		}

		void RunValidationTests(SelfTestContext& context) noexcept
		{
			context.Check(IsCameraPathValid(MakePath({ MakeKey(0, 0.0f), MakeKey(9, 1.0f) })) &&
				GetCameraPathFrameCount(MakePath({ MakeKey(0, 0.0f), MakeKey(9, 1.0f) })) == 10,
				"A path with a frame-0 key and increasing frames is valid; frame count is last key + 1");

			CameraPath noVersion = MakePath({ MakeKey(0, 0.0f) });
			noVersion.m_Version = 0;
			CameraPath vertical = MakePath({ MakeKey(0, 0.0f) });
			vertical.m_Keys[0].m_Target = { 0.0f, 20.0f, 0.0f };
			CameraPath clampedFov = MakePath({ MakeKey(0, 0.0f, false, 180.0f) });
			CameraPath clampedNear = MakePath({ MakeKey(0, 0.0f) });
			clampedNear.m_NearPlane = 0.0f;
			context.Check(!IsCameraPathValid(MakePath({})) &&
				!IsCameraPathValid(MakePath({ MakeKey(1, 0.0f) })) &&
				!IsCameraPathValid(MakePath({ MakeKey(0, 0.0f), MakeKey(0, 1.0f) })) &&
				!IsCameraPathValid(noVersion) && !IsCameraPathValid(vertical) &&
				!IsCameraPathValid(clampedFov) && !IsCameraPathValid(clampedNear) &&
				GetCameraPathFrameCount(noVersion) == 0 &&
				!EvaluateCameraPath(noVersion, 0),
				"Paths reject missing keys, non-zero first frames, non-increasing frames, a zero "
				"version, a view beyond the pitch limit and values the camera would clamp");
		}

		void RunEvaluationTests(SelfTestContext& context) noexcept
		{
			const CameraPath linear = MakePath({ MakeKey(0, 0.0f), MakeKey(10, 10.0f) });
			const std::optional<CameraPathPose> start = EvaluateCameraPath(linear, 0);
			const std::optional<CameraPathPose> middle = EvaluateCameraPath(linear, 4);
			const std::optional<CameraPathPose> end = EvaluateCameraPath(linear, 10);
			context.Check(start && start->m_Cut && middle && !middle->m_Cut &&
				Near(middle->m_Position, { 4.0f, 1.0f, 0.0f }) &&
				Near(middle->m_Target, { 4.0f, 1.0f, 10.0f }) && end &&
				Near(end->m_Position, { 10.0f, 1.0f, 0.0f }) && !EvaluateCameraPath(linear, 11),
				"Linear paths move at constant velocity, frame 0 is a cut and frames past the end "
				"do not exist");

			// Collinear, evenly spaced keys: uniform Catmull-Rom reproduces the line inside
			// the shot and passes through every key.
			const CameraPath spline = MakePath(
				{ MakeKey(0, 0.0f), MakeKey(10, 10.0f), MakeKey(20, 20.0f), MakeKey(30, 30.0f) },
				CameraPathInterpolation::CatmullRom);
			const std::optional<CameraPathPose> splineKey = EvaluateCameraPath(spline, 20);
			const std::optional<CameraPathPose> splineMiddle = EvaluateCameraPath(spline, 15);
			context.Check(splineKey && Near(splineKey->m_Position, { 20.0f, 1.0f, 0.0f }) &&
				splineMiddle && Near(splineMiddle->m_Position, { 15.0f, 1.0f, 0.0f }),
				"Catmull-Rom paths pass through keys and keep evenly spaced collinear motion");

			const CameraPath shots = MakePath({ MakeKey(0, 0.0f), MakeKey(4, 4.0f),
				MakeKey(8, 100.0f, true), MakeKey(10, 102.0f) }, CameraPathInterpolation::CatmullRom);
			const std::optional<CameraPathPose> held = EvaluateCameraPath(shots, 6);
			const std::optional<CameraPathPose> cut = EvaluateCameraPath(shots, 8);
			const std::optional<CameraPathPose> after = EvaluateCameraPath(shots, 9);
			const std::optional<CameraPathPose> firstShot = EvaluateCameraPath(shots, 2);
			context.Check(held && !held->m_Cut && Near(held->m_Position, { 4.0f, 1.0f, 0.0f }) &&
				cut && cut->m_Cut && Near(cut->m_Position, { 100.0f, 1.0f, 0.0f }) &&
				after && !after->m_Cut && Near(after->m_Position, { 101.0f, 1.0f, 0.0f }) &&
				firstShot && Near(firstShot->m_Position, { 2.0f, 1.0f, 0.0f }),
				"Interpolation never crosses a cut: a shot holds its last pose until the cut "
				"frame, which is the only cut pose");
		}

		void RunCameraRigTests(SelfTestContext& context) noexcept
		{
			CameraRig unattached;
			context.Check(!unattached.SetCameraPaths({ MakePath({ MakeKey(0, 0.0f) }) }),
				"Camera paths require an attached main camera");

			Camera camera(Camera::CreateInfo{});
			CameraController controller(CameraController::CreateInfo{});
			CameraRig rig;
			rig.AttachMainCamera(camera, controller);
			CameraPath duplicate = MakePath({ MakeKey(0, 0.0f) });
			context.Check(!rig.SetCameraPaths({ duplicate, duplicate }) &&
				rig.GetCameraPaths().empty(),
				"Duplicate path ids are rejected without registering any path");

			const CameraPath path = MakePath({ MakeKey(0, 0.0f), MakeKey(4, 4.0f),
				MakeKey(5, 50.0f, true, 45.0f), MakeKey(6, 51.0f, false, 45.0f) });
			context.Check(rig.SetCameraPaths({ path }) && rig.FindCameraPath("SEQ_Test") &&
				!rig.FindCameraPath("SEQ_Missing"),
				"Valid paths register and are found by id");

			const uint64_t initialSerial = camera.GetTemporalResetSerial();
			const std::optional<CameraPathPose> first = rig.ApplyCameraPathFrame("SEQ_Test", 0);
			const uint64_t afterFirst = camera.GetTemporalResetSerial();
			const std::optional<CameraPathPose> moving = rig.ApplyCameraPathFrame("SEQ_Test", 2);
			const uint64_t afterMoving = camera.GetTemporalResetSerial();
			const Vector3 movingPosition = camera.GetPosition();
			const Vector3 movingForward = camera.GetForward();
			const std::optional<CameraPathPose> cut = rig.ApplyCameraPathFrame("SEQ_Test", 5);
			context.Check(first && afterFirst == initialSerial + 1 && moving &&
				afterMoving == afterFirst && Near(movingPosition, { 2.0f, 1.0f, 0.0f }) &&
				Near(movingForward, { 0.0f, 0.0f, 1.0f }) && cut &&
				camera.GetTemporalResetSerial() == afterMoving + 1 &&
				camera.GetFov() == 45.0f && camera.GetNear() == 0.2f &&
				camera.GetFar() == 500.0f && camera.GetManualEV100() == 12.0f &&
				rig.GetDisplayViewId() == RenderViewID::Main,
				"Frame 0 and cut keys reset temporal history; other frames move the camera "
				"without a reset and apply the path's projection and exposure");
			context.Check(!rig.ApplyCameraPathFrame("SEQ_Test", 7) &&
				!rig.ApplyCameraPathFrame("SEQ_Missing", 0),
				"Missing paths and frames past the end are not applied");
		}
	}

	void RunCameraPathSelfTests(SelfTestContext& context) noexcept
	{
		RunValidationTests(context);
		RunEvaluationTests(context);
		RunCameraRigTests(context);
	}
}
