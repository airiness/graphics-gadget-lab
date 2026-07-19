#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/AssetManagerPanel.h"
#include "Core/Utility/StringUtils.h"
#include "DevTools/AssetSnapshotText.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/EnumText/EnumTextGraphics.h"
#include "DevTools/EnumText/EnumTextRHI.h"
#include "DevTools/RHIText.h"
#include "Graphics/Asset/AssetManager.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/Snapshots/AssetSnapshot.h"
#include "Diagnostics/Snapshots/SamplerRegistrySnapshot.h"

#include <algorithm>
#include <limits>

namespace gglab
{
	namespace
	{
		struct AssetManagerPanelState
		{
			std::array<char, 512> m_ModelPath{};
			std::array<char, 512> m_TexturePath{};
			int32_t m_TextureSemanticIndex = 6;
			std::string m_Status;
		};

		constexpr std::array TextureSemantics =
		{
			TextureSemantic::BaseColor,
			TextureSemantic::Emissive,
			TextureSemantic::Normal,
			TextureSemantic::MetallicRoughness,
			TextureSemantic::Occlusion,
			TextureSemantic::UVTest,
			TextureSemantic::GenericColor,
			TextureSemantic::GenericData,
			TextureSemantic::Unknown,
		};

		[[nodiscard]] std::string PathText(const std::filesystem::path& path)
		{
			return path.empty() ? std::string{} : path.generic_string();
		}

		[[nodiscard]] const char* StreamingWorkKindText(AssetStreamingWorkKind kind) noexcept
		{
			switch (kind)
			{
			case AssetStreamingWorkKind::Model: return "Model";
			case AssetStreamingWorkKind::Texture: return "Texture";
			case AssetStreamingWorkKind::Mesh: return "Mesh";
			default: return "Unknown";
			}
		}

		[[nodiscard]] const char* TaskPriorityText(TaskPriority priority) noexcept
		{
			switch (priority)
			{
			case TaskPriority::Critical: return "Critical";
			case TaskPriority::High: return "High";
			case TaskPriority::Normal: return "Normal";
			case TaskPriority::Background: return "Background";
			default: return "Unknown";
			}
		}

		[[nodiscard]] const char* PublicationStageText(
			AssetResourcePublicationStage stage) noexcept
		{
			switch (stage)
			{
			case AssetResourcePublicationStage::Textures: return "Textures";
			case AssetResourcePublicationStage::Materials: return "Materials";
			case AssetResourcePublicationStage::Meshes: return "Meshes";
			case AssetResourcePublicationStage::MeshInstances: return "Mesh Instances";
			case AssetResourcePublicationStage::Dependencies: return "Dependencies";
			case AssetResourcePublicationStage::Commit: return "Commit";
			case AssetResourcePublicationStage::ReleaseRetains: return "Release Retains";
			default: return "Unknown";
			}
		}

