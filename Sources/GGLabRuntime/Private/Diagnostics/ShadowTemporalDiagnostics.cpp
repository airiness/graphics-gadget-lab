#include "GGLabRuntime/Diagnostics/ShadowTemporalDiagnostics.h"

#include <algorithm>
#include <cmath>

namespace gglab
{
	namespace
	{
		bool SamePolicy(const DirectionalShadowSettings& a, const DirectionalShadowSettings& b) noexcept
		{
			return a.m_Enable == b.m_Enable && a.m_EnablePCF == b.m_EnablePCF &&
				a.m_ShadowMapSize == b.m_ShadowMapSize && a.m_CascadeCount == b.m_CascadeCount &&
				a.m_SplitLambda == b.m_SplitLambda &&
				a.m_CascadeBlendFraction == b.m_CascadeBlendFraction &&
				a.m_DistanceFadeFraction == b.m_DistanceFadeFraction &&
				a.m_MaxShadowDistance == b.m_MaxShadowDistance &&
				a.m_FitMode == b.m_FitMode &&
				a.m_EnableTexelSnapping == b.m_EnableTexelSnapping &&
				a.m_CasterExtrusionDistance == b.m_CasterExtrusionDistance &&
				a.m_OrthoPadding == b.m_OrthoPadding && a.m_DepthPadding == b.m_DepthPadding;
		}

		bool SameOrientation(Vector3 a, Vector3 b) noexcept
		{
			const float aLength = a.LengthSquared();
			const float bLength = b.LengthSquared();
			if (aLength <= 1.0e-8f || bLength <= 1.0e-8f) return aLength == bLength;
			a /= std::sqrt(aLength);
			b /= std::sqrt(bLength);
			return std::abs(a.m_X - b.m_X) <= 1.0e-6f &&
				std::abs(a.m_Y - b.m_Y) <= 1.0e-6f &&
				std::abs(a.m_Z - b.m_Z) <= 1.0e-6f;
		}

		bool SameTexelScale(float a, float b) noexcept
		{
			return a > 0.0f && b > 0.0f &&
				std::abs(a - b) <= std::max(a, b) * 1.0e-5f;
		}
	}

	void ShadowTemporalDiagnostics::Reset() noexcept
	{
		m_Previous = {};
		m_Samples.clear();
		m_ReferenceId.clear();
		m_CameraId = 0;
		m_HasPrevious = false;
		m_ResetCount = 0;
	}

	bool ShadowTemporalDiagnostics::IsComparable(const ShadowDiagnosticsSnapshot& current,
		uint64_t cameraId, std::string_view referenceId) const noexcept
	{
		if (!m_HasPrevious || current.m_FrameSerial != m_Previous.m_FrameSerial + 1 ||
			cameraId != m_CameraId || referenceId != m_ReferenceId ||
			current.m_MainView.m_TemporalResetIdentity != m_Previous.m_MainView.m_TemporalResetIdentity ||
			current.m_MainView.m_TemporalSessionIdentity != m_Previous.m_MainView.m_TemporalSessionIdentity ||
			current.m_MainView.m_Width != m_Previous.m_MainView.m_Width ||
			current.m_MainView.m_Height != m_Previous.m_MainView.m_Height ||
			current.m_MainView.m_FovRadians != m_Previous.m_MainView.m_FovRadians ||
			current.m_MainView.m_Aspect != m_Previous.m_MainView.m_Aspect ||
			current.m_MainView.m_Near != m_Previous.m_MainView.m_Near ||
			current.m_MainView.m_Far != m_Previous.m_MainView.m_Far ||
			current.m_Cascades.size() != m_Previous.m_Cascades.size() ||
			!SamePolicy(current.m_Settings, m_Previous.m_Settings) ||
			!SameOrientation(current.m_LightDirection, m_Previous.m_LightDirection))
		{
			return false;
		}
		for (size_t index = 0; index < current.m_Cascades.size(); ++index)
		{
			const auto& a = current.m_Cascades[index];
			const auto& b = m_Previous.m_Cascades[index];
			if (a.m_SplitNear != b.m_SplitNear || a.m_SplitFar != b.m_SplitFar ||
				a.m_BlendStart != b.m_BlendStart ||
				!SameTexelScale(a.m_Projection.m_WorldUnitsPerTexel.m_X,
					b.m_Projection.m_WorldUnitsPerTexel.m_X) ||
				!SameTexelScale(a.m_Projection.m_WorldUnitsPerTexel.m_Y,
					b.m_Projection.m_WorldUnitsPerTexel.m_Y))
			{
				return false;
			}
		}
		return true;
	}

	bool ShadowTemporalDiagnostics::Record(const ShadowDiagnosticsSnapshot& snapshot,
		uint64_t cameraId, std::string_view referenceId) noexcept
	{
		if (snapshot.m_FrameSerial == 0 || cameraId == 0 ||
			!snapshot.m_MainView.m_IsValid || snapshot.m_Cascades.empty() ||
			snapshot.m_Cascades.size() > MaxDirectionalShadowCascades)
		{
			if (m_HasPrevious)
			{
				m_Previous = {};
				m_Samples.clear();
				m_HasPrevious = false;
				++m_ResetCount;
			}
			return false;
		}
		if (m_HasPrevious && snapshot.m_FrameSerial == m_Previous.m_FrameSerial) return false;
		const bool comparable = IsComparable(snapshot, cameraId, referenceId);
		if (m_HasPrevious && !comparable)
		{
			m_Samples.clear();
			++m_ResetCount;
		}
		ShadowTemporalSample sample{};
		sample.m_FrameSerial = snapshot.m_FrameSerial;
		sample.m_CascadeCount = static_cast<uint32_t>(snapshot.m_Cascades.size());
		if (comparable)
		{
			for (size_t index = 0; index < snapshot.m_Cascades.size(); ++index)
			{
				const auto& previous = m_Previous.m_Cascades[index].m_Projection;
				const auto& current = snapshot.m_Cascades[index].m_Projection;
				if (current.m_WorldUnitsPerTexel.m_X > 0.0f &&
					current.m_WorldUnitsPerTexel.m_Y > 0.0f)
				{
					sample.m_ProjectionDeltaTexels[index] = {
						(current.m_CenterLS.m_X - previous.m_CenterLS.m_X) / current.m_WorldUnitsPerTexel.m_X,
						(current.m_CenterLS.m_Y - previous.m_CenterLS.m_Y) / current.m_WorldUnitsPerTexel.m_Y,
					};
				}
			}
		}
		if (m_Samples.size() == MaxSamples) m_Samples.erase(m_Samples.begin());
		m_Samples.push_back(sample);
		m_Previous = snapshot;
		m_CameraId = cameraId;
		m_ReferenceId = referenceId;
		m_HasPrevious = true;
		return true;
	}
}
