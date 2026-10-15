#include "Capture/NonFiniteWatchCoordinator.h"

#include <algorithm>
#include <utility>

namespace gglab
{
	namespace
	{
		bool SameAddress(const RGResourceAddress& left, const RGResourceAddress& right) noexcept
		{
			return left.m_Resource == right.m_Resource && left.m_Position == right.m_Position && left.m_Pass == right.m_Pass &&
				left.m_Occurrence == right.m_Occurrence && left.m_Mip == right.m_Mip && left.m_FirstSlice == right.m_FirstSlice &&
				left.m_SliceCount == right.m_SliceCount && left.m_Aspect == right.m_Aspect;
		}
	}

	bool NonFiniteWatchCoordinator::Configure(NonFiniteWatchConfiguration configuration, std::string& error) noexcept
	{
		error.clear();
		if (configuration.m_Enabled)
		{
			if (!m_Capture)
			{
				error = "NonFiniteWatchUnavailable: no Runtime capture service is composed.";
			}
			else if (!configuration.m_Boundary && !configuration.m_Production)
			{
				error = "A watch configuration must request at least one claim.";
			}
			else if (configuration.m_Mode == NonFiniteWatchMode::Fail)
			{
				error = configuration.m_Production && !m_Capture->HasNonFiniteInstrumentation()
					? "RequiresRestart: production fail mode requires --watch-nonfinite at startup."
					: "FailModeUnavailable: watch coverage and sequence completion are not implemented yet.";
			}
			else if (configuration.m_Boundary && configuration.m_Targets.empty())
			{
				error = "DefaultTargetsUnavailable: supply explicit watch targets until semantic defaults are implemented.";
			}
			else if (configuration.m_Targets.size() > 64 || (!configuration.m_Boundary && !configuration.m_Targets.empty()))
			{
				error = "Watch targets require the boundary claim and are limited to 64 addresses.";
			}
			else
			{
				for (const auto& target : configuration.m_Targets)
				{
					if (target.m_Address.m_Resource.empty() || target.m_BufferLayout.m_Failure != NonFiniteScanLayoutFailure::None)
					{
						error = "Watch targets require a resource address and a valid optional buffer interpretation.";
						break;
					}
				}
			}
		}
		if (!error.empty())
		{
			return false;
		}
		const uint64_t version = m_Status.m_ConfigurationVersion + 1;
		m_Configuration = std::move(configuration);
		m_Status = { .m_ConfigurationVersion = version, .m_Enabled = m_Configuration.m_Enabled,
			.m_BoundaryRequested = m_Configuration.m_Boundary, .m_ProductionRequested = m_Configuration.m_Production };
		m_FirstApplicableFrame = m_LastPreparedFrame + 1;
		if (m_Status.m_Enabled)
		{
			if (m_Configuration.m_Boundary)
			{
				m_Status.m_Boundary = {};
			}
			if (m_Configuration.m_Production && m_Capture->HasNonFiniteInstrumentation())
			{
				m_Status.m_Production = {};
			}
			for (const auto& target : m_Configuration.m_Targets)
			{
				m_Status.m_Targets.push_back({ .m_Address = target.m_Address });
			}
		}
		return true;
	}

	void NonFiniteWatchCoordinator::PrepareFrame(uint64_t frameSerial, bool yieldBoundaryCapacity) noexcept
	{
		if (frameSerial <= m_LastPreparedFrame)
		{
			return;
		}
		m_LastPreparedFrame = frameSerial;
		if (!m_Status.m_Enabled || !m_Configuration.m_Boundary)
		{
			return;
		}
		const uint64_t id = !yieldBoundaryCapacity && m_Pending.size() < 64
			? m_Capture->RequestNonFiniteScan(m_Configuration.m_Targets, m_Status.m_ConfigurationVersion, frameSerial) : 0;
		if (id == 0)
		{
			m_Status.m_BoundaryFrameSerial = frameSerial;
			m_Status.m_Boundary = {};
			m_Status.m_Failure = yieldBoundaryCapacity ? "MissingCoverage: boundary reporting yielded capacity to a scheduled resource export."
				: "MissingCoverage: watch request capacity is exhausted.";
			for (auto& target : m_Status.m_Targets)
			{
				target = { .m_Address = target.m_Address };
			}
			return;
		}
		m_Pending.emplace(id, Pending{ m_Status.m_ConfigurationVersion, frameSerial });
	}

	void NonFiniteWatchCoordinator::RecordAnomaly(NonFiniteWatchAnomaly anomaly) noexcept
	{
		if (!m_Status.m_FirstAnomaly || anomaly.m_FrameSerial < m_Status.m_FirstAnomaly->m_FrameSerial)
		{
			m_Status.m_FirstAnomaly = std::move(anomaly);
		}
	}

