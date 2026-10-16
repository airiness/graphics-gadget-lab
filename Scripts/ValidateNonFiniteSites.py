"""Audit the Shader-domain non-finite site inventory; this is not GPU evidence.

The lexical check covers all branches, including disabled #if bodies. It pins each
registered function's tokens so changing a guard while retaining its call count
still requires review. It is deliberately conservative, not an HLSL data-flow
proof: new aliases/bit-pattern guards also require source review.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys
import unittest


# Keep this list in the checker, not in the inventory being checked. Include
# wrappers whose callers can hide values without spelling isfinite themselves.
SANITIZERS = frozenset({
    "SanitizeHDRChannel", "SanitizeHDRColor", "SanitizeSceneRadianceChannel",
    "SanitizeSceneRadiance", "EncodeSceneColorChannel", "EncodeSceneColor",
    "EncodeForwardSceneColor", "ACESFitted",
})
GUARDS = frozenset({
    "isfinite", "isnan", "isinf", "IsTemporalColorFinite",
    "IsTemporalHistoryAccumulationValid",
})
WATCHED = SANITIZERS | GUARDS
TRIVIA = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', re.S)
TOKEN = re.compile(r"[A-Za-z_][A-Za-z_0-9]*|[0-9]+|[^\s]")
POLICIES = {"report", "error", "expected", "budgeted"}
INJECT = "NonFiniteInject"
CLAIMS = {"boundary", "production"}


def discover_sites(source, evidence=None, injections=None):
    """Return (function, operation) -> (count, normalized-body SHA-256).

    When given, injections receives (function, first argument) -> call count for
    every NonFiniteInject call, so each injection point names a registered symbol.
    """
    tokens = TOKEN.findall(TRIVIA.sub(" ", source.replace("\\\n", "")))
    pairs = {}
    stack = []
    for i, token in enumerate(tokens):
        if token in {"(", "{"}:
            stack.append((token, i))
        elif token in {")", "}"}:
            if not stack or stack[-1][0] != ({")": "(", "}": "{"}[token]):
                raise ValueError("Unbalanced shader delimiters")
            _, begin = stack.pop()
            pairs[begin] = i
            pairs[i] = begin
    if stack:
        raise ValueError("Unbalanced shader delimiters")

    functions = []
    declarations = set()
    depth = 0
    for i, token in enumerate(tokens):
        if token == "{":
            if depth == 0:
                # A function may have an output semantic between ')' and '{'.
                previous = i - 1
                if previous >= 1 and tokens[previous] != ")":
                    if previous >= 2 and tokens[previous - 1] == ":":
                        previous -= 2
                if previous >= 1 and tokens[previous] == ")":
                    opening = pairs[previous]
                    name = tokens[opening - 1]
                    if name not in {"register", "if", "for", "while", "switch"}:
                        functions.append((name, i, pairs[i]))
                        declarations.add(opening - 1)
            depth += 1
        elif token == "}":
            depth -= 1

    grouped = {}
    for i, token in enumerate(tokens):
        if injections is not None and token == INJECT and i not in declarations:
            owner = next((name for name, begin, end in functions if begin < i < end), "global")
            argument = tokens[i + 2] if i + 2 < len(tokens) and tokens[i + 1] == "(" else ""
            injections[(owner, argument)] = injections.get((owner, argument), 0) + 1
        # Include bare macro aliases (#define CHECK isfinite), not only direct calls.
        if token not in WATCHED or i in declarations:
            continue
        owner = next(((name, begin, end) for name, begin, end in functions if begin < i < end), None)
        if owner:
            name, begin, end = owner
        else:
            # Macro/global call sites cannot escape registration.
            name, begin, end = "global", -1, len(tokens)
        body = " ".join(tokens[begin + 1:end]).encode("utf-8")
        key = (name, token)
        if evidence is not None:
            arguments = tokens[i + 2:pairs[i + 1]] if i + 1 < len(tokens) and tokens[i + 1] == "(" else []
            evidence.setdefault(key, []).append((arguments, tokens[begin + 1:end]))
        digest = hashlib.sha256(body).hexdigest()
        grouped.setdefault(key, []).append(digest)
    # Overloads and preprocessor alternatives share a semantic site. Include every
    # call's owner body, in source order, rather than silently taking one variant.
    return {key: (len(bodies), hashlib.sha256(" ".join(bodies).encode("ascii")).hexdigest())
            for key, bodies in grouped.items()}



def counter_route_is_explicit(site, calls):
    from GenerateNonFiniteSites import symbol
    forwarding = SANITIZERS - {"ResolveDisplayColor"}
    if not calls:
        return False
    for arguments, body in calls:
        if site["function"] in {"SanitizeHDRChannel", "SanitizeSceneRadianceChannel"}:
            if "NonFiniteRecordChannel" not in body or "nonFiniteSite" not in body:
                return False
        elif site["operation"] in SANITIZERS:
            expected = {"nonFiniteSite", "NF_HDR_FORWARD"} if site["function"] in forwarding else {symbol(site)}
            if not expected.intersection(arguments):
                return False
        elif arguments[:3] != ["NonFiniteObserveValue", "(", symbol(site)]:
            return False
    return True


def discover_tree(root, injections=None):
    result = {}
    shaders = root / "Shaders"
    for path in sorted(shaders.rglob("*")):
        if path.suffix not in {".hlsl", ".hlsli"} or "Tests" in path.relative_to(shaders).parts:
            continue
        source = path.relative_to(shaders).as_posix()
        calls = {}
        for (function, operation), evidence in discover_sites(path.read_text(encoding="utf-8-sig"), injections=calls).items():
            result[(source, function, operation)] = evidence
        if injections is not None:
            for (function, argument), count in calls.items():
                injections[(source, function, argument)] = count
    return result


def discover_programs(root):
    """Map included source files to all potential program IDs, without #if pruning."""
    references = (root / "Sources/ShaderArtifactRuntime/Public/ShaderArtifactRuntime/GGLabShaderPrograms.h").read_text(encoding="utf-8")
    ids = dict(re.findall(r'ShaderProgramRef\s+(\w+)\s*\{\s*"([^"]+)"', references))
    recipes = (root / "Sources/Tools/ShaderCompiler/GGLabRuntimeShaderBuild.cpp").read_text(encoding="utf-8")
    programs = {}
    for symbol, source in re.findall(r'ShaderProgramBuildRecord\{\s*&(\w+),\s*L"([^"]+)"', recipes):
        if symbol not in ids:
            raise ValueError(f"Unknown shader program reference: {symbol}")
        pending = [source]
        visited = set()
        while pending:
            name = pending.pop()
            if name in visited:
                continue
            visited.add(name)
            programs.setdefault(name, set()).add(ids[symbol])
            path = root / "Shaders" / name
            text = path.read_text(encoding="utf-8-sig")
            text = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
            for include in re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', text, re.M):
                target = root / "Shaders" / include
                if not target.is_file():
                    target = path.parent / include
                if target.is_file():
                    pending.append(target.relative_to(root / "Shaders").as_posix())
    return {source: sorted(values) for source, values in programs.items()}


