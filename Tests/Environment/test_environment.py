"""Producer contract tests. Synthetic closure fixtures do not qualify native readiness."""
import copy
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("publisher", ROOT / "Scripts/Environment/publish_environment.py")
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)
PRODUCER = {"publisherVersion": "1.0.0", "publisherSha256": "1" * 64, "sourceRevision": "0" * 40,
            "sourceDirty": True, "deployment": "Build/Output/x64/Test"}


def fixture(root):
    names = list(p.ROLES.values())
    directories = {p.ROLES[k] for k in ("shaderSources", "baseArtifacts", "assets", "vulkanLayers")}
    names = [n for n in names if n not in directories]
    names += ["payload/Assets/fixture.txt"]
    names += ["payload/dxcompiler.dll", "payload/dxil.dll"]
    names += ["payload/VulkanLayers/VkLayer_khronos_validation.dll", "payload/VulkanLayers/VkLayer_khronos_validation.json"]
    names += [f"payload/BaseArtifacts/active/{t}/program-registry.ggsh.active" for t in p.TARGETS]
    for name in names:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"synthetic fixture; not executable\n")
    m = {"manifestVersion": 1, "kind": p.KIND, "producer": PRODUCER, "roles": p.ROLES,
         "writableState": {"root": "external", "roles": p.STATE},
         "members": sorted([{"path": n, "size": (root / n).stat().st_size, "sha256": p.digest(root / n)} for n in names], key=lambda v: v["path"])}
    m["environmentId"] = p.identity(m)
    (root / "environment.json").write_bytes(p.canonical(m) + b"\n")
    return m


class EnvironmentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="gglab-environment-tests-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "environment"
        self.root.mkdir()
        self.m = fixture(self.root)

    def rejects(self, code, callback):
        with self.assertRaises(p.ContractError) as caught:
            callback()
        self.assertEqual(caught.exception.code, code)

    def test_valid_and_identity_order_independence(self):
        self.assertEqual(p.verify(self.root), self.m)
        self.assertEqual(p.identity(dict(reversed(list(self.m.items())))), self.m["environmentId"])

    def test_versions(self):
        for version in [0, 2, -1, "1", True, None]:
            m = copy.deepcopy(self.m)
            m["manifestVersion"] = version
            self.rejects("unsupported-version", lambda: p.validate_manifest(m))
            self.rejects("unsupported-version", lambda: p.dispatch({"requestVersion": version}))

    def test_invalid_paths(self):
        for value in ["../x", "/root", "C:/x", "a\\b", "a//b", "a/./b", "a/../b", "a/x.", "a/x ", "a/x:y", "a/NUL.txt", "a/COM1", "a/é"]:
            self.rejects("invalid-path", lambda: p.locator(value))

    def test_json_duplicate_bom_nonfinite_and_limit(self):
        for data in [b'{"a":1,"a":2}', b'\xef\xbb\xbf{}', b'{"a":NaN}']:
            self.rejects("invalid-json", lambda: p.parse(data))
        self.rejects("limit-exceeded", lambda: p.parse(b" " * (p.MAX_JSON + 1)))

    def test_missing_and_extra_members(self):
        target = self.root / p.ROLES["tool"]
        target.unlink()
        self.rejects("membership-mismatch", lambda: p.verify(self.root))
        target.write_bytes(b"synthetic fixture; not executable\n")
        (self.root / "unlisted").write_text("x")
        self.rejects("membership-mismatch", lambda: p.verify(self.root))

    def test_hash_and_identity(self):
        (self.root / p.ROLES["tool"]).write_bytes(b"replacement")
        self.rejects("hash-mismatch", lambda: p.verify(self.root))
        m = copy.deepcopy(self.m)
        m["producer"]["sourceDirty"] = False
        self.rejects("identity-mismatch", lambda: p.validate_manifest(m))

    def test_role_and_case_conflicts(self):
        m = copy.deepcopy(self.m)
        m["roles"]["tool"] = "state/tool.exe"
        self.rejects("invalid-role", lambda: p.validate_manifest(m))
        m = copy.deepcopy(self.m)
        m["members"].insert(1, dict(m["members"][0], path=m["members"][0]["path"].upper()))
        # Invalid payload prefix also explicitly rejects this case alias.
        with self.assertRaises(p.ContractError):
            p.validate_manifest(m)
        m = copy.deepcopy(self.m)
        m["members"].insert(1, m["members"][0])
        self.rejects("path-conflict", lambda: p.validate_manifest(m))

    def test_incomplete_publication(self):
        (self.root / "environment.json").unlink()
        self.rejects("incomplete-publication", lambda: p.verify(self.root))
        fixture(self.root)
        stage = self.root.with_name(".staging-abandoned")
        self.root.rename(stage)
        self.rejects("incomplete-publication", lambda: p.verify(stage))

    def test_state_preserves_identity_and_refuses_overlap_or_overwrite(self):
        state = self.root.with_name("state")
        p.init_state(self.root, state)
        (state / "ShaderCache/cache").write_text("mutable")
        (state / "ShaderArtifacts/active/gglab-dx12/program-registry.ggsh.active").write_text("new pointer")
        self.assertEqual(p.verify(self.root)["environmentId"], self.m["environmentId"])
        self.rejects("state-exists", lambda: p.init_state(self.root, state))
        self.rejects("invalid-path", lambda: p.init_state(self.root, self.root / "state"))

    def test_hard_link(self):
        source = self.root / p.ROLES["tool"]
        os.link(source, self.root.with_name("hardlink"))
        self.rejects("hard-link", lambda: p.verify(self.root))

    @unittest.skipUnless(os.name == "nt", "Windows junction contract")
    def test_junction_and_external_source_link(self):
        external = self.root.with_name("external")
        external.mkdir()
        link = self.root / "payload/link"
        subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(external)], capture_output=True, check=True)
        self.addCleanup(lambda: os.rmdir(link) if link.exists() else None)
        self.rejects("reparse-point", lambda: p.verify(self.root))

    def test_cancel(self):
        cancel = self.root.with_name("cancel")
        cancel.touch()
        self.rejects("cancelled", lambda: p.check_cancel(str(cancel)))

    @unittest.skipUnless(os.name == "nt", "Windows development junctions")
    def test_discovery_multiple_candidates_and_external_source_refusal(self):
        repo = self.root.with_name("repository")
        for name in ("Shaders", "Assets"):
            (repo / name).mkdir(parents=True)
            (repo / name / "fixture.txt").write_text("fixture")
        deployments = []
        for flavor in ("CustomA", "CustomB"):
            deployment = repo / "Build/Output/x64" / flavor
            deployment.mkdir(parents=True)
            deployments.append(deployment)
            for name in ("GraphicsGadgetLab.exe", "gglab-shaderc.exe", "dxcompiler.dll", "dxil.dll"):
                (deployment / name).write_bytes(b"synthetic binary")
            for name in ("Shaders", "Assets"):
                link = deployment / name
                subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(repo / name)], capture_output=True, check=True)
                self.addCleanup(lambda link=link: os.rmdir(link) if link.exists() else None)
        candidates = p.discover(repo, ["Build/Output"])
        self.assertEqual([c["deployment"] for c in candidates], ["Build/Output/x64/CustomA", "Build/Output/x64/CustomB"])
        external = self.root.with_name("outside")
        external.mkdir()
        link = deployments[0] / "Shaders"
        os.rmdir(link)
        subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(external)], capture_output=True, check=True)
        self.rejects("source-external-link", lambda: p.deployment_inputs(repo, deployments[0]))
        self.assertEqual(len(p.discover(repo, ["Build/Output"])), 1)

    @unittest.skipUnless(os.name == "nt", "Windows no-replace finalization")
    def test_publish_retry_conflict_and_failure_recovery(self):
        repo = self.root.with_name("repo")
        deploy = repo / "Build/Output/x64/Test"
        deploy.mkdir(parents=True)
        for name in ["GraphicsGadgetLab.exe", "gglab-shaderc.exe", "dxcompiler.dll", "dxil.dll"]:
            (deploy / name).write_bytes(b"synthetic executable")
        shutil.copytree(self.root / "payload/Shaders", repo / "Shaders")
        (repo / "Assets/Textures/Skybox").mkdir(parents=True)
        (repo / "Assets/Textures/Skybox/fixture.txt").write_text("fixture")
        for name in ("UVTest1K.png", "UVTest4K.png"):
            (repo / "Assets/Textures" / name).write_bytes(b"synthetic texture")
        (repo / "LICENSE").write_text("fixture")
        destination = self.root.with_name("published")
        def native(exe, args, cwd, cancel):
            if args[0] == "build-runtime":
                artifacts = Path(args[args.index("--artifact-root") + 1])
                target = args[args.index("--target") + 1]
                pointer = artifacts / "active" / target / "program-registry.ggsh.active"
                pointer.parent.mkdir(parents=True, exist_ok=True)
                pointer.write_bytes(b"synthetic registry")
            return b"{}"
        with patch.object(p, "deployment_inputs", return_value=(repo, deploy)), patch.object(p, "provenance", return_value=PRODUCER), patch.object(p, "run_native", side_effect=native), patch.object(p, "bundle_crt"):
            first = p.publish(repo, deploy, destination)
            self.assertEqual(first["outcome"], "published")
            self.assertEqual(p.publish(repo, deploy, destination)["outcome"], "already-present")
            concurrent_destination = self.root.with_name("concurrent")
            with ThreadPoolExecutor(max_workers=2) as executor:
                futures = [executor.submit(p.publish, repo, deploy, concurrent_destination) for _ in range(2)]
                self.assertEqual(sorted(f.result()["outcome"] for f in futures), ["already-present", "published"])
            cancel = self.root.with_name("cancel-late")
            def cancel_during_build(exe, args, cwd, cancel_path):
                result = native(exe, args, cwd, cancel_path)
                cancel.touch()
                return result
            with patch.object(p, "run_native", side_effect=cancel_during_build):
                self.rejects("cancelled", lambda: p.publish(repo, deploy, destination, str(cancel)))
            self.assertEqual(p.verify(destination)["environmentId"], first["environmentId"])
            with patch.object(p, "run_native", side_effect=p.ContractError("native-failed", "injected")):
                self.rejects("native-failed", lambda: p.publish(repo, deploy, destination))
            self.assertEqual(p.verify(destination)["environmentId"], first["environmentId"])
            self.assertEqual(p.publish(repo, deploy, destination)["outcome"], "already-present")
            (deploy / "gglab-shaderc.exe").write_bytes(b"new tool")
            self.rejects("destination-conflict", lambda: p.publish(repo, deploy, destination))
            self.assertEqual(p.verify(destination)["environmentId"], first["environmentId"])

    def test_checked_in_reader_vectors(self):
        directory = ROOT / "Tests/Environment/fixtures"
        for vector in json.loads((directory / "index.json").read_text())["cases"]:
            data = (directory / vector["file"]).read_bytes()
            if vector["accept"]:
                self.assertEqual(p.validate_manifest(p.parse(data))["environmentId"], vector["environmentId"])
            else:
                self.rejects(vector["error"], lambda: p.validate_manifest(p.parse(data)))

    def test_process_vectors(self):
        vectors = json.loads((ROOT / "Tests/Environment/fixtures/process-cases.json").read_text())
        for case in vectors["cases"]:
            self.rejects(case["error"], lambda: p.dispatch(case["request"]))

    def test_process_transport(self):
        import sys
        for request, expected_exit in [({"requestVersion": 1, "operation": "verify", "environmentRoot": str(self.root)}, 0),
                                       ({"requestVersion": 9, "operation": "verify"}, 2)]:
            result = subprocess.run([sys.executable, str(ROOT / "Scripts/Environment/publish_environment.py")],
                                    input=p.canonical(request), capture_output=True, check=False)
            self.assertEqual(result.returncode, expected_exit)
            self.assertEqual(len(result.stdout.splitlines()), 1)
            response = p.parse(result.stdout)
            self.assertEqual(response["resultVersion"], 1)
            self.assertEqual(response["success"], expected_exit == 0)
            self.assertEqual(result.stderr, b"")

    def test_filesystem_vectors(self):
        vectors = json.loads((ROOT / "Tests/Environment/fixtures/filesystem-cases.json").read_text())
        for case in vectors["cases"]:
            root = self.root.with_name(case["name"])
            root.mkdir()
            fixture(root)
            target = root / case["path"]
            if case["mutation"] == "remove":
                target.unlink()
            else:
                target.write_bytes(case["replacementUtf8"].encode("utf-8"))
            self.rejects(case["error"], lambda: p.verify(root))


if __name__ == "__main__":
    unittest.main()
