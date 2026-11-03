"""FP64 tessellated-sphere point raster oracle for real 1x1 GPU picking."""
import argparse
import copy
import json
from pathlib import Path

import numpy as np

from ValidateGIProbeVisualization import load, oracle, require
from ValidateGIProbeDepth import admitted, comparable, conformance
import ValidateGIProbePicking as picking_seal

UV = np.asarray([(x / 16, y / 8) for y in range(9) for x in range(17)])
UNIT = np.column_stack((np.sin(UV[:, 1] * np.pi) * np.cos(UV[:, 0] * 2 * np.pi),
                        np.cos(UV[:, 1] * np.pi),
                        np.sin(UV[:, 1] * np.pi) * np.sin(UV[:, 0] * 2 * np.pi)))
TRIANGLES = np.asarray([t for y in range(8) for x in range(16)
                       for a in [y * 17 + x] for t in ((a, a + 17, a + 18), (a, a + 18, a + 1))])


def read(directory):
    resource = load(next(directory.glob('*.resource-export.json')))
    arrays = {t['address']['resource']: np.load(directory / t['array'], allow_pickle=False)
              for t in resource['targets']}
    return resource, resource['frame']['readiness']['giField']['summary'], arrays


def clipped(triangle):
    polygon = list(triangle)
    for plane in (lambda v: v[2], lambda v: v[3] - v[2], lambda v: v[3] - 1e-10):
        output = []
        for a, b in zip(polygon, polygon[1:] + polygon[:1]):
            da, db = plane(a), plane(b)
            if da >= 0:
                output.append(a)
            if (da < 0) != (db < 0):
                output.append(a + (b - a) * da / (da - db))
        polygon = output
        if len(polygon) < 3:
            return []
    return [np.asarray((polygon[0], polygon[i], polygon[i + 1])) for i in range(1, len(polygon) - 1)]


