#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/ProfilingPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Core/Profiling/CpuProfiler.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/Renderer.h"
#include "Diagnostics/DiagnosticsRuntime.h"

namespace gglab
{
	namespace
	{
		struct ProfilingPanelState
		{
			CpuProfileFrameSnapshot m_DisplayedFrame;
			GpuProfileFrameSnapshot m_DisplayedGpuFrame;
			bool m_Paused = false;
		};

		void DrawGpuSamples(const GpuProfileFrameSnapshot& frame) noexcept
		{
			ImGui::TextUnformatted("GPU Passes:");
			if (!frame.IsValid())
			{
				ImGui::TextDisabled("Waiting for completed GPU timestamp data...");
				return;
			}

			constexpr ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp;
			if (!ImGui::BeginTable("GpuProfileSamples", 3, tableFlags))
			{
				return;
			}

			ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("GPU Time", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 55.0f);
			ImGui::TableHeadersRow();

			double profiledMilliseconds = 0.0;
			for (const auto& sample : frame.m_Samples)
			{
				profiledMilliseconds += sample.m_Milliseconds;
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(sample.m_Name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%.3f ms", sample.m_Milliseconds);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%u", sample.m_CallCount);
			}

			const double unscopedMilliseconds =
				std::max(0.0, frame.m_FrameMilliseconds - profiledMilliseconds);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextDisabled("Unscoped / Misc");
			ImGui::TableSetColumnIndex(1);
			ImGui::TextDisabled("%.3f ms", unscopedMilliseconds);
			ImGui::TableSetColumnIndex(2);
			ImGui::TextDisabled("-");

			ImGui::EndTable();
		}

		void DrawCpuSamples(const CpuProfileFrameSnapshot& frame) noexcept
		{
			ImGui::TextUnformatted("CPU:");

			constexpr ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp;
			if (!ImGui::BeginTable("CpuProfileSamples", 4, tableFlags))
			{
				return;
			}

			ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("Inclusive", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Self", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 55.0f);
			ImGui::TableHeadersRow();

			for (const auto& sample : frame.m_Samples)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(sample.m_Name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%.3f ms", sample.m_InclusiveMilliseconds);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.3f ms", sample.m_SelfMilliseconds);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", sample.m_CallCount);
			}

			ImGui::EndTable();
		}

		void DrawSnapshotProfiles(DiagnosticsRuntime* diagnostics) noexcept
		{
			if (!diagnostics || !ImGui::CollapsingHeader("Snapshot Capture"))
			{
				return;
			}
			const auto profiles = diagnostics->GetProfiles();
			if (!ImGui::BeginTable("SnapshotProfiles", 7,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				return;
			}
			ImGui::TableSetupColumn("Snapshot");
			ImGui::TableSetupColumn("Policy");
			ImGui::TableSetupColumn("Last (ms)");
			ImGui::TableSetupColumn("Average (ms)");
			ImGui::TableSetupColumn("Max (ms)");
			ImGui::TableSetupColumn("Captures / Hits");
			ImGui::TableSetupColumn("Refresh");
			ImGui::TableHeadersRow();
			for (const auto& profile : profiles)
			{
				const char* policy = profile.m_Policy == SnapshotUpdatePolicy::EveryFrame ? "Every Frame" :
					profile.m_Policy == SnapshotUpdatePolicy::OnDemand ? "On Demand" : "Manual";
				ImGui::PushID(static_cast<int>(profile.m_Id));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(profile.m_Name.data(), profile.m_Name.data() + profile.m_Name.size());
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(policy);
				ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", profile.m_LastCaptureMilliseconds);
				ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", profile.m_AverageCaptureMilliseconds);
				ImGui::TableSetColumnIndex(4); ImGui::Text("%.3f", profile.m_MaxCaptureMilliseconds);
				ImGui::TableSetColumnIndex(5); ImGui::Text("%llu / %llu",
					static_cast<unsigned long long>(profile.m_CaptureCount),
					static_cast<unsigned long long>(profile.m_CacheHitCount));
				ImGui::TableSetColumnIndex(6);
				if (ImGui::SmallButton("Refresh")) diagnostics->RequestRefresh(profile.m_Id);
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	void ProfilingPanel::Draw(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<ProfilingPanelState>();
		auto& profiler = CpuProfiler::Get();
		GpuProfiler* gpuProfiler = context.m_Renderer ? context.m_Renderer->GetGpuProfiler() : nullptr;

		bool enabled = profiler.IsEnabled();
		if (ImGui::Checkbox("CPU profiling", &enabled))
		{
			profiler.SetEnabled(enabled);
		}
		ImGui::SameLine();
		if (gpuProfiler)
		{
			bool gpuEnabled = gpuProfiler->IsEnabled();
			if (ImGui::Checkbox("GPU profiling", &gpuEnabled))
			{
				gpuProfiler->SetEnabled(gpuEnabled);
			}
		}
		else
		{
			ImGui::BeginDisabled();
			bool gpuEnabled = false;
			ImGui::Checkbox("GPU profiling", &gpuEnabled);
			ImGui::EndDisabled();
		}
		ImGui::SameLine();
		ImGui::Checkbox("Pause display", &state.m_Paused);
		DrawSnapshotProfiles(context.m_Diagnostics);

		if (!state.m_Paused)
		{
			auto latestFrame = profiler.GetLatestFrame();
			if (latestFrame.IsValid() &&
				latestFrame.m_FrameIndex != state.m_DisplayedFrame.m_FrameIndex)
			{
				state.m_DisplayedFrame = std::move(latestFrame);
			}

			if (gpuProfiler)
			{
				auto latestGpuFrame = gpuProfiler->GetLatestFrame();
				if (latestGpuFrame.IsValid() &&
					latestGpuFrame.m_FrameIndex != state.m_DisplayedGpuFrame.m_FrameIndex)
				{
					state.m_DisplayedGpuFrame = std::move(latestGpuFrame);
				}
			}
		}

		ImGui::Separator();
		const auto& frame = state.m_DisplayedFrame;
		if (!frame.IsValid())
		{
			ImGui::TextUnformatted("Waiting for a completed CPU profile frame...");
			return;
		}

		const double fps = frame.m_FrameMilliseconds > 0.0 ?
			1000.0 / frame.m_FrameMilliseconds : 0.0;
		ImGui::Text("Frame: %.2f ms / %.1f fps", frame.m_FrameMilliseconds, fps);
		ImGui::Text("CPU Frame: %.2f ms", frame.m_FrameMilliseconds);
		if (state.m_DisplayedGpuFrame.IsValid())
		{
			ImGui::Text("GPU Frame: %.2f ms", state.m_DisplayedGpuFrame.m_FrameMilliseconds);
		}
		else
		{
			ImGui::TextDisabled("GPU Frame: waiting for timestamp data");
		}
		ImGui::TextDisabled("Frame %llu (latest completed)",
			static_cast<unsigned long long>(frame.m_FrameIndex));

		ImGui::Spacing();
		DrawGpuSamples(state.m_DisplayedGpuFrame);

		ImGui::Spacing();
		DrawCpuSamples(frame);

		ImGui::Spacing();
		ImGui::TextDisabled(
			"Inclusive includes child scopes; Self excludes profiled child scopes.");
	}
}
