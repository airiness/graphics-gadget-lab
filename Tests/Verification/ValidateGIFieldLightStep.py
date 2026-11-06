"""Audit a bounded Physical Sky step experiment, without claiming F7 or G3 exit.

Timing always uses the recipe's own settled endpoints. Optional independently
certified references add GI accuracy/sign/amplitude and two distinct Ready reports;
Final stays a composite signal. This is not F7, whole-G3 or performance qualification.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path

import numpy as np

from ValidateGIFieldBaseline import camera_inputs, parameters
from ValidateGIFieldReadiness import records as readiness_records
from ValidateGIFieldLightStepReference import LIGHT_STEPS, certified_endpoint, endpoint_inputs, roi_masks

LUMA = np.array([.2126, .7152, .0722])
FRAME_COUNT = 360
EVENT_FRAME = 30
HOLD = 30
FLOOR = .001


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def reference_source_identity(source):
    rows = [r["path"]+" "+r["sha256"] for r in source if
            r["path"].startswith(("Sources/", "Shaders/", "Tests/Verification/", "Scripts/Verification/"))]
    require(len(rows) == len({r.rsplit(" ", 1)[0] for r in rows}), "Duplicate reference source paths")
    # The shared seal preserves its enumeration order; the reference publisher
    # hashes ordinal UTF-16 path order. Do not confuse ordering with source drift.
    rows.sort(key=lambda row: row.encode("utf-16-be"))
    return hashlib.sha256("\n".join(rows).encode()).hexdigest()


def first_consumed(effect, frames):
    require(effect["state"] == "Consumed" and effect["identity"]["domain"] == "lighting" and
            effect["identity"]["generation"] > 0 and
            effect["requested"] <= effect["applied"] <= effect["published"] <= effect["firstConsumed"],
            "A successful lighting receipt with four ordered stamps is required")
    generation = effect["identity"]["generation"]
    matches = [i for i, f in enumerate(frames) if f["frame"]["serial"] == effect["firstConsumed"]]
    require(len(matches) == 1, "firstConsumed is not an exact submitted sequence frame")
    index = matches[0]
    active = [f["parameters"]["lighting"]["active"]["generation"] for f in frames]
    require(active[index] == generation and generation not in active[:index],
            "The receipt does not identify first actual consumption of the new generation")
    require(all(g == generation for g in active[index:]), "A later lighting target replaced the owned event")
    return index


def measurement_phase(field):
    change = field["summary"].get("changeObservation")
    if change is None:
        return None
    observation = change["observation"]
    require(observation["field"] == field["summary"]["field"] and
            observation["kind"] == "completed-readiness" and
            observation["readinessUpdate"] == field["completedUpdates"] and
            change["state"] == field["state"] and change["phase"] in ("refresh", "steady"),
            "Readiness phase is not the same completed field/epoch/update")
    return change["phase"]


def field_update(field, updates):
    epoch_update = int(updates[0])
    global_update = field["summary"]["field"]["epochOriginUpdate"] + epoch_update
    require(np.all(updates == epoch_update) and epoch_update > 0 and
            global_update == field["summary"]["submittedUpdates"]+1 and
            epoch_update >= field["completedUpdates"], "Stale/mixed GPU field update provenance")
    return global_update


def response(values, t0, old, new, limit):
    y = np.asarray(values, dtype=np.float64)
    require(y.ndim == 1 and np.isfinite(y).all() and 0 <= t0 < len(y), "Invalid response series")
    amplitude = abs(new-old)
    result = dict(old=float(old), new=float(new), amplitude=float(amplitude))
    if amplitude < FLOOR:
        return dict(**result, status="NotMeasurable", targetsPassed=False)
    band = max(.1*amplitude, FLOOR)
    inside = np.abs(y-new) <= band
    enter = next((i for i in range(t0, len(y)-HOLD+1) if inside[i:].all()), None)
    overshoot = float(max(0., np.max((y[t0:]-new)*np.sign(new-old)))/amplitude)
    return dict(**result, status="Measured", band=float(band), t0=t0, tEnter=enter,
                tConfirm=None if enter is None else enter+HOLD-1,
                responseFrames=None if enter is None else enter-t0, overshoot=overshoot,
                targetsPassed=bool(enter is not None and enter-t0 <= limit and overshoot <= .1))


def read_group(path):
    record = load(path)
    require(record["submission"]["completed"] and
            record["frameSerial"] == record["frame"]["frame"]["serial"], "Not a same-frame completed GPU observation")
    return record


def invariant_inputs(frame):
    inputs = endpoint_inputs(frame)
    sun = inputs["scene"]["sun"]
    for key in ("toaIlluminanceLux", "directIlluminance", "diskRadiance"):
        sun.pop(key)
    inputs["parameters"] = {k: v for k, v in parameters(frame).items() if k != "sun_step_lux"}
    return inputs


def reference_error(rgb, reference):
    rgb, reference = np.asarray(rgb, dtype=np.float64), np.asarray(reference, dtype=np.float64)
    require(rgb.ndim == 2 and rgb.shape[1] == 3 and reference.shape == (3,) and
            np.isfinite(rgb).all() and np.isfinite(reference).all() and (rgb >= 0).all() and (reference >= 0).all(),
            "Invalid independent endpoint series/reference")
    y, ref_y = rgb @ LUMA, float(reference @ LUMA)
    energy = float(np.max(np.abs(y-ref_y))/max(ref_y, FLOOR))
    chroma = None
    if reference.sum() > FLOOR:
        if np.any(rgb.sum(axis=1) <= FLOOR):
            return dict(energyError=energy, chromaticityError=None, temporalRms=None, passed=False,
                        reason="MissingNonblackResponse")
        chroma = float(np.max(np.abs(rgb[:, :2]/rgb.sum(axis=1)[:, None]-reference[:2]/reference.sum())))
    rms = float(np.std(y)/max(ref_y, FLOOR))
    return dict(meanRGB=rgb.mean(0).tolist(), referenceRGB=reference.tolist(),
                meanLuminance=float(y.mean()), referenceLuminance=ref_y,
                signedLuminanceBias=float((y.mean()-ref_y)/max(ref_y, FLOOR)),
                energyError=energy, chromaticityError=chroma,
                temporalRms=rms if len(rgb) > 1 else None, samples=len(rgb),
                passed=bool(energy <= .1 and (chroma is None or chroma <= .02) and rms <= .02))


def reference_response(old, new, ref_old, ref_new):
    delta, ref_delta = float(new-old), float(ref_new-ref_old)
    amplitude, ref_amplitude = abs(delta), abs(ref_delta)
    return dict(signedChange=delta, referenceSignedChange=ref_delta,
                amplitude=amplitude, referenceAmplitude=ref_amplitude, required=True,
                referenceMeasurable=ref_amplitude >= FLOOR,
                signPassed=bool(ref_amplitude < FLOOR or delta*ref_delta > 0),
                amplitudePassed=bool(amplitude >= .5*ref_amplitude),
                passed=bool(ref_amplitude >= FLOOR and delta*ref_delta > 0 and amplitude >= .5*ref_amplitude))


def reference_metrics(rgb, references, first_ready, steady_ready):
    old, new = references
    if any(r["status"] != "ReferenceConverged" for r in references):
        return dict(status="ReferenceUnconverged", responseQualityQualified=False,
                    referenceAmplitudeQualified=False, referenceCorrelatedReadinessQualified=False)
    rows = {}
    for roi, values in rgb.items():
        values = np.asarray(values)
        a, b = np.asarray(old["rois"][roi]), np.asarray(new["rois"][roi])
        amplitude = reference_response(values[:EVENT_FRAME].mean(0) @ LUMA, values[-HOLD:].mean(0) @ LUMA,
                                       a @ LUMA, b @ LUMA)
        rows[roi] = dict(oldEndpoint=reference_error(values[:EVENT_FRAME], a),
                        newEndpoint=reference_error(values[-HOLD:], b), response=amplitude,
                        firstRefreshReady=reference_error(values[first_ready:first_ready+1], b),
                        firstSteadyReady=reference_error(values[steady_ready:steady_ready+1], b))
    endpoints = all(r[k]["passed"] for r in rows.values() for k in ("oldEndpoint", "newEndpoint"))
    amplitude = all(r["response"]["passed"] for r in rows.values())
    return dict(status="ReferenceConverged", references=references, rois=rows,
                endpointQualityPassed=endpoints, referenceAmplitudeQualified=amplitude,
                responseQualityQualified=endpoints and amplitude,
                referenceCorrelatedReadinessQualified=all(r[k]["passed"] for r in rows.values()
                    for k in ("firstRefreshReady", "firstSteadyReady")))


def step_recipe(case):
    name = case.get("step", "increase")
    require(name in LIGHT_STEPS, "Unknown bounded lighting-step recipe")
    recipe = LIGHT_STEPS[name]
    require((case["oldLux"], case["newLux"]) == (recipe["oldLux"], recipe["newLux"]),
            "Changed recipe endpoint pair")
    return recipe


def settled_endpoint(field):
    return (field["state"] == "Ready" and field["completedUpdates"] >= field["readyUpdate"]+120 and
            measurement_phase(field) == "steady")


def progressive_hysteresis(update):
    require(update >= 1, "Progressive refresh requires a completed epoch update")
    if update == 1:
        return .5
    decay = float(np.float32(.998))  # Frozen F4 profile, not a fitted response coefficient.
    tail = decay**(update-1)
    weight = 2*tail+(1-tail)/(1-decay)
    return 1-1/weight


def transient_frame(case, index):
    return bool(case.get("transientAttribution") and (29 <= index <= 80 or index == 359))


def observation_targets(record, case, index):
    # Preserve the strict two-target qualification recipe. Only explicit engineering
    # cases may observe the six extra addresses, including both atlas versions.
    targets = record["targets"]
    keys = [(t["address"]["resource"], json.dumps(t["address"]["at"], sort_keys=True)) for t in targets]
    expected = [("GI.Field.ReceiverIrradiance" if case["tap"] == "gi" else "TAA.ResolvedSceneColor", '"final"'),
                ("GI.Field.Readiness", '"final"')]
    if transient_frame(case, index):
        expected += [("GI.Field.Irradiance", json.dumps(dict(before="GI.Field.BlendShared"))),
                     *[("GI.Field."+name, '"final"') for name in
                       ("Irradiance", "Distance", "Classification", "Rays", "ReceiverQuery")]]
    require(len(keys) == len(expected) and set(keys) == set(expected), "Incomplete/changed numerical observation")
    return {t["address"]["resource"]: t for t in targets if t["address"]["at"] == "final"}


def validate_case(root, case, qualify_references=False):
    step_recipe(case)
    engineering = case.get("transientAttribution", False)
    progressive = case.get("progressiveRefresh", False)
    require(not (engineering and progressive), "Historical attribution requires fixed refresh integration")
    if progressive:
        require(isinstance(case["traceSeed"], int) and 0 <= case["traceSeed"] <= 0xffffffff and
                case["traceSeed"] != 61051 and 128 <= case["preEventSettleFrames"] <= 512,
                "Invalid progressive-refresh replay inputs")
    if engineering:
        require(not qualify_references and case["tap"] == "gi" and not case["feedback"] and
                case["step"] in ("small-increase", "small-decrease") and
                0 <= case["traceSeed"] <= 0xffffffff and case["traceSeed"] != 61051 and
                128 <= case["preEventSettleFrames"] <= 512, "Changed engineering-only transient recipe")
    require(case["fixture"] == 4 and case["environment"] == 3 and case["view"] == "F4_Floor" and
            case["frameCount"] == FRAME_COUNT and case["eventFrame"] == EVENT_FRAME and
            case["tap"] in ("gi", "final"),
            "Changed bounded experiment inputs")
    directory = root/case["directory"]
    receipt = load(directory/"sequence.json")["sequence"]
    require(receipt["state"] == "completed" and receipt["submittedFrames"] == FRAME_COUNT and
            receipt["parameterFrameCount"] == FRAME_COUNT and
            receipt["parameterPublication"]["outcome"] == "Published", "Incomplete submitted/published sequence range")
    events = receipt["events"]
    require(len(events) == 1 and events[0]["frame"] == EVENT_FRAME and events[0]["batch"]["succeeded"],
            "Missing successful owned event")
    effect = events[0]["batch"]["items"][0]
    require(effect["id"] == "gi_reference.sun_step_lux", "Wrong light-step control")
    range_file = receipt["parameterPublication"]["files"][0]
    parameter_frames = load(range_file)["frames"]
    require(len(parameter_frames) == FRAME_COUNT, "Parameter product omitted transition frames")
    paths = list((directory/"samples").glob("*.resource-export.json"))
    require(len(paths) == FRAME_COUNT, "Every submitted frame needs its own numerical tap")
    groups = sorted(((p, read_group(p)) for p in paths), key=lambda pair: pair[1]["frame"]["sequence"]["frame"])
    frames = [r["frame"] for _, r in groups]
    require([f["sequence"]["frame"] for f in frames] == list(range(FRAME_COUNT)), "Skipped/duplicated observation frames")
    t0 = first_consumed(effect, frames)
    require(EVENT_FRAME <= t0 < FRAME_COUNT-2*HOLD, "No bounded post-consumption observation window")
    mask_paths = list((directory/"mask").glob("*.resource-export.json"))
    require(len(mask_paths) == 1, "Missing independently replayed unjittered receiver mask")
    mask_record = read_group(mask_paths[0])
    mask_meta = mask_record["frame"]
    mask_targets = {t["address"]["resource"]: t for t in mask_record["targets"]}
    require(len(mask_targets) == len(mask_record["targets"]) == (3 if engineering else 1) and
            set(mask_targets) == ({"GI.Receiver.Position", "GI.Receiver.VertexNormal", "GI.Receiver.ShadingNormal"}
                                 if engineering else {"GI.Receiver.Position"}), "Changed geometry observation")
    mask_target = mask_targets["GI.Receiver.Position"]
    require(mask_target["address"]["resource"] == "GI.Receiver.Position" and
            parameters(mask_meta)["fixture"] == 4 and parameters(mask_meta)["environment"] == 3 and
            camera_inputs(mask_meta) == camera_inputs(frames[0]), "Mask is not the same authored geometry/view")
    masks = roi_masks(np.load(mask_paths[0].parent/mask_target["array"], allow_pickle=False))
    initial = frames[0]["readiness"]["giField"]
    initial_identity = initial["summary"]["field"]
    target_epoch = None
    updates = []
    raw_field = []
    series = {name: [] for name in masks}
    rgb_series = {name: [] for name in masks}
    ready_frames = []
    steady_ready_frames = []
    refresh_ready = False
    for index, (path, record) in enumerate(groups):
        frame = record["frame"]
        field = frame["readiness"]["giField"]
        identity = field["summary"]["field"]
        p = parameters(frame)
        require(frame["backend"] == case["backend"] and frame["content"]["labId"] == "gglab.lab.gi_reference" and
                p["fixture"] == 4 and p["environment"] == 3 and p.get("lighting_step_experiment") and p["field_enabled"] and p["field_sampling"] and
                p["field_readiness"] and p["field_feedback"] == case["feedback"] and
                p.get("field_progressive_refresh", False) == progressive and
                p["receiver_diagnostics"] == (case["tap"] == "gi"), "Wrong experiment composition")
        require(frame["parameters"]["frameSerial"] == parameter_frames[index]["parameters"]["frameSerial"] ==
                frame["frame"]["serial"] and frame["parameters"] == parameter_frames[index]["parameters"],
                "GPU tap and submitted parameter product are not frame-paired")
        temporal = frame["temporal"]
        require(temporal["renderExtent"] == temporal["displayExtent"] == [320, 200] and
                temporal["status"] == ("disabled" if case["tap"] == "gi" else "active") and
                not temporal["gtao"]["enabled"] and not frame["developmentTools"] and
                not frame["sceneInputs"]["postProcess"]["bloomEnabled"], "Changed Native/tap composition")
        require(invariant_inputs(frame) == invariant_inputs(frames[0]), "A non-step parameter or effective scene input changed")
        require(camera_inputs(frame) == camera_inputs(frames[0]) and
                temporal["resetIdentity"] == frames[0]["temporal"]["resetIdentity"] and
                frame["sceneInputs"]["exposure"] == frames[0]["sceneInputs"]["exposure"],
                "Camera, exposure or temporal history changed at the light step")
        require(identity["generation"] == initial_identity["generation"] and
                identity["sceneRevision"] == initial_identity["sceneRevision"] and
                field["enabled"] and not field["failed"] and not field["summary"]["invalid"] and
                field["summary"]["initialized"], "Field/history was replaced, geometry changed or measurement failed")
        expected_generation = initial["lightingGeneration"] if index < t0 else effect["identity"]["generation"]
        require(field["lightingGeneration"] == field["environmentGeneration"] == identity["lightingGeneration"] ==
                identity["environmentGeneration"] == expected_generation and
                frame["parameters"]["lighting"]["active"]["generation"] == expected_generation and
                frame["sceneInputs"]["sun"]["toaIlluminanceLux"] == (case["oldLux"] if index < t0 else case["newLux"]),
                "Field and actual sun do not follow the built frame's published lighting generation")
        if engineering or progressive:
            require(p["seed"] == case["traceSeed"] and settled_endpoint(initial),
                    "Trace seed changed or old endpoint lacks steady Ready plus 120")
        phase = measurement_phase(field)
        if index >= t0:
            target_epoch = target_epoch or identity["refreshEpoch"]
            require(identity["refreshEpoch"] == target_epoch > initial_identity["refreshEpoch"] and
                    identity["epochOriginUpdate"] > 0, "Lighting refresh reset history or changed epoch during the observation")
            if progressive and field["completedUpdates"] > 0:
                require(abs(field["hysteresis"]-progressive_hysteresis(field["completedUpdates"])) < 1e-6,
                        "Progressive integration does not match the completed epoch's actual sample mass")
            if field["state"] == "Ready":
                ready_frames.append(index)
                if phase == "refresh":
                    refresh_ready = True
                elif phase == "steady":
                    steady_ready_frames.append(index)
        for gate in frame["readiness"]["gates"]:
            require(gate["state"] == "ready" or
                    (gate["state"] == "pending" and gate["name"] == "ibl" and EVENT_FRAME <= index < t0) or
                    (gate["state"] == "pending" and gate["name"] == "gi-field" and index >= t0 and
                     not steady_ready_frames and
                     ((field["refreshing"] and not refresh_ready) or
                      (refresh_ready and phase == "steady"))), "An unrelated readiness failure was hidden")
        targets = observation_targets(record, case, index)
        readiness = readiness_records(path.parent/targets["GI.Field.Readiness"]["array"])
        require(readiness.shape == (1+2*18**3, 4) and np.isfinite(readiness[:, :3]).all(), "Invalid field measurement ABI")
        update = np.ascontiguousarray(readiness[2::2, 3]).view(np.uint32)
        updates.append(field_update(field, update))
        active = readiness[2::2, 2] == 1
        require(active.any(), "Experiment lost all active probes")
        raw_field.append(float(readiness[2::2, 0][active].mean(dtype=np.float64)))
        name = "GI.Field.ReceiverIrradiance" if case["tap"] == "gi" else "TAA.ResolvedSceneColor"
        require(name in targets, "Wrong GI-only/Final tap")
        value = np.load(path.parent/targets[name]["array"], allow_pickle=False)
        require(value.shape == (1, 200, 320, 4) and np.isfinite(value).all(), "Nonfinite/invalid receiver or Final payload")
        rgb = value[0, ..., :3].astype(np.float64)
        if case["tap"] == "final":
            pre_exposure = frame["sceneInputs"]["exposure"]["preExposure"]
            require(pre_exposure > 0, "Invalid Final pre-exposure")
            rgb /= pre_exposure
        for roi, mask in masks.items():
            mean = rgb[mask].mean(axis=0)
            rgb_series[roi].append(mean.tolist())
            series[roi].append((mean @ LUMA).item())
    require(np.all(np.diff(updates) == 1), "Sequence skipped or inserted GI update frames")
    require(ready_frames and steady_ready_frames and
            measurement_phase(frames[ready_frames[0]]["readiness"]["giField"]) == "refresh" and
            frames[-1]["readiness"]["ready"] and
            measurement_phase(frames[-1]["readiness"]["giField"]) == "steady",
            "The lighting refresh did not finish steady Ready")
    metrics = {}
    for roi, values in series.items():
        old, new = float(np.mean(values[:EVENT_FRAME])), float(np.mean(values[-HOLD:]))
        metrics[roi] = response(values, t0, old, new, 45 if case["tap"] == "gi" else 60)
        metrics[roi].update(pixels=int(masks[roi].sum()),
                            endpointTemporalRms=float(np.std(values[-HOLD:])/max(abs(new), FLOOR)),
                            errorAtFirstReady=float(abs(values[ready_frames[0]]-new)/max(abs(new), FLOOR)))
        require(metrics[roi]["endpointTemporalRms"] <= .02, "The measured endpoint is not temporally stable")
    if engineering:
        require(all(settled_endpoint(f["readiness"]["giField"]) for f in frames[:EVENT_FRAME]+frames[-HOLD:]),
                "Engineering endpoint windows require steady Ready plus 120 throughout")
    reference = dict(status="NotApplicableComposite" if case["tap"] == "final" else "NotRequested")
    if qualify_references and case["tap"] == "gi":
        # Every sample used as a settled endpoint needs the prerequisite, not
        # merely the last frame of a window that began too early.
        require(all(settled_endpoint(f["readiness"]["giField"]) for f in frames[:EVENT_FRAME]+frames[-HOLD:]),
                "Reference endpoint windows require steady Ready plus 120 throughout")
        mode = "multibounce" if case["feedback"] else "single-bounce"
        binaries = {b["file"]: b["sha256"] for b in load(root/"binaries.json")}
        source_identity = reference_source_identity(load(root/"source-identity.json"))
        references = []
        for lux, frame in ((case["oldLux"], frames[0]), (case["newLux"], frames[-1])):
            arm = "on" if case["feedback"] else "off"
            ref_root = root/"refs"/f"{arm}-{lux}"/case["configuration"]/case["backend"]
            require(load(ref_root/"recipe.json")["sourceIdentity"] == source_identity, "Reference/experiment sources differ")
            references.append(certified_endpoint(ref_root, mode, lux, frame, masks, binaries))
        reference = reference_metrics(rgb_series, references, ready_frames[0], steady_ready_frames[0])
    return dict(case=case, t0=t0, requested=effect["requested"], published=effect["published"],
                firstConsumed=effect["firstConsumed"], firstFieldReady=ready_frames[0],
                firstSteadyFieldReady=steady_ready_frames[0],
                fieldGeneration=initial["generation"], refreshEpoch=target_epoch, globalUpdates=updates,
                rawFieldMeanLuminance=raw_field, roiSeries=series, rois=metrics, reference=reference,
                observedResponseTargetsPassed=all(m["targetsPassed"] for m in metrics.values()))


def validate(root, qualify_references=False):
    cases = load(root/"cases.json")
    require(cases and len({c["directory"] for c in cases}) == len(cases), "Missing/duplicate experiment cases")
    result = [validate_case(root, case, qualify_references) for case in cases]
    references = [r["reference"] for r in result if r["case"]["tap"] == "gi"]
    require(not qualify_references or references, "Reference qualification needs a GI-only arm")
    reference_ready = bool(qualify_references and references and all(r["status"] == "ReferenceConverged" for r in references))
    timing = all(r["observedResponseTargetsPassed"] for r in result)
    return dict(contractsPassed=True, experimentOnly=True,
                referenceStatus="ReferenceConverged" if reference_ready else (
                    "ReferenceUnconverged" if qualify_references else "NotRequested"),
                responseQualityQualified=bool(reference_ready and timing and all(r["responseQualityQualified"] for r in references)),
                referenceAmplitudeQualified=bool(reference_ready and all(r["referenceAmplitudeQualified"] for r in references)),
                referenceCorrelatedReadinessQualified=bool(reference_ready and all(r["referenceCorrelatedReadinessQualified"] for r in references)),
                wholeG3Qualified=False, performanceQualified=False, observations=result, observedResponseTargetsPassed=timing)


def self_test():
    require(progressive_hysteresis(1) == .5 and .66 < progressive_hysteresis(2) < .67 and
            .97 < progressive_hysteresis(45) < .98,
            "Progressive refresh fabricated steady history or retained the two-sample filter")
    for name, recipe in LIGHT_STEPS.items():
        case = dict(step=name, oldLux=recipe["oldLux"], newLux=recipe["newLux"])
        step_recipe(case)
        old, new = case["oldLux"]/10000., case["newLux"]/10000.
        require(response(np.r_[np.full(30, old), np.full(330, new)], 30, old, new, 45)["targetsPassed"] and
                reference_response(old, new, old, new)["passed"], "A declared signed step failed")
    step_recipe(dict(oldLux=10000, newLux=20000))
    for bad in (dict(step="unknown", oldLux=10000, newLux=20000),
                dict(step="decrease", oldLux=10000, newLux=20000)):
        try:
            step_recipe(bad)
            raise AssertionError("Unknown or mislabeled recipe accepted")
        except ValueError:
            pass
    require(not reference_response(1.2, 1.3, 1.2, 1.)["passed"], "Opposite small-step response passed")
    require(not reference_response(1.2, 1.11, 1.2, 1.)["passed"], "Weak small-step response passed")
    settled = dict(state="Ready", readyUpdate=80, completedUpdates=200, summary=dict(
        field=dict(refreshEpoch=2), changeObservation=dict(phase="steady", state="Ready",
        observation=dict(kind="completed-readiness", field=dict(refreshEpoch=2), readinessUpdate=200))))
    require(settled_endpoint(settled), "Ready plus 120 boundary changed")
    require(not settled_endpoint(dict(settled, readyUpdate=81)), "An endpoint window started before Ready plus 120")
    require(not settled_endpoint(dict(settled, state="Settling")), "Pending endpoints were certified")
    settled["summary"]["changeObservation"]["phase"] = "refresh"
    require(not settled_endpoint(settled), "Refresh-only Ready certified a settled endpoint")
    source = [dict(path="Sources/Passes/B.cpp", sha256="B"*64),
              dict(path="Sources/PBR/A.cpp", sha256="A"*64)]
    expected = hashlib.sha256(("Sources/PBR/A.cpp "+"A"*64+"\nSources/Passes/B.cpp "+"B"*64).encode()).hexdigest()
    require(reference_source_identity(source) == reference_source_identity(source[::-1]) == expected,
            "Source enumeration order changed the ordinal reference fingerprint")
    changed = copy.deepcopy(source)
    changed[0]["sha256"] = "C"*64
    require(reference_source_identity(changed) != expected, "Changed source bytes were hidden")
    require(reference_source_identity(source[:1]) != expected, "Missing sources were hidden")
    require(reference_source_identity(source+[dict(path="Sources/Extra.cpp", sha256="D"*64)]) != expected,
            "Additional sources were hidden")
    rejected = False
    try:
        reference_source_identity(source+source[:1])
    except ValueError:
        rejected = True
    require(rejected, "Duplicate source paths were accepted")
    effect = dict(state="Consumed", identity=dict(domain="lighting", generation=8),
                  requested=100, applied=100, published=102, firstConsumed=103)
    frames = [dict(frame=dict(serial=serial), parameters=dict(lighting=dict(active=dict(generation=g))))
              for serial, g in ((101, 7), (102, 7), (103, 8), (104, 8))]
    require(first_consumed(effect, frames) == 2, "Request/publication cannot replace firstConsumed")
    rejected = 0
    for key, val in (("state", "Published"), ("firstConsumed", 102), ("firstConsumed", 999), ("applied", 104)):
        bad = copy.deepcopy(effect)
        bad[key] = val
        try:
            first_consumed(bad, frames)
        except ValueError:
            rejected += 1
    require(rejected == 4, "Invalid event attribution was accepted")
    y = np.r_[np.ones(30), np.full(10, 1.5), np.full(50, 2.)]
    r = response(y, 30, 1, 2, 45)
    require(r["tEnter"] == 40 and r["tConfirm"] == 69 and r["responseFrames"] == 10,
            "The hold was added to response latency")
    y[75] = 1.5
    require(response(y, 30, 1, 2, 45)["tEnter"] is None, "A later band escape was ignored")
    y = np.r_[np.ones(30)*2, np.ones(60)]
    require(response(y, 30, 2, 1, 45)["targetsPassed"], "Negative steps need their own direction")
    y[31] = .8
    require(not response(y, 30, 2, 1, 45)["targetsPassed"], "Negative-step overshoot was hidden")
    require(response(np.ones(90), 30, 1, 1, 45)["status"] == "NotMeasurable", "No response cannot pass")
    field = dict(state="Ready", completedUpdates=33, summary=dict(field=dict(refreshEpoch=2)))
    change = dict(phase="refresh", state="Ready", observation=dict(kind="completed-readiness",
                  field=dict(refreshEpoch=2), readinessUpdate=33))
    field["summary"]["changeObservation"] = change
    require(measurement_phase(field) == "refresh", "Refresh Ready was hidden")
    change["phase"] = "steady"
    require(measurement_phase(field) == "steady", "Steady Ready was not distinguished")
    rejected = 0
    for key, value in (("field", dict(refreshEpoch=1)), ("readinessUpdate", 32), ("kind", "probe-atlas")):
        bad = copy.deepcopy(field)
        bad["summary"]["changeObservation"]["observation"][key] = value
        try:
            measurement_phase(bad)
        except ValueError:
            rejected += 1
    require(rejected == 3, "Stale/unrelated phase evidence was accepted")
    field["summary"]["changeObservation"] = None
    require(measurement_phase(field) is None, "Missing phase evidence was fabricated")
    field = dict(completedUpdates=9, summary=dict(submittedUpdates=9, field=dict(epochOriginUpdate=0)))
    require(field_update(field, np.array([10, 10])) == 10, "Initial epoch age was misread")
    field = dict(completedUpdates=0, summary=dict(submittedUpdates=50, field=dict(epochOriginUpdate=50)))
    require(field_update(field, np.array([1, 1])) == 51, "A lighting epoch reset lost global history")
    rejected = 0
    for updates in ([1, 2], [0, 0], [51, 51]):
        try:
            field_update(field, np.array(updates))
        except ValueError:
            rejected += 1
    require(rejected == 3, "Mixed, zero or incorrectly global readiness ages were accepted")
    require(reference_response(1, 1.4, 1, 2)["passed"] is False, "Missing half-reference response was accepted")
    require(reference_response(1, 1.5, 1, 2)["passed"], "The half-amplitude boundary changed")
    require(not reference_response(2, 1, 1, 2)["signPassed"], "Opposite GI-only sign was accepted")
    require(not reference_response(1, 1, 1, 1)["passed"], "Unmeasurable reference cannot qualify")
    require(reference_error(np.ones((30, 3)), np.ones(3))["passed"], "Matching independent endpoint failed")
    require(not reference_error(np.ones((30, 3)), np.full(3, 2))["passed"], "Stable wrong energy was accepted")
    require(not reference_error(np.array([[2., 1., 1.]]), np.ones(3))["passed"], "Chromaticity drift was accepted")
    require(not reference_error(np.zeros((30, 3)), np.ones(3))["passed"], "Missing nonblack energy was accepted")
    require(reference_metrics({}, [dict(status="ReferenceUnconverged")]*2, 30, 40)["status"] ==
            "ReferenceUnconverged", "Unconverged references were blamed on DDGI")
    rgb = np.r_[np.ones((30, 3)), np.full((330, 3), 2.)]
    rgb[69] = 1.5
    refs = [dict(status="ReferenceConverged", rois={"floor": [x]*3}) for x in (1., 2.)]
    result = reference_metrics({"floor": rgb}, refs, 69, 120)
    require(result["responseQualityQualified"] and not result["referenceCorrelatedReadinessQualified"] and
            not result["rois"]["floor"]["firstRefreshReady"]["passed"] and
            result["rois"]["floor"]["firstSteadyReady"]["passed"], "Two Ready times were conflated with endpoint accuracy")
    print(json.dumps(dict(status="passed", checks=50)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--qualify-references", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        require(args.root is not None, "An experiment run directory is required")
        result = validate(args.root, args.qualify_references)
        (args.root/"validation.json").write_text(json.dumps(result, indent=2, allow_nan=False)+"\n", encoding="utf-8")
        print(json.dumps({k: v for k, v in result.items() if k != "observations"}))
