#pragma once

#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gglab
{
	class RHIDevice;

	struct AerialPerspectiveProbeSample
	{
		std::array<float, 3> m_Surface{};
		uint32_t m_FrameSerialLow = 0;
		std::array<float, 3> m_Composite{};
		uint32_t m_FrameSerialHigh = 0;
		std::array<float, 4> m_Transmittance{};
		std::array<float, 4> m_InScattering{};
		// Raw depth, Euclidean ray distance in km, pre-exposure, background flag.
		std::array<float, 4> m_Metadata{};
	};
	static_assert(sizeof(AerialPerspectiveProbeSample) == 80);
	static_assert(offsetof(AerialPerspectiveProbeSample, m_FrameSerialLow) == 12);
	static_assert(offsetof(AerialPerspectiveProbeSample, m_Composite) == 16);
	static_assert(offsetof(AerialPerspectiveProbeSample, m_FrameSerialHigh) == 28);
	static_assert(offsetof(AerialPerspectiveProbeSample, m_Transmittance) == 32);
	static_assert(offsetof(AerialPerspectiveProbeSample, m_InScattering) == 48);
	static_assert(offsetof(AerialPerspectiveProbeSample, m_Metadata) == 64);

	class AerialPerspectiveProbeReadback final
	{
	public:
		AerialPerspectiveProbeReadback() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(AerialPerspectiveProbeReadback);
		~AerialPerspectiveProbeReadback();

		void Initialize(RHIDevice& device, uint32_t frameSlotCount) noexcept;
		void ConsumeCompletedSlot(uint32_t frameSlot) noexcept;
		void MarkScheduled(uint32_t frameSlot, uint64_t frameSerial, uint64_t worldGeneration,
			float verticalFovRadians, float manualEV100) noexcept;
		[[nodiscard]] RHIBufferHandle GetBuffer(uint32_t frameSlot) const noexcept;

		static constexpr uint32_t SampleCount = 2;
		static constexpr uint64_t ReadbackSizeInBytes =
			SampleCount * sizeof(AerialPerspectiveProbeSample);

	private:
		struct Slot
		{
			RHIBufferHandle m_Buffer{};
			uint64_t m_FrameSerial = 0;
			uint64_t m_WorldGeneration = 0;
			float m_VerticalFovRadians = 0.0f;
			float m_ManualEV100 = 0.0f;
			bool m_Pending = false;
		};

		void ReleaseBuffers() noexcept;
		RHIDevice* m_Device = nullptr;
		std::vector<Slot> m_Slots;
		float m_LastLoggedFovRadians = -1.0f;
		uint64_t m_LastLoggedGeneration = 0;
	};
}
