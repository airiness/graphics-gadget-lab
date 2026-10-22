#include "ParameterTransactionSelfTests.h"
#include "Lab/LabParameter.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Parameters/ParameterEffectReceipt.h"
#include "GGLabTestCore/SelfTest.h"

#include <array>
#include <limits>
#include <string>
#include <utility>

namespace gglab
{
	namespace
	{
		LabParameterSet MakeParameters(SelfTestContext& context) noexcept
		{
			LabParameterSet parameters;
			context.Check(parameters.Add({ .m_Id = LabParameterId("radius"), .m_Name = "Radius",
				.m_Type = LabParameterType::Float, .m_DefaultValue = 1.0f,
				.m_MinValue = LabValue(0.0f), .m_MaxValue = LabValue(4.0f) }), "Register radius");
			context.Check(parameters.Add({ .m_Id = LabParameterId("layout"), .m_Name = "Layout",
				.m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::RestartSession,
				.m_DefaultValue = uint32_t(2), .m_MinValue = LabValue(uint32_t(1)),
				.m_MaxValue = LabValue(uint32_t(8)) }), "Register layout");
			context.Check(parameters.Add({ .m_Id = LabParameterId("enabled"), .m_Name = "Enabled",
				.m_Type = LabParameterType::Bool, .m_DefaultValue = false }), "Register toggle");
			context.Check(parameters.Add({ .m_Id = LabParameterId("mode"), .m_Name = "Mode",
				.m_Type = LabParameterType::Enum, .m_DefaultValue = int32_t(0),
				.m_EnumItems = { { 0, "Off" }, { 2, "On" } } }), "Register sparse enum");
			context.Check(parameters.Add({ .m_Id = LabParameterId("vector"), .m_Name = "Vector",
				.m_Type = LabParameterType::Vector3, .m_DefaultValue = Vector3(0.0f),
				.m_MinValue = LabValue(Vector3(-1.0f)), .m_MaxValue = LabValue(Vector3(1.0f)) }),
				"Register component ranges");
			context.Check(parameters.Add({ .m_Id = LabParameterId("color"), .m_Name = "Color",
				.m_Type = LabParameterType::Color, .m_DefaultValue = Color(0.5f) }), "Register color");
			context.Check(parameters.Add({ .m_Id = LabParameterId("offset"), .m_Name = "Offset",
				.m_Type = LabParameterType::Int, .m_DefaultValue = int32_t(0),
				.m_MinValue = LabValue(int32_t(-2)), .m_MaxValue = LabValue(int32_t(2)) }),
				"Register signed range");
			return parameters;
		}

