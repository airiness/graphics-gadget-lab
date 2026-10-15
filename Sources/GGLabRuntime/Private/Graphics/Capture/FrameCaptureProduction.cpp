#include "Graphics/Capture/FrameCaptureService.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"
#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>

namespace gglab
{
	bool FrameCaptureService::PrepareProductionCounters(uint64_t frameSerial, Tap& tap, RGProductionCounters& counters) noexcept
	{
		if (!m_ScanShaders || !m_ScanResolver || !m_ScanPipelineSystem)
		{
			return false;
		}
		if (!m_ScanPipelineSystem->IsAlive(m_ProductionClearLayout))
		{
			RHIBindingLayoutDesc desc;
			desc.m_Slots[0] = { .m_Type = RHIBindingType::ReadWriteStorageBuffer, .m_Visibility = RHIShaderStage::Compute, .m_Binding = 0 };
			desc.m_Slots[1] = { .m_Type = RHIBindingType::PushConstants, .m_Visibility = RHIShaderStage::Compute, .m_Binding = 2, .m_SizeInBytes = 8 };
			desc.m_SlotCount = 2;
			desc.m_DebugName = "NonFiniteCounters.Clear";
			m_ProductionClearLayout = m_ScanPipelineSystem->CreateBindingLayout(desc);
		}
		if (!m_ProductionClearRecipe.m_CSId.IsValid())
		{
			m_ProductionClearRecipe.m_CSId = m_ScanShaders->LoadProgram(shader_programs::NonFiniteCountersClearCompute);
		}
		if (!m_ProductionClearRecipe.m_CSId.IsValid() || !m_ProductionClearLayout.IsValid())
		{
			return false;
		}
		m_ProductionClearRecipe.m_BindingLayout = m_ProductionClearLayout;
		const auto pipeline = m_ScanResolver->Resolve(m_ProductionClearSlot, m_ProductionClearRecipe,
			{ .m_TypeName = "NonFiniteCounters", .m_DisplayName = "Non-finite production counters", .m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute });
		if (!pipeline.IsValid())
		{
			return false;
		}
		tap.m_FrameSerial = frameSerial;
		tap.m_Source = FrameCaptureSource::NonFiniteScan;
		tap.m_AdapterCompatibilityIdentity = m_Device->GetAdapterCompatibilityIdentity();
		tap.m_ProductionCounters = true;
		tap.m_NonFiniteScanActive = true;
		tap.m_Buffer = RHIBufferOwner(m_Device, m_Device->CreateBuffer({ .m_SizeInBytes = nonfinite::BufferBytes,
			.m_Usage = RHIBufferUsage::CopyDest, .m_MemoryUsage = RHIMemoryUsage::GpuToCpu, .m_DebugName = "NonFiniteCounters.Readback" }));
		if (!tap.m_Buffer)
		{
			return false;
		}
		counters = { .m_Readback = tap.m_Buffer.Get(), .m_Bytes = nonfinite::BufferBytes, .m_Binding = nonfinite::CounterBinding,
			.m_Clear = [pipeline, frameSerial](RGExecuteContext& context, RGBufferId record)
			{
				auto* commands = context.GetDirectComputeCommandContext();
				commands->SetPipeline(pipeline);
				commands->SetReadWriteBuffer(0, context.GetBufferHandle(record));
				commands->SetPushConstants(1, std::array{ static_cast<uint32_t>(frameSerial), static_cast<uint32_t>(frameSerial >> 32) });
				commands->Dispatch((nonfinite::SiteCount * nonfinite::ProgramCount + 63) / 64, 1, 1);
			} };
		return true;
	}

	void FrameCaptureService::PublishProductionTap(Tap& tap) noexcept
	{
		auto frame = std::make_shared<NonFiniteProductionFrame>();
		frame->m_FrameSerial = tap.m_FrameSerial;
		const auto* mapped = static_cast<const uint32_t*>(m_Device->MapBuffer(tap.m_Buffer.Get(), { 0, nonfinite::BufferBytes }));
		if (!mapped)
		{
			frame->m_Failure = "Production counter readback mapping failed.";
			m_LatestProduction = std::move(frame);
			return;
		}
		*frame = DecodeNonFiniteProductionFrame(std::span(reinterpret_cast<const std::byte*>(mapped), nonfinite::BufferBytes), tap.m_FrameSerial);
		m_Device->UnmapBuffer(tap.m_Buffer.Get(), {});
		frame->m_SubmissionFenceIndex = tap.m_Fence.m_Fence.Index();
		frame->m_SubmissionFenceGeneration = tap.m_Fence.m_Fence.Generation();
		frame->m_SubmissionFenceValue = tap.m_Fence.m_Value;
		frame->m_AdapterCompatibilityIdentity = tap.m_AdapterCompatibilityIdentity;
		m_RetainedProduction.push_back(frame);
		m_LatestProduction = std::move(frame);
	}
}
