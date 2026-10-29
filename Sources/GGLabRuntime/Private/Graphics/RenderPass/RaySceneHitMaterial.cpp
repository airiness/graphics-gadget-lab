#include "GGLabRuntime/Graphics/Pipeline/RaySceneHitMaterial.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneConstantBindings.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPass/GIReceiverGraphResources.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <array>
#include <cstddef>
#include <vector>

namespace gglab
{
	namespace
	{
		const RenderPassInfo PassInfo{ .m_TypeName = "RayScene.HitMaterial", .m_DisplayName = "GI hit material comparison",
			.m_CategoryName = "Ray Scene", .m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute };
		constexpr std::array OutputNames{ "RayScene.HitAlbedo", "RayScene.HitNormal", "RayScene.HitIdentity" };
		struct Parameters
		{
			uint32_t m_Depth = 0;
			std::array<uint32_t, 3> m_Outputs{};
			uint32_t m_View = 0;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_InstanceCount = 0;
			uint32_t m_GeometryCount = 0;
			uint32_t m_MaterialCount = 0;
			uint32_t m_Receiver = 0;
			uint32_t m_Padding = 0;
		};
		static_assert(sizeof(Parameters) == 48 && offsetof(Parameters, m_InstanceCount) == 28 &&
			offsetof(Parameters, m_MaterialCount) == 36);
	}

	struct RaySceneHitMaterial::State
	{
		ResolvedRenderLayout m_Layout;
		ResolvedBindingRef<SceneGPU> m_Scene;
		ComputePipelineRecipe m_Recipe;
		ComputePipelineSlot m_Slot;
		ResolvedRenderPipeline m_Pipeline;
		std::array<uint32_t, 6> m_Parameters{};
	};

	RaySceneHitMaterial::RaySceneHitMaterial() noexcept : m_State(std::make_unique<State>()) {}
	RaySceneHitMaterial::~RaySceneHitMaterial() = default;

	bool RaySceneHitMaterial::Prepare(const RenderServices& services) noexcept
	{
		auto& state = *m_State;
		if (!state.m_Layout.IsValid())
		{
			const RHITextureDesc desc{ .m_Format = RHIFormat::R32G32B32A32Float,
				.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess | RHITextureUsage::CopySource };
			if (!services.m_Presentation->GetDevice()->QueryTextureSupport(desc).IsSupported())
			{
				return false;
			}
			const auto* common = services.m_BindingLayout->GetCommonLayoutContract();
			if (!common)
			{
				return false;
			}
			std::vector<LayoutBindingRequirement> bindings(common->GetBindings().begin(), common->GetBindings().end());
			for (uint32_t index = 0; index < state.m_Parameters.size(); ++index)
			{
				bindings.push_back({ .m_Key = LayoutBindingKey{ 0x600u + index },
					.m_Slot = { .m_Type = index == 0 ? RHIBindingType::AccelerationStructure : RHIBindingType::ReadOnlyStorageBuffer,
						.m_Visibility = RHIShaderStage::Compute, .m_Binding = 5 + index, .m_Count = 1 },
					.m_ParameterOrder = 10 + index });
			}
			const std::array fragments{ LayoutFragment{ "HitMaterial", bindings } };
			const auto composed = ComposeLayoutFragments(fragments);
			if (!composed.IsSuccess())
			{
				return false;
			}
			state.m_Layout = ResolvedRenderLayout::Create(services.m_Presentation->GetRHIContext()->GetPipelineSystem(),
				*composed.m_Layout, "RayScene.HitMaterial");
			for (uint32_t index = 0; index < state.m_Parameters.size(); ++index)
			{
				const auto parameter = composed.m_Layout->FindParameterIndex(LayoutBindingKey{ 0x600u + index });
				if (!parameter)
				{
					return false;
				}
				state.m_Parameters[index] = *parameter;
			}
			state.m_Scene = ResolvedBindingRef<SceneGPU>::Resolve(state.m_Layout, scene_bindings::SceneConstants);
			state.m_Recipe.m_BindingLayout = state.m_Layout.GetNativeLayout();
			state.m_Recipe.m_CSId = services.m_ShaderPrograms->LoadProgram(shader_programs::RaySceneHitMaterialCompute);
		}
		if (!state.m_Layout.IsValid() || !state.m_Scene.IsValid())
		{
			return false;
		}
		state.m_Pipeline = state.m_Layout.Resolve(*services.m_PipelineResolver, state.m_Slot, state.m_Recipe, PassInfo);
		return state.m_Pipeline.IsValid();
	}

