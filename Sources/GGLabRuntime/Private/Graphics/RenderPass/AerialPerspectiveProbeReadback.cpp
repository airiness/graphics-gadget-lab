#include "Graphics/RenderPass/AerialPerspectiveProbeReadback.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace gglab
{
	AerialPerspectiveProbeReadback::~AerialPerspectiveProbeReadback()
	{
		ReleaseBuffers();
	}

	void AerialPerspectiveProbeReadback::Initialize(RHIDevice& device,
		uint32_t frameSlotCount) noexcept
	{
		if (m_Device == &device && m_Slots.size() == frameSlotCount) return;
		ReleaseBuffers();
		m_Device = &device;
		m_Slots.resize(frameSlotCount);
		for (auto& slot : m_Slots)
		{
			const RHIBufferDesc desc{
				.m_SizeInBytes = ReadbackSizeInBytes,
				.m_Usage = RHIBufferUsage::CopyDest,
				.m_MemoryUsage = RHIMemoryUsage::GpuToCpu,
				.m_DebugName = "Atmosphere.AerialProbeReadback",
			};
			slot.m_Buffer = device.CreateBuffer(desc);
			GGLAB_ASSERT_MSG(slot.m_Buffer.IsValid(),
				"Aerial probe requires a frame-slot readback buffer.");
		}
	}

	void AerialPerspectiveProbeReadback::ConsumeCompletedSlot(uint32_t frameSlot) noexcept
	{
		if (!m_Device || frameSlot >= m_Slots.size()) return;
		auto& slot = m_Slots[frameSlot];
		if (!slot.m_Pending) return;
		// The RHI has waited for this frame slot before graph construction reuses it.
		const void* mapped = m_Device->MapBuffer(slot.m_Buffer,
			{ .m_Begin = 0, .m_End = ReadbackSizeInBytes });
		if (!mapped)
		{
			GGLAB_LOG_GRAPHICS_ERROR("Aerial probe could not map completed frame slot {}.", frameSlot);
			slot.m_Pending = false;
			return;
		}
		std::array<AerialPerspectiveProbeSample, SampleCount> samples{};
		std::memcpy(samples.data(), mapped, sizeof(samples));
		m_Device->UnmapBuffer(slot.m_Buffer, {});
		slot.m_Pending = false;
		const uint64_t copiedFrameSerial = uint64_t(samples[0].m_FrameSerialLow) |
			(uint64_t(samples[0].m_FrameSerialHigh) << 32);
		const uint64_t skyFrameSerial = uint64_t(samples[1].m_FrameSerialLow) |
			(uint64_t(samples[1].m_FrameSerialHigh) << 32);
		if (copiedFrameSerial != slot.m_FrameSerial || skyFrameSerial != slot.m_FrameSerial)
		{
			// Recorded copies in aborted frames leave the previous slot contents intact.
			return;
		}
		if (slot.m_WorldGeneration == 0 ||
			(slot.m_VerticalFovRadians == m_LastLoggedFovRadians &&
				slot.m_WorldGeneration == m_LastLoggedGeneration))
		{
			return;
		}
		const auto& center = samples[0];
		const auto& sky = samples[1];
		if (!std::isfinite(center.m_Metadata[1]) || center.m_Metadata[3] != 0.0f ||
			sky.m_Metadata[3] != 1.0f || center.m_Metadata[2] <= 0.0f)
		{
			GGLAB_LOG_GRAPHICS_INFO(
				"Aerial probe frame={} fovRad={} has no paired center surface/sky sample "
				"(centerBackground={}, skyBackground={}, preExposure={}).",
				slot.m_FrameSerial, slot.m_VerticalFovRadians, center.m_Metadata[3],
				sky.m_Metadata[3], center.m_Metadata[2]);
			m_LastLoggedFovRadians = slot.m_VerticalFovRadians;
			m_LastLoggedGeneration = slot.m_WorldGeneration;
			return;
		}
		float residual = 0.0f;
		float skyResidual = 0.0f;
		for (size_t channel = 0; channel < 3; ++channel)
		{
			const float expected = center.m_Surface[channel] * center.m_Transmittance[channel] +
				center.m_InScattering[channel];
			residual = std::max(residual, std::abs(center.m_Composite[channel] - expected));
			skyResidual = std::max(skyResidual,
				std::abs(sky.m_Composite[channel] - sky.m_Surface[channel]));
		}
		GGLAB_LOG_GRAPHICS_INFO(
			"Aerial probe frame={} worldGen={} fovRad={} EV100={} preExposure={} "
			"centerKm={} surface=({},{},{}) composite=({},{},{}) T=({},{},{}) "
			"scattering=({},{},{}) maxAbsResidual={} skyMaxAbsChange={}.",
			slot.m_FrameSerial, slot.m_WorldGeneration, slot.m_VerticalFovRadians,
			slot.m_ManualEV100, center.m_Metadata[2], center.m_Metadata[1],
			center.m_Surface[0], center.m_Surface[1], center.m_Surface[2],
			center.m_Composite[0], center.m_Composite[1], center.m_Composite[2],
			center.m_Transmittance[0], center.m_Transmittance[1], center.m_Transmittance[2],
			center.m_InScattering[0], center.m_InScattering[1], center.m_InScattering[2],
			residual, skyResidual);
		m_LastLoggedFovRadians = slot.m_VerticalFovRadians;
		m_LastLoggedGeneration = slot.m_WorldGeneration;
	}

	void AerialPerspectiveProbeReadback::MarkScheduled(uint32_t frameSlot,
		uint64_t frameSerial, uint64_t worldGeneration, float verticalFovRadians,
		float manualEV100) noexcept
	{
		GGLAB_ASSERT(frameSlot < m_Slots.size());
		auto& slot = m_Slots[frameSlot];
		slot.m_FrameSerial = frameSerial;
		slot.m_WorldGeneration = worldGeneration;
		slot.m_VerticalFovRadians = verticalFovRadians;
		slot.m_ManualEV100 = manualEV100;
		slot.m_Pending = true;
	}

	RHIBufferHandle AerialPerspectiveProbeReadback::GetBuffer(uint32_t frameSlot) const noexcept
	{
		GGLAB_ASSERT(frameSlot < m_Slots.size());
		return m_Slots[frameSlot].m_Buffer;
	}

	void AerialPerspectiveProbeReadback::ReleaseBuffers() noexcept
	{
		if (m_Device)
		{
			for (auto& slot : m_Slots)
			{
				if (slot.m_Buffer.IsValid()) m_Device->DestroyBuffer(slot.m_Buffer);
			}
		}
		m_Slots.clear();
		m_Device = nullptr;
		m_LastLoggedFovRadians = -1.0f;
		m_LastLoggedGeneration = 0;
	}
}