		void RunBatchTests(SelfTestContext& context) noexcept
		{
			LabParameterSet parameters = MakeParameters(context);
			const auto rejected = [&](LabParameterValue invalid)
				{
					const std::array batch
					{
						LabParameterValue{ LabParameterId("radius"), 2.0f },
						std::move(invalid),
					};
					std::string error;
					context.Check(!parameters.ApplyBatch(batch, error) && !error.empty(),
						"An invalid tail rejects the whole typed batch");
					context.Check(parameters.Get(LabParameterId("radius"), 0.0f) == 1.0f &&
						!parameters.Find(LabParameterId("radius"))->m_Dirty,
						"A rejected batch changes neither an earlier valid value nor its dirty bit");
				};
			rejected({ LabParameterId("missing"), false });
			rejected({ LabParameterId("enabled"), int32_t(1) });
			rejected({ LabParameterId("layout"), uint32_t(9) });
			rejected({ LabParameterId("layout"), uint32_t(0) });
			rejected({ LabParameterId("layout"), int32_t(2) });
			rejected({ LabParameterId("offset"), int32_t(-3) });
			rejected({ LabParameterId("mode"), int32_t(1) });
			rejected({ LabParameterId("vector"), Vector3(0.0f, 2.0f, 0.0f) });
			rejected({ LabParameterId("vector"), Vector3(-2.0f, 0.0f, 0.0f) });
			rejected({ LabParameterId("vector"), Vector3(0.0f, 0.0f,
				std::numeric_limits<float>::infinity()) });
			rejected({ LabParameterId("color"), Color(0.5f, -0.1f, 0.5f, 1.0f) });
			rejected({ LabParameterId("color"), Color(0.5f, 0.5f, 0.5f, 1.1f) });
			rejected({ LabParameterId("color"), Color(std::numeric_limits<float>::quiet_NaN()) });
			rejected({ LabParameterId("radius"), 3.0f });

			std::string error;
			const auto rejectedRadius = [&](float value)
				{
					const std::array batch{ LabParameterValue{ LabParameterId("radius"), value } };
					context.Check(!parameters.ApplyBatch(batch, error) &&
						parameters.Get(LabParameterId("radius"), 0.0f) == 1.0f,
						"Float batches reject non-finite and out-of-range values without clamping");
				};
			rejectedRadius(std::numeric_limits<float>::quiet_NaN());
			rejectedRadius(std::numeric_limits<float>::infinity());
			rejectedRadius(-std::numeric_limits<float>::infinity());
			rejectedRadius(-1.0f);
			rejectedRadius(4.1f);
			context.Check(!parameters.ApplyBatch({}, error), "An empty batch is not an accepted transaction");

			const std::array batch
			{
				LabParameterValue{ LabParameterId("radius"), 4.0f },
				LabParameterValue{ LabParameterId("layout"), uint32_t(2) },
				LabParameterValue{ LabParameterId("vector"), Vector3(-1.0f, 0.0f, 1.0f) },
				LabParameterValue{ LabParameterId("color"), Color(0.0f, 1.0f, 0.5f, 1.0f) },
				LabParameterValue{ LabParameterId("offset"), int32_t(-2) },
				LabParameterValue{ LabParameterId("enabled"), true },
				LabParameterValue{ LabParameterId("mode"), int32_t(2) },
			};
			const auto applied = parameters.ApplyBatch(batch, error);
			context.Check(applied && error.empty() && applied->m_Changed.size() == 6 &&
				applied->m_Impact == LabChangeImpact::Immediate &&
				!parameters.Find(LabParameterId("layout"))->m_Dirty,
				"A valid mixed batch applies exact boundary values without rebuilding unchanged layout");
			context.Check(parameters.Get(LabParameterId("radius"), 0.0f) == 4.0f &&
				parameters.Get(LabParameterId("enabled"), false) &&
				parameters.Get(LabParameterId("mode"), int32_t(-1)) == 2 &&
				LabValuesEqual(parameters.Find(LabParameterId("vector"))->m_Value, batch[2].m_Value) &&
				LabValuesEqual(parameters.Find(LabParameterId("color"))->m_Value, batch[3].m_Value),
				"Every typed item retains exactly the accepted value");
			const auto repeated = parameters.ApplyBatch(batch, error);
			context.Check(repeated && repeated->m_Changed.empty() &&
				repeated->m_Impact == LabChangeImpact::Immediate,
				"Reapplying the same batch is NoChange, including vectors and colors");
			const std::array rebuild{ LabParameterValue{ LabParameterId("layout"), uint32_t(8) } };
			const auto rebuilt = parameters.ApplyBatch(rebuild, error);
			context.Check(rebuilt && rebuilt->m_Impact == LabChangeImpact::RestartSession &&
				rebuilt->m_Changed.size() == 1, "Changed layout preserves its owning Lab rebuild impact");
			context.Check(parameters.Set(LabParameterId("radius"), 9.0f) &&
				parameters.Get(LabParameterId("radius"), 0.0f) == 4.0f,
				"Existing interactive edits retain their clamping policy");
			const auto& layout = parameters.Find(LabParameterId("layout"))->m_Desc;
			context.Check(GetParameterEffectClass(layout) == ParameterEffectClass::Rebuild,
				"Rebuild impact cannot inherit an Immediate effect declaration");
			LabParameterDesc deferred{ .m_EffectClass = ParameterEffectClass::Deferred };
			context.Check(GetParameterEffectClass(deferred) == ParameterEffectClass::Deferred,
				"An Immediate change impact may explicitly declare a deferred domain effect");
			LabParameterDesc colorRange{ .m_Type = LabParameterType::Color,
				.m_MinValue = LabValue(Color(0.25f)), .m_MaxValue = LabValue(Color(0.75f)) };
			context.Check(!LabParameterSet::ValidateValue(colorRange, Color(0.1f), error) &&
				LabParameterSet::ValidateValue(colorRange, Color(0.25f), error),
				"Color validation respects declared component ranges as well as normalized storage");
		}

