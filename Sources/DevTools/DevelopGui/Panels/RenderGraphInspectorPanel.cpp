#include "Core/Precompiled.h"
#include "Core/Utility/StringUtils.h"
#include "DevTools/EnumText/EnumTextRenderGraph.h"
#include "DevTools/DevelopGui/Panels/RenderGraphInspectorPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Graphics/RenderGraph/RenderGraphSnapshot.h"
#include <algorithm>
#include <cmath>

namespace gglab
{
	namespace
	{
		struct RenderGraphInspectorPanelState
		{
			bool m_HideCulledPasses = false;
			bool m_ShowBarrierDetails = true;
			int32_t m_SelectedPassIndex = -1;
			int32_t m_SelectedResourceIndex = -1;
			int32_t m_GraphPopupPassIndex = -1;
			int32_t m_GraphPopupResourceIndex = -1;
			char m_Filter[128] = {};
		};

		static bool TextMatchesFilter(const std::string& text, const char* filter) noexcept
		{
			if (!filter || !*filter)
			{
				return true;
			}

			const std::string_view filterView(filter);
			const auto match = std::search(
				text.begin(),
				text.end(),
				filterView.begin(),
				filterView.end(),
				[](char lhs, char rhs)
				{
					return std::tolower(static_cast<unsigned char>(lhs)) ==
						std::tolower(static_cast<unsigned char>(rhs));
				});

			return match != text.end();
		}

		static std::string UsageBitsText(uint64_t bits, RGResourceType resourceType) noexcept
		{
			if (resourceType == RGResourceType::RGTexture)
			{
				return devtools::EnumFlagsText<RHITextureUsage>(static_cast<std::underlying_type_t<RHITextureUsage>>(bits));
			}

			return devtools::EnumFlagsText<RHIBufferUsage>(static_cast<std::underlying_type_t<RHIBufferUsage>>(bits));
		}

		static std::string AccessText(uint64_t value, RGResourceType resourceType) noexcept
		{
			if (resourceType == RGResourceType::RGTexture)
			{
				return std::string(devtools::EnumText(static_cast<RGTextureAccess>(value)));
			}

			return std::string(devtools::EnumText(static_cast<RGBufferAccess>(value)));
		}

		static std::string PassIndexToString(int32_t passIndex) noexcept
		{
			return passIndex >= 0 ? std::format("{}", passIndex) : "-";
		}

		static std::string ResourceIndexToString(ResourceIndex resourceIndex) noexcept
		{
			return resourceIndex.IsValid() ? std::format("{}", resourceIndex.Value()) : "-";
		}

		static std::string PassListToString(const std::vector<int32_t>& passIndices) noexcept
		{
			if (passIndices.empty())
			{
				return "-";
			}

			std::string result;
			for (const auto passIndex : passIndices)
			{
				if (!result.empty())
				{
					result += ", ";
				}
				result += PassIndexToString(passIndex);
			}
			return result;
		}

		static std::string ResourceListToString(
			const RGSnapshot& snapshot,
			const std::vector<uint32_t>& resourceIndices) noexcept
		{
			if (resourceIndices.empty())
			{
				return "-";
			}

			std::string result;
			for (const auto resourceIndex : resourceIndices)
			{
				if (!result.empty())
				{
					result += ", ";
				}

				if (resourceIndex < snapshot.m_Resources.size())
				{
					result += std::format("#{} {}", resourceIndex, snapshot.m_Resources[resourceIndex].m_Name);
				}
				else
				{
					result += "?";
				}
			}
			return result;
		}

		static bool PassMatchesFilter(const RGSnapshotPassInfo& pass, const char* filter) noexcept
		{
			if (TextMatchesFilter(pass.m_Name, filter))
			{
				return true;
			}

			for (const auto& access : pass.m_Accesses)
			{
				if (TextMatchesFilter(access.m_ResourceName, filter))
				{
					return true;
				}
			}

			return false;
		}

		static bool ResourceMatchesFilter(const RGSnapshotResourceInfo& resource, const char* filter) noexcept
		{
			return TextMatchesFilter(resource.m_Name, filter);
		}

		static bool PassAccessesResource(const RGSnapshotPassInfo& pass, uint32_t resourceIndex) noexcept
		{
			for (const auto& access : pass.m_Accesses)
			{
				if (access.m_VirtualResourceIndex == resourceIndex)
				{
					return true;
				}
			}

			return false;
		}

		static ImVec2 Add(const ImVec2& lhs, const ImVec2& rhs) noexcept
		{
			return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
		}

		static ImVec2 Subtract(const ImVec2& lhs, const ImVec2& rhs) noexcept
		{
			return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y);
		}

		static ImVec2 Multiply(const ImVec2& value, float scale) noexcept
		{
			return ImVec2(value.x * scale, value.y * scale);
		}

		static ImVec2 RectCenter(const ImVec2& min, const ImVec2& max) noexcept
		{
			return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
		}

		static void DrawClippedText(
			ImDrawList* drawList,
			const ImVec2& min,
			const ImVec2& max,
			const char* text,
			ImU32 color) noexcept
		{
			drawList->PushClipRect(min, max, true);
			drawList->AddText(min, color, text);
			drawList->PopClipRect();
		}

