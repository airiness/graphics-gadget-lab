"""Validate published resource groups and their NPY bytes (jsonschema and numpy)."""

import argparse
import copy
import json
import tempfile
from pathlib import Path

import numpy as np
from jsonschema import Draft202012Validator


def validate(path, validator):
	document = json.loads(path.read_text(encoding="utf-8"))
	validator.validate(document)
	if document["frameSerial"] != document["frame"]["frame"]["serial"]:
		raise ValueError("Frame serials disagree")
	if document["graph"] is not None and not document["observation"]["graph"]:
		raise ValueError("Graph filename and observation flag disagree")
	filenames = set()
	for target in document["targets"]:
		name = target["array"]
		if name in filenames or Path(name).name != name:
			raise ValueError("Each target needs a distinct local array filename")
		filenames.add(name)
		with (path.parent / name).open("rb") as stream:
			if np.lib.format.read_magic(stream) != (1, 0):
				raise ValueError("Only NPY v1 is supported")
			shape, fortran, dtype = np.lib.format.read_array_header_1_0(stream)
			if fortran or list(shape) != target["shape"]:
				raise ValueError("NPY shape/order disagrees with the sidecar")
			payload_start = stream.tell()
			if stream.seek(0, 2) - payload_start != target["byteCount"]:
				raise ValueError("NPY payload byte count disagrees with the sidecar")
		array = np.load(path.parent / name, allow_pickle=False, mmap_mode="r")
		if array.nbytes != target["byteCount"]:
			raise ValueError("NPY dtype/shape and byte count disagree")
		fields = target["fields"]
		if target["dtype"] == "structured":
			if target["type"] != "Buffer" or len(shape) != 1 or not fields or dtype.itemsize != target["strideBytes"]:
				raise ValueError("Structured dtype requires a registered buffer stride/layout")
			names = set()
			end = 0
			for field in fields:
				name = field["name"]
				expected = np.dtype(field["dtype"])
				if field["count"] != 1:
					expected = np.dtype((expected, (field["count"],)))
				if name in names or field["offset"] < end or name not in dtype.fields:
					raise ValueError("Buffer fields overlap, repeat or are absent from NPY")
				names.add(name)
				actual, offset = dtype.fields[name][:2]
				if actual != expected or offset != field["offset"]:
					raise ValueError("Buffer field type/offset disagrees with NPY")
				end = offset + expected.itemsize
		else:
			if fields or dtype.str != target["dtype"]:
				raise ValueError("NPY scalar dtype disagrees with the sidecar")
		if target["type"] == "Texture":
			if len(shape) != 4 or shape[0] != target["address"]["slices"][1] or fields:
				raise ValueError("Texture arrays require [slices, height, width, channels]")
		else:
			if shape[0] != target["elementCount"] or array.nbytes != target["elementCount"] * target["strideBytes"]:
				raise ValueError("Buffer element count/stride disagrees with NPY")
			if target["dtype"] != "structured" and (len(shape) != 2 or dtype.str != "<u4" or shape[1] * 4 != target["strideBytes"]):
				raise ValueError("Unregistered buffers require raw 32-bit words")
	if document["graph"] is not None:
		graph = json.loads((path.parent / document["graph"]).read_text(encoding="utf-8"))
		if graph["frameSerial"] != document["frameSerial"] or graph["submission"] != document["submission"]:
			raise ValueError("The paired graph belongs to a different submitted frame")
		if graph["schemaVersion"] != 2 or not graph["observation"]["resourceExport"]:
			raise ValueError("Paired graphs must include resource observation nodes")
	return document


def self_test(validator):
	with tempfile.TemporaryDirectory(prefix="gglab-resource-schema-") as directory:
		path = Path(directory) / "group.resource-export.json"
		array = np.array([[[[1.25], [-2.0]]]], dtype="<f4")
		np.save(path.parent / "target.npy", array)
		unavailable = dict(status="unavailable", reason="Fixture has no identity source.")
		target = dict(address=dict(resource="SceneColor", at="final", occurrence=None, mip=0, slices=[0, 1], aspect="Color"),
			resourceNodeIndex=1, version=1, writerPassIndex=0, writerPass="Write", writerOccurrence=1, type="Texture",
			array="target.npy", physicalFormat="R32Float", viewFormat="R32Float", rawTypeless=False, dtype="<f4",
			shape=list(array.shape), byteCount=array.nbytes, encoding="raw", channelOrder="R", strideBytes=0,
			elementCount=0, fields=[], semantics=None, extendedLifetimes=[])
		document = dict(schema="gglab.resource-export", schemaVersion=1, frameSerial=1,
			frame=dict(backend="dx12", content={}, frame=dict(serial=1, index=0), camera={}, time={}, readiness={}, temporal={},
				adapterDriverIdentity="test", codeIdentity=unavailable, shaderIdentity=unavailable, parameterRevision=None),
			submission=dict(fenceIndex=1, fenceGeneration=1, fenceValue=1, completed=True),
			observation=dict(graph=False, resourceExport=True, nonFiniteWatch=False), graph=None, targets=[target])
		path.write_text(json.dumps(document), encoding="utf-8")
		validate(path, validator)
		structured = copy.deepcopy(document)
		dtype = np.dtype(dict(names=["offset", "vector"], formats=["<u4", ("<f4", (2,))], offsets=[0, 8], itemsize=16))
		np.save(path.parent / "structured.npy", np.zeros(2, dtype=dtype))
		structured["targets"][0].update(type="Buffer", array="structured.npy", dtype="structured", shape=[2], byteCount=32,
			strideBytes=16, elementCount=2, fields=[dict(name="offset", offset=0, dtype="<u4", count=1),
				dict(name="vector", offset=8, dtype="<f4", count=2)])
		path.write_text(json.dumps(structured), encoding="utf-8")
		validate(path, validator)
		cases = [("shape", [1, 1, 1, 2]), ("dtype", "<f2"), ("byteCount", 4), ("array", "../target.npy"),
			("type", "Buffer"), ("rawTypeless", "true"), ("encoding", "linearized"), ("fields", [dict(name="a", offset=0, dtype="<f4", count=1)])]
		for key, value in cases:
			invalid = copy.deepcopy(document)
			invalid["targets"][0][key] = value
			path.write_text(json.dumps(invalid), encoding="utf-8")
			try:
				validate(path, validator)
			except Exception:
				pass
			else:
				raise AssertionError(f"Validator accepted invalid {key}")
	print(f"Resource schema/NPY self-test passed ({len(cases)} rejection cases)")


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("paths", nargs="*", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args()
	schema = json.loads((Path(__file__).resolve().parents[1] / "Docs/Schemas/ResourceExport.schema.json").read_text(encoding="utf-8"))
	Draft202012Validator.check_schema(schema)
	validator = Draft202012Validator(schema)
	if args.self_test:
		self_test(validator)
	if not args.paths and not args.self_test:
		parser.error("Provide exports or --self-test")
	files = set()
	for path in args.paths:
		found = {file for file in path.rglob("*.resource-export*.json") if not file.name.endswith(".graph.json")} if path.is_dir() else {path}
		if not found:
			parser.error(f"No resource exports found in {path}")
		files.update(found)
	failed = 0
	for path in sorted(files):
		try:
			validate(path, validator)
		except Exception as error:
			print(f"FAIL {path}: {error}")
			failed += 1
		else:
			print(f"PASS {path}")
	print(f"Validated {len(files)} resource groups; {failed} failures")
	return int(failed != 0)


if __name__ == "__main__":
	raise SystemExit(main())
