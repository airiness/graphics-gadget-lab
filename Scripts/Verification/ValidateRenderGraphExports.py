"""Validate published graph exports (requires the Python jsonschema package)."""

import argparse
import copy
import json
from pathlib import Path

from jsonschema import Draft202012Validator, ValidationError

import ValidateParameterFrames as parameters

SCAN_KINDS = {"NonFiniteScan", "ProductionCounters"}


def validate_allocations(document):
	"""Recompute v5 frame totals from the frozen allocation records."""
	resources = document["compile"]["resources"]
	execution = document["execution"]
	for resource in resources:
		if resource["type"] == "Buffer" and resource["logicalBytes"] != resource["buffer"]["sizeBytes"]:
			raise ValueError("A buffer's logical size is its byte size")
	counted = set()
	sizes = []
	logical = unavailable_logical = allocation_bytes = unavailable = unique = 0
	for entry in execution["allocations"]:
		resource = resources[entry["resourceIndex"]]
		if entry["imported"] != resource["imported"]:
			raise ValueError("Allocation and resource disagree on import ownership")
		if entry["resourceIndex"] not in counted:
			counted.add(entry["resourceIndex"])
			logical += resource["logicalBytes"] or 0
			unavailable_logical += resource["logicalBytes"] is None
		if entry["allocationFacts"] != "known":
			unavailable += 1
			if entry["unavailableReason"] == "external" and not entry["imported"]:
				raise ValueError("Only imported resources can adopt external native objects")
			continue
		allocation_bytes += entry["allocatedBytes"]
		ordinal = entry["nativeAllocation"]
		if ordinal == len(sizes):
			sizes.append(entry["allocatedBytes"])
			unique += entry["allocatedBytes"]
		elif ordinal > len(sizes):
			raise ValueError("Native allocation ordinals must be numbered in first-use order")
		elif sizes[ordinal] != entry["allocatedBytes"]:
			raise ValueError("Records sharing a native allocation disagree on its size")
	totals = execution["totals"]
	expected = dict(logicalBytes=logical, unavailableLogicalResources=unavailable_logical,
		allocationBytes=allocation_bytes, unavailableAllocations=unavailable,
		uniquePhysicalAllocationBytes=unique)
	if any(totals[key] != value for key, value in expected.items()):
		raise ValueError("Frame totals disagree with the allocation records")
	blocks, allocator = totals["allocatorBlockBytes"], totals["allocatorAllocationBytes"]
	if (blocks is None) != (allocator is None) or (allocator is not None and allocator > blocks):
		raise ValueError("Allocator totals must be known together and fit in their blocks")
	if allocator is not None and unique > allocator:
		raise ValueError("Unique physical allocation bytes exceed the allocator's allocation bytes")


