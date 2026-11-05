"""Independent endpoint certificates for the opt-in Physical Sky lighting experiment.

Reuse the convergence ladder and immutable receipts, not the frozen constant-sky
F4 reader. D=1 certifies sampling only; feedback-on requires depth convergence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile

import numpy as np

from ValidateCoastalGIReference import load_export, validate_arrays
from ValidateGIFieldBaseline import camera_inputs, parameters, scene_inputs
from ValidateGIReferenceConvergence import (COMPONENTS, LUMA, SEEDS, compare, compare_rhi, digest,
    file_receipts, key, load, merge_errors, plan as depth_plan, qualified, require, save,
    validate_recipe as validate_depth_recipe)

SCHEMA = 1
ROIS = ("floor", "red-adjacent", "green-adjacent", "simple-patch")


def roi_masks(position):
    p = np.asarray(position)
    require(p.shape == (1, 200, 320, 4) and np.isfinite(p).all(), "Invalid receiver-mask ABI")
    x, y, z, covered = (p[0, ..., i] for i in range(4))
    floor = (covered == 1) & (np.abs(y) < 1e-5) & (np.abs(x) <= 1.5) & (z >= -1.5) & (z <= .5)
    masks = {"floor": floor, "red-adjacent": floor & (x <= -.5),
             "green-adjacent": floor & (x >= .5),
             "simple-patch": floor & (np.abs(x) <= .25) & (z >= -1) & (z <= 0)}
    require(all(m.sum() >= 32 for m in masks.values()), "A frozen required experiment ROI is empty")
    return masks


def endpoint_inputs(frame):
    # Field/reference policy and ephemeral generations differ intentionally.
    # Compare actual effective lighting, authored camera, material and geometry inputs.
    p = parameters(frame)
    return dict(camera=camera_inputs(frame), scene=scene_inputs(frame),
                materials={k: p[k] for k in ("fixture", "albedo", "secondary_albedo", "environment", "ev100")},
                experiment=p.get("lighting_step_experiment", False),
                extent=frame["temporal"]["renderExtent"], code=frame["codeIdentity"], shader=frame["shaderIdentity"])


def validate_recipe(p):
    require(p["schemaVersion"] == SCHEMA and p["experiment"] == "gi-field-light-step" and
            p["sunLux"] in (10000, 20000) and p["referenceMode"] in ("single-bounce", "multibounce") and
            p["views"] == ["F4_Floor"] and p["extent"] == [320, 200], "Changed lighting-step reference recipe")
    proxy = dict(p, schemaVersion=2)
    if p["referenceMode"] == "single-bounce":
        require(p["depths"] == [1], "D=1 cannot certify multibounce transport")
        proxy["depths"] = [8, 16]
    validate_depth_recipe(proxy)


def plan(state):
    p, records = state["recipe"], state["observations"]
    if p["referenceMode"] == "multibounce":
        result = depth_plan(state)
    else:
        n = p["initialF4Samples"]
        final = dict(status="ReferenceUnconverged", reason="SamplingLimit")
        result = None
        while 2*n <= p["maximumSamples"]:
            names = [key("F4_Floor", 1, s, seed) for s in (n, 2*n) for seed in SEEDS]
            missing = next((name for name in names if name not in records), None)
            if missing:
                index = names.index(missing)
                result = dict(status="Running", results={}, request=dict(directory="captures/"+missing,
                    view="F4_Floor", fixture=4, depth=1, samples=(n, 2*n)[index//2], seed=SEEDS[index%2],
                    batch_samples=p["batchSamples"], tile_pixels=0, albedo=0, secondary_albedo=0, ev100=10))
                break
            rows = [records[name] for name in names]
            sampling = merge_errors([compare(rows[a], rows[b], p) for a, b in ((0, 1), (2, 3), (0, 2), (1, 3))])
            passed = qualified(sampling, p)
            final = dict(status="ReferenceConverged" if passed else "ReferenceUnconverged",
                reason="SamplingQualified" if passed else "SamplingLimit", depth=1, samples=2*n,
                sampling=sampling, seeds=list(SEEDS), candidates=names[2:] if passed else [],
                roiStatus={roi: "ReferenceConverged" if max(row.values()) < .01 else "ReferenceUnconverged"
                           for roi, row in sampling.items()})
            if passed:
                break
            n *= 2
        if result is None:
            result = dict(status=final["status"], results={"F4_Floor": final})
    if result["status"] == "Running":
        request = result["request"]
        # This recipe has one fixed view; keep nested publication paths below Win32 MAX_PATH.
        request["directory"] = f"c/d{request['depth']}-n{request['samples']}-s{request['seed']}"
        request.update(environment=3, sun_step_lux=p["sunLux"], lighting_step_experiment=True)
    result.update(referenceMode=p["referenceMode"], sunLux=p["sunLux"],
                  samplingOnly=p["referenceMode"] == "single-bounce", depthConverged=(
                      p["referenceMode"] == "multibounce" and result["status"] == "ReferenceConverged"))
    return result


def summarize(root, case, recipe):
    require(case["view"] == "F4_Floor" and case["fixture"] == 4 and case["environment"] == 3 and
            case["sun_step_lux"] == recipe["sunLux"] and case["lighting_step_experiment"] and
            case["depth"] in recipe["depths"] and case["seed"] in SEEDS, "Wrong endpoint reference request")
    directory = root/case["directory"]
    values, frame = load_export(directory)
    receivers, receiver_frame = load_export(directory/"receivers")
    require(endpoint_inputs(frame) == endpoint_inputs(receiver_frame) and
            frame["parameters"]["values"] == receiver_frame["parameters"]["values"], "Receiver/reference input drift")
    p = parameters(frame)
    p.setdefault("sun_step_lux", 10000)
    require(frame["backend"] == recipe["rhi"] and frame["adapterDriverIdentity"] == recipe["adapterDriverIdentity"] and
            frame["content"]["labId"] == "gglab.lab.gi_reference" and
            frame["camera"]["referenceView"] == "F4_Floor" and p.get("lighting_step_experiment") and
            not p["field_enabled"] and not p["field_feedback"] and p["accumulate"] and p["receiver_diagnostics"] and
            all(p[k] == case[k] for k in ("fixture", "depth", "samples", "batch_samples", "seed", "albedo",
                "secondary_albedo", "environment", "ev100", "sun_step_lux")) and
            p.get("tile_pixels", 0) == 0, "Reference must be independent of the realtime field")
    for f in (frame, receiver_frame):
        t = f["temporal"]
        require(t["status"] == "disabled" and t["jitterPixels"] == [0, 0] and
                t["renderExtent"] == t["displayExtent"] == [320, 200] and not t["gtao"]["enabled"] and
                not f["developmentTools"] and not f["sceneInputs"]["postProcess"]["bloomEnabled"], "Wrong reference composition")
    gate = [g for g in frame["readiness"]["gates"] if g["name"] == "gi-reference"]
    require(frame["readiness"]["ready"] and len(gate) == 1 and gate[0]["state"] == "ready" and
            f"D={case['depth']}," in gate[0]["detail"] and
            f"{case['samples']}/{case['samples']} completed samples" in gate[0]["detail"], "Incomplete reference")
    lighting = frame["parameters"]["lighting"]
    require(lighting["active"]["generation"] == lighting["requested"]["generation"] ==
            frame["sceneInputs"]["environment"]["activeIBLGeneration"] > 0 and
            frame["sceneInputs"]["sun"]["toaIlluminanceLux"] == recipe["sunLux"] and
            frame["sceneInputs"]["atmosphere"] is not None, "Unpublished/non-Physical Sky endpoint")
    validate_arrays(values, receivers, case["depth"])
    masks = roi_masks(receivers["GI.Receiver.Position"][None])
    inputs = endpoint_inputs(frame)
    input_identity = digest(inputs)
    rois = {}
    for name, mask in masks.items():
        row = dict(pixels=int(mask.sum()), maskSha256=hashlib.sha256(mask.tobytes()).hexdigest())
        for component in COMPONENTS:
            mean = values["GI.Reference."+component][mask, :3].mean(0, dtype=np.float64)
            row[component] = [*mean.tolist(), float(mean @ LUMA)]
        rois[name] = row
    return dict(identity=digest(dict(inputs=input_identity, backend=frame["backend"],
                adapter=frame["adapterDriverIdentity"])), inputIdentity=input_identity, inputs=inputs,
                rois=rois, case=case, files=file_receipts(directory))


def certified_endpoint(root, expected_mode, lux, frame, masks, binaries):
    state, report = load(root/"checkpoint.json"), load(root/"report.json")
    p = state["recipe"]
    validate_recipe(p)
    require(state["schemaVersion"] == SCHEMA and state["recipeIdentity"] == report["recipeIdentity"] == digest(p) and
            p["referenceMode"] == expected_mode and p["sunLux"] == lux and
            p["rhi"] == frame["backend"] and p["adapterDriverIdentity"] == frame["adapterDriverIdentity"] and
            {k: v.lower() for k, v in p["binaries"].items()} == {k: v.lower() for k, v in binaries.items()},
            "Changed reference mode/build/backend/device/recipe")
    fresh = plan(state)
    if report["status"] != "ReferenceConverged":
        return dict(status="ReferenceUnconverged", reason=report.get("budgetReason", report["status"]))
    require(fresh["status"] == "ReferenceConverged" and fresh["results"] == report["results"], "Stale reference certificate")
    # Reconstruct every prerequisite, including lower-N/depth comparisons. A
    # checkpoint's cached means alone are not evidence of independent transport.
    for record in state["observations"].values():
        require(record["files"] and record["files"] == file_receipts(root/record["case"]["directory"]),
                "Missing/modified endpoint reference artifacts")
        require(record == summarize(root, record["case"], p), "Cached reference summary differs from completed artifacts")
    rows = []
    for name in fresh["results"]["F4_Floor"]["candidates"]:
        record = state["observations"][name]
        require(record["inputs"] == endpoint_inputs(frame), "Reference/experiment effective inputs differ")
        for roi, mask in masks.items():
            require(record["rois"][roi]["maskSha256"] == hashlib.sha256(mask.tobytes()).hexdigest(), "Reference/experiment ROI differs")
        rows.append(record)
    require(len(rows) == 2, "Independent reference seeds are required")
    return dict(status="ReferenceConverged", samplingOnly=expected_mode == "single-bounce",
                depth=fresh["results"]["F4_Floor"]["depth"], samples=fresh["results"]["F4_Floor"]["samples"],
                rois={roi: np.mean([r["rois"][roi]["Irradiance"][:3] for r in rows], axis=0).tolist() for roi in ROIS})


def self_test():
    p = dict(schemaVersion=SCHEMA, experiment="gi-field-light-step", sunLux=10000, referenceMode="single-bounce",
             views=["F4_Floor"], extent=[320, 200], depths=[1], initialSamples=2, initialF4Samples=2,
             maximumSamples=8, batchSamples=2, relativeLimit=.01, absoluteFloor=1e-6)
    validate_recipe(p)
    state = dict(recipe=p, observations={})
    require(plan(state)["request"]["environment"] == 3, "Constant-sky F4 must not be reused")
    for n in (2, 4):
        for seed in SEEDS:
            state["observations"][key("F4_Floor", 1, n, seed)] = dict(identity="fixed", rois={roi: dict(
                maskSha256=roi, **{c: [1.]*4 for c in COMPONENTS}) for roi in ROIS})
    result = plan(state)
    require(result["status"] == "ReferenceConverged" and result["samplingOnly"] and
            not result["depthConverged"] and result["results"]["F4_Floor"]["depth"] == 1, "D=1 is not depth convergence")
    state["observations"][key("F4_Floor", 1, 4, SEEDS[1])]["rois"]["simple-patch"]["SunIrradiance"][0] = 2
    require(plan(state)["status"] == "Running", "A small ROI/channel failure cannot hide")
    p["maximumSamples"] = 4
    require(plan(state)["status"] == "ReferenceUnconverged", "Sampling exhaustion must not pass")
    for field, value in (("depths", [8, 16]), ("sunLux", 15000), ("relativeLimit", .02), ("extent", [640, 400])):
        bad = dict(p, **{field: value})
        try:
            validate_recipe(bad)
            raise AssertionError("Changed recipe accepted")
        except ValueError:
            pass
    p.update(referenceMode="multibounce", depths=[8, 16, 32])
    validate_recipe(p)
    multi = dict(recipe=p, observations={})
    request = plan(multi)["request"]
    require(request["depth"] == 8 and request["sun_step_lux"] == 10000, "Multibounce ladder lost experiment inputs")
    for depth in (8, 16):
        for n in (2, 4):
            for seed in SEEDS:
                multi["observations"][key("F4_Floor", depth, n, seed)] = dict(identity="fixed", rois={roi: dict(
                    maskSha256=roi, **{c: [1.]*4 for c in COMPONENTS}) for roi in ROIS})
    require(plan(multi)["depthConverged"] and not plan(multi)["samplingOnly"], "Multibounce needs both numerical gates")
    multi["observations"][key("F4_Floor", 16, 4, SEEDS[1])]["identity"] = "other-lighting"
    try:
        plan(multi)
        raise AssertionError("Mixed lighting identity accepted")
    except ValueError:
        pass
    x, z = np.meshgrid(np.linspace(-1.5, 1.5, 320), np.linspace(-1.5, .5, 200))
    position = np.stack([x, np.zeros_like(x), z, np.ones_like(x)], axis=-1)[None]
    require(tuple(roi_masks(position)) == ROIS, "The declared four receiver ROIs changed")
    position[0, 0, 0, 0] = np.nan
    try:
        roi_masks(position)
        raise AssertionError("Nonfinite receiver outside the small ROI accepted")
    except ValueError:
        pass
    # Even a numerically perfect JSON certificate needs completed GPU products.
    p = dict(p, referenceMode="single-bounce", depths=[1], rhi="dx12",
             adapterDriverIdentity="device", binaries={"app": "hash"})
    fake = dict(schemaVersion=SCHEMA, recipe=p, recipeIdentity=digest(p), observations={})
    for n in (2, 4):
        for seed in SEEDS:
            fake["observations"][key("F4_Floor", 1, n, seed)] = dict(identity="fixed", files={},
                case=dict(directory="missing"), rois={roi: dict(maskSha256=roi,
                    **{c: [1.]*4 for c in COMPONENTS}) for roi in ROIS})
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        save(root/"checkpoint.json", fake)
        save(root/"report.json", dict(plan(fake), recipeIdentity=digest(p)))
        frame = dict(backend="dx12", adapterDriverIdentity="device")
        for mode in ("single-bounce", "multibounce"):
            try:
                certified_endpoint(root, mode, 10000, frame, {}, p["binaries"])
                raise AssertionError("Unbacked certificate or wrong transport mode accepted")
            except ValueError:
                pass
    print('{"status":"passed","checks":15}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--initialize", action="store_true")
    parser.add_argument("--ingest", type=Path)
    parser.add_argument("--audit", action="store_true")
    parser.add_argument("--check-recipe", type=Path)
    parser.add_argument("--compare-rhi", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    root = args.directory
    if args.compare_rhi:
        compare_rhi(root)
        return
    p = load(root/"recipe.json")
    validate_recipe(p)
    if args.check_recipe:
        require(digest(p) == digest(load(args.check_recipe)), "Changed recipe/source/binary/device")
        print('{"status":"RecipeMatches"}')
        return
    checkpoint = root/"checkpoint.json"
    if args.initialize and not checkpoint.exists():
        save(checkpoint, dict(schemaVersion=SCHEMA, recipe=p, recipeIdentity=digest(p), observations={}))
    state = load(checkpoint)
    require(state["schemaVersion"] == SCHEMA and state["recipeIdentity"] == digest(p), "Changed checkpoint identity")
    if args.audit:
        for row in state["observations"].values():
            require(row["files"] == file_receipts(root/row["case"]["directory"]), "Modified cached reference")
    if args.ingest:
        case = load(args.ingest)
        name = key(case["view"], case["depth"], case["samples"], case["seed"])
        require(name not in state["observations"], "Completed cache entries are immutable")
        state["observations"][name] = summarize(root, case, p)
        save(checkpoint, state)
    result = plan(state)
    result.update(schemaVersion=SCHEMA, recipeIdentity=digest(p), observations=len(state["observations"]),
                  runtimeCompletionIsConvergence=False)
    if not args.audit or args.initialize or args.ingest:
        save(root/"report.json", result)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
