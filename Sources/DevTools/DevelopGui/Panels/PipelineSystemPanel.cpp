#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/PipelineSystemPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Graphics/Renderer.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RHI/RHIPipelineSystemSnapshot.h"
#include "Graphics/RHI/RHIFormat.h"
#include "Graphics/RHI/DX12/DX12PipelineSystem.h"

namespace gglab
{
	namespace
	{
		struct PipelineSystemPanelState
		{
			uint32_t m_SelectedLayout = RHIBindingLayoutHandle::InvalidIndex;
			uint32_t m_SelectedPipeline = RHIPipelineHandle::InvalidIndex;
		};

		const char* BindingTypeText(RHIBindingType type) noexcept
		{
			switch (type)
			{
			case RHIBindingType::ConstantBuffer: return "ConstantBuffer";
			case RHIBindingType::ReadOnlyStorageBuffer: return "ReadOnlyStorageBuffer";
			case RHIBindingType::ReadWriteStorageBuffer: return "ReadWriteStorageBuffer";
			case RHIBindingType::SampledTexture: return "SampledTexture";
			case RHIBindingType::StorageTexture: return "StorageTexture";
			case RHIBindingType::Sampler: return "Sampler";
			case RHIBindingType::PushConstants: return "PushConstants";
			case RHIBindingType::BindlessSampledTextureTable: return "BindlessTextureTable";
			case RHIBindingType::BindlessSamplerTable: return "BindlessSamplerTable";
			case RHIBindingType::Unknown: default: return "Unknown";
			}
		}

		const char* ShaderVisibilityText(RHIShaderStage visibility) noexcept
		{
			if (visibility == RHIShaderStage::All) return "All";
			if (visibility == RHIShaderStage::AllGraphics) return "AllGraphics";
			if (visibility == RHIShaderStage::Vertex) return "Vertex";
			if (visibility == RHIShaderStage::Pixel) return "Pixel";
			if (visibility == RHIShaderStage::Compute) return "Compute";
			return "None";
		}

		const char* FormatText(RHIFormat format) noexcept
		{
			return GetRHIFormatInfo(format).m_Name;
		}

		const char* TopologyText(RHIPrimitiveTopology topology) noexcept
		{
			switch (topology)
			{
			case RHIPrimitiveTopology::PointList: return "PointList";
			case RHIPrimitiveTopology::LineList: return "LineList";
			case RHIPrimitiveTopology::LineStrip: return "LineStrip";
			case RHIPrimitiveTopology::TriangleList: return "TriangleList";
			case RHIPrimitiveTopology::TriangleStrip: return "TriangleStrip";
			case RHIPrimitiveTopology::Unknown: default: return "Unknown";
			}
		}

		const char* FillModeText(RHIFillMode mode) noexcept
		{
			return mode == RHIFillMode::Wireframe ? "Wireframe" : "Solid";
		}

		const char* CullModeText(RHICullMode mode) noexcept
		{
			switch (mode)
			{
			case RHICullMode::None: return "None";
			case RHICullMode::Front: return "Front";
			case RHICullMode::Back: default: return "Back";
			}
		}

		const char* CompareOpText(RHICompareOp op) noexcept
		{
			switch (op)
			{
			case RHICompareOp::Never: return "Never";
			case RHICompareOp::Less: return "Less";
			case RHICompareOp::Equal: return "Equal";
			case RHICompareOp::LessEqual: return "LessEqual";
			case RHICompareOp::Greater: return "Greater";
			case RHICompareOp::NotEqual: return "NotEqual";
			case RHICompareOp::GreaterEqual: return "GreaterEqual";
			case RHICompareOp::Always: default: return "Always";
			}
		}

		const char* RenderPassCategoryText(RenderPassCategory category) noexcept
		{
			switch (category)
			{
			case RenderPassCategory::Geometry: return "Geometry";
			case RenderPassCategory::Lighting: return "Lighting";
			case RenderPassCategory::Shadow: return "Shadow";
			case RenderPassCategory::IBL: return "IBL";
			case RenderPassCategory::PostProcess: return "PostProcess";
			case RenderPassCategory::Debug: return "Debug";
			case RenderPassCategory::UI: return "UI";
			case RenderPassCategory::Unknown: default: return "Unknown";
			}
		}

