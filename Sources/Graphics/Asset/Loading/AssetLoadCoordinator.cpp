#include "Core/Precompiled.h"
#include "Graphics/Asset/Loading/AssetLoadCoordinator.h"
#include "Core/Task/TaskSystem.h"

namespace gglab
{
	namespace
	{
		struct ImportJob
		{
			ImportedModel m_Model;
		};
	}

	AssetLoadCoordinator::AssetLoadCoordinator(const CreateInfo& createInfo) noexcept :
		m_TaskSystem(createInfo.m_TaskSystem)
	{
		GGLAB_ASSERT_NOT_NULL(m_TaskSystem);
	}

	AssetLoadCoordinator::~AssetLoadCoordinator()
	{
		GGLAB_ASSERT_MSG(
			!HasActiveOperations(),
			"AssetLoadCoordinator destroyed while load operations are still active.");
		GGLAB_ASSERT_MSG(
			!HasPendingCompletions(),
			"AssetLoadCoordinator destroyed while load completions are pending.");
	}

	AssetLoadSubmission AssetLoadCoordinator::SubmitModelImport(
		ModelImportRequest request) noexcept
	{
		if (!request.m_ContentVersion.IsValid() || request.m_SourcePath.empty() ||
			request.m_Priority == TaskPriority::Count)
		{
			return {};
		}
		if (const auto existing = m_ModelImports.find(request.m_ContentVersion.m_Key);
			existing != m_ModelImports.end())
		{
			if (existing->second.m_Operation.m_ContentVersion == request.m_ContentVersion)
			{
				return {
					.m_Operation = existing->second.m_Operation,
					.m_Task = existing->second.m_Task,
				};
			}
			GGLAB_UNUSED(m_TaskSystem->Cancel(existing->second.m_Task));
			m_ModelImports.erase(existing);
		}

		const AssetOperationToken operation = AllocateOperation(request.m_ContentVersion);
		auto job = std::make_shared<ImportJob>();
		const std::filesystem::path sourcePath = std::move(request.m_SourcePath);
		const ModelImportSettings importSettings = request.m_ImportSettings;
		const ProgressChannelPtr progress = std::move(request.m_Progress);
		const TaskHandle task = m_TaskSystem->Submit(
			{
				.m_Name = std::format(
					"Asset.ModelImport: {}",
					sourcePath.filename().generic_string()),
				.m_Priority = request.m_Priority,
				.m_Progress = progress,
			},
			[sourcePath, importSettings, job, progress](
				std::stop_token stopToken) noexcept
			{
				ModelImportResult result = ModelImporter::Import(
					sourcePath,
					importSettings,
					stopToken,
					ProgressReporter(progress, 0.05f, 0.62f));
				if (!result.Succeeded())
				{
					return TaskResult::Failure(std::move(result.m_Error));
				}
				job->m_Model = std::move(result.m_Model);
				return TaskResult::Success();
			},
			[this, operation, job](const TaskCompletionInfo& completion) mutable noexcept
			{
				if (!IsCurrentModelImport(operation))
				{
					return;
				}
				if (completion.m_Status == TaskStatus::Succeeded)
				{
					m_PendingCompletions.emplace_back(ModelImportSucceeded{
						.m_Operation = operation,
						.m_Completion = completion,
						.m_Model = std::move(job->m_Model),
					});
				}
				else
				{
					m_PendingCompletions.emplace_back(ModelImportFailed{
						.m_Operation = operation,
						.m_Completion = completion,
					});
				}
			});
		if (!task.IsValid())
		{
			return {};
		}
		m_ModelImports.emplace(
			request.m_ContentVersion.m_Key,
			OperationRecord{
				.m_Operation = operation,
				.m_Task = task,
			});
		return { .m_Operation = operation, .m_Task = task };
	}

	AssetLoadSubmission AssetLoadCoordinator::SubmitMeshReload(
		MeshReloadRequest request) noexcept
	{
		if (!request.m_SourceModelVersion.IsValid() || request.m_SourcePath.empty() ||
			request.m_Priority == TaskPriority::Count)
		{
			return {};
		}
		if (const auto existing = m_MeshReloads.find(request.m_SourceModelVersion.m_Key);
			existing != m_MeshReloads.end())
		{
			if (existing->second.m_Operation.m_ContentVersion ==
				request.m_SourceModelVersion)
			{
				return {
					.m_Operation = existing->second.m_Operation,
					.m_Task = existing->second.m_Task,
				};
			}
			GGLAB_UNUSED(m_TaskSystem->Cancel(existing->second.m_Task));
			m_MeshReloads.erase(existing);
		}

		const AssetOperationToken operation = AllocateOperation(
			request.m_SourceModelVersion);
		auto job = std::make_shared<ImportJob>();
		const std::filesystem::path sourcePath = std::move(request.m_SourcePath);
		const ModelImportSettings importSettings = request.m_ImportSettings;
		const TaskHandle task = m_TaskSystem->Submit(
			{
				.m_Name = std::format(
					"Asset.ModelResidencyReload: {}",
					sourcePath.filename().generic_string()),
				.m_Priority = request.m_Priority,
			},
			[sourcePath, importSettings, job](std::stop_token stopToken) noexcept
			{
				ModelImportResult result = ModelImporter::Import(
					sourcePath,
					importSettings,
					stopToken);
				if (!result.Succeeded())
				{
					return TaskResult::Failure(std::move(result.m_Error));
				}
				job->m_Model = std::move(result.m_Model);
				job->m_Model.m_Textures.clear();
				job->m_Model.m_Materials.clear();
				job->m_Model.m_MeshInstances.clear();
				return TaskResult::Success();
			},
			[this, operation, job](const TaskCompletionInfo& completion) mutable noexcept
			{
				if (!IsCurrentMeshReload(operation))
				{
					return;
				}
				if (completion.m_Status == TaskStatus::Succeeded)
				{
					m_PendingCompletions.emplace_back(MeshReloadSucceeded{
						.m_Operation = operation,
						.m_Completion = completion,
						.m_Model = std::move(job->m_Model),
					});
				}
				else
				{
					m_PendingCompletions.emplace_back(MeshReloadFailed{
						.m_Operation = operation,
						.m_Completion = completion,
					});
				}
			});
		if (!task.IsValid())
		{
			return {};
		}
		m_MeshReloads.emplace(
			request.m_SourceModelVersion.m_Key,
			OperationRecord{
				.m_Operation = operation,
				.m_Task = task,
			});
		return { .m_Operation = operation, .m_Task = task };
	}