def validate(document, actual, programs=None, instrumented=None, injections=None):
    if set(document) != {"schema", "schemaVersion", "sites", "injectionPoints"} or document["schema"] != "gglab.shader-nonfinite-sites" or type(document["schemaVersion"]) is not int or document["schemaVersion"] != 2:
        raise ValueError("Invalid site inventory envelope")
    if not isinstance(document["sites"], list) or not isinstance(document["injectionPoints"], list):
        raise ValueError("sites and injectionPoints must be arrays")
    validate_injection_points(document["injectionPoints"], programs, injections or {})
    seen = set()
    ids = set()
    for site in document["sites"]:
        if set(site) != {"id", "source", "function", "operation", "count", "bodySha256", "programs", "kind", "evidence", "policy", "rationale"}:
            raise ValueError("Invalid site inventory fields")
        if not isinstance(site["id"], str) or not re.fullmatch(r"[a-z0-9]+(?:[.-][a-z0-9]+)*", site["id"]) or site["id"] in ids:
            raise ValueError("Invalid or duplicate site id")
        ids.add(site["id"])
        if not all(isinstance(site[key], str) and site[key] for key in ("source", "function", "operation", "rationale")):
            raise ValueError("Missing site identity or rationale")
        key = (site["source"], site["function"], site["operation"])
        if key in seen:
            raise ValueError(f"Duplicate source site: {key}")
        seen.add(key)
        if type(site["count"]) is not int or site["count"] <= 0 or not isinstance(site["bodySha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", site["bodySha256"]):
            raise ValueError("Invalid call count or body identity")
        if key not in actual or actual[key] != (site["count"], site["bodySha256"]):
            raise ValueError(f"Stale or changed site requires source/policy review: {key}")
        if not isinstance(site["programs"], list) or not site["programs"] or any(not isinstance(value, str) or not value.startswith("gglab.shader.") for value in site["programs"]):
            raise ValueError(f"Missing shader program identity: {key}")
        if programs is not None and site["programs"] != programs.get(site["source"], []):
            raise ValueError(f"Shader program consumer inventory changed: {key}")
        if site["kind"] not in {"sanitization", "recovery", "validity"} or (site["operation"] in SANITIZERS and site["kind"] != "sanitization"):
            raise ValueError(f"Invalid site kind: {key}")
        if site["kind"] == "validity":
            if site["evidence"] != "report-only":
                raise ValueError(f"Invalid validity evidence: {key}")
        elif site["evidence"] not in {"counter", "forwarded-counter"} or instrumented is None or key not in instrumented:
            raise ValueError(f"Missing production operand/counter routing: {key}")
        if not isinstance(site["policy"], dict) or set(site["policy"]) != {"negative", "fp16Maximum"} or any(value not in POLICIES - {"budgeted"} for value in site["policy"].values()):
            raise ValueError(f"Invalid clamp policy (budgeted sites need explicit reviewed limits): {key}")
    missing = set(actual) - seen
    if missing:
        raise ValueError(f"Unregistered non-finite sites: {sorted(missing)}")


def validate_injection_points(points, programs, injections):
    """Each Debug verification injection call names exactly one registered point."""
    from GenerateNonFiniteSites import injection_symbol
    registered = {}
    for point in points:
        if set(point) != {"id", "source", "function", "count", "programs", "claim", "rationale"}:
            raise ValueError("Invalid injection point fields")
        if not isinstance(point["id"], str) or not re.fullmatch(r"[a-z0-9]+(?:[.-][a-z0-9]+)*", point["id"]):
            raise ValueError("Invalid injection point id")
        if not all(isinstance(point[key], str) and point[key] for key in ("source", "function", "rationale")):
            raise ValueError("Missing injection point identity or rationale")
        key = (point["source"], point["function"], injection_symbol(point))
        if key in registered:
            raise ValueError(f"Duplicate injection point: {key}")
        if point["claim"] not in CLAIMS:
            raise ValueError(f"Invalid injection point claim: {key}")
        if type(point["count"]) is not int or point["count"] <= 0 or injections.get(key) != point["count"]:
            raise ValueError(f"Stale or changed injection point requires review: {key}")
        if not isinstance(point["programs"], list) or not point["programs"] or (programs is not None and point["programs"] != programs.get(point["source"], [])):
            raise ValueError(f"Injection point program consumer inventory changed: {key}")
        registered[key] = point
    unregistered = set(injections) - set(registered)
    if unregistered:
        raise ValueError(f"Unregistered injection points: {sorted(unregistered)}")


class SourceCheckTests(unittest.TestCase):
    def fixture(self, source="float f(float x) { return isfinite(x) ? x : 0; }"):
        sites = discover_sites(source)
        key, (count, digest) = next(iter(sites.items()))
        actual = {("Common/Test.hlsli", *key): (count, digest)}
        site = dict(id="test.f.guard", source="Common/Test.hlsli", function=key[0], operation=key[1], count=count,
                    bodySha256=digest, programs=["gglab.shader.test"], kind="recovery", evidence="counter",
                    policy=dict(negative="report", fp16Maximum="report"), rationale="Test recovery")
        return dict(schema="gglab.shader-nonfinite-sites", schemaVersion=2, sites=[site], injectionPoints=[]), actual

    def injection_fixture(self):
        source = "float f(float x, uint2 p) { return NonFiniteInject(NF_INJECT_TEST_F_VALUE, p, x); }"
        calls = {}
        discover_sites(source, injections=calls)
        injections = {("Common/Test.hlsli", *key): count for key, count in calls.items()}
        point = dict(id="test.f.value", source="Common/Test.hlsli", function="f", count=1,
                     programs=["gglab.shader.test"], claim="production", rationale="Test injection")
        return [point], injections

    def test_accept_registered(self):
        document, actual = self.fixture()
        validate(document, actual, instrumented=set(actual))

    def test_comments_strings_and_declaration_are_not_calls(self):
        self.assertEqual(discover_sites('float SanitizeHDRChannel(float x) { return x; } // isfinite(x)\n#define S "isnan(x)"'), {})

    def test_wrapped_sanitization_and_semantics(self):
        found = discover_sites("float4 PSMain(float4 x : SV_Position) : SV_Target0 { return EncodeSceneColor(x, 1); }")
        self.assertIn(("PSMain", "EncodeSceneColor"), found)

    def test_program_consumer_change_fails(self):
        document, actual = self.fixture()
        with self.assertRaises(ValueError):
            validate(document, actual, {"Common/Test.hlsli": ["gglab.shader.other"]})

    def test_overloads_and_alternative_bodies_are_both_pinned(self):
        first = "float f(float x) { return isfinite(x) ? x : 0; }"
        second = "float f(float2 x) { return isfinite(x.x) ? x.x : 0; }"
        found = discover_sites(first + second)
        self.assertEqual(found[("f", "isfinite")][0], 2)
        self.assertNotEqual(found, discover_sites(first + second.replace("x.x : 0", "0 : x.x")))

    def test_disabled_branch_is_checked(self):
        self.assertIn(("f", "isinf"), discover_sites("float f(float x) {\n#if 0\nreturn isinf(x);\n#endif\nreturn x; }"))

    def test_macro_calls_are_not_skipped(self):
        self.assertIn(("global", "SanitizeHDRColor"), discover_sites("#define SAFE(x) SanitizeHDRColor(x)"))
        self.assertIn(("global", "isfinite"), discover_sites("#define CHECK isfinite"))

    def test_whitespace_and_comments_do_not_change_identity(self):
        self.assertEqual(discover_sites("float f(float x){return isfinite(x)?x:0;}"), discover_sites("float f(float x) { /* review */ return isfinite ( x ) ? x : 0; }"))

    def test_unregistered_fails(self):
        document, actual = self.fixture()
        document["sites"] = []
        with self.assertRaises(ValueError):
            validate(document, actual, instrumented=set(actual))

    def test_changed_guard_with_same_call_count_fails(self):
        document, _ = self.fixture()
        _, actual = self.fixture("float f(float x) { return isfinite(x) ? 0 : x; }")
        with self.assertRaises(ValueError):
            validate(document, actual, instrumented=set(actual))

    def test_added_call_fails(self):
        document, _ = self.fixture()
        _, actual = self.fixture("float f(float x) { return isfinite(x) && isfinite(x) ? x : 0; }")
        with self.assertRaises(ValueError):
            validate(document, actual, instrumented=set(actual))

    def test_stale_and_duplicate_sites_fail(self):
        for mutation in ("stale", "duplicate"):
            document, actual = self.fixture()
            if mutation == "stale":
                actual = {}
            else:
                document["sites"].append(document["sites"][0].copy())
            with self.assertRaises(ValueError):
                validate(document, actual, instrumented=set(actual))

    def test_missing_counter_route_fails(self):
        document, actual = self.fixture()
        with self.assertRaises(ValueError):
            validate(document, actual, instrumented=set())

    def test_operand_route_is_required_for_every_guard_call(self):
        site = self.fixture()[0]["sites"][0]
        evidence = {}
        discover_sites("float f(float x) { return isfinite(NonFiniteObserveValue(NF_SITE_TEST_F_GUARD, x)) ? x : 0; }", evidence)
        self.assertTrue(counter_route_is_explicit(site, evidence[("f", "isfinite")]))
        discover_sites("float f(float x) { return isfinite(x) ? x : 0; }", evidence)
        self.assertFalse(counter_route_is_explicit(site, evidence[("f", "isfinite")]))

    def test_fabricated_instrumentation_fails(self):
        document, actual = self.fixture()
        document["sites"][0]["evidence"] = "fabricated"
        with self.assertRaises(ValueError):
            validate(document, actual, instrumented=set(actual))

    def test_unknown_policy_and_unknown_fields_fail(self):
        for mutation in ("policy", "field"):
            document, actual = self.fixture()
            if mutation == "policy":
                document["sites"][0]["policy"]["negative"] = "ignore"
            else:
                document["sites"][0]["ignored"] = True
            with self.assertRaises(ValueError):
                validate(document, actual, instrumented=set(actual))

    def test_registered_injection_point_is_accepted(self):
        points, injections = self.injection_fixture()
        self.assertEqual(injections, {("Common/Test.hlsli", "f", "NF_INJECT_TEST_F_VALUE"): 1})
        validate_injection_points(points, None, injections)

    def test_injection_declaration_is_not_a_call(self):
        calls = {}
        discover_sites("float NonFiniteInject(uint point, uint2 p, float x) { return x; }", injections=calls)
        self.assertEqual(calls, {})

    def test_unregistered_stale_or_changed_injection_points_fail(self):
        for mutation in ("unregistered", "stale", "count", "claim", "symbol"):
            points, injections = self.injection_fixture()
            if mutation == "unregistered":
                points = []
            elif mutation == "stale":
                injections = {}
            elif mutation == "count":
                points[0]["count"] = 2
            elif mutation == "claim":
                points[0]["claim"] = "boundary-only"
            else:
                injections = {("Common/Test.hlsli", "f", "NF_INJECT_OTHER"): 1}
            with self.assertRaises(ValueError):
                validate_injection_points(points, None, injections)

    def test_bad_delimiters_fail_closed(self):
        with self.assertRaises(ValueError):
            discover_sites("float f(float x) { return isfinite(x);")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return 0 if unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(SourceCheckTests)).wasSuccessful() else 1
    try:
        document = json.loads((args.root / "Shaders/Metadata/NonFiniteSites.json").read_text(encoding="utf-8"))
        injections = {}
        actual = discover_tree(args.root, injections)
        instrumented = set()
        by_source = {}
        for site in document["sites"]:
            if site["source"] not in by_source:
                evidence = {}
                discover_sites((args.root / "Shaders" / site["source"]).read_text(encoding="utf-8"), evidence)
                by_source[site["source"]] = evidence
            calls = by_source[site["source"]].get((site["function"], site["operation"]), [])
            if counter_route_is_explicit(site, calls):
                instrumented.add((site["source"], site["function"], site["operation"]))
        validate(document, actual, discover_programs(args.root), instrumented, injections)
        counts = Counter(site["kind"] for site in document["sites"])
        print(json.dumps(dict(status="passed", sites=len(actual), kinds=dict(counts),
                              injectionPoints=len(document["injectionPoints"]), productionCoverage="instrumented-source")))
        return 0
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"Non-finite site validation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