		const char* RenderPassTypeText(RenderPassType type) noexcept
		{
			switch (type)
			{
			case RenderPassType::Graphics: return "Graphics";
			case RenderPassType::Compute: return "Compute";
			case RenderPassType::Transfer: return "Transfer";
			case RenderPassType::Mixed: return "Mixed";
			}
			return "Unknown";
		}

		std::string HandleText(auto handle)
		{
			return handle.IsValid() ?
				std::format("{}:{}", handle.Index(), handle.Generation()) : "-";
		}

		std::string HashText(ShaderHash128 hash)
		{
			return std::format("{:016X}{:016X}", hash.m_HighBits, hash.m_LowBits);
		}

		std::string RegisterText(const RHIBindingSlotSnapshot& slot)
		{
			const char prefix = [&]() noexcept
				{
					switch (slot.m_Type)
					{
					case RHIBindingType::ConstantBuffer:
					case RHIBindingType::PushConstants: return 'b';
					case RHIBindingType::ReadWriteStorageBuffer:
					case RHIBindingType::StorageTexture: return 'u';
					case RHIBindingType::Sampler:
					case RHIBindingType::BindlessSamplerTable: return 's';
					default: return 't';
					}
				}();
			return std::format("{}{} space{}", prefix, slot.m_Binding, slot.m_Space);
		}

		std::string BackendMappingText(const RHIBindingSlotSnapshot& slot)
		{
			if (slot.m_BackendMapping == RHIBindingBackendMapping::DirectlyIndexed)
			{
				return "No root parameter, DIRECTLY_INDEXED flag";
			}
			if (slot.m_BackendMapping == RHIBindingBackendMapping::RootParameter)
			{
				return std::format("RootParam {} {} {}", slot.m_RootParameter,
					slot.m_BackendBindingType, RegisterText(slot));
			}
			return "-";
		}

		std::string ShaderSummary(const RHIShaderSnapshot& shader)
		{
			if (!shader.m_Present) return "-";
			return shader.m_DebugName.empty() ? HandleText(shader.m_Handle) : shader.m_DebugName;
		}

		std::string RenderTargetFormatsText(const RHIPipelineSnapshot& pipeline)
		{
			if (pipeline.m_RenderTargetFormats.empty()) return "None";
			std::string result;
			for (size_t index = 0; index < pipeline.m_RenderTargetFormats.size(); ++index)
			{
				if (index) result += ", ";
				result += FormatText(pipeline.m_RenderTargetFormats[index]);
			}
			return result;
		}

		std::string PipelineUsagesText(const RHIPipelineSnapshot& pipeline)
		{
			if (pipeline.m_RenderPasses.empty()) return "Unregistered";
			std::string result;
			for (size_t index = 0; index < pipeline.m_RenderPasses.size(); ++index)
			{
				if (index) result += ", ";
				const auto& pass = pipeline.m_RenderPasses[index];
				result += pass.m_DisplayName.empty() ? pass.m_TypeName : pass.m_DisplayName;
			}
			return result;
		}

