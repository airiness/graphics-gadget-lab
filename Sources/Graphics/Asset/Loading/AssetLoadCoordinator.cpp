#include "Core/Precompiled.h"
#include "Graphics/Asset/Loading/AssetLoadCoordinator.h"
#include "Core/Task/TaskSystem.h"
#include "Graphics/Asset/DerivedData/SourceSnapshot.h"
#include "Graphics/Asset/DerivedData/TextureArtifactCodec.h"
#include "Graphics/Asset/Loading/TextureLoader.h"
#include "Graphics/Asset/TextureArtifact.h"

namespace gglab
{
	namespace
	{
		struct ImportJob
		{
			ImportedModel m_Model;
		};

		struct TextureDecodeJob
		{
			TextureAssetData m_TextureData;
			AssetContentFingerprint m_ContentFingerprint{};
			ArtifactContentDigest m_ArtifactContentDigest{};
			SourceDigest m_SourceDigest{};
			DerivedDataKey m_DerivedDataKey{};
			bool m_DerivedDataCacheHit = false;
		};
	}

	AssetLoadCoordinator::AssetLoadCoordinator(const CreateInfo& createInfo) noexcept :
		m_TaskSystem(createInfo.m_TaskSystem),
		m_TextureDerivedDataStore(createInfo.m_TextureDerivedDataCacheDirectory)
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

