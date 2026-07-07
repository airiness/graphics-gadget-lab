#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/MathFoundationLabSession.h"
#include "Core/Math/BoundingVolumes.h"
#include "Core/Math/MathFunctions.h"
#include "Core/Math/Quaternion.h"
#include "Core/Math/Transform.h"
#include "Graphics/Camera.h"
#include "Graphics/DebugDraw/DebugDraw.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

namespace gglab
{
	namespace
	{
		enum class MathTopic : int32_t
		{
			Vector,
			MatrixTransform,
			Quaternion,
			Bounds,
			ProjectionFrustum,
			Degenerate,
		};

		const LabParameterId TopicId("math.topic");
		const LabParameterId EnableCameraInputId("math.camera.enable_input");
		const LabParameterId AnimateId("math.quaternion.animate");
		const LabParameterId InterpolationId("math.quaternion.interpolation");
		const StringID MathChannel("Math.Foundation");

		DebugDrawStyle MakeStyle(
			const Color& color,
			DebugDrawFillMode fillMode = DebugDrawFillMode::Wireframe,
			DebugDrawDepthMode depthMode = DebugDrawDepthMode::Tested) noexcept
		{
			return {
				.m_Color = color,
				.m_DepthMode = depthMode,
				.m_FillMode = fillMode,
				.m_Channel = MathChannel,
			};
		}

		Vector3 HomogeneousToPoint(const Vector4& value) noexcept
		{
			if (!math::IsFinite(value) || std::abs(value.m_W) <= 1.0e-6f)
			{
				return Vector3::Zero;
			}
			return Vector3(value.m_X, value.m_Y, value.m_Z) / value.m_W;
		}

		bool IsFinite(const Quaternion& value) noexcept
		{
			return math::IsFinite(value.m_X) && math::IsFinite(value.m_Y) &&
				math::IsFinite(value.m_Z) && math::IsFinite(value.m_W);
		}

		bool IsFinite(const Matrix& value) noexcept
		{
			for (const auto& row : value.m_M)
			{
				for (float component : row)
				{
					if (!math::IsFinite(component))
					{
						return false;
					}
				}
			}
			return true;
		}
	}

