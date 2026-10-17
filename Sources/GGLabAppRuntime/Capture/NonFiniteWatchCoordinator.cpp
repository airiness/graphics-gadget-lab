#include "Capture/NonFiniteWatchCoordinator.h"
#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"

#include <algorithm>
#include <format>
#include <utility>

namespace gglab
{
	namespace
	{
		bool Clean(NonFiniteClaimResult claim) noexcept
		{
			return claim.m_Status == NonFiniteClaimStatus::Passed || claim.m_Status == NonFiniteClaimStatus::NotApplicable;
		}
		void Merge(NonFiniteClaimResult& aggregate, NonFiniteClaimResult claim) noexcept
		{
			if (claim.m_Status == NonFiniteClaimStatus::Failed ||
				(aggregate.m_Status != NonFiniteClaimStatus::Failed && !Clean(claim)))
			{
				aggregate = claim.m_Status == NonFiniteClaimStatus::Unavailable ? NonFiniteClaimResult{} : claim;
			}
			else if (aggregate.m_Status == NonFiniteClaimStatus::NotApplicable && claim.m_Status == NonFiniteClaimStatus::Passed)
			{
				aggregate = claim;
			}
		}
		std::string Describe(const NonFiniteWatchAnomaly& anomaly, const std::optional<NonFiniteInjection>& injection)
		{
			std::string text = std::format("{}: frame {}", GetNonFiniteWatchFailureName(anomaly.m_Failure), anomaly.m_FrameSerial);
			if (anomaly.m_Target)
			{
				const auto& target = *anomaly.m_Target;
				text += std::format(", resource '{}', mip {}", target.m_Address.m_Resource, target.m_Address.m_Mip);
				if (target.m_TextureLocation)
				{
					const auto location = *target.m_TextureLocation;
					text += std::format(", x {}, y {}, slice {}, channel {}", location.m_X, location.m_Y, location.m_Slice, location.m_Channel);
				}
				else if (target.m_Element && target.m_ByteOffset)
				{
					text += std::format(", element {}, byte offset {}", *target.m_Element, *target.m_ByteOffset);
				}
			}
			else if (anomaly.m_ProgramIndex && anomaly.m_SiteIndex)
			{
				text += std::format(", program {}, site {}", *anomaly.m_ProgramIndex, *anomaly.m_SiteIndex);
				if (*anomaly.m_ProgramIndex < nonfinite::ProgramCount && *anomaly.m_SiteIndex < nonfinite::SiteCount)
				{
					text += std::format(" ('{}', '{}')", nonfinite::Programs[*anomaly.m_ProgramIndex], nonfinite::Sites[*anomaly.m_SiteIndex].m_Id);
				}
			}
			if (injection && injection->m_PointIndex < nonfinite::InjectionPoints.size())
			{
				text += std::format(", under Debug injection '{}'", nonfinite::InjectionPoints[injection->m_PointIndex].m_Id);
			}
			return text;
		}
		bool SameAddress(const RGResourceAddress& left, const RGResourceAddress& right) noexcept
		{
			return left.m_Resource == right.m_Resource && left.m_Position == right.m_Position && left.m_Pass == right.m_Pass &&
				left.m_Occurrence == right.m_Occurrence && left.m_Mip == right.m_Mip && left.m_FirstSlice == right.m_FirstSlice &&
				left.m_SliceCount == right.m_SliceCount && left.m_Aspect == right.m_Aspect;
		}
	}

	const char* GetNonFiniteWatchFailureName(NonFiniteWatchFailure failure) noexcept
	{
		switch (failure)
		{
		case NonFiniteWatchFailure::NonFiniteDetected:
			return "NonFiniteDetected";
		case NonFiniteWatchFailure::NonFiniteReplaced:
			return "NonFiniteReplaced";
		case NonFiniteWatchFailure::ClampPolicyExceeded:
			return "ClampPolicyExceeded";
		case NonFiniteWatchFailure::MissingCoverage:
			return "MissingCoverage";
		default:
			return "None";
		}
	}

