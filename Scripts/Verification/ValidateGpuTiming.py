"""Validate complete sequence timing, sample membership and reported statistics."""
import argparse
import copy
import json
import math
from pathlib import Path

from jsonschema import Draft202012Validator, ValidationError
import ValidateNonFiniteCoverage as coverage

SCHEMAS = Path(__file__).resolve().parents[2] / "Schemas"


def validator(name="GpuTiming.schema.json"):
	schema = json.loads((SCHEMAS / name).read_text(encoding="utf-8"))
	Draft202012Validator.check_schema(schema)
	return Draft202012Validator(schema)


def summary(values):
	ordered = sorted(values)
	return dict(count=len(values), meanMs=sum(values) / len(values),
		medianMs=ordered[math.ceil(len(values) * 0.5) - 1],
		p90Ms=ordered[math.ceil(len(values) * 0.9) - 1], minMs=ordered[0], maxMs=ordered[-1])


def check_statistics(values, statistics):
	if not values or any(not math.isfinite(value) or value < 0 for value in values):
		raise ValueError("Timing values must be finite and non-negative")
	for key, expected in summary(values).items():
		if not math.isfinite(statistics[key]) or not math.isclose(statistics[key], expected, rel_tol=1e-9, abs_tol=1e-10):
			raise ValueError(f"Incorrect timing statistic: {key}")


def validate(document, timing_validator=None):
	(timing_validator or validator()).validate(document)
	sequence, recorded = document["sequence"], document["coverage"]
	references = max(1, sequence["referenceSamples"])
	expected = sequence["frameCount"] * references
	samples = recorded["samples"]
	if recorded["requestedSamples"] != expected or recorded["recordedSamples"] != expected or len(samples) != expected:
		raise ValueError("A complete timing product requires every requested sample")
	serials = [sample["frameSerial"] for sample in samples]
	if len(set(serials)) != expected or min(serials) != sequence["firstFrameSerial"] or max(serials) != sequence["lastFrameSerial"]:
		raise ValueError("Sample serials disagree with the submitted sequence range")
	identities = [(sample["sequenceFrame"], sample["referenceSample"]) for sample in samples]
	if (len(set(identities)) != expected or
		any(frame >= sequence["frameCount"] or reference >= references for frame, reference in identities)):
		raise ValueError("Sample identities do not cover the requested frame/reference grid")
	ordered = sorted(zip(identities, serials))
	if [serial for _, serial in ordered] != sorted(serials):
		raise ValueError("Render serials must follow submission order, independent of result arrival order")
	if any(sample["firstFrame"] != (sample["sequenceFrame"] == 0) or
		not sample["conditions"]["gpuProfiling"] for sample in samples):
		raise ValueError("First-frame or profiling measurement facts are inconsistent")
	if len(document["frame"]["milliseconds"]) != expected:
		raise ValueError("Every recorded sample requires a frame time")
	check_statistics(document["frame"]["milliseconds"], document["frame"]["statistics"])
	keys, stable = set(), True
	for scope in document["scopes"]:
		key = scope["kind"], scope["name"], scope["occurrence"]
		if key in keys:
			raise ValueError("Scope identity is duplicated")
		keys.add(key)
		members = scope["sampleIndices"]
		if len(members) != len(scope["milliseconds"]) or any(index >= expected for index in members):
			raise ValueError("Scope times must name their actual recorded samples")
		check_statistics(scope["milliseconds"], scope["statistics"])
		stable &= scope["kind"] != "pass" or len(members) == expected
	if document["stablePassSet"] != stable:
		raise ValueError("Pass structure stability disagrees with sample membership")
	if document["measurement"]["profilingStateChanged"] != any(sample["conditions"]["profilingStateChanged"] for sample in samples):
		raise ValueError("Profiling change summary disagrees with recorded samples")
	watch = document["watch"]
	if watch is not None:
		coverage.validate(watch, validator("NonFiniteWatch.schema.json"))
		if (not watch["complete"] or watch["frames"] != expected or watch["completedFrames"] != expected or
			watch["firstFrameSerial"] != min(serials) or watch["lastFrameSerial"] != max(serials)):
			raise ValueError("Timing coverage must resolve its entire submitted range")
		if watch["mode"] == "fail" and (watch["failure"] or any(watch[claim] == "Failed" for claim in watch["claims"])):
			raise ValueError("Failed required watch coverage cannot publish complete timing")
	return document


