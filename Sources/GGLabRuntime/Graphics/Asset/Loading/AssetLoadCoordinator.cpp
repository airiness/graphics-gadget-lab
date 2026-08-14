#include "Graphics/Asset/Loading/AssetLoadCoordinator.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Core/Log/LogMacros.h"
#include "Core/Task/TaskSystem.h"
#include "Graphics/Asset/DerivedData/SourceSnapshot.h"
#include "Graphics/Asset/Loading/TextureLoader.h"
#include "Graphics/Asset/ModelImportArtifactCache.h"
#include "Graphics/Asset/TextureArtifact.h"

#include <filesystem>
#include <format>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		struct ImportJob
		{
			ModelImportArtifactHandle m_Artifact;
		};

		struct TextureDecodeJob
		{
			TextureDerivedDataArtifact m_Artifact;
			SourceDigest m_SourceDigest{};
			DerivedDataKey m_DerivedDataKey{};
		};

		[[nodiscard]] TaskResult ResolveTextureArtifact(TextureDerivedDataSystem& derivedDataSystem,
			const std::filesystem::path& sourcePath, const TextureImportSettings& importSettings,
			SourceDigest expectedSourceDigest, DerivedDataKey expectedDerivedDataKey,
			ArtifactContentDigest expectedArtifactContentDigest, const ProgressReporter& progress,
			std::stop_token stopToken, TextureDecodeJob& job) noexcept
		{
			const auto validateExpectedArtifact =
				[expectedArtifactContentDigest](
					const TextureDerivedDataArtifact& artifact) noexcept -> std::string
				{
					if (!expectedArtifactContentDigest.IsValid() ||
						(artifact.m_Artifact &&
							artifact.m_Artifact->m_ContentDigest == expectedArtifactContentDigest))
					{
						return {};
					}
					const ArtifactContentDigest actualDigest =
						artifact.m_Artifact ? artifact.m_Artifact->m_ContentDigest
						: ArtifactContentDigest{};
					return std::format(
						"Texture artifact changed for immutable generation (expected {}, resolved {}).",
						ArtifactContentDigestText(expectedArtifactContentDigest),
						ArtifactContentDigestText(actualDigest));
				};

			job = {};
			if (stopToken.stop_requested())
			{
				return TaskResult::Success();
			}
			job.m_SourceDigest = expectedSourceDigest;
			job.m_DerivedDataKey = expectedDerivedDataKey;
			SourceSnapshotResult snapshot{};
			bool hasSourceSnapshot = false;
			if (!job.m_DerivedDataKey.IsValid())
			{
				snapshot = ReadSourceSnapshot(sourcePath);
				if (!snapshot.Succeeded())
				{
					return TaskResult::Failure(std::move(snapshot.m_Error));
				}
				hasSourceSnapshot = true;
				job.m_SourceDigest = snapshot.m_Snapshot.m_Digest;
				job.m_DerivedDataKey =
					BuildTextureDerivedDataKey(job.m_SourceDigest, sourcePath, importSettings);
			}
			if (!job.m_DerivedDataKey.IsValid())
			{
				return TaskResult::Failure("Failed to build the texture derived-data key.");
			}

			TextureDerivedDataRequestResult requestResult =
				derivedDataSystem.Request(job.m_DerivedDataKey);
			if (requestResult.m_Disposition == ArtifactRequestDisposition::Hit)
			{
				job.m_Artifact = std::move(requestResult.m_Artifact);
				if (!job.m_Artifact.IsValid())
				{
					return TaskResult::Failure("Shared texture artifact hit was invalid.");
				}
				if (std::string error = validateExpectedArtifact(job.m_Artifact); !error.empty())
				{
					return TaskResult::Failure(std::move(error));
				}
				return TaskResult::Success();
			}
			if (requestResult.m_Disposition == ArtifactRequestDisposition::Waiting)
			{
				progress.Report(0.14f, "Waiting for shared texture artifact",
					DerivedDataKeyText(job.m_DerivedDataKey));
				TextureArtifactWaitResult waitResult =
					derivedDataSystem.Wait(std::move(requestResult.m_Waiter), stopToken);
				if (waitResult.m_Disposition == ArtifactWaitDisposition::Cancelled)
				{
					return TaskResult::Success();
				}
				if (waitResult.m_Disposition == ArtifactWaitDisposition::Failed)
				{
					return TaskResult::Failure(std::move(waitResult.m_Error));
				}
				job.m_Artifact = std::move(waitResult.m_Artifact);
				if (std::string error = validateExpectedArtifact(job.m_Artifact); !error.empty())
				{
					return TaskResult::Failure(std::move(error));
				}
				progress.Report(0.60f, "Shared texture artifact resolved",
					DerivedDataKeyText(job.m_DerivedDataKey));
				return job.m_Artifact.IsValid()
					? TaskResult::Success()
					: TaskResult::Failure("Shared texture artifact result was invalid.");
			}

			TextureArtifactWaiterHandle participant = std::move(requestResult.m_Waiter);
			TextureArtifactBuildClaim buildClaim = std::move(requestResult.m_BuildClaim);
			std::stop_callback cancelParticipant(
				stopToken, [&participant]() noexcept { participant.Cancel(); });

			progress.Report(0.12f, "Looking up shared texture derived data",
				DerivedDataKeyText(job.m_DerivedDataKey));
			TextureDerivedDataArtifact cached =
				derivedDataSystem.Read(job.m_DerivedDataKey, importSettings);
			if (cached.IsValid())
			{
				if (std::string error = validateExpectedArtifact(cached); !error.empty())
				{
					GGLAB_UNUSED(derivedDataSystem.Fail(std::move(buildClaim), error));
					return TaskResult::Failure(std::move(error));
				}
				job.m_Artifact = cached;
				if (!derivedDataSystem.Publish(std::move(buildClaim), std::move(cached)))
				{
					return TaskResult::Failure("Failed to publish a shared texture DDC hit.");
				}
				progress.Report(0.60f, "Shared texture derived data cache hit",
					DerivedDataKeyText(job.m_DerivedDataKey));
				return TaskResult::Success();
			}

			if (!hasSourceSnapshot)
			{
				snapshot = ReadSourceSnapshot(sourcePath);
				if (!snapshot.Succeeded())
				{
					const std::string error = snapshot.m_Error;
					GGLAB_UNUSED(derivedDataSystem.Fail(std::move(buildClaim), error));
					return TaskResult::Failure(error);
				}
				job.m_SourceDigest = snapshot.m_Snapshot.m_Digest;
				const DerivedDataKey observedKey =
					BuildTextureDerivedDataKey(job.m_SourceDigest, sourcePath, importSettings);
				if (observedKey != job.m_DerivedDataKey)
				{
					const std::string error = std::format(
						"Texture source changed for immutable generation (expected key {}, observed {}).",
						DerivedDataKeyText(job.m_DerivedDataKey), DerivedDataKeyText(observedKey));
					GGLAB_UNUSED(derivedDataSystem.Fail(std::move(buildClaim), error));
					return TaskResult::Failure(error);
				}
			}
			TextureAssetData textureData = TextureLoader::LoadTextureData(sourcePath,
				snapshot.m_Snapshot.m_Bytes, importSettings, progress.Subrange(0.18f, 0.62f));
			if (!textureData.IsValid())
			{
				const std::string error =
					std::format("Failed to decode texture '{}'.", sourcePath.string());
				GGLAB_UNUSED(derivedDataSystem.Fail(std::move(buildClaim), error));
				return TaskResult::Failure(error);
			}
			const AssetContentFingerprint contentFingerprint =
				ComputeTextureContentFingerprint(textureData, importSettings);
			TextureArtifactBuildResult built = CreateTextureArtifact(std::move(textureData));
			if (!built.Succeeded() || !contentFingerprint.IsValid())
			{
				const std::string error = std::format(
					"Failed to build decoded texture artifact '{}'.", sourcePath.string());
				GGLAB_UNUSED(derivedDataSystem.Fail(std::move(buildClaim), error));
				return TaskResult::Failure(error);
			}
			const ArtifactContentDigest artifactContentDigest = built.m_Artifact.m_ContentDigest;
			if (expectedArtifactContentDigest.IsValid() &&
				artifactContentDigest != expectedArtifactContentDigest)
			{
				const std::string error = std::format(
					"Texture source rebuild changed immutable generation content (expected artifact {}, rebuilt artifact {}).",
					ArtifactContentDigestText(expectedArtifactContentDigest),
					ArtifactContentDigestText(artifactContentDigest));
				GGLAB_UNUSED(derivedDataSystem.Fail(std::move(buildClaim), error));
				return TaskResult::Failure(error);
			}
			TextureArtifactHandle artifact =
				std::make_shared<const TextureArtifact>(std::move(built.m_Artifact));
			if (!derivedDataSystem.Write(job.m_DerivedDataKey, *artifact))
			{
				GGLAB_LOG_GRAPHICS_WARN("Failed to publish texture DDC entry '{}'.",
					DerivedDataKeyText(job.m_DerivedDataKey));
			}
			job.m_Artifact = {
				.m_Artifact = std::move(artifact),
				.m_ContentFingerprint = contentFingerprint,
				.m_DerivedDataCacheHit = false,
			};
			if (!derivedDataSystem.Publish(std::move(buildClaim), job.m_Artifact))
			{
				return TaskResult::Failure("Failed to publish a shared texture artifact.");
			}
			return TaskResult::Success();
		}

		[[nodiscard]] TaskResult ResolveModelTextureSources(
			TextureDerivedDataSystem& derivedDataSystem, const ImportedModel& model,
			const ProgressReporter& progress, std::stop_token stopToken,
			std::vector<ResolvedModelImportTexture>& resolvedTextures) noexcept
		{
			resolvedTextures.clear();
			resolvedTextures.reserve(model.m_TextureSources.size());
			uint32_t derivedDataCacheHitCount = 0;
			for (size_t textureIndex = 0; textureIndex < model.m_TextureSources.size();
				++textureIndex)
			{
				if (stopToken.stop_requested())
				{
					return TaskResult::Success();
				}
				const ImportedTextureSource& source = model.m_TextureSources[textureIndex];
				const float begin = static_cast<float>(textureIndex) /
					static_cast<float>(model.m_TextureSources.size());
				const float end = static_cast<float>(textureIndex + 1) /
					static_cast<float>(model.m_TextureSources.size());
				progress.Report(begin, "Resolving model texture artifacts",
					std::format("{} of {}: {}", textureIndex + 1, model.m_TextureSources.size(),
						source.m_CanonicalPath.filename().generic_string()),
					static_cast<uint32_t>(textureIndex),
					static_cast<uint32_t>(model.m_TextureSources.size()));
				TextureDecodeJob textureJob{};
				TaskResult result = ResolveTextureArtifact(derivedDataSystem,
					source.m_CanonicalPath, source.m_ImportSettings, {}, {}, {},
					progress.Subrange(begin, end), stopToken, textureJob);
				if (!result.m_Succeeded || stopToken.stop_requested())
				{
					return result;
				}
				if (!textureJob.m_Artifact.IsValid() || !textureJob.m_SourceDigest.IsValid() ||
					!textureJob.m_DerivedDataKey.IsValid())
				{
					return TaskResult::Failure(std::format(
						"Failed to resolve model texture '{}'.", source.m_CanonicalPath.string()));
				}
				derivedDataCacheHitCount += textureJob.m_Artifact.m_DerivedDataCacheHit ? 1u : 0u;
				resolvedTextures.push_back({
					.m_Artifact = std::move(textureJob.m_Artifact.m_Artifact),
					.m_ContentFingerprint = textureJob.m_Artifact.m_ContentFingerprint,
					.m_SourceDigest = textureJob.m_SourceDigest,
					.m_DerivedDataKey = textureJob.m_DerivedDataKey,
					});
			}
			progress.Report(1.0f, "Model texture artifacts resolved",
				std::format("{} textures", resolvedTextures.size()),
				static_cast<uint32_t>(resolvedTextures.size()),
				static_cast<uint32_t>(resolvedTextures.size()));
			if (!resolvedTextures.empty())
			{
				GGLAB_LOG_GRAPHICS_INFO(
					"Model texture artifacts resolved (textures={}, ddcHits={}).",
					resolvedTextures.size(), derivedDataCacheHitCount);
			}
			return TaskResult::Success();
		}
	}

	AssetLoadCoordinator::AssetLoadCoordinator(const CreateInfo& createInfo) noexcept :
		m_TaskSystem(createInfo.m_TaskSystem),
		m_ModelImportArtifactCache(createInfo.m_ModelImportArtifactCache),
		m_TextureArtifactCache(createInfo.m_TextureArtifactCache),
		m_TextureDerivedDataSystem(createInfo.m_TextureDerivedDataCacheDirectory)
	{
		GGLAB_ASSERT_NOT_NULL(m_TaskSystem);
		GGLAB_ASSERT_NOT_NULL(m_ModelImportArtifactCache);
		GGLAB_ASSERT_NOT_NULL(m_TextureArtifactCache);
	}

	AssetLoadCoordinator::~AssetLoadCoordinator()
	{
		GGLAB_ASSERT_MSG(!HasActiveOperations(),
			"AssetLoadCoordinator destroyed while load operations are still active.");
		GGLAB_ASSERT_MSG(!HasPendingCompletions(),
			"AssetLoadCoordinator destroyed while load completions are pending.");
	}

	AssetLoadSubmission AssetLoadCoordinator::SubmitModelImport(ModelImportRequest request) noexcept
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
		TextureArtifactCache* const textureArtifactCache = m_TextureArtifactCache;
		const TaskHandle task = m_TaskSystem->Submit(
			{
				.m_Name =
					std::format("Asset.ModelImport: {}", sourcePath.filename().generic_string()),
				.m_Priority = request.m_Priority,
				.m_Progress = progress,
			},
			[this, sourcePath, importSettings, job, progress, textureArtifactCache](
				std::stop_token stopToken) noexcept
			{
				ModelImportResult result = ModelImporter::Import(sourcePath, importSettings,
					stopToken, ProgressReporter(progress, 0.05f, 0.48f));
				if (!result.Succeeded())
				{
					return TaskResult::Failure(std::move(result.m_Error));
				}
				std::vector<ResolvedModelImportTexture> resolvedTextures;
				TaskResult textureResult =
					ResolveModelTextureSources(m_TextureDerivedDataSystem, result.m_Model,
						ProgressReporter(progress, 0.48f, 0.62f), stopToken, resolvedTextures);
				if (!textureResult.m_Succeeded || stopToken.stop_requested())
				{
					return textureResult;
				}
				job->m_Artifact = CreateModelImportArtifact(
					std::move(result.m_Model), std::move(resolvedTextures), *textureArtifactCache);
				if (!job->m_Artifact)
				{
					return TaskResult::Failure("Failed to create model import artifact");
				}
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
						.m_Artifact = std::move(job->m_Artifact),
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
		m_ModelImports.emplace(request.m_ContentVersion.m_Key,
			OperationRecord{
				.m_Operation = operation,
				.m_Task = task,
			});
		return { .m_Operation = operation, .m_Task = task };
	}

	AssetLoadSubmission AssetLoadCoordinator::SubmitMeshReload(MeshReloadRequest request) noexcept
	{
		if (!request.m_SourceModelVersion.IsValid() || request.m_SourcePath.empty() ||
			request.m_Priority == TaskPriority::Count ||
			!request.m_ExpectedArtifactContentDigest.IsValid())
		{
			return {};
		}
		if (const auto existing = m_MeshReloads.find(request.m_SourceModelVersion.m_Key);
			existing != m_MeshReloads.end())
		{
			if (existing->second.m_Operation.m_ContentVersion == request.m_SourceModelVersion)
			{
				return {
					.m_Operation = existing->second.m_Operation,
					.m_Task = existing->second.m_Task,
				};
			}
			GGLAB_UNUSED(m_TaskSystem->Cancel(existing->second.m_Task));
			m_MeshReloads.erase(existing);
		}

		const AssetOperationToken operation = AllocateOperation(request.m_SourceModelVersion);
		auto job = std::make_shared<ImportJob>();
		if (request.m_ExpectedArtifactContentDigest.IsValid())
		{
			job->m_Artifact =
				m_ModelImportArtifactCache->Find(request.m_ExpectedArtifactContentDigest);
		}
		const std::filesystem::path sourcePath = std::move(request.m_SourcePath);
		const ModelImportSettings importSettings = request.m_ImportSettings;
		const ArtifactContentDigest expectedArtifactContentDigest =
			request.m_ExpectedArtifactContentDigest;
		TextureArtifactCache* const textureArtifactCache = m_TextureArtifactCache;
		const bool cacheHit = static_cast<bool>(job->m_Artifact);
		const std::string taskName = cacheHit ? std::format("Asset.ModelResidencyCacheHit: {}",
			sourcePath.filename().generic_string())
			: std::format("Asset.ModelResidencyReload: {}",
				sourcePath.filename().generic_string());
		const TaskHandle task = m_TaskSystem->Submit(
			{
				.m_Name = taskName,
				.m_Priority = request.m_Priority,
			},
			[this, sourcePath, importSettings, expectedArtifactContentDigest, job, cacheHit,
			textureArtifactCache](std::stop_token stopToken) noexcept
			{
				if (cacheHit)
				{
					if (job->m_Artifact->m_ContentDigest != expectedArtifactContentDigest)
					{
						return TaskResult::Failure(
							"Model artifact cache returned content with an unexpected digest.");
					}
					return TaskResult::Success();
				}
				ModelImportResult result =
					ModelImporter::Import(sourcePath, importSettings, stopToken);
				if (!result.Succeeded())
				{
					return TaskResult::Failure(std::move(result.m_Error));
				}
				std::vector<ResolvedModelImportTexture> resolvedTextures;
				TaskResult textureResult = ResolveModelTextureSources(
					m_TextureDerivedDataSystem, result.m_Model, {}, stopToken, resolvedTextures);
				if (!textureResult.m_Succeeded || stopToken.stop_requested())
				{
					return textureResult;
				}
				job->m_Artifact = CreateModelImportArtifact(
					std::move(result.m_Model), std::move(resolvedTextures), *textureArtifactCache);
				if (!job->m_Artifact)
				{
					return TaskResult::Failure("Failed to create model import artifact");
				}
				if (expectedArtifactContentDigest.IsValid() &&
					job->m_Artifact->m_ContentDigest != expectedArtifactContentDigest)
				{
					return TaskResult::Failure(std::format(
						"Model source changed for immutable content generation (expected artifact {}, rebuilt artifact {}).",
						ArtifactContentDigestText(expectedArtifactContentDigest),
						ArtifactContentDigestText(job->m_Artifact->m_ContentDigest)));
				}
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
						.m_Artifact = std::move(job->m_Artifact),
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
		m_MeshReloads.emplace(request.m_SourceModelVersion.m_Key,
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
			request.m_Priority == TaskPriority::Count ||
			(request.m_ResidencyReload && (!request.m_ExpectedArtifactContentDigest.IsValid() ||
				!request.m_ResidencyOperation.IsValid())))
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
			const bool preserveSharedProducer =
				request.m_ExpectedDerivedDataKey.IsValid() &&
				existing->second.m_DerivedDataKey == request.m_ExpectedDerivedDataKey;
			if (!preserveSharedProducer)
			{
				GGLAB_UNUSED(m_TaskSystem->Cancel(existing->second.m_Task));
			}
			else if (request.m_Priority < existing->second.m_Priority)
			{
				GGLAB_UNUSED(
					m_TaskSystem->UpdatePriority(existing->second.m_Task, request.m_Priority));
			}
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
		const ArtifactContentDigest expectedArtifactContentDigest =
			request.m_ExpectedArtifactContentDigest;
		const bool residencyReload = request.m_ResidencyReload;
		const AssetResidencyOperation residencyOperation = request.m_ResidencyOperation;
		const TaskHandle task = m_TaskSystem->Submit(
			{
				.m_Name =
					std::format("Asset.TextureDecode: {}", sourcePath.filename().generic_string()),
				.m_Priority = request.m_Priority,
				.m_Progress = progress,
			},
			[this, sourcePath, importSettings, expectedSourceDigest, expectedDerivedDataKey,
			expectedArtifactContentDigest, job, progress](std::stop_token stopToken) noexcept
			{
				return ResolveTextureArtifact(m_TextureDerivedDataSystem, sourcePath,
					importSettings, expectedSourceDigest, expectedDerivedDataKey,
					expectedArtifactContentDigest, ProgressReporter(progress), stopToken, *job);
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
						.m_Artifact = std::move(job->m_Artifact.m_Artifact),
						.m_ContentFingerprint = job->m_Artifact.m_ContentFingerprint,
						.m_SourceDigest = job->m_SourceDigest,
						.m_DerivedDataKey = job->m_DerivedDataKey,
						.m_DerivedDataCacheHit = job->m_Artifact.m_DerivedDataCacheHit,
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
		m_TextureDecodes.emplace(request.m_ContentVersion.m_Key,
			OperationRecord{
				.m_Operation = operation,
				.m_Task = task,
				.m_DerivedDataKey = expectedDerivedDataKey,
				.m_Priority = request.m_Priority,
			});
		return { .m_Operation = operation, .m_Task = task };
	}

	TaskHandle AssetLoadCoordinator::GetModelImportTask(
		AssetContentVersion contentVersion) const noexcept
	{
		const auto operation = m_ModelImports.find(contentVersion.m_Key);
		return operation != m_ModelImports.end() &&
			operation->second.m_Operation.m_ContentVersion == contentVersion
			? operation->second.m_Task
			: TaskHandle{};
	}

	bool AssetLoadCoordinator::HasMeshReload(AssetContentVersion sourceModelVersion) const noexcept
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
			operation->second.m_Operation.m_ContentVersion == contentVersion
			? operation->second.m_Task
			: TaskHandle{};
	}

	bool AssetLoadCoordinator::CancelModelImport(AssetContentVersion contentVersion) noexcept
	{
		return m_TaskSystem->Cancel(GetModelImportTask(contentVersion));
	}

	bool AssetLoadCoordinator::UpdateModelImportPriority(
		AssetContentVersion contentVersion, TaskPriority priority) noexcept
	{
		const auto operation = m_ModelImports.find(contentVersion.m_Key);
		return operation != m_ModelImports.end() &&
			operation->second.m_Operation.m_ContentVersion == contentVersion &&
			m_TaskSystem->UpdatePriority(operation->second.m_Task, priority);
	}

	bool AssetLoadCoordinator::UpdateMeshReloadPriority(
		AssetContentVersion sourceModelVersion, TaskPriority priority) noexcept
	{
		const auto operation = m_MeshReloads.find(sourceModelVersion.m_Key);
		return operation != m_MeshReloads.end() &&
			operation->second.m_Operation.m_ContentVersion == sourceModelVersion &&
			m_TaskSystem->UpdatePriority(operation->second.m_Task, priority);
	}

	bool AssetLoadCoordinator::CancelTextureDecode(AssetContentVersion contentVersion) noexcept
	{
		return m_TaskSystem->Cancel(GetTextureDecodeTask(contentVersion));
	}

	bool AssetLoadCoordinator::UpdateTextureDecodePriority(
		AssetContentVersion contentVersion, TaskPriority priority) noexcept
	{
		const auto operation = m_TextureDecodes.find(contentVersion.m_Key);
		return operation != m_TextureDecodes.end() &&
			operation->second.m_Operation.m_ContentVersion == contentVersion &&
			m_TaskSystem->UpdatePriority(operation->second.m_Task, priority);
	}

	bool AssetLoadCoordinator::IsCurrentModelImport(AssetOperationToken operation) const noexcept
	{
		return Matches(m_ModelImports, operation);
	}

	bool AssetLoadCoordinator::IsCurrentMeshReload(AssetOperationToken operation) const noexcept
	{
		return Matches(m_MeshReloads, operation);
	}

	bool AssetLoadCoordinator::IsCurrentTextureDecode(AssetOperationToken operation) const noexcept
	{
		return Matches(m_TextureDecodes, operation);
	}

	void AssetLoadCoordinator::CompleteModelImport(AssetOperationToken operation) noexcept
	{
		Complete(m_ModelImports, operation);
	}

	void AssetLoadCoordinator::CompleteMeshReload(AssetOperationToken operation) noexcept
	{
		Complete(m_MeshReloads, operation);
	}

	void AssetLoadCoordinator::CompleteTextureDecode(AssetOperationToken operation) noexcept
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

	void AssetLoadCoordinator::DrainCompletions(std::vector<AssetLoadCompletion>& output) noexcept
	{
		output.clear();
		output.swap(m_PendingCompletions);
	}

	bool AssetLoadCoordinator::HasActiveOperations() const noexcept
	{
		return !m_ModelImports.empty() || !m_MeshReloads.empty() || !m_TextureDecodes.empty();
	}

	AssetOperationToken AssetLoadCoordinator::AllocateOperation(
		AssetContentVersion contentVersion) noexcept
	{
		return MakeAssetOperationToken(contentVersion, m_NextOperationSerial++);
	}

	bool AssetLoadCoordinator::Matches(
		const OperationMap& operations, AssetOperationToken operation) noexcept
	{
		const auto current = operations.find(operation.m_ContentVersion.m_Key);
		return current != operations.end() && current->second.m_Operation == operation;
	}

	void AssetLoadCoordinator::Complete(
		OperationMap& operations, AssetOperationToken operation) noexcept
	{
		const auto current = operations.find(operation.m_ContentVersion.m_Key);
		if (current != operations.end() && current->second.m_Operation == operation)
		{
			operations.erase(current);
		}
	}
}
