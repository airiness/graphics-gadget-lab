#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/TaskSystemPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/Snapshots/TaskSystemSnapshot.h"

#include <cctype>
#include <numeric>

namespace gglab
{
	namespace
	{
		constexpr size_t HistoryCapacity = 240;

		struct TaskSystemPanelState
		{
			std::array<char, 128> m_NameFilter{};
			int32_t m_PriorityFilter = -1;
			int32_t m_StatusFilter = -1;
			std::vector<float> m_QueuedHistory;
			std::vector<float> m_RunningHistory;
			std::vector<float> m_CompletionHistory;
		};

		const char* PriorityText(TaskPriority priority) noexcept
		{
			switch (priority)
			{
			case TaskPriority::Critical:
				return "Critical";
			case TaskPriority::High:
				return "High";
			case TaskPriority::Normal:
				return "Normal";
			case TaskPriority::Background:
				return "Background";
			case TaskPriority::Count:
				break;
			}
			return "Unknown";
		}

		const char* StatusText(TaskStatus status) noexcept
		{
			switch (status)
			{
			case TaskStatus::Invalid:
				return "Invalid";
			case TaskStatus::Queued:
				return "Queued";
			case TaskStatus::Running:
				return "Running";
			case TaskStatus::Succeeded:
				return "Succeeded";
			case TaskStatus::Failed:
				return "Failed";
			case TaskStatus::Cancelled:
				return "Cancelled";
			}
			return "Unknown";
		}

		bool ContainsInsensitive(std::string_view text, std::string_view filter) noexcept
		{
			if (filter.empty())
			{
				return true;
			}
			if (filter.size() > text.size())
			{
				return false;
			}
			for (size_t start = 0; start + filter.size() <= text.size(); ++start)
			{
				bool matches = true;
				for (size_t index = 0; index < filter.size(); ++index)
				{
					const auto lhs = static_cast<unsigned char>(text[start + index]);
					const auto rhs = static_cast<unsigned char>(filter[index]);
					if (std::tolower(lhs) != std::tolower(rhs))
					{
						matches = false;
						break;
					}
				}
				if (matches)
				{
					return true;
				}
			}
			return false;
		}

		template <typename T>
		bool MatchesFilters(const T& task, const TaskSystemPanelState& state) noexcept
		{
			return ContainsInsensitive(task.m_Name, state.m_NameFilter.data()) &&
				(state.m_PriorityFilter < 0 ||
					static_cast<int32_t>(task.m_Priority) == state.m_PriorityFilter) &&
				(state.m_StatusFilter < 0 ||
					static_cast<int32_t>(task.m_Status) == state.m_StatusFilter);
		}

		void AppendHistory(std::vector<float>& history, float value)
		{
			if (history.size() == HistoryCapacity)
			{
				history.erase(history.begin());
			}
			history.push_back(value);
		}

