#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace gglab
{
	struct AssetLoadProgress;

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

	struct LoadingProgressStep
	{
		LoadingStatus m_Status = LoadingStatus::Preparing;
		float m_Fraction = 0.0f;
		std::string_view m_Stage;
		std::string_view m_Detail;
	};

	class LoadingProgressBuilder
	{
	public:
		explicit LoadingProgressBuilder(std::string title = {}) noexcept;

		void AddStep(float weight, const LoadingProgressStep& step) noexcept;
		void AddAssetStep(
			float weight,
			const AssetLoadProgress& progress,
			std::string_view detail = {}) noexcept;
		void AddCompletedStep(float weight) noexcept;

		[[nodiscard]] LoadingProgress Build() const noexcept;

	private:
		std::string m_Title;
		std::string m_CurrentStage;
		std::string m_CurrentDetail;
		float m_TotalWeight = 0.0f;
		float m_WeightedFraction = 0.0f;
		bool m_HasPreparingStep = false;
		bool m_HasFailedStep = false;
		bool m_HasSelectedStep = false;
	};
}
