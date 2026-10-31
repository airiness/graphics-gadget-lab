"""Injected GI numerical errors quarantine the owner without poisoning its atlases."""
import argparse
from pathlib import Path
import numpy as np
from ValidateGIProbeUpdate import load, save, require, fields


def snapshot(directory, field):
    record = load(next(directory.glob('*.resource-export.json')))
    arrays = {}
    for target in record['targets']:
        value = np.load(directory / target['array'], allow_pickle=False)
        if value.dtype.names == ('radianceDistance',):
            value = value['radianceDistance']
        arrays[target['address']['resource'].rsplit('.', 1)[-1]] = value
    require(set(arrays) == {'Rays', 'Irradiance', 'Distance', 'State', 'UpdateStatus'}, 'Missing diagnostic payload')
    for name in ('Irradiance', 'Distance', 'State'):
        require(np.isfinite(arrays[name]).all(), f'{directory}: nonfinite persistent {name}')
    status = arrays['UpdateStatus']
    require(status.shape == (1,) and status.dtype.names == ('error', 'probe', 'update', 'element') and
            all(status.dtype[n] == np.dtype('<u4') for n in status.dtype.names), 'GPU status ABI differs')
    verdict = {n: int(status[n][0]) for n in status.dtype.names}
    metrics = {m['name']: m['value'] for m in load(directory / 'status.json')['lab']['metrics']}
    progress = fields(metrics['GI field' if field else 'Probe progress'])
    cpu = fields(metrics['GI field update status' if field else 'Probe update status'])
    require(cpu == verdict, 'CPU status disagrees with the completed GPU readback')
    return arrays, verdict, progress


def validate(root, backends):
    count = 0
    for backend in backends:
        for mode in ('direct', 'shared', 'field'):
            for directory in sorted((root / backend / mode).iterdir()):
                case = load(directory / 'case.json')
                bad, status, progress = snapshot(directory / 'bad', mode == 'field')
                frozen, frozen_status, frozen_progress = snapshot(directory / 'frozen', mode == 'field')
                require(status['error'] == case['error'] and status['probe'] == 0 and
                        status['element'] == case['element'], 'First error lost its injected location')
                flag = 'invalid' if mode == 'field' else 'failed'
                require(progress[flag] == 1 and progress['initialized'] == 0 and
                        progress['completed'] <= status['update'] < progress['submitted'],
                        'A failed numerical update became completed/initialized')
                require(frozen_status == status and frozen_progress[flag] == 1 and
                        frozen_progress['submitted'] == progress['submitted'] and
                        frozen_progress['completed'] == progress['completed'], 'Disabling injection resumed a poisoned owner')
                for name in bad:
                    require(np.array_equal(bad[name], frozen[name], equal_nan=True) if not bad[name].dtype.names
                            else np.array_equal(bad[name], frozen[name]), 'Quarantined storage changed')
                if case['error'] == 1:
                    require(status['update'] == 0 and np.isnan(bad['Rays'][case['element']]).all(),
                            'The first bad ray was hidden or overwritten')
                    require(np.all(bad['Irradiance'] == 0) and np.all(bad['Distance'] == 0),
                            'Trace failure wrote atlas data after initialization')
                    require(bad['State'][0, 0, 0, 3] == 1 / 256, 'Invalid ray fraction was lost')
                else:
                    warm, warm_status, warm_progress = snapshot(directory / 'warm', mode == 'field')
                    require(warm_status['error'] == 0 and warm_progress['completed'] > 0 and
                            np.any(warm['Irradiance'][..., :3] > 0), 'History injection did not use initialized nonzero history')
                    require(np.isfinite(bad['Rays']).all(), 'History rejection corrupted the raw rays')
                clean, clean_status, clean_progress = snapshot(directory / 'replaced', mode == 'field')
                require(clean_status == dict(error=0, probe=2**32-1, update=2**32-1, element=2**32-1) and
                        clean_progress[flag] == 0 and clean_progress['initialized'] == 1 and
                        clean_progress['completed'] > 0 and np.isfinite(clean['Rays']).all(),
                        'Safe owner replacement did not restore valid updates')
                if mode == 'field':
                    require(clean_progress['generation'] != progress['generation'], 'Recovery reused the invalid field generation')
                count += 1
    require(count == len(backends) * 15, 'Injection matrix is incomplete')
    return dict(status='passed', cases=count, backends=backends)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--rhi', default='dx12,vulkan')
    args = parser.parse_args()
    result = validate(args.root, args.rhi.split(','))
    save(args.root / 'validation.json', result)
    print(f"GI numerical quarantine checks passed ({result['cases']} cases)")
