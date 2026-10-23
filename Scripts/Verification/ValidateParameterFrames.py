"""Validate frozen parameter subrecords and submitted sequence sample coverage."""

import argparse
import copy
import json
from functools import lru_cache
from pathlib import Path

from jsonschema import Draft202012Validator, ValidationError

import ValidateNonFiniteCoverage as coverage

SCHEMAS = Path(__file__).resolve().parents[2] / "Schemas"


@lru_cache(maxsize=1)
def validators():
	schema = json.loads((SCHEMAS / "ParameterFrames.schema.json").read_text(encoding="utf-8"))
	Draft202012Validator.check_schema(schema)
	frame_schema = dict(schema["$defs"]["frame"], **{"$defs": schema["$defs"]})
	return Draft202012Validator(schema), Draft202012Validator(frame_schema)


def validate_frame(frame, serial=None):
	validators()[1].validate(frame)
	if serial is not None and frame["frameSerial"] != serial:
		raise ValueError("Parameters belong to a different submitted frame")
	ids = [item["id"] for item in frame["values"]]
	if len(set(ids)) != len(ids) or any(item["value"] == item["default"] for item in frame["values"]):
		raise ValueError("Parameter values must be distinct non-default inputs")
	keys = set()
	for item in frame["receipts"]:
		key = item["batchId"], item["id"]
		if key in keys:
			raise ValueError("A frame cannot repeat a batch receipt")
		keys.add(key)
		stamps = [item[key] for key in ("requested", "applied", "published", "firstConsumed")]
		present = [stamp for stamp in stamps if stamp is not None]
		if present != sorted(present) or any(stamp > frame["frameSerial"] for stamp in present):
			raise ValueError("Receipt stamps must be ordered evidence at or before this frame")
		if item["firstConsumed"] is not None and (item["state"] != "Consumed" or None in stamps):
			raise ValueError("Consumption requires all four receipt stamps")
		if item["state"] == "Consumed" and item["firstConsumed"] is None:
			raise ValueError("Consumed receipts require submitted frame evidence")
		if item["declaredClass"] == "Immediate" and (item["identity"]["domain"] or item["identity"]["generation"]):
			raise ValueError("Immediate receipts have no asynchronous domain identity")
	return frame


def validate_export(frame, version, first_version, serial):
	parameters = frame.get("parameters")
	if version < first_version:
		if frame.get("parameterRevision") is not None or parameters is not None:
			raise ValueError("Legacy export versions do not carry parameter provenance")
	else:
		if parameters is None:
			raise ValueError("This export version requires frozen parameters")
		validate_frame(parameters, serial)
		if frame["parameterRevision"] != parameters["revision"]:
			raise ValueError("Export parameter revision disagrees with its frozen snapshot")


def validate(document):
	validators()[0].validate(document)
	sequence, frames = document["sequence"], document["frames"]
	per_frame = max(1, sequence["referenceSamples"])
	if len(frames) != sequence["frameCount"] * per_frame:
		raise ValueError("Parameter coverage must include every submitted sequence sample")
	previous = 0
	for index, sample in enumerate(frames):
		frame = validate_frame(sample["parameters"])
		if (sample["sequenceFrame"], sample["referenceSample"]) != divmod(index, per_frame) or frame["frameSerial"] <= previous:
			raise ValueError("Parameter frames must follow the complete submitted sequence grid")
		previous = frame["frameSerial"]
	if (frames[0]["parameters"]["frameSerial"], previous) != (sequence["firstFrameSerial"], sequence["lastFrameSerial"]):
		raise ValueError("Parameter frames disagree with the sequence serial range")
	watch = document["watch"]
	if watch is not None:
		schema = json.loads((SCHEMAS / "NonFiniteWatch.schema.json").read_text(encoding="utf-8"))
		coverage.validate(watch, Draft202012Validator(schema))
		if (not watch["complete"] or watch["frames"] != len(frames) or watch["completedFrames"] != len(frames) or
			watch["firstFrameSerial"] != sequence["firstFrameSerial"] or watch["lastFrameSerial"] != sequence["lastFrameSerial"]):
			raise ValueError("Parameter range requires complete matching watch coverage")
		if watch["mode"] == "fail" and (watch["failure"] or any(watch[claim] == "Failed" for claim in watch["claims"])):
			raise ValueError("Failed watch coverage cannot complete a parameter range")
	return document


def frame_fixture(serial=1):
	lighting = dict(generation=7, profileId=None, profileHash=None, profileHashVersion=None)
	return dict(schemaVersion=1, frameSerial=serial, revision=2, contentId="gglab.lab.fixture", available=True,
		values=[dict(id="view.exposure.manualEV100", source="host", type="float", value=1.0, default=0.0)],
		lighting=dict(requested=copy.deepcopy(lighting), active=copy.deepcopy(lighting)), receipts=[])


def fixture():
	return dict(schema="gglab.parameter-frames", schemaVersion=1,
		sequence=dict(id=3, path="fixture", pathVersion=1, frameCount=2, referenceSamples=0, firstFrameSerial=1, lastFrameSerial=3),
		frames=[dict(sequenceFrame=i, referenceSample=0, parameters=frame_fixture(serial)) for i, serial in enumerate((1, 3))], watch=None)


def self_test():
	good = fixture()
	validate(good)
	cases = []
	for key, value in (("frames", good["frames"][:1]), ("schemaVersion", 2)):
		bad = copy.deepcopy(good)
		bad[key] = value
		cases.append(bad)
	for keys, value in ((("sequenceFrame",), 0), (("parameters", "frameSerial"), 1),
		(("parameters", "revision"), -1), (("parameters", "values", 0, "value"), 0),
		(("parameters", "lighting", "active", "profileId"), "unproven-profile")):
		bad = copy.deepcopy(good)
		target = bad["frames"][1]
		for key in keys[:-1]:
			target = target[key]
		target[keys[-1]] = value
		cases.append(bad)
	for bad in cases:
		try:
			validate(bad)
		except (ValueError, ValidationError):
			pass
		else:
			raise AssertionError("Accepted invalid frozen parameter evidence")
	print("Parameter frame schema self-tests passed")


if __name__ == "__main__":
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("paths", nargs="*", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args()
	if args.self_test:
		self_test()
	for path in args.paths:
		validate(json.loads(path.read_text(encoding="utf-8")))
		print(f"Validated {path}")