def validate(document, validator):
	parameters.validate_export(document["frame"], document["schemaVersion"], 6, document["frameSerial"])
	validator.validate(document)
	passes = document["compile"]["passes"]
	if document["frameSerial"] != document["frame"]["frame"]["serial"]:
		raise ValueError("Frame serials disagree")
	flags = document["observation"]
	modern = document["schemaVersion"] >= 5
	if modern:
		# Capabilities come from the listed node kinds, never from the version number.
		kinds = flags["nodeKinds"]
		if kinds != sorted(set(kinds)):
			raise ValueError("Observation node kinds must be sorted and unique")
		if flags["resourceExport"] != ("ResourceExport" in kinds) or flags["nonFiniteWatch"] != bool(SCAN_KINDS & set(kinds)):
			raise ValueError("Observation node kinds and activity flags disagree")
		observed = bool(kinds)
	else:
		observed = document["schemaVersion"] != 1
	if observed != (flags["resourceExport"] or flags["nonFiniteWatch"]):
		raise ValueError("Graph schema version and observation flags disagree")
	if document["mode"] == "full":
		markers = {entry["index"] for entry in passes if entry.get("observation", False)}
		observers = document["execution"]["observationNodes"]
		nodes = {entry["passIndex"] for entry in observers}
		if flags["resourceExport"] != any(entry["kind"] == "ResourceExport" for entry in observers) or flags["nonFiniteWatch"] != any(entry["kind"] in SCAN_KINDS for entry in observers):
			raise ValueError("Observation kinds and activity flags disagree")
		if markers != nodes or len(nodes) != len(observers) or bool(markers) != observed:
			raise ValueError("Observation pass markers and execution nodes disagree")
		if modern:
			if sorted({entry["kind"] for entry in observers}) != flags["nodeKinds"]:
				raise ValueError("Observation node kinds disagree with execution nodes")
			validate_allocations(document)
	if document["mode"] == "summary":
		orders = [entry["executionOrder"] for entry in passes]
		if orders != list(range(len(passes))):
			raise ValueError("Summary passes must be in contiguous execution order")
		return

	versions = {node["index"]: node for node in document["compile"]["versions"]}
	for entry in passes:
		for access in entry["accesses"]:
			node = versions[access["resourceNodeIndex"]]
			if node["version"] != access["version"] or node["resourceIndex"] != access["resourceIndex"]:
				raise ValueError("Access disagrees with its resource version")
			if access["dependencyAccess"] == "Read":
				continue
			written = versions[access["writtenResourceNodeIndex"]]
			if (written["writer"] != entry["index"] or written["version"] != access["writtenVersion"]
				or written["resourceIndex"] != access["resourceIndex"]):
				raise ValueError("Written version disagrees with its writer or resource")
			if access["dependencyAccess"] == "ReadWrite":
				if written["previous"] != node["index"] or written["version"] != node["version"] + 1:
					raise ValueError("ReadWrite must consume the previous version and produce its successor")