		void DrawBindingLayoutDetail(const RHIBindingLayoutSnapshot& layout) noexcept
		{
			ImGui::SeparatorText("Binding Layout Detail");
			ImGui::Text("Handle %s | %s | Root parameters %u | RootSignature ID %u | Native 0x%llX",
				HandleText(layout.m_Handle).c_str(), layout.m_Alive ? "Alive" : "Dead",
				layout.m_RootParameterCount, layout.m_BackendRootSignatureId,
				static_cast<unsigned long long>(layout.m_BackendRootSignaturePointer));
			ImGui::Text("Name: %s | Direct indexing: resources=%s samplers=%s",
				layout.m_DebugName.empty() ? "-" : layout.m_DebugName.c_str(),
				layout.m_DirectlyIndexedResources ? "yes" : "no",
				layout.m_DirectlyIndexedSamplers ? "yes" : "no");

			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX;
			if (ImGui::BeginTable("BindingLayoutSlots", 8, flags))
			{
				ImGui::TableSetupColumn("RHI Slot");
				ImGui::TableSetupColumn("Name");
				ImGui::TableSetupColumn("Type");
				ImGui::TableSetupColumn("Visibility");
				ImGui::TableSetupColumn("Register");
				ImGui::TableSetupColumn("Count");
				ImGui::TableSetupColumn("Size");
				ImGui::TableSetupColumn("DX12 Mapping");
				ImGui::TableHeadersRow();
				for (const auto& slot : layout.m_Slots)
				{
					const std::string registerText = RegisterText(slot);
					const std::string mappingText = BackendMappingText(slot);
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::Text("%u", slot.m_Slot);
					ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(slot.m_DebugName.empty() ? "-" : slot.m_DebugName.c_str());
					ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(BindingTypeText(slot.m_Type));
					ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(ShaderVisibilityText(slot.m_Visibility));
					ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(registerText.c_str());
					ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(slot.m_Count == 0 ? "Unbounded" : std::to_string(slot.m_Count).c_str());
					ImGui::TableSetColumnIndex(6); ImGui::Text("%u B", slot.m_SizeInBytes);
					ImGui::TableSetColumnIndex(7); ImGui::TextUnformatted(mappingText.c_str());
				}
				ImGui::EndTable();
			}
		}

