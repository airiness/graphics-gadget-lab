"""Publish from a disposable source mirror, hide that mirror, qualify final native paths.

Only the newly created mirror is renamed. Original repositories are never moved,
modified or denied access. This test proves loss of the publication input paths;
it is not a system-wide sandbox or an assertion about arbitrary malicious binaries.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("publisher", ROOT / "Scripts/Environment/publish_environment.py")
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def observation(path):
    data = path.read_bytes()
    assert len(data) == 90 and data[:8] == b"GGSHOBSV" and struct.unpack_from("<II", data, 8) == (1, 1)
    return {"attempt": struct.unpack_from("<Q", data, 16)[0], "observed": data[24:56].hex(),
            "loaded": data[56:88].hex(), "status": data[88], "rejection": data[89]}


def run(exe, args, work, name, env):
    result = subprocess.run([str(exe), *map(str, args)], cwd=work, env=env, capture_output=True, timeout=600,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    (work / (name + ".stdout.log")).write_bytes(result.stdout)
    (work / (name + ".stderr.log")).write_bytes(result.stderr)
    return result


def wait_observed(process, path, sequence):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        assert process.poll() is None, "Runtime exited before observation"
        if path.exists():
            value = observation(path)
            if value["attempt"] == sequence:
                assert value["status"] == 1 and value["rejection"] == 0 and value["observed"] == value["loaded"]
                return value
        time.sleep(0.1)
    raise TimeoutError("Runtime did not publish the expected loaded observation")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--deployment", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    assert not output.exists(), "Choose a new qualification directory; evidence is never overwritten"
    output.mkdir(parents=True)
    report = {"evidenceVersion": 1, "sourceIsolation": "disposable-publication-source-mirror-renamed",
              "originalCheckoutAccessDenied": False, "native": []}
    try:
        mirror = output / "source"
        subprocess.run(["git", "clone", "--local", "--no-hardlinks", "--no-checkout", str(ROOT), str(mirror)], check=True, capture_output=True)
        for name in ("LICENSE", ".gitignore"):
            shutil.copyfile(ROOT / name, mirror / name)
        shutil.copytree(ROOT / "Shaders", mirror / "Shaders")
        shutil.copytree(ROOT / "Assets/Textures/Skybox", mirror / "Assets/Textures/Skybox")
        for name in ("UVTest1K.png", "UVTest4K.png"):
            shutil.copyfile(ROOT / "Assets/Textures" / name, mirror / "Assets/Textures" / name)
        deployment = mirror / "Build/Output/x64/Selected"
        deployment.mkdir(parents=True)
        for item in args.deployment.resolve().iterdir():
            if item.name in ("gglab-shaderc.exe", "GraphicsGadgetLab.exe") or item.suffix.lower() == ".dll":
                shutil.copyfile(item, deployment / item.name)
        if (args.deployment / "D3D12").exists():
            shutil.copytree(args.deployment / "D3D12", deployment / "D3D12")
        for name in ("Shaders", "Assets"):
            subprocess.run(["cmd", "/c", "mklink", "/J", str(deployment / name), str(mirror / name)], check=True, capture_output=True)
        environment = output / "environment"
        report["publication"] = p.publish(mirror, deployment, environment)
        report["duplicatePublication"] = p.publish(mirror, deployment, environment)
        assert report["duplicatePublication"]["outcome"] == "already-present"
        # Validate the fully resolved rename target before moving this test-owned source tree.
        assert mirror.parent == output and mirror.name == "source" and p.disjoint(mirror, ROOT / "Shaders")
        hidden = output / "unavailable-source"
        mirror.rename(hidden)
        probes = [mirror / "Shaders/Programs/ShaderGraphPreview/descriptor.json", deployment / "gglab-shaderc.exe"]
        assert all(not path.exists() for path in probes)
        report["unavailableSourcePaths"] = list(map(str, probes))
        manifest = p.verify(environment)
        payload = environment / "payload"
        report["finalExecutables"] = {name: {"path": str(payload / name), "sha256": p.digest(payload / name)}
                                      for name in ("gglab-shaderc.exe", "GraphicsGadgetLab.exe")}
        env = dict(os.environ)
        env["PATH"] = str(payload) + ";" + os.environ["SystemRoot"] + "\\System32"
        env["VK_LAYER_PATH"] = str(payload / "VulkanLayers")
        for command in ("describe", "describe-preview"):
            result = run(payload / "gglab-shaderc.exe", [command], output, command, env)
            assert result.returncode == 0
            report[command] = json.loads(result.stdout)
        for backend, target in (("dx12", "gglab-dx12"), ("vulkan", "gglab-vulkan13")):
            for profile, contract in ((1, "numeric"), (2, "texture2d")):
                label = f"{backend}-v{profile}"
                state = output / ("state-" + label)
                p.init_state(environment, state)
                paths = run(payload / "GraphicsGadgetLab.exe", ["--state-root", state, "--self-test", "app-path-composition"],
                            output, label + "-path-composition", env)
                assert paths.returncode == 0, paths.stdout.decode(errors="replace")
                ordinary = run(payload / "gglab-shaderc.exe", ["compile", "--source-root", payload / "Shaders",
                    "--include", payload / "Shaders",
                    "--source", f"Tests/SurfaceGeneratedV{profile}ContractCompile.hlsl", "--stage", "pixel", "--entry", "PSMain",
                    "--target", target, "--cache-root", state / "ShaderCache", "--artifact-root", state / "ShaderArtifacts",
                    "--result-format", "json"], output, label + "-ordinary-compile", env)
                assert ordinary.returncode == 0, ordinary.stdout.decode(errors="replace")
                generated = state / "Generated/SurfaceGenerated.hlsli"
                source_bytes = (payload / f"Shaders/Tests/Generated/SurfaceGeneratedV{profile}.hlsli").read_bytes()
                generated.write_bytes(source_bytes)
                session = uuid.uuid4().hex
                session_root = state / "ShaderArtifacts/shader-preview-sessions" / session
                pointer = session_root / "active.ggsh.preview-active"
                observed = session_root / "observed.ggsh.preview-observed"
                def build(sequence, data):
                    generated.write_bytes(data)
                    return run(payload / "gglab-shaderc.exe", ["build-preview", "--source-root", payload / "Shaders",
                        "--generated-source", generated, "--generated-source-identity", hashlib.sha256(data).hexdigest(),
                        "--target", target, "--profile-id", "gglab.surface", "--profile-version", profile,
                        "--preview-input-contract-id", "gglab.preview-input.surface." + contract,
                        "--preview-program-descriptor-identity", p.digest(payload / "Shaders/Programs/ShaderGraphPreview/descriptor.json"),
                        "--session-id", session, "--attempt-sequence", sequence, "--cache-root", state / "ShaderCache",
                        "--artifact-root", state / "ShaderArtifacts", "--result-format", "json"], output, label + f"-build-{sequence}", env)
                first = build(1, source_bytes)
                assert first.returncode == 0, first.stdout.decode(errors="replace")
                first_pointer = pointer.read_bytes()
                runtime_args = ["--rhi", backend, "--lab", "gglab.lab.shader_graph_preview", "--shader-preview-session", session,
                                "--state-root", str(state), "--absolute-mouse"]
                stdout = (output / (label + "-runtime.stdout.log")).open("wb")
                stderr = (output / (label + "-runtime.stderr.log")).open("wb")
                process = subprocess.Popen([str(payload / "GraphicsGadgetLab.exe"), *runtime_args], cwd=output, env=env,
                                           stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW)
                evidence = {"backend": backend, "profileVersion": profile, "sessionId": session,
                            "developmentToolsEnabled": True,
                            "ordinaryCompile": json.loads(ordinary.stdout)}
                report["native"].append(evidence)
                try:
                    evidence["initialObservation"] = wait_observed(process, observed, 1)
                    failed = build(2, b"invalid HLSL\n")
                    assert failed.returncode != 0 and pointer.read_bytes() == first_pointer
                    assert process.poll() is None and observation(observed) == evidence["initialObservation"]
                    evidence["failedBuild"] = {"exitCode": failed.returncode, "lastGoodPointerPreserved": True,
                                               "lastGoodObservationPreserved": True}
                    recovered = build(3, source_bytes + b"\n// Recovery publication.\n")
                    assert recovered.returncode == 0, recovered.stdout.decode(errors="replace")
                    evidence["recoveryObservation"] = wait_observed(process, observed, 3)
                    evidence["success"] = True
                finally:
                    if process.poll() is None:
                        process.terminate()
                    process.wait(timeout=15)
                    stdout.close()
                    stderr.close()
                evidence["immutableClosurePreserved"] = p.verify(environment)["environmentId"] == manifest["environmentId"]
                diagnostics = (output / (label + "-runtime.stdout.log")).read_text(errors="replace") + (output / (label + "-runtime.stderr.log")).read_text(errors="replace")
                evidence["validationErrors"] = diagnostics.count("Vulkan validation [error]")
                evidence["validationWarnings"] = diagnostics.count("Vulkan validation [warning]")
                evidence["platformInputUnavailable"] = "GameInput is unavailable" in diagnostics
                evidence["runtimeErrors"] = [line for line in diagnostics.splitlines()
                    if "[error]" in line.lower() and "GameInput is unavailable" not in line]
                evidence["firstProductionFrame"] = "completed its first production submit/present frame transaction" in diagnostics
                assert evidence["validationErrors"] == 0
                assert not evidence["runtimeErrors"] and evidence["firstProductionFrame"]
        report["success"] = True
    except Exception as error:
        report["success"] = False
        report["error"] = str(error)
        raise
    finally:
        save(output / "evidence.json", report)


if __name__ == "__main__":
    main()
