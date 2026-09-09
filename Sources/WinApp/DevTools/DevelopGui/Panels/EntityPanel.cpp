#include "DevTools/DevelopGui/Panels/EntityPanel.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Scene/Components.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Core/StringIdFormatting.h"
#include "GGLabRuntime/Scene/WorldTooling.h"
#include <format>
#include <span>
#include <string>
#include <string_view>
#include "DevTools/AssetSnapshotText.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiProjectionUtils.h"
#include "DevTools/DevelopGui/DevelopGuiStyle.h"
#include "DevTools/EnumText/EnumTextGraphics.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderViewSnapshot.h"
#include "Diagnostics/Snapshots/AssetSnapshot.h"

#include <algorithm>
#include <vector>

#include <imgui.h>

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
			EntityToolingTarget m_SelectedEntity{};
			EntityToolingTarget m_DeleteTarget{};
			bool m_RequestDelete = false;
			int32_t m_ComponentFilter = static_cast<int32_t>(EntityComponentFilter::All);
			bool m_SelectCreatedEntity = true;
			bool m_ShowWorldLinks = true;
		};

		struct EntityListItemAnchor
		{
			EntityToolingTarget m_Entity{};
			ImVec2 m_Position = ImVec2(0.0f, 0.0f);
			bool m_Selected = false;
			bool m_Hovered = false;
		};

		[[nodiscard]] bool PassesFilter(const EntityToolingObservation& entity,
			EntityComponentFilter filter) noexcept
		{
			switch (filter)
			{
			case EntityComponentFilter::Transform: return entity.m_Transform.has_value();
			case EntityComponentFilter::Model: return entity.m_Model.has_value();
			case EntityComponentFilter::Light: return entity.m_Light.has_value();
			default: return true;
			}
		}

		[[nodiscard]] std::string BuildEntityLabel(const EntityToolingObservation& entity)
		{
			std::string label = std::format("Entity {}", entity.m_Target.m_EntityId);
			bool hasAnyComponent = false;
			auto appendComponent = [&](std::string_view name) {
				label += hasAnyComponent ? ", " : " [";
				label += name;
				hasAnyComponent = true;
			};
			if (entity.m_Transform) appendComponent("Transform");
			if (entity.m_Model) appendComponent("Model");
			if (entity.m_Light) appendComponent("Light");
			if (hasAnyComponent) label += "]";
			return label;
		}

		void DrawEntityListToolbar(WorldToolingControlBase* control, EntityPanelState& state) noexcept
		{
			ImGui::PushID("EntityListToolbar");
			ImGui::BeginDisabled(!control);
			if (ImGui::Button("Add Entity") && control)
			{
				const auto entity = control->CreateEntity();
				if (state.m_SelectCreatedEntity)
				{
					state.m_SelectedEntity = entity;
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::Checkbox("Select Created", &state.m_SelectCreatedEntity);
			ImGui::SameLine();
			ImGui::Checkbox("World Links", &state.m_ShowWorldLinks);

			const char* filterItems[] = { "All", "Transform", "Model", "Light" };
			ImGui::Combo("Component Filter", &state.m_ComponentFilter, filterItems,
				IM_ARRAYSIZE(filterItems));
			ImGui::PopID();
		}

		void DrawEntityList(std::span<const EntityToolingObservation> entities,
			EntityPanelState& state, std::vector<EntityListItemAnchor>& anchors) noexcept
		{
			ImGui::PushID("EntityList");
			ImGui::SeparatorText("Entities");
			ImGui::Text("%u entities", static_cast<uint32_t>(entities.size()));

			if (!ImGui::BeginChild("List", ImVec2(0.0f, 0.0f), true))
			{
				ImGui::EndChild();
				ImGui::PopID();
				return;
			}

			for (const auto& observation : entities)
			{
				const auto entity = observation.m_Target;
				const std::string label = BuildEntityLabel(observation);
				ImGui::PushID(static_cast<int>(entity.m_EntityId));
				if (ImGui::Selectable(label.c_str(), entity == state.m_SelectedEntity))
				{
					state.m_SelectedEntity = entity;
				}
				if (ImGui::IsItemVisible())
				{
					const ImVec2 rectMin = ImGui::GetItemRectMin();
					const ImVec2 rectMax = ImGui::GetItemRectMax();

					EntityListItemAnchor anchor{};
					anchor.m_Entity = entity;
					anchor.m_Position = ImVec2(rectMax.x, (rectMin.y + rectMax.y) * 0.5f);
					anchor.m_Selected = entity == state.m_SelectedEntity;
					anchor.m_Hovered =
						ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
					anchors.emplace_back(anchor);
				}
				ImGui::PopID();
			}

			ImGui::EndChild();
			ImGui::PopID();
		}

		void DrawEntityWorldLinks(const WorldToolingSnapshot& snapshot, const EntityPanelState& state,
			std::span<const EntityListItemAnchor> anchors,
			const RenderView* mainRenderView) noexcept
		{
			if (!state.m_ShowWorldLinks || mainRenderView == nullptr)
			{
				return;
			}

			ImDrawList* drawList = ImGui::GetForegroundDrawList();
			if (drawList == nullptr)
			{
				return;
			}

			const ImU32 selectedColor = IM_COL32(255, 214, 96, 230);
			const ImU32 hoveredColor = IM_COL32(96, 180, 255, 210);
			const ImU32 shadowColor = IM_COL32(0, 0, 0, 120);
			for (const EntityListItemAnchor& anchor : anchors)
			{
				const bool selected =
					anchor.m_Selected || anchor.m_Entity == state.m_SelectedEntity;
				const bool shouldDraw = selected || anchor.m_Hovered;
				if (!shouldDraw)
				{
					continue;
				}

				const auto* entity = snapshot.FindEntity(anchor.m_Entity);
				if (!entity || !entity->m_Transform)
				{
					continue;
				}

				ImVec2 worldScreenPosition;
				bool screenClamped = false;
				if (!devtools::ProjectWorldPositionToScreenClamped(
					*mainRenderView, entity->m_Transform->m_Position, worldScreenPosition, screenClamped))
				{
					continue;
				}

				const ImU32 color = selected ? selectedColor : hoveredColor;
				const float thickness = selected ? 2.5f : 1.75f;
				const float radius = selected ? 5.0f : 4.0f;

				drawList->AddLine(ImVec2(anchor.m_Position.x + 1.0f, anchor.m_Position.y + 1.0f),
					ImVec2(worldScreenPosition.x + 1.0f, worldScreenPosition.y + 1.0f), shadowColor,
					thickness + 1.0f);
				drawList->AddLine(anchor.m_Position, worldScreenPosition, color, thickness);
				if (screenClamped)
				{
					drawList->AddCircle(worldScreenPosition, radius + 1.5f, shadowColor, 16, 3.0f);
					drawList->AddCircle(worldScreenPosition, radius, color, 16, 2.0f);
				}
				else
				{
					drawList->AddCircleFilled(worldScreenPosition, radius + 1.0f, shadowColor, 16);
					drawList->AddCircleFilled(worldScreenPosition, radius, color, 16);
				}

				const std::string label =
					std::format("Entity {}", anchor.m_Entity.m_EntityId);
				const ImVec2 labelPosition(
					worldScreenPosition.x + radius + 5.0f, worldScreenPosition.y - radius - 2.0f);
				drawList->AddText(ImVec2(labelPosition.x + 1.0f, labelPosition.y + 1.0f),
					shadowColor, label.c_str());
				drawList->AddText(labelPosition, color, label.c_str());
			}
		}

		bool DrawTransformComponent(components::TransformComponent& transform) noexcept
		{
			bool changed = false;
			ImGui::PushID("Component.Transform");
			if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
			{
				changed |= ImGui::DragFloat3("Translate", &transform.m_Position.m_X, 0.05f);

				Vector3 euler = transform.m_Rotation.ToEuler();
				float rotationDegrees[3] = {
					math::ToDegrees(euler.m_X),
					math::ToDegrees(euler.m_Y),
					math::ToDegrees(euler.m_Z),
				};
				if (ImGui::DragFloat3("Rotate (deg)", rotationDegrees, 0.25f))
				{
					changed = true;
					transform.m_Rotation = math::CreateFromYawPitchRoll(
						math::ToRadians(rotationDegrees[1]), math::ToRadians(rotationDegrees[0]),
						math::ToRadians(rotationDegrees[2]));
				}

				if (ImGui::DragFloat3("Scale", &transform.m_Scale.m_X, 0.01f, 0.001f, 1000.0f))
				{
					changed = true;
					transform.m_Scale.m_X = std::max(transform.m_Scale.m_X, 0.001f);
					transform.m_Scale.m_Y = std::max(transform.m_Scale.m_Y, 0.001f);
					transform.m_Scale.m_Z = std::max(transform.m_Scale.m_Z, 0.001f);
				}
			}
			ImGui::PopID();
			return changed;
		}

		bool DrawDirectionalShadowSettings(DirectionalShadowSettings& settings) noexcept
		{
			bool changed = false;
			ImGui::PushID("DirectionalShadowSettings");
			changed |= ImGui::Checkbox("Enable Shadow", &settings.m_Enable);
			ImGui::SameLine();
			changed |= ImGui::Checkbox("PCF", &settings.m_EnablePCF);

			int shadowMapSize = static_cast<int>(settings.m_ShadowMapSize);
			if (ImGui::DragInt("Shadow Map Size", &shadowMapSize, 16.0f, 256, 8192))
			{
				changed = true;
				settings.m_ShadowMapSize = static_cast<uint32_t>(std::max(shadowMapSize, 1));
			}
			changed |= ImGui::DragFloat(
				"Max Shadow Distance", &settings.m_MaxShadowDistance, 1.0f, 1.0f, 10000.0f, "%.1f");
			changed |= ImGui::DragFloat("Caster Extrusion", &settings.m_CasterExtrusionDistance, 1.0f, 0.0f,
				10000.0f, "%.1f");
			changed |= ImGui::DragFloat(
				"Ortho Padding", &settings.m_OrthoPadding, 0.1f, 0.0f, 1000.0f, "%.2f");
			changed |= ImGui::DragFloat(
				"Depth Padding", &settings.m_DepthPadding, 0.5f, 0.0f, 10000.0f, "%.1f");
			changed |= ImGui::DragFloat(
				"Receiver Depth Bias", &settings.m_ReceiverDepthBias, 0.0001f, 0.0f, 0.1f, "%.5f");
			changed |= ImGui::DragInt(
				"Rasterizer Depth Bias", &settings.m_RasterizerDepthBias, 1.0f, -100000, 100000);
			changed |= ImGui::DragFloat("Slope Scaled Bias", &settings.m_RasterizerSlopeScaledDepthBias, 0.01f,
				-100.0f, 100.0f, "%.3f");
			ImGui::PopID();
			return changed;
		}

		bool DrawLightComponent(components::LightComponent& light) noexcept
		{
			bool changed = false;
			ImGui::PushID("Component.Light");
			if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
			{
				const char* lightTypes[] = { "Directional", "Spot", "Point" };
				int type = static_cast<int>(light.m_Type);
				if (ImGui::Combo("Type", &type, lightTypes, IM_ARRAYSIZE(lightTypes)))
				{
					changed = true;
					light.m_Type = static_cast<LightType>(
						std::clamp(type, static_cast<int>(LightType::Directional),
							static_cast<int>(LightType::Point)));
					if (light.m_Type != LightType::Directional)
					{
						light.m_DirectionalShadowSettings.reset();
					}
				}

				float color[3] = {
					light.m_Color.m_R,
					light.m_Color.m_G,
					light.m_Color.m_B,
				};
				if (ImGui::ColorEdit3("Color", color))
				{
					changed = true;
					light.m_Color.m_R = color[0];
					light.m_Color.m_G = color[1];
					light.m_Color.m_B = color[2];
				}

				changed |= ImGui::DragFloat("Intensity", &light.m_Intensity, 0.05f, 0.0f, 1000.0f, "%.3f");
				changed |= ImGui::DragFloat("Range", &light.m_Range, 0.1f, 0.001f, 10000.0f, "%.2f");
				changed |= ImGui::DragFloat("Spot Angle", &light.m_SpotAngle, 0.1f, 0.001f, 179.0f, "%.2f");

				changed |= light.m_Intensity < 0.0f || light.m_Range < 0.001f ||
					light.m_SpotAngle < 0.001f || light.m_SpotAngle > 179.0f;
				light.m_Intensity = std::max(light.m_Intensity, 0.0f);
				light.m_Range = std::max(light.m_Range, 0.001f);
				light.m_SpotAngle = std::clamp(light.m_SpotAngle, 0.001f, 179.0f);

				if (light.m_Type == LightType::Directional)
				{
					bool castShadows = light.m_DirectionalShadowSettings.has_value();
					if (ImGui::Checkbox("Cast Directional Shadows", &castShadows))
					{
						changed = true;
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
						changed |= DrawDirectionalShadowSettings(*light.m_DirectionalShadowSettings);
						ImGui::TreePop();
					}
				}

				const std::string lightType = devtools::EnumText(light.m_Type);
				ImGui::Text("Resolved Type: %s", lightType.c_str());
			}
			ImGui::PopID();
			return changed;
		}

		void DrawModelComponent(
			const components::ModelComponent& model, const AssetSnapshot* assetSnapshot) noexcept
		{
			ImGui::PushID("Component.Model");
			if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("ModelID: %u", model.m_ModelId.Value());
				if (assetSnapshot)
				{
					const auto asset = std::find_if(assetSnapshot->m_Models.begin(),
						assetSnapshot->m_Models.end(),
						[&](const AssetSnapshot::Model& entry) { return entry.m_Id == model.m_ModelId; });
					if (asset != assetSnapshot->m_Models.end())
					{
						ImGui::Text("Mesh Instances: %u", asset->m_MeshInstanceCount);
						const std::string name = utils::StringIdToString(asset->m_Name);
						if (!name.empty())
						{
							ImGui::Text("Name: %s", name.c_str());
						}
					}
					else
					{
						ImGui::TextColored(
							devtools::style::NoticeTextColor, "Model asset is not present in the current snapshot.");
					}
				}
				else
				{
					ImGui::TextDisabled("Asset diagnostics snapshot is not available.");
				}
				ImGui::TextDisabled("Model assignment is read-only in this panel for now.");
			}
			ImGui::PopID();
		}

		void DrawAddComponentButton(WorldToolingControlBase* control, const EntityToolingObservation& observation,
			const AssetSnapshot* assetSnapshot) noexcept
		{
			ImGui::PushID("AddComponent");
			ImGui::BeginDisabled(!control);
			const auto entity = observation.m_Target;
			if (ImGui::Button("+"))
			{
				ImGui::OpenPopup("ComponentMenu");
			}
			if (ImGui::BeginPopup("ComponentMenu"))
			{
				const bool hasTransform = observation.m_Transform.has_value();
				const bool hasLight = observation.m_Light.has_value();
				const bool hasModel = observation.m_Model.has_value();

				if (ImGui::MenuItem("Transform", "Required", false, !hasTransform))
				{
					if (control) control->AddTransform(entity);
				}
				if (ImGui::MenuItem("Light", nullptr, false, !hasLight))
				{
					if (control) control->AddLight(entity);
				}
				if (hasModel)
				{
					ImGui::MenuItem("Model", nullptr, false, false);
				}
				else if (!assetSnapshot)
				{
					ImGui::MenuItem("Model", "No asset snapshot", false, false);
				}
				else if (ImGui::BeginMenu("Model"))
				{
					if (assetSnapshot->m_Models.empty())
					{
						ImGui::MenuItem("No loaded models", nullptr, false, false);
					}
					for (const auto& model : assetSnapshot->m_Models)
					{
						const std::string label = std::format(
							"{}##{}", devtools::ModelDisplayName(model), model.m_Id.Value());
						if (ImGui::MenuItem(label.c_str()))
						{
							if (control) control->AddModel(entity, model.m_Id);
						}
					}
					ImGui::EndMenu();
				}
				ImGui::EndPopup();
			}
			ImGui::EndDisabled();
			ImGui::PopID();
		}

		void DrawSelectedEntity(const WorldToolingViewBase& view, WorldToolingControlBase* control,
			EntityPanelState& state, const AssetSnapshot* assetSnapshot) noexcept
		{
			auto snapshot = view.GetEntities();
			const auto* selected = snapshot.FindEntity(state.m_SelectedEntity);
			if (!selected)
			{
				ImGui::TextDisabled("No entity selected.");
				state.m_SelectedEntity = {};
				return;
			}
			auto observation = *selected;
			const auto entity = observation.m_Target;
			ImGui::PushID(static_cast<int>(entity.m_EntityId));
			ImGui::Text("Entity %u", entity.m_EntityId);
			ImGui::SameLine();
			DrawAddComponentButton(control, observation, assetSnapshot);
			snapshot = view.GetEntities();
			if (const auto* updated = snapshot.FindEntity(entity)) observation = *updated;
			ImGui::SameLine();
			ImGui::BeginDisabled(!control);
			if (ImGui::Button("Delete Entity"))
			{
				state.m_DeleteTarget = entity;
				state.m_RequestDelete = true;
			}
			ImGui::Separator();
			if (observation.m_Transform)
			{
				if (DrawTransformComponent(*observation.m_Transform) && control)
					control->SetTransform(entity, *observation.m_Transform);
			}
			else
			{
				ImGui::TextColored(devtools::style::NoticeTextColor,
					"Transform is missing. New entities always include it.");
				if (ImGui::Button("Repair Transform") && control) control->AddTransform(entity);
			}
			if (observation.m_Light && DrawLightComponent(*observation.m_Light) && control)
				control->SetLight(entity, *observation.m_Light);
			ImGui::EndDisabled();
			if (observation.m_Model) DrawModelComponent(*observation.m_Model, assetSnapshot);
			ImGui::PopID();
		}

		void DrawDeleteConfirmation(const WorldToolingSnapshot& snapshot,
			WorldToolingControlBase* control, EntityPanelState& state) noexcept
		{
			if (state.m_RequestDelete)
			{
				ImGui::OpenPopup("DeleteEntityConfirm");
				state.m_RequestDelete = false;
			}
			if (!ImGui::BeginPopupModal("DeleteEntityConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
				return;
			if (!snapshot.FindEntity(state.m_DeleteTarget))
			{
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}
			ImGui::Text("Delete Entity %u?", state.m_DeleteTarget.m_EntityId);
			ImGui::BeginDisabled(!control);
			if (ImGui::Button("Delete") && control)
			{
				if (control->DestroyEntity(state.m_DeleteTarget) && state.m_SelectedEntity == state.m_DeleteTarget)
					state.m_SelectedEntity = {};
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

	}

	void EntityPanel::Draw(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<EntityPanelState>();
		if (!context.m_WorldView)
		{
			ImGui::TextUnformatted("World tooling query is not available.");
			state.m_SelectedEntity = {};
			DrawDeleteConfirmation({}, nullptr, state);
			return;
		}

		const auto& view = *context.m_WorldView;
		auto snapshot = view.GetEntities();
		std::vector<EntityListItemAnchor> entityAnchors;
		if (!snapshot.FindEntity(state.m_SelectedEntity))
		{
			state.m_SelectedEntity = {};
		}

		ImGui::TextUnformatted("Entity");
		ImGui::Separator();

		if (ImGui::BeginTable("EntityPanelLayout", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Entities", ImGuiTableColumnFlags_WidthFixed, 360.0f);
			ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			DrawEntityListToolbar(context.m_WorldControl, state);
			snapshot = view.GetEntities();

			const auto updatedFilter = static_cast<EntityComponentFilter>(std::clamp(
				state.m_ComponentFilter, static_cast<int32_t>(EntityComponentFilter::All),
				static_cast<int32_t>(EntityComponentFilter::Light)));
			std::vector<EntityToolingObservation> entities;
			for (const auto& entity : snapshot.m_Entities)
			{
				if (PassesFilter(entity, updatedFilter)) entities.push_back(entity);
			}
			entityAnchors.reserve(entities.size());
			DrawEntityList(entities, state, entityAnchors);

			ImGui::TableSetColumnIndex(1);
			const auto* assetSnapshot = context.m_Diagnostics
				? context.m_Diagnostics->GetSnapshot<AssetSnapshot>()
				: nullptr;
			DrawSelectedEntity(view, context.m_WorldControl, state, assetSnapshot);

			ImGui::EndTable();
		}

		snapshot = view.GetEntities();
		DrawDeleteConfirmation(snapshot, context.m_WorldControl, state);

		const auto* views = context.m_Diagnostics
			? context.m_Diagnostics->GetSnapshot<RenderViewSnapshot>() : nullptr;
		snapshot = view.GetEntities();
		DrawEntityWorldLinks(snapshot, state, std::span<const EntityListItemAnchor>(entityAnchors),
			views ? views->FindView(RenderViewID::Main) : nullptr);
	}
}