		static void DrawArrow(
			ImDrawList* drawList,
			const ImVec2& start,
			const ImVec2& end,
			ImU32 color,
			float thickness) noexcept
		{
			const ImVec2 delta = Subtract(end, start);
			const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
			if (length <= 1.0f)
			{
				return;
			}

			const ImVec2 direction(delta.x / length, delta.y / length);
			const ImVec2 normal(-direction.y, direction.x);
			const float arrowSize = 7.0f;
			const ImVec2 headBase = Subtract(end, Multiply(direction, arrowSize));

			drawList->AddLine(start, end, color, thickness);
			drawList->AddTriangleFilled(
				end,
				Add(headBase, Multiply(normal, arrowSize * 0.55f)),
				Subtract(headBase, Multiply(normal, arrowSize * 0.55f)),
				color);
		}

		static void BeginGraphNodeTooltip() noexcept
		{
			ImGui::SetNextWindowSizeConstraints(
				ImVec2(340.0f, 0.0f),
				ImVec2(520.0f, std::numeric_limits<float>::max()));
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 500.0f);
		}

		static void EndGraphNodeTooltip() noexcept
		{
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		static void PushGraphNodePopupWrap() noexcept
		{
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 500.0f);
		}

		static void DrawGraphPassPopupContent(const RGSnapshotPassInfo& pass) noexcept
		{
			ImGui::Text("Pass #%u", pass.m_Index);
			ImGui::TextWrapped("%s", pass.m_Name.c_str());
			ImGui::Separator();
			ImGui::Text("Execution Order: %s", PassIndexToString(pass.m_ExecutionOrder).c_str());
			ImGui::Text("Culled: %s", utils::BoolToString(pass.m_Culled));
			ImGui::Text("SideEffect: %s", utils::BoolToString(pass.m_SideEffect));
			ImGui::Text("Accesses: %u", static_cast<uint32_t>(pass.m_Accesses.size()));
			ImGui::Text("Barriers: %u pre / %u post",
				static_cast<uint32_t>(pass.m_PreBarriers.size()),
				static_cast<uint32_t>(pass.m_PostBarriers.size()));
		}

		static void DrawGraphResourcePopupContent(const RGSnapshotResourceInfo& resource) noexcept
		{
			const std::string usage = UsageBitsText(resource.m_UsageBits, resource.m_ResourceType);
			const std::string initialState = devtools::BarrierStateText(resource.m_InitialBarrierState);

			ImGui::Text("Resource #%u", resource.m_Index);
			ImGui::TextWrapped("%s", resource.m_Name.c_str());
			ImGui::Separator();
			ImGui::Text("Type: %s", devtools::EnumText(resource.m_ResourceType).data());
			ImGui::Text("Imported: %s", utils::BoolToString(resource.m_Imported));
			ImGui::Text("Devirtualized: %s", utils::BoolToString(resource.m_Devirtualized));
			ImGui::Text("First User: %s", PassIndexToString(resource.m_FirstUserPassIndex).c_str());
			ImGui::Text("Last User: %s", PassIndexToString(resource.m_LastUserPassIndex).c_str());
			ImGui::TextWrapped("Usage: %s", usage.c_str());
			ImGui::TextWrapped("Initial: %s", initialState.c_str());
		}

		static void DrawSelectedPassDetails(
			const RGSnapshot& snapshot,
			const RenderGraphInspectorPanelState& state) noexcept;

		static void DrawSelectedResourceDetails(
			const RGSnapshot& snapshot,
			const RenderGraphInspectorPanelState& state) noexcept;

		static void DrawGraphTab(
			const RGSnapshot& snapshot,
			RenderGraphInspectorPanelState& state) noexcept
		{
			const bool hasFilter = state.m_Filter[0] != '\0';

			std::vector<uint8_t> resourceMatches(snapshot.m_Resources.size(), false);
			for (size_t resourcePosition = 0; resourcePosition < snapshot.m_Resources.size(); ++resourcePosition)
			{
				resourceMatches[resourcePosition] = ResourceMatchesFilter(snapshot.m_Resources[resourcePosition], state.m_Filter);
			}

			std::vector<size_t> visiblePassPositions;
			visiblePassPositions.reserve(snapshot.m_Passes.size());
			for (size_t passPosition = 0; passPosition < snapshot.m_Passes.size(); ++passPosition)
			{
				const auto& pass = snapshot.m_Passes[passPosition];
				if (state.m_HideCulledPasses && pass.m_Culled)
				{
					continue;
				}

				bool visible = !hasFilter || PassMatchesFilter(pass, state.m_Filter);
				if (!visible)
				{
					for (const auto& access : pass.m_Accesses)
					{
						if (access.m_VirtualResourceIndex < resourceMatches.size() &&
							resourceMatches[access.m_VirtualResourceIndex])
						{
							visible = true;
							break;
						}
					}
				}

				if (visible)
				{
					visiblePassPositions.push_back(passPosition);
				}
			}

			std::sort(visiblePassPositions.begin(), visiblePassPositions.end(),
				[&snapshot](size_t lhs, size_t rhs)
				{
					const auto& lhsPass = snapshot.m_Passes[lhs];
					const auto& rhsPass = snapshot.m_Passes[rhs];
					const int32_t lhsOrder = lhsPass.m_ExecutionOrder >= 0 ?
						lhsPass.m_ExecutionOrder :
						std::numeric_limits<int32_t>::max();
					const int32_t rhsOrder = rhsPass.m_ExecutionOrder >= 0 ?
						rhsPass.m_ExecutionOrder :
						std::numeric_limits<int32_t>::max();
					if (lhsOrder != rhsOrder)
					{
						return lhsOrder < rhsOrder;
					}
					return lhsPass.m_Index < rhsPass.m_Index;
				});

			std::vector<int32_t> passColumnByPosition(snapshot.m_Passes.size(), -1);
			std::vector<int32_t> passColumnByIndex(snapshot.m_Passes.size(), -1);
			for (size_t ordinal = 0; ordinal < visiblePassPositions.size(); ++ordinal)
			{
				const size_t passPosition = visiblePassPositions[ordinal];
				passColumnByPosition[passPosition] = static_cast<int32_t>(ordinal);

				const uint32_t passIndex = snapshot.m_Passes[passPosition].m_Index;
				if (passIndex < passColumnByIndex.size())
				{
					passColumnByIndex[passIndex] = static_cast<int32_t>(ordinal);
				}
			}

			std::vector<uint8_t> visibleResources(snapshot.m_Resources.size(), !hasFilter);
			for (size_t resourcePosition = 0; resourcePosition < snapshot.m_Resources.size(); ++resourcePosition)
			{
				visibleResources[resourcePosition] = visibleResources[resourcePosition] || resourceMatches[resourcePosition];
			}
			for (const size_t passPosition : visiblePassPositions)
			{
				for (const auto& access : snapshot.m_Passes[passPosition].m_Accesses)
				{
					if (access.m_VirtualResourceIndex < visibleResources.size())
					{
						visibleResources[access.m_VirtualResourceIndex] = true;
					}
				}
			}

			std::vector<size_t> visibleResourcePositions;
			visibleResourcePositions.reserve(snapshot.m_Resources.size());
			for (size_t resourcePosition = 0; resourcePosition < visibleResources.size(); ++resourcePosition)
			{
				if (visibleResources[resourcePosition])
				{
					visibleResourcePositions.push_back(resourcePosition);
				}
			}

			if (visiblePassPositions.empty() && visibleResourcePositions.empty())
			{
				ImGui::TextDisabled("No graph items match the current filter.");
				return;
			}

			const ImU32 passFill = ImGui::GetColorU32(ImVec4(0.16f, 0.26f, 0.40f, 1.0f));
			const ImU32 passCulledFill = ImGui::GetColorU32(ImVec4(0.16f, 0.18f, 0.22f, 0.72f));
			const ImU32 passBorder = ImGui::GetColorU32(ImVec4(0.37f, 0.56f, 0.82f, 1.0f));
			const ImU32 resourceTextureFill = ImGui::GetColorU32(ImVec4(0.20f, 0.38f, 0.30f, 1.0f));
			const ImU32 resourceBufferFill = ImGui::GetColorU32(ImVec4(0.38f, 0.30f, 0.18f, 1.0f));
			const ImU32 resourceBorder = ImGui::GetColorU32(ImVec4(0.68f, 0.76f, 0.66f, 1.0f));
			const ImU32 selectedBorder = ImGui::GetColorU32(ImVec4(1.0f, 0.86f, 0.38f, 1.0f));
			const ImU32 readArrow = ImGui::GetColorU32(ImVec4(0.24f, 0.72f, 0.96f, 0.82f));
			const ImU32 writeArrow = ImGui::GetColorU32(ImVec4(1.0f, 0.56f, 0.22f, 0.88f));
			const ImU32 gridColor = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
			const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);

			ImGui::TextUnformatted("Legend:");
			ImGui::SameLine();
			ImGui::ColorButton("##RGGraphPassLegend", ImVec4(0.16f, 0.26f, 0.40f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(12.0f, 12.0f));
			ImGui::SameLine();
			ImGui::TextUnformatted("Pass");
			ImGui::SameLine();
			ImGui::ColorButton("##RGGraphResourceLegend", ImVec4(0.20f, 0.38f, 0.30f, 1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(12.0f, 12.0f));
			ImGui::SameLine();
			ImGui::TextUnformatted("Resource");
			ImGui::SameLine();
			ImGui::ColorButton("##RGGraphReadLegend", ImVec4(0.24f, 0.72f, 0.96f, 0.82f), ImGuiColorEditFlags_NoTooltip, ImVec2(12.0f, 12.0f));
			ImGui::SameLine();
			ImGui::TextUnformatted("Read");
			ImGui::SameLine();
			ImGui::ColorButton("##RGGraphWriteLegend", ImVec4(1.0f, 0.56f, 0.22f, 0.88f), ImGuiColorEditFlags_NoTooltip, ImVec2(12.0f, 12.0f));
			ImGui::SameLine();
			ImGui::TextUnformatted("Write");

			const float leftMargin = 28.0f;
			const float topMargin = 34.0f;
			const float passWidth = 150.0f;
			const float passHeight = 46.0f;
			const float columnStride = 190.0f;
			const float resourceTop = 124.0f;
			const float resourceHeight = 28.0f;
			const float resourceRowStride = 44.0f;
			const float resourceMinWidth = 150.0f;
			const float canvasWidth = std::max(
				ImGui::GetContentRegionAvail().x,
				leftMargin * 2.0f + std::max<size_t>(visiblePassPositions.size(), 1) * columnStride + passWidth);
			const float canvasHeight = std::max(
				360.0f,
				resourceTop + std::max<size_t>(visibleResourcePositions.size(), 1) * resourceRowStride + 48.0f);

			if (!ImGui::BeginChild(
				"RenderGraphGraphCanvas",
				ImVec2(0.0f, 460.0f),
				true,
				ImGuiWindowFlags_HorizontalScrollbar))
			{
				ImGui::EndChild();
				return;
			}

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const ImVec2 childMin = ImGui::GetWindowPos();
			const ImVec2 childMax = Add(childMin, ImGui::GetWindowSize());
			const bool graphInputActive =
				ImGui::IsWindowHovered(ImGuiHoveredFlags_None) &&
				ImGui::IsMouseHoveringRect(childMin, childMax, true);
			ImGui::Dummy(ImVec2(canvasWidth, canvasHeight));
			drawList->PushClipRect(childMin, childMax, true);

			std::vector<ImVec2> passMins(snapshot.m_Passes.size());
			std::vector<ImVec2> passMaxs(snapshot.m_Passes.size());
			std::vector<uint8_t> hasPassRect(snapshot.m_Passes.size(), false);
			std::vector<ImVec2> resourceMins(snapshot.m_Resources.size());
			std::vector<ImVec2> resourceMaxs(snapshot.m_Resources.size());
			std::vector<uint8_t> hasResourceRect(snapshot.m_Resources.size(), false);

			for (size_t ordinal = 0; ordinal < visiblePassPositions.size(); ++ordinal)
			{
				const float x = leftMargin + static_cast<float>(ordinal) * columnStride;
				drawList->AddLine(
					Add(origin, ImVec2(x + passWidth * 0.5f, 8.0f)),
					Add(origin, ImVec2(x + passWidth * 0.5f, canvasHeight - 8.0f)),
					gridColor,
					1.0f);

				const size_t passPosition = visiblePassPositions[ordinal];
				passMins[passPosition] = Add(origin, ImVec2(x, topMargin));
				passMaxs[passPosition] = Add(origin, ImVec2(x + passWidth, topMargin + passHeight));
				hasPassRect[passPosition] = true;
			}

			for (size_t row = 0; row < visibleResourcePositions.size(); ++row)
			{
				const size_t resourcePosition = visibleResourcePositions[row];
				const auto& resource = snapshot.m_Resources[resourcePosition];
				int32_t firstColumn = std::numeric_limits<int32_t>::max();
				int32_t lastColumn = -1;

				for (const size_t passPosition : visiblePassPositions)
				{
					const auto& pass = snapshot.m_Passes[passPosition];
					if (!PassAccessesResource(pass, resource.m_Index))
					{
						continue;
					}

					const int32_t column = passColumnByPosition[passPosition];
					firstColumn = std::min(firstColumn, column);
					lastColumn = std::max(lastColumn, column);
				}

				if (firstColumn == std::numeric_limits<int32_t>::max())
				{
					if (resource.m_FirstUserPassIndex >= 0 &&
						static_cast<size_t>(resource.m_FirstUserPassIndex) < passColumnByIndex.size() &&
						passColumnByIndex[resource.m_FirstUserPassIndex] >= 0)
					{
						firstColumn = passColumnByIndex[resource.m_FirstUserPassIndex];
					}
					else
					{
						firstColumn = 0;
					}
				}

				if (lastColumn < firstColumn)
				{
					if (resource.m_LastUserPassIndex >= 0 &&
						static_cast<size_t>(resource.m_LastUserPassIndex) < passColumnByIndex.size() &&
						passColumnByIndex[resource.m_LastUserPassIndex] >= 0)
					{
						lastColumn = passColumnByIndex[resource.m_LastUserPassIndex];
					}
					else
					{
						lastColumn = firstColumn;
					}
				}

				const float xMin = leftMargin + static_cast<float>(firstColumn) * columnStride;
				const float xMax = std::max(
					xMin + resourceMinWidth,
					leftMargin + static_cast<float>(lastColumn) * columnStride + passWidth);
				const float y = resourceTop + static_cast<float>(row) * resourceRowStride;
				resourceMins[resourcePosition] = Add(origin, ImVec2(xMin, y));
				resourceMaxs[resourcePosition] = Add(origin, ImVec2(xMax, y + resourceHeight));
				hasResourceRect[resourcePosition] = true;
			}

			std::vector<int32_t> visibleReadCounts(snapshot.m_Passes.size(), 0);
			std::vector<int32_t> readSlotCursors(snapshot.m_Passes.size(), 0);
			for (const size_t passPosition : visiblePassPositions)
			{
				const auto& pass = snapshot.m_Passes[passPosition];
				if (!hasPassRect[passPosition])
				{
					continue;
				}

				for (const auto& access : pass.m_Accesses)
				{
					if (access.m_DependencyAccess == RGDependencyAccess::Write ||
						access.m_VirtualResourceIndex >= hasResourceRect.size() ||
						!hasResourceRect[access.m_VirtualResourceIndex])
					{
						continue;
					}

					++visibleReadCounts[passPosition];
				}
			}

			for (const size_t passPosition : visiblePassPositions)
			{
				const auto& pass = snapshot.m_Passes[passPosition];
				if (!hasPassRect[passPosition])
				{
					continue;
				}

				for (const auto& access : pass.m_Accesses)
				{
					if (access.m_VirtualResourceIndex >= hasResourceRect.size() ||
						!hasResourceRect[access.m_VirtualResourceIndex])
					{
						continue;
					}

					const bool writeAccess = access.m_DependencyAccess != RGDependencyAccess::Read;
					float laneX = passMins[passPosition].x + 18.0f;
					if (!writeAccess)
					{
						const int32_t readCount = std::max(visibleReadCounts[passPosition], 1);
						const int32_t readSlot = readSlotCursors[passPosition]++;
						const float readStep = passWidth / static_cast<float>(readCount + 1);
						laneX = passMins[passPosition].x + readStep * static_cast<float>(readSlot + 1);
					}

					const ImVec2 passPort(laneX, passMaxs[passPosition].y + 4.0f);
					const ImVec2 resourcePort(laneX, resourceMins[access.m_VirtualResourceIndex].y - 4.0f);
					const float horizontalLimit = std::min(
						std::max(laneX, resourceMins[access.m_VirtualResourceIndex].x + 12.0f),
						resourceMaxs[access.m_VirtualResourceIndex].x - 12.0f);
					const ImVec2 clampedResourcePort(horizontalLimit, resourcePort.y);

					DrawArrow(
						drawList,
						writeAccess ? passPort : clampedResourcePort,
						writeAccess ? clampedResourcePort : passPort,
						writeAccess ? writeArrow : readArrow,
						1.8f);
				}
			}

			for (const size_t resourcePosition : visibleResourcePositions)
			{
				const auto& resource = snapshot.m_Resources[resourcePosition];
				if (!hasResourceRect[resourcePosition])
				{
					continue;
				}

				const ImVec2 min = resourceMins[resourcePosition];
				const ImVec2 max = resourceMaxs[resourcePosition];
				const bool selected = state.m_SelectedResourceIndex == static_cast<int32_t>(resource.m_Index);
				const bool hovered = graphInputActive && ImGui::IsMouseHoveringRect(min, max, true);
				const ImU32 fill = resource.m_ResourceType == RGResourceType::RGTexture ?
					resourceTextureFill :
					resourceBufferFill;
				const ImU32 border = selected || hovered ? selectedBorder : resourceBorder;
				const float borderThickness = selected ? 2.4f : 1.4f;

				drawList->AddRectFilled(min, max, fill, 5.0f);
				drawList->AddRect(min, max, border, 5.0f, 0, borderThickness);
				if (resource.m_Imported)
				{
					drawList->AddCircleFilled(Add(min, ImVec2(8.0f, 8.0f)), 3.0f, selectedBorder);
				}

				const std::string label = std::format("#{} {}", resource.m_Index, resource.m_Name);
				DrawClippedText(drawList, Add(min, ImVec2(8.0f, 6.0f)), Subtract(max, ImVec2(8.0f, 4.0f)), label.c_str(), textColor);

				if (hovered)
				{
					BeginGraphNodeTooltip();
					DrawGraphResourcePopupContent(resource);
					EndGraphNodeTooltip();

					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						state.m_SelectedResourceIndex = static_cast<int32_t>(resource.m_Index);
						state.m_SelectedPassIndex = -1;
						state.m_GraphPopupResourceIndex = static_cast<int32_t>(resource.m_Index);
						state.m_GraphPopupPassIndex = -1;
						ImGui::OpenPopup("RenderGraphGraphNodePopup");
					}
				}
			}

			for (const size_t passPosition : visiblePassPositions)
			{
				const auto& pass = snapshot.m_Passes[passPosition];
				if (!hasPassRect[passPosition])
				{
					continue;
				}

				const ImVec2 min = passMins[passPosition];
				const ImVec2 max = passMaxs[passPosition];
				const bool selected = state.m_SelectedPassIndex == static_cast<int32_t>(pass.m_Index);
				const bool hovered = graphInputActive && ImGui::IsMouseHoveringRect(min, max, true);
				const ImU32 fill = pass.m_Culled ? passCulledFill : passFill;
				const ImU32 border = selected || hovered ? selectedBorder : passBorder;
				const float borderThickness = selected ? 2.4f : 1.4f;

				drawList->AddRectFilled(min, max, fill, 0.0f);
				drawList->AddRect(min, max, border, 0.0f, 0, borderThickness);

				const std::string label = std::format("#{} {}", pass.m_Index, pass.m_Name);
				const std::string order = std::format("order {}", PassIndexToString(pass.m_ExecutionOrder));
				DrawClippedText(drawList, Add(min, ImVec2(8.0f, 7.0f)), Subtract(max, ImVec2(8.0f, 22.0f)), label.c_str(), textColor);
				DrawClippedText(drawList, Add(min, ImVec2(8.0f, 25.0f)), Subtract(max, ImVec2(8.0f, 5.0f)), order.c_str(), textColor);

				if (hovered)
				{
					BeginGraphNodeTooltip();
					DrawGraphPassPopupContent(pass);
					EndGraphNodeTooltip();

					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						state.m_SelectedPassIndex = static_cast<int32_t>(pass.m_Index);
						state.m_SelectedResourceIndex = -1;
						state.m_GraphPopupPassIndex = static_cast<int32_t>(pass.m_Index);
						state.m_GraphPopupResourceIndex = -1;
						ImGui::OpenPopup("RenderGraphGraphNodePopup");
					}
				}
			}

			drawList->PopClipRect();

			ImGui::SetNextWindowSizeConstraints(
				ImVec2(340.0f, 0.0f),
				ImVec2(520.0f, std::numeric_limits<float>::max()));
			if (ImGui::BeginPopup("RenderGraphGraphNodePopup"))
			{
				PushGraphNodePopupWrap();
				if (state.m_GraphPopupPassIndex >= 0 &&
					static_cast<size_t>(state.m_GraphPopupPassIndex) < snapshot.m_Passes.size())
				{
					DrawGraphPassPopupContent(snapshot.m_Passes[state.m_GraphPopupPassIndex]);
				}
				else if (state.m_GraphPopupResourceIndex >= 0 &&
					static_cast<size_t>(state.m_GraphPopupResourceIndex) < snapshot.m_Resources.size())
				{
					DrawGraphResourcePopupContent(snapshot.m_Resources[state.m_GraphPopupResourceIndex]);
				}

				ImGui::PopTextWrapPos();
				ImGui::EndPopup();
			}

			ImGui::EndChild();

			if (state.m_SelectedPassIndex >= 0)
			{
				DrawSelectedPassDetails(snapshot, state);
			}
			else if (state.m_SelectedResourceIndex >= 0)
			{
				DrawSelectedResourceDetails(snapshot, state);
			}
		}

		static void DrawBarrierTable(const char* label, const std::vector<RGSnapshotBarrierInfo>& barriers) noexcept
		{
			if (barriers.empty())
			{
				ImGui::Text("%s: none", label);
				return;
			}

			ImGui::Text("%s: %u", label, static_cast<uint32_t>(barriers.size()));
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp;

			if (!ImGui::BeginTable(label, 4, flags))
			{
				return;
			}

			ImGui::TableSetupColumn("Resource");
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Before");
			ImGui::TableSetupColumn("After");
			ImGui::TableHeadersRow();

			for (const auto& barrier : barriers)
			{
				const std::string before = devtools::BarrierStateText(barrier.m_Before);
				const std::string after = devtools::BarrierStateText(barrier.m_After);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(barrier.m_ResourceName.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(devtools::EnumText(barrier.m_ResourceType).data());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(before.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(after.c_str());
			}

			ImGui::EndTable();
		}

		static void DrawSelectedPassDetails(
			const RGSnapshot& snapshot,
			const RenderGraphInspectorPanelState& state) noexcept
		{
			if (state.m_SelectedPassIndex < 0 ||
				static_cast<size_t>(state.m_SelectedPassIndex) >= snapshot.m_Passes.size())
			{
				ImGui::TextDisabled("Select a pass to inspect its declared accesses and generated barriers.");
				return;
			}

			const auto& pass = snapshot.m_Passes[state.m_SelectedPassIndex];
			ImGui::SeparatorText("Selected Pass");
			ImGui::Text("Pass #%u: %s", pass.m_Index, pass.m_Name.c_str());
			ImGui::Text("Execution Order: %s", PassIndexToString(pass.m_ExecutionOrder).c_str());
			ImGui::Text("Culled: %s, SideEffect: %s", utils::BoolToString(pass.m_Culled), utils::BoolToString(pass.m_SideEffect));

			const std::string devirtualizeResources = ResourceListToString(snapshot, pass.m_DevirtualizeResources);
			const std::string destroyResources = ResourceListToString(snapshot, pass.m_DestroyResources);
			ImGui::TextWrapped("Devirtualize: %s", devirtualizeResources.c_str());
			ImGui::TextWrapped("Destroy: %s", destroyResources.c_str());

			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp;

			if (ImGui::BeginTable("SelectedPassAccesses", 6, flags))
			{
				ImGui::TableSetupColumn("Resource");
				ImGui::TableSetupColumn("Type");
				ImGui::TableSetupColumn("Dependency");
				ImGui::TableSetupColumn("Access");
				ImGui::TableSetupColumn("Node");
				ImGui::TableSetupColumn("Version");
				ImGui::TableHeadersRow();

				for (const auto& access : pass.m_Accesses)
				{
					const std::string accessText = AccessText(access.m_AccessValue, access.m_ResourceType);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(access.m_ResourceName.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(devtools::EnumText(access.m_ResourceType).data());
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(devtools::EnumText(access.m_DependencyAccess).data());
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(accessText.c_str());
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%u", access.m_ResourceNodeIndex);
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%u", access.m_ResourceVersion);
				}

				ImGui::EndTable();
			}

			if (state.m_ShowBarrierDetails)
			{
				DrawBarrierTable("Pre Barriers", pass.m_PreBarriers);
				DrawBarrierTable("Post Barriers", pass.m_PostBarriers);
			}
		}

		static void DrawSelectedResourceDetails(
			const RGSnapshot& snapshot,
			const RenderGraphInspectorPanelState& state) noexcept
		{
			if (state.m_SelectedResourceIndex < 0 ||
				static_cast<size_t>(state.m_SelectedResourceIndex) >= snapshot.m_Resources.size())
			{
				ImGui::TextDisabled("Select a resource to inspect lifetime and state.");
				return;
			}

			const auto& resource = snapshot.m_Resources[state.m_SelectedResourceIndex];
			const std::string initialState = devtools::BarrierStateText(resource.m_InitialBarrierState);
			const std::string finalState = resource.m_HasFinalBarrierState ?
				devtools::BarrierStateText(resource.m_FinalBarrierState) :
				"-";
			const std::string usage = UsageBitsText(resource.m_UsageBits, resource.m_ResourceType);
			const std::string gpuIndex = ResourceIndexToString(resource.m_GpuResourceIndex);

			ImGui::SeparatorText("Selected Resource");
			ImGui::Text("Resource #%u: %s", resource.m_Index, resource.m_Name.c_str());
			ImGui::Text("Type: %s", devtools::EnumText(resource.m_ResourceType).data());
			ImGui::Text("Imported: %s, Devirtualized: %s, RefCount: %d",
				utils::BoolToString(resource.m_Imported),
				utils::BoolToString(resource.m_Devirtualized),
				resource.m_RefCount);
			ImGui::Text("First User: %s, Last User: %s, GPU Index: %s",
				PassIndexToString(resource.m_FirstUserPassIndex).c_str(),
				PassIndexToString(resource.m_LastUserPassIndex).c_str(),
				gpuIndex.c_str());
			ImGui::TextWrapped("Accumulated Usage: %s", usage.c_str());
			ImGui::TextWrapped("Initial State: %s", initialState.c_str());
			ImGui::TextWrapped("Exported Final State: %s", finalState.c_str());
		}

		static void DrawPassesTab(
			const RGSnapshot& snapshot,
			RenderGraphInspectorPanelState& state) noexcept
		{
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_SizingStretchProp;

			if (ImGui::BeginTable("RenderGraphPasses", 8, flags, ImVec2(0.0f, 280.0f)))
			{
				ImGui::TableSetupColumn("Order");
				ImGui::TableSetupColumn("Index");
				ImGui::TableSetupColumn("Name");
				ImGui::TableSetupColumn("Culled");
				ImGui::TableSetupColumn("SideEffect");
				ImGui::TableSetupColumn("Accesses");
				ImGui::TableSetupColumn("Pre");
				ImGui::TableSetupColumn("Post");
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				for (const auto& pass : snapshot.m_Passes)
				{
					if (state.m_HideCulledPasses && pass.m_Culled)
					{
						continue;
					}
					if (!PassMatchesFilter(pass, state.m_Filter))
					{
						continue;
					}

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(PassIndexToString(pass.m_ExecutionOrder).c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%u", pass.m_Index);
					ImGui::TableSetColumnIndex(2);
					const bool selected = state.m_SelectedPassIndex == static_cast<int32_t>(pass.m_Index);
					const std::string label = std::format("{}##rg_pass_{}", pass.m_Name, pass.m_Index);
					if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
					{
						state.m_SelectedPassIndex = static_cast<int32_t>(pass.m_Index);
					}
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(utils::BoolToString(pass.m_Culled));
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(utils::BoolToString(pass.m_SideEffect));
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%u", static_cast<uint32_t>(pass.m_Accesses.size()));
					ImGui::TableSetColumnIndex(6);
					ImGui::Text("%u", static_cast<uint32_t>(pass.m_PreBarriers.size()));
					ImGui::TableSetColumnIndex(7);
					ImGui::Text("%u", static_cast<uint32_t>(pass.m_PostBarriers.size()));
				}

				ImGui::EndTable();
			}

			DrawSelectedPassDetails(snapshot, state);
		}

		static void DrawResourcesTab(
			const RGSnapshot& snapshot,
			RenderGraphInspectorPanelState& state) noexcept
		{
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_SizingStretchProp;

			if (ImGui::BeginTable("RenderGraphResources", 10, flags, ImVec2(0.0f, 280.0f)))
			{
				ImGui::TableSetupColumn("Index");
				ImGui::TableSetupColumn("Name");
				ImGui::TableSetupColumn("Type");
				ImGui::TableSetupColumn("Imported");
				ImGui::TableSetupColumn("Devirt");
				ImGui::TableSetupColumn("Ref");
				ImGui::TableSetupColumn("First");
				ImGui::TableSetupColumn("Last");
				ImGui::TableSetupColumn("GPU");
				ImGui::TableSetupColumn("Usage");
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				for (const auto& resource : snapshot.m_Resources)
				{
					if (!ResourceMatchesFilter(resource, state.m_Filter))
					{
						continue;
					}

					const std::string usage = UsageBitsText(resource.m_UsageBits, resource.m_ResourceType);
					const std::string gpuIndex = ResourceIndexToString(resource.m_GpuResourceIndex);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u", resource.m_Index);
					ImGui::TableSetColumnIndex(1);
					const bool selected = state.m_SelectedResourceIndex == static_cast<int32_t>(resource.m_Index);
					const std::string label = std::format("{}##rg_resource_{}", resource.m_Name, resource.m_Index);
					if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
					{
						state.m_SelectedResourceIndex = static_cast<int32_t>(resource.m_Index);
					}
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(devtools::EnumText(resource.m_ResourceType).data());
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(utils::BoolToString(resource.m_Imported));
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(utils::BoolToString(resource.m_Devirtualized));
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%d", resource.m_RefCount);
					ImGui::TableSetColumnIndex(6);
					ImGui::TextUnformatted(PassIndexToString(resource.m_FirstUserPassIndex).c_str());
					ImGui::TableSetColumnIndex(7);
					ImGui::TextUnformatted(PassIndexToString(resource.m_LastUserPassIndex).c_str());
					ImGui::TableSetColumnIndex(8);
					ImGui::TextUnformatted(gpuIndex.c_str());
					ImGui::TableSetColumnIndex(9);
					ImGui::TextUnformatted(usage.c_str());
				}

				ImGui::EndTable();
			}

			DrawSelectedResourceDetails(snapshot, state);
		}

		static void DrawResourceNodesTab(const RGSnapshot& snapshot) noexcept
		{
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_SizingStretchProp;

			if (!ImGui::BeginTable("RenderGraphResourceNodes", 7, flags, ImVec2(0.0f, 360.0f)))
			{
				return;
			}

			ImGui::TableSetupColumn("Node");
			ImGui::TableSetupColumn("Resource");
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Slot");
			ImGui::TableSetupColumn("Version");
			ImGui::TableSetupColumn("Writer");
			ImGui::TableSetupColumn("Readers");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			for (const auto& node : snapshot.m_ResourceNodes)
			{
				const std::string readers = PassListToString(node.m_ReaderPassIndices);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%u", node.m_Index);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(node.m_ResourceName.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(devtools::EnumText(node.m_ResourceType).data());
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", node.m_ResourceSlot);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%u", node.m_ResourceVersion);
				ImGui::TableSetColumnIndex(5);
				ImGui::TextUnformatted(PassIndexToString(node.m_WriterPassIndex).c_str());
				ImGui::TableSetColumnIndex(6);
				ImGui::TextUnformatted(readers.c_str());
			}

			ImGui::EndTable();
		}

		static void DrawDependenciesTab(const RGSnapshot& snapshot) noexcept
		{
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_SizingStretchProp;

			if (!ImGui::BeginTable("RenderGraphDependencies", 5, flags, ImVec2(0.0f, 360.0f)))
			{
				return;
			}

			ImGui::TableSetupColumn("From");
			ImGui::TableSetupColumn("To");
			ImGui::TableSetupColumn("Resource");
			ImGui::TableSetupColumn("Node");
			ImGui::TableSetupColumn("Reason");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			for (const auto& edge : snapshot.m_DependencyEdges)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%s #%s", edge.m_FromPassName.c_str(), PassIndexToString(edge.m_FromPassIndex).c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s #%s", edge.m_ToPassName.c_str(), PassIndexToString(edge.m_ToPassIndex).c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(edge.m_ResourceName.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", edge.m_ResourceNodeIndex);
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted("Writer -> Reader");
			}

			ImGui::EndTable();
		}
	}

	void RenderGraphInspectorPanel::Draw(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<RenderGraphInspectorPanelState>();

		ImGui::TextUnformatted("RenderGraph Inspector");
		ImGui::Separator();

		if (!context.m_RenderGraph)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "RenderGraph is null.");
			return;
		}

		RGSnapshot snapshot;
		BuildRenderGraphSnapshot(*context.m_RenderGraph, snapshot);

		uint32_t culledPassCount = 0;
		uint32_t preBarrierCount = 0;
		uint32_t postBarrierCount = 0;
		for (const auto& pass : snapshot.m_Passes)
		{
			culledPassCount += pass.m_Culled ? 1u : 0u;
			preBarrierCount += static_cast<uint32_t>(pass.m_PreBarriers.size());
			postBarrierCount += static_cast<uint32_t>(pass.m_PostBarriers.size());
		}

		ImGui::Text("Passes: %u live / %u total, Resources: %u, Nodes: %u, Edges: %u",
			static_cast<uint32_t>(snapshot.m_Passes.size()) - culledPassCount,
			static_cast<uint32_t>(snapshot.m_Passes.size()),
			static_cast<uint32_t>(snapshot.m_Resources.size()),
			static_cast<uint32_t>(snapshot.m_ResourceNodes.size()),
			static_cast<uint32_t>(snapshot.m_DependencyEdges.size()));
		ImGui::SameLine();
		ImGui::Text("Barriers: %u pre / %u post", preBarrierCount, postBarrierCount);

		ImGui::InputText("Filter", state.m_Filter, IM_ARRAYSIZE(state.m_Filter));
		ImGui::SameLine();
		ImGui::Checkbox("Hide Culled Passes", &state.m_HideCulledPasses);
		ImGui::SameLine();
		ImGui::Checkbox("Show Barrier Details", &state.m_ShowBarrierDetails);

		if (ImGui::BeginTabBar("RenderGraphInspectorTabs"))
		{
			if (ImGui::BeginTabItem("Graph"))
			{
				DrawGraphTab(snapshot, state);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Passes"))
			{
				DrawPassesTab(snapshot, state);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Resources"))
			{
				DrawResourcesTab(snapshot, state);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Resource Nodes"))
			{
				DrawResourceNodesTab(snapshot);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Dependencies"))
			{
				DrawDependenciesTab(snapshot);
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
}