	void RaySceneHitMaterial::AddPass(RenderGraph& graph, const RenderFrameContext& frame,
		const RenderServices& services, const RaySceneGraphResources& scene) noexcept
	{
		GGLAB_UNUSED(services);
		struct PassData
		{
			RGTextureViewId m_Depth;
			RGTextureViewId m_Receiver;
			std::array<RGTextureViewId, OutputNames.size()> m_Outputs{};
			Parameters m_Parameters;
		};
		graph.AddPass<PassData>(PassInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
			[&frame, scene](RenderGraph::RGBuilder& builder, PassData& data)
			{
				const auto& receivers = builder.GetBlackboard().Get<RGGIReceiverResources>(GIReceiverResourcesName);
				const auto receiverRead = builder.Read(receivers.m_Textures[0], RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_Receiver = builder.CreateView<RHITextureViewType::ShaderResource>(receiverRead);
				const auto& depth = builder.GetBlackboard().Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const auto extent = builder.GetTextureDesc(depth.m_Texture).m_Extent;
				const auto read = builder.Read(depth.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_Depth = builder.CreateView<RHITextureViewType::ShaderResource>(read, depth.m_SrvDesc);
				for (size_t index = 0; index < data.m_Outputs.size(); ++index)
				{
					auto output = builder.CreateTexture(OutputNames[index], { .m_Format = RHIFormat::R32G32B32A32Float, .m_Extent = extent });
					builder.WriteInPlace(output, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_Outputs[index] = builder.CreateView<RHITextureViewType::UnorderedAccess>(output);
				}
				GGLAB_UNUSED(builder.Read(scene.m_TopLevel, RGAccelerationStructureAccess::QueryRead));
				for (const auto blas : scene.m_BottomLevels)
				{
					GGLAB_UNUSED(builder.Read(blas, RGAccelerationStructureAccess::QueryRead));
				}
				for (const auto buffer : { scene.m_Vertices, scene.m_Indices, scene.m_Geometries, scene.m_Instances, scene.m_Materials })
				{
					GGLAB_UNUSED(builder.Read(buffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader));
				}
				data.m_Parameters = { .m_View = frame.GetRenderQueue(frame.GetDisplayViewId()).m_CoverageRasterDomain.m_ViewBindingId,
					.m_Width = extent.m_Width, .m_Height = extent.m_Height, .m_InstanceCount = scene.m_InstanceCount,
					.m_GeometryCount = scene.m_GeometryCount, .m_MaterialCount = scene.m_MaterialCount };
				builder.SideEffect();
			},
			[this, &frame, scene](RGExecuteContext& execute, PassData& data)
			{
				auto* commands = execute.GetDirectComputeCommandContext();
				ScopedConstantBindings<RHIComputeCommandContext> constants(*commands, m_State->m_Layout, m_State->m_Scene);
				if (!commands->RetainForSubmission(scene.m_Lifetime) || !constants.SetPipeline(m_State->m_Pipeline) ||
					!constants.Bind(m_State->m_Scene, frame.m_FrameConstants.m_SceneSnapshot) ||
					!commands->SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB), frame.m_FrameConstants.m_Views) ||
					!commands->SetAccelerationStructure(m_State->m_Parameters[0], execute.GetAccelerationStructureHandle(scene.m_TopLevel)))
				{
					return;
				}
				const std::array buffers{ scene.m_Vertices, scene.m_Indices, scene.m_Geometries, scene.m_Instances, scene.m_Materials };
				for (uint32_t index = 0; index < buffers.size(); ++index)
				{
					commands->SetReadOnlyBuffer(m_State->m_Parameters[index + 1], execute.GetBufferHandle(buffers[index]));
				}
				auto parameters = data.m_Parameters;
				parameters.m_Depth = execute.GetViewDescriptor(data.m_Depth).m_Index;
				parameters.m_Receiver = execute.GetViewDescriptor(data.m_Receiver).m_Index;
				for (size_t index = 0; index < data.m_Outputs.size(); ++index)
				{
					parameters.m_Outputs[index] = execute.GetViewDescriptor(data.m_Outputs[index]).m_Index;
				}
				if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
				{
					commands->Dispatch((parameters.m_Width + 7) / 8, (parameters.m_Height + 7) / 8, 1);
				}
			});
	}
}
