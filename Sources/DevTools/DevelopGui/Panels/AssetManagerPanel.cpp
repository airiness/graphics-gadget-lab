#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/AssetManagerPanel.h"
#include "Core/Utility/StringUtils.h"
#include "DevTools/AssetSnapshotText.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/EnumText/EnumTextGraphics.h"
#include "DevTools/RHIText.h"
#include "Graphics/AssetManager.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/Snapshots/AssetSnapshot.h"

#include <algorithm>

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
			ImGui::SeparatorText("Loaded Models");
			ImGui::Text("%u models", static_cast<uint32_t>(models.size()));

			if (ImGui::BeginTable("ModelAssetsTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Generation", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
				ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 112.0f);
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableSetupColumn("Meshes", ImGuiTableColumnFlags_WidthFixed, 80.0f);
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
					ImGui::Text("%llu", model.m_Generation);
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(name.c_str());
					ImGui::TableSetColumnIndex(3);
					const std::string modelState = devtools::EnumText(model.m_State);
					ImGui::TextUnformatted(modelState.c_str());
					ImGui::TableSetColumnIndex(4);
					const std::string modelType = devtools::EnumText(model.m_Type);
					ImGui::TextUnformatted(modelType.c_str());
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%u", model.m_MeshInstanceCount);
					ImGui::TableSetColumnIndex(6);
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
			ImGui::SeparatorText("Loaded Textures");
			ImGui::Text("%u textures", static_cast<uint32_t>(textures.size()));

			if (ImGui::BeginTable("TextureAssetsTable", 10,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Generation", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
				ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 112.0f);
				ImGui::TableSetupColumn("Semantic", ImGuiTableColumnFlags_WidthFixed, 140.0f);
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
					ImGui::Text("%llu", texture.m_Generation);
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(name.c_str());
					ImGui::TableSetColumnIndex(3);
					const std::string textureState = devtools::EnumText(texture.m_State);
					ImGui::TextUnformatted(textureState.c_str());
					ImGui::TableSetColumnIndex(4);
					const std::string semantic = devtools::EnumText(texture.m_Semantic);
					ImGui::TextUnformatted(semantic.c_str());
					ImGui::TableSetColumnIndex(5);
					ImGui::TextUnformatted(utils::BoolToString(texture.m_IsUploaded));
					ImGui::TableSetColumnIndex(6);
					ImGui::TextUnformatted(utils::BoolToString(texture.m_IsReserved));
					ImGui::TableSetColumnIndex(7);
					const std::string textureHandle = devtools::RHIHandleText(texture.m_Texture);
					ImGui::TextUnformatted(textureHandle.c_str());
					ImGui::TableSetColumnIndex(8);
					ImGui::TextUnformatted(texture.m_DebugName.empty() ? "-" : texture.m_DebugName.c_str());
					ImGui::TableSetColumnIndex(9);
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

			if (ImGui::BeginTable("MeshAssetsTable", 7,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Generation", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 112.0f);
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
					ImGui::Text("%llu", mesh.m_Generation);
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(name.empty() ? "<unnamed>" : name.c_str());
					ImGui::TableSetColumnIndex(3);
					const std::string meshState = devtools::EnumText(mesh.m_State);
					ImGui::TextUnformatted(meshState.c_str());
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%u", mesh.m_VertexCount);
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%u", mesh.m_IndexCount);
					ImGui::TableSetColumnIndex(6);
					ImGui::TextUnformatted(utils::BoolToString(mesh.m_IsUploaded));
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
				8,
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
			const double averageWait = queue.m_ProcessedCount > 0 ?
				queue.m_TotalQueueMilliseconds / static_cast<double>(queue.m_ProcessedCount) : 0.0;
			const double averageExecution = queue.m_ProcessedCount > 0 ?
				queue.m_TotalExecutionMilliseconds / static_cast<double>(queue.m_ProcessedCount) : 0.0;
			ImGui::Text(
				"%s: pending=%u high=%u enqueued=%llu processed=%llu failures=%llu wait(avg/max)=%.3f/%.3f ms run(avg/max)=%.3f/%.3f ms",
				label,
				queue.m_PendingCount,
				queue.m_HighWatermark,
				queue.m_EnqueuedCount,
				queue.m_ProcessedCount,
				queue.m_CallbackFailureCount,
				averageWait,
				queue.m_MaxQueueMilliseconds,
				averageExecution,
				queue.m_MaxExecutionMilliseconds);
			if (queue.m_PendingWork.empty())
			{
				return;
			}

			if (ImGui::BeginTable(
				tableId,
				5,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 72.0f);
				ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthFixed, 72.0f);
				ImGui::TableSetupColumn("Generation", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Queued (ms)", ImGuiTableColumnFlags_WidthFixed, 96.0f);
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
				}
				ImGui::EndTable();
			}
		}

		void DrawAssetUploads(const AssetSnapshot& assetSnapshot) noexcept
		{
			ImGui::SeparatorText("Streaming Queues");
			DrawStreamingQueue("CPU Ready", "CpuReadyQueue", assetSnapshot.m_CpuReadyQueue);
			DrawStreamingQueue("Upload Ready", "UploadReadyQueue", assetSnapshot.m_UploadReadyQueue);
			DrawStreamingQueue(
				"Publication Ready",
				"PublicationReadyQueue",
				assetSnapshot.m_PublicationReadyQueue);

			ImGui::SeparatorText("GPU Uploads");
			ImGui::Text(
				"Pending: %u   Submitted: %llu   Succeeded: %llu   Failed: %llu   Callback failures: %llu",
				assetSnapshot.m_PendingUploadCount,
				assetSnapshot.m_SubmittedUploadCount,
				assetSnapshot.m_SucceededUploadCount,
				assetSnapshot.m_FailedUploadCount,
				assetSnapshot.m_UploadCompletionCallbackFailureCount);

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
			if (ImGui::BeginTabItem("Uploads"))
			{
				DrawAssetUploads(*snapshot);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
}
