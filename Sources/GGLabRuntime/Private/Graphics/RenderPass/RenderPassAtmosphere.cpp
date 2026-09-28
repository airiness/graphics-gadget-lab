#include "Graphics/RenderPass/RenderPassAtmosphere.h"
#include "Graphics/RenderPass/AtmosphereGraphResources.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
namespace gglab
{
	namespace
	{
		struct Parameters
		{
			uint32_t m_Output = 0, m_Transmittance = 0, m_Multiple = 0, m_Sampler = 0;
			uint32_t m_Stage = 0, m_Width = 0, m_Height = 0, m_Padding = 0;
		};
		static_assert(sizeof(Parameters) == 32 && IsPassRootConstantStruct<Parameters>);
		struct PassData { RGTextureViewId m_Output, m_Transmittance, m_Multiple; };
		struct SetupData {};
	}
	void RenderPassAtmosphere::AddPass(RenderGraph& rg, const RenderFrameContext& context,
		const RenderServices& services) noexcept
	{
		auto* atmosphere = services.m_Atmosphere;
		if (!atmosphere) return;
		const auto& scene = context.m_RenderScene;
		if (!scene.m_Atmosphere || !scene.m_WorldSun)
		{
			atmosphere->Disable();
			return;
		}
		const bool skyConstants = services.m_Environment &&
			services.m_Environment->GetEnvironmentLightingSettings().m_EnableSkybox &&
			services.m_Environment->GetEnvironmentLightingSettings().m_BackgroundMode !=
				EnvironmentBackgroundMode::TextureEnvironment;
		AddLuts(rg, services, *atmosphere, ResolveAtmosphere(*scene.m_Atmosphere, *scene.m_WorldSun,
			context.GetDisplayRenderView().m_CameraPosition), skyConstants, false);
	}

	void RenderPassAtmosphere::AddBakePass(RenderGraph& rg, const RenderServices& services) noexcept
	{
		if (!services.m_Environment || !services.m_BakeAtmosphere ||
			services.m_Environment->GetStageForRecording() != IBLBakeStage::Environment) return;
		if (const auto* parameters = services.m_Environment->GetBakingAtmosphereParameters())
		{
			AddLuts(rg, services, *services.m_BakeAtmosphere, *parameters, true, true);
		}
	}