	bool NonFiniteWatchCoordinator::OnScanResult(const FrameCaptureResult& result) noexcept
	{
		if (result.m_Source != FrameCaptureSource::NonFiniteScan)
		{
			return false;
		}
		const auto found = m_Pending.find(result.m_RequestId);
		if (found == m_Pending.end())
		{
			return false;
		}
		const Pending pending = found->second;
		m_Pending.erase(found);
		if (!m_Status.m_Enabled || pending.m_ConfigurationVersion != m_Status.m_ConfigurationVersion)
		{
			return true;
		}
		std::vector<NonFiniteWatchTargetStatus> targets;
		NonFiniteClaimResult claim;
		std::string failure = result.m_Failure;
		const auto& payload = result.m_NonFiniteScans;
		const bool addressesMatch = payload && payload->m_Boundaries.size() == m_Configuration.m_Targets.size() &&
			std::ranges::equal(payload->m_Boundaries, m_Configuration.m_Targets, [](const auto& boundary, const auto& target)
				{ return SameAddress(boundary.m_Target.m_Address, target.m_Address); });
		if (result.m_Status == FrameCaptureStatus::Completed && result.m_FrameSerial == pending.m_FrameSerial && payload &&
			payload->m_ConfigurationVersion == pending.m_ConfigurationVersion && addressesMatch)
		{
			claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None };
			for (const auto& boundary : payload->m_Boundaries)
			{
				NonFiniteWatchTargetStatus target{ .m_Address = boundary.m_Target.m_Address,
					.m_Record = boundary.m_Record, .m_Result = boundary.m_Result };
				if (const auto index = target.m_Result.m_FirstNonFiniteIndex; index)
				{
					if (boundary.m_Target.m_Type == RGResourceType::RGTexture)
					{
						target.m_TextureLocation = GetNonFiniteTextureLocation(*index, boundary.m_Parameters.m_Width,
							boundary.m_Parameters.m_Height, boundary.m_Target.m_Subresources.m_ArraySliceCount, boundary.m_Parameters.m_Channels);
						if (target.m_TextureLocation)
						{
							target.m_TextureLocation->m_Slice += boundary.m_Target.m_Subresources.m_BaseArraySlice;
						}
					}
					else if (const auto& components = boundary.m_BufferLayout.m_Components; !components.empty())
					{
						target.m_Element = *index / static_cast<uint32_t>(components.size());
						target.m_ByteOffset = components[*index % components.size()].m_Offset;
					}
				}
				if (target.m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Failed)
				{
					RecordAnomaly({ .m_FrameSerial = pending.m_FrameSerial, .m_Failure = target.m_Result.m_Claim.m_Failure, .m_Target = target });
					claim = target.m_Result.m_Claim;
				}
				else if (target.m_Result.m_Claim.m_Status != NonFiniteClaimStatus::Passed && claim.m_Status != NonFiniteClaimStatus::Failed)
				{
					claim = {};
				}
				targets.push_back(std::move(target));
			}
		}
		else
		{
			for (const auto& target : m_Configuration.m_Targets)
			{
				targets.push_back({ .m_Address = target.m_Address });
			}
			if (failure.empty())
			{
				failure = "MissingCoverage: the watch frame returned missing or mismatched scan evidence.";
			}
		}
		if (pending.m_FrameSerial >= m_Status.m_BoundaryFrameSerial)
		{
			m_Status.m_BoundaryFrameSerial = pending.m_FrameSerial;
			m_Status.m_Boundary = claim;
			m_Status.m_Targets = std::move(targets);
			m_Status.m_Failure = std::move(failure);
		}
		return true;
	}

	void NonFiniteWatchCoordinator::OnProductionFrame(const NonFiniteProductionFrame& frame) noexcept
	{
		if (!m_Status.m_Enabled || !m_Configuration.m_Production || frame.m_FrameSerial < m_FirstApplicableFrame)
		{
			return;
		}
		for (const auto& site : frame.m_Sites)
		{
			if (site.m_Claim.m_Status == NonFiniteClaimStatus::Failed)
			{
				RecordAnomaly({ .m_FrameSerial = frame.m_FrameSerial, .m_Failure = site.m_Claim.m_Failure,
					.m_ProgramIndex = site.m_ProgramIndex, .m_SiteIndex = site.m_SiteIndex });
			}
		}
		if (frame.m_FrameSerial >= m_Status.m_ProductionFrameSerial)
		{
			m_Status.m_ProductionFrameSerial = frame.m_FrameSerial;
			m_Status.m_Production = frame.m_Claim;
			m_Status.m_ProductionFailure = frame.m_Failure;
		}
	}
}