	bool NonFiniteWatchCoverage::Fails() const noexcept
	{
		return m_Mode == NonFiniteWatchMode::Fail && (!m_Failure.empty() ||
			(m_BoundaryRequested && m_Boundary.m_Status == NonFiniteClaimStatus::Failed) ||
			(m_ProductionRequested && m_Production.m_Status == NonFiniteClaimStatus::Failed));
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
			else if (configuration.m_Mode == NonFiniteWatchMode::Fail && configuration.m_Production && !m_Capture->HasNonFiniteInstrumentation())
			{
				error = "RequiresRestart: production fail mode requires --watch-nonfinite at startup.";
			}
			else if (configuration.m_Injection && !m_Capture->HasNonFiniteInstrumentation())
			{
				error = "RequiresRestart: non-finite injection requires --watch-nonfinite at startup.";
			}
			else if (configuration.m_Injection && (!configuration.m_Production || !IsValidNonFiniteInjection(*configuration.m_Injection)))
			{
				error = "Non-finite injection requires the production claim, a registered point and a component below 4.";
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
		// Frames built after this call carry the new request; earlier frames are drained
		// as an older configuration version.
		const std::optional<NonFiniteInjection> injection = configuration.m_Enabled ? configuration.m_Injection : std::nullopt;
		if ((injection || m_Status.m_Injection) && m_Capture && !m_Capture->SetNonFiniteInjection(injection))
		{
			error = "NonFiniteWatchUnavailable: the Runtime capture service rejected the injection request.";
			return false;
		}
		const uint64_t version = m_Status.m_ConfigurationVersion + 1;
		for (auto& [id, coverage] : m_Coverage)
		{
			if (!coverage.m_Report.m_Complete)
			{
				coverage.m_Closed = true;
				coverage.m_Frames.clear();
				coverage.m_Report.m_CompletedFrames = coverage.m_Report.m_FrameCount;
				if (coverage.m_Report.m_Failure.empty())
				{
					coverage.m_Report.m_Failure = "MissingCoverage: watch configuration changed during the coverage range.";
				}
				Merge(coverage.m_Boundary, {});
				Merge(coverage.m_Production, {});
			}
		}
		RefreshCoverage();
		m_PreparedCoverage.reset();
		m_Configuration = std::move(configuration);
		m_Status = { .m_ConfigurationVersion = version, .m_Enabled = m_Configuration.m_Enabled,
			.m_Mode = m_Configuration.m_Mode,
			.m_BoundaryRequested = m_Configuration.m_Boundary, .m_ProductionRequested = m_Configuration.m_Production,
			.m_DefaultTargets = m_Configuration.m_Targets.empty(), .m_Injection = injection };
		m_FirstApplicableFrame = m_LastPreparedFrame + 1;
		m_Generations = {};
		m_GenerationCursor = 0;
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

	void NonFiniteWatchCoordinator::PrepareFrame(uint64_t frameSerial, bool yieldBoundaryCapacity,
		std::span<const NonFiniteWatchDefault> defaults) noexcept
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
		Pending pending{ m_Status.m_ConfigurationVersion, frameSerial };
		const bool exportFrame = yieldBoundaryCapacity;
		yieldBoundaryCapacity &= m_Configuration.m_Mode != NonFiniteWatchMode::Fail;
		std::vector<size_t> generationCandidates;
		if (m_Status.m_DefaultTargets)
		{
			for (size_t i = 0; i < NonFiniteWatchSemanticCount; ++i)
			{
				NonFiniteWatchTargetStatus target{ .m_Semantic = static_cast<NonFiniteWatchSemantic>(i) };
				if (defaults.size() == NonFiniteWatchSemanticCount && defaults[i].m_Semantic == *target.m_Semantic)
				{
					const auto& resolved = defaults[i];
					bool completeMips = !resolved.m_Mips.empty() && resolved.m_Mips.size() <= 32;
					for (size_t mip = 0; mip < resolved.m_Mips.size(); ++mip)
					{
						const auto& address = resolved.m_Mips[mip];
						completeMips &= address.m_Mip == mip && address.m_FirstSlice == 0 &&
							address.m_SliceCount && *address.m_SliceCount != 0 && !address.m_Resource.empty();
					}
					if (GetNonFiniteWatchTier(*target.m_Semantic) == 2 && resolved.m_Ready &&
						resolved.m_Generation.IsValid() && completeMips)
					{
						auto& generation = m_Generations[i];
						if (generation.m_Generation != resolved.m_Generation || generation.m_Results.size() != resolved.m_Mips.size())
						{
							generation = { resolved.m_Generation };
							generation.m_Results.resize(resolved.m_Mips.size());
							generation.m_InFlight.resize(resolved.m_Mips.size());
						}
						for (size_t mip = 0; mip < resolved.m_Mips.size(); ++mip)
						{
							NonFiniteWatchTargetStatus surface{ .m_Semantic = target.m_Semantic,
								.m_Address = resolved.m_Mips[mip], .m_Generation = resolved.m_Generation };
							ApplyGenerationEvidence(surface, frameSerial);
							if (!generation.m_Results[mip] && generation.m_InFlight[mip] == 0)
							{
								if (resolved.m_WrittenThisFrame)
								{
									// A later write can replace this revision before rotation reaches it,
									// so it joins the per-frame scans: fail mode defers a busy frame
									// instead of leaving the revision unscanned.
									pending.m_ScanIndices.push_back(pending.m_Targets.size());
									pending.m_Scans.push_back({ .m_Address = surface.m_Address });
								}
								else
								{
									generationCandidates.push_back(pending.m_Targets.size());
								}
							}
							pending.m_Targets.push_back(std::move(surface));
						}
						continue;
					}
					if (GetNonFiniteWatchTier(*target.m_Semantic) == 2)
					{
						m_Generations[i] = {};
						target.m_Result.m_Claim = resolved.m_Ready ? NonFiniteClaimResult{} : resolved.m_Claim;
						target.m_Failure = resolved.m_Ready ? "MissingCoverage: generation identity or mip coverage is missing." : resolved.m_Failure;
						pending.m_Targets.push_back(std::move(target));
						continue;
					}
					target.m_Address = resolved.m_Address;
					target.m_Result.m_Claim = resolved.m_Claim;
					target.m_Failure = resolved.m_Failure;
					if (resolved.m_Ready)
					{
						pending.m_ScanIndices.push_back(pending.m_Targets.size());
						pending.m_Scans.push_back({ .m_Address = resolved.m_Address });
					}
				}
				else
				{
					target.m_Failure = "MissingCoverage: default applicability was not resolved for this frame.";
				}
				pending.m_Targets.push_back(std::move(target));
			}
			// A generation consists of individually scheduled mip scans, not one
			// oversized group. Original per-frame scans retain priority. Rotate across
			// generation surfaces so a frequently changing SkyView cannot starve IBL.
			const size_t limit = exportFrame ? pending.m_Scans.size() : m_Capture->GetNonFiniteScanTargetLimit();
			const size_t count = pending.m_Targets.size();
			for (size_t offset = 0; offset < count && pending.m_Scans.size() < limit; ++offset)
			{
				const size_t index = (m_GenerationCursor + offset) % count;
				if (std::ranges::find(generationCandidates, index) != generationCandidates.end())
				{
					pending.m_ScanIndices.push_back(index);
					pending.m_Scans.push_back({ .m_Address = pending.m_Targets[index].m_Address });
				}
			}
			if (!pending.m_ScanIndices.empty())
			{
				m_GenerationCursor = (pending.m_ScanIndices.back() + 1) % count;
			}
		}
		else
		{
			pending.m_Scans = m_Configuration.m_Targets;
			for (const auto& scan : pending.m_Scans)
			{
				pending.m_ScanIndices.push_back(pending.m_Targets.size());
				pending.m_Targets.push_back({ .m_Address = scan.m_Address });
			}
		}
		if (pending.m_Scans.empty())
		{
			m_PreparedCoverage = CoverageFrame{ .m_FrameSerial = frameSerial, .m_Targets = pending.m_Targets };
			PublishBoundary(std::move(pending));
			return;
		}
		const uint64_t id = !yieldBoundaryCapacity && m_Pending.size() < 64
			? m_Capture->RequestNonFiniteScan(pending.m_Scans, m_Status.m_ConfigurationVersion, frameSerial,
				false, 0, m_Configuration.m_Mode == NonFiniteWatchMode::Fail) : 0;
		m_PreparedCoverage = CoverageFrame{ .m_FrameSerial = frameSerial, .m_BoundaryPending = id != 0, .m_Targets = pending.m_Targets };
		if (id == 0)
		{
			for (const size_t index : pending.m_ScanIndices)
			{
				pending.m_Targets[index].m_Failure = yieldBoundaryCapacity
					? "MissingCoverage: boundary reporting yielded capacity to a scheduled resource export."
					: "MissingCoverage: watch request capacity is exhausted.";
			}
			PublishBoundary(std::move(pending));
			return;
		}
		for (const size_t index : pending.m_ScanIndices)
		{
			const auto& target = pending.m_Targets[index];
			if (target.m_Generation.IsValid())
			{
				m_Generations[static_cast<size_t>(*target.m_Semantic)].m_InFlight[target.m_Address.m_Mip] = id;
			}
		}
		m_Pending.emplace(id, std::move(pending));
	}

	void NonFiniteWatchCoordinator::ApplyGenerationEvidence(NonFiniteWatchTargetStatus& target, uint64_t frameSerial) const noexcept
	{
		if (!target.m_Generation.IsValid() || !target.m_Semantic)
		{
			return;
		}
		const auto& generation = m_Generations[static_cast<size_t>(*target.m_Semantic)];
		const size_t mip = target.m_Address.m_Mip;
		if (generation.m_Generation != target.m_Generation || mip >= generation.m_Results.size())
		{
			return;
		}
		if (const auto& evidence = generation.m_Results[mip]; evidence)
		{
			const auto address = target.m_Address;
			target = *evidence;
			target.m_Address = address;
			if (target.m_CheckedFrameSerial != frameSerial && target.m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Passed)
			{
				// No new generation: retain the last checked record without claiming a
				// scan of this frame. Failed content stays failed while it is active.
				target.m_Result.m_Claim = { NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None };
			}
		}
		else if (target.m_Failure.empty())
		{
			target.m_Failure = generation.m_InFlight[mip] != 0
				? "MissingCoverage: generation scan is pending completion."
				: "MissingCoverage: generation scan is awaiting admission.";
		}
	}

	void NonFiniteWatchCoordinator::PublishBoundary(Pending pending) noexcept
	{
		if (pending.m_FrameSerial < m_Status.m_BoundaryFrameSerial)
		{
			pending.m_FrameSerial = m_Status.m_BoundaryFrameSerial;
			pending.m_Targets = m_Status.m_Targets;
		}
		for (auto& target : pending.m_Targets)
		{
			ApplyGenerationEvidence(target, pending.m_FrameSerial);
		}
		NonFiniteClaimResult claim{ NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None };
		bool applicable = false;
		std::string failure;
		for (const auto& target : pending.m_Targets)
		{
			const auto current = target.m_Result.m_Claim;
			applicable |= current.m_Status != NonFiniteClaimStatus::NotApplicable;
			if (current.m_Status == NonFiniteClaimStatus::Failed)
			{
				claim = current;
			}
			else if (claim.m_Status != NonFiniteClaimStatus::Failed && current.m_Status != NonFiniteClaimStatus::Passed &&
				current.m_Status != NonFiniteClaimStatus::NotApplicable)
			{
				claim = {};
			}
			if (failure.empty() && !target.m_Failure.empty())
			{
				failure = target.m_Semantic ? std::string(GetNonFiniteWatchSemanticName(*target.m_Semantic)) + ": " : "";
				failure += target.m_Failure;
			}
		}
		if (!applicable)
		{
			claim = { NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None };
		}
		m_Status.m_BoundaryFrameSerial = pending.m_FrameSerial;
		m_Status.m_Boundary = claim;
		m_Status.m_Targets = std::move(pending.m_Targets);
		m_Status.m_Failure = std::move(failure);
	}

	void NonFiniteWatchCoordinator::AccumulateSites(const NonFiniteProductionFrame& frame) noexcept
	{
		++m_Status.m_ProductionFrames;
		for (const auto& site : frame.m_Sites)
		{
			const auto& counts = site.m_Counters;
			if (counts.m_NonFiniteReplacements == 0 && counts.m_NegativeClamps == 0 && counts.m_Fp16MaximumClamps == 0)
			{
				continue;
			}
			auto found = std::ranges::find_if(m_Status.m_SiteTotals, [&site](const NonFiniteWatchSiteTotals& totals)
				{
					return totals.m_ProgramIndex == site.m_ProgramIndex && totals.m_SiteIndex == site.m_SiteIndex;
				});
			if (found == m_Status.m_SiteTotals.end())
			{
				found = m_Status.m_SiteTotals.insert(m_Status.m_SiteTotals.end(), { .m_ProgramIndex = site.m_ProgramIndex,
					.m_SiteIndex = site.m_SiteIndex, .m_FirstFrameSerial = frame.m_FrameSerial });
			}
			auto& totals = *found;
			++totals.m_Frames;
			totals.m_FailedFrames += site.m_Claim.m_Status == NonFiniteClaimStatus::Failed ? 1 : 0;
			totals.m_FirstFrameSerial = std::min(totals.m_FirstFrameSerial, frame.m_FrameSerial);
			totals.m_Counters.m_NonFiniteReplacements += counts.m_NonFiniteReplacements;
			totals.m_Counters.m_NegativeClamps += counts.m_NegativeClamps;
			totals.m_Counters.m_Fp16MaximumClamps += counts.m_Fp16MaximumClamps;
			if (counts.m_MostNegative)
			{
				totals.m_Counters.m_MostNegative = std::min(totals.m_Counters.m_MostNegative.value_or(0.0f), *counts.m_MostNegative);
			}
			if (counts.m_Maximum)
			{
				totals.m_Counters.m_Maximum = std::max(totals.m_Counters.m_Maximum.value_or(0.0f), *counts.m_Maximum);
			}
		}
	}

	void NonFiniteWatchCoordinator::AccumulateTarget(const NonFiniteWatchTargetStatus& target, uint64_t frameSerial) noexcept
	{
		const auto status = target.m_Result.m_Claim.m_Status;
		if (status != NonFiniteClaimStatus::Passed && status != NonFiniteClaimStatus::Failed)
		{
			return;
		}
		auto found = std::ranges::find_if(m_Status.m_TargetTotals, [&target](const NonFiniteWatchTargetTotals& totals)
			{
				return totals.m_Semantic == target.m_Semantic && SameAddress(totals.m_Address, target.m_Address);
			});
		if (found == m_Status.m_TargetTotals.end())
		{
			found = m_Status.m_TargetTotals.insert(m_Status.m_TargetTotals.end(),
				{ .m_Semantic = target.m_Semantic, .m_Address = target.m_Address });
		}
		auto& totals = *found;
		++totals.m_CheckedFrames;
		if (status == NonFiniteClaimStatus::Failed)
		{
			++totals.m_FailedFrames;
			totals.m_FirstFailedFrameSerial = totals.m_FirstFailedFrameSerial == 0
				? frameSerial : std::min(totals.m_FirstFailedFrameSerial, frameSerial);
		}
		totals.m_NanCount += target.m_Record.m_NanCount;
		totals.m_PositiveInfCount += target.m_Record.m_PositiveInfCount;
		totals.m_NegativeInfCount += target.m_Record.m_NegativeInfCount;
		if (const auto minimum = target.m_Result.m_Minimum; minimum)
		{
			totals.m_Minimum = totals.m_Minimum ? std::min(*totals.m_Minimum, *minimum) : *minimum;
		}
		if (const auto maximum = target.m_Result.m_Maximum; maximum)
		{
			totals.m_Maximum = totals.m_Maximum ? std::max(*totals.m_Maximum, *maximum) : *maximum;
		}
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
		Pending pending = std::move(found->second);
		m_Pending.erase(found);
		if (!m_Status.m_Enabled || pending.m_ConfigurationVersion != m_Status.m_ConfigurationVersion)
		{
			return true;
		}
		const auto& payload = result.m_NonFiniteScans;
		const bool addressesMatch = payload && payload->m_Boundaries.size() == pending.m_Scans.size() &&
			std::ranges::equal(payload->m_Boundaries, pending.m_Scans, [](const auto& boundary, const auto& target)
				{
					return SameAddress(boundary.m_Target.m_Address, target.m_Address);
				});
		if (result.m_Status == FrameCaptureStatus::Completed && result.m_FrameSerial == pending.m_FrameSerial && payload &&
			payload->m_ConfigurationVersion == pending.m_ConfigurationVersion && addressesMatch)
		{
			for (size_t i = 0; i < payload->m_Boundaries.size(); ++i)
			{
				const auto& boundary = payload->m_Boundaries[i];
				auto& target = pending.m_Targets[pending.m_ScanIndices[i]];
				target.m_Record = boundary.m_Record;
				target.m_Result = boundary.m_Result;
				target.m_CheckedFrameSerial = pending.m_FrameSerial;
				target.m_Failure.clear();
				if (target.m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Incomplete)
				{
					target.m_Failure = "MissingCoverage: scan evidence is incomplete.";
				}
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
				AccumulateTarget(target, pending.m_FrameSerial);
				if (target.m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Failed)
				{
					RecordAnomaly({ .m_FrameSerial = pending.m_FrameSerial, .m_Failure = target.m_Result.m_Claim.m_Failure, .m_Target = target });
				}
			}
		}
		else
		{
			for (const size_t index : pending.m_ScanIndices)
			{
				pending.m_Targets[index].m_Failure = result.m_Failure.empty()
					? "MissingCoverage: the watch frame returned missing or mismatched scan evidence." : result.m_Failure;
			}
		}
		for (const size_t index : pending.m_ScanIndices)
		{
			const auto& target = pending.m_Targets[index];
			if (!target.m_Generation.IsValid() || !target.m_Semantic)
			{
				continue;
			}
			auto& generation = m_Generations[static_cast<size_t>(*target.m_Semantic)];
			const size_t mip = target.m_Address.m_Mip;
			if (generation.m_Generation == target.m_Generation && mip < generation.m_InFlight.size() &&
				generation.m_InFlight[mip] == result.m_RequestId)
			{
				generation.m_InFlight[mip] = 0;
				if (target.m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Passed || target.m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Failed)
				{
					generation.m_Results[mip] = target;
				}
			}
		}
		if (result.m_Status == FrameCaptureStatus::Busy && !pending.m_Submitted)
		{
			// Admission rolled back the entire frame. It contributes no coverage gap
			// or fail-mode history; selected generation mips are now retryable.
			return true;
		}
		ObserveCoverageBoundary(pending);
		PublishBoundary(std::move(pending));
		RefreshCoverage();
		return true;
	}

	void NonFiniteWatchCoordinator::OnProductionFrame(const NonFiniteProductionFrame& frame) noexcept
	{
		if (!m_Status.m_Enabled || !m_Configuration.m_Production || frame.m_FrameSerial < m_FirstApplicableFrame)
		{
			return;
		}
		// Evidence must carry exactly the configured injection request. Otherwise the
		// frame cannot say whether its anomalies were injected, so it proves nothing.
		const std::optional<NonFiniteInjection> evidenceRequest =
			frame.m_Injection ? std::optional(frame.m_Injection->m_Request) : std::nullopt;
		const bool injectionMatches = evidenceRequest == m_Configuration.m_Injection;
		const NonFiniteClaimResult claim = injectionMatches ? frame.m_Claim : NonFiniteClaimResult{};
		const std::string failure = injectionMatches ? frame.m_Failure
			: std::string("MissingCoverage: production evidence carries a different injection request than the watch configuration.");
		const bool injectionApplied = injectionMatches && frame.m_Injection && frame.m_Injection->m_AppliedCount != 0;
		if (injectionMatches && frame.m_Injection && frame.m_FrameSerial >= m_Status.m_InjectionFrameSerial)
		{
			m_Status.m_InjectionFrameSerial = frame.m_FrameSerial;
			m_Status.m_InjectionAppliedCount = frame.m_Injection->m_AppliedCount;
		}
		if (claim.m_Status == NonFiniteClaimStatus::Passed || claim.m_Status == NonFiniteClaimStatus::Failed)
		{
			AccumulateSites(frame);
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
			m_Status.m_Production = claim;
			m_Status.m_ProductionFailure = failure;
		}
		for (auto& [id, coverage] : m_Coverage)
		{
			for (auto& pending : coverage.m_Frames)
			{
				if (pending.m_FrameSerial == frame.m_FrameSerial && pending.m_ProductionPending)
				{
					pending.m_ProductionPending = false;
					pending.m_Production = claim;
					pending.m_Failure = failure;
					if (injectionMatches && frame.m_Injection)
					{
						++coverage.m_Report.m_InjectedFrames;
						coverage.m_Report.m_AppliedFrames += injectionApplied ? 1 : 0;
					}
					for (const auto& site : frame.m_Sites)
					{
						if (site.m_Claim.m_Status == NonFiniteClaimStatus::Failed && !pending.m_Anomaly)
						{
							pending.m_Anomaly = NonFiniteWatchAnomaly{ .m_FrameSerial = frame.m_FrameSerial,
								.m_Failure = site.m_Claim.m_Failure, .m_ProgramIndex = site.m_ProgramIndex, .m_SiteIndex = site.m_SiteIndex };
						}
					}
				}
			}
		}
		RefreshCoverage();
	}

	uint64_t NonFiniteWatchCoordinator::OpenCoverage() noexcept
	{
		if (!m_Status.m_Enabled)
		{
			return 0;
		}
		const uint64_t id = m_NextCoverageId++;
		auto& report = m_Coverage[id].m_Report;
		report.m_ConfigurationVersion = m_Status.m_ConfigurationVersion;
		report.m_Mode = m_Configuration.m_Mode;
		report.m_BoundaryRequested = m_Configuration.m_Boundary;
		report.m_ProductionRequested = m_Configuration.m_Production;
		report.m_InstrumentationAvailable = m_Capture && m_Capture->HasNonFiniteInstrumentation();
		report.m_Injection = m_Status.m_Injection;
		return id;
	}

	void NonFiniteWatchCoordinator::TrackCoverageFrame(uint64_t id, uint64_t frameSerial) noexcept
	{
		const auto found = m_Coverage.find(id);
		if (found == m_Coverage.end() || found->second.m_Closed)
		{
			return;
		}
		auto& coverage = found->second;
		auto& report = coverage.m_Report;
		if (frameSerial == 0 || frameSerial <= report.m_LastFrameSerial || frameSerial != m_LastSubmittedFrame)
		{
			report.m_Failure = "MissingCoverage: range frame does not match a new successful submission.";
			return;
		}
		if (report.m_FrameCount++ == 0)
		{
			report.m_FirstFrameSerial = frameSerial;
		}
		report.m_LastFrameSerial = frameSerial;
		CoverageFrame frame{ .m_FrameSerial = frameSerial, .m_SubmissionOrdinal = m_SubmissionOrdinal };
		if (report.m_BoundaryRequested)
		{
			if (m_PreparedCoverage && m_PreparedCoverage->m_FrameSerial == frameSerial)
			{
				frame = *m_PreparedCoverage;
				frame.m_SubmissionOrdinal = m_SubmissionOrdinal;
			}
			else
			{
				frame.m_Targets.emplace_back();
			}
		}
		frame.m_ProductionPending = report.m_ProductionRequested && report.m_InstrumentationAvailable;
		frame.m_TrackedAt = std::chrono::steady_clock::now();
		frame.m_Production = report.m_InstrumentationAvailable ? NonFiniteClaimResult{}
		: NonFiniteClaimResult{ NonFiniteClaimStatus::Unavailable, NonFiniteWatchFailure::MissingCoverage };
		coverage.m_Frames.push_back(std::move(frame));
		RefreshCoverage();
	}

	void NonFiniteWatchCoordinator::CloseCoverage(uint64_t id) noexcept
	{
		if (const auto found = m_Coverage.find(id); found != m_Coverage.end())
		{
			found->second.m_Closed = true;
			RefreshCoverage();
		}
	}
	void NonFiniteWatchCoordinator::ReleaseCoverage(uint64_t id) noexcept { m_Coverage.erase(id); }
	void NonFiniteWatchCoordinator::UpdateCoverage() noexcept { RefreshCoverage(); }
	std::shared_ptr<const NonFiniteWatchCoverage> NonFiniteWatchCoordinator::GetCoverage(uint64_t id) const noexcept
	{
		const auto found = m_Coverage.find(id);
		return found == m_Coverage.end() ? nullptr : std::make_shared<const NonFiniteWatchCoverage>(found->second.m_Report);
	}
	void NonFiniteWatchCoordinator::OnFrameSubmitted(uint64_t frameSerial) noexcept
	{
		if (frameSerial > m_LastSubmittedFrame)
		{
			m_LastSubmittedFrame = frameSerial;
			++m_SubmissionOrdinal;
			for (auto& [id, pending] : m_Pending)
			{
				if (pending.m_FrameSerial == frameSerial)
				{
					pending.m_Submitted = true;
				}
			}
			RefreshCoverage();
		}
	}
	void NonFiniteWatchCoordinator::FinalizeCoverage() noexcept
	{
		m_SubmissionOrdinal += MaxPendingCoverageSubmissions;
		for (auto& [id, coverage] : m_Coverage)
		{
			coverage.m_Closed = true;
		}
		RefreshCoverage();
	}
	std::string NonFiniteWatchCoordinator::GetFailModeFailure() const noexcept
	{
		if (!m_Status.m_Enabled || m_Status.m_Mode != NonFiniteWatchMode::Fail)
		{
			return {};
		}
		if (m_Status.m_FirstAnomaly)
		{
			return Describe(*m_Status.m_FirstAnomaly, m_Status.m_Injection);
		}
		for (const auto& target : m_Status.m_Targets)
		{
			if (m_Status.m_BoundaryFrameSerial != 0 && !target.m_Generation.IsValid() && target.m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Incomplete)
			{
				return std::format("MissingCoverage: frame {}, target '{}': {}", m_Status.m_BoundaryFrameSerial,
					target.m_Semantic ? GetNonFiniteWatchSemanticName(*target.m_Semantic) : target.m_Address.m_Resource, target.m_Failure);
			}
		}
		if (m_Status.m_ProductionFrameSerial != 0 && !Clean(m_Status.m_Production))
		{
			return std::format("MissingCoverage: frame {}, production: {}", m_Status.m_ProductionFrameSerial, m_Status.m_ProductionFailure);
		}
		return {};
	}

	void NonFiniteWatchCoordinator::ObserveCoverageBoundary(const Pending& pending) noexcept
	{
		for (auto& [id, coverage] : m_Coverage)
		{
			for (auto& frame : coverage.m_Frames)
			{
				if (frame.m_FrameSerial == pending.m_FrameSerial)
				{
					frame.m_BoundaryPending = false;
				}
				for (size_t index = 0; index < frame.m_Targets.size(); ++index)
				{
					auto& target = frame.m_Targets[index];
					if (!target.m_Generation.IsValid())
					{
						// Default roles may share an empty address, and explicit requests
						// may interpret the same buffer differently. Preserve row identity.
						if (frame.m_FrameSerial == pending.m_FrameSerial && index < pending.m_Targets.size())
						{
							const auto& evidence = pending.m_Targets[index];
							if (target.m_Semantic == evidence.m_Semantic && SameAddress(target.m_Address, evidence.m_Address))
							{
								target = evidence;
							}
						}
						continue;
					}
					for (const auto& evidence : pending.m_Targets)
					{
						const bool generation = target.m_Generation.IsValid() && target.m_Generation == evidence.m_Generation &&
							target.m_Semantic == evidence.m_Semantic && target.m_Address.m_Mip == evidence.m_Address.m_Mip;
						if (generation && (Clean(evidence.m_Result.m_Claim) || evidence.m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Failed))
						{
							target = evidence;
						}
					}
				}
			}
		}
	}

	void NonFiniteWatchCoordinator::RefreshCoverage() noexcept
	{
		for (auto& [id, coverage] : m_Coverage)
		{
			auto& report = coverage.m_Report;
			std::erase_if(coverage.m_Frames, [&](CoverageFrame& frame)
				{
					bool pending = frame.m_BoundaryPending || frame.m_ProductionPending;
					NonFiniteClaimResult boundary{ NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None };
					for (auto& target : frame.m_Targets)
					{
						ApplyGenerationEvidence(target, frame.m_FrameSerial);
						pending |= target.m_Generation.IsValid() && !Clean(target.m_Result.m_Claim) &&
							target.m_Result.m_Claim.m_Status != NonFiniteClaimStatus::Failed;
						Merge(boundary, target.m_Result.m_Claim);
						if (target.m_Result.m_Claim.m_Status == NonFiniteClaimStatus::Failed && !frame.m_Anomaly)
						{
							frame.m_Anomaly = NonFiniteWatchAnomaly{ .m_FrameSerial = target.m_CheckedFrameSerial ? target.m_CheckedFrameSerial : frame.m_FrameSerial,
								.m_Failure = target.m_Result.m_Claim.m_Failure, .m_Target = target };
						}
					}
					if (frame.m_Anomaly && (!report.m_FirstAnomaly || frame.m_Anomaly->m_FrameSerial < report.m_FirstAnomaly->m_FrameSerial))
					{
						report.m_FirstAnomaly = frame.m_Anomaly;
						report.m_Failure = Describe(*frame.m_Anomaly, report.m_Injection);
					}
					// A stalled/deferred renderer may submit no tail frames at all. The
					// wall-clock bound also releases retained capture payloads in that case.
					if (pending && m_SubmissionOrdinal - frame.m_SubmissionOrdinal < MaxPendingCoverageSubmissions &&
						std::chrono::steady_clock::now() - frame.m_TrackedAt < std::chrono::seconds(30))
					{
						return false;
					}
					if (frame.m_BoundaryPending)
					{
						Merge(boundary, {});
					}
					if (report.m_BoundaryRequested)
					{
						Merge(coverage.m_Boundary, boundary);
					}
					if (report.m_ProductionRequested)
					{
						Merge(coverage.m_Production, frame.m_Production);
					}
					if (report.m_Failure.empty() && ((report.m_BoundaryRequested && !Clean(boundary)) ||
						(report.m_ProductionRequested && !Clean(frame.m_Production))))
					{
						if (frame.m_Failure.empty())
						{
							for (const auto& target : frame.m_Targets)
							{
								if (!Clean(target.m_Result.m_Claim))
								{
									frame.m_Failure = target.m_Semantic ? std::string(GetNonFiniteWatchSemanticName(*target.m_Semantic)) : target.m_Address.m_Resource;
									frame.m_Failure += ": " + (target.m_Failure.empty() ? "no complete scan result" : target.m_Failure);
									break;
								}
							}
						}
						report.m_Failure = std::format("MissingCoverage: frame {}: {}", frame.m_FrameSerial,
							frame.m_Failure.empty() ? "not every applicable target and site returned complete evidence" : frame.m_Failure);
					}
					++report.m_CompletedFrames;
					return true;
				});
			report.m_Boundary = report.m_BoundaryRequested ? coverage.m_Boundary
				: NonFiniteClaimResult{ NonFiniteClaimStatus::Unavailable, NonFiniteWatchFailure::None };
			report.m_Production = report.m_ProductionRequested ? coverage.m_Production
				: NonFiniteClaimResult{ NonFiniteClaimStatus::Unavailable, NonFiniteWatchFailure::None };
			if (!coverage.m_Frames.empty())
			{
				if (report.m_BoundaryRequested)
				{
					Merge(report.m_Boundary, {});
				}
				if (report.m_ProductionRequested)
				{
					Merge(report.m_Production, {});
				}
			}
			report.m_Complete = coverage.m_Closed && coverage.m_Frames.empty();
		}
	}
}