	MathFoundationLabSession::MathFoundationLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(
			GetDescriptor(),
			createInfo,
			std::make_unique<RenderPipelineForwardPBR>())
	{
		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = TopicId,
			.m_Name = "Validation Topic",
			.m_Group = "Math",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(MathTopic::Vector),
			.m_EnumItems = {
				{ .m_Value = int32_t(MathTopic::Vector), .m_Name = "Vector Operations" },
				{ .m_Value = int32_t(MathTopic::MatrixTransform), .m_Name = "Matrix / Transform" },
				{ .m_Value = int32_t(MathTopic::Quaternion), .m_Name = "Quaternion" },
				{ .m_Value = int32_t(MathTopic::Bounds), .m_Name = "Bounds" },
				{ .m_Value = int32_t(MathTopic::ProjectionFrustum), .m_Name = "Projection / Frustum" },
				{ .m_Value = int32_t(MathTopic::Degenerate), .m_Name = "Degenerate Inputs" },
			},
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = EnableCameraInputId,
			.m_Name = "Enable Camera Input",
			.m_Group = "Camera",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = true,
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = AnimateId,
			.m_Name = "Animate Interpolation",
			.m_Group = "Quaternion",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = true,
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = InterpolationId,
			.m_Name = "Interpolation",
			.m_Group = "Quaternion",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.5f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(1.0f),
		}));

		ApplyImmediateParameters();
		ApplyCameraPreset();
	}

	void MathFoundationLabSession::OnEnter() noexcept
	{
		m_Services.m_DebugDraw->SetChannelEnabled(MathChannel, true);
	}

	void MathFoundationLabSession::OnExit() noexcept
	{
		if (auto* debugDraw = m_Services.m_DebugDraw)
		{
			debugDraw->ClearChannel(MathChannel);
		}
	}

	void MathFoundationLabSession::Update(float deltaTime) noexcept
	{
		m_ElapsedTime += std::max(deltaTime, 0.0f);
		if (m_EnableCameraInput)
		{
			UpdateCamera(deltaTime);
		}
		else
		{
			GetCamera().Update();
		}

		auto& debugDraw = *m_Services.m_DebugDraw;
		debugDraw.Grid(
			Vector3(0.0f, -1.0f, 6.0f), Vector3::UnitY, Vector3::UnitX,
			8.0f, 16, MakeStyle(Color(0.18f, 0.18f, 0.18f, 1.0f)));

		const auto topic = static_cast<MathTopic>(
			GetParameters().Get(TopicId, int32_t(MathTopic::Vector)));
		switch (topic)
		{
		case MathTopic::MatrixTransform:
			DrawMatrixValidation(debugDraw);
			break;
		case MathTopic::Quaternion:
		{
			const bool animate = GetParameters().Get(AnimateId, true);
			const float t = animate ?
				0.5f + 0.5f * std::sin(m_ElapsedTime) :
				GetParameters().Get(InterpolationId, 0.5f);
			DrawQuaternionValidation(debugDraw, t);
			break;
		}
		case MathTopic::Bounds:
			DrawBoundsValidation(debugDraw);
			break;
		case MathTopic::ProjectionFrustum:
			DrawProjectionValidation(debugDraw);
			break;
		case MathTopic::Degenerate:
			DrawDegenerateValidation(debugDraw);
			break;
		case MathTopic::Vector:
		default:
			DrawVectorValidation(debugDraw);
			break;
		}
	}

	void MathFoundationLabSession::ApplyImmediateParameters() noexcept
	{
		m_EnableCameraInput = GetParameters().Get(EnableCameraInputId, true);
	}

	void MathFoundationLabSession::ApplyCameraPreset() noexcept
	{
		GetCamera().LookAt(Vector3(8.0f, 6.0f, -9.0f), Vector3(0.0f, 1.0f, 6.0f));
		GetCamera().SetNearFar(0.1f, 100.0f);
		GetCamera().Update();
	}

	void MathFoundationLabSession::DrawVectorValidation(DebugDrawContext& debugDraw) noexcept
	{
		const Vector3 origin(0.0f, 0.0f, 5.0f);
		const Vector3 a(2.5f, 0.5f, 0.5f);
		const Vector3 b(-0.5f, 2.0f, 1.0f);
		const Vector3 cross = a.Cross(b).Normalized() * 2.0f;
		debugDraw.Axes(math::CreateTranslation(origin), 1.2f, 0.18f,
			MakeStyle(Color::White, DebugDrawFillMode::Solid));
		debugDraw.Arrow(origin, origin + a, 0.35f, MakeStyle(Color::Red, DebugDrawFillMode::Solid));
		debugDraw.Arrow(origin, origin + b, 0.35f, MakeStyle(Color::Green, DebugDrawFillMode::Solid));
		debugDraw.Arrow(origin, origin + a + b, 0.35f, MakeStyle(Color::Yellow, DebugDrawFillMode::Solid));
		debugDraw.Arrow(origin, origin + cross, 0.35f, MakeStyle(Color::Cyan, DebugDrawFillMode::Solid));
		const std::array parallelogram = { origin + a, origin + a + b, origin + b };
		debugDraw.Polyline(parallelogram, false, MakeStyle(Color::Silver));
	}

	void MathFoundationLabSession::DrawMatrixValidation(DebugDrawContext& debugDraw) noexcept
	{
		const Matrix scale = math::CreateScale(Vector3(1.5f, 0.65f, 0.9f));
		const Matrix rotation = math::CreateFromQuaternion(
			math::CreateFromYawPitchRoll(0.65f, 0.3f, 0.15f));
		const Matrix translation = math::CreateTranslation(Vector3(-1.5f, 0.5f, 6.0f));
		const Matrix expectedOrder = scale * rotation * translation;
		const Matrix reversedOrder = translation * rotation * scale;

		debugDraw.Obb(expectedOrder, Vector3::One,
			MakeStyle(Color::Green));
		debugDraw.Axes(expectedOrder, 1.6f, 0.2f,
			MakeStyle(Color::White, DebugDrawFillMode::Solid));
		debugDraw.Obb(reversedOrder, Vector3::One,
			MakeStyle(Color::Red));

		const Vector3 localPoint(1.0f, 0.0f, 0.0f);
		const Vector3 transformedPoint = math::TransformPoint(localPoint, translation);
		const Vector3 transformedDirection = math::TransformDirection(localPoint, translation);
		debugDraw.Point(transformedPoint, 0.18f, MakeStyle(Color::Yellow));
		debugDraw.Arrow(Vector3::Zero, transformedDirection, 0.2f,
			MakeStyle(Color::Cyan, DebugDrawFillMode::Solid));
		debugDraw.Line(localPoint, transformedPoint, MakeStyle(Color::Gold));
	}

	void MathFoundationLabSession::DrawQuaternionValidation(
		DebugDrawContext& debugDraw, float interpolationT) noexcept
	{
		const Vector3 origin(0.0f, 0.0f, 6.0f);
		const Vector3 target = Vector3(0.8f, 0.55f, 0.35f).Normalized();
		const Quaternion targetRotation = math::RotationFromTo(Vector3::Forward, target);
		const Quaternion interpolated = math::Slerp(
			Quaternion::Identity, targetRotation, std::clamp(interpolationT, 0.0f, 1.0f));
		const Vector3 rotatedForward = math::TransformDirection(
			Vector3::Forward, math::CreateFromQuaternion(interpolated)).Normalized();

		debugDraw.Arrow(origin, origin + Vector3::Forward * 3.0f, 0.35f,
			MakeStyle(Color::Blue, DebugDrawFillMode::Solid));
		debugDraw.Arrow(origin, origin + target * 3.0f, 0.35f,
			MakeStyle(Color::Green, DebugDrawFillMode::Solid));
		debugDraw.Arrow(origin, origin + rotatedForward * 3.0f, 0.35f,
			MakeStyle(Color::Gold, DebugDrawFillMode::Solid));
		const Matrix interpolatedTransform =
			math::CreateFromQuaternion(interpolated) * math::CreateTranslation(origin);
		debugDraw.Axes(interpolatedTransform, 1.4f, 0.18f,
			MakeStyle(Color::White, DebugDrawFillMode::Solid));
	}

	void MathFoundationLabSession::DrawBoundsValidation(DebugDrawContext& debugDraw) noexcept
	{
		const math::Aabb sourceAabb(Vector3(-2.5f, 0.0f, 5.0f), Vector3(1.2f, 0.7f, 0.9f));
		const math::Sphere sourceSphere(Vector3(2.0f, 0.0f, 5.0f), 1.0f);
		const Matrix transform = math::CreateTransformMatrix(
			Vector3(1.5f, 0.7f, 1.1f),
			math::CreateFromYawPitchRoll(0.55f, 0.2f, 0.0f),
			Vector3(0.8f, 1.0f, 3.0f));
		const math::Aabb transformedAabb = math::Transform(sourceAabb, transform);
		const math::Sphere transformedSphere = math::Transform(sourceSphere, transform);

		debugDraw.Aabb(sourceAabb, MakeStyle(Color::White));
		debugDraw.Sphere(sourceSphere.m_Center, sourceSphere.m_Radius, MakeStyle(Color::White));
		debugDraw.Aabb(transformedAabb, MakeStyle(Color::Yellow));
		debugDraw.Sphere(transformedSphere.m_Center, transformedSphere.m_Radius, MakeStyle(Color::Cyan));
		debugDraw.Obb(math::CreateTranslation(sourceAabb.m_Center) * transform, sourceAabb.m_Extents,
			MakeStyle(Color::Green));
	}

	void MathFoundationLabSession::DrawProjectionValidation(DebugDrawContext& debugDraw) noexcept
	{
		const Vector3 eye(-2.0f, 1.5f, 1.0f);
		const Vector3 target(0.0f, 0.5f, 6.0f);
		const Matrix view = math::CreateLookAtLH(eye, target, Vector3::UnitY);
		const Matrix projection = math::CreatePerspectiveFieldOfViewLH(
			math::ToRadians(50.0f), 1.6f, 0.5f, 7.0f);
		const Matrix inverseViewProjection = math::Inverse(view * projection);
		constexpr std::array<Vector4, 8> clipCorners = {
			Vector4(-1.0f, 1.0f, 0.0f, 1.0f), Vector4(1.0f, 1.0f, 0.0f, 1.0f),
			Vector4(1.0f, -1.0f, 0.0f, 1.0f), Vector4(-1.0f, -1.0f, 0.0f, 1.0f),
			Vector4(-1.0f, 1.0f, 1.0f, 1.0f), Vector4(1.0f, 1.0f, 1.0f, 1.0f),
			Vector4(1.0f, -1.0f, 1.0f, 1.0f), Vector4(-1.0f, -1.0f, 1.0f, 1.0f),
		};
		std::array<Vector3, 8> worldCorners{};
		for (size_t index = 0; index < clipCorners.size(); ++index)
		{
			worldCorners[index] = HomogeneousToPoint(
				math::Transform(clipCorners[index], inverseViewProjection));
		}
		debugDraw.Frustum(worldCorners, MakeStyle(Color::Gold));
		debugDraw.Point(eye, 0.2f, MakeStyle(Color::Cyan));
		debugDraw.Arrow(eye, target, 0.35f, MakeStyle(Color::Green, DebugDrawFillMode::Solid));
	}

	void MathFoundationLabSession::DrawDegenerateValidation(DebugDrawContext& debugDraw) noexcept
	{
		const Vector3 origin(0.0f, 0.0f, 5.0f);
		Vector3 rawZero = Vector3::Zero;
		rawZero.Normalize();
		const bool rawNormalizeRejected = !math::IsFinite(rawZero);
		const Vector3 safeZero = math::SafeNormalize(Vector3::Zero, Vector3::UnitX);
		const Vector3 safeNearZero = math::SafeNormalize(Vector3(1.0e-10f), Vector3::UnitY);
		const Quaternion opposite = math::RotationFromTo(Vector3::Forward, Vector3::Backward);
		const Vector3 oppositeResult = math::TransformDirection(
			Vector3::Forward, math::CreateFromQuaternion(opposite));
		const Quaternion zeroDirectionRotation =
			math::RotationFromTo(Vector3::Zero, Vector3::Forward);
		const Matrix singularInverse = math::Inverse(
			math::CreateScale(Vector3(1.0f, 0.0f, 1.0f)));

		debugDraw.Arrow(origin, origin + safeZero * 2.0f, 0.3f,
			MakeStyle(Color::Green, DebugDrawFillMode::Solid));
		debugDraw.Arrow(origin + Vector3(0.0f, 0.8f, 0.0f),
			origin + Vector3(0.0f, 0.8f, 0.0f) + safeNearZero * 2.0f, 0.3f,
			MakeStyle(Color::Cyan, DebugDrawFillMode::Solid));
		debugDraw.Arrow(origin + Vector3(0.0f, 1.6f, 0.0f),
			origin + Vector3(0.0f, 1.6f, 0.0f) + oppositeResult * 2.0f, 0.3f,
			MakeStyle(Color::Gold, DebugDrawFillMode::Solid));
		debugDraw.Point(origin + Vector3(-1.0f, 0.0f, 0.0f), 0.25f,
			MakeStyle(rawNormalizeRejected ? Color::Red : Color::Green));
		debugDraw.Point(origin + Vector3(-1.0f, 0.8f, 0.0f), 0.25f,
			MakeStyle(IsFinite(zeroDirectionRotation) ? Color::Green : Color::Red));
		debugDraw.Point(origin + Vector3(-1.0f, 1.6f, 0.0f), 0.25f,
			MakeStyle(IsFinite(singularInverse) ? Color::Green : Color::Red));

		const float nan = std::numeric_limits<float>::quiet_NaN();
		debugDraw.Line(Vector3(nan, 0.0f, 0.0f), Vector3::Zero, MakeStyle(Color::Red));
	}

	LabId MathFoundationLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.math_foundation");
	}

	LabDescriptor MathFoundationLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Math Foundation",
			.m_Category = "Foundation",
			.m_Description = "Visual validation for vectors, matrices, transforms, quaternions, bounds, projection and degenerate inputs.",
			.m_Kind = LabKind::Scene,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> MathFoundationLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<MathFoundationLabSession>(createInfo);
	}
}
