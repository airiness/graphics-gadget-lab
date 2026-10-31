"""Locked Coastal receiver/mask inputs and the small smooth-receiver GPU contract.

All covered receivers and every path are validated BEFORE ROI selection. Masks
retain the original rectangles and opaque membership, including metallic pixels.
Incident E convergence does not establish a diffuse response at those receivers.
This module checks inputs/calculation; sampling/depth certification is external.
"""
import copy
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

from ValidateGIReferenceLighting import require, overlay, DEFAULTS
from ValidateGIReferenceTransport import check_path_diagnostics

INPUT_PATH = Path(__file__).parent / "GlobalIllumination/Inputs.json"
COASTAL_VIEWS = ("Retreat_SeaPassage", "Retreat_Courtyard", "GI_InteriorExterior", "Retreat_Lounge")
CONTRACT_VIEWS = ("Contract_SmallReceiver", "Contract_SmallReceiverGrazing")
COMPONENTS = ("Irradiance", "SunIrradiance", "EnvironmentIrradiance")


def input_lock():
    return json.loads(INPUT_PATH.read_text(encoding="utf-8-sig"))


def load_export(directory):
    receipt = json.loads((directory / "capture.json").read_text(encoding="utf-8-sig"))
    require(receipt["status"] == "completed" and receipt["publication"] == "Published", "Unpublished reference")
    paths = list(directory.glob("*.resource-export.json"))
    require(len(paths) == 1, "Expected exactly one bounded resource group")
    record = json.loads(paths[0].read_text(encoding="utf-8-sig"))
    values = {}
    for target in record["targets"]:
        a = np.load(directory / target["array"], allow_pickle=False)
        require(a.dtype == np.dtype("<f4") and a.ndim == 4 and a.shape[0] == 1 and
                np.isfinite(a).all(), "Invalid/nonfinite reference or receiver payload")
        name = target["address"]["resource"]
        require(name not in values, "Duplicate reference resource")
        values[name] = a[0]
    return values, record["frame"]


def validate_arrays(values, receivers, depth):
    face = receivers["GI.Receiver.FaceNormal"]
    covered = face[..., 3] == 1
    require(covered.any() and np.isin(face[..., 3], [0, 1]).all(), "Missing/unsupported opaque receivers")
    require(np.array_equal(covered, receivers["DisplayView.DepthBuffer"][..., 0] > 0), "Opaque depth/receiver mismatch")
    position = receivers["GI.Receiver.Position"]
    require(np.array_equal(position[..., 3], face[..., 3]), "Raster interpolant coverage differs")
    for name in ("FaceNormal", "VertexNormal", "ShadingNormal"):
        a = receivers["GI.Receiver." + name]
        require(np.isfinite(a).all() and np.all(np.abs(np.linalg.norm(a[covered, :3], axis=-1) - 1) < .002),
                "Invalid receiver " + name)
    e = values["GI.Reference.Irradiance"]
    require(np.all(e[..., 3][covered] == 1) and np.all(e[..., 3][~covered] == 0),
            "Unmatched/invalid covered receiver cannot be excluded")
    for name in COMPONENTS:
        a = values["GI.Reference." + name]
        require(np.isfinite(a).all() and np.all(a[..., :3] >= 0), "Invalid/nonfinite component, including outside masks")
    require(np.allclose(e[..., :3], values["GI.Reference.SunIrradiance"][..., :3] +
            values["GI.Reference.EnvironmentIrradiance"][..., :3], rtol=1e-6, atol=1e-6), "Component sum differs")
    ids = values["GI.Reference.Identity"]
    require(np.all(ids[..., :2][covered] >= 0) and np.all(ids[..., :2][covered] == np.floor(ids[..., :2][covered])) and
            np.isin(ids[..., 3][covered], [1, 2, 3]).all(), "Invalid native triangle identity/match kind")
    native = values["GI.Reference.Position"]
    require(np.array_equal(native[..., 3] == 1, covered) and np.isfinite(native).all(), "Invalid native position")
    diag = values["GI.Reference.Diagnostics"]
    require(np.isfinite(diag).all() and np.all(diag >= 0) and np.all(diag.sum(-1) <= 1.00001) and
            np.all(diag[..., 3] == 0), "Invalid secondary query cannot disappear outside an ROI")
    check_path_diagnostics(values["GI.Reference.PathDiagnostics"], covered, depth)
    diffuse = receivers["GI.Receiver.DiffuseWeight"]
    require(np.isfinite(diffuse).all() and np.all((diffuse >= 0) & (diffuse <= 1)), "Invalid diffuse receiver weight")
    return covered


