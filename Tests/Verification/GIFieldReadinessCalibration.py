"""Readiness refresh policy and steady calibration shared by the GI field validators.

A field declares, per refresh reason, how its refresh integrates irradiance and the
change RMS that gates each phase. A sigma is the maximum over probes of the RMS change
in one phase's calibration window; it is not a stationary-noise certificate. Profile
schema 1 declared one calibration and one integration flag for both reasons; schema 2
declares the hard reset and the lighting refresh separately.

Steady sigma is measured after the timeout (the calibration field's refresh at the
uncalibrated minimum ends at the timeout) or after a calibrated handoff: a second field
at the calibration seed adopts the measured refresh sigma, so its refresh ends at a
calibrated Ready, as the gated runs' does, and the steady window follows the same
integration history. A root's readiness-calibration.json declares the method; roots
without it calibrated after the timeout.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import struct
import tempfile

REASONS = ("hardReset", "lighting")
INTEGRATIONS = ("rapid", "progressive")
STEADY_CALIBRATIONS = ("after-timeout", "after-handoff")
RECIPE_FILE = "readiness-calibration.json"
STEADY_EFFECT_FILE = "steady-calibration-effect.json"
STEADY_STATUS_FILE = "steady-calibration.json"
NOISE_SAMPLES = 64
REFRESH_SAMPLES = 30
# Lab inputs for each reason's refresh and steady sigma.
SIGMA_INPUTS = dict(hardReset=("field_refresh_noise", "field_steady_noise"),
                    lighting=("field_lighting_refresh_noise", "field_lighting_steady_noise"))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def f32(value):
    """The single-precision value a Lab input or profile sigma holds."""
    return struct.unpack("<f", struct.pack("<f", float(value)))[0]


def profile_policy(profile):
    """Per-reason integration and sigma of a field profile receipt, in schema 2 form."""
    readiness = profile["readiness"]
    if profile["schemaVersion"] == 1:
        require(set(readiness) == {"enabled", "noiseSigma", "steadyNoiseSigma", "refreshHysteresis",
                                   "progressiveRefresh"}, "Invalid schema 1 readiness profile")
        reason = dict(integration="progressive" if readiness["progressiveRefresh"] else "rapid",
                      refreshSigma=readiness["noiseSigma"], steadySigma=readiness["steadyNoiseSigma"])
        return dict(enabled=readiness["enabled"], refreshHysteresis=readiness["refreshHysteresis"],
                    hardReset=reason, lighting=dict(reason))
    require(profile["schemaVersion"] == 2 and set(readiness) == {"enabled", "refreshHysteresis", *REASONS},
            "Invalid readiness profile schema")
    for reason in REASONS:
        value = readiness[reason]
        require(isinstance(value, dict) and set(value) == {"integration", "refreshSigma", "steadySigma"} and
                value["integration"] in INTEGRATIONS and
                all(isinstance(value[k], (int, float)) and math.isfinite(value[k]) and value[k] >= 0
                    for k in ("refreshSigma", "steadySigma")), f"Invalid {reason} readiness policy")
    return copy.deepcopy(readiness)


def parameter_policy(values, per_reason=True):
    """Per-reason integration that unprefixed Lab inputs select; absent inputs are defaults.

    Binaries with a schema 1 profile predate per-reason policy: one flag then selected
    both reasons' integration, and field_rapid_hard_reset did not exist.
    """
    progressive = values.get("field_progressive_refresh", False)
    require(isinstance(progressive, bool), "Invalid progressive refresh input")
    if not per_reason:
        require("field_rapid_hard_reset" not in values, "A schema 1 binary has no rapid hard-reset input")
        legacy = "progressive" if progressive else "rapid"
        return dict(hardReset=legacy, lighting=legacy)
    rapid = values.get("field_rapid_hard_reset", False)
    require(isinstance(rapid, bool), "Invalid rapid hard-reset input")
    return dict(hardReset="rapid" if rapid else "progressive", lighting="progressive" if progressive else "rapid")


def require_policy_matches(profile, values):
    """The binary's declared per-reason integration equals the one its inputs select."""
    policy = profile_policy(profile)
    selected = parameter_policy(values, per_reason=profile["schemaVersion"] >= 2)
    require(all(policy[reason]["integration"] == selected[reason] for reason in REASONS),
            "Field profile refresh integration differs from its inputs")
    return policy


