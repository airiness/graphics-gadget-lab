#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIProbePicking.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace gglab
{
	class GGLabAppRuntime;

	struct GIProbePickingDisplay
	{
		std::shared_ptr<const GIFieldSummary> m_Summary;
		float m_DpiScale = 1;
		std::string m_ContentId;
	};

	class GIProbePickingControlBase
	{
	public:
		virtual ~GIProbePickingControlBase() = default;
		[[nodiscard]] virtual const GIProbePickingDisplay& GetDisplay() const noexcept = 0;
		[[nodiscard]] virtual bool Pick(GIProbePickingRequest request) noexcept = 0;
		virtual void Cancel() noexcept = 0;
		[[nodiscard]] virtual std::shared_ptr<const GIProbePickingResult> GetResult() const noexcept = 0;
		[[nodiscard]] virtual const std::string& GetStatus() const noexcept = 0;
	};

	class ToolingGIProbePicking final : public GIProbePickingControlBase
	{
	public:
		explicit ToolingGIProbePicking(GGLabAppRuntime& runtime) noexcept : m_Runtime(runtime) {}
		[[nodiscard]] const GIProbePickingDisplay& GetDisplay() const noexcept override { return m_Display; }
		[[nodiscard]] bool Pick(GIProbePickingRequest request) noexcept override;
		void Cancel() noexcept override;
		void Update() noexcept;
		void OnPresented(std::shared_ptr<const GIFieldSummary> summary, float dpiScale, std::string contentId) noexcept;
		[[nodiscard]] std::shared_ptr<const GIProbePickingResult> GetResult() const noexcept override { return m_Result; }
		[[nodiscard]] const std::string& GetStatus() const noexcept override { return m_Status; }

	private:
		void SubmitLatest() noexcept;
		GGLabAppRuntime& m_Runtime;
		GIProbePickingDisplay m_Display;
		std::optional<GIProbePickingRequest> m_Latest;
		std::string m_SourceContent;
		uint64_t m_JobId = 0;
		bool m_Discard = false;
		std::shared_ptr<const GIProbePickingResult> m_Result;
		std::string m_Status = "No probe pick requested.";
	};
}