def coastal_masks(receivers, observation):
    covered = receivers["GI.Receiver.FaceNormal"][..., 3] == 1
    diffuse = np.max(receivers["GI.Receiver.DiffuseWeight"][..., :3], axis=-1) > 0
    yy, xx = np.indices(covered.shape)
    result, reports = {}, {}
    for selector in observation["masks"]:
        x0, y0, x1, y1 = selector["rectPixels"]
        rectangle = (xx >= x0) & (xx < x1) & (yy >= y0) & (yy < y1)
        mask = rectangle & covered
        require(mask.sum() >= 32, "Empty declared opaque ROI: " + selector["id"])
        result[selector["id"]] = mask
        reports[selector["id"]] = dict(rectPixels=selector["rectPixels"], pixels=int(mask.sum()),
            backgroundPixels=int((rectangle & ~covered).sum()),
            nondiffusePixels=int((rectangle & covered & ~diffuse).sum()),
            diffusePixels=int((mask & diffuse).sum()),
            hasDiffuseReceivers=bool((mask & diffuse).any()),
            diffuseResponseQualification="NotEstablished" if (mask & diffuse).any() else "UnavailableNoDiffuseReceivers",
            maskSha256=hashlib.sha256(mask.tobytes()).hexdigest())
    return result, reports


def normalized_scene(scene):
    scene = copy.deepcopy(scene)
    scene["environment"].pop("activeIBLGeneration", None)
    return scene


