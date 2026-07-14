#pragma once

#include <cstdint>
#include <string>

namespace gglab
{
	enum class LoadingStatus : uint8_t
	{
		Preparing,
		Ready,
		Failed,
	};

	struct LoadingProgress
	{
		LoadingStatus m_Status = LoadingStatus::Ready;
		float m_Fraction = 1.0f;
		std::string m_Title;
		std::string m_Stage;
		std::string m_Detail;

		[[nodiscard]] bool IsPreparing() const noexcept
		{
			return m_Status == LoadingStatus::Preparing;
		}

		[[nodiscard]] bool IsReady() const noexcept
		{
			return m_Status == LoadingStatus::Ready;
		}

		[[nodiscard]] bool HasFailed() const noexcept
		{
			return m_Status == LoadingStatus::Failed;
		}

		[[nodiscard]] static LoadingProgress Ready() noexcept
		{
			return {};
		}
	};
}
