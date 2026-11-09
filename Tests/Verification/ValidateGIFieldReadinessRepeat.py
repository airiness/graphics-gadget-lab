"""Hard-reset readiness repeatability: Ready counts per trace seed and a same-seed repeat.

Each run's status carries the runtime's opt-in readiness trace: every update since
the hard reset with its block change measures and decisions. An independent replay
of the threshold, warming, quiet-hold, blocker and state rules must reproduce every
traced decision. Ready counts never certify reference accuracy or temporal
stability. The change-measure tolerance belongs to a committed acceptance plan;
without one, only exact equality establishes determinism and other differences
stay unresolved.

Two Ready updates are reported against the 120-update limit: the first Ready, which
ends the refresh window (plan section 6.4), and the first Ready after the steady
handoff. The exit limit counts the steady Ready; the first Ready is the handoff
diagnostic. The runtime records the update after the one that ended the refresh in
steady mode, so every run's handoff latency must be one update.

The root's recipe.json declares the arm: the hard-reset refresh integration
(hardResetIntegration) and where steady change RMS is calibrated. after-timeout measures
it in the calibration run, whose refresh at the uncalibrated minimum ends at the timeout.
after-handoff measures it in a second hard reset at the calibration seed whose refresh
uses the adopted refresh RMS, so its steady schedule starts where the trace runs' does.
Roots from before per-reason refresh policy declared one progressiveRefresh flag for both
reasons, with a schema 1 field profile; roots without recipe.json ran the then-default
rapid refresh calibrated after the timeout.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import tempfile

import numpy as np

import GIFieldReadinessCalibration as readiness_policy

BLOCK_WIDTH = 4
MINIMUM_WARM_UPDATES = 34
QUIET_UPDATES = 30
TIMEOUT_UPDATES = 240
NOISE_SAMPLES = 64
REFRESH_HYSTERESIS = 0.5
MINIMUM_THRESHOLD = np.float32(0.01)
READY_LIMIT = 120
CALIBRATION_SEED = 61051
BLOCK_ORDER = "x + Bx * (z + Bz * y)"
BLOCKERS = dict(warming=1, p95=2, maximum=4, quietHold=8)
CASES = {
    "sphere": dict(fixture=0, view="F1_Sphere", counts=[13, 13, 13]),
    "plane": dict(fixture=1, view="F1_Plane", counts=[13, 13, 13]),
    "sealed": dict(fixture=2, view="F2_Interior", counts=[9, 9, 9]),
    "thin": dict(fixture=3, view="F3_Interior", counts=[9, 9, 9]),
    "sunroom": dict(fixture=4, view="F4_Floor", counts=[18, 18, 18]),
    "parallel": dict(fixture=20, view="F5_Doorway", counts=[11, 7, 7]),
    "camera": dict(fixture=4, view="F6_PoseWest", counts=[18, 18, 18]),
}
# The default single-bounce recipe; the trace observes decisions without changing them.
# The arm selects its refresh integration.
RECIPE = dict(environment=0, albedo=0, secondary_albedo=0, ev100=10, field_enabled=True, field_feedback=False,
              field_sampling=True, field_readiness=True, field_progressive_sampling=False,
              receiver_diagnostics=True, field_irradiance_width=0, field_update_limit=0,
              field_readiness_trace=True, probe_enabled=True, accumulate=True, depth=1, batch_samples=64)
LEGACY_DEFAULT = dict(progressiveRefresh=False, steadyCalibration="after-timeout")
STEADY_CALIBRATIONS = readiness_policy.STEADY_CALIBRATIONS
VERDICTS = ("byte-identical", "within-tolerance", "determinism-finding", "tolerance-undeclared")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def read_arm(root):
    """The declared arm with both reasons' integration; perReasonPolicy marks schema 2 roots."""
    path = root/"recipe.json"
    declared = load(path) if path.exists() else dict(LEGACY_DEFAULT)
    require(isinstance(declared, dict), "Invalid readiness repeat recipe arm")
    if set(declared) == {"hardResetIntegration", "steadyCalibration"}:
        require(declared["hardResetIntegration"] in readiness_policy.INTEGRATIONS and
                declared["steadyCalibration"] in STEADY_CALIBRATIONS, "Invalid readiness repeat recipe arm")
        return dict(hardResetIntegration=declared["hardResetIntegration"], lightingIntegration="rapid",
                    steadyCalibration=declared["steadyCalibration"], perReasonPolicy=True)
    require(set(declared) == set(LEGACY_DEFAULT) and isinstance(declared["progressiveRefresh"], bool) and
            declared["steadyCalibration"] in STEADY_CALIBRATIONS, "Invalid readiness repeat recipe arm")
    integration = "progressive" if declared["progressiveRefresh"] else "rapid"
    return dict(hardResetIntegration=integration, lightingIntegration=integration,
                steadyCalibration=declared["steadyCalibration"], perReasonPolicy=False)


def run_recipe(arm):
    recipe = dict(RECIPE, field_progressive_refresh=arm["lightingIntegration"] == "progressive")
    if arm["perReasonPolicy"]:
        recipe["field_rapid_hard_reset"] = arm["hardResetIntegration"] == "rapid"
    return recipe


def profile_matches(profile, arm, refresh_sigma, steady_sigma):
    """The field profile declares the arm's integrations and the adopted hard-reset sigma."""
    if profile["schemaVersion"] != (2 if arm["perReasonPolicy"] else 1):
        return False
    policy = readiness_policy.profile_policy(profile)
    hard, lighting = policy["hardReset"], policy["lighting"]
    # A hard-reset repeat has no lighting refresh, so its lighting sigma stays uncalibrated.
    return (policy["enabled"] and policy["refreshHysteresis"] == REFRESH_HYSTERESIS and
            hard["integration"] == arm["hardResetIntegration"] and
            lighting["integration"] == arm["lightingIntegration"] and
            np.float32(hard["refreshSigma"]) == np.float32(refresh_sigma) and
            np.float32(hard["steadySigma"]) == np.float32(steady_sigma) and
            (not arm["perReasonPolicy"] or lighting["refreshSigma"] == lighting["steadySigma"] == 0))


