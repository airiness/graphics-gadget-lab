"""Align immutable progressive-refresh evidence by GPU sample identity, not wait time.

Exported rays independently reconstruct the current sample at every required
receiver, using all nonzero cage corners. Historical receiver inversion and
illumination scaling remain explicitly conditional engineering attributions,
not historical Trace/Blend observations or independent transport certificates.
"""
import argparse
import json
import shutil
from pathlib import Path

import numpy as np

from AnalyzeGIFieldTransient import (details, geometry, lookup, receipt, save,
                                    trace_error, trace_receiver)
from ValidateGIFieldLightStep import (FLOOR, LUMA, invariant_inputs, load,
    progressive_hysteresis, read_group, require, validate_case)


def cohort(root):
    binaries = {r["file"]: r["sha256"] for r in load(root/"binaries.json")}
    product = {r["path"]: r["sha256"] for r in load(root/"source-identity.json")
               if r["path"].startswith(("Sources/", "Shaders/", "Projects/", "PropertySheets/"))}
    require(binaries and product, "Missing product cohort seal")
    return binaries, product


def compatible(historical, observed):
    keys = ("backend", "configuration", "fixture", "environment", "view", "tap",
            "feedback", "step", "oldLux", "newLux", "traceSeed", "progressiveRefresh")
    return (all(k in historical and k in observed and historical[k] == observed[k] for k in keys) and
            historical["progressiveRefresh"] and not historical["feedback"] and
            historical["tap"] == "gi" and observed.get("progressiveAttribution", False))


def matched_sample(historical_update, observed_update, previous, actual, endpoint,
                   hysteresis, ray_sample, observed_lux, historical_lux):
    require(historical_update == observed_update and 1 <= historical_update <= 0xffffffff,
            "Different GPU sampling updates cannot be aligned by sequence frame or wait")
    values = np.array([previous, actual, endpoint, hysteresis, ray_sample,
                       observed_lux, historical_lux], dtype=float)
    require(np.isfinite(values).all() and 0 <= hysteresis < 1 and
            observed_lux > 0 and historical_lux > 0, "Invalid matched sample or coefficient")
    derived = (actual-hysteresis*previous)/(1-hysteresis)
    scaled = ray_sample*historical_lux/observed_lux
    # Deliberately retain the existing 0.5% arithmetic normalization budget.
    error = abs(scaled-derived)/max(abs(derived), FLOOR)
    return dict(derivedHistoricalSampleE=float(derived), observedRaySampleE=float(ray_sample),
                observedLux=observed_lux, historicalLux=historical_lux,
                illuminationScale=float(historical_lux/observed_lux),
                scaledRaySampleE=float(scaled), relativeDifference=float(error), budget=.005,
                arithmeticPassed=bool(error <= .005),
                historyDeltaE=float(hysteresis*(previous-endpoint)),
                derivedSampleDeltaE=float((1-hysteresis)*(derived-endpoint)),
                raySampleDeltaE=float((1-hysteresis)*(scaled-endpoint)),
                predictedHistoricalE=float(hysteresis*previous+(1-hysteresis)*scaled),
                attribution="ConditionalFixedHistoricalQueryAndSingleBounceIlluminationScaling")


def replay_samples(initial, samples):
    """Conditional scalar replay: one measured old value, then only exported rays."""
    samples = np.asarray(samples, dtype=float)
    require(samples.ndim == 1 and len(samples) > 0 and np.isfinite(samples).all() and
            np.isfinite(initial), "A replay requires finite initial history and current samples")
    value = float(initial)
    result = []
    for update, sample in enumerate(samples, 1):
        h = progressive_hysteresis(update)
        value = h*value+(1-h)*sample
        result.append(float(value))
    return result


