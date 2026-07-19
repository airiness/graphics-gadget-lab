#include "Core/Precompiled.h"
#include "Graphics/Utility/DXGIFormatUtils.h"
#include "DevTools/EnumText/EnumTextDXGI.h"
#include "DevTools/EnumText/EnumTextGraphics.h"
#include "DevTools/DevelopGui/Panels/IBLViewerPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiStyle.h"
#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/Snapshots/IBLDiagnosticsSnapshot.h"
#include "Core/Math/MathFunctions.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/EnvironmentAssetController.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/Renderer.h"

namespace gglab
{
	namespace
	{
		struct IBLViewerPanelState
		{
			float m_BrdfLutPreviewSize = 256.0f;
			float m_EnvironmentPreviewWidth = 512.0f;
			float m_IrradiancePreviewWidth = 512.0f;
			float m_PrefilteredSpecularPreviewWidth = 512.0f;
			bool m_ShowMetadata = true;
			bool m_FlipPreviewY = false;
		};

		static bool DrawPreviewLayoutCombo(const char* label, RenderResourceRegistry::IBLPreviewLayout& layout) noexcept
		{
			using PreviewLayout = RenderResourceRegistry::IBLPreviewLayout;
			bool changed = false;

			if (ImGui::BeginCombo(label, devtools::EnumText(layout).data()))
			{
				const PreviewLayout layouts[] = {
					PreviewLayout::Grid2x3,
					PreviewLayout::Cross,
				};

				for (const auto candidate : layouts)
				{
					const bool selected = (layout == candidate);
					if (ImGui::Selectable(devtools::EnumText(candidate).data(), selected))
					{
						layout = candidate;
						changed = true;
					}
					if (selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}

				ImGui::EndCombo();
			}

			return changed;
		}

		static bool DrawPrefilterSampleCountCombo(uint32_t& sampleCount) noexcept
		{
			struct SampleCountOption
			{
				uint32_t m_Value;
				const char* m_Label;
			};

			constexpr SampleCountOption Options[] = {
				{ 64, "64" },
				{ 128, "128" },
				{ 256, "256" },
				{ 512, "512" },
				{ 1024, "1024" },
				{ 2048, "2048" },
				{ 4096, "4096" },
			};

			const auto selected = std::ranges::find(Options, sampleCount, &SampleCountOption::m_Value);
			const char* previewLabel = selected != std::ranges::end(Options) ? selected->m_Label : "Custom";
			bool changed = false;
			if (ImGui::BeginCombo("Prefilter Sample Count", previewLabel))
			{
				for (const auto& option : Options)
				{
					const bool isSelected = sampleCount == option.m_Value;
					if (ImGui::Selectable(option.m_Label, isSelected))
					{
						sampleCount = option.m_Value;
						changed = true;
					}
					if (isSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			return changed;
		}

		static bool DrawQualityPresetCombo(IBLQualityPreset& preset) noexcept
		{
			bool changed = false;
			if (ImGui::BeginCombo("Bake Quality", GetIBLQualityPresetName(preset).data()))
			{
				constexpr IBLQualityPreset Presets[] = {
					IBLQualityPreset::Low,
					IBLQualityPreset::Medium,
					IBLQualityPreset::High,
					IBLQualityPreset::Offline,
				};
				for (const IBLQualityPreset candidate : Presets)
				{
					const bool selected = preset == candidate;
					if (ImGui::Selectable(GetIBLQualityPresetName(candidate).data(), selected))
					{
						preset = candidate;
						changed = true;
					}
					if (selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			return changed;
		}

		static void DrawBakeState(IBLBakeState state) noexcept
		{
			switch (state)
			{
			case IBLBakeState::Ready:
				ImGui::TextColored(devtools::style::SuccessTextColor, "Ready");
				break;
			case IBLBakeState::Dirty:
				ImGui::TextColored(devtools::style::WarningTextColor, "Bake Pending");
				break;
			default:
				ImGui::TextColored(devtools::style::ErrorTextColor, "Unavailable");
				break;
			}
		}

		static void DrawBakePipelineStatus(
			const IBLDiagnosticsSnapshot& snapshot,
			IBLBakeScheduler* scheduler) noexcept
		{
			const auto& bake = snapshot.m_BakeStatus;
			const char* cacheCoverage = bake.m_CacheHit ? "full hit" :
				bake.m_PartialCacheHit ? "partial hit" : "miss";
			ImGui::Text("Stage: %s", GetIBLBakeStageName(bake.m_Stage).data());
			ImGui::ProgressBar(bake.m_Progress, ImVec2(-1.0f, 0.0f));
			ImGui::Text("Generation: requested %llu | baking %llu | active %llu",
				static_cast<unsigned long long>(bake.m_RequestedGeneration),
				static_cast<unsigned long long>(bake.m_BakingGeneration),
				static_cast<unsigned long long>(bake.m_ActiveGeneration));
			ImGui::Text("Cache: %s (%u cached, %u GPU-built)%s",
				cacheCoverage,
				bake.m_CacheHitStageCount,
				bake.m_GpuBuildStageCount,
				bake.m_CacheWritePending ? " | DDC write pending" : "");
			for (size_t index = 0; index < bake.m_Artifacts.size(); ++index)
			{
				const IBLArtifactStage stage = static_cast<IBLArtifactStage>(index);
				const auto& artifact = bake.m_Artifacts[index];
				ImGui::TextDisabled(
					"%s: %s | key %s | artifact %s",
					GetIBLArtifactStageName(stage).data(),
					GetIBLArtifactResolutionName(artifact.m_Resolution).data(),
					DerivedDataKeyText(artifact.m_DerivedDataKey).c_str(),
					ArtifactContentDigestText(artifact.m_ContentDigest).c_str());
			}
			const auto& cpuCache = snapshot.m_ArtifactCache;
			ImGui::Text(
				"CPU cache: %u entries | %.1f / %.1f MiB | hit %llu | miss %llu | evicted %llu",
				cpuCache.m_CachedEntryCount,
				static_cast<double>(cpuCache.m_CachedBytes) / (1024.0 * 1024.0),
				static_cast<double>(cpuCache.m_BudgetBytes) / (1024.0 * 1024.0),
				static_cast<unsigned long long>(cpuCache.m_HitCount),
				static_cast<unsigned long long>(cpuCache.m_MissCount),
				static_cast<unsigned long long>(cpuCache.m_EvictionCount));
			const auto& ddc = snapshot.m_DerivedDataStore;
			ImGui::Text(
				"Local DDC: %llu entries | %.1f MiB | hit %llu | miss %llu | corrupt %llu | write failures %llu",
				static_cast<unsigned long long>(ddc.m_StoredEntryCount),
				static_cast<double>(ddc.m_StoredBytes) / (1024.0 * 1024.0),
				static_cast<unsigned long long>(ddc.m_HitCount),
				static_cast<unsigned long long>(ddc.m_MissCount),
				static_cast<unsigned long long>(ddc.m_CorruptionCount),
				static_cast<unsigned long long>(ddc.m_WriteFailureCount));
			if (scheduler && ImGui::Button("Clear IBL CPU Cache"))
			{
				scheduler->ClearArtifactCache();
			}
			if (scheduler)
			{
				ImGui::SameLine();
				if (ImGui::Button("Clear IBL Local DDC"))
				{
					scheduler->ClearDerivedDataStore();
				}
			}
			if (bake.m_GpuTimingAvailable)
			{
				ImGui::Text("Bake GPU: %.3f ms", bake.m_GpuMilliseconds);
			}
			else
			{
				ImGui::TextDisabled("Bake GPU: unavailable (enable GPU profiling before rebuilding)");
			}

			if (!ImGui::BeginTable("IBLBakePipelineStatus", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				return;
			}

			ImGui::TableSetupColumn("Stage");
			ImGui::TableSetupColumn("Status");
			ImGui::TableHeadersRow();
			const auto drawRow = [](const char* name, const IBLTextureDiagnostics& texture) noexcept
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(name);
					ImGui::TableSetColumnIndex(1);
					DrawBakeState(texture.m_BakeState);
				};
			drawRow("Environment", snapshot.m_Environment);
			drawRow("Irradiance", snapshot.m_Irradiance);
			drawRow("Prefiltered Specular", snapshot.m_PrefilteredSpecular);
			drawRow("BRDF LUT", snapshot.m_BrdfLut);
			ImGui::EndTable();
		}
	}

	void IBLViewerPanel::Draw(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<IBLViewerPanelState>();

		ImGui::TextUnformatted("IBL Viewer");
		ImGui::Separator();

		auto* renderer = context.m_Renderer;
		if (!renderer)
		{
			ImGui::TextColored(devtools::style::ErrorTextColor, "Renderer is null.");
			return;
		}

		auto* renderResRegistry = renderer->GetRenderResourceRegistry();
		if (!renderResRegistry)
		{
			ImGui::TextColored(devtools::style::ErrorTextColor, "RenderResourceRegistry is null.");
			return;
		}

		auto* environmentSystem = renderer->GetEnvironmentLightingSystem();
		auto* environmentAssets = context.m_EnvironmentAssetController;
		const auto* diagnosticsSnapshot = context.m_Diagnostics ?
			context.m_Diagnostics->GetSnapshot<IBLDiagnosticsSnapshot>() : nullptr;

		if (diagnosticsSnapshot && ImGui::CollapsingHeader("Environment Source", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const auto activeIndex = diagnosticsSnapshot->m_ActiveEnvironmentIndex;
			const char* activeLabel = "<Procedural Fallback>";
			if (activeIndex < diagnosticsSnapshot->m_Environments.size())
			{
				activeLabel = diagnosticsSnapshot->m_Environments[activeIndex].m_DisplayName.c_str();
			}

			if (ImGui::BeginCombo("HDR Environment", activeLabel))
			{
				for (const auto& entry : diagnosticsSnapshot->m_Environments)
				{
					const bool selected = entry.m_Active;
					if (ImGui::Selectable(entry.m_DisplayName.c_str(), selected) && environmentAssets)
					{
						GGLAB_UNUSED(environmentAssets->SelectEnvironment(entry.m_Index));
						context.m_Diagnostics->Invalidate<IBLDiagnosticsSnapshot>();
					}
					if (selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			if (activeIndex < diagnosticsSnapshot->m_Environments.size())
			{
				ImGui::TextWrapped("%s", diagnosticsSnapshot->m_Environments[activeIndex].m_Path.string().c_str());
			}
			const IBLEnvironmentEntryDiagnostics* latestSelection = nullptr;
			for (const auto& entry : diagnosticsSnapshot->m_Environments)
			{
				if (entry.m_LastSelectionSerial != 0 &&
					(!latestSelection || entry.m_LastSelectionSerial >
						latestSelection->m_LastSelectionSerial))
				{
					latestSelection = &entry;
				}
			}
			if (latestSelection)
			{
				switch (latestSelection->m_State)
				{
				case IBLEnvironmentEntryState::Loading:
					ImGui::TextDisabled(
						"Loading %s; current environment remains active.",
						latestSelection->m_DisplayName.c_str());
					break;
				case IBLEnvironmentEntryState::Ready:
					ImGui::TextDisabled(
						"Active: %s.",
						latestSelection->m_DisplayName.c_str());
					break;
				case IBLEnvironmentEntryState::Failed:
					ImGui::TextColored(
						devtools::style::ErrorTextColor,
						"Failed to load %s.",
						latestSelection->m_DisplayName.c_str());
					break;
				case IBLEnvironmentEntryState::InvalidShape:
					ImGui::TextColored(
						devtools::style::ErrorTextColor,
						"%s is not a 2:1 environment texture.",
						latestSelection->m_DisplayName.c_str());
					break;
				case IBLEnvironmentEntryState::Unrequested:
					break;
				}
			}
		}

		if (diagnosticsSnapshot && ImGui::CollapsingHeader("Bake Pipeline", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawBakePipelineStatus(
				*diagnosticsSnapshot,
				renderer->GetIBLBakeScheduler());
		}

		if (environmentSystem && ImGui::CollapsingHeader("Environment Settings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const auto settings = environmentSystem->GetSettings();

			bool skyboxEnabled = settings.m_EnableSkybox;
			if (ImGui::Checkbox("Enable Skybox", &skyboxEnabled))
			{
				environmentSystem->SetSkyboxEnabled(skyboxEnabled);
			}

			float intensity = settings.m_Intensity;
			if (ImGui::DragFloat("Environment Intensity", &intensity, 0.01f, 0.0f, 100.0f, "%.3f"))
			{
				environmentSystem->SetIntensity(intensity);
			}

			float rotationDegrees = math::ToDegrees(settings.m_RotationRadians);
			if (ImGui::SliderFloat("Environment Yaw", &rotationDegrees, -180.0f, 180.0f, "%.1f deg"))
			{
				environmentSystem->SetRotationRadians(math::ToRadians(rotationDegrees));
			}

			IBLQualityPreset qualityPreset = settings.m_QualityPreset;
			if (DrawQualityPresetCombo(qualityPreset))
			{
				environmentSystem->SetQualityPreset(qualityPreset);
			}
			const auto& bakeConfig = settings.m_BakeConfig;
			ImGui::TextDisabled(
				"Environment %u | Irradiance %u (%u samples) | Specular %u (%u mips)",
				bakeConfig.m_EnvironmentCubemapSize,
				bakeConfig.m_IrradianceCubemapSize,
				bakeConfig.m_IrradianceSampleCount,
				bakeConfig.m_PrefilteredSpecularCubemapSize,
				bakeConfig.m_PrefilteredSpecularMipLevels);

			uint32_t sampleCount = settings.m_BakeConfig.m_PrefilteredSpecularSampleCount;
			if (DrawPrefilterSampleCountCombo(sampleCount))
			{
				environmentSystem->SetPrefilteredSpecularSampleCount(sampleCount);
			}

			float maxSampleLuminance = settings.m_BakeConfig.m_PrefilteredSpecularMaxSampleLuminance;
			if (ImGui::DragFloat(
				"Prefilter Firefly Clamp",
				&maxSampleLuminance,
				10.0f,
				1.0f,
				65000.0f,
				"%.0f"))
			{
				environmentSystem->SetPrefilteredSpecularMaxSampleLuminance(maxSampleLuminance);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip(
					"Limits individual HDR samples while baking rough specular mips.\n"
					"Mip 0 remains an exact copy of the environment.");
			}

			ImGui::TextDisabled("Skybox, diffuse IBL, specular IBL, and previews share these settings.");
			if (ImGui::Button("Rebuild IBL"))
			{
				environmentSystem->RequestRebake(true);
			}
			ImGui::TextDisabled("Bake changes are generated in staging resources and published atomically.");
		}

		ImGui::Spacing();

		using TextureIndex = RenderResourceRegistry::TextureIndex;
		constexpr TextureIndex EnvironmentIndex = TextureIndex::IBL_EnvironmentCubemap;
		constexpr TextureIndex EnvironmentPreviewIndex = TextureIndex::Preview_IBL_EnvironmentCubemap;
		constexpr TextureIndex IrradianceIndex = TextureIndex::IBL_IrradianceCubemap;
		constexpr TextureIndex IrradiancePreviewIndex = TextureIndex::Preview_IBL_IrradianceCubemap;
		constexpr TextureIndex PrefilteredSpecularIndex = TextureIndex::IBL_PrefilteredSpecularCubemap;
		constexpr TextureIndex PrefilteredSpecularPreviewIndex = TextureIndex::Preview_IBL_PrefilteredSpecularCubemap;
		constexpr TextureIndex BrdfLutIndex = TextureIndex::IBL_BrdfLut;

		// Make sure the persistent BRDF LUT resource exists.
		// This only creates the texture and descriptor if missing.
		// Actual content is generated by RenderPassIBLBrdfLUT.
		const auto* brdfLutDesc = renderResRegistry->GetTextureDesc(BrdfLutIndex);
		if (!brdfLutDesc)
		{
			ImGui::TextColored(devtools::style::ErrorTextColor, "BRDF LUT texture is not allocated.");
			return;
		}

		if (ImGui::CollapsingHeader("IBL BRDF LUT"))
		{
			DrawBakeState(diagnosticsSnapshot ?
				diagnosticsSnapshot->m_BrdfLut.m_BakeState :
				(renderResRegistry->IsDirty(BrdfLutIndex) ? IBLBakeState::Dirty : IBLBakeState::Ready));

			ImGui::Checkbox("Show Metadata", &state.m_ShowMetadata);

			if (state.m_ShowMetadata)
			{
				const uint32_t srvIndex = renderResRegistry->GetShaderVisibleSrvIndex(BrdfLutIndex);

				ImGui::Text("Size: %llu x %u",
					static_cast<unsigned long long>(brdfLutDesc->m_Extent.m_Width),
					brdfLutDesc->m_Extent.m_Height);

				ImGui::Text("MipLevels: %u", brdfLutDesc->m_MipLevels);
				ImGui::Text("Format: %s", devtools::EnumText(ToDXGIFormat(brdfLutDesc->m_Format)).data());
				ImGui::Text("Shader Visible SRV Index: %u", srvIndex);
			}

			ImGui::Separator();

			ImGui::SliderFloat(
				"Preview Size",
				&state.m_BrdfLutPreviewSize,
				64.0f,
				512.0f,
				"%.0f");

			ImGui::Checkbox("Flip Preview Y", &state.m_FlipPreviewY);

			const ImTextureID textureId = devtools::ResolveImGuiTextureId(
				context.m_DevelopGuiSystem,
				renderResRegistry->GetSrvDescriptor(BrdfLutIndex));

			if (!textureId)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor, "BRDF LUT SRV GPU handle is invalid.");
				return;
			}

			const float previewSize = std::clamp(state.m_BrdfLutPreviewSize, 16.0f, 2048.0f);
			const ImVec2 imageSize(previewSize, previewSize);

			const ImVec2 uv0 = state.m_FlipPreviewY
				? ImVec2(0.0f, 1.0f)
				: ImVec2(0.0f, 0.0f);

			const ImVec2 uv1 = state.m_FlipPreviewY
				? ImVec2(1.0f, 0.0f)
				: ImVec2(1.0f, 1.0f);

			ImGui::TextUnformatted("Preview");
			ImGui::Image(textureId, imageSize, uv0, uv1);

			ImVec2 imageMin = ImGui::GetItemRectMin();
			ImVec2 imageMax = ImGui::GetItemRectMax();

			if (ImGui::IsItemHovered())
			{
				ImVec2 mouse = ImGui::GetMousePos();

				float u = (mouse.x - imageMin.x) / (imageMax.x - imageMin.x);
				float v = (mouse.y - imageMin.y) / (imageMax.y - imageMin.y);

				u = std::clamp(u, 0.0f, 1.0f);
				v = std::clamp(v, 0.0f, 1.0f);

				if (state.m_FlipPreviewY)
				{
					v = 1.0f - v;
				}

				ImGui::BeginTooltip();
				ImGui::Text("NoV: %.3f", u);
				ImGui::Text("Perceptual Roughness: %.3f", v);
				ImGui::EndTooltip();
			}

			ImGui::TextDisabled("Expected axis: X = NoV, Y = perceptual roughness.");
		}

		ImGui::Spacing();

		const auto* environmentDesc = renderResRegistry->GetTextureDesc(EnvironmentIndex);
		const auto* environmentPreviewDesc = renderResRegistry->GetTextureDesc(EnvironmentPreviewIndex);

		if (ImGui::CollapsingHeader("IBL Environment"))
		{
			renderResRegistry->RequestIBLPreview(RenderResourceRegistry::IBLPreviewType::Environment);
			DrawBakeState(diagnosticsSnapshot ?
				diagnosticsSnapshot->m_Environment.m_BakeState :
				(renderResRegistry->IsDirty(EnvironmentIndex) ? IBLBakeState::Dirty : IBLBakeState::Ready));

			if (!environmentDesc || !environmentPreviewDesc)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor, "Environment preview texture is not allocated.");
				return;
			}

			using PreviewLayout = RenderResourceRegistry::IBLPreviewLayout;
			PreviewLayout previewLayout = renderResRegistry->GetIBLEnvironmentPreviewLayout();
			if (DrawPreviewLayoutCombo("Display Mode##EnvironmentPreviewLayout", previewLayout))
			{
				renderResRegistry->SetIBLEnvironmentPreviewLayout(previewLayout);
			}

			const uint32_t environmentMipLevels = environmentDesc->m_MipLevels;
			const uint32_t maxEnvironmentMip = environmentMipLevels > 0 ? environmentMipLevels - 1u : 0u;
			uint32_t selectedEnvironmentMip = std::min(
				renderResRegistry->GetIBLEnvironmentPreviewMip(),
				maxEnvironmentMip);
			if (selectedEnvironmentMip != renderResRegistry->GetIBLEnvironmentPreviewMip())
			{
				renderResRegistry->SetIBLEnvironmentPreviewMip(selectedEnvironmentMip);
			}

			int selectedEnvironmentMipInt = static_cast<int>(selectedEnvironmentMip);
			if (ImGui::SliderInt("Source Mip", &selectedEnvironmentMipInt, 0, static_cast<int>(maxEnvironmentMip)))
			{
				selectedEnvironmentMip = static_cast<uint32_t>(
					std::clamp(selectedEnvironmentMipInt, 0, static_cast<int>(maxEnvironmentMip)));
				renderResRegistry->SetIBLEnvironmentPreviewMip(selectedEnvironmentMip);
			}

			const uint64_t selectedEnvironmentWidth = std::max<uint64_t>(
				1u,
				environmentDesc->m_Extent.m_Width >> selectedEnvironmentMip);
			const uint32_t selectedEnvironmentHeight = std::max(
				1u,
				environmentDesc->m_Extent.m_Height >> selectedEnvironmentMip);
			ImGui::Text("Selected Source Mip: %u / %u (%llu x %u)",
				selectedEnvironmentMip,
				maxEnvironmentMip,
				static_cast<unsigned long long>(selectedEnvironmentWidth),
				selectedEnvironmentHeight);

			if (state.m_ShowMetadata)
			{
				const uint32_t environmentSrvIndex = renderResRegistry->GetShaderVisibleSrvIndex(EnvironmentIndex);
				const uint32_t previewSrvIndex = renderResRegistry->GetShaderVisibleSrvIndex(EnvironmentPreviewIndex);

				ImGui::Text("Environment Size: %llu x %u x %u",
					static_cast<unsigned long long>(environmentDesc->m_Extent.m_Width),
					environmentDesc->m_Extent.m_Height,
					environmentDesc->m_ArraySize);
				ImGui::Text("Environment Format: %s", devtools::EnumText(ToDXGIFormat(environmentDesc->m_Format)).data());
				ImGui::Text("Environment MipLevels: %u", environmentMipLevels);
				ImGui::Text("Environment Shader Visible SRV Index: %u", environmentSrvIndex);

				ImGui::Text("Preview Canvas Size: %llu x %u",
					static_cast<unsigned long long>(environmentPreviewDesc->m_Extent.m_Width),
					environmentPreviewDesc->m_Extent.m_Height);
				ImGui::Text("Preview Format: %s", devtools::EnumText(ToDXGIFormat(environmentPreviewDesc->m_Format)).data());
				ImGui::Text("Preview Shader Visible SRV Index: %u", previewSrvIndex);
			}

			ImGui::SliderFloat(
				"Environment Preview Width",
				&state.m_EnvironmentPreviewWidth,
				192.0f,
				768.0f,
				"%.0f");

			const ImTextureID environmentPreviewTextureId =
				devtools::ResolveImGuiTextureId(
					context.m_DevelopGuiSystem,
					renderResRegistry->GetSrvDescriptor(EnvironmentPreviewIndex));

			if (!environmentPreviewTextureId)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor, "Environment preview SRV GPU handle is invalid.");
				return;
			}

			const float environmentPreviewWidth = std::clamp(state.m_EnvironmentPreviewWidth, 16.0f, 2048.0f);

			ImVec2 uv0(0.0f, 0.0f);
			ImVec2 uv1(1.0f, 1.0f);
			float aspect = 3.0f / 4.0f;
			const char* layoutHint = "Cross Layout: +Y / -X +Z +X -Z / -Y";

			if (previewLayout == PreviewLayout::Grid2x3)
			{
				uv1 = ImVec2(3.0f / 4.0f, 2.0f / 3.0f);
				aspect = 2.0f / 3.0f;
				layoutHint = "2x3 Layout: +X -X +Y / -Y +Z -Z";
			}

			const ImVec2 environmentImageSize(environmentPreviewWidth, environmentPreviewWidth * aspect);

			ImGui::TextUnformatted(layoutHint);
			if (!diagnosticsSnapshot || diagnosticsSnapshot->m_EnvironmentPreview.m_UpdateCount > 0)
			{
				ImGui::Image(environmentPreviewTextureId, environmentImageSize, uv0, uv1);
			}
			else
			{
				ImGui::TextDisabled("Preview requested; it will be available next frame.");
			}
			if (diagnosticsSnapshot)
			{
				const auto& preview = diagnosticsSnapshot->m_EnvironmentPreview;
				ImGui::TextDisabled("Preview updates: %llu%s",
					static_cast<unsigned long long>(preview.m_UpdateCount),
					preview.m_Dirty ? " (refresh pending)" : "");
			}
		}

		ImGui::Spacing();

		const auto* irradianceDesc = renderResRegistry->GetTextureDesc(IrradianceIndex);
		const auto* irradiancePreviewDesc = renderResRegistry->GetTextureDesc(IrradiancePreviewIndex);
		if (ImGui::CollapsingHeader("IBL Irradiance"))
		{
			renderResRegistry->RequestIBLPreview(RenderResourceRegistry::IBLPreviewType::Irradiance);
			DrawBakeState(diagnosticsSnapshot ? diagnosticsSnapshot->m_Irradiance.m_BakeState : IBLBakeState::Unavailable);

			if (!irradianceDesc || !irradiancePreviewDesc)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor, "Irradiance preview texture is not allocated.");
				return;
			}

			using PreviewLayout = RenderResourceRegistry::IBLPreviewLayout;
			PreviewLayout previewLayout = renderResRegistry->GetIBLIrradiancePreviewLayout();
			if (DrawPreviewLayoutCombo("Display Mode##IrradiancePreviewLayout", previewLayout))
			{
				renderResRegistry->SetIBLIrradiancePreviewLayout(previewLayout);
			}

			if (state.m_ShowMetadata)
			{
				ImGui::Text("Cubemap Size: %llu x %u x %u",
					static_cast<unsigned long long>(irradianceDesc->m_Extent.m_Width),
					irradianceDesc->m_Extent.m_Height,
					irradianceDesc->m_ArraySize);
				ImGui::Text("Cubemap Format: %s", devtools::EnumText(ToDXGIFormat(irradianceDesc->m_Format)).data());
				ImGui::Text("Cubemap Shader Visible SRV Index: %u",
					renderResRegistry->GetShaderVisibleSrvIndex(IrradianceIndex));
			}

			ImGui::SliderFloat(
				"Irradiance Preview Width",
				&state.m_IrradiancePreviewWidth,
				192.0f,
				768.0f,
				"%.0f");
			const ImTextureID previewTextureId = devtools::ResolveImGuiTextureId(
				context.m_DevelopGuiSystem,
				renderResRegistry->GetSrvDescriptor(IrradiancePreviewIndex));
			if (!previewTextureId)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor, "Irradiance preview SRV is invalid.");
				return;
			}

			const float previewWidth = std::clamp(state.m_IrradiancePreviewWidth, 16.0f, 2048.0f);
			ImVec2 uv1(1.0f, 1.0f);
			float aspect = 3.0f / 4.0f;
			const char* layoutHint = "Cross Layout: +Y / -X +Z +X -Z / -Y";
			if (previewLayout == PreviewLayout::Grid2x3)
			{
				uv1 = ImVec2(3.0f / 4.0f, 2.0f / 3.0f);
				aspect = 2.0f / 3.0f;
				layoutHint = "2x3 Layout: +X -X +Y / -Y +Z -Z";
			}
			ImGui::TextUnformatted(layoutHint);
			if (!diagnosticsSnapshot || diagnosticsSnapshot->m_IrradiancePreview.m_UpdateCount > 0)
			{
				ImGui::Image(previewTextureId, ImVec2(previewWidth, previewWidth * aspect), ImVec2(0.0f, 0.0f), uv1);
			}
			else
			{
				ImGui::TextDisabled("Preview requested; it will be available next frame.");
			}
			if (diagnosticsSnapshot)
			{
				const auto& preview = diagnosticsSnapshot->m_IrradiancePreview;
				ImGui::TextDisabled("Preview updates: %llu%s",
					static_cast<unsigned long long>(preview.m_UpdateCount),
					preview.m_Dirty ? " (refresh pending)" : "");
			}
		}

		ImGui::Spacing();

		const auto* prefilteredSpecularDesc = renderResRegistry->GetTextureDesc(PrefilteredSpecularIndex);
		const auto* prefilteredSpecularPreviewDesc = renderResRegistry->GetTextureDesc(PrefilteredSpecularPreviewIndex);

		if (ImGui::CollapsingHeader("IBL Prefiltered Specular"))
		{
			renderResRegistry->RequestIBLPreview(RenderResourceRegistry::IBLPreviewType::PrefilteredSpecular);
			DrawBakeState(diagnosticsSnapshot ?
				diagnosticsSnapshot->m_PrefilteredSpecular.m_BakeState :
				(renderResRegistry->IsDirty(PrefilteredSpecularIndex) ? IBLBakeState::Dirty : IBLBakeState::Ready));

			if (!prefilteredSpecularDesc || !prefilteredSpecularPreviewDesc)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor, "Prefiltered specular preview texture is not allocated.");
				return;
			}

			const uint32_t mipLevels = prefilteredSpecularDesc->m_MipLevels;
			const uint32_t maxMip = mipLevels > 0 ? mipLevels - 1u : 0u;

			uint32_t selectedMip = std::min(renderResRegistry->GetIBLPrefilteredSpecularPreviewMip(), maxMip);
			if (selectedMip != renderResRegistry->GetIBLPrefilteredSpecularPreviewMip())
			{
				renderResRegistry->SetIBLPrefilteredSpecularPreviewMip(selectedMip);
			}

			int selectedMipInt = static_cast<int>(selectedMip);
			if (ImGui::SliderInt("Output Mip", &selectedMipInt, 0, static_cast<int>(maxMip)))
			{
				selectedMip = static_cast<uint32_t>(std::clamp(selectedMipInt, 0, static_cast<int>(maxMip)));
				renderResRegistry->SetIBLPrefilteredSpecularPreviewMip(selectedMip);
			}

			const float roughness = maxMip > 0 ? static_cast<float>(selectedMip) / static_cast<float>(maxMip) : 0.0f;
			const float alpha = roughness * roughness;
			ImGui::Text("Selected Output Mip: %u / %u", selectedMip, maxMip);
			ImGui::Text("Perceptual Roughness: %.3f", roughness);
			ImGui::Text("GGX Alpha: %.3f", alpha);

			using PreviewLayout = RenderResourceRegistry::IBLPreviewLayout;
			PreviewLayout previewLayout = renderResRegistry->GetIBLPrefilteredSpecularPreviewLayout();
			if (DrawPreviewLayoutCombo("Display Mode##PrefilteredSpecularPreviewLayout", previewLayout))
			{
				renderResRegistry->SetIBLPrefilteredSpecularPreviewLayout(previewLayout);
			}

			if (state.m_ShowMetadata)
			{
				const uint32_t prefilteredSpecularSrvIndex =
					renderResRegistry->GetShaderVisibleSrvIndex(PrefilteredSpecularIndex);
				const uint32_t previewSrvIndex =
					renderResRegistry->GetShaderVisibleSrvIndex(PrefilteredSpecularPreviewIndex);

				ImGui::Text("Cubemap Size: %llu x %u x %u",
					static_cast<unsigned long long>(prefilteredSpecularDesc->m_Extent.m_Width),
					prefilteredSpecularDesc->m_Extent.m_Height,
					prefilteredSpecularDesc->m_ArraySize);
				ImGui::Text("Cubemap MipLevels: %u", mipLevels);
				ImGui::Text("Cubemap Format: %s", devtools::EnumText(ToDXGIFormat(prefilteredSpecularDesc->m_Format)).data());
				ImGui::Text("Cubemap Shader Visible SRV Index: %u", prefilteredSpecularSrvIndex);

				ImGui::Text("Preview Canvas Size: %llu x %u",
					static_cast<unsigned long long>(prefilteredSpecularPreviewDesc->m_Extent.m_Width),
					prefilteredSpecularPreviewDesc->m_Extent.m_Height);
				ImGui::Text("Preview Format: %s", devtools::EnumText(ToDXGIFormat(prefilteredSpecularPreviewDesc->m_Format)).data());
				ImGui::Text("Preview Shader Visible SRV Index: %u", previewSrvIndex);
			}

			ImGui::SliderFloat(
				"Prefiltered Specular Preview Width",
				&state.m_PrefilteredSpecularPreviewWidth,
				192.0f,
				768.0f,
				"%.0f");

			const ImTextureID prefilteredSpecularPreviewTextureId =
				devtools::ResolveImGuiTextureId(
					context.m_DevelopGuiSystem,
					renderResRegistry->GetSrvDescriptor(PrefilteredSpecularPreviewIndex));

			if (!prefilteredSpecularPreviewTextureId)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor, "Prefiltered specular preview SRV GPU handle is invalid.");
				return;
			}

