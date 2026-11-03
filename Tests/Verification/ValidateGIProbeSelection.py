"""Independent selected-probe coverage, budget isolation and identity checks."""
import argparse
import copy
import json
from pathlib import Path

import numpy as np
from PIL import Image

from ValidateGIProbeVisualization import load, oracle, require
from ValidateGIProbeIrradiance import raster


def read(directory):
    resource = load(next(directory.glob('*.resource-export.json')))
    summary = resource['frame']['readiness']['giField']['summary']
    arrays = {t['address']['resource']: np.load(directory / t['array'], allow_pickle=False)
              for t in resource['targets']}
    images = {}
    for path in directory.glob('*.json'):
        value = load(path)
        if value.get('image'):
            images[value['source']] = np.asarray(Image.open(directory / value['image']['file']))[..., :3]
    return resource, summary, arrays, images


def choose(directory, category):
    _, summary, arrays, _ = read(directory)
    all_probes = copy.deepcopy(summary)
    all_probes['probeVisualization']['settings']['budget'] = 16384
    selected, _ = oracle(all_probes, arrays['GI.Field.Classification'])
    selected = selected[selected[:, 1] == category]
    nx, _, nz = summary['counts']
    ids = selected[:, 0]
    xyz = np.column_stack((ids % nx, ids // (nx * nz), (ids // nx) % nz))
    world = np.asarray(summary['minimum']) + xyz * summary['spacing']
    view = summary['probeVisualization']['view']
    clip = np.column_stack((world, np.ones(len(world)))) @ np.asarray(view['viewProjection']).reshape(4, 4)
    ndc = clip[:, :3] / clip[:, 3, None]
    ordinary = arrays['GI.Probes.RenderedIDs'].reshape(2, -1)[0]
    admitted = (clip[:, 3] > 2 * summary['spacing']) & (np.max(np.abs(ndc[:, :2]), axis=1) < .7)
    admitted &= (ndc[:, 2] > 0) & (ndc[:, 2] < 1) & ~np.isin(ids, ordinary)
    # A center visible against native scene depth gives the depth-tested arms a
    # positive rendering obligation, not just a vacuous no-through-wall check.
    candidates = np.flatnonzero(admitted)
    scene = arrays['DisplayView.DepthBuffer'].squeeze()
    height, width = scene.shape
    screen = (ndc[candidates, :2] * (1, -1) + 1) * (width / 2, height / 2)
    pixels = np.clip(screen.astype(int), (0, 0), (width - 1, height - 1))
    center_depth = scene[pixels[:, 1], pixels[:, 0]]
    visible = ((ndc[candidates, 2] > center_depth + 1e-5) if view['reversedDepth'] else
               (ndc[candidates, 2] < center_depth - 1e-5))
    blocked = candidates[~visible]
    candidates = candidates[visible]
    require(len(candidates), 'No scene-visible requested-category probe outside the ordinary budget')
    score = np.sum(ndc[candidates, :2] ** 2, axis=1) + clip[candidates, 3] * .001
    index = candidates[np.argmin(score)]

    def choice(i):
        return dict(selection=dict(coordinate=xyz[i].tolist(), expectedGeneration=summary['field']['generation']),
                    counts=summary['counts'], id=int(ids[i]), category=category)

    result = choice(index)
    result['occluded'] = None
    model = copy.deepcopy(summary)
    model['probeVisualization']['settings']['radiusInSpacings'] *= 1.12
    blocked_score = np.sum(ndc[blocked, :2] ** 2, axis=1) + clip[blocked, 3] * .001
    for i in blocked[np.argsort(blocked_score)[:16]]:
        _, winners, _, depth = raster(model, np.asarray(((ids[i], 1),), dtype=np.uint32), full=True)
        covered = winners >= 0
        behind = (depth < scene - 1e-5) if view['reversedDepth'] else (depth > scene + 1e-5)
        if covered.sum() >= 20 and np.all(behind[covered]):
            result['occluded'] = choice(i)
            break
    return result


def validate_case(directory, choice, name):
    resource, summary, arrays, images = read(directory)
    display = summary['probeVisualization']
    require(display['schemaVersion'] in (4, 5, 6, 7) and not resource['frame']['developmentTools'], 'Selection is not a headless selection diagnostic')
    expected, counts = oracle(summary, arrays['GI.Field.Classification'])
    stats = arrays['GI.Probes.Counts'].reshape(-1)
    ids = arrays['GI.Probes.RenderedIDs'].reshape(2, -1).T
    require(np.array_equal(stats[2:9], counts) and np.array_equal(ids[:len(expected)], expected), 'Highlight changed ordinary filter counts or stable IDs')
    require(display['selectionLogicalBytes'] == 64 + display['settings']['budget'] * 8, 'Highlight added an unreported selection allocation')
    ready = name not in ('ordinary', 'cleared', 'replacement')
    require(stats[9] == display['highlightCount'] == int(ready), 'Highlight count is not separate from ordinary array count')
    require(display['highlight']['state'] == ('field-mismatch' if name == 'replacement' else 'ready' if ready else 'none'), 'Selection lifecycle state is incorrect')
    image = images['composited'].astype(float)
    changed = np.any(images['scene'] != images['composited'], axis=-1)
    yellow = np.all(np.abs(image - np.asarray((1, .9, .1)) * 255) <= 1, axis=-1) & changed
    if not ready:
        require(not yellow.any(), 'An inactive selection still draws its diagnostic overlay')
        if name == 'cleared':
            require(not changed.any(), 'Clear left diagnostic pixels with an empty ordinary array')
        return dict(case=name, pixels=0), arrays
    require(display['settings']['selection'] == choice['selection'] and
            summary['field']['generation'] == choice['selection']['expectedGeneration'], 'Temporal/display controls rebound or reset the selected field')
    model = copy.deepcopy(summary)
    model['probeVisualization']['settings']['radiusInSpacings'] *= 1.12
    mask, winners, _, near_depth = raster(model, np.asarray(((choice['id'], 1),), dtype=np.uint32), full=True)
    coverage = winners >= 0
    padded = coverage.copy()
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
        padded |= np.roll(coverage, (dy, dx), axis=(0, 1))
    require(not np.any(yellow & ~padded), 'Diagnostic pixels do not belong to the independently projected selected probe')
    if display['depthMode'] == 'xray':
        require(yellow.sum() >= 15, f'Selected probe disappeared outside ordinary filters/budget: {directory}')
    else:
        source = display['sceneDepthSource']
        scene = arrays[source].squeeze()
        occluded = mask & ((near_depth < scene - 1e-5) if display['view']['reversedDepth'] else (near_depth > scene + 1e-5))
        require(not np.any(yellow & occluded), 'Scene-depth highlight draws through a production-depth occluder')
        if name == 'occluded':
            require(occluded.sum() >= 20 and not yellow.any(), 'Fully scene-occluded highlight was not rejected')
        else:
            require(yellow.sum() >= 5, f'Scene-visible selected probe disappeared in depth-tested mode: {directory}')
    if name not in ('selected', 'after-rejection', 'occluded'):
        require(len(expected) == 0 and np.all(ids[:, 0] == 0xffffffff), 'Out-of-filter highlight entered ordinary IDs')
        clear = 0 if display['view']['reversedDepth'] else 1
        require(np.all(arrays['GI.Probes.PrivateDepth'] == clear), 'Diagnostic highlight wrote ordinary private depth')
    if 'GI.Field.Irradiance' in arrays:
        atlas = arrays['GI.Field.Irradiance']
        require(np.all(atlas[..., :3] == 0) and np.all(atlas[..., 3] == 1),
                'Black fixture must contain initialized atlas texels with zero RGB irradiance')
    return dict(case=name, pixels=int(yellow.sum()), generation=summary['field']['generation'],
                temporalAA=display['view']['temporalAA'], resolution=display['view']['resolutionPreset']), arrays


def validate(root):
    records = []
    for backend in load(root / 'backends.json'):
        for fixture in load(root / 'fixtures.json'):
            directory = root / backend / fixture['id']
            choice = load(directory / 'choice.json')
            require(choice['category'] == fixture['category'], 'Selected classification does not match the fixture')
            cases = ['ordinary', 'selected', 'after-rejection', 'filtered', 'cleared', 'irradiance', 'scene-depth',
                     'native-taa', 'native-taa-depth', 'quality', 'quality-depth']
            if (directory / 'replacement').is_dir():
                cases.append('replacement')
            if fixture['id'] == 'f4':
                require(choice['occluded'] is not None, 'Depth rejection requires a fully occluded probe control')
                cases.append('occluded')
            previous = None
            for case in cases:
                selected_choice = choice['occluded'] if case == 'occluded' else choice
                record, arrays = validate_case(directory / case, selected_choice, case)
                if case == 'ordinary':
                    previous = arrays
                if case in ('selected', 'after-rejection'):
                    require(np.array_equal(arrays['GI.Probes.PrivateDepth'], previous['GI.Probes.PrivateDepth']) and
                            np.array_equal(arrays['GI.Probes.RenderedIDs'], previous['GI.Probes.RenderedIDs']),
                            'Selection changed ordinary depth winners or IDs')
                record.update(backend=backend, fixture=fixture['id'])
                records.append(record)
            numeric = load(next((directory / 'numeric').glob('*.resource-export.json')))
            result = numeric['frame']['giProbeInspection']
            selected = numeric['frame']['readiness']['giField']['summary']['probeVisualization']['settings']['selection']
            require(selected == choice['selection'] and result['request']['coordinate'] != choice['selection']['coordinate'],
                    'Independent numeric inspection overwrote the persistent selection')
    return dict(ok=True, cases=records)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--choose', type=Path)
    parser.add_argument('--category', type=int)
    args = parser.parse_args()
    if args.choose:
        print(json.dumps(choose(args.choose, args.category)))
    else:
        result = validate(args.root)
        (args.root / 'validation.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(json.dumps(dict(ok=True, cases=len(result['cases']))))