def compare(historical_root, observed_root, case_name, output, observed_case_name=None):
    require(cohort(historical_root) == cohort(observed_root), "Historical and observed product cohorts differ")
    historical_cases = load(historical_root/"cases.json")
    selected = [c for c in historical_cases if c["directory"] == case_name]
    require(len(selected) == 1, "One explicit historical case is required")
    case = selected[0]
    require(case.get("progressiveRefresh") and not case.get("progressiveAttribution") and
            not case.get("transientAttribution") and not case["feedback"] and case["tap"] == "gi" and
            case["fixture"] == 4 and case["environment"] == 3 and
            case["step"] in ("small-increase", "small-decrease"),
            "Historical comparison requires the bounded uninstrumented progressive recipe")
    observed_cases = [c for c in load(observed_root/"cases.json") if compatible(case, c) and
                      (observed_case_name is None or c["directory"] == observed_case_name)]
    require(observed_cases, "No compatible, explicitly instrumented sampling streams")
    inputs = []
    for root, cases in ((historical_root, [case]), (observed_root, observed_cases)):
        inputs += [root/n for n in ("cases.json", "source-identity.json", "binaries.json", "result.json")]
        for c in cases:
            inputs += [p for p in (root/c["directory"]).rglob("*") if p.suffix in (".json", ".npy")]
    inputs = sorted(set(inputs))
    before = [receipt(p) for p in inputs]
    code = sorted(Path(__file__).parent.glob("*.py"))+[Path(__file__).with_name("GIFieldLightStepRecipes.json")]
    code_before = [receipt(p) for p in code]
    historical = validate_case(historical_root, case)
    first, last = historical["t0"], historical["firstFieldReady"]
    updates = {historical["globalUpdates"][i]: i for i in range(first, last)}
    historical_paths = sorted((historical_root/case_name/"samples").glob("*.resource-export.json"))
    historical_frames = {read_group(p)["frame"]["sequence"]["frame"]: read_group(p)["frame"]
                         for p in historical_paths}
    mask_path = next((historical_root/case_name/"mask").glob("*.resource-export.json"))
    mask_record = read_group(mask_path)
    mask_target = mask_record["targets"][0]
    position = np.load(mask_path.parent/mask_target["array"], allow_pickle=False)
    comparisons = []
    for observed_case in observed_cases:
        observed = validate_case(observed_root, observed_case)
        directory = observed_root/observed_case["directory"]
        g = geometry(directory/"mask")
        require(np.array_equal(position[0, g["union"], :3], g["position"]),
                "Historical and observed receiver positions differ")
        _, _, baseline, _ = details(next(p for p in (directory/"samples").glob("*.resource-export.json")
            if read_group(p)["frame"]["sequence"]["frame"] == 359), g, observed_case)
        for path in sorted((directory/"samples").glob("*.resource-export.json")):
            record = read_group(path)
            i = record["frame"]["sequence"]["frame"]
            update = observed["globalUpdates"][i]
            if not (observed_case.get("attributionStartFrame", 29) <= i <= 80 and update in updates):
                continue
            j = updates[update]
            require(invariant_inputs(record["frame"]) == invariant_inputs(historical_frames[j]),
                    "Matched sampling streams have different effective inputs")
            _, arrays, current, closure = details(path, g, observed_case)
            require(np.array_equal(current["weights"], baseline["weights"]) and
                    np.array_equal(current["support"], baseline["support"]),
                    "Observed query weights/support changed; scalar historical inversion is unproven")
            raw, probes = trace_receiver(g, arrays, update, case["traceSeed"], current)
            previous = lookup(g, arrays["Previous"], arrays["Distance"], arrays["Classification"], g["atlasSlices"])
            field = record["frame"]["readiness"]["giField"]
            epoch_update = update-field["summary"]["field"]["epochOriginUpdate"]
            if i < observed["t0"]:
                require(field["summary"]["field"]["refreshEpoch"] == 1 and
                        field["summary"]["field"]["epochOriginUpdate"] == 0,
                        "Unproven pre-event history mass")
                decay = float(np.float32(.998))
                h = 1-(1-decay)/(1-decay**epoch_update)
            else:
                h = progressive_hysteresis(epoch_update)
            blend = trace_error(current["energy"], h*previous["energy"]+(1-h)*raw)
            lux = record["frame"]["sceneInputs"]["sun"]["toaIlluminanceLux"]
            rows = {}
            for roi, mask in g["masks"].items():
                series = historical["roiSeries"][roi]
                rows[roi] = matched_sample(update, historical["globalUpdates"][j], series[j-1], series[j],
                    historical["rois"][roi]["new"], progressive_hysteresis(j-first+1),
                    float((raw[mask] @ LUMA).mean()), lux, case["newLux"])
            comparisons.append(dict(historicalFrame=j, observedFrame=i, globalUpdate=update,
                observedCase=observed_case["directory"], contributingProbes=probes,
                receiverClosure=closure, rayBlendClosure=blend, rois=rows))
    require(comparisons, "No exact GPU-update overlap in the bounded export window")
    peaks = []
    for roi, metrics in historical["rois"].items():
        if metrics["targetsPassed"]:
            continue
        series = historical["roiSeries"][roi]
        sign = np.sign(metrics["new"]-metrics["old"])
        peak = first+int(np.argmax(sign*(np.asarray(series[first:])-metrics["new"])))
        matches = [r for r in comparisons if r["historicalFrame"] == peak]
        covered = sorted({r["historicalFrame"] for r in comparisons if first <= r["historicalFrame"] <= peak})
        replays = []
        for observed_case in observed_cases:
            stream = {r["historicalFrame"]: r for r in comparisons
                      if r["observedCase"] == observed_case["directory"]}
            if not all(i in stream for i in range(first, peak+1)):
                continue
            window = [stream[i]["rois"][roi] for i in range(first, peak+1)]
            values = replay_samples(series[first-1], [r["scaledRaySampleE"] for r in window])
            error = max(abs(value-series[i])/max(abs(series[i]), FLOOR)
                        for i, value in enumerate(values, first))
            replays.append(dict(observedCase=observed_case["directory"],
                initialHistoricalFrame=first-1, initialHistoricalE=series[first-1],
                sampleUpdates=historical["globalUpdates"][first:peak+1],
                sampleE=[r["scaledRaySampleE"] for r in window],
                illuminationScales=[r["illuminationScale"] for r in window],
                replayedE=values, maximumRelativeError=float(error), arithmeticPassed=error <= .005,
                replayedPeakE=values[-1], replayedOvershoot=float(max(0, sign*(values[-1]-metrics["new"]))/metrics["amplitude"]),
                attribution="ConditionalFixedHistoricalQueryScalarReplayNotOriginalAtlasObservation"))
        peaks.append(dict(roi=roi, frame=peak, globalUpdate=historical["globalUpdates"][peak],
            luminance=series[peak], originalOvershoot=metrics["overshoot"], originalTargetsPassed=False,
            matchedCases=[r["observedCase"] for r in matches], independentlyMatchedHistoryFrames=covered,
            missingHistoryFrames=[i for i in range(first, peak+1) if i not in covered],
            completeHistoricalSampleWindowMatched=len(covered) == peak-first+1, scalarReplays=replays))
    passed = (all(r["rayBlendClosure"]["arithmeticPassed"] and
                  all(v["arithmeticPassed"] for v in r["rois"].values()) for r in comparisons) and
              all(r["arithmeticPassed"] for peak in peaks for r in peak["scalarReplays"]))
    require(before == [receipt(p) for p in inputs] and code_before == [receipt(p) for p in code],
            "Raw evidence or analysis code changed during comparison")
    output.mkdir(parents=True, exist_ok=False)
    save(output/"input-receipts.json", before)
    save(output/"analysis-code-receipts.json", code_before)
    snapshot = output/"analysis-code"
    snapshot.mkdir()
    for path in code:
        shutil.copy2(path, snapshot/path.name)
    result = dict(status="analyzed" if passed else "analyzed-with-arithmetic-failures", contractsPassed=passed,
        engineeringOnly=True, historicalRoot=str(historical_root.resolve()), observedRoot=str(observed_root.resolve()),
        historicalCase=case, originalResponseTargetsPassed=historical["observedResponseTargetsPassed"],
        responseQualityQualified=False, referenceQualification=False, wholeG3Qualified=False,
        historicalBlendDirectlyObserved=False, peaks=peaks, comparisons=comparisons,
        maximumSampleRelativeDifference=max(v["relativeDifference"] for r in comparisons for v in r["rois"].values()))
    save(output/"result.json", result)
    print(json.dumps({k: v for k, v in result.items() if k != "comparisons"}))
    return passed


