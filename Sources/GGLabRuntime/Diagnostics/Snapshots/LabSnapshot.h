#pragma once
#include "Core/Math/Color.h"
#include "Core/Math/Vector.h"
#include "Diagnostics/SnapshotCommon.h"
#include "Graphics/RenderScene.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gglab
{
	struct LabIdSnapshot
	{
		bool IsValid() const noexcept { return !m_Name.empty(); }
		bool operator==(const LabIdSnapshot&) const noexcept = default;

		std::string m_Name;
	};

	struct LabParameterIdSnapshot
	{
		bool IsValid() const noexcept { return !m_Name.empty(); }
		bool operator==(const LabParameterIdSnapshot&) const noexcept = default;

		std::string m_Name;
	};

	enum class LabSnapshotRunState : uint8_t
	{
		Uninitialized,
		Loading,
		WarmingUp,
		Ready,
		Capturing,
		Completed,
		Failed,
	};

	struct LabDescriptorSnapshot
	{
		LabIdSnapshot m_Id;
		std::string m_DisplayName;
		std::string m_Category;
		std::string m_Description;
		uint32_t m_SchemaVersion = 1;
	};

	using LabSnapshotValue = std::variant<bool, int32_t, uint32_t, float, Vector3, Color>;

	enum class LabSnapshotParameterType : uint8_t
	{
		Bool,
		Int,
		UInt,
		Float,
		Enum,
		Vector3,
		Color,
	};

	enum class LabSnapshotParameterEditPolicy : uint8_t
	{
		Continuous,
		CommitOnEditEnd,
	};

	struct LabEnumItemSnapshot
	{
		int32_t m_Value = 0;
		std::string m_Name;
	};

	struct LabParameterDescSnapshot
	{
		LabParameterIdSnapshot m_Id;
		std::string m_Name;
		std::string m_Group;
		LabSnapshotParameterType m_Type = LabSnapshotParameterType::Bool;
		LabSnapshotParameterEditPolicy m_EditPolicy = LabSnapshotParameterEditPolicy::Continuous;
		LabSnapshotValue m_DefaultValue = false;
		std::optional<LabSnapshotValue> m_MinValue;
		std::optional<LabSnapshotValue> m_MaxValue;
		std::vector<LabEnumItemSnapshot> m_EnumItems;
	};

	struct LabRunConfigSnapshot
	{
		uint64_t m_RandomSeed = 0;
		uint32_t m_WarmupFrames = 8;
		bool m_UseFixedDeltaTime = false;
		float m_FixedDeltaTime = 1.0f / 60.0f;
	};

	enum class LabDiagnosticCheckStatus : uint8_t
	{
		Pending,
		Passed,
		Failed,
	};

	struct LabDiagnosticMetric
	{
		std::string m_Name;
		std::string m_Value;
	};

	struct LabDiagnosticCheck
	{
		std::string m_Name;
		LabDiagnosticCheckStatus m_Status = LabDiagnosticCheckStatus::Pending;
		std::string m_Detail;
	};

	struct LabDiagnosticsSnapshot
	{
		std::string m_Title;
		std::vector<LabDiagnosticMetric> m_Metrics;
		std::vector<LabDiagnosticCheck> m_Checks;
	};

	struct LabParameterSnapshot
	{
		LabParameterDescSnapshot m_Desc;
		LabSnapshotValue m_Value = false;
	};

	struct LabFrameFeedbackSnapshot
	{
		RenderSceneBuildStatus m_RenderSceneStatus = RenderSceneBuildStatus::GpuUploadFailed;
		uint64_t m_SubmittedFenceValue = 0;
		uint64_t m_ApplicationFrameIndex = 0;
		uint32_t m_BackBufferIndex = 0;
		bool m_HasFeedback = false;
	};

	struct LabSnapshot
	{
		std::vector<LabDescriptorSnapshot> m_AvailableLabs;
		LabIdSnapshot m_ActiveLabId;
		std::string m_ActiveLabName;
		LabIdSnapshot m_PendingLabId;
		std::string m_PendingLabName;
		std::string m_Category;
		std::string m_Description;
		uint32_t m_SchemaVersion = 0;
		LabSnapshotRunState m_State = LabSnapshotRunState::Uninitialized;
		LabRunConfigSnapshot m_RunConfig{};
		LabFrameFeedbackSnapshot m_LastFrame;
		uint64_t m_FrameInSession = 0;
		uint32_t m_WarmupFramesRemaining = 0;
		float m_EffectiveDeltaTime = 0.0f;
		std::vector<LabParameterSnapshot> m_Parameters;
		LabDiagnosticsSnapshot m_Diagnostics;
		float m_LoadingFraction = 0.0f;
		std::string m_LoadingStage;
		std::string m_LoadingDetail;
		std::string m_LastError;
		uint32_t m_RetiringSessionCount = 0;
		bool m_HasPendingCommands = false;
		bool m_HasPendingSession = false;
		bool m_IsHostActive = false;
	};

	class LabSnapshotSourceBase
	{
	public:
		virtual ~LabSnapshotSourceBase() = default;
		virtual LabSnapshot GetLabSnapshot() const noexcept = 0;
	};

	template <> struct SnapshotTraits<LabSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.LabSnapshot");
	};
}