def point(summary, selected, pixel, scene=None):
    view = summary['probeVisualization']['view']
    width, height = view['displayExtent']
    matrix = np.asarray(view['viewProjection']).reshape(4, 4)
    nx, _, nz = summary['counts']
    radius = summary['spacing'] * summary['probeVisualization']['settings']['radiusInSpacings']
    best, winner, candidates, margin = (0.0 if view['reversedDepth'] else 1.0), None, [], 0.0
    for probe, category in selected:
        center = np.asarray(summary['minimum']) + np.asarray((probe % nx, probe // (nx * nz), (probe // nx) % nz)) * summary['spacing']
        world = center + UNIT * radius
        clip = np.column_stack((world, np.ones(len(world)))) @ matrix
        triangles = clip[TRIANGLES]
        inside = np.all((triangles[..., 2] >= 0) & (triangles[..., 2] <= triangles[..., 3]) & (triangles[..., 3] > 0), axis=1)
        partial = triangles[~inside]
        pieces = [p for t in partial for p in clipped(t)]
        triangles = np.concatenate((triangles[inside], np.asarray(pieces).reshape(-1, 3, 4)))
        if not len(triangles):
            continue
        ndc = triangles[..., :3] / triangles[..., 3, None]
        screen = (ndc[..., :2] * (1, -1) + 1) * (width / 2, height / 2)
        a, b, c = screen[:, 0], screen[:, 1], screen[:, 2]
        ab, ac, ap = b - a, c - a, np.asarray(pixel) + .5 - a
        det = ab[:, 0] * ac[:, 1] - ab[:, 1] * ac[:, 0]
        safe = np.where(abs(det) > 1e-12, det, 1)
        beta = (ap[:, 0] * ac[:, 1] - ap[:, 1] * ac[:, 0]) / safe
        gamma = (ab[:, 0] * ap[:, 1] - ab[:, 1] * ap[:, 0]) / safe
        weights = np.column_stack((1 - beta - gamma, beta, gamma))
        covered = np.all(weights >= -1e-10, axis=1) & (abs(det) > 1e-12)
        depths = np.sum(weights * ndc[..., 2], axis=1).astype(np.float32)
        covered &= np.isfinite(depths) & (depths >= 0) & (depths <= 1) & (depths != (0 if view['reversedDepth'] else 1))
        if scene is not None:
            d = float(scene[pixel[1], pixel[0]])
            covered &= np.isfinite(d) & (0 <= d <= 1) & ((depths >= d) if view['reversedDepth'] else (depths <= d))
        choices = np.flatnonzero(covered)
        if not len(choices):
            continue
        index = choices[np.argmax(depths[choices]) if view['reversedDepth'] else np.argmin(depths[choices])]
        depth = float(depths[index])
        candidates.append(int(probe))
        if (depth > best) if view['reversedDepth'] else (depth < best):
            best, winner = depth, int(probe)
            altitudes = abs(det[index]) / np.maximum(np.linalg.norm(
                np.asarray((b[index] - c[index], a[index] - c[index], a[index] - b[index])), axis=1), 1e-12)
            margin = float(np.min(weights[index] * altitudes))
    return dict(id=winner, depth=best, candidates=candidates, margin=margin)


def choose(directory):
    _, summary, arrays = read(directory)
    ids = arrays['GI.Probes.RenderedIDs'].reshape(2, -1).T
    ids = ids[ids[:, 0] != 0xffffffff]
    view = summary['probeVisualization']['view']
    width, height = view['displayExtent']
    nx, _, nz = summary['counts']
    xyz = np.column_stack((ids[:, 0] % nx, ids[:, 0] // (nx * nz), (ids[:, 0] // nx) % nz))
    world = np.asarray(summary['minimum']) + xyz * summary['spacing']
    clip = np.column_stack((world, np.ones(len(world)))) @ np.asarray(view['viewProjection']).reshape(4, 4)
    with np.errstate(divide='ignore', invalid='ignore'):
        screen = (clip[:, :2] / clip[:, 3, None] * (1, -1) + 1) * (width / 2, height / 2)
    result = []
    for p in screen[np.all(np.isfinite(screen), axis=1) & (clip[:, 3] > 0)]:
        pixel = np.floor(p).astype(int).tolist()
        if not all(0 <= p < n for p, n in zip(pixel, (width, height))):
            continue
        sample = point(summary, ids, pixel)
        if sample['id'] is not None and sample['margin'] > .15:
            result.append(dict(pixel=pixel, **sample))
            if len(result) == 3:
                break
    require(result, 'No unambiguous sphere interior for picking')
    result += [dict(pixel=p) for p in ((0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1))]
    return result


def audit_injections(frame):
    picking_seal.validate(frame)
    for change in range(13):
        bad = copy.deepcopy(frame)
        result = bad['giProbePicking']
        if change == 0:
            result['sourceField']['generation'] += 1
        elif change == 1:
            result['observation']['producingFrame'] = result['sourceFrame']
        elif change == 2:
            result['observation']['readinessUpdate'] += 1
        elif change == 3:
            result['executionView']['cameraPosition'][0] += 1
        elif change == 4:
            result['sourceView']['revision'] += 1
        elif change == 5:
            result['token'] = 0
        elif change == 6:
            result['validity'] = 'NoHit'
        elif change == 7:
            result['coordinate'][0] += 1
        elif change == 8:
            result['depth'] = 0 if result['executionView']['reversedDepth'] else 1
        elif change == 9:
            result['settings']['budget'] += 1
        elif change == 10:
            result['pixel'][0] = result['executionView']['displayExtent'][0]
        elif change == 11:
            result['attachmentExtent'] = [2, 1]
        else:
            result['schemaVersion'] = 2
        bad['giProbePickingObservation'] = picking_seal.observation(result)
        bad['giProbePickingObservation']['executionDpiScale'] = result['mapping']['dpiScale']
        try:
            picking_seal.validate(bad)
        except (ValueError, KeyError, TypeError):
            continue
        raise ValueError(f'Picking seal accepted injected contradiction {change}')
    graph = copy.deepcopy(frame)
    graph.pop('giProbePicking')
    picking_seal.validate_pairing([('graph', graph), ('resource-export', frame)])
    for change in range(3):
        bad = copy.deepcopy(graph)
        if change == 0:
            bad.pop('giProbePickingObservation')
        else:
            bad['giProbePickingObservation']['token'] += 1
        try:
            products = [('graph', bad)] if change == 2 else [('graph', bad), ('resource-export', frame)]
            picking_seal.validate_pairing(products)
        except (ValueError, KeyError, TypeError):
            continue
        raise ValueError('Picking seal accepted an orphan or contradictory product')
    return 16


def validate(root):
    results = []
    injections = 0
    for resource_path in sorted(root.rglob('*.resource-export.json')):
        directory = resource_path.parent
        resource, summary, arrays = read(directory)
        frame = resource['frame']
        if 'giProbePicking' not in frame:
            continue
        result = frame['giProbePicking']
        if not injections and result['validity'] == 'Hit':
            injections = audit_injections(frame)
        ids = arrays['GI.Probes.RenderedIDs'].reshape(2, -1).T
        ids = ids[ids[:, 0] != 0xffffffff]
        expected_ids, counts = oracle(summary, arrays['GI.Field.Classification'])
        require(np.array_equal(ids, expected_ids) and np.array_equal(arrays['GI.Probes.Counts'].reshape(-1)[2:9], counts),
                'Picking changed deterministic display filtering')
        display = summary['probeVisualization']
        depth_name = 'DisplayView.DepthBuffer' if display['view']['renderExtent'] == display['view']['displayExtent'] else 'DisplayView.DisplayDepth'
        scene = arrays[depth_name].squeeze() if display['depthMode'] == 'scene-depth' else None
        expected = point(summary, ids, result['pixel'], scene)
        encoded = int(arrays['GI.Probes.PickID'].item())
        depth = float(arrays['GI.Probes.PickDepth'].item())
        require(result['probeId'] == expected['id'] and encoded == (0 if expected['id'] is None else expected['id'] + 1),
                f"Picking disagrees with FP64 rasterization: {directory} {result['probeId']} != {expected}")
        require(abs(depth - expected['depth']) <= 2e-6, f'Picking Z/W differs: {directory}')
        graph = load(next(directory.glob('*.graph.json')))
        pick = [p for p in graph['compile']['passes'] if p['name'] == 'GI.Probes.Pick']
        require(len(pick) == 1, 'A pick has no unique on-demand pass')
        tiny = [r for r in graph['compile']['resources'] if r['name'] in ('GI.Probes.PickID', 'GI.Probes.PickDepth')]
        require(len(tiny) == 2 and all(r['logicalBytes'] == 4 and r['texture']['extent'] == [1, 1, 1] and
                r['texture']['samples'] == 1 for r in tiny), 'Picking allocated a full-view attachment')
        reads = {a['resource'] for a in pick[0]['accesses'] if a['dependencyAccess'] == 'Read'}
        require({'GI.Probes.Counts', 'GI.Probes.RenderedIDs'} <= reads and
                (depth_name in reads) == (scene is not None), 'Picking did not share display selection/depth dependencies')
        allocations = [dict(name=graph['compile']['resources'][a['resourceIndex']]['name'], **a)
                       for a in graph['execution']['allocations']
                       if graph['compile']['resources'][a['resourceIndex']]['name'].startswith(('GI.Probes.Pick', 'Observation.ResourceExport'))]
        require(all(any(a['resourceIndex'] == r['index'] and a['allocationFacts'] == 'known' and
                        a['allocatedBytes'] >= 4 for a in allocations) for r in tiny), 'Missing native tiny-attachment allocation facts')
        results.append(dict(directory=str(directory.relative_to(root)), result=result['validity'],
                            id=result['probeId'], pixel=result['pixel'], candidates=len(expected['candidates']),
                            sourceFrame=result['sourceFrame'], executionFrame=result['observation']['producingFrame'],
                            allocations=allocations))
    require(results and any(r['result'] == 'Hit' for r in results) and any(r['result'] == 'NoHit' for r in results),
            'Picking verification lacks positive and empty observations')
    (root / 'audit-injections.json').write_text(json.dumps(dict(rejected=injections)), encoding='utf-8')
    return results


def validate_depth(root, subpixel_bits):
    results = []
    for resource_path in sorted(root.rglob('*.resource-export.json')):
        directory = resource_path.parent
        if directory.name == 'controls':
            require(len(conformance(directory)) == 60, 'Full-view D32 control changed')
            continue
        resource = load(resource_path)
        arrays = {t['address']['resource']: np.load(directory / t['array'], allow_pickle=False)
                  for t in resource['targets']}
        convention = directory.parent.name
        inputs = arrays.get(f'GI.Depth.{convention}.Inputs')
        inputs = inputs.squeeze() if inputs is not None else None
        indices = sorted({int(name.split('.')[3]) for name in arrays
                          if name.startswith('GI.Pick.') and name.split('.')[3].isdigit()})
        for index in indices:
            prefix = f'GI.Pick.{convention}.{index}'
            winner, depth = arrays[prefix + '.ID'], arrays[prefix + '.D32']
            if index < 30:
                words = inputs[8, index * 16:(index + 1) * 16]
                expected_id, expected_bits = 0, (0 if words[1] else 0x3f800000)
                for probe, raw in enumerate(words[4:6], 1):
                    value, scene = comparable(int(raw)), comparable(int(words[6]))
                    if words[3] and not admitted(int(raw), bool(words[1])):
                        continue
                    if words[2] and (not admitted(int(raw), bool(words[1])) or scene > 0x3f800000 or
                                     not (value >= scene if words[1] else value <= scene)):
                        continue
                    if value > expected_bits if words[1] else value < expected_bits:
                        expected_id, expected_bits = probe, value
                actual_bits = int(depth.view(np.uint32).item())
                actual_bits = 0 if actual_bits == 0x80000000 else actual_bits
                require(int(winner.item()) == expected_id and actual_bits == expected_bits,
                        f'Tiny cropped D32/tie differs: {convention}/{index}')
            else:
                pixel = (0, 8, 15)[index - 30]
                expected = .5 + .7 * (2 * (pixel + .5) / 16 - 1)
                clip_admitted = 0 <= expected <= 1
                require(int(winner.item()) == int(clip_admitted), f'Crop pixel/clip admission differs: {convention}/{index}')
                reference_id = arrays[f'GI.Pick.{convention}.ClipReference.ID'].squeeze()[pixel, pixel]
                reference_depth = arrays[f'GI.Pick.{convention}.ClipReference.D32'].squeeze()[pixel, pixel]
                # Clip the analytic plane at z=0/1, then quantize its screen vertices
                # to the qualified raster subpixel grid before interpolating depth.
                scale = 2 ** subpixel_bits
                left = round((16 / 7) * scale) / scale
                right = round((16 - 16 / 7) * scale) / scale
                quantized = (pixel + .5 - left) / (right - left) if clip_admitted else (
                    0 if convention == 'Reversed' else 1)
                require(abs(float(depth.item()) - quantized) < 2e-7 and
                        abs(float(reference_depth) - quantized) < 2e-7,
                        f'Native depth differs from the independent quantized-plane oracle: {convention}/{index}')
                require(int(winner.item()) == int(reference_id) and
                        abs(float(depth.item()) - float(reference_depth)) < 2e-7,
                        f'Cropped interpolation differs from the native full-view plane: {convention}/{index}')
            graph = load(next(directory.glob('*.graph.json')))
            for suffix in ('.ID', '.D32'):
                target = next(r for r in graph['compile']['resources'] if r['name'] == prefix + suffix)
                require(target['texture']['extent'] == [1, 1, 1] and target['logicalBytes'] == 4,
                        'Conformance used a full-sized attachment')
            results.append(dict(rhi=resource['frame']['backend'], convention=convention, case=index,
                                id=int(winner.item()), depth=float(depth.item())))
    backends = load(root / 'backends.json')
    require(len(results) == 66 * len(backends) and {r['rhi'] for r in results} == set(backends),
            'Incomplete requested-RHI/depth-convention crop coverage')
    (root / 'validation.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--choose', type=Path)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--depth', type=Path)
    parser.add_argument('--subpixel-bits', type=int, default=8)
    args = parser.parse_args()
    if args.choose:
        print(json.dumps(choose(args.choose)))
    elif args.depth:
        print(f'Picking D32/crop passed: {len(validate_depth(args.depth, args.subpixel_bits))} native observations')
    else:
        result = validate(args.root)
        (args.root / 'validation.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(f'Picking passed: {len(result)} compact GPU observations')
