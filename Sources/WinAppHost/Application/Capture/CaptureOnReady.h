#pragma once
#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

namespace gglab
{
	// One startup job owns its admission retry and terminal consumption independently
	// of interactive history. A timeout never claims that publication was prevented.
	class CaptureOnReady final
	{
	public:
		enum class State : uint8_t
		{
			Pending,
			Completed,
			Failed,
			Cancelled,
			Rejected,
			TimedOut,
		};
		struct CreateInfo
		{
			CapturePlan m_Plan;
			std::chrono::duration<double> m_Timeout{ 120.0 };
			std::function<CaptureAdmission(CapturePlan&&)> m_TrySubmit;
			std::function<CaptureCancelResult(uint64_t)> m_Cancel;
			std::function<std::optional<CaptureJobResult>(uint64_t)> m_TakeResult;
		};

		explicit CaptureOnReady(CreateInfo createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(CaptureOnReady);
		// Called before a frame; Busy retains the plan without creating an identity.
		void TryAdmit() noexcept;
		// Called after a frame; starts the wall-clock deadline on the first call.
		State Poll(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept;
		// Shutdown may finish a timed-out job whose writer had already claimed publication.
		void CollectResult() noexcept;
		[[nodiscard]] uint64_t GetJobId() const noexcept { return m_Admission.m_JobId; }
		[[nodiscard]] CaptureAdmission GetAdmission() const noexcept { return m_Admission; }
		[[nodiscard]] std::optional<CaptureCancelResult> GetCancellation() const noexcept { return m_Cancellation; }
		[[nodiscard]] const std::optional<CaptureJobResult>& GetResult() const noexcept { return m_Result; }

	private:
		CreateInfo m_Info;
		State m_State = State::Pending;
		CaptureAdmission m_Admission;
		std::optional<std::chrono::steady_clock::time_point> m_Deadline;
		std::optional<CaptureCancelResult> m_Cancellation;
		std::optional<CaptureJobResult> m_Result;
	};
}
