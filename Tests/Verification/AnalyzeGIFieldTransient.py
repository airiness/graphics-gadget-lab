"""Read-only small-signal GI attribution; never an independent-reference certificate.

Reconstruct receivers and per-probe observations from published artifacts, retaining
own-endpoint failures. Optional eight-slot exports separate atlas and normalized
query-weight changes by an exact symmetric two-factor identity, and check the
refresh Trace -> Blend equation against an FP64 integration of exported rays.
"""
import argparse
import csv
import hashlib
import itertools
import json
import shutil
from pathlib import Path

import numpy as np

from ValidateGIFieldFeedback import hash32, quaternion, rotate
from ValidateGIFieldLightStep import (EVENT_FRAME, FLOOR, HOLD, LUMA, load, read_group,
    require, invariant_inputs, observation_targets, progressive_hysteresis, transient_case, validate_case)
from ValidateGIFieldLightStepReference import roi_masks
from ValidateGIFieldReadiness import records
from ValidateGIFieldSampling import bilinear, oracle
from ValidateGIProbeUpdate import bordered, directions, octahedral

# Owning F4 descriptor in GIReferenceLabSession.cpp, not a fitted diagnostic bias.
VIEW_BIAS = .15


def save(path, result):
    Path(path).write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")


def receipt(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024*1024), b""):
            h.update(block)
    return dict(path=str(Path(path).resolve()), sha256=h.hexdigest())


def array(path, target):
    return np.load(path.parent/target["array"], allow_pickle=False)


def geometry(directory):
    paths = list(Path(directory).glob("*.resource-export.json"))
    require(len(paths) == 1, "One completed paired geometry capture is required")
    path = paths[0]
    group = read_group(path)
    targets = {t["address"]["resource"]: t for t in group["targets"]}
    require(set(targets) == {"GI.Receiver."+s for s in ("Position", "VertexNormal", "ShadingNormal")},
            "Missing paired receiver geometry/normals")
    values = [array(path, targets["GI.Receiver."+s])[0] for s in ("Position", "VertexNormal", "ShadingNormal")]
    require(all(a.shape == (200, 320, 4) and np.isfinite(a).all() for a in values), "Invalid geometry ABI")
    masks = roi_masks(values[0][None])
    union = np.logical_or.reduce(list(masks.values()))
    p, vertex, normal = [a[union, :3].astype(float) for a in values]
    require(np.allclose(np.linalg.norm(vertex, axis=1), 1, atol=1e-5) and
            np.allclose(np.linalg.norm(normal, axis=1), 1, atol=1e-5), "Invalid receiver normals")
    summary = group["frame"]["readiness"]["giField"]["summary"]
    camera = np.asarray(group["frame"]["camera"]["position"])
    view = camera-p
    view /= np.linalg.norm(view, axis=1)[:, None]
    cage = ((p+summary["spacing"]*(.1*vertex+VIEW_BIAS*view)-summary["minimum"])/summary["spacing"])
    counts = np.asarray(summary["counts"])
    lower = np.minimum(np.floor(cage.clip(0, counts-1)).astype(int), counts-2)
    first, last = int(lower[:, 1].min()), int(lower[:, 1].max()+1)
    return dict(frame=group["frame"], position=p, vertex=vertex, normal=normal, union=union,
                masks={k: m[union] for k, m in masks.items()},
                camera=camera, summary=summary, atlasSlices=[first, last-first+1])


