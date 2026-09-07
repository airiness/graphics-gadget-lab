#include "DevTools/DevelopGui/Panels/RenderViewPanel.h"
#include "GGLabRuntime/Core/Math/Culling.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Core/StringIdFormatting.h"
#include "DevTools/EnumText/EnumTextGraphics.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiMathWidgets.h"
#include "GGLabRuntime/Graphics/CameraRenderViewQueryBase.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderQueueSnapshot.h"

#include <algorithm>
#include <string>
#include <vector>

#include <imgui.h>

namespace gglab
{
	namespace
	{
		struct RenderViewPanelState
		{
			RenderViewID m_SelectedViewId = RenderViewID::Main;
			bool m_ShowMatrices = true;
			bool m_ShowFrustumPlanes = false;
		};

		const char* RenderViewKindLabel(RenderViewID viewId) noexcept
		{
			if (viewId == RenderViewID::Main)
			{
				return "Main";
			}
			if (viewId == RenderViewID::DirectionalShadow)
			{
				return "Shadow";
			}
			if (IsDebugCameraRenderViewID(viewId))
			{
				return "Debug Camera";
			}
			return "Unknown";
		}

		const char* ProjectionLabel(RenderViewID viewId) noexcept
		{
			return viewId == RenderViewID::DirectionalShadow ? "Orthographic" : "Perspective";
		}

		std::string CullingSourceLabel(const DevelopGuiContext& context, RenderViewID viewId)
		{
			if (viewId == RenderViewID::Main)
			{
				return devtools::EnumText(RenderViewVisibilityMode::Self);
			}
			if (viewId == RenderViewID::DirectionalShadow)
			{
				return "Shadow Queue";
			}
			if (!context.m_CameraRenderViewQuery)
			{
				return devtools::EnumText(RenderViewID::Unknown);
			}
			const auto visibility = context.m_CameraRenderViewQuery->GetRenderViewVisibilityMode(viewId);
			return visibility ? devtools::EnumText(*visibility)
				: devtools::EnumText(RenderViewVisibilityMode::None);
		}

		std::string RenderViewName(const RenderView& view)
		{
			std::string name = utils::StringIdToString(view.m_Name);
			if (!name.empty())
			{
				return name;
			}
			return devtools::EnumText(view.m_ViewId);
		}

		const RenderQueueEntrySnapshot* FindRenderQueue(
			std::span<const RenderQueueEntrySnapshot> queues, RenderViewID viewId) noexcept
		{
			const size_t index = utils::ToIndex(viewId);
			if (index < queues.size() && queues[index].m_ViewId == viewId)
			{
				return &queues[index];
			}
			for (const RenderQueueEntrySnapshot& queue : queues)
			{
				if (queue.m_ViewId == viewId)
				{
					return &queue;
				}
			}
			return nullptr;
		}

		const RenderView* FindRenderView(
			std::span<const RenderView> views, RenderViewID viewId) noexcept
		{
			const size_t index = utils::ToIndex(viewId);
			if (index < views.size() && views[index].m_ViewId == viewId)
			{
				return &views[index];
			}
			for (const RenderView& view : views)
			{
				if (view.m_ViewId == viewId)
				{
					return &view;
				}
			}
			return nullptr;
		}

		RenderViewID ResolveDisplayViewId(const DevelopGuiContext& context) noexcept
		{
			return context.m_CameraRenderViewQuery ? context.m_CameraRenderViewQuery->GetDisplayViewId()
				: RenderViewID::Main;
		}

		RenderViewID ResolveSelectedViewId(
			const RenderViewPanelState& state, std::span<const RenderView> views) noexcept
		{
			if (FindRenderView(views, state.m_SelectedViewId))
			{
				return state.m_SelectedViewId;
			}
			if (FindRenderView(views, RenderViewID::Main))
			{
				return RenderViewID::Main;
			}
			if (!views.empty())
			{
				return views.front().m_ViewId;
			}
			return RenderViewID::Unknown;
		}

		float VisiblePercent(const RenderQueueStatisticsSnapshot& stats) noexcept
		{
			if (stats.m_TotalInstanceCount == 0)
			{
				return 0.0f;
			}
			return 100.0f * static_cast<float>(stats.m_VisibleInstanceCount) /
				static_cast<float>(stats.m_TotalInstanceCount);
		}