		void DrawFilters(TaskSystemPanelState& state) noexcept
		{
			ImGui::SetNextItemWidth(220.0f);
			ImGui::InputTextWithHint("##TaskNameFilter", "Filter by name...",
				state.m_NameFilter.data(), state.m_NameFilter.size());
			ImGui::SameLine();
			ImGui::SetNextItemWidth(125.0f);
			const char* priorityPreview =
				state.m_PriorityFilter < 0
				? "All priorities"
				: PriorityText(static_cast<TaskPriority>(state.m_PriorityFilter));
			if (ImGui::BeginCombo("##TaskPriorityFilter", priorityPreview))
			{
				if (ImGui::Selectable("All priorities", state.m_PriorityFilter < 0))
				{
					state.m_PriorityFilter = -1;
				}
				for (int32_t value = 0; value < static_cast<int32_t>(TaskPriority::Count); ++value)
				{
					if (ImGui::Selectable(PriorityText(static_cast<TaskPriority>(value)),
						state.m_PriorityFilter == value))
					{
						state.m_PriorityFilter = value;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(125.0f);
			const char* statusPreview =
				state.m_StatusFilter < 0
				? "All statuses"
				: StatusText(static_cast<TaskStatus>(state.m_StatusFilter));
			if (ImGui::BeginCombo("##TaskStatusFilter", statusPreview))
			{
				if (ImGui::Selectable("All statuses", state.m_StatusFilter < 0))
				{
					state.m_StatusFilter = -1;
				}
				for (int32_t value = static_cast<int32_t>(TaskStatus::Queued);
					value <= static_cast<int32_t>(TaskStatus::Cancelled); ++value)
				{
					if (ImGui::Selectable(StatusText(static_cast<TaskStatus>(value)),
						state.m_StatusFilter == value))
					{
						state.m_StatusFilter = value;
					}
				}
				ImGui::EndCombo();
			}
		}

		void DrawOverview(const TaskSystemSnapshot& snapshot) noexcept
		{
			const uint32_t queued = std::accumulate(
				snapshot.m_QueuedByPriority.begin(), snapshot.m_QueuedByPriority.end(), 0u);
			if (ImGui::BeginTable("TaskSystemOverview", 4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchSame))
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Workers\n%u", snapshot.m_WorkerCount);
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("Queued\n%u", queued);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("Running\n%u", snapshot.m_RunningCount);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("Completions\n%u", snapshot.m_PendingCompletionCount);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text(
					"Submitted\n%llu", static_cast<unsigned long long>(snapshot.m_SubmittedCount));
				ImGui::TableSetColumnIndex(1);
				ImGui::Text(
					"Succeeded\n%llu", static_cast<unsigned long long>(snapshot.m_SucceededCount));
				ImGui::TableSetColumnIndex(2);
				ImGui::Text(
					"Failed\n%llu", static_cast<unsigned long long>(snapshot.m_FailedCount));
				ImGui::TableSetColumnIndex(3);
				ImGui::Text(
					"Cancelled\n%llu", static_cast<unsigned long long>(snapshot.m_CancelledCount));
				ImGui::EndTable();
			}

			ImGui::Text("Queues: Critical %u | High %u | Normal %u | Background %u",
				snapshot.m_QueuedByPriority[static_cast<size_t>(TaskPriority::Critical)],
				snapshot.m_QueuedByPriority[static_cast<size_t>(TaskPriority::High)],
				snapshot.m_QueuedByPriority[static_cast<size_t>(TaskPriority::Normal)],
				snapshot.m_QueuedByPriority[static_cast<size_t>(TaskPriority::Background)]);
			ImGui::Text("Completion callbacks: %llu succeeded, %llu threw",
				static_cast<unsigned long long>(snapshot.m_CompletionCallbackCount),
				static_cast<unsigned long long>(snapshot.m_CompletionCallbackFailureCount));
		}

		void DrawActiveTasks(
			const TaskSystemSnapshot& snapshot, const TaskSystemPanelState& state) noexcept
		{
			std::vector<const TaskActivity*> tasks;
			for (const TaskActivity& task : snapshot.m_ActiveTasks)
			{
				if (MatchesFilters(task, state))
					tasks.push_back(&task);
			}
			if (!ImGui::BeginTable("ActiveTasks", 9,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
				ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable,
				ImVec2(0.0f, 180.0f)))
			{
				return;
			}
			ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 85.0f);
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Worker", ImGuiTableColumnFlags_WidthFixed, 55.0f);
			ImGui::TableSetupColumn("Queue ms", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableSetupColumn("Run ms", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableHeadersRow();
			if (const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
				sortSpecs && sortSpecs->SpecsCount > 0)
			{
				const ImGuiTableColumnSortSpecs spec = sortSpecs->Specs[0];
				std::sort(tasks.begin(), tasks.end(),
					[spec](const TaskActivity* lhs, const TaskActivity* rhs)
					{
						int comparison = 0;
						switch (spec.ColumnIndex)
						{
						case 0:
							comparison = lhs->m_Handle < rhs->m_Handle ? -1
								: lhs->m_Handle > rhs->m_Handle ? 1
								: 0;
							break;
						case 1:
							comparison = lhs->m_Name.compare(rhs->m_Name);
							break;
						case 2:
							comparison = static_cast<int>(lhs->m_Priority) -
								static_cast<int>(rhs->m_Priority);
							break;
						case 3:
							comparison =
								static_cast<int>(lhs->m_Status) - static_cast<int>(rhs->m_Status);
							break;
						case 4:
							comparison =
								lhs->m_Progress.m_Fraction < rhs->m_Progress.m_Fraction ? -1
								: lhs->m_Progress.m_Fraction > rhs->m_Progress.m_Fraction ? 1
								: 0;
							break;
						case 5:
							comparison = lhs->m_Progress.m_Stage.compare(rhs->m_Progress.m_Stage);
							break;
						case 6:
							comparison = lhs->m_WorkerIndex < rhs->m_WorkerIndex ? -1
								: lhs->m_WorkerIndex > rhs->m_WorkerIndex ? 1
								: 0;
							break;
						case 7:
							comparison = lhs->m_QueueMilliseconds < rhs->m_QueueMilliseconds ? -1
								: lhs->m_QueueMilliseconds > rhs->m_QueueMilliseconds ? 1
								: 0;
							break;
						case 8:
							comparison =
								lhs->m_ExecutionMilliseconds < rhs->m_ExecutionMilliseconds ? -1
								: lhs->m_ExecutionMilliseconds > rhs->m_ExecutionMilliseconds ? 1
								: 0;
							break;
						default:
							break;
						}
						return spec.SortDirection == ImGuiSortDirection_Ascending ? comparison < 0
							: comparison > 0;
					});
			}
			for (const TaskActivity* task : tasks)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%llu", static_cast<unsigned long long>(task->m_Handle.m_Value));
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(task->m_Name.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(PriorityText(task->m_Priority));
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(StatusText(task->m_Status));
				ImGui::TableSetColumnIndex(4);
				if (task->m_Progress.HasProgress())
				{
					ImGui::Text("%.1f%%", task->m_Progress.m_Fraction * 100.0f);
				}
				else
				{
					ImGui::TextUnformatted("-");
				}
				ImGui::TableSetColumnIndex(5);
				ImGui::TextUnformatted(
					task->m_Progress.m_Stage.empty() ? "-" : task->m_Progress.m_Stage.c_str());
				if (!task->m_Progress.m_Detail.empty() && ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", task->m_Progress.m_Detail.c_str());
				}
				ImGui::TableSetColumnIndex(6);
				if (task->m_Status == TaskStatus::Running)
					ImGui::Text("%u", task->m_WorkerIndex);
				else
					ImGui::TextUnformatted("-");
				ImGui::TableSetColumnIndex(7);
				ImGui::Text("%.3f", task->m_QueueMilliseconds);
				ImGui::TableSetColumnIndex(8);
				ImGui::Text("%.3f", task->m_ExecutionMilliseconds);
			}
			ImGui::EndTable();
		}

		void DrawRecentTasks(
			const TaskSystemSnapshot& snapshot, const TaskSystemPanelState& state) noexcept
		{
			std::vector<const TaskCompletionInfo*> tasks;
			for (const TaskCompletionInfo& task : snapshot.m_RecentTasks)
			{
				if (MatchesFilters(task, state))
					tasks.push_back(&task);
			}
			if (!ImGui::BeginTable("RecentTasks", 7,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
				ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable,
				ImVec2(0.0f, 220.0f)))
			{
				return;
			}
			ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 85.0f);
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableSetupColumn("Queue ms", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableSetupColumn("Run ms", ImGuiTableColumnFlags_WidthFixed, 75.0f);
			ImGui::TableSetupColumn("Error", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();
			if (const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
				sortSpecs && sortSpecs->SpecsCount > 0)
			{
				const ImGuiTableColumnSortSpecs spec = sortSpecs->Specs[0];
				std::sort(tasks.begin(), tasks.end(),
					[spec](const TaskCompletionInfo* lhs, const TaskCompletionInfo* rhs)
					{
						int comparison = 0;
						switch (spec.ColumnIndex)
						{
						case 0:
							comparison = lhs->m_Handle < rhs->m_Handle ? -1
								: lhs->m_Handle > rhs->m_Handle ? 1
								: 0;
							break;
						case 1:
							comparison = lhs->m_Name.compare(rhs->m_Name);
							break;
						case 2:
							comparison = static_cast<int>(lhs->m_Priority) -
								static_cast<int>(rhs->m_Priority);
							break;
						case 3:
							comparison =
								static_cast<int>(lhs->m_Status) - static_cast<int>(rhs->m_Status);
							break;
						case 4:
							comparison = lhs->m_QueueMilliseconds < rhs->m_QueueMilliseconds ? -1
								: lhs->m_QueueMilliseconds > rhs->m_QueueMilliseconds ? 1
								: 0;
							break;
						case 5:
							comparison =
								lhs->m_ExecutionMilliseconds < rhs->m_ExecutionMilliseconds ? -1
								: lhs->m_ExecutionMilliseconds > rhs->m_ExecutionMilliseconds ? 1
								: 0;
							break;
						case 6:
							comparison = lhs->m_Error.compare(rhs->m_Error);
							break;
						default:
							break;
						}
						return spec.SortDirection == ImGuiSortDirection_Ascending ? comparison < 0
							: comparison > 0;
					});
			}
			for (const TaskCompletionInfo* task : tasks)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%llu", static_cast<unsigned long long>(task->m_Handle.m_Value));
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(task->m_Name.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(PriorityText(task->m_Priority));
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(StatusText(task->m_Status));
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%.3f", task->m_QueueMilliseconds);
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%.3f", task->m_ExecutionMilliseconds);
				ImGui::TableSetColumnIndex(6);
				ImGui::TextUnformatted(task->m_Error.empty() ? "-" : task->m_Error.c_str());
			}
			ImGui::EndTable();
		}
	}

	void TaskSystemPanel::Draw(DevelopGuiContext& context) noexcept
	{
		const auto* snapshot = context.m_Diagnostics
			? context.m_Diagnostics->GetSnapshot<TaskSystemSnapshot>()
			: nullptr;
		if (!snapshot)
		{
			ImGui::TextDisabled("TaskSystem snapshot provider is not available.");
			return;
		}

		auto& state = context.PanelState<TaskSystemPanelState>();
		const uint32_t queued = std::accumulate(
			snapshot->m_QueuedByPriority.begin(), snapshot->m_QueuedByPriority.end(), 0u);
		AppendHistory(state.m_QueuedHistory, static_cast<float>(queued));
		AppendHistory(state.m_RunningHistory, static_cast<float>(snapshot->m_RunningCount));
		AppendHistory(
			state.m_CompletionHistory, static_cast<float>(snapshot->m_PendingCompletionCount));

		ImGui::TextColored(snapshot->m_AcceptingTasks ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f)
			: ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
			snapshot->m_AcceptingTasks ? "Accepting tasks" : "Not accepting tasks");
		DrawOverview(*snapshot);

		if (snapshot->m_PendingCompletionCount > 128)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
				"Warning: completion backlog is %u.", snapshot->m_PendingCompletionCount);
		}
		if (snapshot->m_CompletionCallbackFailureCount > 0)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.25f, 1.0f),
				"Warning: %llu completion callbacks threw exceptions.",
				static_cast<unsigned long long>(snapshot->m_CompletionCallbackFailureCount));
		}

		if (ImGui::CollapsingHeader("History", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::PlotLines("Queued", state.m_QueuedHistory.data(),
				static_cast<int>(state.m_QueuedHistory.size()), 0, nullptr, 0.0f, FLT_MAX,
				ImVec2(0.0f, 55.0f));
			ImGui::PlotLines("Running", state.m_RunningHistory.data(),
				static_cast<int>(state.m_RunningHistory.size()), 0, nullptr, 0.0f, FLT_MAX,
				ImVec2(0.0f, 55.0f));
			ImGui::PlotLines("Completion backlog", state.m_CompletionHistory.data(),
				static_cast<int>(state.m_CompletionHistory.size()), 0, nullptr, 0.0f, FLT_MAX,
				ImVec2(0.0f, 55.0f));
		}

		DrawFilters(state);
		ImGui::SeparatorText("Active Tasks");
		DrawActiveTasks(*snapshot, state);
		ImGui::SeparatorText("Recent Tasks");
		DrawRecentTasks(*snapshot, state);
	}
}
