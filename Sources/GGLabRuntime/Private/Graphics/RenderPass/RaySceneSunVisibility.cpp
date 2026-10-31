#include "GGLabRuntime/Graphics/Pipeline/RaySceneSunVisibility.h"
#include "Graphics/Pipeline/RaySceneShaderBindings.h"
#include "Graphics/Pipeline/GIComputeDispatch.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneConstantBindings.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/ShadowGraphResources.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <array>
#include <cstddef>
#include <limits>
#include <vector>

namespace gglab
{
	namespace
	{
		struct Parameters
		{
			uint32_t m_Depth = 0;
			uint32_t m_Output = 0;
			uint32_t m_ShadowMap = 0;
			uint32_t m_ShadowSampler = 0;
			uint32_t m_View = 0;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_ShadowSize = 0;
			uint32_t m_ShadowFlags = 0;
			std::array<float, 3> m_SunDirection{};
			uint32_t m_InstanceCount = 0;
			uint32_t m_GeometryCount = 0;
			uint32_t m_Padding0 = 0;
			uint32_t m_Padding1 = 0;
		};
		static_assert(sizeof(Parameters) == 64 && offsetof(Parameters, m_ShadowFlags) == 32 &&
			offsetof(Parameters, m_InstanceCount) == 48);
		const RenderPassInfo PassInfo{ .m_TypeName = "RayScene.SunVisibility",
			.m_DisplayName = "RT sun visibility and CSM comparison", .m_CategoryName = "Ray Scene",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute };
	}

	struct RaySceneSunVisibility::State
	{
		ResolvedRenderLayout m_Layout;
		ResolvedBindingRef<SceneGPU> m_Scene;
		ResolvedBindingRef<DirectionalShadowGPU> m_Shadow;
		ComputePipelineRecipe m_Recipe;
		ComputePipelineSlot m_Slot;
		ResolvedRenderPipeline m_Pipeline;
		std::array<uint32_t, ray_scene_bindings::GeometryOnlyCount> m_RayParameters{};
	};

	RaySceneSunVisibility::RaySceneSunVisibility() noexcept : m_State(std::make_unique<State>()) {}
	RaySceneSunVisibility::~RaySceneSunVisibility() = default;