		void DrawFrustumPlaneRow(const char* label, const Plane& plane) noexcept
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text(
				"(%.4f, %.4f, %.4f)", plane.m_Normal.m_X, plane.m_Normal.m_Y, plane.m_Normal.m_Z);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%.4f", plane.m_Distance);
		}

		void DrawFrustumPlanes(const RenderView& view) noexcept
		{
			const math::Frustum frustum =
				math::CreateFrustumFromViewProjection(view.m_UnjitteredViewProj);
			if (!ImGui::BeginTable("FrustumPlanes", 3,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchProp))
			{
				return;
			}

			ImGui::TableSetupColumn("Plane");
			ImGui::TableSetupColumn("Normal");
			ImGui::TableSetupColumn("Distance");
			ImGui::TableHeadersRow();

			DrawFrustumPlaneRow("Left", frustum[FrustumPlane::Left]);
			DrawFrustumPlaneRow("Right", frustum[FrustumPlane::Right]);
			DrawFrustumPlaneRow("Bottom", frustum[FrustumPlane::Bottom]);
			DrawFrustumPlaneRow("Top", frustum[FrustumPlane::Top]);
			DrawFrustumPlaneRow("Near", frustum[FrustumPlane::Near]);
			DrawFrustumPlaneRow("Far", frustum[FrustumPlane::Far]);

			ImGui::EndTable();
		}

