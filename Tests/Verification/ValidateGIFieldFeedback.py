"""Previous-update feedback contracts and separately qualified static measurements.

No D=1 reference can certify feedback-on energy. The optional reference root must
contain independently sampling/depth-qualified observations from the same build.
"""
import argparse
import json
from pathlib import Path
import tempfile

import numpy as np

from ValidateGIFieldBaseline import (camera_inputs, scene_inputs, parameters, temporal_metrics,
                                    validate_readiness, digest)
from ValidateGIFieldLightStep import reference_source_identity
from ValidateGIFieldSampling import oracle
from ValidateGIProbeUpdate import directions, progressive_directions
from ValidateGIReferenceConvergence import file_receipts
from ValidateGIReferenceTransport import read_case, transport_masks

LUMA = np.array([.2126, .7152, .0722])


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def group(directory):
    receipt = load(directory/"receipt.json")
    require(receipt["status"] == "completed" and receipt["publication"] == "Published", "Unpublished feedback observation")
    paths = list(directory.glob("*.resource-export.json"))
    require(len(paths) == 1, "Expected one same-frame feedback group")
    record = load(paths[0])
    require(record["submission"]["completed"] and record["frameSerial"] == record["frame"]["frame"]["serial"],
            "Feedback data is not GPU-complete or frame-paired")
    arrays = {}
    for target in record["targets"]:
        a = np.load(directory/target["array"], allow_pickle=False)
        if a.dtype.names:
            require(a.dtype.names in (("record",), ("radianceDistance",)), "Unknown feedback buffer layout")
            a = a[a.dtype.names[0]]
        require(np.isfinite(a).all(), "Nonfinite feedback payload, including outside ROI")
        address = target["address"]
        selector = address.get("at")
        key = address["resource"], selector.get("before") if isinstance(selector, dict) else None
        require(key not in arrays, "Duplicate feedback resource address")
        arrays[key] = a
    return record["frame"], arrays


def value(arrays, name, before=None):
    return arrays[name, before]


