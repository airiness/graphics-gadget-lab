"""Validate final bundles, required files, frame provenance and sequence timing."""

import argparse
import copy
import json
import struct
import tempfile
import zlib
from pathlib import Path, PureWindowsPath

import numpy as np
from jsonschema import Draft202012Validator, ValidationError

import ValidateNonFiniteCoverage as coverage
import ValidateRenderGraphExports as graphs
import ValidateResourceExports as resources
import ValidateGpuTiming as timing


SCHEMAS = Path(__file__).resolve().parents[2] / "Schemas"
PROVENANCE = ("backend", "content", "sceneInputs", "frame", "camera", "time",
	"developmentTools", "readiness", "temporal", "sequence")


def validator(name):
	schema = json.loads((SCHEMAS / name).read_text(encoding="utf-8"))
	Draft202012Validator.check_schema(schema)
	return Draft202012Validator(schema)


def local_file(root, name):
	if (not name or name in (".", "..") or "/" in name or "\\" in name or
		PureWindowsPath(name).drive or name.endswith(".partial")):
		raise ValueError("Bundle files must be final local filenames")
	path = root / name
	if not path.is_file() or path.resolve().parent != root.resolve():
		raise ValueError(f"Missing or external bundle file: {name}")
	return path


def validate(path, bundle_validator=None):
	document = json.loads(path.read_text(encoding="utf-8"))
	(bundle_validator or validator("CaptureBundle.schema.json")).validate(document)
	if document["consistency"] == "sequence-range":
		return validate_sequence(path, document)
	identity = f'{document["runIdentity"]}-j{document["jobId"]}-a{document["attempt"]}'
	if document["bundleId"] != identity:
		raise ValueError("Bundle identity disagrees with its qualified job/attempt")
	environment = document.get("environment")
	if environment is not None:
		run = environment["process"]["runIdentity"]
		if run is not None and run != document["runIdentity"]:
			raise ValueError("Environment run identity disagrees with the bundle")
		device = environment["device"]
		if device is not None:
			if device["backend"] != document["provenance"]["backend"]:
				raise ValueError("Environment backend disagrees with the captured frame")
			if device["compatibilityIdentity"] != document["submission"]["adapterDriverIdentity"]:
				raise ValueError("Environment adapter disagrees with the bundle submission")
	products = document["products"]
	if any(sum(product["source"] == source for product in products) > 1 for source in ("graph", "resource-export")):
		raise ValueError("A bundle supports one required graph and one resource group")
	if [product["index"] for product in products] != list(range(len(products))):
		raise ValueError("Required product indices must be contiguous and in plan order")
	watch = document["watch"]
	if watch is not None:
		coverage.validate(watch, validator("NonFiniteWatch.schema.json"))
		serial = document["provenance"]["frame"]["serial"]
		if (not watch["complete"] or watch["frames"] != 1 or watch["completedFrames"] != 1 or
			watch["firstFrameSerial"] != serial or watch["lastFrameSerial"] != serial):
			raise ValueError("Bundle coverage must resolve exactly its submitted frame")
		if watch["mode"] == "fail" and (watch["failure"] or any(watch[claim] == "Failed" for claim in watch["claims"])):
			raise ValueError("Failed required coverage cannot complete a bundle")
	all_files = set()
	graph_files = {product["metadata"] for product in products if product["source"] == "graph"}
	for product in products:
		files = product["files"]
		if product["metadata"] not in files or path.name in files or all_files.intersection(files):
			raise ValueError("Each required file belongs to exactly one product")
		all_files.update(files)
		for name in files:
			local_file(path.parent, name)
		metadata_path = local_file(path.parent, product["metadata"])
		metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
		if metadata["schemaVersion"] != product["schemaVersion"]:
			raise ValueError("Product version disagrees with its owning metadata")
		source = product["source"]
		if source in ("scene", "composited", "diagnostic"):
			if product["schema"] != "gglab.frame-capture" or product["schemaVersion"] != 1:
				raise ValueError("Unsupported image metadata schema")
			if metadata["source"] != source or metadata["diagnosticTap"] != product["diagnosticTap"]:
				raise ValueError("Image source/tap disagrees with the required product")
			if (source == "diagnostic") != (product["diagnosticTap"] is not None):
				raise ValueError("Only a Diagnostic product may name a Diagnostic tap")
			image = metadata["image"]["file"]
			if set(files) != {product["metadata"], image} or len(files) != 2:
				raise ValueError("An image product requires its PNG and sidecar")
			with local_file(path.parent, image).open("rb") as stream:
				header = stream.read(24)
			if (len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR" or
				struct.unpack(">II", header[16:24]) != (metadata["image"]["width"], metadata["image"]["height"])):
				raise ValueError("PNG header dimensions disagree with metadata")
			frame = metadata
		elif source == "graph":
			if product["schema"] != "gglab.render-graph" or files != [product["metadata"]]:
				raise ValueError("A graph product requires one owning graph record")
			graphs.validate(metadata, validator("RenderGraph.schema.json"))
			frame = metadata["frame"]
		else:
			if product["schema"] != "gglab.resource-export":
				raise ValueError("Wrong resource metadata schema")
			if bool(graph_files) != (metadata["graph"] is not None):
				raise ValueError("A required graph and resource group must share their paired delivery")
			for target in metadata["targets"]:
				local_file(path.parent, target["array"])
			if metadata["graph"] is not None:
				local_file(path.parent, metadata["graph"])
				if metadata["graph"] not in graph_files:
					raise ValueError("A paired graph must be a required product of this bundle")
			resources.validate(metadata_path, validator("ResourceExport.schema.json"))
			if set(files) != {product["metadata"], *(target["array"] for target in metadata["targets"])}:
				raise ValueError("Resource files disagree with the array inventory")
			frame = metadata["frame"]
		if source in ("graph", "resource-export"):
			if product["diagnosticTap"] is not None:
				raise ValueError("Numerical/graph products cannot name an image tap")
			for key in ("fenceIndex", "fenceGeneration", "fenceValue"):
				if metadata["submission"][key] != document["submission"][key]:
					raise ValueError("Product submission disagrees with the bundle")
			if frame["adapterDriverIdentity"] != document["submission"]["adapterDriverIdentity"]:
				raise ValueError("Product adapter disagrees with the bundle")
		if any(frame.get(key) != document["provenance"][key] for key in PROVENANCE) or frame.get("watch") != watch:
			raise ValueError("Required products disagree on actual frame, camera, time, scene inputs or watch coverage")
	return document