def self_test():
    case = dict(backend="vulkan", configuration="Develop", fixture=4, environment=3, view="F4_Floor",
                tap="gi", feedback=False, step="small-increase", oldLux=10000, newLux=12000,
                traceSeed=81052, progressiveRefresh=True, preEventSettleFrames=145)
    observed = dict(case, progressiveAttribution=True, preEventSettleFrames=128)
    require(compatible(case, observed), "Wait minima were incorrectly treated as sample identities")
    for changed in (dict(traceSeed=81053), dict(backend="dx12"), dict(feedback=True),
                    dict(progressiveAttribution=False), dict(configuration="Release")):
        require(not compatible(case, dict(observed, **changed)), "Different sample cohort accepted")
    scaled = matched_sample(530, 530, 90, 92, 91, .75, 98/1.2, 10000, 12000)
    require(scaled["arithmeticPassed"] and scaled["predictedHistoricalE"] == 92 and
            abs(scaled["historyDeltaE"]+scaled["raySampleDeltaE"]-1) < 1e-12,
            "Conditional illumination-scaled history/sample closure failed")
    same = matched_sample(530, 530, 90, 92, 91, .75, 98, 12000, 12000)
    require(same["arithmeticPassed"] and same["illuminationScale"] == 1,
            "Same-lighting update matching changed")
    require(not matched_sample(530, 530, 90, 92, 91, .75, 100, 12000, 12000)["arithmeticPassed"],
            "An arithmetic mismatch was hidden by engineering completion")
    for update, h, lux, ray in ((531, .75, 12000, 98), (530, 1., 12000, 98),
                                (530, .75, 0, 98), (530, .75, 12000, float("nan"))):
        try:
            matched_sample(530, update, 90, 92, 91, h, ray, lux, 12000)
            raise AssertionError("Invalid sampling identity or numerical input accepted")
        except ValueError:
            pass
    raw = [4., 2., 5., 3., 2., 4.]
    replayed = replay_samples(1., raw)
    decay = float(np.float32(.998))
    weights = decay**np.arange(len(raw)-1, -1, -1)
    expected = (weights[0]+np.dot(weights, raw))/(weights[0]+weights.sum())
    require(replayed[0] == 2.5 and abs(replayed[-1]-expected) < 1e-12 and
            replayed[-1] != 0.5*(replayed[-2]+raw[-1]),
            "Scalar replay reused historical outputs or a fixed integration coefficient")
    for initial, samples in ((float("nan"), raw), (1., []), (1., [float("inf")])):
        try:
            replay_samples(initial, samples)
            raise AssertionError("An incomplete or nonfinite replay acquired a peak")
        except ValueError:
            pass
    print(json.dumps(dict(status="passed", checks=17)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--historical-root", type=Path)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--case")
    parser.add_argument("--observed-case", help="Select one explicit attribution case without relaxing input checks")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        require(all((args.historical_root, args.root, args.case, args.output)) and not args.output.exists(),
                "Two raw roots, one explicit historical case and a fresh output directory are required")
        if not compare(args.historical_root, args.root, args.case, args.output, args.observed_case):
            raise SystemExit(1)
