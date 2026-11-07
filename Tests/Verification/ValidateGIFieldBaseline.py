"""Single-bounce reference, completed readiness and fixed-ROI temporal baseline.

D=1 sampling qualification is deliberately not depth convergence. Runtime Ready,
energy accuracy and temporal RMS are independent gates; none can substitute for
another. Resource exports are measurements, never quiet performance evidence.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile

import numpy as np

from ValidateGIReference import ROOM_FACES
from ValidateGIReferenceLighting import LUMA, f4_oracle, masks as lighting_masks
from ValidateGIFieldReadiness import records as readiness_records

FRAME_COUNT = 120
FLOOR = 1e-6
SEEDS = (61051, 61052)
COMPONENTS = ("Irradiance", "SunIrradiance", "EnvironmentIrradiance")
CASES = {
    "plane": dict(fixture=1, view="F1_Plane", counts=[13, 13, 13]),
    "sealed": dict(fixture=2, view="F2_Interior", counts=[9, 9, 9]),
    "sunroom": dict(fixture=4, view="F4_Floor", counts=[18, 18, 18]),
    "sunroom-back": dict(fixture=4, view="F4_Back", counts=[18, 18, 18]),
    "black": dict(fixture=19, view="F2_Interior", counts=[9, 9, 9]),
}
DEFAULTS = dict(fixture=0, seed=61051, depth=1, samples=64, batch_samples=64,
                albedo=0, secondary_albedo=0, environment=0, ev100=10,
                accumulate=True, probe_enabled=True, field_enabled=False,
                field_sampling=True, field_feedback=False, field_readiness=True, receiver_diagnostics=True,
                field_refresh_noise=0., field_steady_noise=0., field_irradiance_width=0)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def validate_case(case):
    require(case.get("id") in CASES and case == dict(id=case["id"], **CASES[case["id"]]),
            "Changed frozen baseline case, receiver view or field counts")


def parameters(frame):
    result = DEFAULTS.copy()
    result.update({p["id"].removeprefix("gi_reference."): p["value"]
                   for p in frame["parameters"]["values"]
                   if p["id"].startswith("gi_reference.")})
    return result


def camera_inputs(frame):
    camera = copy.deepcopy(frame["camera"])
    camera.pop("referenceView", None)
    return camera


def scene_inputs(frame):
    scene = copy.deepcopy(frame["sceneInputs"])
    scene["environment"].pop("activeIBLGeneration", None)
    return scene


def validate_frame(frame, case, field):
    p = parameters(frame)
    require(frame["content"]["labId"] == "gglab.lab.gi_reference" and
            p["fixture"] == case["fixture"] and p["albedo"] == p["secondary_albedo"] == 0 and
            p["environment"] == 0 and p["ev100"] == 10 and p["accumulate"] and
            p["probe_enabled"] and p["field_enabled"] == field and p["field_sampling"] and not p["field_feedback"] and
            p["receiver_diagnostics"] and p["field_readiness"], "Wrong baseline fixture/field inputs")
    temporal = frame["temporal"]
    require(temporal["status"] == "disabled" and temporal["jitterPixels"] == [0, 0] and
            temporal["renderExtent"] == temporal["displayExtent"] == [320, 200] and
            not temporal["gtao"]["enabled"] and not frame["developmentTools"] and
            not frame["sceneInputs"]["postProcess"]["bloomEnabled"],
            "Baseline requires fixed Native 320x200, unjittered, TAA/GTAO/bloom/tools off")
    scene = frame["sceneInputs"]
    require(scene["exposure"]["effectiveEV100"] == 10 and scene["atmosphere"] is None and
            scene["environment"]["intensity"] == 0, "Changed baseline exposure/environment")
    lighting = frame["parameters"]["lighting"]
    require(lighting["active"]["generation"] == lighting["requested"]["generation"] ==
            scene["environment"]["activeIBLGeneration"] > 0, "Unpublished baseline lighting")
    sun = scene["sun"]
    if case["fixture"] == 4:
        direction = np.array([.2, -1, -1]) / np.sqrt(2.04)
        require(sun is not None and sun["toaIlluminanceLux"] == 10000 and
                np.max(np.abs(np.asarray(sun["photonDirection"])-direction)) < 1e-6,
                "Wrong frozen F4 sun")
    else:
        require(sun is None, "Furnace/sealed/black baseline must have no sun")
    require(p["field_irradiance_width"] == 0, "Baseline must use the declared fixture profile")
    return p


def read_export(directory):
    paths = list(directory.glob("*.resource-export.json"))
    require(len(paths) == 1, f"Expected one bounded resource group: {directory}")
    record = load(paths[0])
    require(record["frameSerial"] == record["frame"]["frame"]["serial"] and
            record["submission"]["completed"], "Resource group is not GPU-complete")
    names = [t["address"]["resource"] for t in record["targets"]]
    require(len(names) == len(set(names)), "Duplicate baseline resource")
    values = {t["address"]["resource"]: np.load(directory/t["array"], allow_pickle=False)
              for t in record["targets"] if t["address"]["resource"] != "GI.Field.Readiness"}
    for name, array in values.items():
        require(np.isfinite(array).all(), f"Nonfinite baseline resource: {name}")
        if name != "GI.Field.Classification":
            require(array.dtype == np.dtype("<f4") and array.shape == (1, 200, 320, 4),
                    f"Wrong receiver texture ABI: {name}")
    return record, values


def roi_masks(case, values):
    position = values["GI.Reference.Position"][0]
    identity = values["GI.Reference.Identity"][0]
    covered = position[..., 3] == 1
    if case["fixture"] == 1:
        masks = {"plane": covered & (identity[..., 0] == 0) &
                 np.isin(identity[..., 1], [0, 1]) & (np.abs(position[..., 0]) <= 1) &
                 (np.abs(position[..., 2]) <= 1)}
    elif case["fixture"] in (2, 19):
        instance, triangles = ROOM_FACES[case["view"]]
        masks = {"back": covered & (identity[..., 0] == instance) &
                 np.isin(identity[..., 1], triangles) & (np.abs(position[..., 0]) <= 1.5) &
                 (position[..., 1] >= .5) & (position[..., 1] <= 3.5)}
    else:
        masks = lighting_masks(case, {k: v[0] for k, v in values.items()}, {})
    require(masks and all(m.dtype == np.bool_ and m.sum() >= 32 for m in masks.values()),
            "Empty or narrowed baseline ROI")
    return masks


def means(energy, masks):
    return {name: energy[mask, :3].mean(axis=0, dtype=np.float64) for name, mask in masks.items()}


def sampling_errors(rows):
    require(len(rows) == 4 and all(set(row) == set(rows[0]) for row in rows),
            "Sampling needs independent seeds at N and 2N with the same ROI set")
    result = {}
    for roi in rows[0]:
        result[roi] = {}
        for component in COMPONENTS:
            errors = []
            for a, b in ((0, 1), (2, 3), (0, 2), (1, 3)):
                va, vb = rows[a][roi][component], rows[b][roi][component]
                va, vb = np.r_[va, va @ LUMA], np.r_[vb, vb @ LUMA]
                errors.append(float(np.max(np.abs(va-vb)/np.maximum(np.abs(vb), FLOOR))))
            result[roi][component] = max(errors)
    return result


def qualify_reference(directory):
    case = load(directory/"case.json")
    validate_case(case)
    observations = load(directory/"references.json")
    require(observations and len({(o["samples"], o["seed"]) for o in observations}) == len(observations),
            "Missing/duplicate reference observations")
    captures, records, summaries, base_values = {}, {}, {}, None
    identity = None
    masks = None
    for observation in observations:
        record, values = read_export(directory/observation["directory"])
        frame = record["frame"]
        p = validate_frame(frame, case, False)
        require(frame["camera"]["referenceView"] == case["view"] and p["depth"] == 1 and
                p["samples"] == observation["samples"] and p["seed"] == observation["seed"] and
                p["seed"] in SEEDS, "D=1 reference parameter provenance differs")
        gate = [g for g in frame["readiness"]["gates"] if g["name"] == "gi-reference"]
        require(frame["readiness"]["ready"] and len(gate) == 1 and gate[0]["state"] == "ready" and
                "D=1," in gate[0]["detail"], "Reference observation is incomplete or has another depth")
        current_identity = dict(camera=camera_inputs(frame), scene=scene_inputs(frame),
                                backend=frame["backend"], device=frame["adapterDriverIdentity"],
                                code=frame["codeIdentity"], shader=frame["shaderIdentity"])
        require(identity is None or current_identity == identity, "Changed reference inputs/device/source")
        identity = current_identity
        needed = {"GI.Reference."+c for c in COMPONENTS} | {
            "GI.Reference.Position", "GI.Reference.Identity", "GI.Reference.Diagnostics",
            "GI.Reference.PathDiagnostics", "GI.Receiver.Position"}
        require(set(values) == needed, "Incomplete D=1 reference resource group")
        position = values["GI.Reference.Position"][0]
        receiver = values["GI.Receiver.Position"][0]
        covered = receiver[..., 3] == 1
        energy = values["GI.Reference.Irradiance"][0]
        require(covered.any() and np.array_equal(position[..., 3], receiver[..., 3]) and
                np.max(np.abs(position[covered, :3]-receiver[covered, :3])) < 2e-4 and
                np.array_equal(energy[..., 3], covered.astype(float)) and (energy[..., :3] >= 0).all(),
                "Reference lost an opaque receiver")
        diag = values["GI.Reference.Diagnostics"][0]
        paths = values["GI.Reference.PathDiagnostics"][0]
        require((diag >= 0).all() and (diag.sum(-1) <= 1.00001).all() and
                (diag[..., 3] == 0).all() and (paths[..., 0] == 0).all() and
                (paths[covered, 2] == 1).all() and (paths[covered, 3] == 1).all(),
                "Invalid or wrong-depth reference path")
        sun = values["GI.Reference.SunIrradiance"][0, ..., :3]
        sky = values["GI.Reference.EnvironmentIrradiance"][0, ..., :3]
        require(np.allclose(energy[..., :3], sun+sky, rtol=1e-6, atol=FLOOR),
                "Reference component sums differ")
        current_masks = roi_masks(case, values)
        if base_values is not None:
            require(np.array_equal(base_values["GI.Receiver.Position"], values["GI.Receiver.Position"]) and
                    np.array_equal(base_values["GI.Reference.Identity"], values["GI.Reference.Identity"]) and
                    all(np.array_equal(masks[n], current_masks[n]) for n in masks),
                    "Reference receiver/mask changed across seeds/sample counts")
        else:
            base_values, masks = values, current_masks
        key = (observation["samples"], observation["seed"])
        captures[key] = values
        records[key] = record
        component_means = {c: means(values["GI.Reference."+c][0], masks) for c in COMPONENTS}
        summaries[key] = {name: {c: component_means[c][name] for c in COMPONENTS} for name in masks}
    high = max(o["samples"] for o in observations)
    low = high//2
    keys = [(n, seed) for n in (low, high) for seed in SEEDS]
    require(all(key in captures for key in keys), "Missing independent N/2N reference quartet")
    errors = sampling_errors([summaries[key] for key in keys])
    qualified = all(e < .01 for row in errors.values() for e in row.values())
    analytic = None
    reference = {roi: np.mean([summaries[(high, s)][roi]["Irradiance"] for s in SEEDS], axis=0)
                 for roi in masks}
    if case["fixture"] == 4:
        analytic = {}
        for name, mask in masks.items():
            sun, sky = f4_oracle(base_values["GI.Reference.Position"][0][mask, :3].astype(float),
                                 case["view"] == "F4_Floor")
            expected = (sun+sky).mean(0)
            analytic[name] = float(np.max(np.abs(reference[name]-expected)/np.maximum(expected, FLOOR)))
        qualified = qualified and all(e <= .01 for e in analytic.values())
    elif case["fixture"] == 1:
        require(all(np.max(np.abs(v/np.pi-1)) <= .01 for v in reference.values()), "F1 reference furnace failed")
    else:
        require(all(np.count_nonzero(captures[key]["GI.Reference.Irradiance"][0, ..., :3]) == 0
                    for key in captures), "Sealed/black reference has false energy")
    result = dict(schemaVersion=1, status="SamplingQualified" if qualified else "SamplingUnconverged",
                  samples=high, seeds=list(SEEDS), errors=errors, analyticRelativeErrors=analytic,
                  referenceDepth=1, referenceSamplingQualified=qualified,
                  referenceDepthConvergenceQualified=False, perPixelNoiseQualified=False,
                  rois={n: dict(pixels=int(m.sum()), maskSha256=hashlib.sha256(m.tobytes()).hexdigest(),
                                mean=reference[n].tolist()) for n, m in masks.items()})
    save(directory/"reference-validation.json", result)
    return result, masks, base_values, records[keys[-1]]["frame"]


def temporal_metrics(samples, reference):
    samples = np.asarray(samples, dtype=float)
    reference = np.asarray(reference, dtype=float)
    require(samples.shape == (FRAME_COUNT, 3) and reference.shape == (3,) and
            np.isfinite(samples).all() and np.isfinite(reference).all() and
            (samples >= 0).all() and (reference >= 0).all(), "Invalid temporal ROI samples")
    y = samples @ LUMA
    ref_y = float(reference @ LUMA)
    signed_bias = (y-ref_y)/max(ref_y, FLOOR)
    rms = float(np.sqrt(np.mean((y-y.mean())**2))/max(ref_y, FLOOR))
    if reference.sum() > FLOOR:
        require((samples.sum(axis=1) > FLOOR).all(), "Nonblack reference lost its receiver response")
        chroma = float(np.max(np.abs(samples[:, :2]/samples.sum(axis=1)[:, None]-
                                    reference[:2]/reference.sum())))
    else:
        chroma = None
    return dict(mean=samples.mean(0).tolist(), minimumLuminance=float(y.min()),
                maximumLuminance=float(y.max()), minimumSignedBias=float(signed_bias.min()),
                maximumSignedBias=float(signed_bias.max()), maximumEnergyError=float(np.abs(signed_bias).max()),
                temporalRms=rms, chromaticityError=chroma,
                chromaticityUnavailableReason="black-reference" if chroma is None else None,
                temporalStabilityPassed=rms <= .02)


def validate_readiness(field, previous, current_update, ready_offset=120):
    require(ready_offset >= 120, "A diagnostic cannot shorten the primary settling gate")
    require(field["schemaVersion"] == 1 and field["generation"] > 0 and field["enabled"] and
            not field["failed"] and not field["refreshing"] and field["state"] == "Ready" and
            field["readyUpdate"] > 0 and field["completedUpdates"] >= field["readyUpdate"]+ready_offset,
            "Observation has not reached its completed-update offset after Ready")
    summary = field["summary"]
    stamp = summary["field"]
    require(summary["initialized"] and not summary["invalid"] and stamp["generation"] == field["generation"] and
            current_update > field["completedUpdates"] and
            current_update+stamp["epochOriginUpdate"] == summary["submittedUpdates"]+1,
            "GPU/CPU field update or epoch identity differs")
    if previous is not None:
        old_field, old_update = previous
        require(stamp == old_field["summary"]["field"] and
                field["readyUpdate"] == old_field["readyUpdate"] and current_update == old_update+1,
                "Temporal baseline skipped an update, replaced/refreshed the field or lost Ready")


def validate_offsets(offsets):
    require(isinstance(offsets, list) and len(offsets) <= 4 and
            all(type(offset) is int and 512 <= offset <= 8192 for offset in offsets) and
            sorted(set(offsets)) == offsets, "Invalid diagnostic completed-update offsets")


def validate_window_relation(previous, current):
    require(previous["field"] == current["field"] and previous["readyUpdate"] == current["readyUpdate"] and
            previous["captureBinding"] == current["captureBinding"] and
            current["observedUpdates"][0] > previous["observedUpdates"][-1],
            "Diagnostic windows changed field/Ready/inputs or overlap GPU updates")


def selected_qualification(results, cross_rhi):
    # Later diagnostic success never grants (or replaces) the primary qualification.
    return all(r["passed"] for r in results) and all(r["passed"] for r in cross_rhi)


def inspect_case(directory, backend, diagnostic_offsets=()):
    case = load(directory/"case.json")
    validate_case(case)
    reference, masks, reference_values, reference_frame = qualify_reference(directory)
    require(reference_frame["backend"] == backend, "Baseline is filed under the wrong backend")
    noise = load(directory/"noise-baseline.json")["frame"]["giField"]
    baseline = load(directory/"baseline-gated/receipt.json")
    require(noise["refreshNoiseSamples"] >= 30 and noise["steadyNoiseSamples"] == 64 and
            baseline["status"] in ("failed", "completed"), "Incomplete noise/uncalibrated gate evidence")
    if baseline["status"] == "failed":
        require("Unconverged" in json.dumps(baseline), "Uncalibrated gate hid another failure")
    adoption = load(directory/"calibrated-effect.json")
    require(adoption["batch"]["succeeded"], "Noise input adoption was not consumed")
    settled = load(directory/"settled/receipt.json")
    if settled["status"] != "completed":
        require(settled["status"] == "failed" and "Unconverged" in json.dumps(settled),
                "Baseline capture failed for a reason other than Unconverged")
        return dict(case=case["id"], passed=False, reference=reference, readinessPassed=False,
                    temporalStabilityQualified=False, reason="Unconverged", rois={})
    require(settled["publication"] == "Published", "Settled baseline was not published")
    result = inspect_window(directory, case, reference, masks, reference_values, reference_frame, noise)
    diagnostics = []
    previous = result
    for offset in diagnostic_offsets:
        window = directory/"diagnostics"/f"ready-{offset}"
        admission = load(window/"admission.json")
        observed = admission["status"]["frame"]["giField"]
        require(admission["readyOffset"] == offset and admission["readyUpdate"] == result["readyUpdate"] and
                admission["field"] == result["field"] and observed["summary"]["field"] == result["field"] and
                observed["state"] == "Ready" and observed["readyUpdate"] == result["readyUpdate"] and
                observed["completedUpdates"] >= result["readyUpdate"]+offset,
                "Diagnostic admission does not meet the original completed-update/epoch anchor")
        current = inspect_window(window, case, reference, masks, reference_values, reference_frame, noise, offset)
        validate_window_relation(previous, current)
        current["diagnosticOnly"] = True
        diagnostics.append(current)
        previous = current
    result["diagnostics"] = diagnostics
    return result


def inspect_window(directory, case, reference, masks, reference_values, reference_frame, noise, ready_offset=120):
    sequence = load(directory/"sequence.json")["sequence"]
    require(sequence["state"] == "completed" and sequence["frameCount"] == FRAME_COUNT and
            sequence["parameterPublication"]["outcome"] == "Published", "Incomplete static baseline sequence")
    paths = list((directory/"samples").glob("*.resource-export.json"))
    require(len(paths) == FRAME_COUNT, "Temporal RMS needs exactly 120 exported samples")
    paths.sort(key=lambda path: load(path)["frame"]["sequence"]["frame"])
    rows = {name: [] for name in masks}
    previous = None
    binding = None
    serials, updates = [], []
    position = None
    first_field = None
    for index, path in enumerate(paths):
        record = load(path)
        frame = record["frame"]
        # The group shares its directory with all sequence frames; address arrays
        # from this exact sidecar rather than discovering another frame by glob.
        require(record["submission"]["completed"] and
                record["frameSerial"] == frame["frame"]["serial"], "Uncompleted/mispaired sequence resources")
        p = validate_frame(frame, case, True)
        seq = frame["sequence"]
        require(seq["frame"] == index and seq["frameCount"] == FRAME_COUNT and
                seq["referenceSamples"] == 0 and seq["cameraPath"] == "GI_Static_"+case["view"],
                "Wrong/gapped static receiver sequence")
        require(camera_inputs(frame) == camera_inputs(reference_frame) and
                scene_inputs(frame) == scene_inputs(reference_frame) and
                frame["backend"] == reference_frame["backend"] and
                frame["adapterDriverIdentity"] == reference_frame["adapterDriverIdentity"] and
                frame["codeIdentity"] == reference_frame["codeIdentity"] and
                frame["shaderIdentity"] == reference_frame["shaderIdentity"],
                "Field/reference camera, scene, device or source inputs differ")
        frozen_parameters = copy.deepcopy(frame["parameters"])
        frozen_parameters.pop("frameSerial", None)
        current_binding = dict(parameters=frozen_parameters, control=frame.get("control"),
                               content=frame["content"], code=frame["codeIdentity"], shader=frame["shaderIdentity"],
                               sequenceId=seq["id"], cameraPathVersion=seq["cameraPathVersion"])
        require(binding is None or current_binding == binding, "Baseline parameter/control/content binding changed")
        binding = current_binding
        require(p["seed"] == 61052 and
                np.isclose(p["field_refresh_noise"], noise["refreshNoiseEstimate"], rtol=1e-6) and
                np.isclose(p["field_steady_noise"], noise["steadyNoiseEstimate"], rtol=1e-6),
                "Measurement reused calibration seed or changed explicitly adopted noise inputs")
        targets = {t["address"]["resource"]: t for t in record["targets"]}
        require(len(targets) == len(record["targets"]) and set(targets) == {
            "GI.Field.ReceiverIrradiance", "GI.Field.ReceiverQuery", "GI.Receiver.Position", "GI.Field.Readiness"} |
            ({"GI.Field.Classification"} if index == 0 else set()), "Wrong temporal resource group")
        values = {name: np.load(path.parent/t["array"], allow_pickle=False)
                  for name, t in targets.items() if name != "GI.Field.Readiness"}
        for name, array in values.items():
            require(np.isfinite(array).all(), f"Nonfinite temporal resource: {name}")
            if name != "GI.Field.Classification":
                require(array.dtype == np.dtype("<f4") and array.shape == (1, 200, 320, 4), "Wrong temporal receiver ABI")
        receiver = values["GI.Receiver.Position"][0]
        if position is None:
            position = receiver
            ref_position = reference_values["GI.Receiver.Position"][0]
            covered = position[..., 3] == 1
            require(np.array_equal(position[..., 3], ref_position[..., 3]) and
                    np.max(np.abs(position[covered, :3]-ref_position[covered, :3])) < 2e-4,
                    "Field and reference receivers do not match")
        else:
            require(np.array_equal(receiver, position), "Temporal receiver raster moved")
        energy = values["GI.Field.ReceiverIrradiance"][0]
        query = values["GI.Field.ReceiverQuery"][0]
        require(np.array_equal(energy[..., 3], covered.astype(float)) and
                np.array_equal(query[..., 3], covered.astype(float)) and (energy[..., :3] >= 0).all() and
                np.isin(query[covered, 2], [1, 2, 3, 4]).all(), "Temporal data dropped invalid/unsupported receivers")
        require((energy[~covered] == 0).all() and (query[~covered] == 0).all(), "Undefined temporal background")
        require((energy[covered & np.isin(query[..., 2], [3, 4]), :3] == 0).all(),
                "Unsupported receiver substituted environment energy")
        probe_records = readiness_records(path.parent/targets["GI.Field.Readiness"]["array"])
        require(probe_records.shape == (1+2*int(np.prod(case["counts"])), 4), "Wrong completed-update buffer layout")
        raw_updates = np.ascontiguousarray(probe_records[2::2, 3]).view(np.uint32)
        require((raw_updates == raw_updates[0]).all() and raw_updates[0] > 0 and
                np.isfinite(probe_records[0]).all() and np.isfinite(probe_records[1::2]).all() and
                np.isfinite(probe_records[2::2, :3]).all(), "Mixed/nonfinite GPU observation updates")
        current_update = int(raw_updates[0])
        field = frame["readiness"]["giField"]
        validate_readiness(field, previous, current_update, ready_offset)
        summary = field["summary"]
        minimum = [-3.125, -1.3125, -3.0625] if case["fixture"] == 4 else (
            [-3, -3, -3] if case["fixture"] == 1 else [-4, -2, -4])
        spacing = .375 if case["fixture"] == 4 else (.5 if case["fixture"] == 1 else 1.)
        require(summary["counts"] == case["counts"] and summary["minimum"] == minimum and
                summary["spacing"] == spacing and
                summary["irradianceInteriorWidth"] == (30 if case["fixture"] == 4 else 6),
                "Baseline field profile changed")
        require(np.isclose(field["noiseSigma"], noise["steadyNoiseEstimate"], rtol=1e-6) and
                np.isclose(field["threshold"], max(.01, 3*noise["steadyNoiseEstimate"]), rtol=1e-6),
                "Runtime changed its adopted readiness threshold")
        if first_field is None:
            first_field = field
            classes = values["GI.Field.Classification"]
            require(classes.shape == (case["counts"][1], case["counts"][2], case["counts"][0], 1) and
                    classes.dtype == np.dtype("<u4") and np.isin(classes, [1, 2]).all(), "Invalid classification population")
            if case["fixture"] == 19:
                require((classes[3:6, 3:6, 3:6] == 1).any() and
                        any(np.isin(query[m, 2], [1, 2]).any() for m in masks.values()),
                        "F10 cannot pass by excluding every interior probe/receiver")
        if case["fixture"] == 19:
            require(np.count_nonzero(energy[..., :3]) == 0, "F10 must preserve exactly zero irradiance")
        if previous is not None:
            require(record["frameSerial"] > serials[-1], "Temporal submitted frames are duplicated/out of order")
        previous = field, current_update
        serials.append(record["frameSerial"])
        updates.append(current_update)
        for name, value in means(energy, masks).items():
            rows[name].append(value)
    rois = {}
    for name, samples in rows.items():
        ref = reference["rois"][name]["mean"]
        result = temporal_metrics(samples, ref)
        if case["fixture"] == 1:
            energy_ok = max(abs(np.asarray(samples) @ LUMA / np.pi - 1)) <= .01
        elif case["fixture"] in (2, 19):
            energy_ok = max(np.asarray(samples) @ LUMA / np.pi) <= .01
        else:
            energy_ok = result["maximumEnergyError"] <= .1
        result.update(pixels=reference["rois"][name]["pixels"], referenceMean=ref,
                      luminanceSeries=(np.asarray(samples) @ LUMA).tolist(),
                      energyPassed=bool(energy_ok), chromaticityPassed=result["chromaticityError"] is None or
                      result["chromaticityError"] <= .02)
        result["passed"] = result["energyPassed"] and result["chromaticityPassed"] and result["temporalStabilityPassed"]
        rois[name] = result
    ready = first_field["readyUpdate"] <= 120
    return dict(case=case["id"], passed=reference["referenceSamplingQualified"] and ready and all(r["passed"] for r in rois.values()),
                reference=reference, readinessPassed=ready, readyUpdate=first_field["readyUpdate"],
                firstCompletedUpdate=first_field["completedUpdates"], field=first_field["summary"]["field"],
                readyOffset=ready_offset, captureBinding=digest({k: v for k, v in binding.items() if k != "sequenceId"}),
                frameSerials=serials, observedUpdates=updates, samples=FRAME_COUNT,
                refreshNoiseInput=noise["refreshNoiseEstimate"], steadyNoiseInput=noise["steadyNoiseEstimate"],
                temporalStabilityQualified=all(r["temporalStabilityPassed"] for r in rois.values()), rois=rois)


def validate(root):
    case_ids, backends = load(root/"cases.json"), load(root/"backends.json")
    offsets_path = root/"diagnostic-ready-offsets.json"
    offsets = load(offsets_path) if offsets_path.exists() else []
    validate_offsets(offsets)
    require(case_ids and len(set(case_ids)) == len(case_ids) and all(c in CASES for c in case_ids), "Invalid baseline cases")
    require(backends and len(set(backends)) == len(backends) and all(b in ("dx12", "vulkan") for b in backends),
            "Invalid baseline backends")
    results = []
    for backend in backends:
        for case_id in case_ids:
            result = inspect_case(root/backend/case_id, backend, offsets)
            results.append(dict(backend=backend, **result))
    cross_rhi = []
    if len(backends) == 2:
        for case_id in case_ids:
            pair = [r for r in results if r["case"] == case_id]
            for roi in pair[0]["rois"].keys() & pair[1]["rois"].keys():
                require(pair[0]["reference"]["rois"][roi]["maskSha256"] ==
                        pair[1]["reference"]["rois"][roi]["maskSha256"], "Cross-RHI baseline masks differ")
                a, b = (np.asarray(r["rois"][roi]["mean"]) for r in pair)
                error = float(abs((a-b) @ LUMA)/max(abs(b @ LUMA), FLOOR))
                cross_rhi.append(dict(case=case_id, roi=roi, relativeDifference=error, passed=error <= .02))
    passed = selected_qualification(results, cross_rhi)
    return dict(schemaVersion=1, status="passed" if passed else "failed", selectedCasesQualified=passed,
                referenceDepth=1, referenceDepthConvergenceQualified=False, wholeG3Qualified=False,
                performanceQualified=False, diagnosticReadyOffsets=offsets,
                extent=[320, 200], results=results, crossRhi=cross_rhi)


def self_test():
    checks = 0

    def reject(action):
        nonlocal checks
        try:
            action()
        except (ValueError, KeyError):
            checks += 1
            return
        raise AssertionError("Invalid baseline evidence was accepted")

    constant = np.full((FRAME_COUNT, 3), 2.)
    metric = temporal_metrics(constant, [2., 2., 2.])
    require(metric["temporalRms"] == 0 and metric["maximumEnergyError"] == 0, "Constant temporal baseline")
    checks += 1
    biased = temporal_metrics(constant*.8, [2., 2., 2.])
    require(biased["temporalStabilityPassed"] and biased["maximumEnergyError"] > .1,
            "Stable energy bias must remain a separate failure")
    checks += 1
    alternating = constant.copy()
    alternating[::2] *= 1.03
    alternating[1::2] *= .97
    noisy = temporal_metrics(alternating, [2., 2., 2.])
    require(np.isclose(noisy["temporalRms"], .03) and not noisy["temporalStabilityPassed"], "RMS must not average away flicker")
    checks += 1
    black = temporal_metrics(np.zeros_like(constant), [0., 0., 0.])
    require(black["temporalRms"] == 0 and black["chromaticityError"] is None, "Black floor/undefined chromaticity")
    checks += 1
    for value in (constant[:-1], np.full_like(constant, np.nan), np.full_like(constant, np.inf), -constant):
        reject(lambda value=value: temporal_metrics(value, [2., 2., 2.]))
    validate_offsets([512, 1024, 2048])
    checks += 1
    for offsets in ([120], [512, 512], [1024, 512], [8193], [True], [512.], list(range(512, 517))):
        reject(lambda offsets=offsets: validate_offsets(offsets))
    require(not selected_qualification([dict(passed=False, diagnostics=[dict(passed=True)])], []),
            "A passing late diagnostic hid a failed primary window")
    checks += 1
    require(selected_qualification([dict(passed=True, diagnostics=[dict(passed=False)])], []),
            "A diagnostic changed the selected primary quality gate")
    checks += 1
    window = dict(field=dict(generation=1), readyUpdate=96, captureBinding="frozen", observedUpdates=[220, 339])
    later = dict(window, observedUpdates=[610, 729])
    validate_window_relation(window, later)
    checks += 1
    for key, value in (("field", dict(generation=2)), ("readyUpdate", 97),
                       ("captureBinding", "changed"), ("observedUpdates", [339, 458])):
        reject(lambda key=key, value=value: validate_window_relation(window, dict(later, **{key: value})))
    field = dict(schemaVersion=1, generation=1, enabled=True, failed=False, refreshing=False, state="Ready",
                 readyUpdate=96, completedUpdates=216,
                 summary=dict(initialized=True, invalid=False, submittedUpdates=218,
                              field=dict(generation=1, epochOriginUpdate=0, refreshEpoch=0)))
    validate_readiness(field, None, 219)
    checks += 1
    reject(lambda: validate_readiness(field, None, 219, 119))
    reject(lambda: validate_readiness(field, None, 219, 512))
    for key, value in (("state", "Settling"), ("enabled", False), ("failed", True), ("refreshing", True),
                       ("completedUpdates", 215), ("readyUpdate", 0)):
        bad = copy.deepcopy(field)
        bad[key] = value
        reject(lambda bad=bad: validate_readiness(bad, None, 219))
    next_field = copy.deepcopy(field)
    next_field["summary"]["submittedUpdates"] += 1
    validate_readiness(next_field, (field, 219), 220)
    checks += 1
    reject(lambda: validate_readiness(next_field, (field, 218), 220))
    bad = copy.deepcopy(next_field)
    bad["summary"]["field"]["refreshEpoch"] += 1
    reject(lambda: validate_readiness(bad, (field, 219), 220))
    rows = [{"floor": {c: np.ones(3) for c in COMPONENTS}} for _ in range(4)]
    require(all(v == 0 for v in sampling_errors(rows)["floor"].values()), "Independent sampling quartet")
    checks += 1
    rows[1]["floor"]["EnvironmentIrradiance"] *= 1.02
    require(sampling_errors(rows)["floor"]["EnvironmentIrradiance"] > .01, "Component error hidden by total energy")
    checks += 1
    reject(lambda: sampling_errors(rows[:3]))
    case = dict(id="plane", **CASES["plane"])
    validate_case(case)
    checks += 1
    for key, value in (("fixture", 4), ("counts", [12, 13, 13]), ("view", "F4_Floor")):
        bad = dict(case, **{key: value})
        reject(lambda bad=bad: validate_case(bad))
    frame = dict(content=dict(labId="gglab.lab.gi_reference"),
                 parameters=dict(values=[dict(id="gi_reference.fixture", value=1)],
                                 lighting=dict(active=dict(generation=2), requested=dict(generation=2))),
                 developmentTools=False,
                 temporal=dict(status="disabled", jitterPixels=[0, 0], renderExtent=[320, 200],
                               displayExtent=[320, 200], gtao=dict(enabled=False)),
                 sceneInputs=dict(exposure=dict(effectiveEV100=10), atmosphere=None, sun=None,
                                  postProcess=dict(bloomEnabled=False),
                                  environment=dict(intensity=0, activeIBLGeneration=2)))
    validate_frame(frame, case, False)
    checks += 1
    for section, key, value in (("temporal", "jitterPixels", [.5, 0]),
                                ("temporal", "status", "active"),
                                ("temporal", "displayExtent", [640, 400])):
        bad = copy.deepcopy(frame)
        bad[section][key] = value
        reject(lambda bad=bad: validate_frame(bad, case, False))
    for name, value in (("depth", 2), ("field_enabled", True), ("environment", 1),
                        ("receiver_diagnostics", False), ("field_sampling", False)):
        bad = copy.deepcopy(frame)
        bad["parameters"]["values"].append(dict(id="gi_reference."+name, value=value))
        if name == "depth":
            # Depth is checked by reference qualification rather than frame admission.
            require(parameters(bad)["depth"] == 2, "Wrong reference depth was defaulted")
            checks += 1
        else:
            reject(lambda bad=bad: validate_frame(bad, case, False))
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        save(directory/"test.resource-export.json", dict(frameSerial=1, frame=dict(frame=dict(serial=1)),
                                                        submission=dict(completed=False), targets=[]))
        reject(lambda: read_export(directory))
        save(directory/"test.resource-export.json", dict(frameSerial=1, frame=dict(frame=dict(serial=2)),
                                                        submission=dict(completed=True), targets=[]))
        reject(lambda: read_export(directory))
    print(json.dumps(dict(ok=True, checks=checks)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--root", type=Path)
    group.add_argument("--reference", type=Path)
    group.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    elif args.reference:
        result, _, _, _ = qualify_reference(args.reference)
        print(json.dumps(result, allow_nan=False))
    else:
        result = validate(args.root)
        save(args.root/"validation.json", result)
        print(json.dumps(dict(status=result["status"], cases=len(result["results"]),
                              wholeG3Qualified=False)))
        sys.exit(0 if result["selectedCasesQualified"] else 1)