		void DrawOverview(RenderViewPanelState& state, const DevelopGuiContext& context,
			std::span<const RenderQueueEntrySnapshot> queues) noexcept
		{
			ImGui::SeparatorText("RenderView Overview");
			if (context.m_RenderViews.empty())
			{
				ImGui::TextDisabled("No RenderViews are available.");
				return;
			}

			const RenderViewID displayViewId = ResolveDisplayViewId(context);
			constexpr float VisibleOverviewRowCount = 10.0f;
			const float overviewHeight =
				ImGui::GetTextLineHeightWithSpacing() * (VisibleOverviewRowCount + 2.0f);
			if (!ImGui::BeginTable("RenderViewOverview", 19,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
				ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_SizingFixedFit,
				ImVec2(0.0f, overviewHeight)))
			{
				return;
			}

			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
			ImGui::TableSetupColumn("ViewId");
			ImGui::TableSetupColumn("Valid");
			ImGui::TableSetupColumn("Display");
			ImGui::TableSetupColumn("Kind");
			ImGui::TableSetupColumn("Size");
			ImGui::TableSetupColumn("Projection");
			ImGui::TableSetupColumn("Near");
			ImGui::TableSetupColumn("Far");
			ImGui::TableSetupColumn("FOV");
			ImGui::TableSetupColumn("Aspect");
			ImGui::TableSetupColumn("Culling");
			ImGui::TableSetupColumn("Total");
			ImGui::TableSetupColumn("Visible");
			ImGui::TableSetupColumn("Visible %");
			ImGui::TableSetupColumn("Culled");
			ImGui::TableSetupColumn("Invalid");
			ImGui::TableSetupColumn("Unbounded");
			ImGui::TableSetupColumn("DrawItems");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			for (const RenderView& view : context.m_RenderViews)
			{
				const auto* queue = FindRenderQueue(queues, view.m_ViewId);
				const RenderQueueStatisticsSnapshot stats =
					queue ? queue->m_Statistics : RenderQueueStatisticsSnapshot{};
				const std::string name = RenderViewName(view);

				ImGui::PushID(static_cast<int>(utils::ToIndex(view.m_ViewId)));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				const bool selected = state.m_SelectedViewId == view.m_ViewId;
				if (ImGui::Selectable(name.c_str(), selected,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
				{
					state.m_SelectedViewId = view.m_ViewId;
				}
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(devtools::EnumText(view.m_ViewId).c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(view.m_IsValid ? "Yes" : "No");
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(view.m_ViewId == displayViewId ? "Yes" : "No");
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(RenderViewKindLabel(view.m_ViewId));
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%u x %u", view.m_Width, view.m_Height);
				ImGui::TableSetColumnIndex(6);
				ImGui::TextUnformatted(ProjectionLabel(view.m_ViewId));
				ImGui::TableSetColumnIndex(7);
				ImGui::Text("%.4f", view.m_Near);
				ImGui::TableSetColumnIndex(8);
				ImGui::Text("%.2f", view.m_Far);
				ImGui::TableSetColumnIndex(9);
				if (view.m_ViewId == RenderViewID::DirectionalShadow)
				{
					ImGui::TextUnformatted("N/A");
				}
				else
				{
					ImGui::Text("%.2f", math::ToDegrees(view.m_FovRadians));
				}
				ImGui::TableSetColumnIndex(10);
				ImGui::Text("%.4f", view.m_Aspect);
				ImGui::TableSetColumnIndex(11);
				ImGui::TextUnformatted(CullingSourceLabel(context, view.m_ViewId).c_str());
				ImGui::TableSetColumnIndex(12);
				ImGui::Text("%u", stats.m_TotalInstanceCount);
				ImGui::TableSetColumnIndex(13);
				ImGui::Text("%u", stats.m_VisibleInstanceCount);
				ImGui::TableSetColumnIndex(14);
				ImGui::Text("%.1f%%", VisiblePercent(stats));
				ImGui::TableSetColumnIndex(15);
				ImGui::Text("%u", stats.m_CulledInstanceCount);
				ImGui::TableSetColumnIndex(16);
				ImGui::Text("%u", stats.m_InvalidInstanceCount);
				ImGui::TableSetColumnIndex(17);
				ImGui::Text("%u", stats.m_UnboundedInstanceCount);
				ImGui::TableSetColumnIndex(18);
				ImGui::Text("%u", stats.m_DrawItemCount);
				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		void DrawQueueStatistics(const RenderQueueEntrySnapshot& queue) noexcept
		{
			const RenderQueueStatisticsSnapshot& stats = queue.m_Statistics;
			if (!ImGui::BeginTable("RenderQueueStats", 2,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchProp))
			{
				return;
			}

			auto rowU32 = [](const char* label, uint32_t value) noexcept
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%u", value);
				};
			auto rowFloat = [](const char* label, float value) noexcept
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%.2f%%", value);
				};

			rowU32("Total instances", stats.m_TotalInstanceCount);
			rowU32("Visible instances", stats.m_VisibleInstanceCount);
			rowU32("Culled instances", stats.m_CulledInstanceCount);
			rowU32("Invalid instances", stats.m_InvalidInstanceCount);
			rowU32("Unbounded instances", stats.m_UnboundedInstanceCount);
			rowU32("Draw items", stats.m_DrawItemCount);
			rowFloat("Visible ratio", VisiblePercent(stats));

			ImGui::EndTable();
		}

		void DrawBucketStatistics(const RenderQueueEntrySnapshot& queue) noexcept
		{
			if (!ImGui::BeginTable("RenderQueueBuckets", 5,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp))
			{
				return;
			}

			ImGui::TableSetupColumn("Bucket");
			ImGui::TableSetupColumn("Start");
			ImGui::TableSetupColumn("DrawItems");
			ImGui::TableSetupColumn("Unique Meshes");
			ImGui::TableSetupColumn("Unique Materials");
			ImGui::TableHeadersRow();

			for (size_t bucketIndex = 0; bucketIndex < utils::ToIndex(RenderBucket::Count);
				++bucketIndex)
			{
				const RenderBucket bucket = static_cast<RenderBucket>(bucketIndex);
				const auto& range = queue.m_Buckets[bucketIndex];

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(devtools::EnumText(bucket).c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", range.m_Start);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%u", range.m_Count);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", range.m_UniqueMeshes);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%u", range.m_UniqueMaterials);
			}

			ImGui::EndTable();
		}

		void DrawSelectedRenderView(
			RenderViewPanelState& state, const DevelopGuiContext& context) noexcept
		{
			state.m_SelectedViewId = ResolveSelectedViewId(state, context.m_RenderViews);
			const RenderView* view = FindRenderView(context.m_RenderViews, state.m_SelectedViewId);
			if (!view)
			{
				ImGui::TextDisabled("No RenderView selected.");
				return;
			}

			const std::string name = RenderViewName(*view);
			const std::string viewIdText = devtools::EnumText(view->m_ViewId);
			const std::string cullingText = CullingSourceLabel(context, view->m_ViewId);
			ImGui::SeparatorText("Selected RenderView");
			ImGui::Text("%s (%s)", name.c_str(), viewIdText.c_str());
			ImGui::Text("Valid: %s", view->m_IsValid ? "Yes" : "No");
			ImGui::Text(
				"Display: %s", view->m_ViewId == ResolveDisplayViewId(context) ? "Yes" : "No");
			ImGui::Text("Kind: %s", RenderViewKindLabel(view->m_ViewId));
			ImGui::Text("Projection: %s", ProjectionLabel(view->m_ViewId));
			ImGui::Text("Culling: %s", cullingText.c_str());
			ImGui::Text(
				"Size: %u x %u, Aspect: %.4f", view->m_Width, view->m_Height, view->m_Aspect);
			ImGui::Text("Near/Far: %.4f / %.2f", view->m_Near, view->m_Far);
			if (view->m_ViewId != RenderViewID::DirectionalShadow)
			{
				ImGui::Text("FOV: %.2f deg", math::ToDegrees(view->m_FovRadians));
			}
			ImGui::Text("Exposure Compensation: %+.2f EV", view->m_ExposureCompensationEV);
			ImGui::Text("Exposure Multiplier: %.4fx", view->m_ExposureMultiplier);
			devtools::DrawVector3Text("Camera Position", view->m_CameraPosition);

			ImGui::Checkbox("Show Matrices", &state.m_ShowMatrices);
			ImGui::SameLine();
			ImGui::Checkbox("Show Frustum Planes", &state.m_ShowFrustumPlanes);
			if (state.m_ShowMatrices)
			{
				devtools::DrawMatrix4x4Tree("View", view->m_View);
				devtools::DrawMatrix4x4Tree("UnjitteredProjection", view->m_UnjitteredProj);
				devtools::DrawMatrix4x4Tree(
					"UnjitteredViewProjection", view->m_UnjitteredViewProj);
				devtools::DrawMatrix4x4Tree("RasterProjection", view->m_RasterProj);
				devtools::DrawMatrix4x4Tree("RasterViewProjection", view->m_RasterViewProj);
				devtools::DrawMatrix4x4Tree("InvView", view->m_InvView);
				devtools::DrawMatrix4x4Tree(
					"InvUnjitteredProjection", view->m_InvUnjitteredProj);
				devtools::DrawMatrix4x4Tree(
					"InvUnjitteredViewProjection", view->m_InvUnjitteredViewProj);
				devtools::DrawMatrix4x4Tree("InvRasterProjection", view->m_InvRasterProj);
				devtools::DrawMatrix4x4Tree(
					"InvRasterViewProjection", view->m_InvRasterViewProj);
			}
			if (state.m_ShowFrustumPlanes && view->m_IsValid)
			{
				DrawFrustumPlanes(*view);
			}
		}

		void DrawSelectedQueue(
			const RenderViewPanelState& state, std::span<const RenderQueueEntrySnapshot> queues) noexcept
		{
			const auto* queue = FindRenderQueue(queues, state.m_SelectedViewId);
			ImGui::SeparatorText("RenderQueue");
			if (!queue)
			{
				ImGui::TextDisabled("No RenderQueue is available for the selected RenderView.");
				return;
			}

			DrawQueueStatistics(*queue);
			ImGui::Spacing();
			DrawBucketStatistics(*queue);
		}
	}

	void RenderViewPanel::Draw(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<RenderViewPanelState>();
		const auto* snapshot = context.m_Diagnostics
			? context.m_Diagnostics->GetSnapshot<RenderQueueSnapshot>() : nullptr;
		const auto queues = snapshot ? std::span<const RenderQueueEntrySnapshot>(snapshot->m_Queues)
			: std::span<const RenderQueueEntrySnapshot>{};
		DrawOverview(state, context, queues);
		ImGui::Spacing();
		DrawSelectedRenderView(state, context);
		ImGui::Spacing();
		DrawSelectedQueue(state, queues);
	}
}
