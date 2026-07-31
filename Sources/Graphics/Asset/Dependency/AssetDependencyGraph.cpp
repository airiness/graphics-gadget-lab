#include "Core/Precompiled.h"
#include "Graphics/Asset/Dependency/AssetDependencyGraph.h"

namespace gglab
{
	bool AssetDependencyGraph::RegisterModel(AssetContentVersion model,
		std::span<const DependencyStatus> dependencies, uint32_t structuralFailureCount) noexcept
	{
		if (!model.IsValid() || model.m_Key.m_Kind != AssetKind::Model)
		{
			return false;
		}

		UnregisterModel(model.m_Key);
		AssetDependencyModelState state{
			.m_Model = model,
			.m_StructuralFailureCount = structuralFailureCount,
		};
		for (const DependencyStatus& dependency : dependencies)
		{
			if (!dependency.IsValid())
			{
				++state.m_StructuralFailureCount;
				continue;
			}
			const auto [stored, inserted] =
				state.m_DependencyStates.emplace(dependency.m_ContentVersion, dependency);
			if (inserted)
			{
				IncrementCounter(state, ProjectDependencyOutcome(stored->second));
			}
		}

		auto [storedModel, inserted] = m_Models.emplace(model.m_Key, std::move(state));
		GGLAB_ASSERT(inserted);
		if (!inserted)
		{
			return false;
		}
		for (const auto& [dependency, status] : storedModel->second.m_DependencyStates)
		{
			GGLAB_UNUSED(status);
			auto& dependents = m_ReverseDependencies[dependency];
			GGLAB_ASSERT(std::ranges::find(dependents, model) == dependents.end());
			dependents.push_back(model);
		}
		++m_GraphBuildCount;
		return true;
	}

	void AssetDependencyGraph::UnregisterModel(AssetContentVersion model) noexcept
	{
		const auto current = m_Models.find(model.m_Key);
		if (current == m_Models.end() || current->second.m_Model != model)
		{
			return;
		}
		UnregisterModel(model.m_Key);
	}

	void AssetDependencyGraph::ApplyStatus(
		const DependencyStatus& status, std::vector<AssetDependencyChange>& changes) noexcept
	{
		changes.clear();
		if (!status.IsValid())
		{
			++m_IgnoredEventCount;
			return;
		}
		const auto reverse = m_ReverseDependencies.find(status.m_ContentVersion);
		if (reverse == m_ReverseDependencies.end())
		{
			++m_IgnoredEventCount;
			return;
		}

		for (const AssetContentVersion dependent : reverse->second)
		{
			auto model = m_Models.find(dependent.m_Key);
			if (model == m_Models.end() || model->second.m_Model != dependent)
			{
				continue;
			}
			auto dependency = model->second.m_DependencyStates.find(status.m_ContentVersion);
			if (dependency == model->second.m_DependencyStates.end() ||
				dependency->second == status)
			{
				continue;
			}

			const ModelDependencyOutcome previousOutcome = EvaluateModel(model->second);
			DecrementCounter(model->second, ProjectDependencyOutcome(dependency->second));
			dependency->second = status;
			IncrementCounter(model->second, ProjectDependencyOutcome(status));
			++model->second.m_EventUpdateCount;
			++m_EventUpdateCount;
			const ModelDependencyOutcome currentOutcome = EvaluateModel(model->second);
			if (previousOutcome != currentOutcome)
			{
				changes.push_back({
					.m_Model = dependent,
					.m_CurrentOutcome = currentOutcome,
					});
			}
		}
	}

	const AssetDependencyModelState* AssetDependencyGraph::FindModel(
		AssetContentVersion model) const noexcept
	{
		const auto current = m_Models.find(model.m_Key);
		return current != m_Models.end() && current->second.m_Model == model
			? std::addressof(current->second)
			: nullptr;
	}

	std::span<const AssetContentVersion> AssetDependencyGraph::FindDependents(
		AssetContentVersion dependency) const noexcept
	{
		const auto dependents = m_ReverseDependencies.find(dependency);
		return dependents != m_ReverseDependencies.end()
			? std::span<const AssetContentVersion>(dependents->second)
			: std::span<const AssetContentVersion>{};
	}

