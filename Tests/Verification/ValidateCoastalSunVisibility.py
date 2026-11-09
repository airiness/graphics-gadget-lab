"""Validate paired sun-visibility T2 exports and the G1 cross-RHI agreement gate."""
import argparse
import json
from pathlib import Path

import numpy as np

VIEWS = ("Retreat_SeaPassage", "Retreat_Courtyard", "GI_InteriorExterior", "Retreat_Lounge")
MIN_AGREEMENT = 0.995


def check(condition, message):
    if not condition:
        raise ValueError(message)


def measure(tap, depth, reversed_depth=True):
    check(tap.dtype == np.dtype("<f4") and tap.ndim == 4 and tap.shape[0] == 1 and tap.shape[-1] == 4,
          "Sun visibility must be a single-slice float32 RGBA texture")
    check(depth.shape == tap.shape[:-1] + (1,), "Opaque depth and tap extents differ")
    check(np.isfinite(tap).all() and np.isfinite(depth).all(), "Non-finite tap or depth")
    values, raw_depth = tap[0], depth[0, ..., 0]
    rt, csm, state, distance = np.moveaxis(values, -1, 0)
    covered = raw_depth > 0 if reversed_depth else raw_depth < 1
    matched = state == 1
    check(np.isin(state, [-1, 0, 1]).all(), "Unexpected receiver state or invalid geometry identity")
    check(np.array_equal(state != 0, covered), "Receiver states do not describe the paired opaque depth")
    check(covered.any(), "No opaque receivers")
    match_rate = np.count_nonzero(matched) / np.count_nonzero(covered)
    check(match_rate >= MIN_AGREEMENT, f"Only {match_rate:.6%} of opaque depth matched a triangle")
    check(np.isin(rt[matched], [0, 1]).all(), "RT visibility is not binary")
    check(((csm[matched] >= 0) & (csm[matched] <= 1)).all(), "CSM visibility is outside [0,1]")
    lit, blocked = matched & (rt == 1), matched & (rt == 0)
    check(lit.any() and blocked.any(), "RT evidence needs both lit and shadowed receivers")
    check((csm[matched] < 0.05).any() and (csm[matched] > 0.95).any(), "CSM comparison is constant or degenerate")
    check((distance[lit] == -1).all(), "A ray miss must use distance -1")
    check(((distance[blocked] >= 0) & (distance[blocked] <= 1000)).all(), "Blocker distance is outside the declared ray interval")
    difference = np.abs(rt[matched] - csm[matched])
    return dict(opaquePixels=int(covered.sum()), matchedPixels=int(matched.sum()),
                unmatchedPixels=int((state == -1).sum()), receiverMatch=float(match_rate),
                rtLitPixels=int(lit.sum()), rtShadowedPixels=int(blocked.sum()),
                csmMeanAbsoluteDifference=float(difference.mean()),
                csmHardClassificationAgreement=float(np.mean(rt[matched] == (csm[matched] >= 0.5))),
                csmFilteredPixels=int(((csm[matched] > 0) & (csm[matched] < 1)).sum()))


def agreement(left, right):
    check(left.shape == right.shape, "Cross-RHI tap extents differ")
    left_state, right_state = left[..., 2], right[..., 2]
    union = (left_state != 0) | (right_state != 0)
    check(union.any(), "No cross-RHI receiver pixels")
    # Count unmatched receivers as disagreement, even if both backends miss them.
    same = (left_state == 1) & (right_state == 1) & (left[..., 0] == right[..., 0])
    rate = np.count_nonzero(same & union) / np.count_nonzero(union)
    check(rate >= MIN_AGREEMENT, f"RT agreement {rate:.6%} is below 99.5%")
    return dict(receiverUnionPixels=int(union.sum()), agreement=float(rate),
                differingPixels=int((union & ~same).sum()))


def load_view(root, view):
    paths = list((root / view).glob("*.resource-export.json"))
    check(len(paths) == 1, f"Expected one paired resource export for {root / view}")
    path = paths[0]
    record = json.loads(path.read_text(encoding="utf-8-sig"))
    targets = {item["address"]["resource"]: item for item in record["targets"]}
    target = targets["RayScene.SunVisibility"]
    check(target["writerPass"] == "RayScene.SunVisibility", "Unexpected sun tap writer")
    check(target["semantics"]["role"] == "ray-scene.sun-visibility", "Missing Runtime tap semantics")
    tap = np.load(path.parent / target["array"], allow_pickle=False)
    depth_target = targets["DisplayView.DepthBuffer"]
    depth = np.load(path.parent / depth_target["array"], allow_pickle=False)
    frame = record["frame"]
    check(frame["camera"]["referenceView"] == view, "Capture used the wrong reference camera")
    check(frame["temporal"]["jitterPixels"] == [0, 0], "Cross-RHI capture requires unjittered samples")
    metrics = measure(tap, depth, depth_target["semantics"]["reversedDepth"])
    return tap, frame, metrics


def inspect_root(root):
    return {view: load_view(root, view)[2] for view in VIEWS}


def compare_roots(left, right):
    results = {}
    for view in VIEWS:
        a, frame_a, _ = load_view(left, view)
        b, frame_b, _ = load_view(right, view)
        for field in ("camera", "sceneInputs", "content"):
            check(frame_a[field] == frame_b[field], f"Cross-RHI {view} inputs differ: {field}")
        for field in ("renderExtent", "displayExtent", "renderScale", "jitterPixels"):
            check(frame_a["temporal"][field] == frame_b["temporal"][field], f"Cross-RHI domain differs: {field}")
        check(frame_a["backend"] == "dx12" and frame_b["backend"] == "vulkan", "Expected a DX12/Vulkan pair")
        results[view] = agreement(a, b)
    return results


def self_test():
    tap = np.zeros((1, 8, 8, 4), dtype="<f4")
    tap[..., 2] = 1
    tap[:, :4, :, 0:2] = 1
    tap[:, :4, :, 3] = -1
    tap[:, 4:, :, 3] = 2
    depth = np.full((1, 8, 8, 1), 0.5, dtype="<f4")
    measure(tap, depth)
    agreement(tap, tap)

    def reject(action):
        try:
            action()
        except ValueError:
            return
        raise AssertionError("An adversarial visibility export was accepted")

    bad = tap.copy()
    bad[0, 0, 0, 0] = 0
    reject(lambda: agreement(tap, bad))
    bad = tap.copy()
    bad[0, 0, 0, 2] = -1
    reject(lambda: agreement(tap, bad))
    reject(lambda: agreement(bad, bad))
    reject(lambda: measure(bad, depth))
    bad = tap.copy()
    bad[..., 0] = 1
    reject(lambda: measure(bad, depth))
    bad = tap.copy()
    bad[0, 0, 0, 1] = np.nan
    reject(lambda: measure(bad, depth))
    reject(lambda: measure(tap.astype("<f2"), depth))
    return dict(checks=9)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path)
    parser.add_argument("--left", type=Path)
    parser.add_argument("--right", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    check(args.self_test or args.root or (args.left and args.right), "Choose self-test, one root or a comparison pair")
    result = {"ok": True}
    try:
        if args.self_test:
            result["selfTest"] = self_test()
        if args.root:
            result["views"] = inspect_root(args.root)
        if args.left and args.right:
            result["crossRhi"] = compare_roots(args.left, args.right)
    except Exception as error:
        result = {"ok": False, "error": f"{type(error).__name__}: {error}"}
    encoded = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(encoded + "\n", encoding="utf-8")
    print(encoded)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
