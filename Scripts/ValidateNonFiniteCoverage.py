"""Validate submitted-range watch metadata in captures, graphs and session receipts."""
import argparse
import copy
import json
from pathlib import Path

from jsonschema import Draft202012Validator


def validate(document, validator):
	validator.validate(document)
	frames, completed = document["frames"], document["completedFrames"]
	if completed > frames or (document["complete"] and completed != frames):
		raise ValueError("Completion counts disagree")
	first, last = document["firstFrameSerial"], document["lastFrameSerial"]
	if (frames == 0 and (first != 0 or last != 0)) or (frames != 0 and (first < 1 or last < first or frames > last - first + 1)):
		raise ValueError("Submitted range serials disagree")
	for claim in ("boundary", "production"):
		if claim not in document["claims"] and document[claim] != "Unavailable":
			raise ValueError("An unrequested claim cannot have coverage")
		if document["complete"] and claim in document["claims"] and document[claim] == "Unavailable":
			raise ValueError("A requested unavailable claim must complete as Incomplete")
	if document["instrumentation"] == "unavailable" and document["production"] in {"Passed", "Failed"}:
		raise ValueError("Production evidence requires counter instrumentation")


def find(document):
	if isinstance(document, dict):
		if document.get("schema") == "gglab.nonfinite-watch":
			yield document
		else:
			for value in document.values():
				yield from find(value)
	elif isinstance(document, list):
		for value in document:
			yield from find(value)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("paths", nargs="*", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args()
	schema = json.loads((Path(__file__).resolve().parents[1] / "Docs/Schemas/NonFiniteWatch.schema.json").read_text(encoding="utf-8"))
	Draft202012Validator.check_schema(schema)
	validator = Draft202012Validator(schema)
	if args.self_test:
		valid = dict(schema="gglab.nonfinite-watch", schemaVersion=1, configurationVersion=1, mode="fail",
			coverage="submitted-range", instrumentation="available", claims=["boundary", "production"], firstFrameSerial=4,
			lastFrameSerial=6, frames=2, completedFrames=2, complete=True, boundary="Passed", production="Passed",
			failure="", firstAnomalyFrameSerial=None, reason="None")
		validate(valid, validator)
		for changes in ({"completedFrames": 1}, {"frames": 4}, {"firstFrameSerial": 0},
			{"instrumentation": "unavailable"}, {"claims": ["boundary"]}, {"production": "Unavailable"}):
			invalid = copy.deepcopy(valid)
			invalid.update(changes)
			try:
				validate(invalid, validator)
			except Exception:
				pass
			else:
				raise AssertionError(f"Accepted invalid coverage: {changes}")
		print("Coverage validator self-test passed (6 rejection cases)")
	count = 0
	for path in args.paths:
		for file in sorted(path.rglob("*.json")) if path.is_dir() else [path]:
			for document in find(json.loads(file.read_text(encoding="utf-8"))):
				validate(document, validator)
				count += 1
	if args.paths and count == 0:
		raise ValueError("No submitted-range watch metadata found")
	if not args.paths and not args.self_test:
		parser.error("Provide metadata paths or --self-test")
	print(f"Validated {count} coverage records; 0 failures")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