	void RenderPassAtmosphere::AddLuts(RenderGraph& rg, const RenderServices& services,
		RenderAtmosphereAccess& access, const AtmosphereGPU& parameters, bool skyConstants, bool bake) noexcept
	{
		auto* atmosphere = &access;
		if (!m_Recipe.m_CSId.IsValid())
		{
			m_Recipe.m_CSId = services.m_ShaderPrograms->LoadProgram(shader_programs::AtmosphereLutCompute);
			m_Recipe.m_BindingLayout = services.m_BindingLayout->GetCommonBindingLayout();
		}
		const auto pipeline = services.m_PipelineResolver->Resolve(m_Slot, m_Recipe, GetInfo());
		if (!pipeline.IsValid()) { atmosphere->Disable(); return; }
		const uint64_t generation = services.m_ShaderPrograms->GetGeneration(m_Recipe.m_CSId);
		if (!atmosphere->Begin(parameters, { generation, generation, generation }, skyConstants)) return;
		const auto diagnostics = atmosphere->GetDiagnostics();
		const auto* resourceName = bake ? BakeAtmosphereResourcesName : AtmosphereResourcesName;
		const auto names = bake ? BakeAtmosphereLutNames : AtmosphereLutNames;
		auto& resources = rg.GetBlackboard().Create<RGAtmosphereResources>(resourceName);
		resources.m_Diagnostics = diagnostics;
		rg.AddPass<SetupData>(bake ? "IBL.Atmosphere.Import" : "Atmosphere.Import",
			[atmosphere, resourceName, names](RenderGraph::RGBuilder& builder, SetupData&)
			{
				auto& resources = builder.GetBlackboard().Get<RGAtmosphereResources>(resourceName);
				for (uint32_t i = 0; i < 3; ++i)
				{
					const bool initialized = atmosphere->IsInitialized(i);
					resources.m_Luts[i] = builder.ImportTexture(names[i], atmosphere->GetTexture(i),
						atmosphere->GetTextureDesc(i), initialized ? CommonRHIResourceState() : UndefinedRHITextureState(),
						initialized ? RGContentValidity::Defined : RGContentValidity::Undefined);
				}
			});
		const uint32_t sampler = services.m_Samplers->GetSamplerIndex(SamplerPreset::LinearClamp);
		for (uint32_t stage = 0; stage < 3; ++stage)
		{
			if (!(diagnostics.m_DirtyMask & (1u << stage))) continue;
			rg.AddPass<PassData>(names[stage], RGPassEncoderType::Compute,
				[stage, resourceName](RenderGraph::RGBuilder& builder, PassData& data)
				{
					auto& resources = builder.GetBlackboard().Get<RGAtmosphereResources>(resourceName);
					builder.WriteInPlace(resources.m_Luts[stage], RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_Output = builder.CreateView<RHITextureViewType::UnorderedAccess>(resources.m_Luts[stage]);
					if (stage > 0)
					{
						const auto source = builder.Read(resources.m_Luts[0], RGTextureAccess::Sample, RHIStage::ComputeShader);
						data.m_Transmittance = builder.CreateView<RHITextureViewType::ShaderResource>(source);
					}
					if (stage > 1)
					{
						const auto source = builder.Read(resources.m_Luts[1], RGTextureAccess::Sample, RHIStage::ComputeShader);
						data.m_Multiple = builder.CreateView<RHITextureViewType::ShaderResource>(source);
					}
				},
				[atmosphere, stage, pipeline, sampler](RGExecuteContext& execute, PassData& data)
				{
					auto* command = execute.GetDirectComputeCommandContext();
					Parameters parameters{};
					parameters.m_Output = execute.GetViewDescriptor(data.m_Output).m_Index;
					if (stage > 0) parameters.m_Transmittance = execute.GetViewDescriptor(data.m_Transmittance).m_Index;
					if (stage > 1) parameters.m_Multiple = execute.GetViewDescriptor(data.m_Multiple).m_Index;
					parameters.m_Sampler = sampler;
					parameters.m_Stage = stage;
					parameters.m_Width = AtmosphereLutWidths[stage];
					parameters.m_Height = AtmosphereLutHeights[stage];
					command->SetPipeline(pipeline);
					// CPU-uploaded, immutable for the frame, retained by AtmosphereSystem until submission completes.
					command->SetConstantBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::SceneCB), atmosphere->GetConstants(), 0);
					command->SetPushConstants(static_cast<uint32_t>(CommonRSRootParamIndex::PassConstants), parameters);
					command->Dispatch((parameters.m_Width + 7) / 8, (parameters.m_Height + 7) / 8, 1);
					atmosphere->NotifyExecuted(stage);
				});
		}
	}

	void RenderPassAtmosphere::AddFinishPass(RenderGraph& rg) noexcept
	{
		// Export closes graph ownership. All scene and diagnostic reads must be declared first.
		for (const bool bake : { false, true })
		{
			const auto* resourceName = bake ? BakeAtmosphereResourcesName : AtmosphereResourcesName;
			if (!rg.GetBlackboard().TryGet<RGAtmosphereResources>(resourceName)) continue;
			rg.AddPass<SetupData>(bake ? "IBL.Atmosphere.Export" : "Atmosphere.Export",
				[resourceName](RenderGraph::RGBuilder& builder, SetupData&)
				{
					builder.SideEffect();
					const auto& resources = builder.GetBlackboard().Get<RGAtmosphereResources>(resourceName);
					for (const auto texture : resources.m_Luts)
					{
						builder.Export(texture, RGTextureAccess::None);
					}
				});
		}
	}
}
