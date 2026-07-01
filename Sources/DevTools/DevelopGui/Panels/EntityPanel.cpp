#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/EntityPanel.h"
#include "Core/Components.h"
#include "Core/Utility/MathUtils.h"
#include "Core/Utility/StringUtils.h"
#include "Core/World.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Graphics/AssetManager.h"
#include "Graphics/AssetSnapshot.h"

#include <algorithm>
#include <vector>

namespace gglab
{
	namespace
	{
		enum class EntityComponentFilter : int32_t
		{
			All,
			Transform,
			Model,
			Light,
		};

		struct EntityPanelState
		{
			entt::entity m_SelectedEntity = entt::null;
			int32_t m_ComponentFilter = static_cast<int32_t>(EntityComponentFilter::All);
			bool m_SelectCreatedEntity = true;
		};

		[[nodiscard]] const char* LightTypeName(LightType type) noexcept
		{
			switch (type)
			{
			case LightType::Directional:
				return "Directional";
			case LightType::Spot:
				return "Spot";
			case LightType::Point:
				return "Point";
			default:
				return "Unknown";
			}
		}

		[[nodiscard]] bool PassesFilter(
			const entt::registry& registry,
			entt::entity entity,
			EntityComponentFilter filter) noexcept
		{
			switch (filter)
			{
			case EntityComponentFilter::Transform:
				return registry.all_of<components::TransformComponent>(entity);
			case EntityComponentFilter::Model:
				return registry.all_of<components::ModelComponent>(entity);
			case EntityComponentFilter::Light:
				return registry.all_of<components::LightComponent>(entity);
			case EntityComponentFilter::All:
			default:
				return true;
			}
		}

		[[nodiscard]] std::string BuildEntityLabel(
			const entt::registry& registry,
			entt::entity entity) noexcept
		{
			std::string label = std::format("Entity {}", entt::to_integral(entity));
			bool hasAnyComponent = false;
			auto appendComponent = [&](std::string_view name)
			{
				label += hasAnyComponent ? ", " : " [";
				label += name;
				hasAnyComponent = true;
			};

			if (registry.all_of<components::TransformComponent>(entity))
			{
				appendComponent("Transform");
			}
			if (registry.all_of<components::ModelComponent>(entity))
			{
				appendComponent("Model");
			}
			if (registry.all_of<components::LightComponent>(entity))
			{
				appendComponent("Light");
			}
			if (hasAnyComponent)
			{
				label += "]";
			}
			return label;
		}

		[[nodiscard]] std::vector<entt::entity> CollectEntities(
			const entt::registry& registry,
			EntityComponentFilter filter)
		{
			std::vector<entt::entity> entities;

			auto addEntity = [&](entt::entity entity)
			{
				if (!PassesFilter(registry, entity, filter) ||
					std::find(entities.begin(), entities.end(), entity) != entities.end())
				{
					return;
				}
				entities.push_back(entity);
			};

			for (const entt::entity entity : registry.view<components::TransformComponent>())
			{
				addEntity(entity);
			}
			for (const entt::entity entity : registry.view<components::ModelComponent>())
			{
				addEntity(entity);
			}
			for (const entt::entity entity : registry.view<components::LightComponent>())
			{
				addEntity(entity);
			}

			std::sort(entities.begin(), entities.end(),
				[](entt::entity lhs, entt::entity rhs)
				{
					return entt::to_integral(lhs) < entt::to_integral(rhs);
				});
			return entities;
		}

		entt::entity CreateEntity(entt::registry& registry) noexcept
		{
			const auto entity = registry.create();
			registry.emplace<components::TransformComponent>(entity);
			return entity;
		}

		void AddDefaultLightComponent(entt::registry& registry, entt::entity entity) noexcept
		{
			if (!registry.valid(entity) ||
				registry.all_of<components::LightComponent>(entity))
			{
				return;
			}

			if (!registry.all_of<components::TransformComponent>(entity))
			{
				registry.emplace<components::TransformComponent>(entity);
			}

			components::LightComponent light{};
			light.m_Type = LightType::Point;
			light.m_Color = color::White;
			light.m_Intensity = 3.0f;
			light.m_Range = 15.0f;
			light.m_SpotAngle = 45.0f;
			registry.emplace<components::LightComponent>(entity, light);
		}

