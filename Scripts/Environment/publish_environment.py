"""GGLab-owned Environment publisher and strict reference reader (Python 3.12+)."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import time
import uuid

VERSION = 1
KIND = "gglab.authoring-environment"
TARGETS = ["gglab-dx12", "gglab-vulkan13"]
ROLES = {
    "tool": "payload/gglab-shaderc.exe",
    "runtime": "payload/GraphicsGadgetLab.exe",
    "shaderSources": "payload/Shaders",
    "surfaceProfile1": "payload/Shaders/Profiles/GGLab.Surface/1/descriptor.json",
    "surfaceProfile2": "payload/Shaders/Profiles/GGLab.Surface/2/descriptor.json",
    "previewProgram": "payload/Shaders/Programs/ShaderGraphPreview/descriptor.json",
    "baseArtifacts": "payload/BaseArtifacts",
    "assets": "payload/Assets",
    "vulkanLayers": "payload/VulkanLayers",
}
STATE = {
    "shaderCache": "ShaderCache", "artifacts": "ShaderArtifacts",
    "generatedSources": "Generated", "previewPublications": "ShaderArtifacts/shader-preview",
    "previewSessions": "ShaderArtifacts/shader-preview-sessions",
    "observations": "ShaderArtifacts/shader-preview-sessions",
    "derivedData": "DerivedDataCache", "settings": "Settings", "logs": "Logs",
}
MAX_JSON = 16 * 1024 * 1024
MAX_FILES = 20000
HEX = re.compile(r"[0-9a-f]{64}\Z")


class ContractError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def require(condition, code, message):
    if not condition:
        raise ContractError(code, message)


def exact(value, fields):
    require(type(value) is dict and set(value) == set(fields), "invalid-shape", "Unexpected or missing fields")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "invalid-json", "Duplicate JSON key")
        result[key] = value
    return result


def parse(data):
    require(len(data) <= MAX_JSON, "limit-exceeded", "JSON exceeds 16 MiB")
    try:
        return json.loads(data.decode("utf-8"), object_pairs_hook=unique_object,
                          parse_constant=lambda _: require(False, "invalid-json", "Non-finite number"))
    except (UnicodeError, ValueError, RecursionError) as error:
        raise ContractError("invalid-json", str(error)) from error


def canonical(value):
    return json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("ascii")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def pe_imports(path):
    """Read PE32+ ordinary imports for app-local CRT closure; never select compiler policy."""
    data = path.read_bytes()
    try:
        require(data[:2] == b"MZ", "invalid-executable", str(path))
        pe = struct.unpack_from("<I", data, 60)[0]
        require(data[pe:pe + 4] == b"PE\0\0", "invalid-executable", str(path))
        machine, count = struct.unpack_from("<HH", data, pe + 4)
        require(machine == 0x8664, "invalid-executable", "Environment requires x64 PE binaries")
        optional_size = struct.unpack_from("<H", data, pe + 20)[0]
        optional = pe + 24
        require(struct.unpack_from("<H", data, optional)[0] == 0x20b, "invalid-executable", str(path))
        sections = optional + optional_size
        def offset(rva):
            for index in range(count):
                size, virtual, raw_size, raw = struct.unpack_from("<IIII", data, sections + index * 40 + 8)
                if virtual <= rva < virtual + max(size, raw_size):
                    return raw + rva - virtual
            raise ContractError("invalid-executable", "Unmapped PE RVA")
        rva = struct.unpack_from("<I", data, optional + 120)[0]
        if not rva:
            return []
        cursor, names = offset(rva), []
        for _ in range(4096):
            entry = struct.unpack_from("<IIIII", data, cursor)
            if not any(entry):
                return names
            name = offset(entry[3])
            end = data.index(b"\0", name, name + 256)
            names.append(data[name:end].decode("ascii").lower())
            cursor += 20
        raise ContractError("invalid-executable", "Unterminated imports")
    except (IndexError, ValueError, struct.error) as error:
        raise ContractError("invalid-executable", str(path)) from error


def bundle_crt(payload):
    pending = list(payload.glob("*.exe")) + list(payload.glob("*.dll"))
    scanned = set()
    while pending:
        binary = pending.pop()
        if binary.name.lower() in scanned:
            continue
        scanned.add(binary.name.lower())
        for name in pe_imports(binary):
            if re.fullmatch(r"(?:msvcp140[^/]*|vcruntime140[^/]*|concrt140[^/]*|ucrtbased)\.dll", name):
                target = payload / locator(name)
                if not target.exists():
                    source = plain_path(Path(os.environ["SystemRoot"]) / "System32" / name)
                    require(source.is_file(), "missing-dependency", name)
                    shutil.copyfile(source, target)
                pending.append(target)


def identity(manifest):
    body = {key: value for key, value in manifest.items() if key != "environmentId"}
    return "sha256:" + hashlib.sha256(b"GGLab.Environment.v1\n" + canonical(body)).hexdigest()


def locator(value):
    require(type(value) is str and 0 < len(value) <= 240, "invalid-path", "Invalid locator length")
    for part in value.split("/"):
        require(re.fullmatch(r"[A-Za-z0-9_.-]+", part) is not None and part not in (".", "..")
                and not part.endswith(".") and not re.fullmatch(r"(?i)(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\..*)?", part),
                "invalid-path", "Locator contains an unsafe component: " + value)
    return value


def is_reparse(path):
    return bool(path.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT) if os.name == "nt" else path.is_symlink()


def plain_path(path):
    path = Path(os.path.abspath(path))
    require(os.name != "nt" or not str(path).startswith("\\\\"), "invalid-path", "UNC/device paths are not supported")
    for item in (path, *path.parents):
        if item.exists() or item.is_symlink():
            require(not is_reparse(item), "reparse-point", "Reparse point is forbidden: " + str(item))
    # Resolve existing components (including Win32 8.3 names) only after rejecting
    # reparse ancestors. Nonexistent destinations retain their unresolved suffix.
    resolved = path.resolve(strict=False)
    for item in (resolved, *resolved.parents):
        if item.exists() or item.is_symlink():
            require(not is_reparse(item), "reparse-point", "Reparse point is forbidden: " + str(item))
    return resolved


def contained(path, root):
    return os.path.commonpath([str(path), str(root)]).casefold() == str(root).casefold()


def disjoint(a, b):
    try:
        return not contained(a, b) and not contained(b, a)
    except ValueError:
        return True


def inventory(root):
    plain_path(root)
    result = []
    seen = set()
    def visit(directory):
        for item in sorted(directory.iterdir()):
            name = locator(item.relative_to(root).as_posix())
            require(name.lower() not in seen, "path-conflict", name)
            seen.add(name.lower())
            require(not is_reparse(item), "reparse-point", str(item))
            if item.is_dir():
                visit(item)
            else:
                require(item.is_file(), "invalid-member", str(item))
                require(item.stat().st_nlink == 1, "hard-link", str(item))
                result.append(item)
                require(len(result) <= MAX_FILES, "limit-exceeded", "Too many members")
    visit(root)
    return result


def validate_manifest(m):
    require(type(m) is dict, "invalid-shape", "Manifest must be an object")
    require(type(m.get("manifestVersion")) is int and m["manifestVersion"] == VERSION,
            "unsupported-version", "Unsupported manifestVersion")
    exact(m, ["manifestVersion", "kind", "environmentId", "producer", "roles", "writableState", "members"])
    require(m["kind"] == KIND, "invalid-shape", "Unknown environment kind")
    exact(m["producer"], ["publisherVersion", "sourceRevision", "sourceDirty", "deployment", "publisherSha256"])
    p = m["producer"]
    require(p["publisherVersion"] == "1.0.0" and type(p["sourceDirty"]) is bool
            and type(p["sourceRevision"]) is str and re.fullmatch(r"[0-9a-f]{40}", p["sourceRevision"])
            and type(p["publisherSha256"]) is str and HEX.fullmatch(p["publisherSha256"]),
            "invalid-shape", "Invalid producer provenance")
    locator(p["deployment"])
    require(m["roles"] == ROLES and m["writableState"] == {"root": "external", "roles": STATE},
            "invalid-role", "Unsupported role layout")
    require(type(m["members"]) is list and 0 < len(m["members"]) <= MAX_FILES, "invalid-shape", "Invalid members")
    names = set()
    prior = ""
    for member in m["members"]:
        exact(member, ["path", "size", "sha256"])
        name = locator(member["path"])
        require(name.startswith("payload/"), "invalid-path", "Member outside payload")
        require(name.lower() not in names, "path-conflict", name)
        require(name > prior, "invalid-shape", "Members must be ASCII path sorted")
        prior = name
        names.add(name.lower())
        require(type(member["size"]) is int and 0 <= member["size"] <= 2**53 - 1
                and type(member["sha256"]) is str and HEX.fullmatch(member["sha256"]),
                "invalid-member", name)
    for name in names:
        require(not any(parent.as_posix().lower() in names for parent in Path(name).parents), "path-conflict", name)
    for role, path in ROLES.items():
        require(path.lower() in names or (role in ("shaderSources", "assets", "baseArtifacts", "vulkanLayers")
                and any(n.startswith(path.lower() + "/") for n in names)), "missing-member", path)
    for target in TARGETS:
        require(f"payload/baseartifacts/active/{target}/program-registry.ggsh.active" in names,
                "missing-member", "Missing base registry pointer")
    for name in ("dxcompiler.dll", "dxil.dll"):
        require("payload/" + name in names, "missing-member", name)
    for name in ("VkLayer_khronos_validation.dll", "VkLayer_khronos_validation.json"):
        require("payload/vulkanlayers/" + name.lower() in names, "missing-member", name)
    require(m["environmentId"] == identity(m), "identity-mismatch", "EnvironmentId does not match canonical manifest")
    return m


def is_staging_name(path):
    # Caller spelling cannot conceal a staging directory behind a short alias.
    return plain_path(path).name.lower().startswith(".staging-")


def verify(root, staging=False):
    root = plain_path(root)
    require(staging or not is_staging_name(root), "incomplete-publication", "Staging is not a usable Environment")
    plain_path(root / "environment.json")
    require((root / "environment.json").is_file(), "incomplete-publication", "Final manifest is absent")
    m = validate_manifest(parse((root / "environment.json").read_bytes()))
    actual = {p.relative_to(root).as_posix(): p for p in inventory(root)}
    expected = {v["path"] for v in m["members"]} | {"environment.json"}
    require(set(actual) == expected, "membership-mismatch", "Missing or unlisted immutable files")
    for member in m["members"]:
        path = actual[member["path"]]
        require(path.stat().st_size == member["size"] and digest(path) == member["sha256"], "hash-mismatch", member["path"])
    return m


def check_cancel(cancel):
    require(not cancel or not Path(cancel).exists(), "cancelled", "Publication cancelled before finalization")


def run_native(executable, args, cwd, cancel=None):
    # Files avoid stdout pipe deadlock; no shell, inherited checkout cwd or PATH dependency.
    with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
        process = subprocess.Popen([str(executable), *map(str, args)], cwd=cwd, stdout=output, stderr=errors,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            deadline = time.monotonic() + 600
            while process.poll() is None:
                check_cancel(cancel)
                require(time.monotonic() < deadline, "native-timeout", "Native operation exceeded 600 seconds")
                time.sleep(0.05)
            output.seek(0)
            errors.seek(0)
            data = output.read(MAX_JSON + 1)
            diagnostic = errors.read(4096).decode("utf-8", errors="replace")
            require(process.returncode == 0, "native-failed", diagnostic or data[:4096].decode("utf-8", errors="replace"))
            return data
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()


def copy_tree(source, destination):
    for path in inventory(source):
        target = destination / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        require(digest(path) == digest(target), "source-changed", str(path))


def deployment_inputs(repository, deployment):
    repository, deployment = plain_path(repository), plain_path(deployment)
    require(contained(deployment, repository) and deployment != repository, "invalid-path", "Deployment must be beneath repository")
    for filename in ("GraphicsGadgetLab.exe", "gglab-shaderc.exe", "dxcompiler.dll", "dxil.dll"):
        p = deployment / filename
        require(p.is_file(), "missing-member", str(p))
        plain_path(p)
    # Only the two known development junctions are accepted, and only to their canonical repository resource roots.
    for name in ("Shaders", "Assets"):
        resource = deployment / name
        expected = plain_path(repository / name)
        require(resource.resolve() == expected.resolve(), "source-external-link", str(resource))
        inventory(expected)
    require(not (deployment / "artifact-only-package.json").exists(), "ineligible-deployment", "Artifact-only runtime is not an authoring deployment")
    return repository, deployment


def discover(repository, roots):
    repository = plain_path(repository)
    candidates = []
    visited = 0
    # Bounded host-owned search: caller supplies roots, default is Build/Output; maximum depth 4.
    for root in roots:
        search = plain_path(repository / locator(root))
        require(contained(search, repository), "invalid-path", str(search))
        if not search.exists():
            continue
        def visit(folder, depth):
            nonlocal visited
            visited += 1
            require(visited <= 10000, "limit-exceeded", "Discovery exceeded 10000 directories")
            if (folder / "gglab-shaderc.exe").is_file():
                try:
                    deployment_inputs(repository, folder)
                    candidates.append({"deployment": folder.relative_to(repository).as_posix(),
                                       "toolSha256": digest(folder / "gglab-shaderc.exe"),
                                       "runtimeSha256": digest(folder / "GraphicsGadgetLab.exe")})
                except ContractError:
                    pass
                return
            if depth < 4:
                for child in sorted(folder.iterdir()):
                    if child.is_dir() and not is_reparse(child):
                        visit(child, depth + 1)
        visit(search, 0)
    return sorted({c["deployment"]: c for c in candidates}.values(), key=lambda c: c["deployment"])


def provenance(repository, deployment):
    def git(*args):
        result = subprocess.run(["git", "-C", str(repository), *args], capture_output=True, check=False)
        require(result.returncode == 0, "provenance-unavailable", result.stderr.decode(errors="replace"))
        return result.stdout.decode("utf-8").strip()
    return {"publisherVersion": "1.0.0", "publisherSha256": digest(Path(__file__)),
            "sourceRevision": git("rev-parse", "HEAD"), "sourceDirty": bool(git("status", "--porcelain")),
            "deployment": deployment.relative_to(repository).as_posix()}


def publish(repository, deployment, destination, cancel=None):
    destination = plain_path(destination)
    require(not is_staging_name(destination), "invalid-path", "Destination uses a reserved staging name")
    repository, deployment = deployment_inputs(repository, deployment)
    require(disjoint(destination, deployment) and disjoint(destination, repository / "Shaders")
            and disjoint(destination, repository / "Assets"), "invalid-path", "Destination overlaps inputs")
    check_cancel(cancel)
    producer = provenance(repository, deployment)
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = destination.parent / (".staging-" + uuid.uuid4().hex)
    stage.mkdir()
    # Never delete a failed stage: process termination can leave a child writing it. It is never discoverable as finalized.
    payload = stage / "payload"
    payload.mkdir()
    for source in sorted(deployment.iterdir()):
        if source.name in ("GraphicsGadgetLab.exe", "gglab-shaderc.exe") or source.suffix.lower() == ".dll":
            plain_path(source)
            require(source.is_file() and source.stat().st_nlink == 1, "invalid-member", str(source))
            shutil.copyfile(source, payload / source.name)
            require(digest(source) == digest(payload / source.name), "source-changed", str(source))
    if (deployment / "D3D12").exists():
        copy_tree(deployment / "D3D12", payload / "D3D12")
    bundle_crt(payload)
    sdk = plain_path(Path(os.environ.get("VULKAN_SDK", "C:/VulkanSDK/1.3.296.0")))
    layers = payload / "VulkanLayers"
    layers.mkdir()
    for name in ("VkLayer_khronos_validation.dll", "VkLayer_khronos_validation.json"):
        source = plain_path(sdk / "Bin" / name)
        require(source.is_file(), "missing-dependency", str(source))
        shutil.copyfile(source, layers / name)
    layer_manifest = parse((layers / "VkLayer_khronos_validation.json").read_bytes())
    require(layer_manifest.get("layer", {}).get("library_path") in (".\\VkLayer_khronos_validation.dll", "./VkLayer_khronos_validation.dll"),
            "source-external-link", "Validation layer library must have a local relative locator")
    copy_tree(Path(__file__).parent / "resources", payload / "Licenses")
    copy_tree(repository / "Shaders", payload / "Shaders")
    # Preview uses procedural geometry/textures. Only the desktop environment catalog is required from Assets.
    copy_tree(repository / "Assets/Textures/Skybox", payload / "Assets/Textures/Skybox")
    for name in ("UVTest1K.png", "UVTest4K.png"):
        source = plain_path(repository / "Assets/Textures" / name)
        require(source.is_file(), "missing-member", str(source))
        shutil.copyfile(source, payload / "Assets/Textures" / name)
    shutil.copyfile(repository / "LICENSE", payload / "LICENSE")
    work = stage / "work"
    work.mkdir()
    tool = payload / "gglab-shaderc.exe"
    for command in ("describe", "describe-preview"):
        parse(run_native(tool, [command], work, cancel))
    # Old deployments must explicitly fail the new Runtime path capability probe.
    run_native(payload / "GraphicsGadgetLab.exe", ["--state-root", str(work), "--self-test", "app-host-configuration"], work, cancel)
    for target in TARGETS:
        run_native(tool, ["build-runtime", "--source-root", payload / "Shaders", "--target", target,
                         "--cache-root", work / "ShaderCache", "--artifact-root", payload / "BaseArtifacts",
                         "--result-format", "json"], work, cancel)
    # Work is moved outside the immutable stage; it remains diagnostic/retry debris, never a contract role.
    work.rename(destination.parent / (".work-" + stage.name[9:]))
    members = [{"path": p.relative_to(stage).as_posix(), "size": p.stat().st_size, "sha256": digest(p)}
               for p in inventory(stage)]
    m = {"manifestVersion": VERSION, "kind": KIND, "producer": producer, "roles": ROLES,
         "writableState": {"root": "external", "roles": STATE}, "members": sorted(members, key=lambda v: v["path"])}
    m["environmentId"] = identity(m)
    validate_manifest(m)
    with (stage / "environment.json").open("xb") as stream:
        stream.write(canonical(m) + b"\n")
        stream.flush()
        os.fsync(stream.fileno())
    verify(stage, staging=True)
    check_cancel(cancel)
    try:
        # Windows rename is a same-volume atomic, no-replace directory operation.
        require(os.name == "nt", "unsupported-platform", "Publication requires Windows local filesystem")
        stage.rename(destination)
        outcome = "published"
    except FileExistsError:
        existing = verify(destination)
        require(existing["environmentId"] == m["environmentId"], "destination-conflict", "Existing Environment differs; select a new destination")
        outcome = "already-present"
    verify(destination)
    return {"outcome": outcome, "environmentRoot": str(destination), "environmentId": m["environmentId"]}


def init_state(environment, state):
    environment, state = plain_path(environment), plain_path(state)
    require(disjoint(environment, state), "invalid-path", "State and Environment must be disjoint")
    m = verify(environment)
    require(not state.exists(), "state-exists", "State initialization never overwrites existing state")
    state.parent.mkdir(parents=True, exist_ok=True)
    staging = state.parent / (".staging-state-" + uuid.uuid4().hex)
    staging.mkdir()
    copy_tree(environment / ROLES["baseArtifacts"], staging / STATE["artifacts"])
    for path in STATE.values():
        (staging / path).mkdir(parents=True, exist_ok=True)
    (staging / "state.json").write_bytes(canonical({"stateVersion": 1, "environmentId": m["environmentId"]}) + b"\n")
    staging.rename(state)
    return {"environmentId": m["environmentId"], "stateRoot": str(state)}


def dispatch(request):
    require(type(request) is dict, "invalid-shape", "Request must be an object")
    require(type(request.get("requestVersion")) is int and request["requestVersion"] == VERSION,
            "unsupported-version", "Unsupported requestVersion")
    operation = request.get("operation")
    common = ["requestVersion", "operation"]
    for key in ("repositoryRoot", "destination", "environmentRoot", "stateRoot"):
        if key in request:
            require(type(request[key]) is str and Path(request[key]).is_absolute(), "invalid-path", key + " must be absolute")
    if operation == "discover":
        exact(request, common + ["repositoryRoot", "searchRoots"])
        require(type(request["searchRoots"]) is list and len(request["searchRoots"]) <= 32, "invalid-shape", "Invalid search roots")
        return {"candidates": discover(Path(request["repositoryRoot"]), request["searchRoots"])}
    if operation == "publish":
        exact(request, common + ["repositoryRoot", "deployment", "destination", "cancelFile"])
        repo = Path(request["repositoryRoot"])
        require(request["cancelFile"] is None or (type(request["cancelFile"]) is str and Path(request["cancelFile"]).is_absolute()),
                "invalid-path", "cancelFile must be null or absolute")
        return publish(repo, repo / locator(request["deployment"]), Path(request["destination"]), request["cancelFile"])
    if operation == "verify":
        exact(request, common + ["environmentRoot"])
        m = verify(Path(request["environmentRoot"]))
        return {"environmentId": m["environmentId"], "roles": m["roles"], "writableState": m["writableState"]}
    if operation == "init-state":
        exact(request, common + ["environmentRoot", "stateRoot"])
        return init_state(Path(request["environmentRoot"]), Path(request["stateRoot"]))
    raise ContractError("unsupported-operation", "Unknown operation")


def main():
    operation = None
    try:
        require(len(sys.argv) == 1, "invalid-request", "Send one UTF-8 JSON request on stdin; no arguments")
        request = parse(sys.stdin.buffer.read(MAX_JSON + 1))
        operation = request.get("operation") if type(request) is dict else None
        result = dispatch(request)
        response = {"resultVersion": VERSION, "operation": operation, "success": True, "result": result, "error": None}
        code = 0
    except (ContractError, OSError, ValueError, TypeError, KeyError) as error:
        response = {"resultVersion": VERSION, "operation": operation, "success": False, "result": None,
                    "error": {"code": getattr(error, "code", "io-error"), "message": str(error)}}
        code = 2
    except KeyboardInterrupt:
        response = {"resultVersion": VERSION, "operation": operation, "success": False, "result": None,
                    "error": {"code": "cancelled", "message": "Interrupted; verify destination before retry"}}
        code = 2
    sys.stdout.buffer.write(canonical(response) + b"\n")
    return code


if __name__ == "__main__":
    sys.exit(main())
