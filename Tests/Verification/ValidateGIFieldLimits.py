"""F3/F5 representation limits at Ready plus 120 against the qualified D=1 references.

Plan 9.3 measures these limits at G3 and gates them at G4, where multi-bounce,
depth-converged references apply; nothing here qualifies either stage. The local
leakage metric's floor is a predeclared input. Without a limit policy, values are
reported over a floor sweep, never as a verdict. The F3 sweep scales the lit roof
reference; the F5 sweep scales the right-floor reference, which at D=1 receives only
sky through the doorway and is therefore no lit basis.
Percentiles use the upper index ceil(0.95 n) - 1 of the sorted pixel values.
"""
import argparse
import json
from pathlib import Path
import tempfile

import numpy as np

from ValidateGIFieldBaseline import FLOOR, load, load_policy, read_export, roi_masks, save, validate_case
from ValidateGIReferenceLighting import LUMA

THIN_WALL_LIMIT = .03
LOCAL_LEAKAGE_LIMIT = .25
ENERGY_LIMIT = .10
STABILITY_LIMIT = .02
FLOOR_SWEEP = (1e-3, 1e-2, 1e-1, 1.)
SEEDS = (61051, 61052)
# F3 patches from F2; the leak mask is the union of the inner wall and ceiling patches.
F3_PATCHES = {"thin": "back", "thin-floor": "floor", "thin-ceiling": "ceiling"}
F3_LEAK = (("thin", "back"), ("thin-ceiling", "ceiling"))
F5_ROIS = {"doorway": ("doorway-strip",), "parallel-floor": ("right-floor", "far-corner")}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def upper_percentile(values, percentile=95):
    values = np.sort(np.asarray(values, dtype=float).reshape(-1))
    require(values.size and np.isfinite(values).all(), "Percentile needs finite pixels")
    return float(values[(percentile*values.size+99)//100-1])


def luminance(array):
    return np.asarray(array[..., :3], dtype=float) @ LUMA


def load_case(directory):
    """Qualified-N reference pair, frozen masks and the Ready-plus-120 field window of one view."""
    case = load(directory/"case.json")
    validate_case(case)
    qualification = load(directory/"reference-validation.json")
    high = qualification["samples"]
    observations = {(o["samples"], o["seed"]): o["directory"] for o in load(directory/"references.json")}
    require(all((high, seed) in observations for seed in SEEDS), "Missing qualified reference pair")
    references = [read_export(directory/observations[(high, seed)])[1] for seed in SEEDS]
    masks = roi_masks(case, references[0])
    require(all(qualification["rois"][name]["pixels"] == int(mask.sum()) for name, mask in masks.items()),
            "Limit masks differ from the reference-qualified ROIs")
    result = dict(case=case, masks=masks, samples=high,
                  referenceSamplingQualified=qualification["referenceSamplingQualified"],
                  reference=[luminance(r["GI.Reference.Irradiance"][0]) for r in references],
                  referenceRgb=[r["GI.Reference.Irradiance"][0, ..., :3].astype(float) for r in references])
    settled = load(directory/"settled/receipt.json")
    if settled["status"] != "completed":
        return dict(result, field=None, reason="Unconverged")
    paths = list((directory/"samples").glob("*.resource-export.json"))
    require(len(paths) == 120, "Limit window needs 120 exported frames")
    paths.sort(key=lambda path: load(path)["frame"]["sequence"]["frame"])
    position = references[0]["GI.Receiver.Position"][0]
    frames, rgb = [], None
    for index, path in enumerate(paths):
        record = load(path)
        require(record["frame"]["sequence"]["frame"] == index and record["submission"]["completed"],
                "Gapped or incomplete limit window")
        targets = {t["address"]["resource"]: t for t in record["targets"]}
        energy = np.load(path.parent/targets["GI.Field.ReceiverIrradiance"]["array"], allow_pickle=False)[0]
        receiver = np.load(path.parent/targets["GI.Receiver.Position"]["array"], allow_pickle=False)[0]
        require(np.array_equal(receiver[..., 3], position[..., 3]) and np.isfinite(energy).all(),
                "Field window receivers differ from the reference")
        frames.append({name: luminance(energy[mask]) for name, mask in masks.items()})
        if index == 0:
            rgb = {name: energy[mask, :3].astype(float).mean(axis=0) for name, mask in masks.items()}
    return dict(result, field=frames, fieldRgb=rgb, reason=None)


def pixel_metrics(field, reference_pair, floors, policy_floor):
    """Per-pixel excess (GI - ref)/max(ref, floor) and the seed-to-seed noise prerequisite."""
    a, b = (np.asarray(r, dtype=float) for r in reference_pair)
    reference = (a+b)/2
    excess = np.asarray(field, dtype=float)-reference
    difference = np.abs(a-b)

    def at(floor):
        scale = np.maximum(reference, floor)
        noise = upper_percentile(difference/scale)
        return dict(floor=floor, excessPercentile95=upper_percentile(excess/scale),
                    referenceNoisePercentile95=noise, noisePrerequisiteMet=noise <= LOCAL_LEAKAGE_LIMIT/3)
    result = dict(pixels=int(reference.size), referenceMean=float(reference.mean()), fieldMean=float(np.mean(field)),
                  absoluteExcessPercentile95=upper_percentile(excess), absoluteExcessMaximum=float(excess.max()),
                  referenceDifferencePercentile95=upper_percentile(difference),
                  sweep=[at(floor) for floor in floors])
    if policy_floor is not None:
        declared = at(policy_floor)
        declared["withinG4Limit"] = declared["noisePrerequisiteMet"] and \
            declared["excessPercentile95"] <= LOCAL_LEAKAGE_LIMIT
        result["declared"] = declared
    return result


def stability(series, reference_mean, floors, policy_floor):
    series = np.asarray(series, dtype=float)
    deviation = float(np.sqrt(np.mean((series-series.mean())**2)))
    result = dict(temporalStd=deviation, sweep=[dict(floor=f, temporalRms=deviation/max(reference_mean, f))
                                                for f in floors])
    if policy_floor is not None:
        rms = deviation/max(reference_mean, policy_floor)
        result["declared"] = dict(floor=policy_floor, temporalRms=rms, passed=rms <= STABILITY_LIMIT)
    return result


def fixture_floors(policy, fixture, key):
    return (policy or {}).get("fixtures", {}).get(str(fixture), {}).get(key)


def thin_wall(cases, policy):
    roof = cases.get("thin-roof")
    if roof is None or any(name not in cases for name, _ in F3_LEAK):
        return dict(available=False, reason="F3 needs thin, thin-ceiling and thin-roof cases")
    basis = float(np.mean([r[roof["masks"]["roof"]].mean() for r in roof["reference"]]))
    require(basis > 0, "F3 roof reference lost its lit basis")
    floors = [fraction*basis for fraction in FLOOR_SWEEP]
    leak_floor = fixture_floors(policy, 3, "leakageFloor")
    stability_floor = fixture_floors(policy, 3, "stabilityFloor")
    patches = {}
    for case_id, roi in F3_PATCHES.items():
        data = cases.get(case_id)
        if data is None or data["field"] is None:
            patches[roi] = dict(available=False, reason="missing case" if data is None else data["reason"])
            continue
        mask = data["masks"][roi]
        reference_mean = float(np.mean([r[mask].mean() for r in data["reference"]]))
        means = [frame[roi].mean() for frame in data["field"]]
        fraction, peak = float(means[0]/basis), float(max(means)/basis)
        patches[roi] = dict(available=True, referenceMean=reference_mean, fieldMean=float(means[0]),
                            leakFraction=fraction, windowMaximumLeakFraction=peak,
                            withinG4Limit=peak <= THIN_WALL_LIMIT,
                            stability=stability(means, reference_mean, floors, stability_floor))
    union = [(cases[case_id], roi) for case_id, roi in F3_LEAK]
    if any(data["field"] is None for data, _ in union):
        leak = dict(available=False, reason="Unconverged leak-mask view")
    else:
        field = np.concatenate([data["field"][0][roi] for data, roi in union])
        pair = [np.concatenate([data["reference"][s][data["masks"][roi]] for data, roi in union]) for s in (0, 1)]
        leak = dict(available=True, **pixel_metrics(field, pair, floors, leak_floor))
        windows = [np.concatenate([data["field"][i][roi] for data, roi in union]) for i in range(120)]
        leak["windowMaximumAbsoluteExcessPercentile95"] = max(upper_percentile(w-(pair[0]+pair[1])/2) for w in windows)
    referenced = all(cases[c]["referenceSamplingQualified"] for c in cases if c in F3_PATCHES or c == "thin-roof")
    return dict(available=True, litBasis=dict(roi="roof", referenceMean=basis), floorSweepFractions=FLOOR_SWEEP,
                referenceSamplingQualified=referenced, thinWallLimit=THIN_WALL_LIMIT, patches=patches,
                leakMask=dict(union=[f"{c}/{r}" for c, r in F3_LEAK], limit=LOCAL_LEAKAGE_LIMIT, **leak))


def parallel_rooms(cases, policy):
    if not any(case_id in cases for case_id in F5_ROIS):
        return dict(available=False, reason="No F5 cases")
    lit = cases.get("parallel-floor")
    basis = None
    if lit is not None:
        basis = float(np.mean([r[lit["masks"]["right-floor"]].mean() for r in lit["reference"]]))
    floors = [fraction*basis for fraction in FLOOR_SWEEP] if basis else []
    leak_floor = fixture_floors(policy, 20, "leakageFloor")
    stability_floor = fixture_floors(policy, 20, "stabilityFloor")
    rois = {}
    for case_id, names in F5_ROIS.items():
        data = cases.get(case_id)
        for roi in names:
            if data is None or data["field"] is None:
                rois[roi] = dict(available=False, reason="missing case" if data is None else data["reason"])
                continue
            mask = data["masks"][roi]
            reference_rgb = np.mean([r[mask].mean(axis=0) for r in data["referenceRgb"]], axis=0)
            reference_mean = float(reference_rgb @ LUMA)
            means = [frame[roi].mean() for frame in data["field"]]
            error = float((means[0]-reference_mean)/max(reference_mean, FLOOR))
            field_rgb = data["fieldRgb"][roi]
            chromaticity = None
            if reference_rgb.sum() > FLOOR and field_rgb.sum() > FLOOR:
                chromaticity = float(np.max(np.abs(field_rgb[:2]/field_rgb.sum()-reference_rgb[:2]/reference_rgb.sum())))
            entry = dict(available=True, referenceSamplingQualified=data["referenceSamplingQualified"],
                         referenceMean=reference_mean, fieldMean=float(means[0]), relativeEnergyError=float(error),
                         windowMaximumAbsoluteEnergyError=float(max(abs(m-reference_mean) for m in means) /
                                                                max(reference_mean, FLOOR)),
                         chromaticityError=chromaticity, energyLimit=ENERGY_LIMIT,
                         withinG4EnergyLimit=abs(error) <= ENERGY_LIMIT,
                         stability=stability(means, reference_mean, floors, stability_floor))
            if roi == "doorway-strip":
                pair = [r[mask] for r in data["reference"]]
                entry["localLeakage"] = dict(limit=LOCAL_LEAKAGE_LIMIT,
                                             **pixel_metrics(data["field"][0][roi], pair, floors, leak_floor))
            rois[roi] = entry
    return dict(available=True, sweepBasis=dict(roi="right-floor", referenceMean=basis) if basis else None,
                floorSweepFractions=FLOOR_SWEEP, rois=rois)


def validate(root):
    policy = load_policy(root/"limit-policy.json")
    case_ids = load(root/"cases.json")
    results = []
    for backend in load(root/"backends.json"):
        cases = {}
        for case_id in case_ids:
            directory = root/backend/case_id
            if load(directory/"case.json")["fixture"] in (3, 20):
                cases[case_id] = load_case(directory)
        results.append(dict(backend=backend, f3=thin_wall(cases, policy), f5=parallel_rooms(cases, policy)))
    return dict(schemaVersion=1, status="measured", limitPolicy=policy, referenceDepth=1,
                referenceDepthConvergenceQualified=False, gatedAt="G4", g3Qualified=False, g4Qualified=False,
                results=results)


def self_test():
    checks = 0

    def reject(action):
        nonlocal checks
        try:
            action()
        except (ValueError, KeyError):
            checks += 1
            return
        raise AssertionError("Invalid limit evidence was accepted")

    require(upper_percentile(np.arange(1, 101)) == 95 and upper_percentile([3., 1., 2.]) == 3 and
            upper_percentile(np.arange(20)) == 18, "Upper-index percentile")
    checks += 1
    reject(lambda: upper_percentile([]))
    reject(lambda: upper_percentile([np.nan]))
    zero = np.zeros(100)
    field = np.linspace(0, .02, 100)
    metrics = pixel_metrics(field, (zero, zero), (.01, .1), None)
    require(metrics["absoluteExcessPercentile95"] == field[94] and
            [s["excessPercentile95"] for s in metrics["sweep"]] == [field[94]/.01, field[94]/.1] and
            all(s["noisePrerequisiteMet"] and s["referenceNoisePercentile95"] == 0 for s in metrics["sweep"]) and
            "declared" not in metrics, "Zero-reference leak scales by the floor alone")
    checks += 1
    declared = pixel_metrics(field, (zero, zero), (), .1)["declared"]
    require(declared["withinG4Limit"] and not pixel_metrics(field, (zero, zero), (), .01)["declared"]["withinG4Limit"],
            "Declared floor gives the G4 verdict")
    checks += 1
    noisy = pixel_metrics(np.ones(10), (np.ones(10), np.full(10, 1.5)), (), 1.)["declared"]
    require(not noisy["noisePrerequisiteMet"] and not noisy["withinG4Limit"],
            "Reference noise above a third of the threshold blocks the percentile verdict")
    checks += 1
    series = np.r_[np.full(60, .99), np.full(60, 1.01)]
    result = stability(series, 1., (.1,), .5)
    require(np.isclose(result["temporalStd"], .01) and np.isclose(result["sweep"][0]["temporalRms"], .01) and
            result["declared"]["passed"], "Stability uses max(reference, floor)")
    require(not stability(series, 0., (), .1)["declared"]["passed"], "Dark-floor stability failure")
    checks += 1
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        save(directory/"case.json", dict(id="doorway", fixture=4, view="F5_Doorway", counts=[11, 7, 7]))
        reject(lambda: load_case(directory))
    require(thin_wall({}, None)["available"] is False and parallel_rooms({}, None)["available"] is False,
            "Missing cases are unavailable, not passing")
    checks += 1
    # Measured pixels arrive as numpy scalars; every recorded verdict must still serialize.
    mask = np.ones((2, 2), dtype=bool)
    lit = np.full((2, 2), np.float32(.5))
    rgb = np.full((2, 2, 3), np.float32(.5))

    def measured(roi):
        return dict(masks={roi: mask}, reference=[lit, lit], referenceRgb=[rgb, rgb], referenceSamplingQualified=True,
                    field=[{roi: luminance(rgb[mask]*np.float32(1.05))} for _ in range(120)],
                    fieldRgb={roi: rgb[mask].mean(axis=0)}, reason=None)
    rooms = parallel_rooms({"doorway": measured("doorway-strip"), "parallel-floor": dict(
        measured("right-floor"), masks={"right-floor": mask, "far-corner": mask},
        field=[{"right-floor": luminance(rgb[mask]), "far-corner": luminance(rgb[mask])}] * 120,
        fieldRgb={"right-floor": rgb[mask].mean(axis=0), "far-corner": rgb[mask].mean(axis=0)})},
        dict(fixtures={"20": dict(stabilityFloor=.1, leakageFloor=.1)}))
    json.dumps(rooms, allow_nan=False)
    require(rooms["rois"]["doorway-strip"]["withinG4EnergyLimit"] is True and
            rooms["rois"]["right-floor"]["relativeEnergyError"] == 0, "F5 verdicts serialize as JSON booleans")
    checks += 1
    print(json.dumps(dict(ok=True, checks=checks)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--root", type=Path)
    group.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        result = validate(args.root)
        save(args.root/"limits.json", result)
        print(json.dumps(dict(status=result["status"], backends=len(result["results"]), g3Qualified=False)))