def read_recipe(root):
    """The steady calibration method a root declares; legacy roots calibrated after the timeout."""
    path = Path(root)/RECIPE_FILE
    if not path.exists():
        return dict(schemaVersion=1, steadyCalibration="after-timeout")
    recipe = load(path)
    require(isinstance(recipe, dict) and set(recipe) == {"schemaVersion", "steadyCalibration"} and
            recipe["schemaVersion"] == 1 and recipe["steadyCalibration"] in STEADY_CALIBRATIONS,
            "Invalid readiness calibration recipe")
    return recipe


def effect_values(effect):
    batch = effect["batch"]
    require(batch["succeeded"], "Calibration inputs were not consumed")
    return {item["id"]: item["value"] for item in batch["values"]}, batch


def first_pass(status, reason="hardReset"):
    """Bounded refresh and steady estimates of a calibration run at the uncalibrated minimum."""
    field = status["frame"]["giField"]
    excluded = field["activeBlocks"] == 0 and field["excludedBlocks"] > 0
    require((field["refreshNoiseSamples"] >= REFRESH_SAMPLES or excluded) and
            field["steadyNoiseSamples"] == NOISE_SAMPLES and
            all(math.isfinite(field[k]) and field[k] >= 0 for k in ("refreshNoiseEstimate", "steadyNoiseEstimate")),
            f"{reason} calibration lacks its bounded refresh and steady measurements")
    if "summary" in field and field["summary"] and field["summary"].get("profile"):
        sigmas = profile_policy(field["summary"]["profile"])[reason]
        require(sigmas["refreshSigma"] == 0 and sigmas["steadySigma"] == 0,
                f"{reason} calibration did not run at the uncalibrated minimum")
    return field


def steady_after_handoff(directory, calibration, reason="hardReset"):
    """Validates a root's after-handoff steady calibration and returns its giField status.

    The second field adopts only the first pass's refresh sigma, with steady sigma at the
    uncalibrated minimum, and it must be a new field. A first pass whose refresh sigma is
    the uncalibrated minimum already gated its handoff with it, and its unchanged inputs
    would not restart a second field; it is then its own steady calibration.
    """
    refresh_input, steady_input = (f"gi_reference.{name}" for name in SIGMA_INPUTS[reason])
    prefix = "" if reason == "hardReset" else "lighting-"
    if f32(calibration["refreshNoiseEstimate"]) == 0:
        require(not (Path(directory)/(prefix+STEADY_EFFECT_FILE)).exists(),
                f"{reason} steady calibration restarted a field its inputs could not change")
        receipt = load(Path(directory)/(prefix+STEADY_STATUS_FILE))
        field = receipt["frame"]["giField"]
        require(receipt.get("reusedCalibrationPass") is True and
                all(field[k] == calibration[k] for k in ("generation", "refreshNoiseSamples", "steadyNoiseSamples")) and
                all(f32(field[k]) == f32(calibration[k]) for k in ("refreshNoiseEstimate", "steadyNoiseEstimate")),
                f"{reason} steady calibration is not its uncalibrated first pass")
        return field
    values, batch = effect_values(load(Path(directory)/(prefix+STEADY_EFFECT_FILE)))
    require(batch["impact"] == "RestartSession" and set(values) == {refresh_input, steady_input} and
            f32(values[refresh_input]) == f32(calibration["refreshNoiseEstimate"]) and values[steady_input] == 0,
            f"{reason} steady calibration inputs are not the adopted refresh sigma with uncalibrated steady sigma")
    field = load(Path(directory)/(prefix+STEADY_STATUS_FILE))["frame"]["giField"]
    require(field["enabled"] and not field["failed"] and field["generation"] > calibration["generation"] and
            field["steadyNoiseSamples"] == NOISE_SAMPLES and math.isfinite(field["steadyNoiseEstimate"]) and
            field["steadyNoiseEstimate"] >= 0, f"{reason} steady calibration lacks a new bounded steady measurement")
    summary = field.get("summary")
    if summary and summary.get("profile"):
        sigmas = profile_policy(summary["profile"])[reason]
        require(f32(sigmas["refreshSigma"]) == f32(calibration["refreshNoiseEstimate"]) and
                sigmas["steadySigma"] == 0,
                f"{reason} steady calibration field did not adopt the measured refresh sigma")
    return field