		void DrawBindingLayouts(
			const RHIPipelineSystemSnapshot& snapshot,
			PipelineSystemPanelState& state) noexcept
		{
			if (state.m_SelectedLayout == RHIBindingLayoutHandle::InvalidIndex &&
				!snapshot.m_BindingLayouts.empty())
			{
				state.m_SelectedLayout = snapshot.m_BindingLayouts.front().m_Handle.Index();
			}
			if (ImGui::BeginTable("BindingLayouts", 6,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Handle");
				ImGui::TableSetupColumn("Alive");
				ImGui::TableSetupColumn("Debug Name");
				ImGui::TableSetupColumn("Slots");
				ImGui::TableSetupColumn("Root Params");
				ImGui::TableSetupColumn("RootSignature");
				ImGui::TableHeadersRow();
				for (const auto& layout : snapshot.m_BindingLayouts)
				{
					const std::string handle = HandleText(layout.m_Handle);
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					if (ImGui::Selectable(handle.c_str(), state.m_SelectedLayout == layout.m_Handle.Index(),
						ImGuiSelectableFlags_SpanAllColumns))
					{
						state.m_SelectedLayout = layout.m_Handle.Index();
					}
					ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(layout.m_Alive ? "Yes" : "No");
					ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(layout.m_DebugName.empty() ? "-" : layout.m_DebugName.c_str());
					ImGui::TableSetColumnIndex(3); ImGui::Text("%u", static_cast<uint32_t>(layout.m_Slots.size()));
					ImGui::TableSetColumnIndex(4); ImGui::Text("%u", layout.m_RootParameterCount);
					ImGui::TableSetColumnIndex(5); ImGui::Text("ID %u / 0x%llX", layout.m_BackendRootSignatureId,
						static_cast<unsigned long long>(layout.m_BackendRootSignaturePointer));
				}
				ImGui::EndTable();
			}

			const auto selected = std::ranges::find_if(snapshot.m_BindingLayouts,
				[&state](const auto& layout) { return layout.m_Handle.Index() == state.m_SelectedLayout; });
			if (selected != snapshot.m_BindingLayouts.end()) DrawBindingLayoutDetail(*selected);
		}

		void DrawShaderTable(const RHIPipelineSnapshot& pipeline) noexcept
		{
			struct StageEntry { const char* m_Stage; const RHIShaderSnapshot* m_Shader; };
			const StageEntry graphicsStages[] = {
				{ "VS", &pipeline.m_VertexShader }, { "PS", &pipeline.m_PixelShader },
				{ "DS", &pipeline.m_DomainShader }, { "HS", &pipeline.m_HullShader },
				{ "GS", &pipeline.m_GeometryShader },
			};
			const StageEntry computeStages[] = { { "CS", &pipeline.m_ComputeShader } };
			const std::span<const StageEntry> stages =
				pipeline.m_Type == RHIPipelineSnapshotType::Graphics ?
				std::span<const StageEntry>(graphicsStages) :
				std::span<const StageEntry>(computeStages);
			if (ImGui::BeginTable("PipelineShaders", 4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Stage");
				ImGui::TableSetupColumn("Handle");
				ImGui::TableSetupColumn("Name");
				ImGui::TableSetupColumn("Hash");
				ImGui::TableHeadersRow();
				for (const auto& stage : stages)
				{
					if (!stage.m_Shader->m_Present) continue;
					const std::string handle = HandleText(stage.m_Shader->m_Handle);
					const std::string hash = HashText(stage.m_Shader->m_Hash);
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(stage.m_Stage);
					ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(handle.c_str());
					ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(stage.m_Shader->m_DebugName.empty() ? "-" : stage.m_Shader->m_DebugName.c_str());
					ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(hash.c_str());
				}
				ImGui::EndTable();
			}
		}

		void DrawGraphicsState(const RHIPipelineSnapshot& pipeline) noexcept
		{
			const std::string rtFormats = RenderTargetFormatsText(pipeline);
			ImGui::Text("Topology: %s | RTV: %s | DSV: %s | Samples: %u quality %u",
				TopologyText(pipeline.m_PrimitiveTopology), rtFormats.c_str(),
				FormatText(pipeline.m_DepthStencilFormat), pipeline.m_SampleCount,
				pipeline.m_SampleQuality);
			ImGui::Text("Rasterizer: fill=%s cull=%s frontCCW=%s depthBias=%d slopeBias=%.3f depthClip=%s",
				FillModeText(pipeline.m_Rasterizer.m_FillMode), CullModeText(pipeline.m_Rasterizer.m_CullMode),
				pipeline.m_Rasterizer.m_FrontCounterClockwise ? "yes" : "no",
				pipeline.m_Rasterizer.m_DepthBias, pipeline.m_Rasterizer.m_SlopeScaledDepthBias,
				pipeline.m_Rasterizer.m_DepthClipEnable ? "yes" : "no");
			ImGui::Text("Depth: test=%s write=%s compare=%s | Stencil=%s | AlphaToCoverage=%s",
				pipeline.m_DepthStencil.m_DepthTestEnable ? "on" : "off",
				pipeline.m_DepthStencil.m_DepthWriteEnable ? "on" : "off",
				CompareOpText(pipeline.m_DepthStencil.m_DepthCompareOp),
				pipeline.m_DepthStencil.m_StencilEnable ? "on" : "off",
				pipeline.m_Blend.m_AlphaToCoverageEnable ? "on" : "off");

			if (ImGui::TreeNode("Input Layout"))
			{
				if (pipeline.m_VertexAttributes.empty())
				{
					ImGui::TextDisabled("No vertex attributes (procedural geometry / SV_VertexID).");
				}
				if (ImGui::BeginTable("PipelineInputLayout", 5,
					ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
				{
					ImGui::TableSetupColumn("Semantic"); ImGui::TableSetupColumn("Format");
					ImGui::TableSetupColumn("Input Slot"); ImGui::TableSetupColumn("Offset");
					ImGui::TableSetupColumn("Buffer Stride"); ImGui::TableHeadersRow();
					for (const auto& attribute : pipeline.m_VertexAttributes)
					{
						const auto buffer = std::ranges::find_if(pipeline.m_VertexBuffers,
							[&attribute](const auto& value) { return value.m_InputSlot == attribute.m_InputSlot; });
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::Text("%s%u", attribute.m_SemanticName.c_str(), attribute.m_SemanticIndex);
						ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(FormatText(attribute.m_Format));
						ImGui::TableSetColumnIndex(2); ImGui::Text("%u", attribute.m_InputSlot);
						ImGui::TableSetColumnIndex(3); ImGui::Text("%u", attribute.m_AlignedByteOffset);
						ImGui::TableSetColumnIndex(4); ImGui::Text("%u", buffer != pipeline.m_VertexBuffers.end() ? buffer->m_StrideInBytes : 0);
					}
					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
			if (ImGui::TreeNode("Blend State"))
			{
				for (uint32_t index = 0; index < pipeline.m_RenderTargetFormats.size(); ++index)
				{
					const auto& blend = pipeline.m_Blend.m_RenderTargets[index];
					ImGui::Text("RT%u: blend=%s writeMask=0x%X", index,
						blend.m_BlendEnable ? "on" : "off", static_cast<uint32_t>(blend.m_WriteMask));
				}
				ImGui::TreePop();
			}
		}

		void DrawRenderPassUsages(const RHIPipelineSnapshot& pipeline) noexcept
		{
			ImGui::SeparatorText("RenderPass Usages");
			if (pipeline.m_RenderPasses.empty())
			{
				ImGui::TextDisabled("No RenderPass usage was registered for this pipeline.");
				return;
			}
			if (ImGui::BeginTable("PipelineRenderPassUsages", 7,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Stable Type");
				ImGui::TableSetupColumn("Display Name");
				ImGui::TableSetupColumn("Category");
				ImGui::TableSetupColumn("Type");
				ImGui::TableSetupColumn("DevelopGUI");
				ImGui::TableSetupColumn("GPU Marker");
				ImGui::TableSetupColumn("Profiling");
				ImGui::TableHeadersRow();
				for (const auto& pass : pipeline.m_RenderPasses)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(pass.m_TypeName.c_str());
					if (!pass.m_Description.empty() && ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", pass.m_Description.c_str());
					}
					ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(pass.m_DisplayName.c_str());
					ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(
						pass.m_CategoryName.empty() ? RenderPassCategoryText(pass.m_Category) : pass.m_CategoryName.c_str());
					ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(RenderPassTypeText(pass.m_Type));
					ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(pass.m_ShowInDevelopGui ? "Yes" : "No");
					ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(pass.m_EnableGpuMarker ? "On" : "Off");
					ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(pass.m_EnableProfiling ? "On" : "Off");
				}
				ImGui::EndTable();
			}
		}

		void DrawPipelineDetail(const RHIPipelineSnapshot& pipeline) noexcept
		{
			ImGui::SeparatorText("Pipeline Detail");
			ImGui::Text("Handle %s | %s | %s",
				HandleText(pipeline.m_Handle).c_str(),
				pipeline.m_Type == RHIPipelineSnapshotType::Graphics ? "Graphics" : "Compute",
				pipeline.m_Alive ? "Alive" : "Dead");
			ImGui::Text("BindingLayout %s | Backend PSO 0x%llX | RootSignature ID %u / 0x%llX",
				HandleText(pipeline.m_BindingLayout).c_str(),
				static_cast<unsigned long long>(pipeline.m_BackendPipelinePointer),
				pipeline.m_BackendRootSignatureId,
				static_cast<unsigned long long>(pipeline.m_BackendRootSignaturePointer));
			DrawRenderPassUsages(pipeline);
			DrawShaderTable(pipeline);
			if (pipeline.m_Type == RHIPipelineSnapshotType::Graphics) DrawGraphicsState(pipeline);
		}

		void DrawPipelines(
			const RHIPipelineSystemSnapshot& snapshot,
			PipelineSystemPanelState& state) noexcept
		{
			if (state.m_SelectedPipeline == RHIPipelineHandle::InvalidIndex &&
				!snapshot.m_Pipelines.empty())
			{
				state.m_SelectedPipeline = snapshot.m_Pipelines.front().m_Handle.Index();
			}
			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
			if (ImGui::BeginTable("Pipelines", 9, flags, ImVec2(0.0f, 280.0f)))
			{
				ImGui::TableSetupColumn("Handle"); ImGui::TableSetupColumn("Type");
				ImGui::TableSetupColumn("Alive"); ImGui::TableSetupColumn("Used By RenderPass");
				ImGui::TableSetupColumn("Binding Layout"); ImGui::TableSetupColumn("VS/CS");
				ImGui::TableSetupColumn("PS"); ImGui::TableSetupColumn("Formats");
				ImGui::TableSetupColumn("Backend PSO"); ImGui::TableHeadersRow();
				for (const auto& pipeline : snapshot.m_Pipelines)
				{
					const std::string handle = HandleText(pipeline.m_Handle);
					const std::string layout = HandleText(pipeline.m_BindingLayout);
					const std::string primaryShader = ShaderSummary(
						pipeline.m_Type == RHIPipelineSnapshotType::Graphics ? pipeline.m_VertexShader : pipeline.m_ComputeShader);
					const std::string pixelShader = ShaderSummary(pipeline.m_PixelShader);
					const std::string usages = PipelineUsagesText(pipeline);
					const std::string formats = pipeline.m_Type == RHIPipelineSnapshotType::Graphics ?
						RenderTargetFormatsText(pipeline) : "-";
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					if (ImGui::Selectable(handle.c_str(), state.m_SelectedPipeline == pipeline.m_Handle.Index(),
						ImGuiSelectableFlags_SpanAllColumns)) state.m_SelectedPipeline = pipeline.m_Handle.Index();
					ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(pipeline.m_Type == RHIPipelineSnapshotType::Graphics ? "Graphics" : "Compute");
					ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(pipeline.m_Alive ? "Yes" : "No");
					ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(usages.c_str());
					ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(layout.c_str());
					ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(primaryShader.c_str());
					ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(pixelShader.c_str());
					ImGui::TableSetColumnIndex(7); ImGui::TextUnformatted(formats.c_str());
					ImGui::TableSetColumnIndex(8); ImGui::Text("0x%llX", static_cast<unsigned long long>(pipeline.m_BackendPipelinePointer));
				}
				ImGui::EndTable();
			}
			const auto selected = std::ranges::find_if(snapshot.m_Pipelines,
				[&state](const auto& pipeline) { return pipeline.m_Handle.Index() == state.m_SelectedPipeline; });
			if (selected != snapshot.m_Pipelines.end()) DrawPipelineDetail(*selected);
		}

		void DrawCache(const RHIPipelineSystemSnapshot& snapshot) noexcept
		{
			const auto& cache = snapshot.m_Cache;
			ImGui::Text("PipelineSystem revision: %llu | Backend cache revision: %llu",
				static_cast<unsigned long long>(cache.m_PipelineSystemRevision),
				static_cast<unsigned long long>(cache.m_BackendCacheRevision));
			ImGui::SeparatorText("RHI Registry");
			ImGui::Text("Registered handles: %u graphics / %u compute",
				cache.m_RegisteredGraphicsPipelines, cache.m_RegisteredComputePipelines);
			ImGui::SeparatorText("DX12 Backend Cache");
			ImGui::Text("PSOs: %u graphics / %u compute",
				cache.m_BackendRHIGraphicsPSOs, cache.m_BackendComputePSOs);
			ImGui::Text("Root signatures: %u", cache.m_BackendRootSignatures);
			ImGui::Spacing();
			ImGui::TextWrapped("PipelineCache is currently a recipe resolver with caller-owned slots. "
				"The centralized native object cache shown here is DX12PSOCache.");
		}
	}

	void PipelineSystemPanel::Draw(DevelopGuiContext& context) noexcept
	{
		if (!context.m_Renderer || !context.m_Renderer->GetRHIContext())
		{
			ImGui::TextDisabled("RHI context is not available.");
			return;
		}

		auto& pipelineSystem = context.m_Renderer->GetRHIContext()->GetPipelineSystem();
		auto* dx12PipelineSystem = dynamic_cast<DX12PipelineSystem*>(&pipelineSystem);
		if (!dx12PipelineSystem)
		{
			ImGui::TextDisabled("Pipeline diagnostics are not implemented for this backend.");
			return;
		}

		const PipelineCache* pipelineCache = context.m_Renderer->GetPipelineCache();
		RHIPipelineSystemSnapshot snapshot;
		BuildDX12PipelineSystemSnapshot(
			*dx12PipelineSystem,
			pipelineCache,
			snapshot);
		auto& state = context.PanelState<PipelineSystemPanelState>();

		ImGui::Text("Backend: %s | Binding layouts: %u | Pipelines: %u",
			snapshot.m_BackendName.c_str(),
			static_cast<uint32_t>(snapshot.m_BindingLayouts.size()),
			static_cast<uint32_t>(snapshot.m_Pipelines.size()));
		if (ImGui::BeginTabBar("PipelineSystemTabs"))
		{
			if (ImGui::BeginTabItem("Binding Layouts"))
			{
				DrawBindingLayouts(snapshot, state);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Pipelines"))
			{
				DrawPipelines(snapshot, state);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Cache / DX12"))
			{
				DrawCache(snapshot);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
}