def fixture():
	samples = [dict(frameSerial=10 + i, sequenceFrame=i // 2, referenceSample=i % 2,
		firstFrame=i < 2, cameraCut=True, conditions=dict(gpuProfiling=True,
		profilingStateChanged=False, developmentTools=False, graph=False,
		resourceExport=False, nonFiniteScan=False, productionCounters=False)) for i in range(4)]
	values = [1.0, 2.0, 3.0, 4.0]
	scopes = [dict(kind="pass", name="Repeated", occurrence=2,
		sampleIndices=[0, 1, 2, 3], milliseconds=values, statistics=summary(values)),
		dict(kind="pass", name="Repeated", occurrence=4, sampleIndices=[0, 2],
		milliseconds=[2.0, 3.0], statistics=summary([2.0, 3.0])),
		dict(kind="other", name="Repeated", occurrence=None, sampleIndices=[1],
		milliseconds=[0.5], statistics=summary([0.5]))]
	return dict(schema="gglab.gpu-timing", schemaVersion=1,
		sequence=dict(id=3, path="Test", pathVersion=1, frameCount=2, referenceSamples=2,
			firstFrameSerial=10, lastFrameSerial=13), context=dict(backend="dx12",
			content={}, fixedDeltaTime=None, initialTemporal={}),
		coverage=dict(requestedSamples=4, recordedSamples=4, samples=samples),
		frame=dict(milliseconds=values, statistics=summary(values)), scopes=scopes,
		stablePassSet=False, measurement=dict(gpuProfilingRequested=True,
			validation=dict(requested="off", effective="off"), profilingStateChanged=False), watch=None)


def self_test(timing_validator):
	valid = fixture()
	validate(valid, timing_validator)
	reordered = copy.deepcopy(valid)
	reordered["coverage"]["samples"] = list(reversed(reordered["coverage"]["samples"]))
	validate(reordered, timing_validator)  # Membership names arrival-order indices.
	cases = []
	for section, key, value in (("coverage", "requestedSamples", 3), ("coverage", "recordedSamples", 3),
		("sequence", "lastFrameSerial", 99), ("sequence", "frameCount", 1),
		("measurement", "profilingStateChanged", True), ("frame", "milliseconds", [1.0])):
		bad = copy.deepcopy(valid)
		bad[section][key] = value
		cases.append(bad)
	for key, value in (("frameSerial", 11), ("sequenceFrame", 3), ("referenceSample", 2), ("firstFrame", False)):
		bad = copy.deepcopy(valid)
		bad["coverage"]["samples"][0][key] = value
		cases.append(bad)
	for key, value in (("sampleIndices", [0, 0]), ("sampleIndices", [4]),
		("sampleIndices", [0]), ("occurrence", None), ("milliseconds", [-1.0] * 4),
		("milliseconds", [math.nan] * 4), ("milliseconds", [math.inf] * 4)):
		bad = copy.deepcopy(valid)
		bad["scopes"][0][key] = value
		cases.append(bad)
	for mutate in (lambda d: d.update(stablePassSet=True),
		lambda d: d["scopes"].append(copy.deepcopy(d["scopes"][0])),
		lambda d: d["scopes"][2].update(occurrence=1),
		lambda d: d["frame"]["statistics"].update(meanMs=100),
		lambda d: d["coverage"]["samples"][0]["conditions"].update(gpuProfiling=False)):
		bad = copy.deepcopy(valid)
		mutate(bad)
		cases.append(bad)
	for bad in cases:
		try:
			validate(bad, timing_validator)
		except (ValueError, ValidationError):
			# Schema errors and semantic errors both reject the malformed product.
			continue
		raise AssertionError(f"Accepted malformed GPU timing: {bad}")
	print(f"GPU timing validator self-test passed ({len(cases)} rejection cases)")


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("paths", nargs="*", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args()
	rules = validator()
	if args.self_test:
		self_test(rules)
	if not args.paths and not args.self_test:
		parser.error("Provide timing products or --self-test")
	files = set()
	for path in args.paths:
		found = set(path.rglob("*.gpu-timing.json")) if path.is_dir() else {path}
		if not found:
			parser.error(f"No GPU timing products found in {path}")
		files.update(found)
	failed = 0
	for path in sorted(files):
		try:
			validate(json.loads(path.read_text(encoding="utf-8")), rules)
		except Exception as error:
			print(f"FAIL {path}: {error}")
			failed += 1
		else:
			print(f"PASS {path}")
	if files:
		print(f"Validated {len(files)} GPU timing products; {failed} failures")
	return int(failed != 0)


if __name__ == "__main__":
	raise SystemExit(main())