def self_test(validator):
	# Pin the format independently of the C++ UI text tables. Runtime tests cover
	# the producer; these cases ensure schema/validator edits cannot admit old labels.
	state = dict(stages="ComputeShader", stageBits=16, access="UnorderedAccess",
		accessBits=32, layout="UnorderedAccess")
	range_ = dict(baseMip=0, mipCount="remaining", baseSlice=0, sliceCount="remaining", aspects="Color")
	access = dict(resourceNodeIndex=0, resourceSlot=0, version=1,
		writtenResourceNodeIndex=1, writtenVersion=2, resourceIndex=0, resource="SceneColor",
		type="Texture", dependencyAccess="ReadWrite", ordering="Ordered", access="StorageReadWrite",
		accessBits=7, stages="ComputeShader", stageBits=16, subresources=range_)
	barrier = dict(resourceIndex=0, resource="SceneColor", type="Texture", kind="Uav",
		reason="OrderedStorageHazard", before=state, after=state, subresources=range_)
	pass_ = dict(index=0, name="Repeated", occurrence=2, executionOrder=0, encoder="Compute",
		sideEffect=True, culled=False, dependencies=[], dependents=[], accesses=[access],
		preBarriers=[barrier], postBarriers=[], devirtualize=[0], destroy=[0])
	resource = dict(index=0, name="SceneColor", type="Texture", imported=True, referenceCount=1,
		firstUser=0, lastUser=0, usageBits=8, usage="UnorderedAccess", initialState=state,
		finalState=None, finalSubresources=None, exportPass=-1,
		texture=dict(format="R8G8B8A8Unorm", extent=[4, 4, 1], mips=1, slices=1, samples=1))
	unavailable = dict(status="unavailable", reason="No identity-preserving timing source.")
	full = dict(schema="gglab.render-graph", schemaVersion=1, mode="full", frameSerial=1,
		frame=dict(backend="dx12", content={}, frame=dict(serial=1, index=0), camera={}, time={},
			readiness={}, temporal={}, adapterDriverIdentity="test", codeIdentity=unavailable,
			shaderIdentity=unavailable, parameterRevision=None),
		submission=dict(fenceIndex=1, fenceGeneration=1, fenceValue=1, completed=True),
		observation=dict(graph=True, resourceExport=False, nonFiniteWatch=False),
		compile=dict(duplicatePassNames=["Repeated"], passes=[pass_], resources=[resource],
			versions=[dict(index=0, resourceSlot=0, version=1, resourceIndex=0, resource="SceneColor",
				type="Texture", writer=-1, previous=-1, readers=[0]),
				dict(index=1, resourceSlot=0, version=2, resourceIndex=0, resource="SceneColor",
					type="Texture", writer=0, previous=0, readers=[])],
			dependencyEdges=[dict(from_=0, to=1, resourceNodeIndex=1, reason="WriterToReader")], diagnostics=[]),
		execution=dict(status="recorded", executedPassIndices=[0], allocations=[],
			resolvedBarriers=[dict(barrier, physical=None, passIndex=0, phase="pre")],
			observationNodes=[], gpuTiming=unavailable))
	# 'from' is a Python keyword, but is the actual schema field.
	full["compile"]["dependencyEdges"][0]["from"] = full["compile"]["dependencyEdges"][0].pop("from_")
	validate(full, validator)
	summary = copy.deepcopy(full)
	summary["mode"] = "summary"
	del summary["execution"]
	summary["compile"] = dict(duplicatePassNames=["Repeated"], passes=[dict(index=0,
		name="Repeated", occurrence=2, executionOrder=0,
		reads=[dict(version=1, resourceNodeIndex=0, subresources=range_,
			address=dict(resource="SceneColor", at=dict(before="Repeated"), occurrence=2))],
		writes=[dict(version=2, resourceNodeIndex=1, subresources=range_,
			address=dict(resource="SceneColor", at=dict(after="Repeated"), occurrence=2))])])
	validate(summary, validator)

	buffer = copy.deepcopy(full)
	buffer["compile"]["resources"][0].update(type="Buffer", buffer=dict(sizeBytes=65536, strideBytes=32))
	del buffer["compile"]["resources"][0]["texture"]
	validate(buffer, validator)

	observed = copy.deepcopy(full)
	observed["schemaVersion"] = 2
	observed["observation"]["resourceExport"] = True
	observed["compile"]["passes"][0]["observation"] = True
	observed["execution"]["observationNodes"] = [dict(passIndex=0, kind="ResourceExport", resource="SceneColor",
		resourceIndex=0, resourceNodeIndex=1, originalInsertionPosition=1)]
	observed["compile"]["dependencyEdges"][0]["reason"] = "ObservationOrder"
	validate(observed, validator)
	scanned = copy.deepcopy(observed)
	scanned["schemaVersion"] = 3
	scanned["observation"].update(resourceExport=False, nonFiniteWatch=True)
	scanned["execution"]["observationNodes"][0]["kind"] = "NonFiniteScan"
	validate(scanned, validator)
	mixed = copy.deepcopy(scanned)
	mixed["observation"]["resourceExport"] = True
	mixed["execution"]["observationNodes"].append(dict(mixed["execution"]["observationNodes"][0], passIndex=1, kind="ResourceExport"))
	mixed["compile"]["passes"].append(dict(mixed["compile"]["passes"][0], index=1, executionOrder=1, accesses=[]))
	validate(mixed, validator)

	counters = copy.deepcopy(scanned)
	counters["schemaVersion"] = 4
	counters["execution"]["observationNodes"][0]["kind"] = "ProductionCounters"
	validate(counters, validator)

	# v5 separates the content version from observation variants and freezes allocator facts.
	def revise(fixture, kinds):
		revised = copy.deepcopy(fixture)
		revised["schemaVersion"] = 5
		revised["observation"]["nodeKinds"] = kinds
		if revised["mode"] == "full":
			for item in revised["compile"]["resources"]:
				item.update(logicalBytes=64 if item["type"] == "Texture" else item["buffer"]["sizeBytes"],
					logicalBytesReason=None)
				if item["type"] == "Texture":
					item["texture"]["dimension"] = "Texture2D"
			revised["execution"]["allocations"] = [dict(resourceIndex=0, passIndex=0, imported=True,
				poolSlot=None, physical=dict(index=1, generation=1), allocationFacts="known",
				unavailableReason=None, allocatedBytes=65536, alignmentBytes=65536,
				memoryClass="device-local", nativeAllocation=0, offsetBytes=0)]
			revised["execution"]["totals"] = dict(logicalBytes=64, unavailableLogicalResources=0,
				allocationBytes=65536, unavailableAllocations=0, uniquePhysicalAllocationBytes=65536,
				allocatorBlockBytes=1 << 21, allocatorAllocationBytes=1 << 20)
		return revised

	full5 = revise(full, [])
	validate(full5, validator)
	summary5 = revise(summary, [])
	validate(summary5, validator)
	observed5 = revise(observed, ["ResourceExport"])
	validate(observed5, validator)
	mixed5 = revise(mixed, ["NonFiniteScan", "ResourceExport"])
	validate(mixed5, validator)
	counters5 = revise(counters, ["ProductionCounters"])
	validate(counters5, validator)
	buffer5 = revise(buffer, [])
	buffer5["compile"]["resources"][0]["logicalBytes"] = 65536
	buffer5["execution"]["totals"]["logicalBytes"] = 65536
	validate(buffer5, validator)
	external5 = copy.deepcopy(full5)
	external5["execution"]["allocations"][0].update(allocationFacts="unavailable",
		unavailableReason="external", allocatedBytes=None, alignmentBytes=None, memoryClass=None,
		nativeAllocation=None, offsetBytes=None)
	external5["execution"]["totals"].update(allocationBytes=0, unavailableAllocations=1,
		uniquePhysicalAllocationBytes=0, allocatorBlockBytes=None, allocatorAllocationBytes=None)
	validate(external5, validator)
	shared5 = copy.deepcopy(full5)
	shared5["execution"]["allocations"].append(dict(shared5["execution"]["allocations"][0], passIndex=0))
	shared5["execution"]["totals"].update(allocationBytes=131072)
	validate(shared5, validator)
	unsized5 = copy.deepcopy(full5)
	unsized5["compile"]["resources"][0].update(logicalBytes=None, logicalBytesReason="unsupported-format")
	unsized5["execution"]["totals"].update(logicalBytes=0, unavailableLogicalResources=1)
	validate(unsized5, validator)

	cases = [
		(counters, ("schemaVersion",), 3),
		(counters, ("observation", "nonFiniteWatch"), False),
		(scanned, ("schemaVersion",), 2),
		(scanned, ("observation", "resourceExport"), True),
		(scanned, ("observation", "nonFiniteWatch"), False),
		(mixed, ("observation", "resourceExport"), False),
		(mixed, ("execution", "observationNodes", 1, "passIndex"), 0),
		(observed, ("execution", "observationNodes", 0, "kind"), "NonFiniteScan"),
		(full, ("compile", "dependencyEdges", 0, "reason"), "ObservationOrder"),
		(observed, ("schemaVersion",), 1),
		(observed, ("compile", "passes", 0, "observation"), False),
		(observed, ("execution", "observationNodes", 0, "kind"), "Unknown"),
		(full, ("compile", "dependencyEdges", 0, "reason"), "Writer -> Reader"),
		(full, ("compile", "passes", 0, "accesses", 0, "access"), "DSV Write"),
		(full, ("compile", "passes", 0, "accesses", 0, "access"), "CopySrc"),
		(full, ("compile", "passes", 0, "preBarriers", 0, "reason"), "Access Transition"),
		(full, ("compile", "passes", 0, "preBarriers", 0, "kind"), "UAV"),
		(full, ("compile", "passes", 0, "accesses", 0, "subresources", "mipCount"), 4294967295),
		(full, ("compile", "passes", 0, "accesses", 0, "writtenVersion"), 1),
		(full, ("compile", "versions", 1, "writer"), 9),
		(full, ("execution", "status"), "unavailable"),
		(full, ("frameSerial",), 2),
		(buffer, ("compile", "resources", 0, "buffer", "strideBytes"), "32"),
		(summary, ("compile", "passes", 0, "executionOrder"), -1),
		(full, ("observation", "nodeKinds"), []),
		(observed, ("schemaVersion",), 5),
		(full5, ("observation", "nodeKinds"), ["ResourceExport"]),
		(observed5, ("observation", "nodeKinds"), []),
		(counters5, ("observation", "nodeKinds"), ["NonFiniteScan"]),
		(mixed5, ("observation", "nodeKinds"), ["ResourceExport", "NonFiniteScan"]),
		(mixed5, ("observation", "nodeKinds"), ["NonFiniteScan", "NonFiniteScan", "ResourceExport"]),
		(summary5, ("observation", "nodeKinds"), None),
		(summary5, ("compile", "resources"), []),
		(full5, ("schemaVersion",), 6),
		(full5, ("execution", "totals"), None),
		(full5, ("execution", "totals", "uniquePhysicalAllocationBytes"), 1),
		(full5, ("execution", "totals", "logicalBytes"), 65),
		(full5, ("execution", "totals", "allocatorAllocationBytes"), 1),
		(full5, ("execution", "totals", "allocatorBlockBytes"), None),
		(full5, ("execution", "allocations", 0, "nativeAllocation"), 1),
		(full5, ("execution", "allocations", 0, "allocatedBytes"), None),
		(full5, ("execution", "allocations", 0, "unavailableReason"), "external"),
		(full5, ("execution", "allocations", 0, "memoryClass"), "host"),
		(full5, ("execution", "allocations", 0, "imported"), False),
		(shared5, ("execution", "allocations", 1, "allocatedBytes"), 4096),
		(external5, ("execution", "allocations", 0, "allocatedBytes"), 4096),
		(external5, ("execution", "allocations", 0, "unavailableReason"), None),
		(external5, ("execution", "allocations", 0, "unavailableReason"), "evicted"),
		(full5, ("compile", "resources", 0, "logicalBytesReason"), "unsupported-format"),
		(unsized5, ("compile", "resources", 0, "logicalBytesReason"), None),
		(full5, ("compile", "resources", 0, "texture", "dimension"), "Cube"),
		(buffer5, ("compile", "resources", 0, "logicalBytes"), 64),
	]
	for fixture, path, value in cases:
		invalid = copy.deepcopy(fixture)
		target = invalid
		for key in path[:-1]:
			target = target[key]
		target[path[-1]] = value
		try:
			validate(invalid, validator)
		except (ValueError, KeyError, ValidationError):
			pass
		else:
			raise AssertionError(f"Validator accepted invalid field {path}")
	print(f"Graph schema validator self-test passed ({len(cases)} rejection cases)")


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("paths", nargs="*", type=Path, help="JSON files or capture directories")
	parser.add_argument("--self-test", action="store_true", help="Check schema acceptance/rejection contracts")
	args = parser.parse_args()
	schema_path = Path(__file__).resolve().parents[2] / "Schemas/RenderGraph.schema.json"
	schema = json.loads(schema_path.read_text(encoding="utf-8"))
	Draft202012Validator.check_schema(schema)
	validator = Draft202012Validator(schema)
	if args.self_test:
		self_test(validator)
	if not args.paths:
		if args.self_test:
			return 0
		parser.error("Provide exports or --self-test")
	files = set()
	for path in args.paths:
		if path.is_dir():
			found = set(path.rglob("*.graph*.json"))
			if not found:
				parser.error(f"No graph exports found in {path}")
			files.update(found)
		else:
			files.add(path)
	failed = 0
	for path in sorted(files):
		try:
			validate(json.loads(path.read_text(encoding="utf-8")), validator)
		except Exception as error:
			print(f"FAIL {path}: {error}")
			failed += 1
		else:
			print(f"PASS {path}")
	print(f"Validated {len(files)} graph exports; {failed} failures")
	return int(failed != 0)


if __name__ == "__main__":
	raise SystemExit(main())
