#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/AssetManagerPanel.h"
#include "Core/Utility/StringUtils.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
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

		struct TextureSemanticItem
		{
			TextureSemantic m_Value = TextureSemantic::Unknown;
			const char* m_Label = "Unknown";
		};

		constexpr std::array TextureSemanticItems =
		{
			TextureSemanticItem{ TextureSemantic::BaseColor, "Base Color" },
			TextureSemanticItem{ TextureSemantic::Emissive, "Emissive" },
			TextureSemanticItem{ TextureSemantic::Normal, "Normal" },
			TextureSemanticItem{ TextureSemantic::MetallicRoughness, "Metallic Roughness" },
			TextureSemanticItem{ TextureSemantic::Occlusion, "Occlusion" },
			TextureSemanticItem{ TextureSemantic::UVTest, "UV Test" },
			TextureSemanticItem{ TextureSemantic::GenericColor, "Generic Color" },
			TextureSemanticItem{ TextureSemantic::GenericData, "Generic Data" },
			TextureSemanticItem{ TextureSemantic::Unknown, "Unknown" },
		};

		[[nodiscard]] const char* ModelTypeText(ModelType type) noexcept
		{
			switch (type)
			{
			case ModelType::GlTF:
				return "glTF";
			case ModelType::Procedural:
				return "Procedural";
			case ModelType::Invalid:
			default:
				return "Invalid";
			}
		}

		[[nodiscard]] const char* TextureSemanticText(TextureSemantic semantic) noexcept
		{
			for (const TextureSemanticItem& item : TextureSemanticItems)
			{
				if (item.m_Value == semantic)
				{
					return item.m_Label;
				}
			}
			return "Unknown";
		}

		[[nodiscard]] std::string PathText(const std::filesystem::path& path)
		{
			return path.empty() ? std::string{} : path.generic_string();
		}

		[[nodiscard]] std::string ModelDisplayName(const AssetSnapshot::Model& model)
		{
			if (!model.m_SourcePath.empty())
			{
				return model.m_SourcePath.filename().generic_string();
			}

			const std::string name = utils::StringIdToString(model.m_Name);
			if (!name.empty())
			{
				return name;
			}

			return std::format("Model {}", model.m_Id.Value());
		}

		[[nodiscard]] std::string TextureDisplayName(const AssetSnapshot::Texture& texture)
		{
			if (!texture.m_SourcePath.empty())
			{
				return texture.m_SourcePath.filename().generic_string();
			}

			const std::string name = utils::StringIdToString(texture.m_Name);
			if (!name.empty())
			{
				return name;
			}

			return std::format("Texture {}", texture.m_Id.Value());
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
				const ModelID modelId = assetManager.LoadModel(path);
				state.m_Status = modelId.IsValid() ?
					std::format("Loaded model {}.", modelId.Value()) :
					"Failed to load model. Only .gltf is currently supported.";
			}
			if (!hasPath)
			{
				ImGui::EndDisabled();
			}

			const auto& models = assetSnapshot.m_Models;
			ImGui::SeparatorText("Loaded Models");
			ImGui::Text("%u models", static_cast<uint32_t>(models.size()));

			if (ImGui::BeginTable("ModelAssetsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 96.0f);
				ImGui::TableSetupColumn("Meshes", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (const auto& model : models)
				{
					const std::string name = ModelDisplayName(model);
					const std::string path = PathText(model.m_SourcePath);

					ImGui::PushID(static_cast<int>(model.m_Id.Value()));
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u", model.m_Id.Value());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(name.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(ModelTypeText(model.m_Type));
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%u", model.m_MeshInstanceCount);
					ImGui::TableSetColumnIndex(4);
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
			const char* semanticPreview = TextureSemanticItems[static_cast<size_t>(state.m_TextureSemanticIndex)].m_Label;
			if (ImGui::BeginCombo("Semantic", semanticPreview))
			{
				for (size_t i = 0; i < TextureSemanticItems.size(); ++i)
				{
					const bool selected = static_cast<size_t>(state.m_TextureSemanticIndex) == i;
					if (ImGui::Selectable(TextureSemanticItems[i].m_Label, selected))
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
				const TextureSemantic semantic = TextureSemanticItems[static_cast<size_t>(state.m_TextureSemanticIndex)].m_Value;
				const TextureID textureId = assetManager.LoadTexture(path, semantic);
				state.m_Status = textureId.IsValid() ?
					std::format("Loaded texture {}.", textureId.Value()) :
					"Failed to load texture.";
			}
			if (!hasPath)
			{
				ImGui::EndDisabled();
			}

			const auto& textures = assetSnapshot.m_Textures;
			ImGui::SeparatorText("Loaded Textures");
			ImGui::Text("%u textures", static_cast<uint32_t>(textures.size()));

			if (ImGui::BeginTable("TextureAssetsTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
				ImGui::TableSetupColumn("Semantic", ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn("Uploaded", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Reserved", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (const auto& texture : textures)
				{
					const std::string name = TextureDisplayName(texture);
					const std::string path = PathText(texture.m_SourcePath);

					ImGui::PushID(static_cast<int>(texture.m_Id.Value()));
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u", texture.m_Id.Value());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(name.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(TextureSemanticText(texture.m_Semantic));
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(utils::BoolToString(texture.m_IsUploaded));
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(utils::BoolToString(texture.m_IsReserved));
					ImGui::TableSetColumnIndex(5);
					ImGui::TextUnformatted(path.empty() ? "<generated>" : path.c_str());
					ImGui::PopID();
				}

				ImGui::EndTable();
			}
			ImGui::PopID();
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
			static_cast<int32_t>(TextureSemanticItems.size() - 1));

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
			ImGui::EndTabBar();
		}
	}
}
