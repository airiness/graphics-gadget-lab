#pragma once
#include "Application/Lab/LabParameter.h"
#include "Application/Lab/LabRunConfig.h"
#include "Application/Lab/LabTypes.h"
#include "Diagnostics/SnapshotCommon.h"
#include "Graphics/RenderScene.h"

namespace gglab
{
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
		LabParameterDesc m_Desc;
		LabValue m_Value = false;
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
		std::vector<LabDescriptor> m_AvailableLabs;
		LabId m_ActiveLabId;
		std::string m_ActiveLabName;
		std::string m_Category;
		std::string m_Description;
		uint32_t m_SchemaVersion = 0;
		LabRunState m_State = LabRunState::Uninitialized;
		LabRunConfig m_RunConfig{};
		LabFrameFeedbackSnapshot m_LastFrame;
		uint64_t m_FrameInSession = 0;
		uint32_t m_WarmupFramesRemaining = 0;
		float m_EffectiveDeltaTime = 0.0f;
		std::vector<LabParameterSnapshot> m_Parameters;
		LabDiagnosticsSnapshot m_Diagnostics;
		std::string m_LastError;
		bool m_HasPendingCommands = false;
		bool m_IsHostActive = false;
	};

	template<>
	struct SnapshotTraits<LabSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.LabSnapshot");
	};
}