		void RunInheritedParameterTests(SelfTestContext& context) noexcept
		{
			const LabParameterId id("samples");
			const auto make = [&]()
				{
					LabParameterSet parameters;
					context.Check(parameters.Add({ .m_Id = id, .m_Name = "Samples",
						.m_Type = LabParameterType::UInt, .m_DefaultValue = uint32_t(512),
						.m_InheritOnEnter = true, .m_MinValue = LabValue(uint32_t(1)),
						.m_MaxValue = LabValue(uint32_t(4096)) }), "Register an entered inherited parameter");
					return parameters;
				};
			auto inherited = make();
			context.Check(inherited.BindInheritedDefault(id, uint32_t(64)) &&
				inherited.Get(id, uint32_t(0)) == 64 && !inherited.Find(id)->m_Dirty &&
				inherited.CaptureRestoredValues().empty(),
				"Entry binds the live baseline without converting inheritance into a restored override");
			context.Check(!inherited.BindInheritedDefault(id, uint32_t(0)) &&
				!inherited.BindInheritedDefault(LabParameterId("missing"), uint32_t(64)) &&
				inherited.Get(id, uint32_t(0)) == 64,
				"An invalid entered baseline is rejected without modifying the parameter");
			auto explicitValue = make();
			context.Check(explicitValue.Set(id, uint32_t(512)) &&
				explicitValue.BindInheritedDefault(id, uint32_t(64)) &&
				explicitValue.Get(id, uint32_t(0)) == 512 &&
				std::get<uint32_t>(explicitValue.Find(id)->m_Desc.m_DefaultValue) == 64 &&
				explicitValue.CaptureRestoredValues().size() == 1,
				"An explicit startup value equal to the placeholder survives a different entered baseline");
			context.Check(explicitValue.FollowInheritedValue(id, uint32_t(512)) &&
				!explicitValue.Find(id)->m_Dirty && explicitValue.CaptureRestoredValues().empty() &&
				!explicitValue.FollowInheritedValue(id, uint32_t(512)),
				"Even a same-value external writer releases explicit intent exactly once");
			context.Check(explicitValue.FollowInheritedValue(id, uint32_t(128)) &&
				explicitValue.Get(id, uint32_t(0)) == 128 &&
				std::get<uint32_t>(explicitValue.Find(id)->m_Desc.m_DefaultValue) == 64,
				"Following an external writer updates current value while preserving the entered reset default");
			GGLAB_UNUSED(explicitValue.ResetAll());
			context.Check(explicitValue.Get(id, uint32_t(0)) == 64 &&
				explicitValue.Find(id)->m_Dirty && explicitValue.CaptureRestoredValues().size() == 1,
				"Reset explicitly requests the entered baseline and survives restart");
			const auto ordinary = MakeParameters(context);
			context.Check(ordinary.CaptureRestoredValues().size() == ordinary.CaptureValues().size(),
				"Ordinary Lab parameters retain the existing restart snapshot policy");
		}

		ParameterEffectReceipt BeginReceipt(ParameterEffectClass effectClass) noexcept
		{
			return *ParameterEffectReceipt::Begin(LabParameterId("test.parameter"), effectClass, 7, 10);
		}