		void DrawModelAssets(AssetManager& assetManager, AssetManagerPanelState& state,
			const AssetSnapshot& assetSnapshot) noexcept
		{
			ImGui::PushID("Models");
			ImGui::SeparatorText("Load Model");
			ImGui::InputText("glTF Path", state.m_ModelPath.data(), state.m_ModelPath.size());
			ImGui::SameLine();
			const bool hasPath = state.m_ModelPath[0] != '\0';
			if (!hasPath)
			{
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Load Model"))
			{
				const std::filesystem::path path(state.m_ModelPath.data());
				const auto request = assetManager.LoadModelAsync(path);
				state.m_Status = request.IsValid() ?
					std::format("Queued model {} (task {}).",
						request.m_ModelId.Value(),
						request.m_Task.m_Value) :
					"Failed to load model. Only .gltf is currently supported.";
			}
			if (!hasPath)
			{
				ImGui::EndDisabled();
			}

			const auto& models = assetSnapshot.m_Models;
			ImGui::SeparatorText("Model Import CPU Artifact Cache");
			const uint64_t artifactLookupCount =
				assetSnapshot.m_ModelImportArtifactCacheHitCount +
				assetSnapshot.m_ModelImportArtifactCacheMissCount;
			const double artifactHitRate = artifactLookupCount == 0 ? 0.0 :
				100.0 * static_cast<double>(assetSnapshot.m_ModelImportArtifactCacheHitCount) /
					static_cast<double>(artifactLookupCount);
			ImGui::Text(
				"Entries: %u | Hits: %llu | Misses: %llu | Hit rate: %.1f%%",
				assetSnapshot.m_ModelImportArtifactCachedEntryCount,
				assetSnapshot.m_ModelImportArtifactCacheHitCount,
				assetSnapshot.m_ModelImportArtifactCacheMissCount,
				artifactHitRate);
			ImGui::Text(
				"Memory: cached %.2f MiB / %.2f MiB | externally retained %.2f MiB | total live %.2f MiB",
				static_cast<double>(assetSnapshot.m_ModelImportArtifactCachedBytes) / (1024.0 * 1024.0),
				static_cast<double>(assetSnapshot.m_ModelImportArtifactCacheBudgetBytes) / (1024.0 * 1024.0),
				static_cast<double>(assetSnapshot.m_ModelImportArtifactExternallyRetainedBytes) / (1024.0 * 1024.0),
				static_cast<double>(assetSnapshot.m_ModelImportArtifactTotalLiveBytes) / (1024.0 * 1024.0));
			ImGui::Text(
				"Admissions: %llu | Rejected: %llu | Evictions: %llu (%.2f MiB)",
				assetSnapshot.m_ModelImportArtifactAdmissionCount,
				assetSnapshot.m_ModelImportArtifactAdmissionRejectedCount,
				assetSnapshot.m_ModelImportArtifactEvictionCount,
				static_cast<double>(assetSnapshot.m_ModelImportArtifactEvictedBytes) / (1024.0 * 1024.0));
			if (ImGui::Button("Clear Model Import CPU Cache"))
			{
				assetManager.ClearModelImportArtifactCache();
				state.m_Status = "Model import CPU artifact cache cleared.";
			}
			ImGui::SeparatorText("Loaded Models");
			ImGui::Text("%u models", static_cast<uint32_t>(models.size()));

			if (ImGui::BeginTable("ModelAssetsTable", 17,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
					ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Content Gen", ImGuiTableColumnFlags_WidthFixed, 88.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
				ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthFixed, 88.0f);
				ImGui::TableSetupColumn("Residency", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableSetupColumn("Epoch", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Policy", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Last Use", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Uses", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Candidate", ImGuiTableColumnFlags_WidthFixed, 76.0f);
				ImGui::TableSetupColumn("Deps R/P/F/C", ImGuiTableColumnFlags_WidthFixed, 128.0f);
				ImGui::TableSetupColumn("Dep Events", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableSetupColumn("Meshes", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("CPU Cache", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Artifact", ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (const auto& model : models)
				{
					const std::string name = devtools::ModelDisplayName(model);
					const std::string path = PathText(model.m_SourcePath);

					ImGui::PushID(static_cast<int>(model.m_Id.Value()));
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u", model.m_Id.Value());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%llu", model.m_ContentGeneration);
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(name.c_str());
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(devtools::EnumText(model.m_ContentState).c_str());
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(devtools::EnumText(model.m_ResidencyState).c_str());
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%llu", model.m_ResidencyEpoch);
					ImGui::TableSetColumnIndex(6);
					ImGui::TextUnformatted(devtools::EnumText(model.m_ResidencyPolicy).c_str());
					ImGui::TableSetColumnIndex(7);
					ImGui::Text("%llu", model.m_LastUsedFrame);
					ImGui::TableSetColumnIndex(8);
					ImGui::Text("%llu", model.m_UseCount);
					ImGui::TableSetColumnIndex(9);
					ImGui::TextUnformatted(utils::BoolToString(model.m_IsEvictionCandidate));
					ImGui::TableSetColumnIndex(10);
					if (model.m_HasDependencyState)
					{
						ImGui::Text(
							"%u/%u/%u/%u",
							model.m_ReadyDependencyCount,
							model.m_PendingDependencyCount,
							model.m_FailedDependencyCount,
							model.m_CancelledDependencyCount);
					}
					else
					{
						ImGui::TextUnformatted("-");
					}
					ImGui::TableSetColumnIndex(11);
					ImGui::Text("%llu", model.m_DependencyEventUpdateCount);
					ImGui::TableSetColumnIndex(12);
					const std::string modelType = devtools::EnumText(model.m_Type);
					ImGui::TextUnformatted(modelType.c_str());
					ImGui::TableSetColumnIndex(13);
					ImGui::Text("%u", model.m_MeshInstanceCount);
					ImGui::TableSetColumnIndex(14);
					ImGui::TextUnformatted(utils::BoolToString(model.m_IsImportArtifactCached));
					ImGui::TableSetColumnIndex(15);
					const std::string artifactDigest = ArtifactContentDigestText(
						model.m_ImportArtifactContentDigest);
					ImGui::TextUnformatted(artifactDigest.empty() ? "-" : artifactDigest.c_str());
					ImGui::TableSetColumnIndex(16);
					ImGui::TextUnformatted(path.empty() ? "<generated>" : path.c_str());
					ImGui::PopID();
				}

				ImGui::EndTable();
			}
			ImGui::PopID();
		}

		void DrawTextureAssets(AssetManager& assetManager, AssetManagerPanelState& state,
			const AssetSnapshot& assetSnapshot) noexcept
		{
			ImGui::PushID("Textures");
			ImGui::SeparatorText("Load Texture");
			ImGui::InputText("Texture Path", state.m_TexturePath.data(), state.m_TexturePath.size());
			const std::string semanticPreview = devtools::EnumText(
				TextureSemantics[static_cast<size_t>(state.m_TextureSemanticIndex)]);
			if (ImGui::BeginCombo("Semantic", semanticPreview.c_str()))
			{
				for (size_t i = 0; i < TextureSemantics.size(); ++i)
				{
					const bool selected = static_cast<size_t>(state.m_TextureSemanticIndex) == i;
					const std::string semanticText = devtools::EnumText(TextureSemantics[i]);
					if (ImGui::Selectable(semanticText.c_str(), selected))
					{
						state.m_TextureSemanticIndex = static_cast<int32_t>(i);
					}
					if (selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			const bool hasPath = state.m_TexturePath[0] != '\0';
			if (!hasPath)
			{
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Load Texture"))
			{
				const std::filesystem::path path(state.m_TexturePath.data());
				const TextureSemantic semantic = TextureSemantics[static_cast<size_t>(state.m_TextureSemanticIndex)];
				const auto request = assetManager.LoadTextureAsync(path, semantic);
				state.m_Status = request.IsValid() ?
					std::format("Queued texture {} (task {}).",
						request.m_TextureId.Value(),
						request.m_Task.m_Value) :
					"Failed to load texture.";
			}
			if (!hasPath)
			{
				ImGui::EndDisabled();
			}

			const auto& textures = assetSnapshot.m_Textures;
			ImGui::SeparatorText("Texture CPU Artifact Cache");
			const uint64_t artifactLookupCount =
				assetSnapshot.m_TextureArtifactCacheHitCount +
				assetSnapshot.m_TextureArtifactCacheMissCount;
			const double artifactHitRate = artifactLookupCount == 0 ? 0.0 :
				100.0 * static_cast<double>(assetSnapshot.m_TextureArtifactCacheHitCount) /
					static_cast<double>(artifactLookupCount);
			ImGui::Text(
				"Entries: %u | Hits: %llu | Misses: %llu | Hit rate: %.1f%%",
				assetSnapshot.m_TextureArtifactCachedEntryCount,
				assetSnapshot.m_TextureArtifactCacheHitCount,
				assetSnapshot.m_TextureArtifactCacheMissCount,
				artifactHitRate);
			ImGui::Text(
				"Memory: cached %.2f MiB / %.2f MiB | externally retained %.2f MiB | total live %.2f MiB",
				static_cast<double>(assetSnapshot.m_TextureArtifactCachedBytes) / (1024.0 * 1024.0),
				static_cast<double>(assetSnapshot.m_TextureArtifactCacheBudgetBytes) / (1024.0 * 1024.0),
				static_cast<double>(assetSnapshot.m_TextureArtifactExternallyRetainedBytes) / (1024.0 * 1024.0),
				static_cast<double>(assetSnapshot.m_TextureArtifactTotalLiveBytes) / (1024.0 * 1024.0));
			ImGui::Text(
				"Admissions: %llu | Rejected: %llu | Evictions: %llu (%.2f MiB)",
				assetSnapshot.m_TextureArtifactAdmissionCount,
				assetSnapshot.m_TextureArtifactAdmissionRejectedCount,
				assetSnapshot.m_TextureArtifactEvictionCount,
				static_cast<double>(assetSnapshot.m_TextureArtifactEvictedBytes) / (1024.0 * 1024.0));
			if (ImGui::Button("Clear Texture CPU Cache"))
			{
				assetManager.ClearTextureArtifactCache();
				state.m_Status = "Texture CPU artifact cache cleared.";
			}
			ImGui::SeparatorText("Texture Local DDC");
			const uint64_t ddcLookupCount = assetSnapshot.m_TextureDerivedDataHitCount +
				assetSnapshot.m_TextureDerivedDataMissCount;
			const double ddcHitRate = ddcLookupCount == 0 ? 0.0 :
				100.0 * static_cast<double>(assetSnapshot.m_TextureDerivedDataHitCount) /
					static_cast<double>(ddcLookupCount);
			ImGui::Text(
				"Entries: %llu (%.2f MiB) | Hits: %llu | Misses: %llu | Hit rate: %.1f%%",
				assetSnapshot.m_TextureDerivedDataStoredEntryCount,
				static_cast<double>(assetSnapshot.m_TextureDerivedDataStoredBytes) / (1024.0 * 1024.0),
				assetSnapshot.m_TextureDerivedDataHitCount,
				assetSnapshot.m_TextureDerivedDataMissCount,
				ddcHitRate);
			ImGui::Text(
				"Reads: %.2f MiB | Writes: %llu (%.2f MiB) | Failures: %llu | Corruptions: %llu",
				static_cast<double>(assetSnapshot.m_TextureDerivedDataReadBytes) / (1024.0 * 1024.0),
				assetSnapshot.m_TextureDerivedDataWriteCount,
				static_cast<double>(assetSnapshot.m_TextureDerivedDataWrittenBytes) / (1024.0 * 1024.0),
				assetSnapshot.m_TextureDerivedDataWriteFailureCount,
				assetSnapshot.m_TextureDerivedDataCorruptionCount);
			ImGui::Text(
				"Shared requests: %llu | Build claims: %llu | Waits: %llu | Immediate hits: %llu",
				assetSnapshot.m_TextureDerivedDataRequestCount,
				assetSnapshot.m_TextureDerivedDataBuildRequiredCount,
				assetSnapshot.m_TextureDerivedDataWaitCount,
				assetSnapshot.m_TextureDerivedDataImmediateHitCount);
			ImGui::Text(
				"Active builds: %u | Active participants: %u | Publishes: %llu | Fan-out: %llu",
				assetSnapshot.m_TextureDerivedDataActiveBuildCount,
				assetSnapshot.m_TextureDerivedDataActiveWaiterCount,
				assetSnapshot.m_TextureDerivedDataPublishCount,
				assetSnapshot.m_TextureDerivedDataFanoutDeliveryCount);
			ImGui::Text(
				"Shared failures: %llu | Cancelled participants: %llu",
				assetSnapshot.m_TextureDerivedDataBuildFailureCount,
				assetSnapshot.m_TextureDerivedDataCancelledWaiterCount);
			if (ImGui::Button("Clear Texture Local DDC"))
			{
				assetManager.ClearTextureDerivedDataCache();
				state.m_Status = "Texture local DDC cleared.";
			}
			ImGui::SeparatorText("Loaded Textures");
			ImGui::Text("%u textures", static_cast<uint32_t>(textures.size()));

			if (ImGui::BeginTable("TextureAssetsTable", 21,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Content Gen", ImGuiTableColumnFlags_WidthFixed, 88.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
				ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthFixed, 88.0f);
				ImGui::TableSetupColumn("Residency", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableSetupColumn("Epoch", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Policy", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Last Use", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Uses", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Candidate", ImGuiTableColumnFlags_WidthFixed, 76.0f);
				ImGui::TableSetupColumn("Semantic", ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn("CPU Cached", ImGuiTableColumnFlags_WidthFixed, 84.0f);
				ImGui::TableSetupColumn("Artifact ID", ImGuiTableColumnFlags_WidthFixed, 136.0f);
				ImGui::TableSetupColumn("DDC Cached", ImGuiTableColumnFlags_WidthFixed, 84.0f);
				ImGui::TableSetupColumn("DDC Key", ImGuiTableColumnFlags_WidthFixed, 136.0f);
				ImGui::TableSetupColumn("Source Digest", ImGuiTableColumnFlags_WidthFixed, 136.0f);
				ImGui::TableSetupColumn("Uploaded", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Reserved", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("RHI Handle", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableSetupColumn("Native Debug Name", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (const auto& texture : textures)
				{
					const std::string name = devtools::TextureDisplayName(texture);
					const std::string path = PathText(texture.m_SourcePath);

					ImGui::PushID(static_cast<int>(texture.m_Id.Value()));
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u", texture.m_Id.Value());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%llu", texture.m_ContentGeneration);
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(name.c_str());
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(devtools::EnumText(texture.m_ContentState).c_str());
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(devtools::EnumText(texture.m_ResidencyState).c_str());
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%llu", texture.m_ResidencyEpoch);
					ImGui::TableSetColumnIndex(6);
					ImGui::TextUnformatted(devtools::EnumText(texture.m_ResidencyPolicy).c_str());
					ImGui::TableSetColumnIndex(7);
					ImGui::Text("%llu", texture.m_LastUsedFrame);
					ImGui::TableSetColumnIndex(8);
					ImGui::Text("%llu", texture.m_UseCount);
					ImGui::TableSetColumnIndex(9);
					ImGui::TextUnformatted(utils::BoolToString(texture.m_IsEvictionCandidate));
					ImGui::TableSetColumnIndex(10);
					const std::string semantic = devtools::EnumText(texture.m_Semantic);
					ImGui::TextUnformatted(semantic.c_str());
					ImGui::TableSetColumnIndex(11);
					ImGui::TextUnformatted(utils::BoolToString(texture.m_IsCpuArtifactCached));
					ImGui::TableSetColumnIndex(12);
					const std::string artifactId = ArtifactContentDigestText(
						texture.m_ArtifactContentDigest);
					ImGui::TextUnformatted(artifactId.empty() ? "-" : artifactId.c_str());
					ImGui::TableSetColumnIndex(13);
					ImGui::TextUnformatted(utils::BoolToString(texture.m_IsDerivedDataCached));
					ImGui::TableSetColumnIndex(14);
					const std::string derivedDataKey = DerivedDataKeyText(texture.m_DerivedDataKey);
					ImGui::TextUnformatted(derivedDataKey.empty() ? "-" : derivedDataKey.c_str());
					ImGui::TableSetColumnIndex(15);
					const std::string sourceDigest = SourceDigestText(texture.m_SourceDigest);
					ImGui::TextUnformatted(sourceDigest.empty() ? "-" : sourceDigest.c_str());
					ImGui::TableSetColumnIndex(16);
					ImGui::TextUnformatted(utils::BoolToString(texture.m_IsUploaded));
					ImGui::TableSetColumnIndex(17);
					ImGui::TextUnformatted(utils::BoolToString(texture.m_IsReserved));
					ImGui::TableSetColumnIndex(18);
					const std::string textureHandle = devtools::RHIHandleText(texture.m_Texture);
					ImGui::TextUnformatted(textureHandle.c_str());
					ImGui::TableSetColumnIndex(19);
					ImGui::TextUnformatted(texture.m_DebugName.empty() ? "-" : texture.m_DebugName.c_str());
					ImGui::TableSetColumnIndex(20);
					ImGui::TextUnformatted(path.empty() ? "<generated>" : path.c_str());
					ImGui::PopID();
				}

				ImGui::EndTable();
			}
			ImGui::PopID();
		}

		void DrawMeshAssets(const AssetSnapshot& assetSnapshot) noexcept
		{
			const auto& meshes = assetSnapshot.m_Meshes;
			ImGui::Text("%u meshes", static_cast<uint32_t>(meshes.size()));

			if (ImGui::BeginTable("MeshAssetsTable", 13,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
					ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Content Gen", ImGuiTableColumnFlags_WidthFixed, 88.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthFixed, 88.0f);
				ImGui::TableSetupColumn("Residency", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableSetupColumn("Epoch", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Policy", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Last Use", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Uses", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Candidate", ImGuiTableColumnFlags_WidthFixed, 76.0f);
				ImGui::TableSetupColumn("Vertices", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Indices", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Uploaded", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableHeadersRow();

				for (const auto& mesh : meshes)
				{
					const std::string name = utils::StringIdToString(mesh.m_Name);
					ImGui::PushID(static_cast<int>(mesh.m_Id.Value()));
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u", mesh.m_Id.Value());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%llu", mesh.m_ContentGeneration);
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(name.empty() ? "<unnamed>" : name.c_str());
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(devtools::EnumText(mesh.m_ContentState).c_str());
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(devtools::EnumText(mesh.m_ResidencyState).c_str());
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%llu", mesh.m_ResidencyEpoch);
					ImGui::TableSetColumnIndex(6);
					ImGui::TextUnformatted(devtools::EnumText(mesh.m_ResidencyPolicy).c_str());
					ImGui::TableSetColumnIndex(7);
					ImGui::Text("%llu", mesh.m_LastUsedFrame);
					ImGui::TableSetColumnIndex(8);
					ImGui::Text("%llu", mesh.m_UseCount);
					ImGui::TableSetColumnIndex(9);
					ImGui::TextUnformatted(utils::BoolToString(mesh.m_IsEvictionCandidate));
					ImGui::TableSetColumnIndex(10);
					ImGui::Text("%u", mesh.m_VertexCount);
					ImGui::TableSetColumnIndex(11);
					ImGui::Text("%u", mesh.m_IndexCount);
					ImGui::TableSetColumnIndex(12);
					ImGui::TextUnformatted(utils::BoolToString(mesh.m_IsUploaded));
					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		std::string SamplerPresetMaskText(uint32_t presetMask)
		{
			std::string text;
			for (uint32_t index = 0; index < utils::EnumCount<SamplerPreset>(); ++index)
			{
				if ((presetMask & (1u << index)) == 0)
				{
					continue;
				}
				if (!text.empty())
				{
					text.append(", ");
				}
				text.append(devtools::EnumText(static_cast<SamplerPreset>(index)));
			}
			return text.empty() ? "Custom" : text;
		}

		void DrawSamplerRegistry(const SamplerRegistrySnapshot& snapshot) noexcept
		{
			const uint64_t lookupCount = snapshot.m_CacheHitCount + snapshot.m_CacheMissCount;
			const double hitRate = lookupCount == 0 ? 0.0 :
				100.0 * static_cast<double>(snapshot.m_CacheHitCount) /
				static_cast<double>(lookupCount);
			ImGui::Text(
				"Unique: %u | Preset-backed: %u | Custom: %u | Preset bindings: %u",
				snapshot.m_UniqueSamplerCount,
				snapshot.m_PresetSamplerCount,
				snapshot.m_CustomSamplerCount,
				snapshot.m_PresetBindingCount);
			ImGui::Text(
				"Cache lookups: %llu hits / %llu misses (%.1f%% hit rate)",
				snapshot.m_CacheHitCount,
				snapshot.m_CacheMissCount,
				hitRate);

			if (ImGui::BeginTable(
				"SamplerRegistryTable",
				12,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
					ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 52.0f);
				ImGui::TableSetupColumn("Preset / Kind", ImGuiTableColumnFlags_WidthFixed, 180.0f);
				ImGui::TableSetupColumn("Filter", ImGuiTableColumnFlags_WidthFixed, 180.0f);
				ImGui::TableSetupColumn("Address U", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Address V", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Address W", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Compare", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Aniso", ImGuiTableColumnFlags_WidthFixed, 52.0f);
				ImGui::TableSetupColumn("Mip Bias", ImGuiTableColumnFlags_WidthFixed, 68.0f);
				ImGui::TableSetupColumn("LOD Range", ImGuiTableColumnFlags_WidthFixed, 128.0f);
				ImGui::TableSetupColumn("Descriptor", ImGuiTableColumnFlags_WidthFixed, 72.0f);
				ImGui::TableSetupColumn("RHI Handle", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableHeadersRow();

				for (const SamplerRegistrySnapshot::Entry& entry : snapshot.m_Entries)
				{
					const SamplerKey& key = entry.m_Key;
					const std::string presets = SamplerPresetMaskText(entry.m_PresetMask);
					const std::string handle = devtools::RHIHandleText(entry.m_Sampler);
					ImGui::PushID(static_cast<int>(entry.m_Id.Value()));
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u", entry.m_Id.Value());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(presets.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(devtools::EnumText(key.m_Filter).c_str());
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(devtools::EnumText(key.m_AddressU).c_str());
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(devtools::EnumText(key.m_AddressV).c_str());
					ImGui::TableSetColumnIndex(5);
					ImGui::TextUnformatted(devtools::EnumText(key.m_AddressW).c_str());
					ImGui::TableSetColumnIndex(6);
					ImGui::TextUnformatted(devtools::EnumText(key.m_CompareOp).c_str());
					ImGui::TableSetColumnIndex(7);
					ImGui::Text("%u", key.m_MaxAnisotropy);
					ImGui::TableSetColumnIndex(8);
					ImGui::Text("%.2f", key.m_MipLODBias);
					ImGui::TableSetColumnIndex(9);
					if (key.m_MaxLOD == std::numeric_limits<float>::max())
					{
						ImGui::Text("%.2f .. Max", key.m_MinLOD);
					}
					else
					{
						ImGui::Text("%.2f .. %.2f", key.m_MinLOD, key.m_MaxLOD);
					}
					ImGui::TableSetColumnIndex(10);
					ImGui::Text("%u", entry.m_DescriptorIndex);
					ImGui::TableSetColumnIndex(11);
					ImGui::TextUnformatted(handle.c_str());
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}

		void DrawUploadTable(
			const char* tableId,
			const std::vector<AssetSnapshot::Upload>& uploads) noexcept
		{
			if (ImGui::BeginTable(
				tableId,
				10,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Generation", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 76.0f);
				ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Fence", ImGuiTableColumnFlags_WidthFixed, 144.0f);
				ImGui::TableSetupColumn("Elapsed (ms)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
				ImGui::TableSetupColumn("Staging (KiB)", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableSetupColumn("Ops", ImGuiTableColumnFlags_WidthFixed, 52.0f);
				ImGui::TableHeadersRow();

				for (const AssetSnapshot::Upload& upload : uploads)
				{
					ImGui::PushID(static_cast<int>(upload.m_Handle.m_Value));
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%llu", upload.m_Handle.m_Value);
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(upload.m_Name.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%llu", upload.m_Identity.m_Generation);
					ImGui::TableSetColumnIndex(3);
					const std::string uploadStatus = devtools::EnumText(upload.m_Status);
					ImGui::TextUnformatted(uploadStatus.c_str());
					ImGui::TableSetColumnIndex(4);
					if (upload.m_Progress.HasProgress())
					{
						ImGui::Text("%.1f%%", upload.m_Progress.m_Fraction * 100.0f);
					}
					else
					{
						ImGui::TextUnformatted("-");
					}
					ImGui::TableSetColumnIndex(5);
					ImGui::TextUnformatted(upload.m_Progress.m_Stage.empty() ?
						"-" : upload.m_Progress.m_Stage.c_str());
					if (!upload.m_Progress.m_Detail.empty() && ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", upload.m_Progress.m_Detail.c_str());
					}
					ImGui::TableSetColumnIndex(6);
					if (upload.m_FencePoint.IsValid())
					{
						ImGui::Text(
							"%u:%u / %llu",
							upload.m_FencePoint.m_Fence.Index(),
							upload.m_FencePoint.m_Fence.Generation(),
							upload.m_FencePoint.m_Value);
					}
					else
					{
						ImGui::TextUnformatted("-");
					}
					ImGui::TableSetColumnIndex(7);
					ImGui::Text("%.2f", upload.m_ElapsedMilliseconds);
					ImGui::TableSetColumnIndex(8);
					ImGui::Text("%.1f", static_cast<double>(upload.m_Estimate.m_StagingBytes) / 1024.0);
					ImGui::TableSetColumnIndex(9);
					ImGui::Text("%u", upload.m_Estimate.m_OperationCount);
					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		void DrawStreamingQueue(
			const char* label,
			const char* tableId,
			const AssetStreamingQueueStatistics& queue) noexcept
		{
			const double averageWait = queue.m_QueueSampleCount > 0 ?
				queue.m_TotalQueueMilliseconds / static_cast<double>(queue.m_QueueSampleCount) : 0.0;
			const double averageExecution = queue.m_ProcessedCount > 0 ?
				queue.m_TotalExecutionMilliseconds / static_cast<double>(queue.m_ProcessedCount) : 0.0;
			ImGui::Text(
				"%s: pending=%u high=%u payload=%.2f/%.2f MiB ops=%llu enqueued=%llu processed=%llu cancelled=%llu failures=%llu wait(avg/max)=%.3f/%.3f ms run(avg/p95/max)=%.3f/%.3f/%.3f ms",
				label,
				queue.m_PendingCount,
				queue.m_HighWatermark,
				static_cast<double>(queue.m_PendingSourceBytes) / (1024.0 * 1024.0),
				static_cast<double>(queue.m_PendingStagingBytes) / (1024.0 * 1024.0),
				queue.m_PendingOperationCount,
				queue.m_EnqueuedCount,
				queue.m_ProcessedCount,
				queue.m_CancelledCount,
				queue.m_CallbackFailureCount,
				averageWait,
				queue.m_MaxQueueMilliseconds,
				averageExecution,
				queue.m_ExecutionP95Milliseconds,
				queue.m_MaxExecutionMilliseconds);
			if (queue.m_ContinueCount > 0 ||
				queue.m_CompletedCount > 0 ||
				queue.m_FailedCount > 0 ||
				queue.m_ResourceCreationCount > 0)
			{
				ImGui::Text(
					"Step jobs: continue=%llu completed=%llu failed=%llu creations=%llu payload moved/destroyed=%.2f/%.2f MiB",
					queue.m_ContinueCount,
					queue.m_CompletedCount,
					queue.m_FailedCount,
					queue.m_ResourceCreationCount,
					static_cast<double>(queue.m_PayloadBytesMovedToUpload) / (1024.0 * 1024.0),
					static_cast<double>(queue.m_PayloadBytesDestroyed) / (1024.0 * 1024.0));
				ImGui::Text(
					"Step diagnostics: over-budget=%llu no-progress=%llu injected-faults=%llu",
					queue.m_OverBudgetExecutionCount,
					queue.m_NoProgressContinueCount,
					queue.m_FaultInjectionCount);
				if (ImGui::BeginTable(
					"PublicationStageTelemetry",
					5,
					ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
				{
					ImGui::TableSetupColumn("Stage");
					ImGui::TableSetupColumn("Steps");
					ImGui::TableSetupColumn("Average (ms)");
					ImGui::TableSetupColumn("P95 (ms)");
					ImGui::TableSetupColumn("Max (ms)");
					ImGui::TableHeadersRow();
					for (size_t stageIndex = 1;
						stageIndex < queue.m_PublicationStages.size();
						++stageIndex)
					{
						const auto& stage = queue.m_PublicationStages[stageIndex];
						if (stage.m_StepCount == 0)
						{
							continue;
						}
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(PublicationStageText(
							static_cast<AssetResourcePublicationStage>(stageIndex)));
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("%llu", stage.m_StepCount);
						ImGui::TableSetColumnIndex(2);
						ImGui::Text("%.3f", stage.m_TotalMilliseconds /
							static_cast<double>(stage.m_StepCount));
						ImGui::TableSetColumnIndex(3);
						ImGui::Text("%.3f", stage.m_P95Milliseconds);
						ImGui::TableSetColumnIndex(4);
						ImGui::Text("%.3f", stage.m_MaxMilliseconds);
					}
					ImGui::EndTable();
				}
			}
			if (queue.m_PendingWork.empty())
			{
				return;
			}

			if (ImGui::BeginTable(
				tableId,
				9,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 72.0f);
				ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthFixed, 72.0f);
				ImGui::TableSetupColumn("Generation", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Queued (ms)", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableSetupColumn("Source (KiB)", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableSetupColumn("Staging (KiB)", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableSetupColumn("Ops", ImGuiTableColumnFlags_WidthFixed, 52.0f);
				ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 84.0f);
				ImGui::TableHeadersRow();
				for (const AssetStreamingWorkActivity& work : queue.m_PendingWork)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(work.m_Name.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(StreamingWorkKindText(work.m_Identity.m_Kind));
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%llu", work.m_Identity.m_StableId);
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%llu", work.m_Identity.m_Generation);
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%.3f", work.m_QueueMilliseconds);
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%.1f", static_cast<double>(work.m_Estimate.m_SourceBytes) / 1024.0);
					ImGui::TableSetColumnIndex(6);
					ImGui::Text("%.1f", static_cast<double>(work.m_Estimate.m_StagingBytes) / 1024.0);
					ImGui::TableSetColumnIndex(7);
					ImGui::Text("%u", work.m_Estimate.m_OperationCount);
					ImGui::TableSetColumnIndex(8);
					ImGui::TextUnformatted(TaskPriorityText(work.m_Priority));
				}
				ImGui::EndTable();
			}
		}

		void DrawAssetUploads(const AssetSnapshot& assetSnapshot) noexcept
		{
			ImGui::SeparatorText("Ownership");
			ImGui::Text(
				"Owners: %u   Leases: %u   Managed assets: %u   Priority updates: %llu",
				assetSnapshot.m_AssetOwnerCount,
				assetSnapshot.m_AssetLeaseCount,
				assetSnapshot.m_ManagedAssetCount,
				assetSnapshot.m_OwnershipPriorityUpdateCount);
			ImGui::Text(
				"Last-interest policy: CPU cancelled=%llu ready-work cancelled=%llu GPU deferred=%llu Ready retained=%llu",
				assetSnapshot.m_OwnershipCpuCancellationCount,
				assetSnapshot.m_OwnershipReadyCancellationCount,
				assetSnapshot.m_OwnershipGpuDeferredCancellationCount,
				assetSnapshot.m_OwnershipReadyRetentionCount);
			ImGui::Text(
				"Publication transaction: active retains=%llu protected cancellations=%llu",
				assetSnapshot.m_PublicationRetainCount,
				assetSnapshot.m_PublicationProtectedCancellationCount);
			if (!assetSnapshot.m_ActiveOwnershipInterests.empty() && ImGui::BeginTable(
				"AssetOwnershipInterests",
				6,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 72.0f);
				ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthFixed, 72.0f);
				ImGui::TableSetupColumn("Generation", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Owners", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Leases", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Effective Priority", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				for (const AssetSnapshot::OwnershipInterest& interest :
					assetSnapshot.m_ActiveOwnershipInterests)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(StreamingWorkKindText(interest.m_Kind));
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%llu", interest.m_StableId);
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%llu", interest.m_Generation);
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%u", interest.m_OwnerCount);
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%u", interest.m_LeaseCount);
					ImGui::TableSetColumnIndex(5);
					ImGui::TextUnformatted(TaskPriorityText(interest.m_EffectivePriority));
				}
				ImGui::EndTable();
			}

			const AssetStreamingFrameBudget& budget = assetSnapshot.m_StreamingFrameBudget;
			const AssetStreamingFrameUsage& usage = assetSnapshot.m_LastStreamingFrameUsage;
			ImGui::SeparatorText("Frame Admission Budget");
			ImGui::Text(
				"CPU payload: %u/%u items, %.3f/%.3f ms   Resource publication: %u/%u steps, %u/%u creations, %.3f/%.3f ms",
				usage.m_CpuPayloadItems,
				budget.m_MaxCpuPayloadItems,
				usage.m_CpuPayloadMilliseconds,
				budget.m_MaxCpuPayloadMilliseconds,
				usage.m_ResourcePublicationSteps,
				budget.m_MaxResourcePublicationSteps,
				usage.m_ResourcePublicationCreations,
				budget.m_MaxResourcePublicationCreations,
				usage.m_ResourcePublicationMilliseconds,
				budget.m_MaxResourcePublicationMilliseconds);
			ImGui::Text(
				"Upload recording: %u/%u items, %.2f/%.2f MiB, %u/%u ops, %.3f/%.3f ms   GPU finalize: %u/%u items, %.3f/%.3f ms",
				usage.m_UploadRecordingItems,
				budget.m_MaxUploadRecordingItems,
				static_cast<double>(usage.m_UploadBytes) / (1024.0 * 1024.0),
				static_cast<double>(budget.m_MaxUploadBytes) / (1024.0 * 1024.0),
				usage.m_UploadOperations,
				budget.m_MaxUploadOperations,
				usage.m_UploadRecordingMilliseconds,
				budget.m_MaxUploadRecordingMilliseconds,
				usage.m_GpuFinalizeItems,
				budget.m_MaxGpuFinalizeItems,
				usage.m_GpuFinalizeMilliseconds,
				budget.m_MaxGpuFinalizeMilliseconds);
			ImGui::Text(
				"Observed queued payload: %.2f MiB (high %.2f)   Upload-recording promotion limit: %.2f MiB   In-flight staging: %.2f/%.2f MiB (high %.2f)",
				static_cast<double>(assetSnapshot.m_ReadyPayloadBytes) / (1024.0 * 1024.0),
				static_cast<double>(assetSnapshot.m_ReadyPayloadHighWatermark) / (1024.0 * 1024.0),
				static_cast<double>(budget.m_MaxUploadRecordingBacklogBytes) / (1024.0 * 1024.0),
				static_cast<double>(assetSnapshot.m_InFlightUploadBytes) / (1024.0 * 1024.0),
				static_cast<double>(budget.m_MaxInFlightBytes) / (1024.0 * 1024.0),
				static_cast<double>(assetSnapshot.m_InFlightUploadHighWatermark) / (1024.0 * 1024.0));
			ImGui::Text(
				"Deferrals: upload-promotion=%llu upload-budget=%llu in-flight=%llu   Oversized admissions=%llu",
				assetSnapshot.m_UploadPromotionBudgetDeferralCount,
				assetSnapshot.m_UploadBudgetDeferralCount,
				assetSnapshot.m_InFlightBudgetDeferralCount,
				assetSnapshot.m_OversizedAdmissionCount);

			ImGui::SeparatorText("Streaming Queues");
			DrawStreamingQueue("CPU Payload", "CpuPayloadQueue", assetSnapshot.m_CpuPayloadQueue);
			DrawStreamingQueue(
				"Resource Publication",
				"ResourcePublicationQueue",
				assetSnapshot.m_ResourcePublicationQueue);
			DrawStreamingQueue(
				"Upload Recording",
				"UploadRecordingQueue",
				assetSnapshot.m_UploadRecordingQueue);
			DrawStreamingQueue("GPU Finalize", "GpuFinalizeQueue", assetSnapshot.m_GpuFinalizeQueue);

			ImGui::SeparatorText("GPU Uploads");
			ImGui::Text(
				"Pending: %u   Submitted: %llu   Succeeded: %llu   Failed: %llu   Callback failures: %llu",
				assetSnapshot.m_PendingUploadCount,
				assetSnapshot.m_SubmittedUploadCount,
				assetSnapshot.m_SucceededUploadCount,
				assetSnapshot.m_FailedUploadCount,
				assetSnapshot.m_UploadCompletionCallbackFailureCount);
			const double averageResourcesPerBatch =
				assetSnapshot.m_UploadBatchSubmissionCount > 0 ?
				static_cast<double>(assetSnapshot.m_SubmittedUploadCount) /
					static_cast<double>(assetSnapshot.m_UploadBatchSubmissionCount) : 0.0;
			ImGui::Text(
				"Transfer batches: %llu   Resources/batch: last=%u average=%.2f max=%u",
				assetSnapshot.m_UploadBatchSubmissionCount,
				assetSnapshot.m_LastUploadBatchResourceCount,
				averageResourcesPerBatch,
				assetSnapshot.m_MaxResourcesPerUploadBatch);

			ImGui::SeparatorText("Pending GPU Uploads");
			if (assetSnapshot.m_PendingUploads.empty())
			{
				ImGui::TextDisabled("No pending uploads.");
			}
			else
			{
				DrawUploadTable("PendingAssetUploads", assetSnapshot.m_PendingUploads);
			}

			ImGui::SeparatorText("Recent GPU Uploads");
			if (assetSnapshot.m_RecentUploads.empty())
			{
				ImGui::TextDisabled("No completed uploads.");
			}
			else
			{
				DrawUploadTable("RecentAssetUploads", assetSnapshot.m_RecentUploads);
			}
		}
	}

	void AssetManagerPanel::Draw(DevelopGuiContext& context) noexcept
	{
		if (!context.m_AssetManager)
		{
			ImGui::TextUnformatted("No AssetManager bound in DevelopGuiContext.");
			return;
		}

		auto& state = context.PanelState<AssetManagerPanelState>();
		const auto* snapshot = context.m_Diagnostics ?
			context.m_Diagnostics->GetSnapshot<AssetSnapshot>() : nullptr;
		const auto* samplerSnapshot = context.m_Diagnostics ?
			context.m_Diagnostics->GetSnapshot<SamplerRegistrySnapshot>() : nullptr;
		if (!snapshot)
		{
			ImGui::TextDisabled("Asset snapshot provider is not available.");
			return;
		}
		state.m_TextureSemanticIndex = std::clamp<int32_t>(
			state.m_TextureSemanticIndex,
			0,
			static_cast<int32_t>(TextureSemantics.size() - 1));

		ImGui::TextUnformatted("Asset Manager");
		ImGui::Separator();

		if (!state.m_Status.empty())
		{
			ImGui::TextWrapped("%s", state.m_Status.c_str());
			ImGui::Separator();
		}

		ImGui::SeparatorText("Logical Residency");
		ImGui::Text(
			"Usage frame: %llu | Resident: %u | Pinned: %u | Cacheable: %u | Candidates: %u",
			snapshot->m_AssetUsageFrame,
			snapshot->m_ResidentAssetCount,
			snapshot->m_PinnedAssetCount,
			snapshot->m_CacheableAssetCount,
			snapshot->m_EvictionCandidateCount);
		ImGui::Text(
			"Controller: %s | Resident: %.2f MiB | Pending release: %.2f MiB (%u)",
			snapshot->m_AutomaticResidencyEvictionEnabled ? "Enabled" : "Disabled",
			static_cast<double>(snapshot->m_LogicalResidentBytes) / (1024.0 * 1024.0),
			static_cast<double>(snapshot->m_PendingEvictionBytes) / (1024.0 * 1024.0),
			snapshot->m_PendingEvictionCount);
		ImGui::Text(
			"Watermarks: %.2f / %.2f MiB | Min unused: %llu frames | Max releases/frame: %u",
			static_cast<double>(snapshot->m_ResidencyLowWatermarkBytes) / (1024.0 * 1024.0),
			static_cast<double>(snapshot->m_ResidencyHighWatermarkBytes) / (1024.0 * 1024.0),
			snapshot->m_ResidencyMinUnusedFrames,
			snapshot->m_MaxResidencyEvictionsPerFrame);
		ImGui::Text(
			"Released: %llu assets / %.2f MiB | Rescinded: %llu | Reloading: %u",
			snapshot->m_ResidencyEvictionCount,
			static_cast<double>(snapshot->m_ResidencyEvictedBytes) / (1024.0 * 1024.0),
			snapshot->m_ResidencyEvictionCancellationCount,
			snapshot->m_ReloadingAssetCount);
		ImGui::Text(
			"Reload requests: %llu | Coalesced: %llu | Last frame: %u | Peak/frame: %u",
			snapshot->m_ResidencyReloadRequestCount,
			snapshot->m_ResidencyReloadCoalescedCount,
			snapshot->m_LastFrameReloadRequestCount,
			snapshot->m_ReloadRequestHighWatermark);
		ImGui::Text(
			"Planning: %llu frames | Last plan: frame %llu, %u actions / %.2f MiB",
			snapshot->m_ResidencyPlanningCount,
			snapshot->m_LastResidencyPlanFrame,
			snapshot->m_LastPlannedResidencyActionCount,
			static_cast<double>(snapshot->m_LastPlannedResidencyBytes) /
				(1024.0 * 1024.0));
		ImGui::Text(
			"Operations: %llu | Revalidation rejects: %llu | Stale completions: %llu",
			snapshot->m_ResidencyOperationCount,
			snapshot->m_ResidencyRevalidationRejectionCount,
			snapshot->m_ResidencyStaleCompletionCount);
		ImGui::Text(
			"Operation events: %llu accepted / %llu completed | Stale: %llu",
			snapshot->m_ResidencyAcceptedStateEventCount,
			snapshot->m_ResidencyCompletedStateEventCount,
			snapshot->m_ResidencyStaleStateEventCount);
		ImGui::Text(
			"Dependency graph: %u models | %u resources | %u edges | Builds: %llu | Events: %llu",
			snapshot->m_TrackedModelDependencyCount,
			snapshot->m_ReverseDependencyCount,
			snapshot->m_ReverseDependencyEdgeCount,
			snapshot->m_DependencyGraphBuildCount,
			snapshot->m_DependencyEventUpdateCount);
		ImGui::Text(
			"Dependency validation: %llu checks | %llu mismatches",
			snapshot->m_DependencyValidationCount,
			snapshot->m_DependencyValidationMismatchCount);

		if (ImGui::BeginTabBar("AssetManagerTabs"))
		{
			if (ImGui::BeginTabItem("Models"))
			{
				DrawModelAssets(*context.m_AssetManager, state, *snapshot);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Textures"))
			{
				DrawTextureAssets(*context.m_AssetManager, state, *snapshot);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Meshes"))
			{
				DrawMeshAssets(*snapshot);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Samplers"))
			{
				if (samplerSnapshot)
				{
					DrawSamplerRegistry(*samplerSnapshot);
				}
				else
				{
					ImGui::TextDisabled("Sampler registry snapshot is not available.");
				}
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Uploads"))
			{
				DrawAssetUploads(*snapshot);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
}
