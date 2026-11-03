"""Per-edge FP64 volume-bounds oracle across the near/eye planes and camera turns."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

from ValidateGIProbeVisualization import load, require


def segments(summary):
    """Clip center lines to all six homogeneous planes before projecting them."""
    view = summary["probeVisualization"]["view"]
    matrix = np.asarray(view["viewProjection"], dtype=np.float64).reshape(4, 4)
    extent = np.asarray(view["displayExtent"])
    minimum = np.asarray(summary["minimum"])
    size = (np.asarray(summary["counts"]) - 1) * summary["spacing"]
    planes = np.asarray(((1, 0, 0, 1), (-1, 0, 0, 1), (0, 1, 0, 1),
                         (0, -1, 0, 1), (0, 0, 1, 0), (0, 0, -1, 1)))
    result = []
    for edge in range(12):
        axis, side = divmod(edge, 4)
        a = np.zeros(3)
        a[(axis + 1) % 3], a[(axis + 2) % 3] = side & 1, side >> 1
        b = a.copy()
        b[axis] = 1
        clip = np.column_stack((minimum + np.asarray((a, b)) * size, np.ones(2))) @ matrix
        low, high = 0.0, 1.0
        for d0, d1 in (clip @ planes.T).T:
            if d0 < 0 and d1 < 0:
                high = -1.0
                break
            if (d0 < 0) != (d1 < 0):
                crossing = -d0 / (d1 - d0)
                if d0 < 0:
                    low = max(low, crossing)
                else:
                    high = min(high, crossing)
        if low >= high:
            continue
        clipped = clip[0] + np.asarray((low, high))[:, None] * (clip[1] - clip[0])
        require(np.all(clipped[:, 3] > 0), "Visible bounds oracle reached a nonpositive perspective divisor")
        ndc = clipped[:, :3] / clipped[:, 3, None]
        screen = (ndc[:, :2] * (1, -1) + 1) * extent / 2
        result.append(dict(edge=edge, screen=screen, depth=ndc[:, 2],
                           eyeCrossing=bool((clip[0, 3] <= 0) != (clip[1, 3] <= 0)),
                           eyeEndpoint=bool(np.any(np.abs(clip[:, 3]) < 1e-5)),
                           behindFirst=bool(clip[0, 3] <= 0)))
    return result


def validate_frame(directory, resource, image, mode, depth_mode):
    summary = resource["frame"]["readiness"]["giField"]["summary"]
    display = summary["probeVisualization"]
    require(display["settings"]["bounds"] and display["settings"]["mode"] == ("irradiance" if mode else "classification") and
            display["settings"]["depthMode"] == ("scene-depth" if depth_mode else "xray"), "Bounds replay settings differ from the requested arm")
    arrays = {t["address"]["resource"]: np.load(directory / t["array"], allow_pickle=False)
              for t in resource["targets"]}
    require(arrays["GI.Probes.Counts"].reshape(-1)[7] == 0, "Bounds test contains ordinary spheres")
    private = arrays["GI.Probes.PrivateDepth"].squeeze()
    scene = arrays["DisplayView.DepthBuffer"].squeeze()
    require(np.all(np.isfinite(private)) and np.all((private >= 0) & (private <= 1)), "Invalid bounds depth")
    view = display["view"]
    reversed_z = view["reversedDepth"]
    covered = private != (0 if reversed_z else 1)
    pixels = np.asarray(Image.open(directory / image["image"]["file"]))[..., :3].astype(float)
    height, width = private.shape
    yy, xx = np.mgrid[:height, :width]
    points = np.stack((xx + .5, yy + .5), axis=-1)
    edges = segments(summary)
    strips = []
    for edge in edges:
        a, b = edge["screen"]
        delta = b - a
        length = np.linalg.norm(delta)
        if length < 8:
            continue
        t = np.sum((points - a) * delta, axis=-1) / (length * length)
        distance = np.abs((points[..., 0] - a[0]) * delta[1] -
                          (points[..., 1] - a[1]) * delta[0]) / length
        # The line is one pixel wide. Keep a 0.2-pixel margin from its sides
        # and omit cap/viewport intersections affected by fixed-point coverage.
        interior = (distance < .3) & (t * length > 3) & ((1 - t) * length > 3)
        z = edge["depth"][0] + t * (edge["depth"][1] - edge["depth"][0])
        if depth_mode:
            interior &= (z > scene + 1e-5) if reversed_z else (z < scene - 1e-5)
        strips.append((edge, interior, distance, t))
    records = []
    for edge, interior, _, _ in strips:
        # A surviving parallel/intersecting edge cannot hide the loss of this edge.
        for other, _, distance, t in strips:
            if other["edge"] != edge["edge"]:
                interior &= ~((distance < 1) & (t >= 0) & (t <= 1))
        count = int(interior.sum())
        if count < 5:
            continue
        missing = int(np.sum(interior & ~covered))
        require(missing == 0, f"Visible bounds edge disappeared: {directory.name}, frame={resource['frameSerial']}, "
                f"edge={edge['edge']}, eyeCrossing={edge['eyeCrossing']}, missing={missing}/{count}")
        error = np.max(np.abs(pixels[interior] - np.asarray((.55, .8, 1)) * 255))
        require(error <= 1, "Visible bounds pixels have the wrong color")
        records.append(dict(edge=edge["edge"], pixels=count, eyeCrossing=edge["eyeCrossing"],
                            eyeEndpoint=edge["eyeEndpoint"], behindFirst=edge["behindFirst"]))
    require(records, "Bounds replay has no independently visible edge interiors")
    return dict(frame=resource["frameSerial"], camera=view["cameraPosition"], edges=records)


def validate(root):
    records = []
    for backend in load(root / "backends.json"):
        for mode in (0, 1):
            for depth in (0, 1):
                directory = root / backend / "bounds" / f"mode{mode}-depth{depth}"
                images = {}
                for path in directory.glob("*.json"):
                    document = load(path)
                    if document.get("image"):
                        images[document["frame"]["serial"]] = document
                frames = []
                for path in sorted(directory.glob("*.resource-export.json")):
                    resource = load(path)
                    require(resource["frameSerial"] in images, "Bounds image/resource submitted frames differ")
                    frames.append(validate_frame(directory, resource, images[resource["frameSerial"]], mode, depth))
                require(len(frames) == 13, "Bounds crossing replay did not capture every camera pose")
                tested = [edge for frame in frames for edge in frame["edges"]]
                require(any(edge["eyeCrossing"] and edge["behindFirst"] for edge in tested) and
                        any(edge["eyeCrossing"] and not edge["behindFirst"] for edge in tested),
                        "Bounds replay must retain crossing edges in both endpoint orders")
                require(any(edge["eyeEndpoint"] for edge in tested), "Bounds replay omitted the zero-w endpoint")
                records.append(dict(backend=backend, mode=mode, depth=depth, frames=frames))
    return records


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    result = dict(ok=True, cases=validate(args.root))
    (args.root / "bounds-validation.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(dict(ok=True, cases=len(result["cases"]))))
