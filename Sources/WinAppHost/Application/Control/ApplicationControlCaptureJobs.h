#pragma once
#include "Capture/CaptureJobTypes.h"
#include "Capture/CapturePlan.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	class GGLabAppRuntime;

	// Session-client owner of protocol capture jobs. Result and cancellation reach
	// only jobs this session admitted; interactive, startup and sequence jobs keep
	// their own owners and are not-found here. It retains identities, never results:
	// each identity holds a reserved receipt until this owner consumes it.
	class ApplicationControlCaptureJobs final
	{
	public:
		struct CreateInfo
		{
			std::function<CaptureAdmission(CapturePlan&&)> m_TrySubmit;
			std::function<CaptureCancelResult(uint64_t)> m_Cancel;
			// Borrowed published receipt, valid until it is consumed.
			std::function<const CaptureJobResult*(uint64_t)> m_FindResult;
			std::function<std::optional<CaptureJobResult>(uint64_t)> m_TakeResult;
			std::function<std::optional<CaptureJobSnapshot>(uint64_t)> m_GetSnapshot;
		};

		explicit ApplicationControlCaptureJobs(CreateInfo createInfo) noexcept;
		explicit ApplicationControlCaptureJobs(GGLabAppRuntime& runtime) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ApplicationControlCaptureJobs);
		~ApplicationControlCaptureJobs() = default;

		// Each call returns the complete protocol response for request id.
		[[nodiscard]] std::string Submit(uint64_t id, CapturePlan&& plan) noexcept;
		[[nodiscard]] std::string Query(uint64_t id, uint64_t jobId, bool consume) noexcept;
		[[nodiscard]] std::string Cancel(uint64_t id, uint64_t jobId) noexcept;
		[[nodiscard]] uint32_t GetOwnedJobCount() const noexcept
		{
			return static_cast<uint32_t>(m_Jobs.size());
		}

	private:
		CreateInfo m_Info;
		std::vector<uint64_t> m_Jobs;
	};
}