def lookup(g, irradiance, distance, classes, slices):
    """Per-pixel, per-corner M and normalized weights, without field fallback."""
    p, normal, vertex, summary = (g[k] for k in ("position", "normal", "vertex", "summary"))
    minimum, spacing, counts = np.asarray(summary["minimum"]), summary["spacing"], np.asarray(summary["counts"])
    require(slices == g["atlasSlices"], "Exported layers do not cover the exact planned receiver cages")
    interior = summary["irradianceInteriorWidth"]
    require(irradiance.shape == (slices[1], counts[2]*(interior+2), counts[0]*(interior+2), 4) and
            distance.shape == (slices[1], counts[2]*16, counts[0]*16, 2) and
            classes.shape == (slices[1], counts[2], counts[0], 1) and
            all(np.isfinite(a).all() for a in (irradiance, distance, classes)), "Invalid sliced field ABI")
    grid = (p-minimum)/spacing
    edge = np.minimum(grid, counts-1-grid).min(axis=1).clip(0, 1)
    require((edge > 0).all(), "A required receiver left field coverage")
    view = g["camera"]-p
    view /= np.linalg.norm(view, axis=1)[:, None]
    biased = p+spacing*(.1*vertex+VIEW_BIAS*view)
    cage = ((biased-minimum)/spacing).clip(0, counts-1)
    lower = np.minimum(np.floor(cage).astype(int), counts-2)
    fraction = (cage-lower).clip(0, 1)
    weights, atlas, probes = [], [], []
    for offset in itertools.product((0, 1), repeat=3):
        offset = np.asarray(offset)
        probe = lower+offset
        local = probe-np.array([0, slices[0], 0])
        require(((local[:, 1] >= 0) & (local[:, 1] < slices[1])).all(), "An unexported cage corner was used")
        origin = minimum+spacing*probe
        toward = origin-p
        toward /= np.maximum(np.linalg.norm(toward, axis=1), 1e-10)[:, None]
        weight = np.where(offset, fraction, 1-fraction).prod(axis=1)*(((toward*normal).sum(axis=1)+1)**2*.25+.2)
        vector = biased-origin
        length = np.linalg.norm(vector, axis=1)
        direction = np.where((length > 1e-10)[:, None], vector/np.maximum(length, 1e-10)[:, None], normal)
        # In FP64 the total-variance filtering identity equals filtered E[d^2]-E[d]^2.
        moments = bilinear(distance, local, direction, 14)
        variance = np.maximum(moments[:, 1]-moments[:, 0]**2, 1e-4*spacing**2)
        delta = np.maximum(length-moments[:, 0], 0)
        weight *= (variance/(variance+delta**2))**3
        weight = np.where(weight < .2, weight**3/.2**2, weight)
        classification = classes[local[:, 1], local[:, 2], local[:, 0], 0]
        require(np.isin(classification, (1, 2)).all(), "Invalid/unclassified receiver cage")
        weights.append(weight*(classification == 1))
        atlas.append(bilinear(irradiance, local, normal, interior)[:, :3])
        probes.append(probe[:, 0]+counts[0]*(probe[:, 2]+counts[2]*probe[:, 1]))
    weights = np.stack(weights, axis=1)
    total = weights.sum(axis=1)
    normalized = weights/np.where(total > 0, total, 1)[:, None]
    atlas = np.stack(atlas, axis=1)
    energy = np.pi*(normalized[..., None]*atlas).sum(axis=1)
    support = np.where(total == 0, 3, np.where(total < .05, 2, 1))
    return dict(weights=normalized, atlas=atlas, energy=energy, total=total,
                support=support, probes=np.stack(probes, axis=1))


def split_change(current, baseline):
    require(np.array_equal(current["probes"], baseline["probes"]), "Receiver cage topology changed")
    a, b = current["weights"], baseline["weights"]
    x, y = current["atlas"], baseline["atlas"]
    atlas = np.pi*(a+b)[..., None]*.5*(x-y)
    weights = np.pi*(x+y)*.5*(a-b)[..., None]
    require(np.allclose((atlas+weights).sum(axis=1), current["energy"]-baseline["energy"],
                        rtol=1e-10, atol=1e-8), "Two-factor decomposition did not close")
    return atlas @ LUMA, weights @ LUMA


def blend_expected(previous, integrated, hysteresis):
    require(0 <= hysteresis < 1, "Invalid blend coefficient")
    return hysteresis*previous+(1-hysteresis)*integrated


def trace_error(actual, expected):
    error = abs(actual-expected)
    scaled = error/np.maximum(abs(expected), FLOOR)
    index = np.unravel_index(scaled.argmax(), scaled.shape)
    maximum = float(scaled[index])
    return dict(maximumRelativeError=maximum, maximumAbsoluteError=float(error.max()),
                arithmeticPassed=maximum <= .005, budget=.005, normalizationFloor=FLOOR,
                worst=dict(texel=int(index[0]), channel=int(index[1]),
                           actual=float(actual[index]), expected=float(expected[index])))


