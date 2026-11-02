#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIReceiverObservation.h"

#include <cstdint>
#include <memory>
#include <string>

namespace gglab
{
	class GGLabAppRuntime;

	class GIReceiverInspectionControlBase
	{
	public:
		virtual ~GIReceiverInspectionControlBase() = default;
		[[nodiscard]] virtual bool Inspect(GIReceiverInspectionRequest request) noexcept = 0;
		virtual void Cancel() noexcept = 0;
		[[nodiscard]] virtual bool IsPending() const noexcept = 0;
		[[nodiscard]] virtual std::shared_ptr<const GIReceiverInspectionResult> GetResult() const noexcept = 0;
		[[nodiscard]] virtual const std::string& GetStatus() const noexcept = 0;
		[[nodiscard]] virtual const std::string& GetImagePath() const noexcept = 0;
	};

	class ToolingGIReceiverInspection final : public GIReceiverInspectionControlBase
	{
	public:
		explicit ToolingGIReceiverInspection(GGLabAppRuntime& runtime) noexcept : m_Runtime(runtime) {}
		[[nodiscard]] bool Inspect(GIReceiverInspectionRequest request) noexcept override;
		void Cancel() noexcept override;
		void Update() noexcept;
		[[nodiscard]] bool IsPending() const noexcept override { return m_JobId != 0; }
		[[nodiscard]] std::shared_ptr<const GIReceiverInspectionResult> GetResult() const noexcept override { return m_Result; }
		[[nodiscard]] const std::string& GetStatus() const noexcept override { return m_Status; }
		[[nodiscard]] const std::string& GetImagePath() const noexcept override { return m_ImagePath; }

	private:
		GGLabAppRuntime& m_Runtime;
		uint64_t m_JobId = 0;
		bool m_Discard = false;
		std::shared_ptr<const GIReceiverInspectionResult> m_Result;
		std::string m_Status = "No receiver inspection requested.";
		std::string m_ImagePath;
	};
}
