"""Versioned, resumable per-ROI reference qualification; Runtime completion is not convergence.

Independent seeds at N and 2N qualify sampling before comparing adjacent depths at
the SAME 2N and seeds. Only completed immutable captures enter the checkpoint.
The metric is max RGB/luminance relative change with a declared 1e-6 absolute floor.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import tempfile

import numpy as np

from ValidateGIReferenceTransport import read_case, transport_masks
from ValidateGIReferenceLighting import require
from ValidateCoastalGIReference import (COASTAL_VIEWS, CONTRACT_VIEWS, INPUT_PATH,
                                       read_case as read_coastal_case, self_test as coastal_self_test)

SCHEMA = 2
SEEDS = (61051, 61052)
LUMA = np.array([.2126, .7152, .0722])
COMPONENTS = ("Irradiance", "SunIrradiance", "EnvironmentIrradiance")
VIEWS = {"F1_Plane": 1, "F2_Floor": 2, "F3_Floor": 3, "F4_Floor": 4, "F4_Back": 4}
VIEWS.update({view: 12 for view in COASTAL_VIEWS})
VIEWS.update({view: 13 for view in CONTRACT_VIEWS})


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def save(path, value):
    path = Path(path)
    pending = path.with_suffix(path.suffix + ".pending")
    pending.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    pending.replace(path)


def key(view, depth, samples, seed):
    return f"{view}-d{depth}-n{samples}-s{seed}"


def changed(a, b, policy):
    a, b = np.asarray(a), np.asarray(b)
    # Both relative and absolute tests are declared before observing any data.
    return float(np.max(np.abs(a-b) / np.maximum(np.abs(b), policy["absoluteFloor"])))


def compare(a, b, policy):
    require(a["identity"] == b["identity"], "Changed camera, masks, lighting, source, device or shader identity")
    result = {}
    for roi in a["rois"]:
        require(a["rois"][roi]["maskSha256"] == b["rois"][roi]["maskSha256"], "Changed ROI mask")
        result[roi] = {component: changed(a["rois"][roi][component], b["rois"][roi][component], policy)
                       for component in COMPONENTS}
    return result


def merge_errors(checks):
    return {roi: {c: max(row[roi][c] for row in checks) for c in COMPONENTS} for roi in checks[0]}


def qualified(errors, policy):
    return all(v < policy["relativeLimit"] for row in errors.values() for v in row.values())


def plan(state):
    p, records = state["recipe"], state["observations"]
    results = {}
    for view in p["views"]:
        n = p["initialCoastalSamples"] if view in COASTAL_VIEWS else (
            p["initialF4Samples"] if view.startswith("F4_") else p["initialSamples"])
        final = None
        for low, high in zip(p["depths"], p["depths"][1:]):
            while 2*n <= p["maximumSamples"]:
                wanted = [(d, s, seed) for d in (low, high) for s in (n, 2*n) for seed in SEEDS]
                for d, s, seed in wanted:
                    name = key(view, d, s, seed)
                    if name not in records:
                        return dict(status="Running", results=results, request=dict(
                            directory="captures/"+name, view=view, fixture=VIEWS[view], depth=d,
                            samples=s, seed=seed, batch_samples=p["batchSamples"], tile_pixels=0,
                            albedo=0, secondary_albedo=0, environment=3 if view in COASTAL_VIEWS else 0,
                            ev100=15 if view in COASTAL_VIEWS else 10))
                sampling = []
                for d in (low, high):
                    rows = [records[key(view, d, s, seed)] for s in (n, 2*n) for seed in SEEDS]
                    sampling.extend(compare(rows[a], rows[b], p) for a, b in ((0, 1), (2, 3), (0, 2), (1, 3)))
                sampling = merge_errors(sampling)
                if not qualified(sampling, p):
                    final = dict(status="ReferenceUnconverged", reason="SamplingLimit", depth=high,
                                 samples=2*n, sampling=sampling,
                                 roiStatus={roi: "ReferenceUnconverged" for roi in sampling})
                    n *= 2
                    continue
                depth = merge_errors([compare(records[key(view, low, 2*n, seed)],
                                              records[key(view, high, 2*n, seed)], p) for seed in SEEDS])
                final = dict(status="ReferenceConverged" if qualified(depth, p) else "ReferenceUnconverged",
                             reason="Qualified" if qualified(depth, p) else "DepthLimit", depth=high,
                             samples=2*n, seeds=list(SEEDS), lowerDepth=low, sampling=sampling, depthChange=depth)
                if final["status"] == "ReferenceConverged":
                    final["candidates"] = [key(view, high, 2*n, seed) for seed in SEEDS]
                    if view in COASTAL_VIEWS:
                        candidate = records[final["candidates"][0]]
                        final["signal"] = "incident-irradiance"
                        final["receiverConstraints"] = {roi: row["receiverConstraints"]
                                                        for roi, row in candidate["rois"].items()}
                final["roiStatus"] = {roi: "ReferenceConverged" if
                    max(sampling[roi].values()) < p["relativeLimit"] and
                    max(depth[roi].values()) < p["relativeLimit"] else "ReferenceUnconverged" for roi in sampling}
                break
            if final and (final["status"] == "ReferenceConverged" or final["reason"] == "SamplingLimit"):
                break
        results[view] = final or dict(status="ReferenceUnconverged", reason="SamplingLimit")
    return dict(status="ReferenceConverged" if all(r["status"] == "ReferenceConverged" for r in results.values())
                else "ReferenceUnconverged", results=results)


def file_receipts(directory):
    files = sorted(p for p in directory.rglob("*") if p.is_file() and p.suffix in (".npy", ".json", ".png"))
    return {str(p.relative_to(directory)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}


def summarize(root, case):
    masks = None
    if case["fixture"] in (12, 13):
        values, frame, masks = read_coastal_case(root, case)
    else:
        values, frame = read_case(root, case)
    actual = {p["id"].split(".", 1)[1]: p["value"] for p in frame["parameters"]["values"]
              if p["id"].startswith("gi_reference.")}
    require(actual.get("tile_pixels", 0) == case.get("tile_pixels", 0), "Wrong tile partition provenance")
    lighting = frame["parameters"]["lighting"]
    generation = lighting["active"]["generation"]
    require(generation == lighting["requested"]["generation"] ==
            frame["sceneInputs"]["environment"]["activeIBLGeneration"], "Stale World Lighting publication")
    if masks is None and case["fixture"] == 1:
        pos, identity = values["GI.Reference.Position"], values["GI.Reference.Identity"]
        masks = {"plane": (pos[..., 3] == 1) & (identity[..., 0] == 0) &
                 np.isin(identity[..., 1], [0, 1]) & (np.abs(pos[..., 0]) <= 1) & (np.abs(pos[..., 2]) <= 1)}
    elif masks is None:
        masks = transport_masks(case, values, frame)
    scene = copy.deepcopy(frame["sceneInputs"])
    scene["environment"].pop("activeIBLGeneration", None)
    # Generation values are ephemeral across safe Lab restarts. Effective inputs
    # and camera are stable identities; actual generations remain in each receipt.
    parameters = {k: v for k, v in actual.items() if k not in ("depth", "samples", "seed", "batch_samples", "tile_pixels")}
    inputs = dict(camera=frame["camera"], scene=scene, parameters=parameters,
                  extent=frame["temporal"]["renderExtent"], code=frame["codeIdentity"], shader=frame["shaderIdentity"])
    input_identity = digest(inputs)
    identity = digest(dict(inputIdentity=input_identity, backend=frame["backend"], adapter=frame["adapterDriverIdentity"]))
    rois = {}
    receiver_report = load(root/case["directory"]/"receiver-report.json") if case["fixture"] == 12 else None
    for name, mask in masks.items():
        require(mask.sum() >= 32, "Empty convergence ROI")
        row = dict(pixels=int(mask.sum()), maskSha256=hashlib.sha256(mask.tobytes()).hexdigest())
        if receiver_report is not None:
            row["receiverConstraints"] = receiver_report["masks"][name]
        for component in COMPONENTS:
            mean = values["GI.Reference."+component][mask, :3].mean(0, dtype=np.float64)
            row[component] = [*mean.tolist(), float(mean @ LUMA)]
        if case["fixture"] in (2, 3):
            covered = values["GI.Reference.Position"][..., 3] == 1
            require(np.all(values["GI.Reference.Irradiance"][covered, :3] == 0) and
                    np.all(values["GI.Reference.Diagnostics"][covered, 0] == 0), "Closed-room false sky/energy")
        if case["fixture"] == 1:
            require(np.max(np.abs(values["GI.Reference.Irradiance"][mask, :3]-np.pi)) < 1e-5,
                    "Constant furnace failed")
        rois[name] = row
    return dict(identity=identity, inputIdentity=input_identity, worldLightingGeneration=generation, rois=rois,
                case=case, files=file_receipts(root/case["directory"]))


def validate_recipe(p):
    require(p["schemaVersion"] == SCHEMA and p["views"] and len(set(p["views"])) == len(p["views"]) and
            all(v in VIEWS for v in p["views"]), "Invalid convergence recipe views/version")
    coastal = any(v in COASTAL_VIEWS for v in p["views"])
    require(all((v in COASTAL_VIEWS) == coastal for v in p["views"]), "Coastal and fixture extents cannot be mixed")
    if coastal:
        require(p["extent"] == [1280, 800] and
                p["inputsSha256"] == hashlib.sha256(INPUT_PATH.read_bytes()).hexdigest(), "Changed Coastal input lock/extent")
    require(p["depths"] in ([8, 16], [8, 16, 32], [8, 16, 32, 64],
                            [8, 16, 32, 64, 128], [8, 16, 32, 64, 128, 256]), "Invalid depth ladder")
    require(p["relativeLimit"] == .01 and p["absoluteFloor"] == 1e-6, "Qualification thresholds are frozen")
    require(1 <= p["initialSamples"]*2 <= p["maximumSamples"] <= 1048576 and
            1 <= p["initialF4Samples"]*2 <= p["maximumSamples"] and 1 <= p["batchSamples"] <= 4096,
            "Invalid sample limits")
    if coastal:
        require(1 <= p["initialCoastalSamples"]*2 <= p["maximumSamples"], "Invalid Coastal sample limits")


def validate_tiles(root):
    cases = load(root/"cases.json")
    references = {}
    checked = 0
    for case in cases:
        values, frame = read_case(root, case)
        actual = {p["id"]: p["value"] for p in frame["parameters"]["values"]}
        require(actual.get("gi_reference.tile_pixels", 0) == case["tile_pixels"], "Wrong actual tile partition")
        group = (frame["backend"], case["fixture"], case["depth"], case["samples"], case["seed"])
        if group in references:
            for name in values:
                require(np.array_equal(references[group][name], values[name]), "Tile/batch partition changed "+name)
            checked += 1
        else:
            references[group] = values
        if case["fixture"] == 1:
            covered = values["GI.Reference.Position"][..., 3] == 1
            require(np.max(np.abs(values["GI.Reference.Irradiance"][covered, :3]-np.pi)) < 1e-5,
                    "High-depth constant furnace failed")
        if case["fixture"] in (2, 3):
            covered = values["GI.Reference.Position"][..., 3] == 1
            require(np.all(values["GI.Reference.Irradiance"][covered, :3] == 0) and
                    np.all(values["GI.Reference.Diagnostics"][covered, 0] == 0), "High-depth sealed enclosure failed")
            mask = transport_masks(case, values, frame)["room"]
            require(mask.sum() >= 32 and values["GI.Reference.PathDiagnostics"][mask, 1].mean() > .5,
                    "High-depth room must exercise full paths even after throughput underflows")
        if case["fixture"] in (1, 2, 3):
            gate = next(g for g in frame["readiness"]["gates"] if g["name"] == "gi-reference")
            rows = (16777216 // (2*case["depth"]) // 640) // 8 * 8
            expected = (400 + rows - 1) // rows
            require(f"/{expected} submitted" in gate["detail"], "High depth did not use the declared bounded tiling")
    require(checked >= 4, "Insufficient independent tile/batch comparisons")
    print(f"GI tile partition checks passed ({len(cases)} captures, {checked} exact comparisons)")


def compare_rhi(root):
    states = [load(root/backend/"checkpoint.json") for backend in ("dx12", "vulkan")]
    require(states[0]["recipe"]["sourceIdentity"] == states[1]["recipe"]["sourceIdentity"] and
            states[0]["recipe"]["binaries"] == states[1]["recipe"]["binaries"], "Cross-RHI source/binary mismatch")
    reports = [load(root/backend/"report.json") for backend in ("dx12", "vulkan")]
    results = {}
    for view in states[0]["recipe"]["views"]:
        rows = [r["results"].get(view, {}) for r in reports]
        if any(r.get("status") != "ReferenceConverged" for r in rows) or any(
                rows[0].get(k) != rows[1].get(k) for k in ("depth", "samples")):
            results[view] = dict(status="Unavailable", reason="No common qualified depth/sample candidate")
            continue
        errors = []
        for seed in SEEDS:
            name = key(view, rows[0]["depth"], rows[0]["samples"], seed)
            a, b = [copy.deepcopy(s["observations"][name]) for s in states]
            require(a["inputIdentity"] == b["inputIdentity"], "Cross-RHI effective inputs differ")
            # Per-backend device identities are recorded independently, never equated.
            a["identity"] = b["identity"] = a["inputIdentity"]
            errors.append(compare(a, b, states[0]["recipe"]))
        errors = merge_errors(errors)
        require(qualified(errors, states[0]["recipe"]), "Cross-RHI ROI difference exceeds 1%")
        results[view] = dict(status="Passed", errors=errors)
    save(root/"cross-rhi.json", dict(schemaVersion=SCHEMA, results=results))
    print(json.dumps(results))


def self_test():
    p = dict(schemaVersion=SCHEMA, views=["F4_Floor"], depths=[8, 16, 32], initialSamples=2,
             initialF4Samples=2, maximumSamples=16, batchSamples=7, relativeLimit=.01, absoluteFloor=1e-6)
    validate_recipe(p)
    state = dict(recipe=p, observations={})
    for depth in (8, 16, 32):
        for n in (2, 4, 8):
            for seed in SEEDS:
                value = 1. if depth == 8 else 1.1
                if n == 2 and seed == SEEDS[1]:
                    value *= 1.1
                row = dict(maskSha256="fixed", **{c: [value]*4 for c in COMPONENTS})
                state["observations"][key("F4_Floor", depth, n, seed)] = dict(identity="same", rois={"floor": row})
    result = plan(state)
    require(result["status"] == "ReferenceConverged" and result["results"]["F4_Floor"]["samples"] == 8 and
            result["results"]["F4_Floor"]["lowerDepth"] == 16, "Doubling and shared-N depth ladder")
    del state["observations"][key("F4_Floor", 16, 8, SEEDS[1])]
    require(plan(state)["request"]["samples"] == 8, "Resume requires all seeds at common N")
    state["observations"].clear()
    require(plan(state)["status"] == "Running", "Completed Runtime is never implicitly qualified")
    a = dict(identity="a", rois={"x": dict(maskSha256="x", **{c: [0.]*4 for c in COMPONENTS})})
    require(qualified(compare(a, a, p), p), "Zero-energy channels")
    require(changed([2.], [1.], p) == 1. and changed([1.], [2.], p) == .5,
            "Expected-value normalization matches the retained lighting gates")
    b = copy.deepcopy(a)
    b["rois"]["x"]["SunIrradiance"][0] = 1e-5
    require(not qualified(compare(a, b, p), p), "A failing component/channel cannot hide in total luminance")
    b = copy.deepcopy(a)
    b["rois"]["x"]["maskSha256"] = "changed"
    try:
        compare(a, b, p)
        raise AssertionError("Changed mask accepted")
    except ValueError:
        pass
    b = copy.deepcopy(a)
    b["identity"] = "changed"
    try:
        compare(a, b, p)
        raise AssertionError("Changed identity accepted")
    except ValueError:
        pass
    with tempfile.TemporaryDirectory() as temp:
        f = Path(temp)/"state.json"
        save(f, state)
        require(load(f) == state, "Atomic checkpoint roundtrip")
        before = file_receipts(Path(temp))
        f.write_text("tampered", encoding="utf-8")
        require(before != file_receipts(Path(temp)), "Immutable receipt tamper detection")
    # Exhaustion never grants a certificate, even when Runtime work completed.
    p["depths"] = [8, 16]
    p["maximumSamples"] = 4
    for d in (8, 16):
        for n in (2, 4):
            for seed in SEEDS:
                state["observations"][key("F4_Floor", d, n, seed)] = dict(identity="same", rois={"floor": dict(
                    maskSha256="fixed", **{c: [float(d)]*4 for c in COMPONENTS})})
    require(plan(state)["status"] == "ReferenceUnconverged" and
            plan(state)["results"]["F4_Floor"]["reason"] == "DepthLimit", "Depth exhaustion is explicit")
    state["observations"][key("F4_Floor", 16, 4, SEEDS[1])]["rois"]["floor"]["SunIrradiance"] = [32.]*4
    require(plan(state)["results"]["F4_Floor"]["reason"] == "SamplingLimit", "Sampling exhaustion is explicit")
    coastal_self_test()
    coastal = dict(p, views=[COASTAL_VIEWS[0]], extent=[1280, 800], initialCoastalSamples=2,
                   inputsSha256=hashlib.sha256(INPUT_PATH.read_bytes()).hexdigest())
    validate_recipe(coastal)
    request = plan(dict(recipe=coastal, observations={}))['request']
    require(request['fixture'] == 12 and request['environment'] == 3 and request['ev100'] == 15,
            "Coastal recipe must select locked Physical Daylight")
    print("GI reference convergence self-tests passed (ladder, seeds, common N, identities, channels, limits, cache)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--initialize", action="store_true")
    parser.add_argument("--ingest", type=Path)
    parser.add_argument("--audit", action="store_true")
    parser.add_argument("--tiles", action="store_true")
    parser.add_argument("--compare-rhi", action="store_true")
    parser.add_argument("--check-recipe", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    root = args.directory
    if args.tiles:
        validate_tiles(root)
        return
    if args.compare_rhi:
        compare_rhi(root)
        return
    p = load(root/"recipe.json")
    validate_recipe(p)
    if args.check_recipe:
        require(digest(p) == digest(load(args.check_recipe)), "Changed recipe/source/binary/device")
        print(json.dumps(dict(status="RecipeMatches", recipeIdentity=digest(p))))
        return
    checkpoint = root/"checkpoint.json"
    if args.initialize and not checkpoint.exists():
        save(checkpoint, dict(schemaVersion=SCHEMA, recipe=p, recipeIdentity=digest(p), observations={}))
    state = load(checkpoint)
    require(state["schemaVersion"] == SCHEMA and state["recipeIdentity"] == digest(p), "Changed recipe/source/binary identity")
    if args.audit:
        for record in state["observations"].values():
            require(record["files"] == file_receipts(root/record["case"]["directory"]), "Modified/incomplete cached capture")
    if args.ingest:
        case = load(args.ingest)
        name = key(case["view"], case["depth"], case["samples"], case["seed"])
        require(name not in state["observations"], "Completed cache entries are immutable")
        state["observations"][name] = summarize(root, case)
        save(checkpoint, state)
    result = plan(state)
    result.update(schemaVersion=SCHEMA, recipeIdentity=state["recipeIdentity"], observations=len(state["observations"]),
                  runtimeCompletionIsConvergence=False)
    if not args.audit or args.initialize or args.ingest:
        save(root/"report.json", result)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