def read_case(root, case):
    directory = root / case["directory"]
    values, frame = load_export(directory)
    receivers, receiver_frame = load_export(directory / "receivers")
    parameter_inputs = lambda f: {k: v for k, v in f["parameters"].items() if k != "frameSerial"}
    require(frame["camera"] == receiver_frame["camera"] and frame["sceneInputs"] == receiver_frame["sceneInputs"] and
            parameter_inputs(frame) == parameter_inputs(receiver_frame), "Receiver/reference input drift")
    require(frame["content"]["labId"] == "gglab.lab.gi_reference" and
            frame["camera"]["referenceView"] == case["view"], "Wrong reference content/camera")
    temporal = frame["temporal"]
    require(temporal["status"] == "disabled" and temporal["jitterPixels"] == [0, 0] and
            temporal["renderExtent"] == temporal["displayExtent"] and not temporal["gtao"]["enabled"],
            "Reference must be Native, unjittered, TAA/GTAO off")
    require(not frame["sceneInputs"]["postProcess"]["bloomEnabled"], "Reference bloom is enabled")
    gate = [g for g in frame["readiness"]["gates"] if g["name"] == "gi-reference"]
    require(frame["readiness"]["ready"] and len(gate) == 1 and gate[0]["state"] == "ready" and
            f"D={case['depth']}," in gate[0]["detail"] and
            f"{case['samples']}/{case['samples']} completed samples" in gate[0]["detail"],
            "Incomplete/wrong-depth/sample observation")
    actual = dict(DEFAULTS, tile_pixels=0)
    actual.update({p["id"].split(".", 1)[1]: p["value"] for p in frame["parameters"]["values"]
                   if p["id"].startswith("gi_reference.")})
    require(all(actual[k] == case[k] for k in ("fixture", "depth", "samples", "batch_samples", "tile_pixels", "seed",
                                              "albedo", "secondary_albedo", "environment")), "Reference parameter drift")
    covered = validate_arrays(values, receivers, case["depth"])
    if case["fixture"] == 12:
        lock = input_lock()
        require(temporal["renderExtent"] == lock["coastal"]["comparisonExtent"], "Frozen Coastal extent differs")
        observation = next(o for o in lock["coastal"]["observations"] if o["referenceView"] == case["view"])
        require(frame["camera"] == observation["camera"], "Frozen Coastal camera differs")
        bundle = frame["content"]["assetBundle"]
        require(all(bundle[k] == lock["assetBundle"][k] for k in ("id", "revision")), "Coastal asset identity differs")
        require(normalized_scene(frame["sceneInputs"]) == normalized_scene(lock["coastal"]["sceneInputs"]),
                "Frozen Coastal lighting/exposure/shadow inputs differ")
        masks, reports = coastal_masks(receivers, observation)
    else:
        require(case["fixture"] == 13 and case["view"] in CONTRACT_VIEWS and
                frame["sceneInputs"]["sun"] is None and frame["sceneInputs"]["environment"]["intensity"] == 0,
                "Wrong small-receiver contract")
        face = receivers["GI.Receiver.FaceNormal"][covered, :3]
        vertex = receivers["GI.Receiver.VertexNormal"][covered, :3]
        expected = np.array([.3, .4, -1]) / np.sqrt(1.25)
        require(np.max(np.abs(face - [0, 0, -1])) < 1e-4 and np.max(np.abs(vertex - expected)) < 1e-5,
                "Small receiver lost the geometric/smooth-normal distinction")
        p = values["GI.Reference.Position"]
        require(np.max(np.abs(p[..., 2][covered])) < 1e-6, "Receiver matched the wrong plane")
        mask = covered & (np.abs(p[..., 0]) <= .005) & (np.abs(p[..., 1]) <= .005)
        require(mask.sum() >= 32, "Empty small-receiver mask")
        # Analytic cosine-hemisphere overlap, independent of shader RNG/triangle lookup.
        expected_e = np.pi * (1 + 1 / np.sqrt(1.25)) / 2
        require(np.max(np.abs(values["GI.Reference.Irradiance"][mask, :3].mean(0) / expected_e - 1)) < .01,
                "Small smooth receiver's below-face/furnace integral differs")
        masks, reports = {"plane": mask}, {"plane": dict(pixels=int(mask.sum()))}
    graph = json.loads(next(directory.glob("*.graph.json")).read_text(encoding="utf-8-sig"))
    passes = {p["name"]: p for p in graph["compile"]["passes"] if not p["culled"]}
    require(all(passes[p]["executionOrder"] < passes["GI.Reference.PathTrace"]["executionOrder"]
                for p in ("GI.Receiver", "GI.Receiver.Position")), "Unpaired Runtime receiver composition")
    ids = values["GI.Reference.Identity"][..., 3]
    summary = dict(coveredPixels=int(covered.sum()),
                   matchKinds={str(i): int(((ids == i) & covered).sum()) for i in (1, 2, 3)}, masks=reports,
                   qualification="calculated; sampling/depth certification remains external")
    (directory / "receiver-report.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    for name, mask in masks.items():
        np.save(directory / (name + ".mask.npy"), mask, allow_pickle=False)
        overlay(directory / (name + "-mask.png"), receivers["GI.Receiver.ShadingNormal"][..., :3], mask)
    return values, frame, masks


def self_test():
    shape = (8, 8, 4)
    receivers = {"GI.Receiver." + k: np.zeros(shape) for k in
                 ("FaceNormal", "VertexNormal", "ShadingNormal", "DiffuseWeight", "Position")}
    for k in ("FaceNormal", "VertexNormal", "ShadingNormal"):
        receivers["GI.Receiver." + k][..., 2] = -1
    receivers["GI.Receiver.FaceNormal"][..., 3] = 1
    receivers["GI.Receiver.Position"][..., 3] = 1
    receivers["GI.Receiver.DiffuseWeight"][..., :3] = .8
    receivers["DisplayView.DepthBuffer"] = np.ones((8, 8, 1))
    values = {"GI.Reference." + k: np.zeros(shape) for k in
              (*COMPONENTS, "Position", "Identity", "Diagnostics", "PathDiagnostics")}
    values["GI.Reference.Irradiance"][..., 3] = 1
    values["GI.Reference.Position"][..., 3] = 1
    values["GI.Reference.Identity"][..., 3] = 3
    values["GI.Reference.PathDiagnostics"][..., 2:] = [16, 1]
    validate_arrays(values, receivers, 16)
    observation = dict(masks=[dict(id="fixed", rectPixels=[0, 4, 8, 8])])
    a, _ = coastal_masks(receivers, observation)
    require(a["fixed"].sum() == 32, "Frozen rectangle membership")
    receivers["GI.Receiver.DiffuseWeight"][4:, :, :3] = 0
    b, reports = coastal_masks(receivers, observation)
    require(np.array_equal(a["fixed"], b["fixed"]) and reports["fixed"]["nondiffusePixels"] == 32 and
            not reports["fixed"]["hasDiffuseReceivers"], "Metallic pixels must remain in E masks and be reported separately")
    for name, channel, bad in (("Irradiance", 3, -2), ("EnvironmentIrradiance", 0, float("nan")),
                               ("Diagnostics", 3, .01), ("PathDiagnostics", 0, .01), ("Identity", 3, 0)):
        changed = copy.deepcopy(values)
        changed["GI.Reference." + name][0, 0, channel] = bad
        try:
            validate_arrays(changed, receivers, 16)
        except ValueError:
            continue
        raise AssertionError("Invalid data outside the ROI was silently dropped: " + name)
    print("Coastal reference contracts passed (coverage, off-ROI invalidity, components, depth, identities, fixed masks)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path)
    parser.add_argument("--case", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    elif args.directory is not None and args.case is not None:
        read_case(args.directory, json.loads(args.case.read_text(encoding="utf-8-sig")))
        print(json.dumps(dict(status="ReceiverInputsValidated", numericalConvergence="not-established-by-this-check")))
    else:
        parser.error("Use --self-test or DIRECTORY --case CASE.json")