	bool RaySceneSunVisibility::Prepare(const RenderFrameContext& frame, const RenderServices& services) noexcept
	{
		if (!frame.m_RenderScene.m_WorldSun || !frame.m_DirectionalShadowFramePlan ||
			frame.m_RenderScene.m_WorldSunLightIndex == std::numeric_limits<uint32_t>::max() ||
			(frame.GetDirectionalShadowFramePlan().m_ShadingEnabled &&
				frame.m_RenderScene.m_WorldSunLightIndex != frame.m_RenderScene.m_DirectionalShadowLightIndex))
		{
			return false;
		}
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
			bindings.push_back({ .m_Key = scene_bindings::DirectionalShadow,
				.m_Slot = { .m_Type = RHIBindingType::ConstantBuffer, .m_Visibility = RHIShaderStage::Compute,
					.m_Binding = 3, .m_Count = 1 },
				.m_PayloadTag = ConstantPayloadTraits<DirectionalShadowGPU>::Tag,
				.m_ParameterOrder = ray_scene_bindings::NextParameterOrder(bindings) });
			ray_scene_bindings::Append(bindings, ray_scene_bindings::GeometryOnlyCount);
			const std::array fragments{ LayoutFragment{ "SunVisibility", bindings } };
			const auto composed = ComposeLayoutFragments(fragments);
			if (!composed.IsSuccess())
			{
				return false;
			}
			state.m_Layout = ResolvedRenderLayout::Create(services.m_Presentation->GetRHIContext()->GetPipelineSystem(),
				*composed.m_Layout, "RayScene.SunVisibility");
			for (uint32_t index = 0; index < state.m_RayParameters.size(); ++index)
			{
				const auto parameter = composed.m_Layout->FindParameterIndex(ray_scene_bindings::Key(index));
				if (!parameter)
				{
					return false;
				}
				state.m_RayParameters[index] = *parameter;
			}
			state.m_Scene = ResolvedBindingRef<SceneGPU>::Resolve(state.m_Layout, scene_bindings::SceneConstants);
			state.m_Shadow = ResolvedBindingRef<DirectionalShadowGPU>::Resolve(state.m_Layout, scene_bindings::DirectionalShadow);
			state.m_Recipe.m_BindingLayout = state.m_Layout.GetNativeLayout();
			state.m_Recipe.m_CSId = services.m_ShaderPrograms->LoadProgram(shader_programs::RaySceneSunVisibilityCompute);
		}
		if (!state.m_Layout.IsValid() || !state.m_Scene.IsValid() || !state.m_Shadow.IsValid())
		{
			return false;
		}
		state.m_Pipeline = state.m_Layout.Resolve(*services.m_PipelineResolver, state.m_Slot, state.m_Recipe, PassInfo);
		return state.m_Pipeline.IsValid();
	}

	void RaySceneSunVisibility::AddPass(RenderGraph& graph, const RenderFrameContext& frame,
		const RenderServices& services, const RaySceneGraphResources& scene) noexcept
	{
		struct PassData
		{
			RGTextureViewId m_Depth;
			RGTextureViewId m_Shadow;
			RGTextureViewId m_Output;
			Parameters m_Parameters;
		};
		graph.AddPass<PassData>(PassInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
			[&frame, services, scene](RenderGraph::RGBuilder& builder, PassData& data)
			{
				const auto& depth = builder.GetBlackboard().Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const auto& shadow = builder.GetBlackboard().Get<RGShadowResources>(ShadowResourcesName);
				const auto extent = builder.GetTextureDesc(depth.m_Texture).m_Extent;
				RHITextureDesc desc{ .m_Format = RHIFormat::R32G32B32A32Float,
					.m_Extent = extent };
				auto output = builder.CreateTexture("RayScene.SunVisibility", desc);
				builder.WriteInPlace(output, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_Output = builder.CreateView<RHITextureViewType::UnorderedAccess>(output);
				const auto depthRead = builder.Read(depth.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_Depth = builder.CreateView<RHITextureViewType::ShaderResource>(depthRead, depth.m_SrvDesc);
				if (frame.GetDirectionalShadowFramePlan().m_ShadingEnabled)
				{
					const auto shadowRead = builder.Read(shadow.m_DirectionalShadowMap, RGTextureAccess::Sample, RHIStage::ComputeShader);
					data.m_Shadow = builder.CreateView<RHITextureViewType::ShaderResource>(shadowRead,
						MakeRHITexture2DArrayViewDesc(RHIFormat::R32Float, 0, 0, shadow.m_CascadeCount, RHITextureAspect::Depth));
				}
				GGLAB_UNUSED(builder.Read(scene.m_TopLevel, RGAccelerationStructureAccess::QueryRead));
				for (const auto blas : scene.m_BottomLevels)
				{
					GGLAB_UNUSED(builder.Read(blas, RGAccelerationStructureAccess::QueryRead));
				}
				for (const auto buffer : { scene.m_Vertices, scene.m_Indices, scene.m_Geometries, scene.m_Instances })
				{
					GGLAB_UNUSED(builder.Read(buffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader));
				}
				const auto& settings = frame.GetDirectionalShadowFramePlan().m_Settings;
				const auto direction = -frame.m_RenderScene.m_WorldSun->m_Direction;
				data.m_Parameters = { .m_ShadowSampler = services.m_Samplers->GetSamplerIndex(SamplerPreset::ShadowCmpLinearClamp),
					.m_View = frame.GetRenderQueue(frame.GetDisplayViewId()).m_CoverageRasterDomain.m_ViewBindingId,
					.m_Width = extent.m_Width, .m_Height = extent.m_Height, .m_ShadowSize = shadow.m_ShadowMapSize,
					.m_ShadowFlags = frame.GetDirectionalShadowFramePlan().m_ShadingEnabled ? (settings.m_EnablePCF ? 3u : 1u) : 0u,
					.m_SunDirection = { direction.m_X, direction.m_Y, direction.m_Z },
					.m_InstanceCount = scene.m_InstanceCount, .m_GeometryCount = scene.m_GeometryCount };
				builder.SideEffect();
			},
			[this, &frame, scene](RGExecuteContext& execute, PassData& data)
			{
				auto* commands = execute.GetDirectComputeCommandContext();
				ScopedConstantBindings<RHIComputeCommandContext> constants(*commands, m_State->m_Layout,
					m_State->m_Scene, m_State->m_Shadow);
				if (!commands->RetainForSubmission(scene.m_Lifetime) || !constants.SetPipeline(m_State->m_Pipeline) ||
					!constants.Bind(m_State->m_Scene, frame.m_FrameConstants.m_SceneSnapshot) ||
					!constants.Bind(m_State->m_Shadow, frame.m_FrameConstants.m_DirectionalShadowSnapshot) ||
					!commands->SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB), frame.m_FrameConstants.m_Views) ||
					!commands->SetAccelerationStructure(m_State->m_RayParameters[ray_scene_bindings::TopLevel],
						execute.GetAccelerationStructureHandle(scene.m_TopLevel)))
				{
					return;
				}
				const std::array buffers{ scene.m_Vertices, scene.m_Indices, scene.m_Geometries, scene.m_Instances };
				for (uint32_t index = 0; index < buffers.size(); ++index)
				{
					commands->SetReadOnlyBuffer(
						m_State->m_RayParameters[index + ray_scene_bindings::Vertices],
						execute.GetBufferHandle(buffers[index]));
				}
				auto parameters = data.m_Parameters;
				parameters.m_Depth = execute.GetViewDescriptor(data.m_Depth).m_Index;
				parameters.m_ShadowMap = data.m_Shadow.IsValid() ? execute.GetViewDescriptor(data.m_Shadow).m_Index : 0;
				parameters.m_Output = execute.GetViewDescriptor(data.m_Output).m_Index;
				if (commands->SetConstantBufferData(static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
				{
					commands->Dispatch((parameters.m_Width + GIQueryGroupWidth - 1) / GIQueryGroupWidth,
						(parameters.m_Height + GIQueryGroupHeight - 1) / GIQueryGroupHeight, 1);
				}
			});
	}
}