		void AddModelComponent(entt::registry& registry, entt::entity entity, ModelID modelId) noexcept
		{
			if (!registry.valid(entity) ||
				!modelId.IsValid() ||
				registry.all_of<components::ModelComponent>(entity))
			{
				return;
			}

			if (!registry.all_of<components::TransformComponent>(entity))
			{
				registry.emplace<components::TransformComponent>(entity);
			}

			components::ModelComponent model{};
			model.m_ModelId = modelId;
			registry.emplace<components::ModelComponent>(entity, model);
		}

		[[nodiscard]] std::string ModelAssetDisplayName(const AssetSnapshot::Model& model)
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

		void DrawEntityListToolbar(
			entt::registry& registry,
			EntityPanelState& state) noexcept
		{
			ImGui::PushID("EntityListToolbar");
			if (ImGui::Button("Add Entity"))
			{
				const entt::entity entity = CreateEntity(registry);
				if (state.m_SelectCreatedEntity)
				{
					state.m_SelectedEntity = entity;
				}
			}
			ImGui::SameLine();
			ImGui::Checkbox("Select Created", &state.m_SelectCreatedEntity);

			const char* filterItems[] = { "All", "Transform", "Model", "Light" };
			ImGui::Combo("Component Filter", &state.m_ComponentFilter, filterItems, IM_ARRAYSIZE(filterItems));
			ImGui::PopID();
		}

		void DrawEntityList(
			const entt::registry& registry,
			std::span<const entt::entity> entities,
			EntityPanelState& state) noexcept
		{
			ImGui::PushID("EntityList");
			ImGui::SeparatorText("Entities");
			ImGui::Text("%u entities", static_cast<uint32_t>(entities.size()));

			if (!ImGui::BeginChild("List", ImVec2(0.0f, 0.0f), true))
			{
				ImGui::PopID();
				return;
			}

			for (const entt::entity entity : entities)
			{
				const std::string label = BuildEntityLabel(registry, entity);
				const bool selected = entity == state.m_SelectedEntity;
				ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
				if (ImGui::Selectable(label.c_str(), selected))
				{
					state.m_SelectedEntity = entity;
				}
				ImGui::PopID();
			}

			ImGui::EndChild();
			ImGui::PopID();
		}

		void DrawTransformComponent(components::TransformComponent& transform) noexcept
		{
			ImGui::PushID("Component.Transform");
			if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::DragFloat3("Translate", &transform.m_Position.x, 0.05f);

				Vector3 euler = transform.m_Rotation.ToEuler();
				float rotationDegrees[3] = {
					utils::ToDegrees(euler.x),
					utils::ToDegrees(euler.y),
					utils::ToDegrees(euler.z),
				};
				if (ImGui::DragFloat3("Rotate (deg)", rotationDegrees, 0.25f))
				{
					transform.m_Rotation = Quaternion::CreateFromYawPitchRoll(
						utils::ToRadians(rotationDegrees[1]),
						utils::ToRadians(rotationDegrees[0]),
						utils::ToRadians(rotationDegrees[2]));
				}

				if (ImGui::DragFloat3("Scale", &transform.m_Scale.x, 0.01f, 0.001f, 1000.0f))
				{
					transform.m_Scale.x = std::max(transform.m_Scale.x, 0.001f);
					transform.m_Scale.y = std::max(transform.m_Scale.y, 0.001f);
					transform.m_Scale.z = std::max(transform.m_Scale.z, 0.001f);
				}
			}
			ImGui::PopID();
		}

		void DrawDirectionalShadowSettings(DirectionalShadowSettings& settings) noexcept
		{
			ImGui::PushID("DirectionalShadowSettings");
			ImGui::Checkbox("Enable Shadow", &settings.m_Enable);
			ImGui::SameLine();
			ImGui::Checkbox("PCF", &settings.m_EnablePCF);

			int shadowMapSize = static_cast<int>(settings.m_ShadowMapSize);
			if (ImGui::DragInt("Shadow Map Size", &shadowMapSize, 16.0f, 256, 8192))
			{
				settings.m_ShadowMapSize = static_cast<uint32_t>(std::max(shadowMapSize, 1));
			}
			ImGui::DragFloat("Max Shadow Distance", &settings.m_MaxShadowDistance, 1.0f, 1.0f, 10000.0f, "%.1f");
			ImGui::DragFloat("Caster Extrusion", &settings.m_CasterExtrusionDistance, 1.0f, 0.0f, 10000.0f, "%.1f");
			ImGui::DragFloat("Ortho Padding", &settings.m_OrthoPadding, 0.1f, 0.0f, 1000.0f, "%.2f");
			ImGui::DragFloat("Depth Padding", &settings.m_DepthPadding, 0.5f, 0.0f, 10000.0f, "%.1f");
			ImGui::DragFloat("Receiver Depth Bias", &settings.m_ReceiverDepthBias, 0.0001f, 0.0f, 0.1f, "%.5f");
			ImGui::DragInt("Rasterizer Depth Bias", &settings.m_RasterizerDepthBias, 1.0f, -100000, 100000);
			ImGui::DragFloat("Slope Scaled Bias", &settings.m_RasterizerSlopeScaledDepthBias, 0.01f, -100.0f, 100.0f, "%.3f");
			ImGui::PopID();
		}