	TaskHandle AssetLoadCoordinator::GetModelImportTask(
		AssetContentVersion contentVersion) const noexcept
	{
		const auto operation = m_ModelImports.find(contentVersion.m_Key);
		return operation != m_ModelImports.end() &&
			operation->second.m_Operation.m_ContentVersion == contentVersion ?
			operation->second.m_Task : TaskHandle{};
	}

	bool AssetLoadCoordinator::HasMeshReload(
		AssetContentVersion sourceModelVersion) const noexcept
	{
		const auto operation = m_MeshReloads.find(sourceModelVersion.m_Key);
		return operation != m_MeshReloads.end() &&
			operation->second.m_Operation.m_ContentVersion == sourceModelVersion;
	}

	bool AssetLoadCoordinator::CancelModelImport(
		AssetContentVersion contentVersion) noexcept
	{
		return m_TaskSystem->Cancel(GetModelImportTask(contentVersion));
	}

	bool AssetLoadCoordinator::UpdateModelImportPriority(
		AssetContentVersion contentVersion,
		TaskPriority priority) noexcept
	{
		const auto operation = m_ModelImports.find(contentVersion.m_Key);
		return operation != m_ModelImports.end() &&
			operation->second.m_Operation.m_ContentVersion == contentVersion &&
			m_TaskSystem->UpdatePriority(operation->second.m_Task, priority);
	}

	bool AssetLoadCoordinator::UpdateMeshReloadPriority(
		AssetContentVersion sourceModelVersion,
		TaskPriority priority) noexcept
	{
		const auto operation = m_MeshReloads.find(sourceModelVersion.m_Key);
		return operation != m_MeshReloads.end() &&
			operation->second.m_Operation.m_ContentVersion == sourceModelVersion &&
			m_TaskSystem->UpdatePriority(operation->second.m_Task, priority);
	}

	bool AssetLoadCoordinator::IsCurrentModelImport(
		AssetOperationToken operation) const noexcept
	{
		return Matches(m_ModelImports, operation);
	}

	bool AssetLoadCoordinator::IsCurrentMeshReload(
		AssetOperationToken operation) const noexcept
	{
		return Matches(m_MeshReloads, operation);
	}

	void AssetLoadCoordinator::CompleteModelImport(
		AssetOperationToken operation) noexcept
	{
		Complete(m_ModelImports, operation);
	}

	void AssetLoadCoordinator::CompleteMeshReload(
		AssetOperationToken operation) noexcept
	{
		Complete(m_MeshReloads, operation);
	}

	void AssetLoadCoordinator::DiscardModelImport(AssetKey model) noexcept
	{
		m_ModelImports.erase(model);
	}

	void AssetLoadCoordinator::DrainCompletions(
		std::vector<AssetLoadCompletion>& output) noexcept
	{
		output.clear();
		output.swap(m_PendingCompletions);
	}

	bool AssetLoadCoordinator::HasActiveOperations() const noexcept
	{
		return !m_ModelImports.empty() || !m_MeshReloads.empty();
	}

	AssetOperationToken AssetLoadCoordinator::AllocateOperation(
		AssetContentVersion contentVersion) noexcept
	{
		return MakeAssetOperationToken(contentVersion, m_NextOperationSerial++);
	}

	bool AssetLoadCoordinator::Matches(
		const OperationMap& operations,
		AssetOperationToken operation) noexcept
	{
		const auto current = operations.find(operation.m_ContentVersion.m_Key);
		return current != operations.end() &&
			current->second.m_Operation == operation;
	}

	void AssetLoadCoordinator::Complete(
		OperationMap& operations,
		AssetOperationToken operation) noexcept
	{
		const auto current = operations.find(operation.m_ContentVersion.m_Key);
		if (current != operations.end() && current->second.m_Operation == operation)
		{
			operations.erase(current);
		}
	}
}