		void RunReceiptTests(SelfTestContext& context) noexcept
		{
			const ParameterStateIdentity immediate{ .m_Revision = 7 };
			const ParameterStateIdentity lighting{ 7, "lighting", 21 };
			context.Check(!ParameterEffectReceipt::Begin({}, ParameterEffectClass::Immediate, 7, 10) &&
				!ParameterEffectReceipt::Begin(LabParameterId("x"), ParameterEffectClass::Immediate, 0, 10) &&
				!ParameterEffectReceipt::Begin(LabParameterId("x"), ParameterEffectClass::Immediate, 7, 0),
				"Receipts require a parameter, non-zero batch revision and render frame serial");

			auto receipt = BeginReceipt(ParameterEffectClass::Deferred);
			context.Check(!receipt.RecordPublished(lighting, 10) &&
				!receipt.RecordSubmittedFrame(lighting, 10) &&
				!receipt.RecordApplied(ParameterEffectClass::Deferred, lighting, 9) &&
				!receipt.RecordApplied(ParameterEffectClass::Deferred, immediate, 10),
				"Deferred effects require ordered application with owning-domain generation evidence");
			context.Check(receipt.RecordApplied(ParameterEffectClass::Deferred, lighting, 11) &&
				!receipt.GetSnapshot().m_Published && !receipt.RecordSubmittedFrame(lighting, 12),
				"Applied does not mean published or consumed");
			for (const auto& wrong : std::array{
				ParameterStateIdentity{ 8, "lighting", 21 },
				ParameterStateIdentity{ 7, "other", 21 },
				ParameterStateIdentity{ 7, "lighting", 22 } })
			{
				context.Check(!receipt.RecordPublished(wrong, 12),
					"Publication rejects a different revision, domain or generation");
			}
			context.Check(!receipt.RecordPublished(lighting, 10) &&
				receipt.RecordPublished(lighting, 14) && receipt.RecordPublished(lighting, 15) &&
				receipt.GetSnapshot().m_Published == 14,
				"Publication records the first matching domain publication frame");
			for (const auto& wrong : std::array{
				ParameterStateIdentity{ 8, "lighting", 21 },
				ParameterStateIdentity{ 7, "other", 21 },
				ParameterStateIdentity{ 7, "lighting", 20 } })
			{
				context.Check(!receipt.RecordSubmittedFrame(wrong, 15) &&
					!receipt.GetSnapshot().m_FirstConsumed,
					"A later successful submission of another state never establishes firstConsumed");
			}
			// Frame 15 aborts. There is deliberately no successful-submission callback.
			context.Check(!receipt.RecordSubmittedFrame(lighting, 13) &&
				receipt.RecordApplied(ParameterEffectClass::Deferred, lighting, 16) &&
				receipt.GetSnapshot().m_Applied == 11 && !receipt.GetSnapshot().m_FirstConsumed,
				"An aborted consuming frame and repeated application preserve evidence without consumption");
			context.Check(receipt.RecordSubmittedFrame(lighting, 16) &&
				receipt.GetSnapshot().m_Requested == 10 && receipt.GetSnapshot().m_Applied == 11 &&
				receipt.GetSnapshot().m_Published == 14 && receipt.GetSnapshot().m_FirstConsumed == 16,
				"The first successfully submitted frame with the matching identity supplies t0");
			context.Check(!receipt.RecordSubmittedFrame(lighting, 17) && !receipt.Supersede() &&
				!receipt.Fail("late failure") && !receipt.Finish() &&
				receipt.GetSnapshot().m_FirstConsumed == 16,
				"Consumed evidence is terminal and cannot be rewritten by later reports");

			auto beforePublication = BeginReceipt(ParameterEffectClass::Deferred);
			context.Check(beforePublication.RecordApplied(ParameterEffectClass::Deferred, lighting, 10) &&
				beforePublication.Supersede() && !beforePublication.RecordPublished(lighting, 12) &&
				!beforePublication.GetSnapshot().m_Published &&
				!beforePublication.GetSnapshot().m_FirstConsumed,
				"Superseding an in-flight generation never invents publication or consumption");
			auto afterPublication = BeginReceipt(ParameterEffectClass::Immediate);
			context.Check(afterPublication.RecordApplied(ParameterEffectClass::Immediate, immediate, 10) &&
				afterPublication.GetSnapshot().m_Published == 10 && afterPublication.Supersede() &&
				!afterPublication.RecordSubmittedFrame(immediate, 11) &&
				afterPublication.GetSnapshot().m_Published == 10 &&
				!afterPublication.GetSnapshot().m_FirstConsumed,
				"An immediate value superseded after an aborted frame retains published but no firstConsumed");
			auto noChange = BeginReceipt(ParameterEffectClass::Deferred);
			context.Check(noChange.RecordNoChange(10) && noChange.GetSnapshot().IsTerminal() &&
				noChange.GetSnapshot().m_State == ParameterEffectState::NoChange &&
				!noChange.GetSnapshot().m_Published && !noChange.RecordApplied(
					ParameterEffectClass::Deferred, lighting, 10),
				"NoChange is terminal without starting another generation or publication");
			auto mismatch = BeginReceipt(ParameterEffectClass::Immediate);
			context.Check(!mismatch.RecordApplied(ParameterEffectClass::Deferred, lighting, 10) &&
				mismatch.GetSnapshot().m_State == ParameterEffectState::Failed &&
				mismatch.GetSnapshot().m_Failure == "EffectMismatch" &&
			mismatch.GetSnapshot().m_Applied == 10 && mismatch.GetSnapshot().m_Identity == lighting &&
			!mismatch.GetSnapshot().m_Published,
				"Actual deferred work contradicting an Immediate declaration fails explicitly");
			auto notReached = BeginReceipt(ParameterEffectClass::Deferred);
			context.Check(notReached.RecordApplied(ParameterEffectClass::Deferred, lighting, 10) &&
				notReached.Finish() && notReached.GetSnapshot().m_State == ParameterEffectState::EffectNotReached &&
				!notReached.RecordPublished(lighting, 12),
				"Finishing the measurement without matching consumption closes EffectNotReached");
			auto failed = BeginReceipt(ParameterEffectClass::Rebuild);
			context.Check(!failed.Fail("") && failed.Fail("The owning domain failed.") &&
				failed.GetSnapshot().m_State == ParameterEffectState::Failed && !failed.Supersede(),
				"Domain failure is explicit and terminal");
			auto rebuilt = BeginReceipt(ParameterEffectClass::Rebuild);
			const ParameterStateIdentity session{ 7, "session", 3 };
			context.Check(rebuilt.RecordApplied(ParameterEffectClass::Rebuild, session, 11) &&
				rebuilt.RecordPublished(session, 20) && rebuilt.RecordSubmittedFrame(session, 21),
				"Rebuild receipts require session publication before matching consumption");
		}
	}

	void RunParameterTransactionSelfTests(SelfTestContext& context) noexcept
	{
		RunBatchTests(context);
		RunInheritedParameterTests(context);
		RunReceiptTests(context);
	}
}