	ModelDependencyOutcome AssetDependencyGraph::EvaluateModel(
		AssetContentVersion model) const noexcept
	{
		const AssetDependencyModelState* state = FindModel(model);
		return state ? EvaluateModel(*state) : ModelDependencyOutcome::Failed;
	}

	AssetDependencyGraphStatistics AssetDependencyGraph::GetStatistics() const noexcept
	{
		AssetDependencyGraphStatistics statistics{
			.m_TrackedModelCount = static_cast<uint32_t>(m_Models.size()),
			.m_ReverseDependencyCount = static_cast<uint32_t>(m_ReverseDependencies.size()),
			.m_GraphBuildCount = m_GraphBuildCount,
			.m_EventUpdateCount = m_EventUpdateCount,
			.m_IgnoredEventCount = m_IgnoredEventCount,
		};
		for (const auto& dependents : m_ReverseDependencies | std::views::values)
		{
			statistics.m_ReverseDependencyEdgeCount += static_cast<uint32_t>(dependents.size());
		}
		return statistics;
	}

	void AssetDependencyGraph::UnregisterModel(AssetKey model) noexcept
	{
		const auto current = m_Models.find(model);
		if (current == m_Models.end())
		{
			return;
		}
		const AssetContentVersion modelVersion = current->second.m_Model;
		for (const auto& [dependency, status] : current->second.m_DependencyStates)
		{
			GGLAB_UNUSED(status);
			const auto reverse = m_ReverseDependencies.find(dependency);
			GGLAB_ASSERT(reverse != m_ReverseDependencies.end());
			if (reverse == m_ReverseDependencies.end())
			{
				continue;
			}
			std::erase(reverse->second, modelVersion);
			if (reverse->second.empty())
			{
				m_ReverseDependencies.erase(reverse);
			}
		}
		m_Models.erase(current);
	}

	void AssetDependencyGraph::IncrementCounter(
		AssetDependencyModelState& state, ModelDependencyOutcome outcome) noexcept
	{
		switch (outcome)
		{
		case ModelDependencyOutcome::Ready:
			++state.m_ReadyCount;
			break;
		case ModelDependencyOutcome::Pending:
			++state.m_PendingCount;
			break;
		case ModelDependencyOutcome::Failed:
			++state.m_FailedCount;
			break;
		case ModelDependencyOutcome::Cancelled:
			++state.m_CancelledCount;
			break;
		}
	}

	void AssetDependencyGraph::DecrementCounter(
		AssetDependencyModelState& state, ModelDependencyOutcome outcome) noexcept
	{
		switch (outcome)
		{
		case ModelDependencyOutcome::Ready:
			GGLAB_ASSERT(state.m_ReadyCount > 0);
			--state.m_ReadyCount;
			break;
		case ModelDependencyOutcome::Pending:
			GGLAB_ASSERT(state.m_PendingCount > 0);
			--state.m_PendingCount;
			break;
		case ModelDependencyOutcome::Failed:
			GGLAB_ASSERT(state.m_FailedCount > 0);
			--state.m_FailedCount;
			break;
		case ModelDependencyOutcome::Cancelled:
			GGLAB_ASSERT(state.m_CancelledCount > 0);
			--state.m_CancelledCount;
			break;
		}
	}

	ModelDependencyOutcome AssetDependencyGraph::EvaluateModel(
		const AssetDependencyModelState& state) noexcept
	{
		if (state.m_StructuralFailureCount > 0 || state.m_FailedCount > 0)
		{
			return ModelDependencyOutcome::Failed;
		}
		if (state.m_CancelledCount > 0)
		{
			return ModelDependencyOutcome::Cancelled;
		}
		if (state.m_PendingCount > 0)
		{
			return ModelDependencyOutcome::Pending;
		}
		return state.m_ReadyCount > 0 ? ModelDependencyOutcome::Ready
			: ModelDependencyOutcome::Failed;
	}
}
