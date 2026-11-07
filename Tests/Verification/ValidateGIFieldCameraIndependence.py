"""F6 camera-independence measurements on held pose-specific fields.

Lookup contracts are required: each held field state is fixed at the declared update,
each view matches the FP64 lookup oracle, and the frozen world-space receiver list
matches it from both viewers, including the per-sample cross-view difference.
World-field differences between pose-specific runs and the static energy and
chromaticity gates are measurements; their tolerances belong to the acceptance plan.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

from ValidateGIFieldSampling import oracle, volume
from ValidateGIReferenceLighting import (F6_EXCLUDED_WALL, F6_OPENING_INSTANCES, LUMA, f4_oracle,
                                         masks as reference_masks, relative_error)

POSES = ("PoseWest", "PoseEast")
POSE_POSITIONS = {"PoseWest": (-0.5, 1.6, 1.4), "PoseEast": (0.5, 1.6, 1.4)}
LATTICE_COLUMNS, LATTICE_ROWS = 9, 7
SEED = 61051
VIEW_BIAS = .15
HYSTERESIS = .998
INTERIOR = 30
SCALED_E_GATE = 2e-3
QUERY_GATE = 1e-4
ENERGY_GATE = .10
CHROMATICITY_GATE = .02
REFERENCE_ORACLE_GATE = .01
RASTER_SAMPLES = 4096
MINIMUM, SPACING, COUNTS = volume(4)
ATLASES = ("GI.Field.Irradiance", "GI.Field.Distance", "GI.Field.Classification")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def lattice():
    """The frozen F6 list: X=-0.4+0.1i, Z=-1.1+0.1j on the floor, ordered by Z, then X."""
    position = np.array([[-0.4+0.1*i, 0.0, -1.1+0.1*j] for j in range(LATTICE_ROWS)
                         for i in range(LATTICE_COLUMNS)]).astype(np.float32)
    normal = np.tile(np.array([0, 1, 0], dtype=np.float32), (len(position), 1))
    return position, normal


def read_export(directory):
    records = list(directory.glob("*.resource-export.json"))
    require(len(records) == 1, f"Expected one export: {directory}")
    record = load(records[0])
    values = {t["address"]["resource"]: np.load(directory/t["array"], allow_pickle=False) for t in record["targets"]}
    require(all(np.isfinite(v).all() for v in values.values() if v.dtype.kind == "f"),
            f"Nonfinite export: {directory}")
    graphs = list(directory.glob("*.graph.json"))
    return record, values, load(graphs[0]) if graphs else None


def lab_parameters(frame):
    return {p["id"].removeprefix("gi_reference."): p["value"] for p in frame["parameters"]["values"]
            if p["id"].startswith("gi_reference.")}


def executed_passes(graph):
    names = {p["index"]: p["name"] for p in graph["compile"]["passes"]}
    return [names[i] for i in graph["execution"]["executedPassIndices"]]


def scaled_error(actual, expected):
    return float(np.max(np.abs(actual-expected)/np.maximum(1., np.abs(expected))))


def static_gate(field_mean, reference_mean):
    field_mean, reference_mean = np.asarray(field_mean, dtype=float), np.asarray(reference_mean, dtype=float)
    bias = float((field_mean-reference_mean)@LUMA/max(float(reference_mean@LUMA), 1e-6))
    chromaticity = float(np.max(np.abs(field_mean[:2]/max(field_mean.sum(), 1e-6) -
                                       reference_mean[:2]/max(reference_mean.sum(), 1e-6))))
    return dict(fieldMean=field_mean.tolist(), referenceMean=reference_mean.tolist(), relativeBias=bias,
                chromaticityError=chromaticity,
                passed=bool(abs(bias) <= ENERGY_GATE and chromaticity <= CHROMATICITY_GATE))


def difference(a, b, floor):
    """Report-only world-field comparison of one exported quantity."""
    a, b = np.asarray(a), np.asarray(b)
    # Storage identity is separate from numeric error: signed zeros and distinct
    # dtypes can compare numerically equal without retaining the same bytes.
    identical = a.dtype == b.dtype and a.shape == b.shape and a.tobytes() == b.tobytes()
    a, b = np.asarray(a, dtype=np.float64), np.asarray(b, dtype=np.float64)
    delta = np.abs(a-b)
    return dict(bitIdentical=bool(identical), differingTexels=int(np.count_nonzero(delta)),
                maximumAbsolute=float(delta.max()) if delta.size else 0.0,
                maximumRelative=float((delta/np.maximum(np.abs(b), floor)).max()) if delta.size else 0.0,
                relativeFloor=floor)


def query_cross_view(energies, expected):
    """Per-sample cross-view difference against the oracle-predicted view-bias difference."""
    observed = energies[1]-energies[0]
    predicted = expected[1]-expected[0]
    scale = np.maximum(1., np.maximum(np.abs(expected[0]), np.abs(expected[1])))
    error = float(np.max(np.abs(observed-predicted)/scale))
    magnitude = np.max(np.abs(predicted)/scale, axis=1)
    return dict(scaledError=error, passed=bool(error < SCALED_E_GATE),
                predictedScaledDifference=dict(maximum=float(magnitude.max()), mean=float(magnitude.mean())),
                observedScaledDifferenceMaximum=float(np.max(np.max(np.abs(observed)/scale, axis=1))))


def check_profile(summary, arm):
    profile = summary["profile"]
    require(profile is not None and profile["counts"] == COUNTS.tolist() and
            np.allclose(profile["minimum"], MINIMUM) and profile["spacing"] == SPACING and
            profile["irradianceInteriorWidth"] == INTERIOR and profile["seed"] == SEED and
            abs(profile["viewBias"]-VIEW_BIAS) < 1e-6 and abs(profile["hysteresis"]-HYSTERESIS) < 1e-6 and
            profile["receiverSampling"] and not profile["feedback"] and not profile["progressiveSampling"] and
            profile["readiness"]["enabled"] == (arm == "on") and not profile["readiness"]["progressiveRefresh"],
            "Field profile differs from the F4 precision profile under the default recipe")
    return profile


def field_summary(frame):
    field = frame["readiness"]["giField"]
    require(field is not None and field.get("summary"), "Export has no field summary")
    return field, field["summary"]


def inspect_reference(root, backend, pose, samples):
    directory = root/backend/"reference"/pose
    record, values, _ = read_export(directory)
    frame = record["frame"]
    require(frame["readiness"]["ready"] and frame["camera"]["referenceView"] == f"F6_{pose}",
            f"Unready or wrong-view F6 reference: {pose}")
    gates = [g for g in frame["readiness"]["gates"] if g["name"] == "gi-reference"]
    require(len(gates) == 1 and gates[0]["state"] == "ready", "F6 reference is not GPU-complete")
    parameters = lab_parameters(frame)
    require(parameters.get("fixture") == 4 and parameters.get("samples") == samples and
            parameters.get("depth", 1) == 1 and parameters.get("seed", SEED) == SEED and
            not parameters.get("field_enabled", False), "F6 reference parameters differ")
    values = {name: array[0] for name, array in values.items()}
    e = values["GI.Reference.Irradiance"]
    covered = values["GI.Reference.Position"][..., 3] == 1
    require(covered.any() and np.array_equal(e[..., 3] == 1, covered), "Reference coverage differs")
    sun, sky = values["GI.Reference.SunIrradiance"], values["GI.Reference.EnvironmentIrradiance"]
    require(np.allclose(e[..., :3], sun[..., :3]+sky[..., :3], rtol=1e-6, atol=1e-6),
            "Sun/environment components do not sum to E")
    # Visibility comes from primary receiver identities, not from the projection.
    identity = values["GI.Reference.Identity"][..., 0]
    visible = {str(i): int(np.count_nonzero(covered & (identity == i))) for i in range(8)}
    require(all(visible[str(i)] == 0 for i in F6_OPENING_INSTANCES), f"F6 {pose} sees the sunlit opening")
    require(visible[str(F6_EXCLUDED_WALL[f"F6_{pose}"])] == 0, f"F6 {pose} sees its excluded coloured wall")
    patch = reference_masks({"fixture": 4, "view": f"F6_{pose}"}, values, frame["camera"])["patch"]
    require(patch.sum() >= 32, f"Empty F6 patch: {pose}")
    position = values["GI.Reference.Position"][patch, :3].astype(np.float64)
    oracle_sun, oracle_sky = f4_oracle(position, True)
    # The analytic D=1 oracle is the shared world-space reference only if both
    # poses' GPU references confirm it on their own patch pixels.
    sun_error = relative_error(sun[patch, :3].mean(0, dtype=np.float64), oracle_sun.mean(0))
    sky_error = relative_error(sky[patch, :3].mean(0, dtype=np.float64), oracle_sky.mean(0))
    require(sun_error < REFERENCE_ORACLE_GATE and sky_error < REFERENCE_ORACLE_GATE,
            f"GPU D=1 reference does not confirm the shared analytic reference: {pose}")
    return dict(values=values, patch=patch, camera=frame["camera"],
                report=dict(patchPixels=int(patch.sum()), visibleInstancePixels=visible,
                            sunOracleRelativeError=sun_error, skyOracleRelativeError=sky_error,
                            referenceMean=e[patch, :3].mean(0, dtype=np.float64).tolist(),
                            analyticMean=(oracle_sun+oracle_sky).mean(0).tolist(),
                            maskSha256=hashlib.sha256(patch.tobytes()).hexdigest()))


def atlases(values):
    classes = values["GI.Field.Classification"]
    require(classes.dtype == np.dtype("<u4") and classes.shape == (COUNTS[1], COUNTS[2], COUNTS[0], 1) and
            np.isin(classes, [1, 2]).all(), "Field classification grid differs or is unclassified")
    tile = INTERIOR+2
    require(values["GI.Field.Irradiance"].shape == (COUNTS[1], COUNTS[2]*tile, COUNTS[0]*tile, 4),
            "Field irradiance profile differs")
    return {name: values[name] for name in ATLASES}


def inspect_raster(values, frame, field, reference):
    position = values["GI.Receiver.Position"][0]
    normal = values["GI.Receiver.ShadingNormal"][0, ..., :3]
    vertex = values["GI.Receiver.VertexNormal"][0, ..., :3]
    energy = values["GI.Field.ReceiverIrradiance"][0]
    query = values["GI.Field.ReceiverQuery"][0]
    covered = position[..., 3] == 1
    require(covered.any() and np.array_equal(energy[..., 3], covered.astype(float)) and
            np.array_equal(query[..., 3], covered.astype(float)), "Field diagnostics dropped opaque receivers")
    matched = reference["values"]["GI.Receiver.Position"]
    require(np.array_equal(covered, matched[..., 3] == 1) and
            np.max(np.abs(position[covered, :3]-matched[covered, :3])) < 1e-4,
            "Field raster receivers differ from the matched reference")
    patch = reference["patch"]
    coords = np.flatnonzero(covered)
    coords = np.union1d(coords[::max(1, len(coords)//RASTER_SAMPLES)], np.flatnonzero(patch))
    camera = np.asarray(frame["camera"]["position"], dtype=np.float64)
    expected, expected_query = oracle(position.reshape(-1, 4)[coords, :3].astype(float),
        normal.reshape(-1, 3)[coords].astype(float), vertex.reshape(-1, 3)[coords].astype(float), camera,
        MINIMUM, SPACING, COUNTS, field["GI.Field.Irradiance"], field["GI.Field.Distance"],
        field["GI.Field.Classification"], view_bias=VIEW_BIAS)
    actual = energy.reshape(-1, 4)[coords, :3].astype(np.float64)
    actual_query = query.reshape(-1, 4)[coords]
    error = scaled_error(actual, expected)
    require(error < SCALED_E_GATE, f"Raster lookup differs from FP64 integration: {error}")
    require(np.max(np.abs(actual_query[:, :2]-expected_query[:, :2])) < QUERY_GATE and
            np.array_equal(actual_query[:, 2], expected_query[:, 2]), "Raster support/coverage differs from oracle")
    field_mean = energy[patch, :3].mean(0, dtype=np.float64)
    reference_mean = reference["values"]["GI.Reference.Irradiance"][patch, :3].mean(0, dtype=np.float64)
    analytic = sum(f4_oracle(position[patch, :3].astype(np.float64), True)).mean(0)
    return dict(checkedPixels=int(len(coords)), patchPixels=int(patch.sum()), scaledError=error,
                supports={str(s): int((query[covered, 2] == s).sum()) for s in (1, 2, 3, 4)},
                staticGate=static_gate(field_mean, reference_mean),
                analyticRelativeBias=float((field_mean-analytic)@LUMA/max(float(analytic@LUMA), 1e-6)))


def inspect_list(values, field):
    data = values["GI.Field.ReceiverList"]
    require(data.dtype == np.dtype("<f4") and data.ndim == 4 and data.shape[0] == 1, "Receiver-list export layout")
    data = data[0]
    position, normal = lattice()
    require(data.shape == (3+3*len(POSES), len(position), 4), "Receiver-list extent differs")
    require(np.array_equal(data[0, :, :3], position) and np.all(data[0, :, 3] == 1) and
            np.array_equal(data[1, :, :3], normal) and np.array_equal(data[2, :, :3], normal) and
            np.all(data[1:3, :, 3] == 0), "Receiver-list inputs differ from the frozen lattice")
    energies, expected_energies, views = [], [], {}
    for index, pose in enumerate(POSES):
        viewer = data[3+3*index]
        require(np.all(viewer[:, :3] == np.float32(POSE_POSITIONS[pose])) and np.all(viewer[:, 3] == index),
                f"Receiver-list viewer differs: {pose}")
        energy, query = data[4+3*index], data[5+3*index]
        require(np.all(energy[:, 3] == 1) and np.all(query[:, 3] == 1), "Receiver list sampled an invalid field")
        expected, expected_query = oracle(position.astype(float), normal.astype(float), normal.astype(float),
            viewer[0, :3].astype(float), MINIMUM, SPACING, COUNTS, field["GI.Field.Irradiance"],
            field["GI.Field.Distance"], field["GI.Field.Classification"], view_bias=VIEW_BIAS)
        actual = energy[:, :3].astype(np.float64)
        error = scaled_error(actual, expected)
        require(error < SCALED_E_GATE, f"Receiver-list lookup differs from FP64 integration ({pose}): {error}")
        require(np.max(np.abs(query[:, :2]-expected_query[:, :2])) < QUERY_GATE and
                np.array_equal(query[:, 2], expected_query[:, 2]), f"Receiver-list support differs ({pose})")
        sun, sky = f4_oracle(position.astype(np.float64), True)
        energies.append(actual)
        expected_energies.append(expected)
        views[pose] = dict(scaledError=error, supports={str(s): int((query[:, 2] == s).sum()) for s in (1, 2, 3, 4)},
                           staticGate=static_gate(actual.mean(0), (sun+sky).mean(0)))
    cross = query_cross_view(energies, expected_energies)
    require(cross["passed"], f"Cross-view difference differs from the predicted view bias: {cross['scaledError']}")
    return dict(samples=len(position), views=views, crossView=cross), data


def inspect_run(root, backend, arm, run_pose, limit, references):
    directory = root/backend/f"readiness-{arm}"/run_pose
    pose_record, _, _ = read_export(directory/"pose")
    pose_frame = pose_record["frame"]
    _, pose_summary = field_summary(pose_frame)
    require(pose_frame["camera"]["referenceView"] == f"F6_{run_pose}" and not pose_summary["updateHeld"] and
            1 <= pose_summary["frameUpdate"] < limit, "The pose did not take effect before the hold")
    generation = pose_summary["field"]["generation"]
    profile = check_profile(pose_summary, arm)
    order = [run_pose]+[pose for pose in POSES if pose != run_pose]
    held, raster, lists, revisions = {}, {}, {}, []
    for view_pose in order:
        for kind in ("raster", "list"):
            record, values, graph = read_export(directory/f"{view_pose}-{kind}")
            frame = record["frame"]
            parameters = lab_parameters(frame)
            require(parameters.get("field_enabled") and parameters.get("field_update_limit") == limit and
                    parameters.get("fixture") == 4, "Held export parameters differ")
            _, summary = field_summary(frame)
            require(frame["camera"]["referenceView"] == f"F6_{view_pose}", "Held export has the wrong view")
            require(summary["updateHeld"] and summary["updateLimit"] == limit and
                    summary["submittedUpdates"] == limit and summary["frameUpdate"] == limit and
                    summary["initialized"] and not summary["invalid"], "Field was not held at the declared update")
            require(summary["field"]["generation"] == generation and check_profile(summary, arm) == profile,
                    "A camera switch reset or replaced the field generation")
            require(frame["temporal"]["status"] == "disabled" and frame["temporal"]["jitterPixels"] == [0, 0],
                    "F6 receivers must be unjittered")
            view = summary["receiverView"]
            require(np.all(np.float32(view["cameraPosition"]) == np.float32(POSE_POSITIONS[view_pose])),
                    "Receiver view camera differs from the pose")
            revisions.append(view["revision"])
            executed = executed_passes(graph)
            require("GI.Field.Classify" not in executed, "A held static field reclassified geometry")
            if kind == "list":
                require(executed.index("GI.Field.ReceiverList") < executed.index("GI.Field.Publish"),
                        "Field export closed access before the receiver list")
            field = atlases(values)
            held[f"{view_pose}-{kind}"] = field
            if kind == "raster":
                raster[view_pose] = inspect_raster(values, frame, field, references[view_pose])
            else:
                lists[view_pose] = inspect_list(values, field)
    first = held[f"{run_pose}-raster"]
    require(all(np.array_equal(first[name], other[name]) for other in held.values() for name in ATLASES),
            "Held field contents changed between exports")
    require(revisions[2] > revisions[1], "The camera switch did not advance the receiver view")
    require(np.array_equal(lists[order[0]][1], lists[order[1]][1]),
            "The world-space receiver list depends on the rendering camera")
    for status_file in ("held-status.json", "final-status.json"):
        summary = load(directory/status_file)["frame"]["giField"]["summary"]
        require(summary["field"]["generation"] == generation and summary["submittedUpdates"] == limit and
                summary["updateHeld"], f"Field state changed around the held exports: {status_file}")
    return dict(poseUpdate=pose_summary["frameUpdate"], heldUpdate=limit, generation=generation,
                receiverViewRevisions=revisions, raster=raster, receiverList=lists[order[0]][0],
                readinessState=load(directory/"held-status.json")["frame"]["giField"]["state"]), \
        dict(atlases=first, receiverList=lists[order[0]][1])


def compare_runs(states):
    west, east = states["PoseWest"], states["PoseEast"]
    classification = bool(np.array_equal(west["atlases"]["GI.Field.Classification"],
                                         east["atlases"]["GI.Field.Classification"]))
    irradiance = difference(west["atlases"]["GI.Field.Irradiance"][..., :3],
                            east["atlases"]["GI.Field.Irradiance"][..., :3], 1e-3)
    moments = {f"moment{index}": difference(west["atlases"]["GI.Field.Distance"][..., index],
                                            east["atlases"]["GI.Field.Distance"][..., index], 1e-3)
               for index in range(west["atlases"]["GI.Field.Distance"].shape[-1])}
    rows = [4+3*index for index in range(len(POSES))]
    queries = difference(west["receiverList"][rows, :, :3], east["receiverList"][rows, :, :3], 1.)
    return dict(classificationIdentical=classification, irradiance=irradiance, distance=moments,
                receiverListIrradiance=queries, toleranceStatus="Undeclared")


def validate(root):
    plan = load(root/"plan.json")
    limit = plan["updateLimit"]
    report = []
    for backend in plan["backends"]:
        references = {pose: inspect_reference(root, backend, pose, plan["referenceSamples"]) for pose in POSES}
        arms = {}
        for arm in plan["readiness"]:
            runs, states = {}, {}
            for run_pose in POSES:
                runs[run_pose], states[run_pose] = inspect_run(root, backend, arm, run_pose, limit, references)
            arms[arm] = dict(runs=runs, worldField=compare_runs(states))
        report.append(dict(rhi=backend, references={pose: value["report"] for pose, value in references.items()},
                           readiness=arms))
    return dict(status="passed", lookupContractsPassed=True, backends=report,
                rules=dict(scaledEGate=SCALED_E_GATE, queryGate=QUERY_GATE, energyGate=ENERGY_GATE,
                           chromaticityGate=CHROMATICITY_GATE, referenceOracleGate=REFERENCE_ORACLE_GATE,
                           sharedReference="analytic F4 D=1 floor oracle, confirmed by each pose's GPU reference",
                           worldField="report-only until the acceptance plan declares per-quantity tolerances"),
                cameraIndependenceQualified=False)


def self_test():
    position, normal = lattice()
    require(len(position) == 63 and np.allclose(position[0], [-.4, 0, -1.1]) and
            np.allclose(position[8], [.4, 0, -1.1]) and np.allclose(position[9], [-.4, 0, -1.]) and
            np.allclose(position[-1], [.4, 0, -.5]), "Frozen lattice order differs")
    require(np.all(np.abs(position[:, 0]) <= .4+1e-6) and np.all(position[:, 2] >= -1.1-1e-6) and
            np.all(position[:, 2] <= -.5+1e-6), "Frozen lattice leaves the F6 patch")
    # A field varying along X makes the view bias observable between the viewers.
    counts = np.array([3, 3, 3])
    atlas = np.ones((3, 24, 24, 4))
    for x in range(3):
        atlas[:, :, x*8:(x+1)*8, :3] = 1+x
    moments = np.empty((3, 48, 48, 2))
    moments[:] = [10, 100]
    classes = np.ones((3, 3, 3, 1), dtype=np.uint32)
    points = np.array([[.7, .4, .9], [1.1, .6, 1.2]])
    normals = np.tile([0., 1., 0.], (2, 1))
    viewers = (np.array([-1., 2., 3.]), np.array([3., 2., 3.]))
    expected = [oracle(points, normals, normals, viewer, np.zeros(3), 1., counts, atlas, moments, classes,
                       view_bias=VIEW_BIAS)[0] for viewer in viewers]
    require(np.max(np.abs(expected[1]-expected[0])) > 1e-2, "Synthetic field does not exercise the view bias")
    exact = query_cross_view(expected, expected)
    require(exact["passed"] and exact["scaledError"] == 0 and exact["predictedScaledDifference"]["maximum"] > 0,
            "Exact view-bias difference rejected or not reported")
    shifted = [expected[0], expected[1]+3e-3*np.maximum(1., np.abs(expected[1]))]
    require(not query_cross_view(shifted, expected)["passed"], "Cross-view error beyond the gate accepted")
    swapped = query_cross_view([expected[1], expected[0]], expected)
    require(not swapped["passed"], "A view-bias difference with the wrong sign accepted")
    require(static_gate([1.05]*3, [1]*3)["passed"] and not static_gate([1.15]*3, [1]*3)["passed"] and
            not static_gate([1.1, 1, .9], [1]*3)["passed"], "Static energy/chromaticity gates")
    same = difference(atlas, atlas, 1e-3)
    changed = atlas.copy()
    changed[0, 0, 0, 0] += 1e-3
    require(same["bitIdentical"] and same["maximumAbsolute"] == 0 and
            difference(changed, atlas, 1e-3)["differingTexels"] == 1, "World-field difference report")
    positive = np.zeros(1, dtype=np.float32)
    signed = difference(positive, -positive, 1e-3)
    require(not signed["bitIdentical"] and signed["maximumAbsolute"] == 0 and
            signed["differingTexels"] == 0, "Signed zero is numerically equal but not byte-identical")
    require(not difference(positive, positive.astype(np.float64), 1e-3)["bitIdentical"],
            "A different payload dtype cannot be reported as byte-identical")
    print("F6 lattice, cross-view view-bias, static-gate and world-field report checks passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        result = validate(args.root)
        (args.root/"validation.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print("F6 held fields, per-view lookups and receiver-list cross-view contracts passed")