def validate_sequence(path, document):
	sequence, environment = document["sequence"], document["environment"]
	if document["bundleId"] != f'{document["runIdentity"]}-s{sequence["id"]}-timing':
		raise ValueError("Sequence bundle identity disagrees with its qualified sequence")
	if environment["process"]["runIdentity"] not in (None, document["runIdentity"]):
		raise ValueError("Environment run identity disagrees with the sequence bundle")
	product = document["products"][0]
	if product["files"] != [product["metadata"]] or product["metadata"] == path.name:
		raise ValueError("A timing bundle requires exactly one owning timing product")
	metadata = json.loads(local_file(path.parent, product["metadata"]).read_text(encoding="utf-8"))
	timing.validate(metadata)
	if metadata["sequence"] != sequence or metadata["watch"] != document["watch"]:
		raise ValueError("Timing product disagrees with its bundle sequence range or watch coverage")
	if metadata["measurement"]["validation"] != environment["validation"]:
		raise ValueError("Timing measurement validation disagrees with its environment")
	if environment["device"] is not None and metadata["context"]["backend"] != environment["device"]["backend"]:
		raise ValueError("Timing backend disagrees with its environment")
	return document


def self_test(bundle_validator):
	with tempfile.TemporaryDirectory(prefix="gglab-bundle-schema-") as directory:
		root = Path(directory)
		frame = dict(backend="dx12", content={}, sceneInputs=None, frame=dict(serial=1, index=0),
			camera={}, time={}, developmentTools=False, readiness={}, temporal={}, sequence=None)
		image = dict(frame, schemaVersion=1, source="scene", diagnosticTap=None, watch=None,
			image=dict(file="image.png", width=1, height=1))
		(root / "image.json").write_text(json.dumps(image), encoding="utf-8")
		def chunk(kind, data):
			return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
		png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0))
		png += chunk(b"IDAT", zlib.compress(b"\0\xff\0\0\xff")) + chunk(b"IEND", b"")
		(root / "image.png").write_bytes(png)
		run = "0123456789abcdef" * 2
		product = dict(index=0, source="scene", diagnosticTap=None, schema="gglab.frame-capture",
			schemaVersion=1, metadata="image.json", files=["image.png", "image.json"])
		environment = dict(schemaVersion=1,
			build=dict(source="build-stamp", revision="0123456789abcdef" * 2 + "01234567", dirty=False,
				configuration="Release", compiler="MSVC 19.0.0", pixIntegration=False),
			process=dict(runIdentity=run, startedAtUtc="2026-01-01T00:00:00Z", os="Windows 10.0.1", host="hidden"),
			device=dict(backend="dx12", adapterName="Fixture adapter", vendorId=1, deviceId=2, subsystemId=None,
				revision=None, driverVersion=None, driverVersionRaw=None, api=dict(featureLevel="12_2"),
				dedicatedVideoMemoryBytes=None, compatibilityIdentity="test"),
			validation=dict(requested="off", effective=None))
		document = dict(schema="gglab.capture-bundle", schemaVersion=2, bundleId=f"{run}-j1-a1",
			runIdentity=run, jobId=1, attempt=1, consistency="same-frame", environment=environment, provenance=frame,
			submission=dict(fenceIndex=1, fenceGeneration=1, fenceValue=1, adapterDriverIdentity="test"),
			watch=None, products=[product])
		path = root / "fixture.capture-bundle.json"
		def check(value):
			path.write_text(json.dumps(value), encoding="utf-8")
			validate(path, bundle_validator)
		check(document)
		# Version 1 manifests predate the environment record and remain readable.
		legacy = copy.deepcopy(document)
		legacy["schemaVersion"] = 1
		del legacy["environment"]
		check(legacy)
		# Unknown build and device facts are reported as null, never estimated.
		unknown = copy.deepcopy(document)
		unknown["environment"]["build"] = dict(source="unavailable", revision=None, dirty=None,
			configuration=None, compiler=None, pixIntegration=None)
		unknown["environment"]["device"] = None
		unknown["environment"]["validation"] = dict(requested=None, effective=None)
		check(unknown)
		cases = []
		for key, value in (("schemaVersion", 3), ("jobId", 0), ("attempt", 2),
			("runIdentity", "old-crc"), ("consistency", "mixed-frame"), ("products", [])):
			invalid = copy.deepcopy(document)
			invalid[key] = value
			cases.append(invalid)
		invalid = copy.deepcopy(document)
		del invalid["environment"]
		cases.append(invalid)
		cases.append(dict(copy.deepcopy(document), schemaVersion=1))
		for section, key, value in (("build", "revision", "not-a-revision"), ("build", "source", "unavailable"),
			("process", "runIdentity", "f" * 32), ("process", "startedAtUtc", "yesterday"),
			("device", "backend", "vulkan"), ("device", "compatibilityIdentity", "other"),
			("validation", "requested", "full")):
			invalid = copy.deepcopy(document)
			invalid["environment"][section][key] = value
			cases.append(invalid)
		for key, value in (("index", 1), ("source", "diagnostic"), ("schemaVersion", 2),
			("metadata", "../image.json"), ("metadata", "C:other.json"),
			("files", ["image.json"]), ("files", ["image.json", "image.png", "missing.png"]),
			("files", ["image.json", "image.png", "../outside.png"]),
			("diagnosticTap", "temporal-history-weight")):
			invalid = copy.deepcopy(document)
			invalid["products"][0][key] = value
			cases.append(invalid)
		for key, value in (("frame", dict(serial=2, index=0)), ("time", dict(total=1)), ("camera", dict(name="other"))):
			invalid = copy.deepcopy(document)
			invalid["provenance"][key] = value
			cases.append(invalid)
		invalid = copy.deepcopy(document)
		invalid["products"].append(dict(product, index=1))
		cases.append(invalid)
		measurement = timing.fixture()
		measurement["measurement"]["validation"] = environment["validation"]
		(root / "timing.json").write_text(json.dumps(measurement), encoding="utf-8")
		sequence_bundle = dict(schema="gglab.capture-bundle", schemaVersion=2,
			bundleId=f"{run}-s3-timing", runIdentity=run, consistency="sequence-range",
			environment=environment, sequence=measurement["sequence"], watch=None,
			products=[dict(index=0, source="gpu-timing", diagnosticTap=None, schema="gglab.gpu-timing",
				schemaVersion=1, metadata="timing.json", files=["timing.json"])])
		check(sequence_bundle)
		for key, value in (("bundleId", f"{run}-j3-a1"), ("schemaVersion", 1),
			("jobId", 3), ("attempt", 1), ("consistency", "same-frame")):
			bad = copy.deepcopy(sequence_bundle)
			bad[key] = value
			cases.append(bad)
		bad = copy.deepcopy(sequence_bundle)
		bad["sequence"]["lastFrameSerial"] += 1
		cases.append(bad)
		bad = copy.deepcopy(sequence_bundle)
		bad["environment"]["validation"]["effective"] = "off"
		cases.append(bad)
		for invalid in cases:
			try:
				check(invalid)
			except (ValueError, KeyError, ValidationError):
				pass
			else:
				raise AssertionError("Bundle validator accepted an invalid contract")
		(root / "image.png").write_bytes(b"bad")
		try:
			check(document)
		except ValueError:
			pass
		else:
			raise AssertionError("Bundle validator accepted a malformed PNG header")
		(root / "image.png").write_bytes(png)
		unavailable = dict(status="unavailable", reason="Fixture has no identity source.")
		owned_frame = dict(frame, adapterDriverIdentity="test", codeIdentity=unavailable,
			shaderIdentity=unavailable, parameterRevision=None, watch=None)
		submission = dict(fenceIndex=1, fenceGeneration=1, fenceValue=1, completed=True)
		pass_ = dict(index=0, name="Export", occurrence=1, executionOrder=0, encoder="Copy",
			sideEffect=True, culled=False, dependencies=[], dependents=[], accesses=[],
			preBarriers=[], postBarriers=[], devirtualize=[], destroy=[], observation=True)
		graph = dict(schema="gglab.render-graph", schemaVersion=2, mode="full", frameSerial=1,
			frame=owned_frame, submission=submission,
			observation=dict(graph=True, resourceExport=True, nonFiniteWatch=False),
			compile=dict(duplicatePassNames=[], passes=[pass_], resources=[], versions=[], dependencyEdges=[], diagnostics=[]),
			execution=dict(status="recorded", executedPassIndices=[0], allocations=[], resolvedBarriers=[],
				observationNodes=[dict(passIndex=0, kind="ResourceExport", resource="Values", resourceIndex=0,
					resourceNodeIndex=0, originalInsertionPosition=0)], gpuTiming=unavailable))
		array = np.array([[[[1.25], [-2.0]]]], dtype="<f4")
		np.save(root / "values.npy", array)
		target = dict(address=dict(resource="Values", at="final", occurrence=None, mip=0, slices=[0, 1], aspect="Color"),
			resourceNodeIndex=0, version=1, writerPassIndex=0, writerPass="Write", writerOccurrence=1, type="Texture",
			array="values.npy", physicalFormat="R32Float", viewFormat="R32Float", rawTypeless=False, dtype="<f4",
			shape=list(array.shape), byteCount=array.nbytes, encoding="raw", channelOrder="R", strideBytes=0,
			elementCount=0, fields=[], semantics=None, extendedLifetimes=[])
		resource = dict(schema="gglab.resource-export", schemaVersion=1, frameSerial=1, frame=owned_frame,
			submission=submission, observation=dict(graph=True, resourceExport=True, nonFiniteWatch=False),
			graph="paired.graph.json", targets=[target])
		(root / "paired.graph.json").write_text(json.dumps(graph), encoding="utf-8")
		(root / "group.resource-export.json").write_text(json.dumps(resource), encoding="utf-8")
		multi = copy.deepcopy(document)
		multi["products"] += [
			dict(index=1, source="graph", diagnosticTap=None, schema="gglab.render-graph", schemaVersion=2,
				metadata="paired.graph.json", files=["paired.graph.json"]),
			dict(index=2, source="resource-export", diagnosticTap=None, schema="gglab.resource-export", schemaVersion=1,
				metadata="group.resource-export.json", files=["group.resource-export.json", "values.npy"])]
		check(multi)
		rejections = len(cases) + 1
		for name, key, value in (("paired.graph.json", "submission", dict(submission, fenceValue=2)),
			("paired.graph.json", "frame", dict(owned_frame, camera=dict(position=[1, 2, 3]))),
			("group.resource-export.json", "graph", "missing.graph.json"),
			("group.resource-export.json", "graph", None),
			("group.resource-export.json", "targets", [dict(target, array="../outside.npy")]),
			("group.resource-export.json", "targets", [dict(target, byteCount=4)])):
			original = graph if name == "paired.graph.json" else resource
			invalid = copy.deepcopy(original)
			invalid[key] = value
			(root / name).write_text(json.dumps(invalid), encoding="utf-8")
			try:
				check(multi)
			except (ValueError, KeyError, ValidationError):
				pass
			else:
				raise AssertionError(f"Bundle accepted a mismatched required file: {key}")
			(root / name).write_text(json.dumps(original), encoding="utf-8")
			rejections += 1
		watch = dict(schema="gglab.nonfinite-watch", schemaVersion=2, configurationVersion=1, mode="report",
			coverage="submitted-range", instrumentation="available", claims=["production"], firstFrameSerial=1,
			lastFrameSerial=1, frames=1, completedFrames=1, complete=True, boundary="Unavailable", production="Failed",
			failure="", firstAnomalyFrameSerial=1, reason="NonFiniteReplaced", injection=None)
		watched = copy.deepcopy(document)
		watched["watch"] = watch
		(root / "image.json").write_text(json.dumps(dict(image, watch=watch)), encoding="utf-8")
		check(watched)
		for changes in (dict(complete=False), dict(firstFrameSerial=2, lastFrameSerial=2), dict(mode="fail")):
			bad_watch = dict(watch, **changes)
			(root / "image.json").write_text(json.dumps(dict(image, watch=bad_watch)), encoding="utf-8")
			try:
				check(dict(watched, watch=bad_watch))
			except (ValueError, ValidationError):
				pass
			else:
				raise AssertionError("Bundle accepted incomplete, wrong-frame or failed required coverage")
			rejections += 1
		# The owning coverage contract accepts NotApplicable when no failure exists.
		not_applicable = dict(watch, mode="fail", production="NotApplicable", firstAnomalyFrameSerial=None, reason="None")
		(root / "image.json").write_text(json.dumps(dict(image, watch=not_applicable)), encoding="utf-8")
		check(dict(watched, watch=not_applicable))
	print(f"Capture bundle self-test passed ({rejections} rejection cases)")


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("paths", nargs="*", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args()
	bundle_validator = validator("CaptureBundle.schema.json")
	if args.self_test:
		self_test(bundle_validator)
	if not args.paths and not args.self_test:
		parser.error("Provide manifests/capture directories or --self-test")
	files = set()
	for path in args.paths:
		found = set(path.rglob("*.capture-bundle.json")) if path.is_dir() else {path}
		if not found:
			parser.error(f"No final capture bundle manifests found in {path}")
		files.update(found)
	for path in sorted(files):
		validate(path, bundle_validator)
		print(f"PASS {path}")
	if files:
		print(f"Validated {len(files)} capture bundles; 0 failures")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