def ledger(records, state, update):
    require(records.dtype == np.dtype("<u4") and records.shape == (state.size//4, 8), "Feedback uint ABI differs")
    require(update > 0 and np.all(records[:, 7] == update), "Mixed or stale feedback update")
    require(np.all(records[:, 5] == 0), "Incompatible field was consumed")
    require(np.all(records[:, 6] == np.any(records[:, :2] != 0, axis=1).astype(np.uint32)), "Wrong consumed age")
    require(np.all(records[:, :6].sum(axis=1) == np.rint(state.reshape(-1, 4)[:, 1]*256)),
            "Feedback support accounting does not cover every surface hit")
    if update == 1:
        require(np.count_nonzero(records[:, :4]) == 0, "Bootstrap consumed an undefined previous field")
    else:
        require(np.count_nonzero(records[:, 4]) == 0, "A prepared field silently reverted to bootstrap")
    return records[:, :6].sum(axis=0, dtype=np.uint64).tolist()


def check_graph(directory, enabled):
    graph = load(next(directory.glob("*.graph.json")))
    passes = {p["name"]: p for p in graph["compile"]["passes"] if not p["culled"]}
    trace, blend, borders = (passes[n] for n in ("GI.Field.TraceShade", "GI.Field.BlendShared", "GI.Field.Borders"))
    require(trace["executionOrder"] < blend["executionOrder"] < borders["executionOrder"], "Feedback update ordering differs")
    reads = {a["resource"]: a for a in trace["accesses"]}
    writes = {a["resource"]: a for a in blend["accesses"]}
    for name in ("GI.Field.Irradiance", "GI.Field.Distance", "GI.Field.Classification"):
        if enabled:
            require(reads[name]["dependencyAccess"] == "Read" and reads[name]["access"] == "Sample" and
                    reads[name]["writtenVersion"] is None and writes[name]["dependencyAccess"] == "ReadWrite" and
                    reads[name]["resourceNodeIndex"] == writes[name]["resourceNodeIndex"],
                    "Trace must sample the previous atlas before in-place Blend")
        else:
            require(name not in reads, "Feedback-off trace acquired an unnecessary atlas dependency")
    present = any(r["name"] == "GI.Field.Feedback" for r in graph["compile"]["resources"])
    require(present == enabled, "Feedback storage survived safe disable or was never declared")


def hash32(x):
    x &= 0xffffffff
    x = ((x ^ (x >> 16))*0x7feb352d) & 0xffffffff
    x = ((x ^ (x >> 15))*0x846ca68b) & 0xffffffff
    return x ^ (x >> 16)


def quaternion(seed):
    samples = []
    for _ in range(3):
        seed = hash32(seed+0x9e3779b9)
        samples.append(np.float32(((seed >> 9)+.5)/8388608))
    u, v, w = samples[0], 2*np.pi*samples[1], 2*np.pi*samples[2]
    return np.asarray([np.sqrt(1-u)*np.sin(v), np.sqrt(1-u)*np.cos(v),
                       np.sqrt(u)*np.sin(w), np.sqrt(u)*np.cos(w)], dtype=np.float32).astype(float)


def rotate(ray, q):
    return ray+2*np.cross(q[:3], np.cross(q[:3], ray)+q[3]*ray)


def field_directions(rays, update, seed, probe, progressive=False, rotating=True, rotation=None):
    index = update-1
    base = progressive_directions(rays, index if rotating else 0) if progressive else directions(rays)
    probe_phase = hash32(index) if rotating and not progressive else 0
    base = rotate(base, quaternion(hash32(probe ^ probe_phase)))
    if rotating:
        common = quaternion(seed ^ (index if not progressive else 0)) if rotation is None else rotation
        base = rotate(base, common)
    return base


def plane_equation(arrays, update, seed, progressive=False):
    rays = value(arrays, "GI.Field.Rays").reshape(-1, 256, 4)
    irradiance = value(arrays, "GI.Field.Irradiance", "GI.Field.TraceShade")
    distance = value(arrays, "GI.Field.Distance", "GI.Field.TraceShade")
    classes = value(arrays, "GI.Field.Classification", "GI.Field.TraceShade")
    maximum, checked = 0., 0
    # Well-inside-plane hits avoid geometric-edge/ray-distance conditioning while
    # still querying the independent FP64 eight-probe visibility/weight oracle.
    probes = [x+13*(z+13*y) for y in (7, 9, 11) for z in (4, 8) for x in (4, 8)]
    for probe in probes:
        origin = np.array([probe % 13, probe//(13*13), (probe//13) % 13])*.5-3
        ray = field_directions(256, update, seed, probe, progressive)
        hit = origin+ray*rays[probe, :, 3, None]
        keep = (ray[:, 1] < -.15) & (rays[probe, :, 3] > 0) & (rays[probe, :, 3] < 1000) & \
               (np.abs(hit[:, 1]) < .002) & (np.abs(hit[:, [0, 2]]) < .9).all(axis=1)
        if not keep.any():
            continue
        p = hit[keep]
        p[:, 1] = 0
        normal = np.tile([0., 1., 0.], (len(p), 1))
        energy, query = oracle(p, normal, normal, origin, np.array([-3., -3., -3.]), .5,
                               np.array([13, 13, 13]), irradiance, distance, classes)
        valid = np.isin(query[:, 2], [1, 2]) & (energy @ LUMA > 1e-5)
        # Frozen F1 uses a unit-diffuse white plane (F4 alone uses 0.8).
        expected = energy[valid]/np.pi
        actual = rays[probe, keep, :3][valid]
        if len(actual):
            maximum = max(maximum, float(np.max(abs(actual-expected)/np.maximum(abs(expected), 1e-5))))
            checked += len(actual)
    require(checked >= 32 and maximum <= .005, f"Feedback hit formula differs from previous-atlas FP64 lookup: {checked}, {maximum}")
    return dict(samples=checked, maximumRelativeError=maximum)


def check_policy(case, p):
    for key, parameter in (("progressiveRefresh", "field_progressive_refresh"),
                           ("progressiveSampling", "field_progressive_sampling")):
        expected, observed = case.get(key, False), p.get(parameter, False)
        require(type(expected) is bool and type(observed) is bool and expected == observed,
                "Feedback sampling/history policy differs from the declared case")
    require(not case.get("progressiveSampling") or case.get("progressiveRefresh"),
            "Progressive sampling feedback study requires progressive refresh")


def check_frame(frame, case, enabled):
    p = parameters(frame)
    check_policy(case, p)
    f = frame["readiness"]["giField"]
    view_matches = frame["camera"]["referenceView"] == case["view"] or (
        (frame.get("sequence") or {}).get("cameraPath") == "GI_Static_"+case["view"])
    require(p["fixture"] == case["fixture"] and p["field_enabled"] and p["field_sampling"] and
            p["field_feedback"] == enabled and p["field_readiness"] and p["receiver_diagnostics"] and
            p["albedo"] == p["secondary_albedo"] == p["environment"] == 0 and p["ev100"] == 10 and
            view_matches and frame["temporal"]["status"] == "disabled" and
            frame["temporal"]["renderExtent"] == frame["temporal"]["displayExtent"] == [320, 200] and
            not f["failed"] and f["summary"]["initialized"] and not f["summary"]["invalid"],
            "Wrong feedback inputs/view or invalid field")
    return p, f


def proof(directory, case):
    frame, arrays = group(directory)
    p, field = check_frame(frame, case, True)
    check_graph(directory, True)
    raw = value(arrays, "GI.Field.Feedback")
    require(raw.dtype == np.dtype("<u4") and raw.shape == (2*int(np.prod(case["counts"])), 4), "Missing feedback records")
    records = raw.reshape(-1, 8)
    update = int(records[0, 7])
    totals = ledger(records, value(arrays, "GI.Field.State"), update)
    readiness = value(arrays, "GI.Field.Readiness")
    require(np.all(readiness[2::2, 3].view(np.uint32)+field["summary"]["field"]["epochOriginUpdate"] == update),
            "Feedback and readiness came from different GPU updates")
    equation = None
    if case["id"] == "plane":
        equation = plane_equation(arrays, update, p["seed"], case.get("progressiveSampling", False))
    return dict(update=update, sourceUpdate=update-1, totals=totals, state=field["state"], equation=equation), frame


def static_window(directory, case):
    paths = sorted((directory/"samples").glob("*.resource-export.json"),
                   key=lambda p: load(p)["frame"]["sequence"]["frame"])
    require(len(paths) == 120, "Feedback stability requires 120 consecutive observations")
    samples, previous, frozen, position = [], None, None, None
    for index, path in enumerate(paths):
        record = load(path)
        frame = record["frame"]
        _, field = check_frame(frame, case, True)
        require(record["submission"]["completed"] and frame["sequence"]["frame"] == index, "Incomplete/gapped feedback sequence")
        arrays = {t["address"]["resource"]: np.load(path.parent/t["array"], allow_pickle=False) for t in record["targets"]}
        for a in arrays.values():
            require(np.isfinite(a[a.dtype.names[0]] if a.dtype.names else a).all(), "Nonfinite static-window payload")
        ready = arrays["GI.Field.Readiness"]
        ready = ready["record"] if ready.dtype.names else ready
        update = int(ready[2, 3].view(np.uint32))+field["summary"]["field"]["epochOriginUpdate"]
        require(np.all(ready[2::2, 3].view(np.uint32)+field["summary"]["field"]["epochOriginUpdate"] == update), "Mixed probe updates")
        validate_readiness(field, previous, update)
        identity = digest(dict(camera=camera_inputs(frame), scene=scene_inputs(frame), parameters=parameters(frame)))
        require(frozen is None or identity == frozen, "Feedback static inputs changed")
        frozen = identity
        previous = field, update
        current_position = arrays["GI.Receiver.Position"][0]
        require(position is None or np.array_equal(position, current_position), "Static receivers moved")
        position = current_position
        samples.append(arrays["GI.Field.ReceiverIrradiance"][0, ..., :3])
    return samples, position, frame


def timing(directory, filename="timing.json"):
    result = load(directory/filename)
    publication = result["sequence"]["timingPublication"]
    require(result["sequence"]["state"] == "completed" and publication["outcome"] == "Published", "Missing complete timing range")
    manifest = load(publication["manifest"])
    products = [p for p in manifest["products"] if p.get("schema", "").startswith("gglab.gpu-timing")]
    require(len(products) == 1, "Timing product missing")
    document = load(Path(publication["manifest"]).parent/products[0]["metadata"])
    scopes = [s for s in document["scopes"] if s["kind"] == "pass" and
              (s["name"].startswith("GI.Field.") or s["name"].startswith("GI.Receiver"))]
    require(any(s["name"] == "GI.Field.TraceShade" for s in scopes), "Timing lacks the full field update")
    totals = np.zeros(len(document["frame"]["milliseconds"]))
    for scope in scopes:
        require(len(scope["sampleIndices"]) == len(totals), "Timing omitted a GI pass/sample")
        totals[scope["sampleIndices"]] += scope["milliseconds"]
    return dict(updateAndReceiverMedianMilliseconds=float(np.median(totals)),
                passMedianMilliseconds={s["name"]: float(np.median(s["milliseconds"])) for s in scopes})


def check_reference_cohort(recipe, source, binaries):
    require(recipe["extent"] == [320, 200] and
            recipe["binaries"] == {r["file"]: r["sha256"] for r in binaries} and
            recipe["sourceIdentity"] == reference_source_identity(source),
            "Feedback/reference source, binary or extent cohort differs")


def quality(samples, position, frame, case, backend, reference_root, binaries, source):
    if case["id"] == "sunroom":
        if reference_root is None:
            return dict(qualified=False, reason="No depth-converged reference supplied")
        root = reference_root/backend
        report, state = load(root/"report.json"), load(root/"checkpoint.json")
        reference = report["results"]["F4_Floor"]
        require(reference["status"] == "ReferenceConverged", "Feedback reference is not sampling/depth qualified")
        check_reference_cohort(state["recipe"], source, binaries)
        means, masks = [], None
        for candidate in reference["candidates"]:
            row = state["observations"][candidate]
            require(row["files"] == file_receipts(root/row["case"]["directory"]), "Reference capture was modified")
            arrays, ref_frame = read_case(root, row["case"])
            reference_position = arrays["GI.Reference.Position"]
            covered = position[..., 3] == 1
            require(camera_inputs(frame) == camera_inputs(ref_frame) and scene_inputs(frame) == scene_inputs(ref_frame) and
                    frame["adapterDriverIdentity"] == ref_frame["adapterDriverIdentity"] and
                    not parameters(ref_frame)["field_enabled"] and not parameters(ref_frame)["field_feedback"] and
                    np.array_equal(position[..., 3], reference_position[..., 3]) and
                    np.max(np.abs(position[covered, :3]-reference_position[covered, :3])) < 2e-4,
                    "Feedback/reference receivers or inputs differ")
            masks = transport_masks(row["case"], arrays, ref_frame)
            means.append({k: arrays["GI.Reference.Irradiance"][mask, :3].mean(axis=0, dtype=float) for k, mask in masks.items()})
        references = {k: np.mean([r[k] for r in means], axis=0) for k in masks}
    elif case["id"] == "coverage":
        return dict(qualified=False, reason="Coverage is a query-contract control, not a reference quality certificate")
    else:
        covered = position[..., 3] == 1
        if case["id"] == "plane":
            covered &= (np.abs(position[..., 0]) <= 1) & (np.abs(position[..., 2]) <= 1)
        masks = {case["id"]: covered}
        references = {case["id"]: np.full(3, np.pi if case["id"] == "plane" else 0.)}
    rois = {}
    for name, mask in masks.items():
        require(mask.sum() >= 32, "Empty feedback ROI")
        reference = references[name]
        result = temporal_metrics([s[mask].mean(axis=0, dtype=float) for s in samples], reference)
        energy = result["maximumEnergyError"] <= (.01 if case["id"] == "plane" else .1)
        if np.max(np.abs(reference)) == 0:
            energy = result["maximumLuminance"] <= 1e-6
        result["passed"] = energy and result["temporalStabilityPassed"] and (
            result["chromaticityError"] is None or result["chromaticityError"] <= .02)
        rois[name] = result
    return dict(qualified=all(r["passed"] for r in rois.values()), rois=rois)


def check_settle(receipt, generation, update):
    require(receipt["generation"] == generation and receipt["settleUpdates"] >= 512 and
            receipt["minimumCompletedUpdates"] == receipt["startCompletedUpdates"]+receipt["settleUpdates"] and
            receipt["completedUpdates"] >= receipt["minimumCompletedUpdates"] and
            update >= receipt["completedUpdates"], "Feedback-off observation did not complete its settling updates")


def validate(root, reference_root):
    rows = []
    binaries = load(root/"binaries.json")
    source = load(root/"source-identity.json")
    for backend in load(root/"backends.json"):
        for case in load(root/"cases.json"):
            directory = root/backend/case["id"]
            off, off_arrays = group(directory/"off")
            disabled, disabled_arrays = group(directory/"disabled")
            for name, frame, arrays in (("off", off, off_arrays), ("disabled", disabled, disabled_arrays)):
                _, field = check_frame(frame, case, False)
                ready = value(arrays, "GI.Field.Readiness")
                updates = ready[2::2, 3].view(np.uint32)+field["summary"]["field"]["epochOriginUpdate"]
                require(np.all(updates == updates[0]), "Mixed feedback-off probe updates")
                check_settle(load(directory/name/"settle.json"), field["generation"], int(updates[0]))
            check_graph(directory/"off", False)
            check_graph(directory/"disabled", False)
            warm, warm_frame = proof(directory/"warming", case)
            measured, frame = proof(directory/"proof", case)
            generations = [f["readiness"]["giField"]["generation"] for f in (off, warm_frame, frame, disabled)]
            require(all(b > a for a, b in zip(generations, generations[1:])), "Feedback toggle reused old field/history")
            if case["id"] == "plane":
                require(warm["state"] != "Ready" and sum(warm["totals"][:2]) > 0, "Feedback was gated by readiness")
            if case["id"] == "unsupported":
                require(measured["totals"][2] > 0, "Unsupported feedback was not exercised")
            if case["id"] == "coverage":
                require(measured["totals"][3] > 0, "Outside-coverage feedback was not exercised")
            samples, position, sample_frame = static_window(directory, case)
            q = quality(samples, position, sample_frame, case, backend, reference_root, binaries, source)
            rows.append(dict(backend=backend, case=case["id"], feedback=measured, warmup=warm,
                             quality=q, onTiming=timing(directory), offTiming=timing(directory, "off-timing.json")))
    cross = []
    for case in load(root/"cases.json"):
        pair = [r for r in rows if r["case"] == case["id"] and r["quality"].get("rois")]
        if len(pair) == 2:
            for roi in pair[0]["quality"]["rois"]:
                a, b = (np.asarray(r["quality"]["rois"][roi]["mean"]) for r in pair)
                error = abs((a-b) @ LUMA)/max(abs(b @ LUMA), 1e-6)
                cross.append(dict(case=case["id"], roi=roi, relativeDifference=float(error), passed=bool(error <= .02)))
    eligible = [r for r in rows if r["case"] != "coverage"]
    has_reference = bool(eligible) and (reference_root is not None or all(r["case"] != "sunroom" for r in eligible))
    passed = all(r["quality"]["qualified"] for r in rows if r["case"] != "coverage") and all(r["passed"] for r in cross)
    available_passed = all(r["quality"]["qualified"] for r in rows if "rois" in r["quality"]) and all(r["passed"] for r in cross)
    return dict(schemaVersion=1, contractsPassed=True, staticQualityQualified=bool(has_reference and passed),
                qualityGatesPassed=bool(passed), availableQualityGatesPassed=bool(available_passed),
                referenceProvided=reference_root is not None,
                performanceQualified=False, wholeG3Qualified=False, results=rows, crossRhi=cross)


def self_test():
    state = np.array([[0., .5, .5, 0.]])
    record = np.array([[100, 28, 0, 0, 0, 0, 1, 7]], dtype=np.uint32)
    ledger(record, state, 7)
    rejected = 0
    for column, val in ((0, 99), (5, 1), (6, 0), (7, 6), (4, 1)):
        bad = record.copy()
        bad[0, column] = val
        try:
            ledger(bad, state, 7)
        except ValueError:
            rejected += 1
    require(rejected == 5, "Malformed feedback ledger was accepted")
    settled = dict(generation=7, startCompletedUpdates=3, settleUpdates=512,
                   minimumCompletedUpdates=515, completedUpdates=515)
    check_settle(settled, 7, 516)
    rejected = 0
    for receipt, generation, update in ((settled, 8, 516), (settled, 7, 514),
            (dict(settled, completedUpdates=514), 7, 516),
            (dict(settled, minimumCompletedUpdates=514), 7, 516)):
        try:
            check_settle(receipt, generation, update)
        except ValueError:
            rejected += 1
    require(rejected == 4, "A stale or incomplete feedback settling receipt was accepted")
    bootstrap = np.array([[0, 0, 0, 0, 128, 0, 0, 1]], dtype=np.uint32)
    ledger(bootstrap, state, 1)
    require(np.max(abs(np.linalg.norm(rotate(directions(256), quaternion(7)), axis=1)-1)) < 1e-6, "Invalid oracle rotation")
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        np.save(root/"payload.npy", np.zeros((1, 4), dtype=np.float32))
        (root/"receipt.json").write_text(json.dumps(dict(status="completed", publication="Published")))
        record = dict(frameSerial=9, frame=dict(frame=dict(serial=9)), submission=dict(completed=True), targets=[
            dict(address=dict(resource="test", at="final"), array="payload.npy"),
            dict(address=dict(resource="test", at=dict(before="Trace")), array="payload.npy")])
        (root/"test.resource-export.json").write_text(json.dumps(record))
        _, arrays = group(root)
        require(set(arrays) == {("test", None), ("test", "Trace")}, "Default and named resource selectors collided")
        record["submission"]["completed"] = False
        (root/"test.resource-export.json").write_text(json.dumps(record))
        try:
            group(root)
            raise AssertionError("Uncompleted feedback data was accepted")
        except ValueError:
            pass
    candidate = dict(progressiveRefresh=True, progressiveSampling=True)
    check_policy({}, {})
    check_policy(candidate, dict(field_progressive_refresh=True, field_progressive_sampling=True))
    for case, p in ((candidate, {}), ({}, dict(field_progressive_sampling=True)),
                    (dict(progressiveSampling=True), dict(field_progressive_sampling=True)),
                    (candidate, dict(field_progressive_refresh=True, field_progressive_sampling=1))):
        try:
            check_policy(case, p)
            raise AssertionError("Mismatched or implicit feedback policy was accepted")
        except ValueError:
            pass
    old = rotate(rotate(directions(256), quaternion(hash32(1445 ^ hash32(529)))), quaternion(61052 ^ 529))
    require(np.array_equal(field_directions(256, 530, 61052, 1445), old),
            "Legacy feedback directions changed")
    require(not np.array_equal(field_directions(256, 530, 61052, 1445, True), old),
            "Progressive oracle reused legacy directions")
    source = [dict(path="Shaders/Passes/Test.hlsl", sha256="a"*64)]
    binaries = [dict(file="test.exe", sha256="b"*64)]
    recipe = dict(extent=[320, 200], binaries={"test.exe": "b"*64},
                  sourceIdentity=reference_source_identity(source))
    check_reference_cohort(recipe, source, binaries)
    for changed in (dict(recipe, sourceIdentity="c"*64), dict(recipe, binaries={"test.exe": "c"*64}),
                    dict(recipe, extent=[640, 400])):
        try:
            check_reference_cohort(changed, source, binaries)
            raise AssertionError("Mismatched feedback/reference cohort was accepted")
        except ValueError:
            pass
    print(json.dumps(dict(ok=True, checks=27)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--reference-root", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        require(args.root is not None, "A feedback run directory is required")
        result = validate(args.root, args.reference_root)
        (args.root/"validation.json").write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
        print(json.dumps(dict(contractsPassed=True, staticQualityQualified=result["staticQualityQualified"])))
        # Without a reference, contract-only results remain explicitly unqualified.
        raise SystemExit(0 if result["availableQualityGatesPassed"] and
                         (result["qualityGatesPassed"] or not result["referenceProvided"]) else 1)
