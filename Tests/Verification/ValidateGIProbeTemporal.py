"""Temporal probe raster, exact depth-source mapping and logical-view identity oracle."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

from ValidateGIProbeVisualization import load, oracle, require
from ValidateGIProbeIrradiance import raster, display_response
from ValidateGIFieldSampling import bilinear
import ValidateCaptureBundles as bundles


def validate_frame(directory, resource_path, image_metadata, arm, fixture, scene_metadata=None):
    resource = load(resource_path)
    summary = resource["frame"]["readiness"]["giField"]["summary"]
    display, view = summary["probeVisualization"], summary["probeVisualization"]["view"]
    require(display["schemaVersion"] in (4, 5) and display["available"] and display["viewSupported"] and
            not resource["frame"]["developmentTools"], "Temporal display unavailable or requires DevTools")
    require(image_metadata["readiness"]["giField"]["summary"]["probeVisualization"]["view"] == view,
            "Image and resource execution views are not frozen together")
    require(view["temporalAA"] == arm["taa"] and view["resolutionPreset"] == ("quality" if arm["quality"] else "native"),
            "Effective temporal settings differ from the requested arm")
    if arm["taa"]:
        require(not summary["receiverInspectionAvailable"] and not summary["receiverView"]["supported"],
                "Temporal visualization accidentally widened the receiver-inspection contract")
    source = "DisplayView.DisplayDepth" if arm["quality"] else "DisplayView.DepthBuffer"
    alignment = "upscaled-nearest-sample-approximation" if arm["quality"] else (
        "native-exact" if not arm["taa"] else "native-jittered-aligned" if arm["projection"] == 0 else
        "native-jittered-depth-approximation")
    jittered = arm["taa"] and not arm["quality"] and arm["projection"] == 0
    require(display["sceneDepthSource"] == source and display["sceneDepthAdditionalLogicalBytes"] == 0 and
            view["depthAlignment"] == alignment and view["postTemporalProjection"] == ("jittered" if jittered else "unjittered"),
            "Projection/depth source/approximation is misreported")
    render_width, render_height = view["renderExtent"]
    width, height = view["displayExtent"]
    projection = np.asarray(view["unjitteredProjection"]).reshape(4, 4)
    expected_projection = projection
    if jittered:
        clip_jitter = np.eye(4)
        clip_jitter[3, :2] = np.asarray(view["jitterPixels"]) * (2 / render_width, -2 / render_height)
        expected_projection = projection @ clip_jitter
    require(np.allclose(view["projection"], expected_projection.reshape(-1), rtol=0, atol=2e-6),
            "Effective post-temporal projection does not follow the declared policy")
    targets = {t["address"]["resource"]: t for t in resource["targets"]}
    arrays = {name: np.load(directory / t["array"], allow_pickle=False) for name, t in targets.items()}
    selected, counts = oracle(summary, arrays["GI.Field.Classification"])
    require(np.array_equal(arrays["GI.Probes.Counts"].reshape(-1)[2:9], counts) and
            np.array_equal(arrays["GI.Probes.RenderedIDs"].reshape(2, -1).T[:len(selected)], selected),
            "Temporal execution changed unjittered deterministic selection")
    stats = arrays["GI.Probes.Counts"].reshape(-1)
    stamp = display["observation"]
    require(int(stats[12]) + (int(stats[13]) << 32) == stamp["producingFrame"] == resource["frameSerial"] and
            int(stats[14]) + (int(stats[15]) << 32) == stamp["fieldUpdate"], "Temporal counts carry the wrong observation")
    completed = display["completed"]
    if completed:
        old_view = completed["view"]
        require(completed["observation"]["producingFrame"] < stamp["producingFrame"] and
                old_view["jitterPhase"] == old_view["temporalFrameIndex"] % old_view["jitterSequenceLength"],
                "Async counters lost their own completed execution view")
    require(not summary["receiverView"]["receiversAvailable"] and
            summary["receiverView"]["receiverAttachmentLogicalBytes"] == 0,
            "The ordinary field route silently allocated receiver diagnostic attachments")
    scene_depth = arrays[source].reshape(height, width)
    if arm["quality"]:
        raw = arrays["DisplayView.DepthBuffer"].reshape(render_height, render_width)
        positions = [(np.arange(n, dtype=np.float32) + np.float32(.5)) * np.float32(r / n)
                     for n, r in ((width, render_width), (height, render_height))]
        coordinates = [np.clip(np.maximum(p + np.float32(j), 0).astype(int), 0, r - 1)
                       for p, j, r in zip(positions, view["jitterPixels"], (render_width, render_height))]
        expected_depth = raw[coordinates[1][:, None], coordinates[0][None, :]]
        require(np.array_equal(scene_depth.view(np.uint32), expected_depth.view(np.uint32)),
                "Display depth does not match the frozen jitter/render-to-display nearest-sample mapping")
    graph = load(directory / resource["graph"])
    draw = next(p for p in graph["compile"]["passes"] if p["name"] == "GI.Probes.Draw")
    reads = [a for a in draw["accesses"] if a["resource"] == source]
    require(len(reads) == 1 and reads[0]["dependencyAccess"] == "Read" and reads[0]["stages"] == "PixelShader",
            "Temporal scene depth is not an explicit read-only pixel dependency")
    passes = {p["name"]: p["executionOrder"] for p in graph["compile"]["passes"] if not p["culled"]}
    if arm["quality"]:
        require(passes["PostProcess.TemporalAA.DisplayDepth"] < passes["GI.Probes.Draw"], "Probe overlay precedes display-depth publication")
    expected_resources = {"GI.Probes.Counts", "GI.Probes.RenderedIDs", "GI.Probes.PrivateDepth", "GI.Probes.Counts.Readback"}
    require({r["name"] for r in graph["compile"]["resources"] if r["name"].startswith("GI.Probes.")} == expected_resources,
            "Temporal probe display adds an undeclared attachment")
    mask, winners, normals, depth = raster(summary, selected, full=True, edge_guard=1/16)
    difference = depth - scene_depth
    margin = np.maximum(np.abs(scene_depth) * 2e-6, 2e-7)
    clear = scene_depth == (0 if view["reversedDepth"] else 1)
    front = difference > margin if view["reversedDepth"] else difference < -margin
    back = difference < -margin if view["reversedDepth"] else difference > margin
    visible, occluded = mask & (front | clear), mask & back & ~clear
    require(visible.sum() > 20 and occluded.sum() > 20, "Temporal fixture does not exercise visible and rejected probes")
    image = np.asarray(Image.open(directory / image_metadata["image"]["file"]))[..., :3]
    if scene_metadata is not None:
        scene = np.asarray(Image.open(directory / scene_metadata["image"]["file"]))[..., :3]
        require(np.array_equal(image[occluded], scene[occluded]), "Scene-rejected probes overwrite the frozen scene")
    else:
        # Sequence image/resource products share the submitted frame. Qualify rejection
        # through the existing private D32 target without importing a swapchain export
        # contract that its graph descriptor does not provide.
        private = arrays["GI.Probes.PrivateDepth"].reshape(height, width)
        require(np.all(private[occluded] == (0 if view["reversedDepth"] else 1)),
                "Rejected temporal fragments wrote private depth")
        require(np.allclose(private[visible], depth[visible], rtol=2e-5, atol=2e-7),
                "Visible temporal fragment depth differs from independent rasterization")
    if arm["mode"]:
        nx, _, nz = summary["counts"]
        ids = winners[visible]
        xyz = np.column_stack((ids % nx, ids // (nx * nz), (ids // nx) % nz))
        response = bilinear(arrays["GI.Field.Irradiance"], xyz, normals[visible], summary["irradianceInteriorWidth"])[:, :3]
        expected = display_response(response, display["settings"]["previewEV"])
    else:
        light = .45 + .55 * np.clip(normals[visible] @ (np.asarray((-1, 2, -3)) / np.sqrt(14)), 0, 1)
        expected = light[:, None] * np.asarray((.08, .8, .5)) * 255
    error = np.max(np.abs(image[visible].astype(float) - expected), axis=-1)
    require(np.quantile(error, .99) <= 3 and np.mean(error <= 2) >= .98,
            f"Temporal raster/normal/atlas mapping differs: p99={np.quantile(error,.99):.3f}")
    border = visible[0].sum() + visible[-1].sum() + visible[:, 0].sum() + visible[:, -1].sum()
    record = dict(rasterEdgeGuardPixels=1/16, frame=resource["frameSerial"], phase=view["jitterPhase"], revision=view["revision"],
                  visiblePixels=int(visible.sum()), occludedPixels=int(occluded.sum()),
                  viewportEdgePixels=int(border), p99ByteError=float(np.quantile(error, .99)))
    return record, selected


def validate(root):
    records = []
    for backend in load(root / "backends.json"):
        for fixture in ("f4", "coastal"):
            for arm in load(root / "arms.json"):
                directory = root / backend / fixture / arm["id"]
                for manifest in directory.glob("*.capture-bundle.json"):
                    bundles.validate(manifest)
                images = {}
                for path in directory.glob("*.json"):
                    document = load(path)
                    if document.get("image"):
                        images[(document["frame"]["serial"], document["source"])] = document
                results, previous_ids = [], None
                for path in sorted(directory.glob("*.resource-export.json")):
                    resource = load(path)
                    require((resource["frameSerial"], "composited") in images, "Sequence image/resource submitted frames differ")
                    record, selected = validate_frame(directory, path, images[(resource["frameSerial"], "composited")], arm, fixture,
                                                      images.get((resource["frameSerial"], "scene")))
                    if previous_ids is not None:
                        require(np.array_equal(previous_ids, selected), "A static logical view's selection changes with jitter phase")
                    previous_ids = selected
                    results.append(record)
                require(results, f"No temporal captures: {directory}")
                if fixture == "coastal" and arm["mode"] == 0 and arm["phases"] > 1:
                    require({r["phase"] for r in results} == set(range(arm["phases"])) and
                            len({r["revision"] for r in results}) == 1, "Phase coverage is incomplete or jitter advances logical revision")
                    require(sum(r["viewportEdgePixels"] for r in results) > 0, "Viewport edges are unqualified")
                records.append(dict(backend=backend, fixture=fixture, arm=arm["id"], frames=results))
    return dict(ok=True, cases=records)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    result = validate(args.root)
    (args.root / "validation.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(dict(ok=True, cases=len(result["cases"]))))