			const float prefilteredSpecularPreviewWidth =
				std::clamp(state.m_PrefilteredSpecularPreviewWidth, 16.0f, 2048.0f);

			ImVec2 uv0(0.0f, 0.0f);
			ImVec2 uv1(1.0f, 1.0f);
			float aspect = 3.0f / 4.0f;
			const char* layoutHint = "Cross Layout: +Y / -X +Z +X -Z / -Y";

			if (previewLayout == RenderResourceRegistry::IBLPreviewLayout::Grid2x3)
			{
				uv1 = ImVec2(3.0f / 4.0f, 2.0f / 3.0f);
				aspect = 2.0f / 3.0f;
				layoutHint = "2x3 Layout: +X -X +Y / -Y +Z -Z";
			}

			const ImVec2 prefilteredSpecularImageSize(
				prefilteredSpecularPreviewWidth,
				prefilteredSpecularPreviewWidth * aspect);

			ImGui::TextUnformatted(layoutHint);
			if (!diagnosticsSnapshot || diagnosticsSnapshot->m_PrefilteredSpecularPreview.m_UpdateCount > 0)
			{
				ImGui::Image(prefilteredSpecularPreviewTextureId, prefilteredSpecularImageSize, uv0, uv1);
			}
			else
			{
				ImGui::TextDisabled("Preview requested; it will be available next frame.");
			}
			if (diagnosticsSnapshot)
			{
				const auto& preview = diagnosticsSnapshot->m_PrefilteredSpecularPreview;
				ImGui::TextDisabled("Preview updates: %llu%s",
					static_cast<unsigned long long>(preview.m_UpdateCount),
					preview.m_Dirty ? " (refresh pending)" : "");
			}
		}
	}
}