	AssetLoadSubmission AssetLoadCoordinator::SubmitTextureDecode(
		TextureDecodeRequest request) noexcept
	{
		if (!request.m_ContentVersion.IsValid() || request.m_SourcePath.empty() ||
			request.m_Priority == TaskPriority::Count)
		{
			return {};
		}
		if (const auto existing = m_TextureDecodes.find(request.m_ContentVersion.m_Key);
			existing != m_TextureDecodes.end())
		{
			if (existing->second.m_Operation.m_ContentVersion == request.m_ContentVersion)
			{
				return {
					.m_Operation = existing->second.m_Operation,
					.m_Task = existing->second.m_Task,
				};
			}
			GGLAB_UNUSED(m_TaskSystem->Cancel(existing->second.m_Task));
			m_TextureDecodes.erase(existing);
		}

		const AssetOperationToken operation = AllocateOperation(request.m_ContentVersion);
		auto job = std::make_shared<TextureDecodeJob>();
		const std::filesystem::path sourcePath = std::move(request.m_SourcePath);
		const TextureImportSettings importSettings = request.m_ImportSettings;
		const TextureSemantic semantic = request.m_Semantic;
		const ProgressChannelPtr progress = std::move(request.m_Progress);
		const SourceDigest expectedSourceDigest = request.m_ExpectedSourceDigest;
		const DerivedDataKey expectedDerivedDataKey = request.m_ExpectedDerivedDataKey;
		const bool residencyReload = request.m_ResidencyReload;
		const AssetResidencyOperation residencyOperation = request.m_ResidencyOperation;
		const TaskHandle task = m_TaskSystem->Submit(
			{
				.m_Name = std::format(
					"Asset.TextureDecode: {}",
					sourcePath.filename().generic_string()),
				.m_Priority = request.m_Priority,
				.m_Progress = progress,
			},
			[this, sourcePath, importSettings, expectedSourceDigest,
				expectedDerivedDataKey, job, progress](std::stop_token stopToken) noexcept
			{
				if (stopToken.stop_requested())
				{
					return TaskResult::Success();
				}

				const auto tryDerivedData = [this, &job, &importSettings, &progress](
					const DerivedDataKey& key) noexcept
				{
					if (!key.IsValid()) return false;
					ProgressReporter(progress).Report(
						0.12f,
						"Looking up texture derived data",
						DerivedDataKeyText(key));
					DerivedDataReadResult cached = m_TextureDerivedDataStore.Read(
						key,
						TextureArtifactType,
						TextureArtifactSchemaVersion);
					if (cached.m_Disposition != DerivedDataReadDisposition::Hit) return false;
					TextureArtifactDecodeResult decoded = TextureArtifactCodec::Deserialize(
						cached.m_Payload,
						cached.m_ArtifactContentDigest);
					if (!decoded.Succeeded())
					{
						m_TextureDerivedDataStore.DiscardCorrupt(key);
						GGLAB_LOG_GRAPHICS_WARN(
							"Texture DDC entry '{}' failed codec validation: {}",
							DerivedDataKeyText(key),
							decoded.m_Error);
						return false;
					}
					job->m_TextureData = std::move(decoded.m_Artifact.m_Data);
					job->m_ArtifactContentDigest = decoded.m_Artifact.m_ContentDigest;
					job->m_ContentFingerprint = ComputeTextureContentFingerprint(
						job->m_TextureData,
						importSettings);
					job->m_DerivedDataKey = key;
					job->m_DerivedDataCacheHit = true;
					ProgressReporter(progress).Report(
						0.60f,
						"Texture derived data cache hit",
						DerivedDataKeyText(key));
					return job->m_ContentFingerprint.IsValid();
				};

				job->m_SourceDigest = expectedSourceDigest;
				if (expectedDerivedDataKey.IsValid() && tryDerivedData(expectedDerivedDataKey))
				{
					return TaskResult::Success();
				}

				SourceSnapshotResult snapshot = ReadSourceSnapshot(sourcePath);
				if (!snapshot.Succeeded()) return TaskResult::Failure(std::move(snapshot.m_Error));
				job->m_SourceDigest = snapshot.m_Snapshot.m_Digest;
				job->m_DerivedDataKey = BuildTextureDerivedDataKey(
					job->m_SourceDigest,
					sourcePath,
					importSettings);
				if (!job->m_DerivedDataKey.IsValid())
				{
					return TaskResult::Failure("Failed to build the texture derived-data key.");
				}
				if (expectedDerivedDataKey.IsValid() &&
					job->m_DerivedDataKey != expectedDerivedDataKey)
				{
					return TaskResult::Failure(std::format(
						"Texture source changed for immutable generation (expected key {}, observed {}).",
						DerivedDataKeyText(expectedDerivedDataKey),
						DerivedDataKeyText(job->m_DerivedDataKey)));
				}
				if (!expectedDerivedDataKey.IsValid() && tryDerivedData(job->m_DerivedDataKey))
				{
					return TaskResult::Success();
				}
				if (stopToken.stop_requested()) return TaskResult::Success();
				job->m_TextureData = TextureLoader::LoadTextureData(
					sourcePath,
					snapshot.m_Snapshot.m_Bytes,
					importSettings,
					ProgressReporter(progress, 0.18f, 0.62f));
				if (!job->m_TextureData.IsValid())
				{
					return TaskResult::Failure(std::format(
						"Failed to decode texture '{}'.",
						sourcePath.string()));
				}
				job->m_ArtifactContentDigest = ComputeTextureArtifactContentDigest(
					job->m_TextureData);
				job->m_ContentFingerprint = ComputeTextureContentFingerprint(
					job->m_TextureData,
					importSettings);
				if (!job->m_ArtifactContentDigest.IsValid() ||
					!job->m_ContentFingerprint.IsValid())
				{
					return TaskResult::Failure(std::format(
						"Failed to fingerprint decoded texture '{}'.",
						sourcePath.string()));
				}
				TextureArtifact artifact{
					.m_Data = std::move(job->m_TextureData),
					.m_ContentDigest = job->m_ArtifactContentDigest,
				};
				const std::vector<std::byte> payload = TextureArtifactCodec::Serialize(artifact);
				job->m_TextureData = std::move(artifact.m_Data);
				if (!payload.empty() && !m_TextureDerivedDataStore.Write(
					job->m_DerivedDataKey,
					TextureArtifactType,
					TextureArtifactSchemaVersion,
					job->m_ArtifactContentDigest,
					payload))
				{
					GGLAB_LOG_GRAPHICS_WARN(
						"Failed to publish texture DDC entry '{}'.",
						DerivedDataKeyText(job->m_DerivedDataKey));
				}
				return TaskResult::Success();
			},
			[this, operation, semantic, residencyReload, residencyOperation, job](
				const TaskCompletionInfo& completion) mutable noexcept
			{
				if (!IsCurrentTextureDecode(operation))
				{
					return;
				}
				if (completion.m_Status == TaskStatus::Succeeded)
				{
					m_PendingCompletions.emplace_back(TextureDecodeSucceeded{
						.m_Operation = operation,
						.m_Completion = completion,
						.m_Semantic = semantic,
						.m_Artifact = {
							.m_Data = std::move(job->m_TextureData),
							.m_ContentDigest = job->m_ArtifactContentDigest,
						},
						.m_ContentFingerprint = job->m_ContentFingerprint,
						.m_SourceDigest = job->m_SourceDigest,
						.m_DerivedDataKey = job->m_DerivedDataKey,
						.m_DerivedDataCacheHit = job->m_DerivedDataCacheHit,
						.m_ResidencyReload = residencyReload,
						.m_ResidencyOperation = residencyOperation,
					});
				}
				else
				{
					m_PendingCompletions.emplace_back(TextureDecodeFailed{
						.m_Operation = operation,
						.m_Completion = completion,
						.m_Semantic = semantic,
						.m_ResidencyReload = residencyReload,
						.m_ResidencyOperation = residencyOperation,
					});
				}
			});
		if (!task.IsValid())
		{
			return {};
		}
		m_TextureDecodes.emplace(
			request.m_ContentVersion.m_Key,
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

	TaskHandle AssetLoadCoordinator::GetTextureDecodeTask(
		AssetContentVersion contentVersion) const noexcept
	{
		const auto operation = m_TextureDecodes.find(contentVersion.m_Key);
		return operation != m_TextureDecodes.end() &&
			operation->second.m_Operation.m_ContentVersion == contentVersion ?
			operation->second.m_Task : TaskHandle{};
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

	bool AssetLoadCoordinator::CancelTextureDecode(
		AssetContentVersion contentVersion) noexcept
	{
		return m_TaskSystem->Cancel(GetTextureDecodeTask(contentVersion));
	}

	bool AssetLoadCoordinator::UpdateTextureDecodePriority(
		AssetContentVersion contentVersion,
		TaskPriority priority) noexcept
	{
		const auto operation = m_TextureDecodes.find(contentVersion.m_Key);
		return operation != m_TextureDecodes.end() &&
			operation->second.m_Operation.m_ContentVersion == contentVersion &&
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

	bool AssetLoadCoordinator::IsCurrentTextureDecode(
		AssetOperationToken operation) const noexcept
	{
		return Matches(m_TextureDecodes, operation);
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

	void AssetLoadCoordinator::CompleteTextureDecode(
		AssetOperationToken operation) noexcept
	{
		Complete(m_TextureDecodes, operation);
	}

	void AssetLoadCoordinator::DiscardModelImport(AssetKey model) noexcept
	{
		m_ModelImports.erase(model);
	}

	void AssetLoadCoordinator::DiscardTextureDecode(AssetKey texture) noexcept
	{
		m_TextureDecodes.erase(texture);
	}

	void AssetLoadCoordinator::DrainCompletions(
		std::vector<AssetLoadCompletion>& output) noexcept
	{
		output.clear();
		output.swap(m_PendingCompletions);
	}

	bool AssetLoadCoordinator::HasActiveOperations() const noexcept
	{
		return !m_ModelImports.empty() || !m_MeshReloads.empty() ||
			!m_TextureDecodes.empty();
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