def integrated_probe(g, rays, update, seed, probe):
    """FP64 quadrature of exported rays, independent of atlas blend/history."""
    counts = np.asarray(g["summary"]["counts"])
    interior = g["summary"]["irradianceInteriorWidth"]
    require(1 <= update <= 0xffffffff and 0 <= seed <= 0xffffffff and
            0 <= probe < counts.prod(), "Invalid trace sampling identity")
    x, y, z = int(probe % counts[0]), int(probe//(counts[0]*counts[2])), int((probe//counts[0]) % counts[2])
    direction = rotate(rotate(directions(rays.shape[1]), quaternion(hash32(int(probe) ^ hash32(update-1)))),
                       quaternion(seed ^ (update-1)))
    weight = np.maximum(octahedral(interior).reshape(-1, 3) @ direction.T, 0)
    integrated = (weight @ rays[probe, :, :3])/np.maximum(weight.sum(axis=1)[:, None], 1e-20)
    target = (y-g["atlasSlices"][0], slice(z*(interior+2)+1, z*(interior+2)+1+interior),
              slice(x*(interior+2)+1, x*(interior+2)+1+interior))
    return target, integrated


def trace_receiver(g, arrays, update, seed, current):
    """Reconstruct every required receiver from all contributing probes' rays."""
    counts = np.asarray(g["summary"]["counts"])
    interior = g["summary"]["irradianceInteriorWidth"]
    rays = np.ascontiguousarray(arrays["Rays"]).view("<f4").reshape(int(counts.prod()), -1, 4)
    require(np.isfinite(rays).all() and rays.shape[1] == 256, "Unexpected/nonfinite tracing recipe")
    require(np.allclose(current["weights"].sum(axis=1), 1, rtol=0, atol=1e-12),
            "A required receiver has no normalized probe support")
    selected = np.unique(current["probes"][current["weights"] > 0])
    atlas = np.zeros_like(arrays["Irradiance"], dtype=float)
    for probe in selected:
        target, integrated = integrated_probe(g, rays, update, seed, probe)
        require(0 <= target[0] < len(atlas), "A contributing probe is outside exported layers")
        tile = bordered(integrated.reshape(interior, interior, 3))
        atlas[target[0], target[1].start-1:target[1].stop+1,
              target[2].start-1:target[2].stop+1, :3] = tile
    # Keep the real GPU distance/classification weights and the same octahedral
    # seams. Axis-aligned receiver normals sample both interior and border texels.
    reconstructed = lookup(g, atlas, arrays["Distance"], arrays["Classification"], g["atlasSlices"])
    require(np.array_equal(current["weights"], reconstructed["weights"]),
            "Ray reconstruction changed normalized query weights")
    return reconstructed["energy"], len(selected)


def trace_check(g, arrays, update, seed, selected, hysteresis=.5):
    """Check selected probes with the coefficient of this actual GPU update."""
    require(0 <= hysteresis < 1, "Invalid observed integration coefficient")
    summary = g["summary"]
    counts = np.asarray(summary["counts"])
    interior = summary["irradianceInteriorWidth"]
    rays = np.ascontiguousarray(arrays["Rays"]).view("<f4").reshape(int(counts.prod()), -1, 4)
    require(np.isfinite(rays).all() and rays.shape[1] == 256, "Unexpected/nonfinite tracing recipe")
    old, new = arrays["Previous"], arrays["Irradiance"]
    maximum, absolute, checked, worst = 0., 0., 0, None
    raw_means = []
    for probe in selected:
        target, integrated = integrated_probe(g, rays, update, seed, probe)
        previous = old[target][..., :3].reshape(-1, 3).astype(float)
        actual = new[target][..., :3].reshape(-1, 3).astype(float)
        expected = blend_expected(previous, integrated, hysteresis)
        error = trace_error(actual, expected)
        if error["maximumRelativeError"] > maximum:
            maximum = error["maximumRelativeError"]
            worst = dict(probe=int(probe), **error["worst"])
        absolute = max(absolute, error["maximumAbsoluteError"])
        radiance = rays[probe, :, :3] @ LUMA
        raw_means.append(dict(probe=int(probe), integratedMeanE=float((integrated @ LUMA).mean()*np.pi),
            previousMeanE=float((previous @ LUMA).mean()*np.pi), blendedMeanE=float((actual @ LUMA).mean()*np.pi),
            brightestRayLuminanceShare=None if radiance.sum() == 0 else float(radiance.max()/radiance.sum())))
        checked += len(actual)
    require(checked > 0, "No trace/blend texels checked")
    # Aggregate, do not hide or abort on the first failed component. The original
    # budget is unchanged; any failure keeps the analysis exit status nonzero.
    return dict(maximumRelativeError=maximum, maximumAbsoluteError=absolute, worst=worst,
                arithmeticPassed=maximum <= .005, budget=.005, normalizationFloor=FLOOR,
                texels=checked, probes=raw_means, hysteresis=hysteresis)


def integration_hysteresis(case, field, global_update):
    require(field["refreshing"], "Trace attribution requires an active refresh")
    if not case.get("progressiveAttribution"):
        require(field["hysteresis"] == .5, "Unproven historical refresh coefficient")
        return .5
    require(case.get("progressiveRefresh") and not case.get("transientAttribution"),
            "Progressive attribution policy is missing or mixed")
    epoch_update = global_update-field["summary"]["field"]["epochOriginUpdate"]
    require(epoch_update >= 1 and epoch_update > field["completedUpdates"],
            "GPU integration update is not paired with the completed CPU snapshot")
    if field["completedUpdates"]:
        require(abs(field["hysteresis"]-progressive_hysteresis(field["completedUpdates"])) < 1e-6,
                "Completed progressive coefficient does not match actual history")
    return progressive_hysteresis(epoch_update)


def details(path, g, case):
    record = read_group(path)
    arrays = {}
    for t in record["targets"]:
        name = t["address"]["resource"].removeprefix("GI.Field.")
        if name == "Readiness":
            continue
        if isinstance(t["address"]["at"], dict):
            require(t["address"]["at"] == dict(before="GI.Field.BlendShared"), "Wrong pre-blend atlas")
            name = "Previous"
        if name in ("Previous", "Irradiance", "Distance", "Classification"):
            require(t["address"]["slices"] == case["atlasSlices"], "Changed atlas slice address")
        arrays[name] = array(path, t)
    expected = lookup(g, arrays["Irradiance"], arrays["Distance"], arrays["Classification"], case["atlasSlices"])
    actual = arrays["ReceiverIrradiance"][0, g["union"], :3]
    query = arrays["ReceiverQuery"][0, g["union"]]
    require(np.isfinite(arrays["ReceiverQuery"]).all() and
            (arrays["ReceiverIrradiance"][0, g["union"], 3] == 1).all() and (query[:, 3] == 1).all(),
            "Invalid receiver compatibility markers")
    error = float(np.max(abs(actual-expected["energy"])/np.maximum(abs(expected["energy"]), FLOOR)))
    weight_error = float(np.max(abs(query[:, 0]-expected["total"])))
    require(error <= .001 and weight_error <= 1e-4 and np.array_equal(query[:, 2], expected["support"]),
            f"FP64 receiver closure failed: energy={error}, weight={weight_error}")
    return record, arrays, expected, dict(energyRelativeError=error, weightAbsoluteError=weight_error)


def analyze_case(root, case):
    result = validate_case(root, case)
    directory = root/case["directory"]
    groups = sorted((directory/"samples").glob("*.resource-export.json"))
    by_frame = {read_group(p)["frame"]["sequence"]["frame"]: p for p in groups}
    probe_values = []
    for i in range(360):
        r = read_group(by_frame[i])
        target = next(t for t in r["targets"] if t["address"]["resource"] == "GI.Field.Readiness")
        probe_values.append(records(by_frame[i].parent/target["array"])[2::2, :3].copy())
    probe_values = np.asarray(probe_values)
    old_probe = probe_values[:EVENT_FRAME, :, 0].mean(axis=0)
    peaks = {}
    for roi, values in result["roiSeries"].items():
        metric = result["rois"][roi]
        values = np.asarray(values)
        sign = np.sign(metric["new"]-metric["old"])
        peak = int(result["t0"]+np.argmax(sign*(values[result["t0"]:]-metric["new"])))
        deviation = float(sign*(values[peak]-metric["new"]))
        delta = sign*(probe_values[peak, :, 0]-old_probe)
        active = probe_values[peak, :, 2] == 1
        ids = np.flatnonzero(active)
        top = ids[np.argsort(delta[ids])[-5:][::-1]]
        field = read_group(by_frame[peak])["frame"]["readiness"]["giField"]
        peaks[roi] = dict(frame=peak, sinceConsumed=peak-result["t0"], globalUpdate=result["globalUpdates"][peak],
            directionalDeviation=deviation, stepNormalized=deviation/metric["amplitude"],
            endpointNormalized=deviation/max(abs(metric["new"]), FLOOR), state=field["state"],
            refreshing=field["refreshing"], targetsPassed=metric["targetsPassed"],
            # Population evidence only: these are NOT identified receiver contributions.
            populationTopDirectionalProbeChanges=[dict(probe=int(p), deltaE=float(delta[p])) for p in top])
    result["peaks"] = peaks
    result["attributionStatus"] = "PopulationOnlyNoQueryOrTraceExports"
    result["actualFirstConsumedUpdate"] = result["globalUpdates"][result["t0"]]
    if transient_case(case):
        g = geometry(directory/"mask")
        require(case["atlasSlices"] == g["atlasSlices"], "Changed cage layer plan")
        require(invariant_inputs(g["frame"]) == invariant_inputs(read_group(by_frame[0])["frame"]),
                "Geometry capture has different effective inputs")
        _, baseline_arrays, baseline, closure = details(by_frame[359], g, case)
        endpoint_offsets = {roi: float((baseline["energy"][mask] @ LUMA).mean()-result["rois"][roi]["new"])
                            for roi, mask in g["masks"].items()}
        rows = []
        maximum_closure = closure["energyRelativeError"]
        maximum_trace = 0.
        previous_atlas = None
        for i in range(case.get("attributionStartFrame", 29), 81):
            record, arrays, current, closure = details(by_frame[i], g, case)
            maximum_closure = max(maximum_closure, closure["energyRelativeError"])
            require(np.array_equal(baseline_arrays["Classification"], arrays["Classification"]), "Static classification changed")
            if previous_atlas is not None:
                require(np.array_equal(previous_atlas, arrays["Previous"]), "Pre-blend atlas is not the preceding submitted update")
            previous_atlas = arrays["Irradiance"].copy()
            atlas, weights = split_change(current, baseline)
            row = dict(frame=i, sinceConsumed=i-result["t0"], rois={})
            for roi, mask in g["masks"].items():
                atlas_term, weight_term = float(atlas[mask].sum(axis=1).mean()), float(weights[mask].sum(axis=1).mean())
                contribution = (atlas+weights)[mask]
                ids = current["probes"][mask]
                unique = np.unique(ids)
                by_probe = np.array([contribution[ids == p].sum()/mask.sum() for p in unique])
                top = np.argsort(abs(by_probe))[-5:][::-1]
                row["rois"][roi] = dict(atlasDeltaE=atlas_term, weightDeltaE=weight_term,
                    deltaE=atlas_term+weight_term, supportChanges=int((current["support"][mask] != baseline["support"][mask]).sum()),
                    normalizedWeightL1=float(abs(current["weights"][mask]-baseline["weights"][mask]).sum(axis=1).mean()),
                    dominantProbes=[dict(probe=int(unique[j]), deltaE=float(by_probe[j])) for j in top])
            if result["t0"] <= i < result["firstFieldReady"]:
                field = record["frame"]["readiness"]["giField"]
                hysteresis = integration_hysteresis(case, field, result["globalUpdates"][i])
                ids = np.unique(current["probes"][current["weights"] > 0])
                influence = np.array([current["weights"][current["probes"] == p].sum() for p in ids])
                selected = ids[np.argsort(influence)[-16:]]
                row["traceBlend"] = trace_check(g, arrays, result["globalUpdates"][i], case["traceSeed"], selected, hysteresis)
                previous = lookup(g, arrays["Previous"], arrays["Distance"], arrays["Classification"], case["atlasSlices"])
                # A derived inversion, not an independent transport reference. For fixed
                # weights the linear query commutes with the paired atlas blend.
                require(np.array_equal(current["weights"], previous["weights"]),
                        "Pre/post blend lookups use different normalized weights")
                raw = (current["energy"]-hysteresis*previous["energy"])/(1-hysteresis)
                for roi, mask in g["masks"].items():
                    row["rois"][roi]["derivedIntegratedE"] = float((raw[mask] @ LUMA).mean())
                    reference = baseline["energy"][mask]
                    history = hysteresis*(previous["energy"][mask]-reference)
                    sample = (1-hysteresis)*(raw[mask]-reference)
                    observed = current["energy"][mask]-reference
                    require(np.allclose(history+sample, observed, rtol=1e-10, atol=1e-8),
                            "Derived history/sample contributions did not close")
                    row["rois"][roi].update(
                        historyDeltaE=float((history @ LUMA).mean()),
                        derivedSampleDeltaE=float((sample @ LUMA).mean()),
                        selectedProbeWeightFraction=float(np.where(np.isin(current["probes"][mask], selected),
                            current["weights"][mask], 0).sum()/mask.sum()))
                row["traceBlend"]["coverage"] = "16 highest total normalized-weight probes across required ROI union"
                maximum_trace = max(maximum_trace, row["traceBlend"]["maximumRelativeError"])
            rows.append(row)
        trace_passed = all(d["traceBlend"]["arithmeticPassed"] for d in rows if "traceBlend" in d)
        result.update(attributionStatus="QueryDecomposedAndRefreshTraceBlendChecked", details=rows,
            traceBlendContractsPassed=trace_passed,
            maximumReceiverClosureError=maximum_closure, maximumTraceBlendError=maximum_trace,
            decompositionBaselineFrame=359, decompositionBaselineEndpointOffsets=endpoint_offsets,
            decompositionBaseline="One settled frame; own endpoint remains the 30-frame mean")
    return result


def analyze(root, output):
    output.mkdir(parents=True, exist_ok=False)
    cases = [c for c in load(root/"cases.json") if c["tap"] == "gi" and not c["feedback"] and
             c.get("step") in ("small-increase", "small-decrease")]
    require(cases, "No feedback-off GI small-step cases")
    # Seal all selected numerical/metadata inputs before and after analysis. Never
    # overwrite the collector's validation/result or reinterpret references here.
    inputs = [root/n for n in ("cases.json", "source-identity.json", "binaries.json")]
    if (root/"result.json").exists():
        inputs.append(root/"result.json")
    for c in cases:
        inputs += sorted(p for p in (root/c["directory"]).rglob("*") if p.suffix in (".json", ".npy"))
    before = [receipt(p) for p in inputs]
    observations = [analyze_case(root, c) for c in cases]
    require(before == [receipt(p) for p in inputs], "Raw evidence changed during read-only attribution")
    save(output/"input-receipts.json", before)
    code = sorted(Path(__file__).parent.glob("*.py"))+[Path(__file__).with_name("GIFieldLightStepRecipes.json")]
    save(output/"analysis-code-receipts.json", [receipt(p) for p in code])
    snapshot = output/"analysis-code"
    snapshot.mkdir()
    for path in code:
        shutil.copy2(path, snapshot/path.name)
    for r in observations:
        name = r["case"]["directory"].replace("/", "-")
        with (output/(name+".csv")).open("w", encoding="utf-8", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["frame", "sinceConsumed", "globalUpdate", "roi", "luminance", "signedStepError", "signedEndpointError"])
            for roi, values in r["roiSeries"].items():
                m = r["rois"][roi]
                for i, value in enumerate(values):
                    error = (value-m["new"])*np.sign(m["new"]-m["old"])
                    writer.writerow([i, i-r["t0"], r["globalUpdates"][i], roi, value,
                                     error/m["amplitude"], error/max(abs(m["new"]), FLOOR)])
    contracts = all(o.get("traceBlendContractsPassed", True) for o in observations)
    result = dict(status="analyzed" if contracts else "analyzed-with-arithmetic-failures",
        analysisOnly=True, engineeringOnly=True, contractsPassed=contracts,
        referenceQualification=False, responseQualityQualified=False, performanceQualified=False, wholeG3Qualified=False,
        root=str(root.resolve()), observations=observations,
        failedROIObservations=sum(not m["targetsPassed"] for r in observations for m in r["rois"].values()),
        observedResponseTargetsPassed=all(r["observedResponseTargetsPassed"] for r in observations))
    save(output/"result.json", result)
    print(json.dumps({k: v for k, v in result.items() if k != "observations"}))
    return contracts


def self_test():
    rng = np.random.default_rng(114)
    probes = np.arange(40).reshape(5, 8)
    def sample():
        w = rng.random((5, 8))
        w /= w.sum(axis=1)[:, None]
        m = rng.random((5, 8, 3))*100
        return dict(weights=w, atlas=m, probes=probes, energy=np.pi*(w[..., None]*m).sum(axis=1))
    a, b = sample(), sample()
    x, y = split_change(a, b)
    require(np.allclose((x+y).sum(axis=1), (a["energy"]-b["energy"]) @ LUMA), "Mixed-factor closure failed")
    x, y = split_change(a, a)
    require(not x.any() and not y.any(), "Identical queries changed")
    c = dict(a, weights=b["weights"], energy=np.pi*(b["weights"][..., None]*a["atlas"]).sum(axis=1))
    x, y = split_change(c, a)
    require(not x.any() and np.any(y), "Weight-only change attributed to atlas")
    c = dict(a, atlas=b["atlas"], energy=np.pi*(a["weights"][..., None]*b["atlas"]).sum(axis=1))
    x, y = split_change(c, a)
    require(not y.any() and np.any(x), "Atlas-only change attributed to query weights")
    require(np.array_equal(blend_expected(np.ones(3), np.full(3, 3), .5), np.full(3, 2)), "Refresh blend changed")
    try:
        split_change(dict(a, probes=probes+1), b)
        raise AssertionError("Mismatched cages accepted")
    except ValueError:
        pass
    counts = np.array([3, 3, 3])
    g = dict(position=np.array([[-2., -2., -2.]]), normal=np.array([[0., 1., 0.]]),
             vertex=np.array([[0., 1., 0.]]), camera=np.array([1., 1., 1.]), atlasSlices=[0, 3],
             summary=dict(minimum=[-3., -3., -3.], counts=counts, spacing=1., irradianceInteriorWidth=6))
    irradiance = rng.random((3, 24, 24, 4))
    distance = np.tile([100., 10000.], (3, 48, 48, 1))
    classes = np.ones((3, 3, 3, 1), dtype=np.uint32)
    actual = lookup(g, irradiance, distance, classes, [0, 3])
    expected, query = oracle(g["position"], g["normal"], g["vertex"], g["camera"], np.array([-3.]*3),
                             1., counts, irradiance, distance, classes, VIEW_BIAS)
    require(np.allclose(actual["energy"], expected) and np.allclose(actual["total"], query[:, 0]),
            "Per-corner decomposition differs from existing FP64 lookup oracle")
    empty = lookup(g, irradiance, distance, classes*2, [0, 3])
    require(not empty["energy"].any() and not empty["weights"].any() and (empty["support"] == 3).all(),
            "Inactive probes acquired energy or support")
    for args in ((irradiance, distance, classes*3, [0, 3]),
                 (irradiance[:1], distance, classes, [0, 3]),
                 (irradiance, distance, classes, [1, 2])):
        try:
            lookup(g, *args)
            raise AssertionError("Invalid classification, layout or layer range accepted")
        except ValueError:
            pass
    case = dict(tap="gi", transientAttribution=True)
    addresses = [dict(resource="GI.Field."+n, at="final") for n in
                 ("ReceiverIrradiance", "Readiness", "Irradiance", "Distance", "Classification", "Rays", "ReceiverQuery")]
    addresses.append(dict(resource="GI.Field.Irradiance", at=dict(before="GI.Field.BlendShared")))
    record = dict(targets=[dict(address=a) for a in addresses])
    require(len(observation_targets(record, case, 47)) == 7, "Eight-slot observation lost a final target")
    for record_case, frame, targets in ((dict(tap="gi"), 47, record["targets"]),
                                      (case, 81, record["targets"]),
                                      (case, 47, record["targets"][:-1])):
        try:
            observation_targets(dict(targets=targets), record_case, frame)
            raise AssertionError("Extra, out-of-window or missing observation accepted")
        except ValueError:
            pass
    require(trace_error(np.array([[.0002179204, 0., 0.]]),
                        np.array([[.0002104658, 0., 0.]]))["arithmeticPassed"] is False,
            "The retained near-black arithmetic failure was turned into a pass")
    require(trace_error(np.array([[.002, 0., 0.]]), np.array([[.002, 0., 0.]]))["arithmeticPassed"],
            "Matching trace arithmetic failed")
    progressive = dict(progressiveRefresh=True, progressiveAttribution=True)
    field = dict(refreshing=True, completedUpdates=5, hysteresis=progressive_hysteresis(5),
                 summary=dict(field=dict(epochOriginUpdate=100)))
    require(integration_hysteresis(progressive, field, 106) == progressive_hysteresis(6) and
            integration_hysteresis(progressive, field, 106) != field["hysteresis"],
            "Progressive attribution used the lagged completed coefficient for the GPU update")
    require(integration_hysteresis({}, dict(refreshing=True, hysteresis=.5), 0) == .5,
            "Historical fixed refresh attribution changed")
    require(integration_hysteresis(progressive,
            dict(field, completedUpdates=3, hysteresis=progressive_hysteresis(3)), 106) == progressive_hysteresis(6),
            "GPU attribution incorrectly assumes exactly one completed-readback frame of lag")
    for recipe, snapshot, update in ((dict(progressive, transientAttribution=True), field, 106),
            (dict(progressiveAttribution=True), field, 106), (progressive, field, 105),
            (progressive, dict(field, hysteresis=.5), 106)):
        try:
            integration_hysteresis(recipe, snapshot, update)
            raise AssertionError("Mixed, stale or unproven progressive coefficient accepted")
        except ValueError:
            pass
    one = dict(summary=dict(counts=[1, 1, 1], irradianceInteriorWidth=6), atlasSlices=[0, 1])
    rays = np.full((1, 256, 4), 5, dtype=np.float32)
    previous = np.ones((1, 8, 8, 4), dtype=np.float32)
    blended = np.full_like(previous, 2)  # Three retained samples at 1, one current sample at 5.
    payload = dict(Rays=rays, Previous=previous, Irradiance=blended)
    require(trace_check(one, payload, 8, 61052, [0], .75)["arithmeticPassed"],
            "Trace closure ignored a nonconstant history coefficient")
    require(not trace_check(one, payload, 8, 61052, [0])["arithmeticPassed"],
            "The fixed-filter oracle accepted a progressive blend")
    # Uniform radiance must reconstruct all receivers without using previous or
    # blended irradiance. Exercise the full multi-probe query, not a top-N subset.
    payload = dict(Rays=np.full((27, 256, 4), 5, dtype=np.float32),
                   Irradiance=irradiance, Distance=distance, Classification=classes)
    current = lookup(g, irradiance, distance, classes, g["atlasSlices"])
    reconstructed, covered = trace_receiver(g, payload, 8, 61052, current)
    require(covered == len(np.unique(current["probes"][current["weights"] > 0])) and
            np.allclose(reconstructed, 5*np.pi), "All-corner ray receiver reconstruction failed")
    try:
        trace_receiver(g, payload, 8, 61052, empty)
        raise AssertionError("A receiver without probe support acquired a ray reference")
    except ValueError:
        pass
    print(json.dumps(dict(status="passed", checks=28)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--geometry", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        require(args.output is not None and not args.output.exists(), "A fresh output path is required")
        if args.geometry:
            g = geometry(args.geometry)
            save(args.output, dict(atlasSlices=g["atlasSlices"], viewBias=VIEW_BIAS,
                                   pixels={k: int(v.sum()) for k, v in g["masks"].items()}))
        else:
            require(args.root is not None, "A raw collection root is required")
            if not analyze(args.root, args.output):
                raise SystemExit(1)