		void DrawLightComponent(components::LightComponent& light) noexcept
		{
			ImGui::PushID("Component.Light");
			if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const char* lightTypes[] = { "Directional", "Spot", "Point" };
				int type = static_cast<int>(light.m_Type);
				if (ImGui::Combo("Type", &type, lightTypes, IM_ARRAYSIZE(lightTypes)))
				{
					light.m_Type = static_cast<LightType>(std::clamp(
						type,
						static_cast<int>(LightType::Directional),
						static_cast<int>(LightType::Point)));
					if (light.m_Type != LightType::Directional)
					{
						light.m_DirectionalShadowSettings.reset();
					}
				}

				float color[3] = {
					light.m_Color.x,
					light.m_Color.y,
					light.m_Color.z,
				};
				if (ImGui::ColorEdit3("Color", color))
				{
					light.m_Color.x = color[0];
					light.m_Color.y = color[1];
					light.m_Color.z = color[2];
				}

				ImGui::DragFloat("Intensity", &light.m_Intensity, 0.05f, 0.0f, 1000.0f, "%.3f");
				ImGui::DragFloat("Range", &light.m_Range, 0.1f, 0.001f, 10000.0f, "%.2f");
				ImGui::DragFloat("Spot Angle", &light.m_SpotAngle, 0.1f, 0.001f, 179.0f, "%.2f");

				light.m_Intensity = std::max(light.m_Intensity, 0.0f);
				light.m_Range = std::max(light.m_Range, 0.001f);
				light.m_SpotAngle = std::clamp(light.m_SpotAngle, 0.001f, 179.0f);

				if (light.m_Type == LightType::Directional)
				{
					bool castShadows = light.m_DirectionalShadowSettings.has_value();
					if (ImGui::Checkbox("Cast Directional Shadows", &castShadows))
					{
						if (castShadows)
						{
							light.m_DirectionalShadowSettings.emplace();
						}
						else
						{
							light.m_DirectionalShadowSettings.reset();
						}
					}

					if (light.m_DirectionalShadowSettings &&
						ImGui::TreeNode("Directional Shadow Settings"))
					{
						DrawDirectionalShadowSettings(*light.m_DirectionalShadowSettings);
						ImGui::TreePop();
					}
				}

				ImGui::Text("Resolved Type: %s", LightTypeName(light.m_Type));
			}
			ImGui::PopID();
		}

		void DrawModelComponent(
			const components::ModelComponent& model,
			const AssetManager* assetManager) noexcept
		{
			ImGui::PushID("Component.Model");
			if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("ModelID: %u", model.m_ModelId.Value());
				if (assetManager)
				{
					if (const auto* asset = assetManager->GetModel(model.m_ModelId))
					{
						ImGui::Text("Mesh Instances: %u", static_cast<uint32_t>(asset->m_MeshInstance.size()));
						const std::string name = utils::StringIdToString(asset->m_Name);
						if (!name.empty())
						{
							ImGui::Text("Name: %s", name.c_str());
						}
					}
					else
					{
						ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Model asset is not loaded.");
					}
				}
				ImGui::TextDisabled("Model assignment is read-only in this panel for now.");
			}
			ImGui::PopID();
		}

		void DrawAddComponentButton(
			entt::registry& registry,
			entt::entity entity,
			AssetManager* assetManager) noexcept
		{
			ImGui::PushID("AddComponent");
			if (ImGui::Button("+"))
			{
				ImGui::OpenPopup("ComponentMenu");
			}
			if (ImGui::BeginPopup("ComponentMenu"))
			{
				const bool hasTransform = registry.all_of<components::TransformComponent>(entity);
				const bool hasLight = registry.all_of<components::LightComponent>(entity);
				const bool hasModel = registry.all_of<components::ModelComponent>(entity);

				if (ImGui::MenuItem("Transform", "Required", false, !hasTransform))
				{
					registry.emplace<components::TransformComponent>(entity);
				}
				if (ImGui::MenuItem("Light", nullptr, false, !hasLight))
				{
					AddDefaultLightComponent(registry, entity);
				}
				if (hasModel)
				{
					ImGui::MenuItem("Model", nullptr, false, false);
				}
				else if (!assetManager)
				{
					ImGui::MenuItem("Model", "No AssetManager", false, false);
				}
				else if (ImGui::BeginMenu("Model"))
				{
					const AssetSnapshot assetSnapshot = BuildAssetSnapshot(*assetManager);
					if (assetSnapshot.m_Models.empty())
					{
						ImGui::MenuItem("No loaded models", nullptr, false, false);
					}
					for (const auto& model : assetSnapshot.m_Models)
					{
						const std::string label = std::format("{}##{}",
							ModelAssetDisplayName(model),
							model.m_Id.Value());
						if (ImGui::MenuItem(label.c_str()))
						{
							AddModelComponent(registry, entity, model.m_Id);
						}
					}
					ImGui::EndMenu();
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		void DrawSelectedEntity(
			entt::registry& registry,
			EntityPanelState& state,
			AssetManager* assetManager) noexcept
		{
			if (state.m_SelectedEntity == entt::null ||
				!registry.valid(state.m_SelectedEntity))
			{
				ImGui::TextDisabled("No entity selected.");
				state.m_SelectedEntity = entt::null;
				return;
			}

			const entt::entity entity = state.m_SelectedEntity;
			ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
			ImGui::Text("Entity %u", entt::to_integral(entity));
			ImGui::SameLine();
			DrawAddComponentButton(registry, entity, assetManager);
			ImGui::SameLine();
			if (ImGui::Button("Delete Entity"))
			{
				ImGui::OpenPopup("DeleteEntityConfirm");
			}

			if (ImGui::BeginPopupModal("DeleteEntityConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text("Delete Entity %u?", entt::to_integral(entity));
				if (ImGui::Button("Delete"))
				{
					if (registry.valid(entity))
					{
						registry.destroy(entity);
					}
					state.m_SelectedEntity = entt::null;
					ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
					ImGui::PopID();
					return;
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
				{
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			ImGui::Separator();

			if (auto* transform = registry.try_get<components::TransformComponent>(entity))
			{
				DrawTransformComponent(*transform);
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
					"Transform is missing. New entities always include it.");
				if (ImGui::Button("Repair Transform"))
				{
					registry.emplace<components::TransformComponent>(entity);
				}
			}

			if (auto* light = registry.try_get<components::LightComponent>(entity))
			{
				DrawLightComponent(*light);
			}

			if (const auto* model = registry.try_get<components::ModelComponent>(entity))
			{
				DrawModelComponent(*model, assetManager);
			}

			ImGui::PopID();
		}
	}

	void EntityPanel::Draw(DevelopGuiContext& context) noexcept
	{
		if (!context.m_World)
		{
			ImGui::TextUnformatted("No world bound in DevelopGuiContext.");
			return;
		}

		auto& state = context.PanelState<EntityPanelState>();
		auto& registry = context.m_World->GetRegistry();
		const auto filter = static_cast<EntityComponentFilter>(std::clamp(
			state.m_ComponentFilter,
			static_cast<int32_t>(EntityComponentFilter::All),
			static_cast<int32_t>(EntityComponentFilter::Light)));
		const std::vector<entt::entity> entities = CollectEntities(registry, filter);
		if (state.m_SelectedEntity != entt::null &&
			!registry.valid(state.m_SelectedEntity))
		{
			state.m_SelectedEntity = entt::null;
		}

		ImGui::TextUnformatted("Entity");
		ImGui::Separator();

		if (ImGui::BeginTable("EntityPanelLayout", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Entities", ImGuiTableColumnFlags_WidthFixed, 360.0f);
			ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			DrawEntityListToolbar(registry, state);
			DrawEntityList(registry, std::span<const entt::entity>(entities), state);

			ImGui::TableSetColumnIndex(1);
			DrawSelectedEntity(registry, state, context.m_AssetManager);

			ImGui::EndTable();
		}
	}
}