def validate_runs(runs):
    require(isinstance(runs, list) and len(runs) >= 3, "Readiness repeat needs two trace seeds and one repeat")
    traces, repeat = runs[:-1], runs[-1]
    seeds = [run["seed"] for run in traces]
    require(all(set(run) == {"id", "seed"} and run["id"] == f"seed-{run['seed']}" for run in traces) and
            len(set(seeds)) == len(seeds) and CALIBRATION_SEED not in seeds,
            "Trace seeds must be distinct and independent of the calibration seed")
    require(set(repeat) == {"id", "seed", "repeatOf"} and repeat["seed"] in seeds and
            repeat["repeatOf"] == f"seed-{repeat['seed']}" and repeat["id"] == f"repeat-{repeat['seed']}" and
            repeat["seed"] != seeds[-1], "The same-seed repeat must follow a different seed")


def threshold(sigma):
    return np.float32(max(MINIMUM_THRESHOLD, np.float32(3)*np.float32(sigma)))


def block_count(counts):
    return int(np.prod([(n+BLOCK_WIDTH-1)//BLOCK_WIDTH for n in counts]))


def measure(value):
    return None if value is None else np.float32(value)


def trace_updates(trace, counts):
    """The traced records of one hard-reset epoch, contiguous from update 1."""
    require(isinstance(trace, dict) and trace.get("schemaVersion") == 1 and trace["capacity"] == TIMEOUT_UPDATES and
            trace["counts"] == counts and trace["blockWidth"] == BLOCK_WIDTH and
            trace["blockOrder"] == BLOCK_ORDER and trace["warmingUpdates"] == MINIMUM_WARM_UPDATES and
            trace["requiredQuietUpdates"] == QUIET_UPDATES and trace["blockerBits"] == BLOCKERS,
            "Readiness trace layout differs from the hard-reset profile")
    updates = trace["updates"]
    require([u["update"] for u in updates] == list(range(1, len(updates)+1)),
            "Readiness trace is not contiguous from the hard reset")
    blocks = block_count(counts)
    for record in updates:
        require(all(len(record[key]) == blocks for key in
                    ("activeProbes", "p95", "maximum", "quietUpdates", "blockers", "currentQuiet")),
                "Readiness trace block arrays differ from the field layout")
    return updates


def replay(updates, refresh_sigma, steady_sigma, warming=MINIMUM_WARM_UPDATES):
    """Reproduces every traced threshold, hold, blocker, count and state from the change measures.

    Holds restart when the measurement phase changes. A hard reset leaves refresh only
    after Ready or the timeout ends it and never returns to it within the epoch.
    """
    values = dict(refresh=threshold(refresh_sigma), steady=threshold(steady_sigma))
    sigmas = dict(refresh=np.float32(refresh_sigma), steady=np.float32(steady_sigma))
    holds = None
    phase = None
    state = "Initializing"
    ready_update = 0
    refreshing = True
    rows = []
    for record in updates:
        update = record["update"]
        require(record["phase"] in values, "Unknown readiness phase")
        require(record["phase"] == "refresh" or not refreshing,
                f"Update {update} left refresh before Ready or the timeout ended it")
        require(record["phase"] == "steady" or phase != "steady", f"Update {update} returned to refresh")
        count = len(record["p95"])
        if holds is None or record["phase"] != phase:
            holds = [0]*count
        phase = record["phase"]
        value = values[phase]
        require(np.float32(record["threshold"]) == value and np.float32(record["noiseSigma"]) == sigmas[phase],
                f"Update {update} threshold differs from the adopted {phase} noise")
        active_blocks = quiet_blocks = 0
        maximum_change = np.float32(0)
        non_quiet = []
        for index in range(count):
            p95, maximum = measure(record["p95"][index]), measure(record["maximum"][index])
            if record["activeProbes"][index] == 0:
                require(p95 is None and maximum is None and record["blockers"][index] == 0 and
                        record["quietUpdates"][index] == 0 and not record["currentQuiet"][index],
                        f"Excluded block {index} at update {update} carries statistics or blockers")
                continue
            require(p95 is not None and maximum is not None and np.isfinite(p95) and np.isfinite(maximum) and
                    0 <= p95 <= maximum, f"Block {index} at update {update} has invalid change measures")
            quiet_now = bool(p95 <= value and maximum <= np.float32(3)*value)
            holds[index] = min(holds[index]+1, QUIET_UPDATES) if update >= warming and quiet_now else 0
            blockers = ((BLOCKERS["warming"] if update < warming else 0) |
                        (BLOCKERS["p95"] if p95 > value else 0) |
                        (BLOCKERS["maximum"] if maximum > np.float32(3)*value else 0) |
                        (BLOCKERS["quietHold"] if holds[index] < QUIET_UPDATES else 0))
            require(record["currentQuiet"][index] == quiet_now and record["quietUpdates"][index] == holds[index] and
                    record["blockers"][index] == blockers,
                    f"Block {index} at update {update} differs from the independent replay")
            active_blocks += 1
            quiet_blocks += holds[index] >= QUIET_UPDATES
            maximum_change = max(maximum_change, maximum)
            if not quiet_now:
                non_quiet.append(index)
        require(record["activeBlocks"] == active_blocks and record["excludedBlocks"] == count-active_blocks and
                record["quietBlocks"] == quiet_blocks and np.float32(record["maximumChange"]) == maximum_change,
                f"Update {update} block counts differ from the independent replay")
        if (active_blocks == 0 or update >= warming) and quiet_blocks == active_blocks:
            ready_update = update if state != "Ready" else ready_update
            state = "Ready"
            refreshing = False
        else:
            ready_update = 0
            state = "Unconverged" if update >= TIMEOUT_UPDATES else "Warming" if update < warming else "Settling"
            refreshing = refreshing and update < TIMEOUT_UPDATES
        require(record["state"] == state and record["readyUpdate"] == ready_update and
                record["refreshing"] == refreshing, f"Update {update} state differs from the independent replay")
        rows.append(dict(update=update, phase=phase, threshold=value, state=state,
                         active=list(record["activeProbes"]), nonQuiet=non_quiet,
                         p95=[measure(v) for v in record["p95"]], maximum=[measure(v) for v in record["maximum"]]))
    return rows


def handoff_latency(updates):
    """Updates from the one that ended the refresh to the first recorded in steady mode."""
    ended = next((u["update"] for u in updates if not u["refreshing"]), None)
    steady = next((u["update"] for u in updates if u["phase"] == "steady"), None)
    return None if ended is None or steady is None else steady-ended


def first_update(rows, state, phase=None):
    return next((r["update"] for r in rows if r["state"] == state and (phase is None or r["phase"] == phase)), None)


def camera_key(camera):
    return [float(v) for v in (*camera["position"], *camera["forward"], camera["verticalFovDegrees"])]


def effective_inputs(directory, spec, arm):
    """The case recipe from its complete calibration batch, overridden by later batches in order."""
    calibration = load(directory/"calibration-effect.json")["batch"]
    batches = [calibration]
    if arm["steadyCalibration"] == "after-handoff":
        batches.append(load(directory/"steady-calibration-effect.json")["batch"])
    effect = load(directory/spec["id"]/"effect.json")["batch"]
    batches.append(effect)
    require(all(b["succeeded"] for b in batches) and effect["impact"] == "RestartSession",
            "Run inputs were not consumed as a session restart")
    values = {}
    for batch in batches:
        values.update({v["id"]: v["value"] for v in batch["values"]})
    require(all(k.startswith("gi_reference.") for k in values), "Run changed parameters outside the Lab")
    return {k.removeprefix("gi_reference."): v for k, v in sorted(values.items())}


def same_value(a, b):
    return np.float32(a) == np.float32(b) if isinstance(a, float) or isinstance(b, float) else a == b


def read_pose(directory):
    """The run's next-frame pose capture: its non-default Lab inputs, view and field update."""
    receipt = load(directory/"pose.json")
    require(receipt["status"] == "completed" and receipt["publication"] == "Published",
            "Run pose capture was not published")
    paths = list((directory/"pose").glob("*.p0.json"))
    require(len(paths) == 1, "Run pose capture lacks its image metadata")
    metadata = load(paths[0])
    require(metadata["timing"]["mode"] == "next-frame", "Run pose was not captured on the next frame")
    values = {v["id"]: v["value"] for v in metadata["parameters"]["values"]}
    inputs = {k.removeprefix("gi_reference."): v for k, v in sorted(values.items()) if k.startswith("gi_reference.")}
    field = metadata["readiness"]["giField"]
    return dict(inputs=inputs, view=metadata["camera"].get("referenceView"), camera=camera_key(metadata["camera"]),
                generation=field["generation"], fieldUpdate=field["summary"]["frameUpdate"])


def read_run(directory, case, adopted, spec, arm):
    inputs = effective_inputs(directory, spec, arm)
    pose = read_pose(directory/spec["id"])
    # Frame metadata lists non-default values; each must be one this run declared.
    require(all(k in inputs and same_value(inputs[k], v) for k, v in pose["inputs"].items()),
            "Run frame inputs differ from its declared recipe")
    refresh_sigma, steady_sigma = np.float32(adopted["refresh"]), np.float32(adopted["steady"])
    require(all(inputs.get(k) == v for k, v in run_recipe(arm).items()) and inputs["fixture"] == case["fixture"] and
            inputs["seed"] == spec["seed"] and np.float32(inputs["field_refresh_noise"]) == refresh_sigma and
            np.float32(inputs["field_steady_noise"]) == steady_sigma,
            "Run is not the declared recipe with its seed and explicitly adopted noise")
    status = load(directory/spec["id"]/"outcome.json")["frame"]
    field = status["giField"]
    require(pose["view"] == case["view"] and pose["generation"] == field["generation"] and
            camera_key(status["camera"]) == pose["camera"],
            "Run camera is not its case view on its own field through the trace")
    summary = field["summary"]
    profile = summary["profile"]
    require(field["enabled"] and not field["failed"] and not summary["invalid"] and summary["initialized"] and
            summary["counts"] == case["counts"] and profile["seed"] == spec["seed"] and
            not profile["feedback"] and not profile["progressiveSampling"] and
            profile_matches(profile, arm, refresh_sigma, steady_sigma) and summary["updateLimit"] == 0,
            "Run field profile differs from its declared recipe")
    stamp = summary["field"]
    require(stamp["generation"] == field["generation"] and stamp["epochOriginUpdate"] == 0 and
            stamp["refreshEpoch"] == 1, "Run is not a hard reset of a fresh field")
    trace = summary["readinessTrace"]
    require(trace is not None and trace["field"] == stamp, "Run has no readiness trace of its own field")
    updates = trace_updates(trace, case["counts"])
    require(len(updates) == TIMEOUT_UPDATES, "Run ended before its trace window was complete")
    rows = replay(updates, refresh_sigma, steady_sigma)
    observation = summary["changeObservation"]
    latest = observation["observation"]["readinessUpdate"]
    require(observation["observation"]["field"] == stamp and latest >= TIMEOUT_UPDATES,
            "Run's completed observation does not follow its trace")
    cross_checked = latest == TIMEOUT_UPDATES
    if cross_checked:
        last = updates[-1]
        require(observation["phase"] == last["phase"] and observation["threshold"] == last["threshold"] and
                [b["activeProbes"] for b in observation["blocks"]] == last["activeProbes"] and
                [b["p95"] for b in observation["blocks"]] == last["p95"] and
                [b["maximum"] for b in observation["blocks"]] == last["maximum"] and
                [b["quietUpdates"] for b in observation["blocks"]] == last["quietUpdates"] and
                [b["blockers"] for b in observation["blocks"]] == last["blockers"],
                "The trace's last update differs from the completed observation")
    ready = first_update(rows, "Ready")
    steady = first_update(rows, "Ready", "steady")
    return dict(id=spec["id"], seed=spec["seed"], generation=field["generation"],
                poseFieldUpdate=pose["fieldUpdate"], readyUpdate=ready,
                readyWithinLimit=ready is not None and ready <= READY_LIMIT, steadyReadyUpdate=steady,
                steadyReadyWithinLimit=steady is not None and steady <= READY_LIMIT,
                steadyFromUpdate=next((r["update"] for r in rows if r["phase"] == "steady"), None),
                handoffLatency=handoff_latency(updates), refreshWaits=summary.get("refreshWaits"),
                finalState=rows[-1]["state"], tracedUpdates=len(rows), observationCrossChecked=cross_checked,
                refreshThreshold=float(threshold(refresh_sigma)), steadyThreshold=float(threshold(steady_sigma)),
                inputs=inputs, rows=rows)


def margin(row, block):
    """Distance of a block's nearer change measure from its own quiet bound."""
    value = float(row["threshold"])
    return min(abs(float(row["p95"][block])-value), abs(float(row["maximum"][block])-3*value))


def compare_repeat(first, second, tolerance=None):
    """Compares change measures, non-quiet blocks and Ready between two same-input runs.

    Change measures compare until the runs first measure in different phases: a
    different Ready update changes the later integration history by design. Phases that
    diverge after the same first Ready mean the steady schedule resumed at a different
    update; no change-measure tolerance covers that, so it is a determinism finding.
    """
    a_rows, b_rows = first["rows"], second["rows"]
    common = min(len(a_rows), len(b_rows))
    require(common and all(a["update"] == b["update"] for a, b in zip(a_rows, b_rows)),
            "Same-input runs share no aligned traced updates")
    window = next((i for i in range(common) if a_rows[i]["phase"] != b_rows[i]["phase"]), common)
    maximum = 0.
    equal = True
    active_equal = True
    differing = []
    for a, b in zip(a_rows[:window], b_rows[:window]):
        if a["active"] != b["active"]:
            active_equal = False
            continue
        for x, y in zip(a["p95"]+a["maximum"], b["p95"]+b["maximum"]):
            if x is not None:
                equal &= bool(x == y)
                maximum = max(maximum, abs(float(x)-float(y)))
        if a["nonQuiet"] != b["nonQuiet"]:
            blocks = sorted(set(a["nonQuiet"]) ^ set(b["nonQuiet"]))
            margins = [min(margin(a, block), margin(b, block)) for block in blocks]
            differing.append(dict(update=a["update"], phase=a["phase"], blocks=blocks,
                                  thresholdMargins=margins, thresholdMargin=max(margins)))
    ready_equal = first["readyUpdate"] == second["readyUpdate"]
    steady_equal = first.get("steadyReadyUpdate") == second.get("steadyReadyUpdate")
    handoffs = [next((r["update"] for r in rows if r["phase"] == "steady"), None) for rows in (a_rows, b_rows)]
    handoff_timing = window < common and ready_equal
    if equal and active_equal and ready_equal and steady_equal and not differing and window == common and \
            len(a_rows) == len(b_rows):
        verdict = "byte-identical"
    elif handoff_timing:
        verdict = "determinism-finding"
    elif tolerance is None:
        verdict = "tolerance-undeclared"
    else:
        # A different Ready update or non-quiet block is accepted only when traced to a
        # change measure within the declared tolerance of its quiet threshold.
        readies = (first["readyUpdate"], second["readyUpdate"], first.get("steadyReadyUpdate"),
                   second.get("steadyReadyUpdate"), 0)
        latest = max(u for u in readies if u is not None)
        traced = all(d["thresholdMargin"] <= tolerance for d in differing)
        ready_traced = (ready_equal and steady_equal) or any(d["update"] <= latest for d in differing)
        accepted = active_equal and maximum <= tolerance and traced and ready_traced
        verdict = "within-tolerance" if accepted else "determinism-finding"
    return dict(comparedUpdates=[a_rows[0]["update"], a_rows[window-1]["update"]] if window else None,
                phaseDivergenceUpdate=a_rows[window]["update"] if window < common else None,
                steadyHandoffUpdates=handoffs, handoffTimingDiffers=handoff_timing, readyEqual=ready_equal,
                steadyReadyEqual=steady_equal, activeProbesEqual=active_equal, changeMeasuresEqual=bool(equal),
                maximumChangeDifference=maximum, differingNonQuiet=differing, changeTolerance=tolerance,
                verdict=verdict)


def refresh_measures_match(first, second):
    """Whether two traces of one seed carry identical change measures while both refresh."""
    pairs = [(a, b) for a, b in zip(first, second) if a["phase"] == b["phase"] == "refresh"]
    equal = all(a["update"] == b["update"] and a["activeProbes"] == b["activeProbes"] and a["p95"] == b["p95"] and
                a["maximum"] == b["maximum"] for a, b in pairs)
    return bool(pairs) and equal, len(pairs)


def steady_calibration(effect, status, calibration, counts, arm):
    """The after-handoff steady noise of a second hard reset at the calibration seed.

    Its refresh uses the adopted refresh noise, so it ends where the trace runs' refresh
    would and the steady noise is sampled on the integration history that follows.
    """
    batch = effect["batch"]
    values = {v["id"]: v["value"] for v in batch["values"]}
    require(batch["succeeded"] and batch["impact"] == "RestartSession" and
            set(values) == {"gi_reference.field_refresh_noise", "gi_reference.field_steady_noise"} and
            np.float32(values["gi_reference.field_refresh_noise"]) ==
            np.float32(calibration["refreshNoiseEstimate"]) and values["gi_reference.field_steady_noise"] == 0,
            "Steady calibration inputs are not the adopted refresh noise with uncalibrated steady noise")
    field = status["giField"]
    summary = field["summary"]
    profile = summary["profile"]
    stamp = summary["field"]
    require(field["enabled"] and not field["failed"] and not summary["invalid"] and
            field["generation"] > calibration["generation"] and summary["counts"] == counts and
            profile["seed"] == CALIBRATION_SEED and not profile["feedback"] and not profile["progressiveSampling"] and
            profile_matches(profile, arm, calibration["refreshNoiseEstimate"], 0) and
            stamp["generation"] == field["generation"] and
            stamp["epochOriginUpdate"] == 0 and stamp["refreshEpoch"] == 1,
            "Steady calibration is not a hard reset of a fresh field at the calibration seed")
    require(field["steadyNoiseSamples"] == NOISE_SAMPLES and math.isfinite(field["steadyNoiseEstimate"]) and
            field["steadyNoiseEstimate"] >= 0, "Steady calibration lacks its bounded steady noise measurement")
    trace = summary["readinessTrace"]
    require(trace is not None and trace["field"] == stamp, "Steady calibration has no readiness trace of its own field")
    updates = trace_updates(trace, counts)
    rows = replay(updates, calibration["refreshNoiseEstimate"], 0)
    first = calibration["summary"]["readinessTrace"]
    require(first is not None, "Calibration has no readiness trace")
    matched, compared = refresh_measures_match(trace_updates(first, counts), updates)
    return dict(generation=field["generation"], refreshReadyUpdate=first_update(rows, "Ready"),
                steadyFromUpdate=next((r["update"] for r in rows if r["phase"] == "steady"), None),
                handoffLatency=handoff_latency(updates), tracedUpdates=len(rows),
                steadyNoiseEstimate=field["steadyNoiseEstimate"], steadyNoiseSamples=field["steadyNoiseSamples"],
                refreshMeasuresMatchCalibration=matched, comparedRefreshUpdates=compared)


def inspect_case(directory, backend, runs, tolerance, arm):
    case = load(directory/"case.json")
    require(case.get("id") in CASES and case == dict(id=case["id"], **CASES[case["id"]]),
            "Changed readiness repeat case, view or field counts")
    status = load(directory/"calibration.json")["frame"]
    calibration = status["giField"]
    require(case["view"] in status["referenceViews"], "Case view is not a registered reference view")
    profile = calibration["summary"]["profile"]
    require(calibration["enabled"] and not calibration["failed"] and profile["seed"] == CALIBRATION_SEED and
            profile_matches(profile, arm, 0, 0) and not profile["feedback"] and not profile["progressiveSampling"] and
            (calibration["refreshNoiseSamples"] >= 30 or
             (calibration["activeBlocks"] == 0 and calibration["excludedBlocks"] > 0)) and
            calibration["steadyNoiseSamples"] == NOISE_SAMPLES and
            math.isfinite(calibration["refreshNoiseEstimate"]) and calibration["refreshNoiseEstimate"] >= 0 and
            math.isfinite(calibration["steadyNoiseEstimate"]) and calibration["steadyNoiseEstimate"] >= 0,
            "Calibration lacks bounded refresh/steady noise measurements at its independent seed")
    after_handoff = arm["steadyCalibration"] == "after-handoff"
    require((directory/"steady-calibration.json").exists() == after_handoff and
            (directory/"steady-calibration-effect.json").exists() == after_handoff,
            "Steady calibration evidence differs from the declared arm")
    adopted = dict(refresh=calibration["refreshNoiseEstimate"], steady=calibration["steadyNoiseEstimate"])
    previous = calibration["generation"]
    steady = None
    if after_handoff:
        steady = steady_calibration(load(directory/"steady-calibration-effect.json"),
                                    load(directory/"steady-calibration.json")["frame"], calibration,
                                    case["counts"], arm)
        adopted["steady"] = steady["steadyNoiseEstimate"]
        previous = steady["generation"]
    observed = {}
    for spec in runs:
        result = read_run(directory, case, adopted, spec, arm)
        require(result["generation"] > previous, "Seed change did not hard-reset the field")
        previous = result["generation"]
        observed[spec["id"]] = result
    repeat = runs[-1]
    first, second = observed[repeat["repeatOf"]], observed[repeat["id"]]
    require(first["inputs"] == second["inputs"], "Same-seed repeat changed effective inputs")
    comparison = compare_repeat(first, second, tolerance)
    public = [{k: v for k, v in r.items() if k not in ("inputs", "rows")} for r in observed.values()]
    seeds = [r for r in public if not r["id"].startswith("repeat-")]
    return dict(backend=backend, case=case["id"], fixture=case["fixture"], view=case["view"],
                calibration=dict(state=calibration["state"], readyUpdate=calibration["readyUpdate"],
                                 completedUpdates=calibration["completedUpdates"],
                                 refreshNoiseEstimate=calibration["refreshNoiseEstimate"],
                                 steadyNoiseEstimate=calibration["steadyNoiseEstimate"],
                                 refreshNoiseSamples=calibration["refreshNoiseSamples"],
                                 steadyNoiseSamples=calibration["steadyNoiseSamples"]),
                steadyCalibration=steady, adoptedNoise=adopted, runs=public, repeat=comparison,
                allSeedsReadyWithin120=all(r["readyWithinLimit"] for r in seeds),
                allSeedsSteadyReadyWithin120=all(r["steadyReadyWithinLimit"] for r in seeds),
                repeatReadyWithin120=public[-1]["readyWithinLimit"],
                repeatSteadyReadyWithin120=public[-1]["steadyReadyWithinLimit"]), observed


def validate(root, tolerance=None):
    arm = read_arm(root)
    runs = load(root/"runs.json")
    validate_runs(runs)
    cases = load(root/"cases.json")
    backends = load(root/"backends.json")
    require(backends and len(set(backends)) == len(backends) and all(b in ("dx12", "vulkan") for b in backends),
            "Invalid readiness repeat backends")
    ids = [case["id"] for case in cases]
    require(ids and len(set(ids)) == len(ids), "Invalid readiness repeat cases")
    results, fields = [], {}
    for backend in backends:
        for case_id in ids:
            result, observed = inspect_case(root/backend/case_id, backend, runs, tolerance, arm)
            results.append(result)
            fields[(backend, case_id)] = observed
    # F6 reuses the F4 field from another camera: matching seeds should reproduce it.
    views = []
    for backend in backends:
        if (backend, "sunroom") in fields and (backend, "camera") in fields:
            for spec in runs:
                views.append(dict(backend=backend, run=spec["id"], **compare_repeat(
                    fields[(backend, "sunroom")][spec["id"]], fields[(backend, "camera")][spec["id"]], tolerance)))
    findings = [dict(backend=r["backend"], case=r["case"],
                     cause="steady-handoff-timing" if r["repeat"]["handoffTimingDiffers"] else "change-measures")
                for r in results if r["repeat"]["verdict"] == "determinism-finding"]
    # The after-handoff calibration's own handoff must follow its refresh end like the runs'.
    handoffs = [run["handoffLatency"] for r in results for run in r["runs"]] + \
        [r["steadyCalibration"]["handoffLatency"] for r in results if r["steadyCalibration"]]
    return dict(schemaVersion=2, status="passed", readyLimit=READY_LIMIT, calibrationSeed=CALIBRATION_SEED,
                recipe=arm, traceSeeds=[r["seed"] for r in runs[:-1]], repeatSeed=runs[-1]["seed"],
                changeTolerance=tolerance, results=results, f4F6FieldComparison=views,
                repeatDeterminismFindings=findings,
                allSeedsReadyWithin120=all(r["allSeedsReadyWithin120"] for r in results),
                allSeedsSteadyReadyWithin120=all(r["allSeedsSteadyReadyWithin120"] for r in results),
                handoffLatencies=sorted(set(handoffs) - {None}),
                allHandoffsFollowRefreshEnd=all(latency == 1 for latency in handoffs if latency is not None),
                refreshWaits=wait_totals(results),
                readyLimitDefinition="steady-ready", readinessRepeatabilityQualified=False,
                referenceAccuracyQualified=False,
                temporalStabilityQualified=False)


def wait_totals(results):
    """Per-backend cost of settling open refreshes; runs without the measure are counted apart."""
    totals = {}
    for r in results:
        entry = totals.setdefault(r["backend"], dict(runs=0, unmeasuredRuns=0, count=0, milliseconds=0.0))
        for run in r["runs"]:
            waits = run["refreshWaits"]
            if waits is None:
                entry["unmeasuredRuns"] += 1
                continue
            entry["runs"] += 1
            entry["count"] += waits["count"]
            entry["milliseconds"] += waits["milliseconds"]
    return totals


def simulate(measures, refresh_sigma, steady_sigma, steady_after=3):
    """Self-test runtime model: one record per (p95, maximum) list; steady starts after Ready."""
    records = []
    holds = []
    state, ready_update, refreshing, phase, since = "Initializing", 0, True, "refresh", None
    for update, blocks in enumerate(measures, 1):
        if since is not None and update >= since:
            phase = "steady"
        value = threshold(refresh_sigma if phase == "refresh" else steady_sigma)
        if not holds or (records and records[-1]["phase"] != phase):
            holds = [0]*len(blocks)
        record = dict(update=update, phase=phase, threshold=float(value),
                      noiseSigma=float(np.float32(refresh_sigma if phase == "refresh" else steady_sigma)),
                      activeProbes=[], p95=[], maximum=[], quietUpdates=[], blockers=[], currentQuiet=[])
        active = quiet = 0
        largest = np.float32(0)
        for index, block in enumerate(blocks):
            if block is None:
                for key, empty in (("activeProbes", 0), ("p95", None), ("maximum", None), ("quietUpdates", 0),
                                   ("blockers", 0), ("currentQuiet", False)):
                    record[key].append(empty)
                continue
            p95, maximum = np.float32(block[0]), np.float32(block[1])
            now = p95 <= value and maximum <= 3*value
            holds[index] = min(holds[index]+1, QUIET_UPDATES) if update >= MINIMUM_WARM_UPDATES and now else 0
            record["activeProbes"].append(64)
            record["p95"].append(float(p95))
            record["maximum"].append(float(maximum))
            record["quietUpdates"].append(holds[index])
            record["blockers"].append((update < MINIMUM_WARM_UPDATES)*1 | (p95 > value)*2 |
                                      (maximum > 3*value)*4 | (holds[index] < QUIET_UPDATES)*8)
            record["currentQuiet"].append(bool(now))
            active += 1
            quiet += holds[index] >= QUIET_UPDATES
            largest = max(largest, maximum)
        if (active == 0 or update >= MINIMUM_WARM_UPDATES) and quiet == active:
            ready_update = update if state != "Ready" else ready_update
            state, refreshing = "Ready", False
            since = update+steady_after if since is None else since
        else:
            ready_update = 0
            state = "Unconverged" if update >= TIMEOUT_UPDATES else \
                "Warming" if update < MINIMUM_WARM_UPDATES else "Settling"
            refreshing = refreshing and update < TIMEOUT_UPDATES
        record.update(state=state, readyUpdate=ready_update, refreshing=refreshing, activeBlocks=active,
                      excludedBlocks=len(blocks)-active, quietBlocks=quiet, maximumChange=float(largest))
        records.append(record)
    return records


def self_test():
    checks = 0

    def reject(action):
        nonlocal checks
        try:
            action()
        except (ValueError, KeyError):
            checks += 1
            return
        raise AssertionError("Invalid readiness repeat evidence was accepted")

    require(threshold(0) == np.float32(0.01) and threshold(0.01) == np.float32(0.03) and
            block_count([13, 13, 13]) == 64 and block_count([11, 7, 7]) == 12, "Threshold and block layout")
    checks += 1
    refresh, steady = 0.02, 0.005
    quiet = [[(0.0, 0.0), (0.01, 0.04), None]]*TIMEOUT_UPDATES
    records = simulate(quiet, refresh, steady)
    rows = replay(records, refresh, steady)
    require(first_update(rows, "Ready") == MINIMUM_WARM_UPDATES+QUIET_UPDATES-1 and
            rows[65]["phase"] == "steady" and first_update(rows, "Ready", "steady") == 66+QUIET_UPDATES-1 and
            rows[0]["state"] == "Warming" and rows[64]["state"] == "Ready" and rows[65]["state"] == "Settling",
            "Earliest Ready, refresh-to-steady handoff and steady Ready")
    checks += 1
    spiky = copy.deepcopy(quiet)
    spiky[49] = [(0.0, 0.0), (0.07, 0.07), None]
    rows = replay(simulate(spiky, refresh, steady), refresh, steady)
    require(first_update(rows, "Ready") == 50+QUIET_UPDATES and rows[49]["nonQuiet"] == [1],
            "A loud update restarts its block hold")
    checks += 1
    loud = [[(1.0, 1.0)]]*TIMEOUT_UPDATES
    rows = replay(simulate(loud, refresh, steady), refresh, steady)
    require(first_update(rows, "Ready") is None and rows[-1]["state"] == "Unconverged" and
            rows[-2]["state"] == "Settling", "A field that never quiets is Unconverged at the timeout")
    checks += 1
    for mutate in (lambda r: r[40]["quietUpdates"].__setitem__(1, 29),
                   lambda r: r[40]["blockers"].__setitem__(0, 9),
                   lambda r: r[40].__setitem__("threshold", 0.07),
                   lambda r: r[63].__setitem__("readyUpdate", 62),
                   lambda r: r[2]["p95"].__setitem__(2, 0.0),
                   lambda r: r[70].__setitem__("phase", "refresh"),
                   lambda r: r[30].__setitem__("phase", "steady"),
                   lambda r: r[63].__setitem__("quietBlocks", 1),
                   lambda r: r[63].__setitem__("refreshing", True)):
        bad = copy.deepcopy(records)
        mutate(bad)
        reject(lambda bad=bad: replay(bad, refresh, steady))
    trace = dict(schemaVersion=1, capacity=TIMEOUT_UPDATES, counts=[5, 4, 6], blockWidth=4, blockOrder=BLOCK_ORDER,
                 warmingUpdates=MINIMUM_WARM_UPDATES, requiredQuietUpdates=QUIET_UPDATES, blockerBits=BLOCKERS,
                 updates=simulate([[(0.0, 0.0)]*4]*5, refresh, steady))
    require(len(trace_updates(trace, [5, 4, 6])) == 5, "Trace layout")
    checks += 1
    gapped = copy.deepcopy(trace)
    del gapped["updates"][2]
    reject(lambda: trace_updates(gapped, [5, 4, 6]))
    reject(lambda: trace_updates(trace, [9, 9, 9]))

    def run(measures, **kwargs):
        rows = replay(simulate(measures, refresh, steady, **kwargs), refresh, steady)
        return dict(readyUpdate=first_update(rows, "Ready"), steadyReadyUpdate=first_update(rows, "Ready", "steady"),
                    rows=rows)
    base = run(quiet)
    require(compare_repeat(base, run(quiet))["verdict"] == "byte-identical", "Exact equality")
    checks += 1
    require(handoff_latency(simulate(quiet, refresh, steady, steady_after=1)) == 1 and
            handoff_latency(simulate(quiet, refresh, steady, steady_after=2)) == 2 and
            handoff_latency(simulate([[(1.0, 1.0)]*4]*10, refresh, steady)) is None,
            "Handoff latency counts from the update that ended the refresh")
    checks += 1
    totals = wait_totals([dict(backend="dx12", runs=[dict(refreshWaits=dict(count=3, milliseconds=1.5)),
                                                     dict(refreshWaits=None)])])
    require(totals == dict(dx12=dict(runs=1, unmeasuredRuns=1, count=3, milliseconds=1.5)), "Wait totals")
    checks += 1
    nudged = [[(float(np.nextafter(np.float32(0.01), np.float32(1))), 0.04) if b == (0.01, 0.04) else b
               for b in u] for u in quiet]
    require(compare_repeat(base, run(nudged))["verdict"] == "tolerance-undeclared",
            "Differences need a declared tolerance")
    require(compare_repeat(base, run(nudged), 1e-6)["verdict"] == "within-tolerance", "Declared tolerance")
    checks += 1
    # Both runs measure one block at its quiet bound; only one rounds above it.
    low, high = copy.deepcopy(quiet), copy.deepcopy(quiet)
    bound, above = float(threshold(refresh)), float(np.nextafter(threshold(refresh), np.float32(1)))
    low[49] = [(0.0, 0.0), (bound, bound), None]
    high[49] = [(0.0, 0.0), (above, above), None]
    edge, shifted = run(low), run(high)
    comparison = compare_repeat(edge, shifted, 1e-6)
    require(edge["readyUpdate"] == 63 and shifted["readyUpdate"] == 80 and
            comparison["verdict"] == "within-tolerance" and
            comparison["phaseDivergenceUpdate"] == 66 and comparison["differingNonQuiet"][0]["update"] == 50,
            "A Ready difference traced to a threshold-edge measure is within tolerance")
    checks += 1
    far = copy.deepcopy(quiet)
    far[49] = [(0.0, 0.0), (0.5, 0.5), None]
    require(compare_repeat(base, run(far), 1e-6)["verdict"] == "determinism-finding",
            "A Ready difference far from the quiet threshold is a determinism finding")
    checks += 1
    late = run(quiet, steady_after=4)
    for tolerance in (None, 1e-6):
        comparison = compare_repeat(base, late, tolerance)
        require(comparison["verdict"] == "determinism-finding" and comparison["handoffTimingDiffers"] and
                comparison["steadyHandoffUpdates"] == [66, 67] and comparison["phaseDivergenceUpdate"] == 66 and
                comparison["changeMeasuresEqual"] and comparison["readyEqual"] and
                not comparison["steadyReadyEqual"], "A later steady handoff after the same Ready is a finding")
    require(not compare_repeat(edge, shifted, 1e-6)["handoffTimingDiffers"],
            "A handoff that follows a different Ready is not a timing finding")
    checks += 1
    validate_runs([dict(id="seed-61052", seed=61052), dict(id="seed-71052", seed=71052),
                   dict(id="repeat-61052", seed=61052, repeatOf="seed-61052")])
    checks += 1
    for runs in ([dict(id="seed-61051", seed=61051), dict(id="seed-71052", seed=71052),
                  dict(id="repeat-71052", seed=71052, repeatOf="seed-71052")],
                 [dict(id="seed-61052", seed=61052), dict(id="seed-71052", seed=71052),
                  dict(id="repeat-71052", seed=71052, repeatOf="seed-71052")],
                 [dict(id="seed-61052", seed=61052), dict(id="repeat-61052", seed=61052, repeatOf="seed-61052")]):
        reject(lambda runs=runs: validate_runs(runs))
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        save(directory/"case.json", dict(id="thin", fixture=2, view="F3_Interior", counts=[9, 9, 9]))
        legacy_default = read_arm(directory)
        reject(lambda: inspect_case(directory, "dx12", [], None, legacy_default))
        require(legacy_default == dict(hardResetIntegration="rapid", lightingIntegration="rapid",
                                       steadyCalibration="after-timeout", perReasonPolicy=False) and
                run_recipe(legacy_default) == dict(RECIPE, field_progressive_refresh=False),
                "A root without an arm ran the then-default rapid refresh")
        save(directory/"recipe.json", dict(progressiveRefresh=True, steadyCalibration="after-handoff"))
        legacy = read_arm(directory)
        require(legacy["hardResetIntegration"] == legacy["lightingIntegration"] == "progressive" and
                not legacy["perReasonPolicy"] and "field_rapid_hard_reset" not in run_recipe(legacy),
                "A legacy arm's one flag selected both reasons")
        declared = dict(hardResetIntegration="progressive", steadyCalibration="after-handoff")
        save(directory/"recipe.json", declared)
        candidate = read_arm(directory)
        require(candidate == dict(declared, lightingIntegration="rapid", perReasonPolicy=True) and
                run_recipe(candidate)["field_progressive_refresh"] is False and
                run_recipe(candidate)["field_rapid_hard_reset"] is False, "Declared per-reason arm")
        checks += 1
        save(directory/"recipe.json", dict(declared, hardResetIntegration="rapid"))
        require(run_recipe(read_arm(directory))["field_rapid_hard_reset"] is True, "Rapid hard-reset arm")
        checks += 1
        for arm in (dict(declared, steadyCalibration="after-ready"), dict(declared, hardResetIntegration="fast"),
                    dict(declared, sampling=True), dict(progressiveRefresh=1, steadyCalibration="after-timeout"),
                    dict(declared, progressiveRefresh=True)):
            save(directory/"recipe.json", arm)
            reject(lambda: read_arm(directory))

    # A second hard reset at the calibration seed whose refresh ends at its calibrated Ready.
    counts, adopted = [12, 4, 4], 0.02
    stamp = dict(generation=5, epochOriginUpdate=0, refreshEpoch=1)

    def layout(updates, field):
        return dict(schemaVersion=1, capacity=TIMEOUT_UPDATES, counts=counts, blockWidth=4, blockOrder=BLOCK_ORDER,
                    warmingUpdates=MINIMUM_WARM_UPDATES, requiredQuietUpdates=QUIET_UPDATES,
                    blockerBits=BLOCKERS, field=field, updates=updates)
    first = dict(generation=4, refreshNoiseEstimate=adopted,
                 summary=dict(readinessTrace=layout(simulate(quiet, 0, 0), dict(stamp, generation=4))))
    effect = dict(batch=dict(succeeded=True, impact="RestartSession",
                             values=[dict(id="gi_reference.field_refresh_noise", value=adopted),
                                     dict(id="gi_reference.field_steady_noise", value=0.0)]))
    status = dict(giField=dict(
        enabled=True, failed=False, generation=5, steadyNoiseSamples=NOISE_SAMPLES, steadyNoiseEstimate=0.004,
        summary=dict(invalid=False, counts=counts, field=stamp,
                     readinessTrace=layout(simulate(quiet[:170], adopted, 0, steady_after=1), stamp),
                     profile=dict(schemaVersion=2, seed=CALIBRATION_SEED, feedback=False, progressiveSampling=False,
                                  readiness=dict(enabled=True, refreshHysteresis=0.5,
                                                 hardReset=dict(integration="progressive", refreshSigma=adopted,
                                                                steadySigma=0),
                                                 lighting=dict(integration="rapid", refreshSigma=0,
                                                               steadySigma=0))))))
    result = steady_calibration(effect, status, first, counts, candidate)
    require(result["refreshReadyUpdate"] == 63 and result["steadyFromUpdate"] == 64 and
            result["handoffLatency"] == 1 and result["steadyNoiseEstimate"] == 0.004 and
            result["refreshMeasuresMatchCalibration"] and result["comparedRefreshUpdates"] == 63,
            "After-handoff steady calibration")
    checks += 1
    drifted = copy.deepcopy(status)
    drifted["giField"]["summary"]["readinessTrace"]["updates"][40]["p95"][1] = 0.0
    require(not steady_calibration(effect, drifted, first, counts, candidate)["refreshMeasuresMatchCalibration"],
            "A steady calibration whose refresh measures differ from the calibration run is reported")
    checks += 1
    legacy_status = copy.deepcopy(status)
    legacy_status["giField"]["summary"]["profile"].update(schemaVersion=1, readiness=dict(
        enabled=True, progressiveRefresh=True, refreshHysteresis=0.5, noiseSigma=adopted, steadyNoiseSigma=0))
    require(steady_calibration(effect, legacy_status, first, counts, legacy)["steadyNoiseEstimate"] == 0.004,
            "A legacy root's schema 1 steady calibration still validates")
    checks += 1
    reject(lambda: steady_calibration(effect, legacy_status, first, counts, candidate))
    for path, value in ((("profile", "seed"), 61052),
                        (("profile", "readiness", "hardReset", "steadySigma"), 0.004),
                        (("profile", "readiness", "hardReset", "refreshSigma"), 0.03),
                        (("profile", "readiness", "hardReset", "integration"), "rapid"),
                        (("profile", "readiness", "lighting", "integration"), "progressive"),
                        (("profile", "readiness", "lighting", "refreshSigma"), 0.5),
                        (("profile", "schemaVersion"), 1), (("field", "refreshEpoch"), 2),
                        (("counts",), [9, 9, 9])):
        bad = copy.deepcopy(status)
        target = bad["giField"]["summary"]
        for key in path[:-1]:
            target = target[key]
        target[path[-1]] = value
        reject(lambda bad=bad: steady_calibration(effect, bad, first, counts, candidate))
    for key, value in (("generation", 4), ("steadyNoiseSamples", NOISE_SAMPLES-1)):
        bad = copy.deepcopy(status)
        bad["giField"][key] = value
        reject(lambda bad=bad: steady_calibration(effect, bad, first, counts, candidate))
    for mutate in (lambda e: e["batch"].__setitem__("impact", "Immediate"),
                   lambda e: e["batch"]["values"][0].__setitem__("value", 0.03),
                   lambda e: e["batch"]["values"][1].__setitem__("value", 0.004),
                   lambda e: e["batch"]["values"].append(dict(id="gi_reference.seed", value=61051))):
        bad = copy.deepcopy(effect)
        mutate(bad)
        reject(lambda bad=bad: steady_calibration(bad, status, first, counts, candidate))
    print(json.dumps(dict(ok=True, checks=checks)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--root", type=Path)
    group.add_argument("--self-test", action="store_true")
    parser.add_argument("--change-tolerance", type=float,
                        help="Absolute change-measure tolerance predeclared by the acceptance plan")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        require(args.change_tolerance is None or (math.isfinite(args.change_tolerance) and args.change_tolerance >= 0),
                "Invalid change-measure tolerance")
        result = validate(args.root, args.change_tolerance)
        save(args.root/"validation.json", result)
        print(json.dumps(dict(status=result["status"], cases=len(result["results"]),
                              allSeedsReadyWithin120=result["allSeedsReadyWithin120"],
                              allSeedsSteadyReadyWithin120=result["allSeedsSteadyReadyWithin120"])))
