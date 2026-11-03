"""Independent completed-change/block arithmetic and heatmap raster oracle."""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

from ValidateGIProbeVisualization import load, oracle, require
from ValidateGIProbeIrradiance import raster
import ValidateCaptureBundles as bundles


def validate_measurement(summary, array):
    observation = summary['changeObservation']
    display = summary['probeVisualization']
    require(observation == display['changeObservation'], 'Heatmap and numeric explanations use different observations')
    stamp = observation['observation']
    require(stamp['field'] == summary['field'] and stamp['kind'] == 'completed-readiness' and
            stamp['readinessUpdate'] == stamp['fieldUpdate'] - stamp['field']['epochOriginUpdate'],
            'Completed data was rebound to a different field/epoch')
    words = array.reshape(-1)
    require(words.dtype == np.dtype('<u4') and np.array_equal(words[:2], [0x47494331, 1]), 'Invalid Change ABI')
    count, blocks = map(int, words[2:4])
    length = (8 + count + 2 * blocks) * 4
    require(len(words) == (length + 255) // 256 * 256 and np.all(words[length:] == 0), 'Unbounded/malformed Change padding')
    records = words[:length].reshape(-1, 4)
    u64 = lambda low, high: int(low) | (int(high) << 32)
    require(u64(*records[1, :2]) == stamp['field']['generation'] and
            u64(*records[1, 2:]) == stamp['field']['refreshEpoch'] and
            u64(*records[2, :2]) == stamp['producingFrame'] and
            u64(*records[2, 2:]) == stamp['fieldUpdate'] and
            u64(*records[3, :2]) == stamp['readinessUpdate'] and
            u64(*records[3, 2:]) == stamp['field']['epochOriginUpdate'], 'Image/export stamp differs from completed source')
    require(count == observation['probeCount'] == np.prod(summary['counts']) and blocks == len(observation['blocks']),
            'Probe/block payload extent differs')
    require(np.array_equal(records[6, :3], summary['counts']) and records[6, 3] == observation['blockWidth'], 'Grid ABI differs')
    values = records[4].view(np.float32)
    require(np.allclose(values, [observation['threshold'], observation['maximumThreshold'],
                               observation['irradianceFloor'], observation['noiseSigma']], rtol=1e-6), 'Threshold profile differs')
    require(np.isclose(values[1], 3 * values[0], rtol=1e-6) and values[0] > 0 and values[2] >= .001,
            'Invalid estimator threshold/floor')
    require(bool(records[5, 1] & 1) == (observation['phase'] == 'refresh') and
            bool(records[5, 1] & 2) == observation['includesDistance'] and
            np.isclose(records[5, :1].copy().view(np.float32)[0], observation['hysteresis'], rtol=1e-6) and
            records[5, 2] == observation['warmingUpdates'] and records[5, 3] == observation['requiredQuietUpdates'],
            'Change phase/warming/hold differs')
    probes = records[8:8 + count]
    changes = probes[:, 0].copy().view(np.float32)
    active = probes[:, 1] == 1
    require(np.isfinite(changes).all() and (changes >= 0).all() and np.isin(probes[:, 1], [1, 2]).all(), 'Invalid raw change population')
    nx, ny, nz = summary['counts']
    block_width = observation['blockWidth']
    index = 0
    categories = probes[:, 1].reshape(ny, nz, nx)
    grid = changes.reshape(ny, nz, nx)
    for y in range(0, ny, block_width):
        for z in range(0, nz, block_width):
            for x in range(0, nx, block_width):
                sl = np.s_[y:y + block_width, z:z + block_width, x:x + block_width]
                population = np.sort(grid[sl][categories[sl] == 1].astype(float))
                block = observation['blocks'][index]
                raw = records[8 + count + 2 * index]
                decisions = records[9 + count + 2 * index]
                require(raw[0] == block['activeProbes'] == len(population) and
                        decisions[0] == block['blockers'] and raw[3] == block['quietUpdates'] and
                        bool(decisions[1]) == block['warmingMet'] and bool(decisions[2]) == block['currentQuiet'],
                        'Block decisions differ between image upload and numeric explanation')
                if not len(population):
                    require(block['excluded'] and block['p95'] is None and block['maximum'] is None and
                            block['blockers'] == 0 and raw[1] == raw[2] == 0, 'Excluded block invented measurements or blockers')
                else:
                    p95 = population[(95 * len(population) + 99) // 100 - 1]
                    maximum = population[-1]
                    require(not block['excluded'] and np.isclose(block['p95'], p95, rtol=1e-6) and
                            np.isclose(block['maximum'], maximum, rtol=1e-6) and
                            np.allclose(raw[1:3].copy().view(np.float32), [p95, maximum], rtol=1e-6),
                            'Independent per-probe reduction disagrees with the estimator explanations')
                    warming = stamp['readinessUpdate'] >= observation['warmingUpdates']
                    reasons = (0 if warming else 1) | (2 if p95 > values[0] else 0) | (4 if maximum > values[1] else 0)
                    reasons |= 8 if block['quietUpdates'] < observation['requiredQuietUpdates'] else 0
                    require(block['warmingMet'] == warming and block['blockers'] == reasons and
                            block['currentQuiet'] == (p95 <= values[0] and maximum <= values[1]),
                            'Blocker/quiet-hold report does not preserve estimator decisions')
                index += 1
    active_blocks = sum(b['activeProbes'] > 0 for b in observation['blocks'])
    quiet_blocks = sum(b['activeProbes'] > 0 and b['blockers'] == 0 for b in observation['blocks'])
    require(np.array_equal(records[7, 1:], [active_blocks, quiet_blocks, blocks-active_blocks]),
            'Field aggregate differs from completed block decisions')
    return changes, categories


def validate(root):
    output = []
    for case in load(root / 'cases.json'):
        directory = root / case['directory']
        bundles.validate(next(directory.glob('*.capture-bundle.json')))
        graph = load(next(directory.glob('*.graph.json')))
        summary = graph['frame']['readiness']['giField']['summary']
        display = summary['probeVisualization']
        require(display['schemaVersion'] == 6 and display['mode'] == 'change', 'Change capability is not implemented')
        images, metadata = {}, {}
        for path in directory.glob('*.json'):
            value = load(path)
            if value.get('image'):
                metadata[value['source']] = value
                images[value['source']] = np.asarray(Image.open(directory / value['image']['file']))[..., :3]
        passes = {p['name'] for p in graph['compile']['passes'] if not p['culled']}
        changed = np.any(images['scene'] != images['composited'], axis=-1)
        if not case['arm']['enabled']:
            require(not any(p.startswith('GI.Probes.') for p in passes) and not changed.any(), 'Unavailable/off Change still draws')
            if case['fixture'] == 'disabled':
                require(summary['changeObservation'] is None and not display['available'], 'Disabled readiness invented a measurement')
            output.append(dict(directory=case['directory'], available=False))
            continue
        resource = load(next(directory.glob('*.resource-export.json')))
        require(resource['frame']['readiness']['giField']['summary'] == summary, 'Products use different frozen summaries')
        require(metadata['composited']['readiness']['giField']['summary'] == summary, 'Image and data are not paired')
        arrays = {t['address']['resource']: np.load(directory / t['array'], allow_pickle=False) for t in resource['targets']}
        changes, categories = validate_measurement(summary, arrays['GI.Probes.Change'])
        require(display['changeBlockOutlineCount'] == (len(summary['changeObservation']['blocks']) if case['arm']['blocks'] else 0),
                'Block outline work is not separately reported')
        require(summary['changeObservation']['observation']['producingFrame'] < resource['frameSerial'], 'Completed source lost its independent producing frame')
        selected, counts = oracle(summary, categories)
        stats = arrays['GI.Probes.Counts'].reshape(-1)
        require(np.array_equal(stats[2:9], counts) and np.array_equal(
            arrays['GI.Probes.RenderedIDs'].reshape(2, -1).T[:len(selected)], selected), 'Change filters use a different observation')
        record = dict(directory=case['directory'], probes=len(changes), blocks=len(summary['changeObservation']['blocks']))
        if len(selected) and not case['arm']['blocks']:
            mask, winners, _, depth = raster(summary, selected, full=True, edge_guard=1 / 16)
            if case['arm']['depth']:
                scene = arrays['DisplayView.DepthBuffer'].squeeze()
                margin = np.maximum(np.abs(scene) * 2e-6, 2e-7)
                mask &= depth > scene + margin if display['view']['reversedDepth'] else depth < scene - margin
            if not case['arm']['depth']:
                require(mask.sum() >= 20, 'No unambiguous heatmap pixels')
            if mask.any():
                value = changes[winners[mask]].astype(float)
                if case['arm']['normalized']:
                    value /= summary['changeObservation']['threshold']
                t = np.clip(value / display['settings']['changeRange'], 0, 1)
                color = np.clip(np.column_stack((2*t, 2-2*np.abs(2*t-1), 2-2*t)), 0, 1) * 255
                error = np.max(np.abs(images['composited'][mask].astype(float) - color), axis=1)
                require(np.quantile(error, .99) <= 2, f'Heatmap differs from completed raw values: {case["directory"]}')
                record.update(pixels=int(mask.sum()), p99ByteError=float(np.quantile(error, .99)))
        if case['fixture'] == 'black':
            require(np.all(arrays['GI.Field.Irradiance'][..., :3] == 0) and
                    summary['changeObservation']['includesDistance'],
                    'Black-field energy or the declared distance-change signal is incorrect')
        if case['fixture'] == 'excluded':
            require(not np.any(categories == 1), 'Excluded fixture has active probes')
            if case['arm']['blocks']:
                require(changed.any(), 'Excluded block outlines disappeared')
        output.append(record)
    return dict(ok=True, cases=output, qualityAcceptance=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--images', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(validate(args.images)))