def adopted(directory, calibration, recipe, reason="hardReset"):
    """The refresh and steady sigma a gated run must adopt for one reason."""
    prefix = "" if reason == "hardReset" else "lighting-"
    present = (Path(directory)/(prefix+STEADY_STATUS_FILE)).exists() or \
        (Path(directory)/(prefix+STEADY_EFFECT_FILE)).exists()
    if recipe["steadyCalibration"] == "after-timeout":
        require(not present, "An after-timeout root holds an after-handoff steady calibration")
        return dict(refreshSigma=calibration["refreshNoiseEstimate"], steadySigma=calibration["steadyNoiseEstimate"],
                    method="after-timeout")
    require(present, "An after-handoff root lacks its steady calibration")
    steady = steady_after_handoff(directory, calibration, reason)
    result = dict(refreshSigma=calibration["refreshNoiseEstimate"], steadySigma=steady["steadyNoiseEstimate"],
                  method="after-handoff", steadyCalibrationGeneration=steady["generation"],
                  timeoutSteadySigma=calibration["steadyNoiseEstimate"])
    if steady["generation"] == calibration["generation"]:
        result["reusedCalibrationPass"] = True
    return result


def self_test():
    checks = 0

    def rejects(function, *args):
        nonlocal checks
        try:
            function(*args)
        except (ValueError, KeyError, TypeError):
            checks += 1
            return
        raise AssertionError(f"{function.__name__} accepted invalid evidence")

    def check(condition, message):
        nonlocal checks
        require(condition, message)
        checks += 1

    legacy = dict(schemaVersion=1, readiness=dict(enabled=True, noiseSigma=0.2, steadyNoiseSigma=0.01,
                                                  refreshHysteresis=0.5, progressiveRefresh=False))
    policy = profile_policy(legacy)
    check(policy["hardReset"] == policy["lighting"] == dict(integration="rapid", refreshSigma=0.2, steadySigma=0.01),
          "A schema 1 profile declared one policy for both reasons")
    current = dict(schemaVersion=2, readiness=dict(
        enabled=True, refreshHysteresis=0.5,
        hardReset=dict(integration="progressive", refreshSigma=0.3, steadySigma=0.1),
        lighting=dict(integration="rapid", refreshSigma=20.0, steadySigma=0.4)))
    check(profile_policy(current)["lighting"]["refreshSigma"] == 20, "Schema 2 keeps per-reason sigma")
    for path, value in ((("readiness", "hardReset", "integration"), "fast"),
                        (("readiness", "lighting", "steadySigma"), -1.0),
                        (("readiness", "hardReset", "extra"), 1), (("schemaVersion",), 3)):
        broken = copy.deepcopy(current)
        target = broken
        for key in path[:-1]:
            target = target[key]
        target[path[-1]] = value
        rejects(profile_policy, broken)
    check(parameter_policy({}, per_reason=False) == dict(hardReset="rapid", lighting="rapid") and
          parameter_policy(dict(field_progressive_refresh=True), per_reason=False) ==
          dict(hardReset="progressive", lighting="progressive"), "A schema 1 binary's one flag selects both reasons")
    check(parameter_policy({}) == dict(hardReset="progressive", lighting="rapid") and
          parameter_policy(dict(field_rapid_hard_reset=True, field_progressive_refresh=True)) ==
          dict(hardReset="rapid", lighting="progressive"), "Per-reason inputs select each reason separately")
    check(require_policy_matches(current, {}) is not None and require_policy_matches(legacy, {}) is not None,
          "Default inputs match each binary's default profile")
    rejects(require_policy_matches, current, dict(field_rapid_hard_reset=True))
    rejects(require_policy_matches, legacy, dict(field_progressive_refresh=True))
    rejects(parameter_policy, dict(field_rapid_hard_reset=1))
    rejects(parameter_policy, dict(field_rapid_hard_reset=False), False)

    calibration = dict(generation=4, refreshNoiseEstimate=0.31999, steadyNoiseEstimate=0.05,
                       refreshNoiseSamples=64, steadyNoiseSamples=64, activeBlocks=10, excludedBlocks=0)
    check(first_pass(dict(frame=dict(giField=calibration)))["generation"] == 4, "A bounded first pass is accepted")
    rejects(first_pass, dict(frame=dict(giField=dict(calibration, steadyNoiseSamples=63))))
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        check(read_recipe(root)["steadyCalibration"] == "after-timeout", "A root without a recipe ran after the timeout")
        check(adopted(root, calibration, read_recipe(root))["steadySigma"] == 0.05,
              "After-timeout adopts the first pass's steady sigma")
        (root/RECIPE_FILE).write_text(json.dumps(dict(schemaVersion=1, steadyCalibration="after-handoff")))
        recipe = read_recipe(root)
        rejects(adopted, root, calibration, recipe)
        effect = dict(batch=dict(succeeded=True, impact="RestartSession", values=[
            dict(id="gi_reference.field_refresh_noise", value=f32(0.31999)),
            dict(id="gi_reference.field_steady_noise", value=0.0)]))
        status = dict(frame=dict(giField=dict(enabled=True, failed=False, generation=5, steadyNoiseSamples=64,
                                              steadyNoiseEstimate=0.02, summary=dict(profile=dict(
                                                  current, readiness=dict(current["readiness"], hardReset=dict(
                                                      integration="progressive", refreshSigma=f32(0.31999),
                                                      steadySigma=0.0)))))))
        (root/STEADY_EFFECT_FILE).write_text(json.dumps(effect))
        (root/STEADY_STATUS_FILE).write_text(json.dumps(status))
        result = adopted(root, calibration, recipe)
        check(result["steadySigma"] == 0.02 and result["refreshSigma"] == 0.31999 and
              result["timeoutSteadySigma"] == 0.05, "After-handoff adopts the second field's steady sigma")
        for change in (lambda e, s: e["batch"]["values"][1].update(value=0.05),
                       lambda e, s: e["batch"].update(impact="Immediate"),
                       lambda e, s: s["frame"]["giField"].update(generation=4),
                       lambda e, s: s["frame"]["giField"].update(steadyNoiseSamples=63),
                       lambda e, s: s["frame"]["giField"]["summary"]["profile"]["readiness"]["hardReset"].update(
                           refreshSigma=0.0)):
            bad_effect, bad_status = copy.deepcopy(effect), copy.deepcopy(status)
            change(bad_effect, bad_status)
            (root/STEADY_EFFECT_FILE).write_text(json.dumps(bad_effect))
            (root/STEADY_STATUS_FILE).write_text(json.dumps(bad_status))
            rejects(adopted, root, calibration, recipe)
        zero = dict(calibration, refreshNoiseEstimate=0.0, steadyNoiseEstimate=0.0, refreshNoiseSamples=0,
                    activeBlocks=0, excludedBlocks=8)
        reuse = dict(reusedCalibrationPass=True, frame=dict(giField=zero))
        (root/STEADY_EFFECT_FILE).unlink()
        (root/STEADY_STATUS_FILE).write_text(json.dumps(reuse))
        reused = adopted(root, zero, recipe)
        check(reused["reusedCalibrationPass"] and reused["steadyCalibrationGeneration"] == 4 and
              reused["steadySigma"] == 0, "A first pass at the minimum refresh sigma is its own steady calibration")
        for bad in (dict(reuse, reusedCalibrationPass=False),
                    dict(reuse, frame=dict(giField=dict(zero, generation=5))),
                    dict(reuse, frame=dict(giField=dict(zero, steadyNoiseEstimate=0.01)))):
            (root/STEADY_STATUS_FILE).write_text(json.dumps(bad))
            rejects(adopted, root, zero, recipe)
        (root/STEADY_STATUS_FILE).write_text(json.dumps(reuse))
        (root/STEADY_EFFECT_FILE).write_text(json.dumps(effect))
        rejects(adopted, root, zero, recipe)
        (root/RECIPE_FILE).write_text(json.dumps(dict(schemaVersion=1, steadyCalibration="after-timeout")))
        rejects(adopted, root, calibration, read_recipe(root))
        (root/RECIPE_FILE).write_text(json.dumps(dict(schemaVersion=1, steadyCalibration="after-ready")))
        rejects(read_recipe, root)
    print(f"GI field readiness calibration self-test passed ({checks} checks)")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--self-test", action="store_true", required=True)
    parser.parse_args()
    self_test()


if __name__ == "__main__":
    main()
