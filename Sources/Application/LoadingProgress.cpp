#include "Core/Precompiled.h"
#include "Application/LoadingProgress.h"
#include "Graphics/Asset/Loading/AssetLoadProgress.h"

namespace gglab
{
	LoadingProgressBuilder::LoadingProgressBuilder(std::string title) noexcept :
		m_Title(std::move(title))
	{}

	void LoadingProgressBuilder::AddStep(
		float weight,
		const LoadingProgressStep& step) noexcept
	{
		weight = std::max(weight, 0.0f);
		m_TotalWeight += weight;
		m_WeightedFraction += weight * std::clamp(step.m_Fraction, 0.0f, 1.0f);

		if (step.m_Status == LoadingStatus::Failed)
		{
			if (!m_HasFailedStep)
			{
				m_CurrentStage = step.m_Stage;
				m_CurrentDetail = step.m_Detail;
				m_HasSelectedStep = true;
			}
			m_HasFailedStep = true;
			return;
		}

		if (step.m_Status == LoadingStatus::Preparing)
		{
			m_HasPreparingStep = true;
			if (!m_HasFailedStep && !m_HasSelectedStep)
			{
				m_CurrentStage = step.m_Stage;
				m_CurrentDetail = step.m_Detail;
				m_HasSelectedStep = true;
			}
		}
	}

	void LoadingProgressBuilder::AddAssetStep(
		float weight,
		const AssetLoadProgress& progress,
		std::string_view detail) noexcept
	{
		LoadingStatus status = LoadingStatus::Preparing;
		if (progress.IsReady())
		{
			status = LoadingStatus::Ready;
		}
		else if (progress.HasFailed())
		{
			status = LoadingStatus::Failed;
		}

		std::string combinedDetail(detail);
		if (!progress.m_Detail.empty())
		{
			combinedDetail = combinedDetail.empty() ? progress.m_Detail :
				std::format("{} | {}", combinedDetail, progress.m_Detail);
		}

		AddStep(weight, {
			.m_Status = status,
			.m_Fraction = progress.m_Fraction,
			.m_Stage = progress.m_Stage,
			.m_Detail = combinedDetail,
		});
	}

	void LoadingProgressBuilder::AddCompletedStep(float weight) noexcept
	{
		AddStep(weight, {
			.m_Status = LoadingStatus::Ready,
			.m_Fraction = 1.0f,
		});
	}

	LoadingProgress LoadingProgressBuilder::Build() const noexcept
	{
		const LoadingStatus status = m_HasFailedStep ? LoadingStatus::Failed :
			(m_HasPreparingStep ? LoadingStatus::Preparing : LoadingStatus::Ready);
		return {
			.m_Status = status,
			.m_Fraction = status == LoadingStatus::Ready ? 1.0f :
				(m_TotalWeight > 0.0f ? m_WeightedFraction / m_TotalWeight : 0.0f),
			.m_Title = m_Title,
			.m_Stage = m_CurrentStage,
			.m_Detail = m_CurrentDetail,
		};
	}
}
