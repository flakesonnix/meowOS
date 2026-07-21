#!/usr/bin/env python3
"""Deterministic "real-world" repository generator for meow release validation.

Given a fixed seed, produces a by-name repository tree (one large primary repo
plus a smaller higher-priority "extra" repo) with:

  * ~N packages (default 600, range 500-1000)
  * 1-3 versions per package, with real .pkg.tar.zst artifacts built via meow-build
  * a mixed dependency graph (some version-constrained)
  * virtual providers (several concrete packages provide a virtual name; other
    packages depend on the virtual)
  * conflicts (mutually exclusive packages)
  * optional dependencies (metadata)
  * a few intentional hard dependency cycles (isolated subset)
  * repository priorities (two repos, extra has higher priority)
  * valid signed repository metadata (repository.toml.sig + packages.toml.sig)

Output (under --out, default test/rc):
  repo-main/            primary by-name repository (signed)
  repo-extra/           smaller higher-priority repository (signed)
  artifacts/            built .pkg.tar.zst files (file:// targets)
  rc-meow.toml          config referencing both repos with priorities
  rc-meta.json          machine-readable facts (virtuals, cycles, conflicts,
                       scenarios) for the comparison tool

Determinism: all structure derives from random.Random(SEED). Artifact content
is not byte-identical across runs (meow-build embeds mtimes), but the repository
*structure* and therefore the resolver comparison are fully reproducible.

No production code is touched; this only drives meow-build / meow-repo.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

SEED = 1337
VIRTUALS = [
    "virtual-logger",
    "virtual-db",
    "virtual-http",
    "virtual-ui",
    "virtual-crypto",
]


def shard(name: str) -> str:
    return name[:2]


def ver_tuple(rng):
    return (rng.randint(1, 3), rng.randint(0, 5), rng.randint(0, 9))


def ver_str(t):
    return "%d.%d.%d" % t


def build_artifact(meow_build, artifacts_dir, name, version, arch="AMD64"):
    src = tempfile.mkdtemp(prefix="mb-")
    try:
        os.makedirs(os.path.join(src, "files", "usr", "bin"), exist_ok=True)
        with open(os.path.join(src, "files", "usr", "bin", name), "w") as f:
            f.write("#!/bin/sh\n")
        os.chmod(os.path.join(src, "files", "usr", "bin", name), 0o755)
        with open(os.path.join(src, "package.toml"), "w") as f:
            f.write(
                'name = "%s"\nversion = "%s"\narchitecture = "%s"\n'
                'description = "rc fixture"\n' % (name, version, arch)
            )
        out = artifacts_dir
        proc = subprocess.run(
            [meow_build, "--output", out, src],
            capture_output=True, text=True,
        )
        if proc.returncode != 0:
            sys.exit("meow-build failed for %s: %s" % (name, proc.stderr))
        m = re.search(r"sha256:\s*([0-9a-f]+)", proc.stdout)
        if not m:
            sys.exit("no sha256 from meow-build for %s" % name)
        return m.group(1)
    finally:
        subprocess.run(["rm", "-rf", src], check=False)


def write_package_toml(path, name, version, depends, conflicts, provides, optional):
    lines = [
        "format_version = 1",
        "[metadata]",
        'name = "%s"' % name,
        'version = "%s"' % version,
        'architecture = "AMD64"',
        'description = "rc generated package"',
    ]
    if depends:
        lines.append("depends = [%s]" % ", ".join('"%s"' % d for d in depends))
    if conflicts:
        lines.append("conflicts = [%s]" % ", ".join('"%s"' % c for c in conflicts))
    if provides:
        lines.append("provides = [%s]" % ", ".join('"%s"' % p for p in provides))
    for od in optional:
        lines.append("[[optional_depends]]")
        lines.append('package = "%s"' % od)
        lines.append('description = "optional"')
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def write_version_toml(path, filename, url, sha256):
    with open(path, "w") as f:
        f.write(
            "[artifact]\nfilename = \"%s\"\nurl = \"%s\"\nsha256 = \"%s\"\n"
            % (filename, url, sha256)
        )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="test/rc")
    ap.add_argument("--meow-build", default="build/meow-build")
    ap.add_argument("--meow-repo", default="build/meow-repo")
    ap.add_argument("--key", default="test/keys/meow-release.pem")
    ap.add_argument("--key-id", default="meow-release")
    ap.add_argument("--count", type=int, default=600)
    args = ap.parse_args()

    if args.count < 1:
        sys.exit("--count must be positive")

    rng = __import__("random").Random(SEED)
    n = args.count

    args.key = os.path.abspath(args.key)
    args.meow_build = os.path.abspath(args.meow_build)
    args.meow_repo = os.path.abspath(args.meow_repo)

    out = os.path.abspath(args.out)
    repo_main = os.path.join(out, "repo-main")
    repo_extra = os.path.join(out, "repo-extra")
    artifacts = os.path.join(out, "artifacts")
    for d in (repo_main, repo_extra, artifacts):
        subprocess.run(["rm", "-rf", d], check=True)
        os.makedirs(d, exist_ok=True)

    # Extra repo: smaller, higher priority, owns a slice of virtual providers
    # and a few exclusive packages.
    extra_count = max(1, n // 12)

    # ---- conflicts: disjoint pairs ----
    conflicts = {}
    conflict_pairs = []
    for _ in range(max(15, n // 20)):
        a = rng.randrange(n)
        b = rng.randrange(n)
        if a == b:
            continue
        na, nb = "pkg%04d" % a, "pkg%04d" % b
        conflicts.setdefault(na, []).append(nb)
        conflicts.setdefault(nb, []).append(na)
        conflict_pairs.append((na, nb))

    # ---- intentional hard cycles on an isolated PREFIX subset ----
    # cycle members are pkg0000..pkg(2*cyc-1); because normal edges only point
    # to LOWER-indexed packages, nothing outside the prefix can depend on them,
    # so they remain unreachable from any success scenario.
    cycle_members = set()
    cycle_pairs = []
    cyc = min(6, n // 50 + 2)
    for a in range(0, 2 * cyc, 2):
        b = a + 1
        na, nb = "pkg%04d" % a, "pkg%04d" % b
        cycle_members.add(na)
        cycle_members.add(nb)
        cycle_pairs.append((na, nb))

    # ---- build package definitions ----
    # each: name -> dict(versions[list of tuples], depends[list], optional[list])
    pkgs = {}
    for i in range(n):
        name = "pkg%04d" % i
        nver = rng.randint(1, 3)
        # cycle members get exactly one version so the cycle scenario isolates
        # cycle detection, not version selection.
        if name in cycle_members:
            nver = 1
        vers = sorted({ver_tuple(rng) for _ in range(nver)})
        deps = []
        # 0-4 dependencies, each only on a LOWER-indexed package. Generating
        # edges strictly backward in index order keeps the normal dependency
        # graph acyclic (a DAG), so every non-cycle scenario resolves. The only
        # intentional cycles are the isolated cycle_pairs below.
        for _ in range(rng.randint(0, 4)):
            if i == 0:
                break
            # bias toward leaves (no deps) so virtual providers have candidates
            if rng.random() < 0.45:
                break
            t = rng.randrange(i)
            tname = "pkg%04d" % t
            cons = ""
            if rng.random() < 0.4:
                vt = ver_tuple(rng)
                cons = ">=%s" % ver_str(vt)
            deps.append(tname + cons)
        # cycle members already carry their mutual edges via cycle_pairs above
        opt = []
        for _ in range(rng.randint(0, 2)):
            t = rng.randrange(n)
            if t != i:
                opt.append("pkg%04d" % t)
        # never depend on self (covers any combined virtual/provider edge)
        deps = [d for d in deps if d != name]
        pkgs[name] = {
            "versions": vers,
            "depends": deps,
            "optional": opt,
            "provides": [],  # filled after leaf/provider assignment
            "conflicts": conflicts.get(name, []),
            "in_extra": i < extra_count,
        }

    # ---- virtual providers: pick LEAF packages (no normal deps) so a virtual
    # consumer's forward edge to its provider cannot close a cycle. ----
    leaf_names = [name for name, p in pkgs.items() if not p["depends"]]
    rng.shuffle(leaf_names)
    providers = {v: [] for v in VIRTUALS}
    li = 0
    for v in VIRTUALS:
        k = rng.randint(3, 5)
        for _ in range(k):
            if li >= len(leaf_names):
                break
            providers[v].append(leaf_names[li])
            li += 1
        # if a virtual got no provider, drop it from usable set implicitly
        if not providers[v]:
            providers[v] = []
    for v in VIRTUALS:
        for prov in providers[v]:
            pkgs[prov]["provides"].append(v)

    # virtual consumers: non-leaf packages that depend on a virtual AND a
    # concrete provider of it (so the provider is reachable and both resolvers
    # resolve the virtual identically). Only use virtuals that actually got
    # providers.
    virtual_consumers = {}
    usable_virtuals = [v for v in VIRTUALS if providers[v]]
    if usable_virtuals:
        candidates = [name for name in pkgs if pkgs[name]["depends"]]
        rng.shuffle(candidates)
        nconsumers = max(20, n // 10)
        for c in candidates[:nconsumers]:
            v = rng.choice(usable_virtuals)
            virtual_consumers[c] = v
            prov = rng.choice(providers[v])
            if prov != c:
                pkgs[c]["depends"].append(v)
                pkgs[c]["depends"].append(prov)

    # ---- write trees + build artifacts ----
    main_names = [name for name in pkgs if not pkgs[name]["in_extra"]]
    extra_names = [name for name in pkgs if pkgs[name]["in_extra"]]
    print("building %d main + %d extra artifacts..." % (len(main_names), len(extra_names)))

    def write_repo(repo_dir, names):
        by_name = os.path.join(repo_dir, "by-name")
        os.makedirs(by_name, exist_ok=True)
        for name in names:
            p = pkgs[name]
            s = shard(name)
            pdir = os.path.join(by_name, s, name)
            os.makedirs(os.path.join(pdir, "versions"), exist_ok=True)
            last_ver = ver_str(p["versions"][-1])
            write_package_toml(
                os.path.join(pdir, "package.toml"),
                name, last_ver,
                p["depends"], p["conflicts"], p["provides"], p["optional"],
            )
            for vt in p["versions"]:
                vs = ver_str(vt)
                fn = "%s-%s.pkg.tar.zst" % (name, vs)
                sha = build_artifact(args.meow_build, artifacts, name, vs)
                url = "file://%s/%s" % (artifacts, fn)
                write_version_toml(
                    os.path.join(pdir, "versions", vs + ".toml"), fn, url, sha
                )

    write_repo(repo_main, main_names)
    write_repo(repo_extra, extra_names)

    # ---- config referencing both repos with priorities ----
    cfg = (
        '[[repositories]]\n'
        'id = "extra"\n'
        'mirrors = ["file://%s"]\n'
        'priority = 100\n\n'
        '[[repositories]]\n'
        'id = "main"\n'
        'mirrors = ["file://%s"]\n'
        'priority = 10\n'
    ) % (repo_extra, repo_main)
    with open(os.path.join(out, "rc-meow.toml"), "w") as f:
        f.write(cfg)

    # ---- scenarios for the comparison tool ----
    # success: a spread of root packages avoiding cycle members
    safe = [name for name in pkgs if name not in cycle_members]
    rng2 = __import__("random").Random(SEED + 1)
    small_roots = rng2.sample(safe, min(40, len(safe)))
    # a larger root set that includes virtual consumers (exercise providers)
    mid_roots = rng2.sample(safe, min(150, len(safe)))
    # failure scenarios
    # 1) conflict: depend on both sides of a conflict pair
    conflict_root = "pkg%04d" % (n + 1000)  # placeholder; use a synthetic root
    # Build a synthetic root package that depends on a conflict pair.
    synth_dir = os.path.join(repo_main, "by-name", "zz", "conflictroot")
    os.makedirs(os.path.join(synth_dir, "versions"), exist_ok=True)
    ca, cb = conflict_pairs[0]
    write_package_toml(
        os.path.join(synth_dir, "package.toml"),
        "conflictroot", "1.0.0", [ca, cb], [], [], [],
    )
    write_version_toml(
        os.path.join(synth_dir, "versions", "1.0.0.toml"),
        "conflictroot-1.0.0.pkg.tar.zst",
        "file://%s/conflictroot-1.0.0.pkg.tar.zst"
        % artifacts,
        build_artifact(args.meow_build, artifacts, "conflictroot", "1.0.0"),
    )
    # 2) impossible version: depend on a package with a too-high constraint
    synth2 = os.path.join(repo_main, "by-name", "zz", "improot")
    os.makedirs(os.path.join(synth2, "versions"), exist_ok=True)
    iv_name = small_roots[0]
    iv_ver = pkgs[iv_name]["versions"][0]
    write_package_toml(
        os.path.join(synth2, "package.toml"),
        "improot", "1.0.0", ["%s>=99.0.0" % iv_name], [], [], [],
    )
    write_version_toml(
        os.path.join(synth2, "versions", "1.0.0.toml"),
        "improot-1.0.0.pkg.tar.zst",
        "file://%s/improot-1.0.0.pkg.tar.zst" % artifacts,
        build_artifact(args.meow_build, artifacts, "improot", "1.0.0"),
    )
    # 3) cycle: root a cycle member
    cyc_root = cycle_pairs[0][0]

    # ---- sign both repos (sync first so repository.toml exists) ----
    for repo_dir in (repo_main, repo_extra):
        subprocess.run(
            [args.meow_repo, "sync", "--repo", repo_dir],
            capture_output=True, text=True,
        )
        proc = subprocess.run(
            [args.meow_repo, "sign", "--key", args.key,
             "--key-id", args.key_id, "--repo", repo_dir],
            capture_output=True, text=True,
        )
        if proc.returncode != 0:
            sys.exit("meow-repo sign failed for %s: %s" % (repo_dir, proc.stderr))

    meta = {
        "virtuals": providers,
        "conflict_root": "conflictroot",
        "impossible_root": "improot",
        "cycle_root": cyc_root,
        "scenarios": {
            "success_small": small_roots,
            "success_mid": mid_roots,
        },
        "config": os.path.join(out, "rc-meow.toml"),
        "repo_main": repo_main,
        "repo_extra": repo_extra,
    }
    with open(os.path.join(out, "rc-meta.json"), "w") as f:
        json.dump(meta, f, indent=2)

    print("generated repo at %s" % out)
    print("  main packages: %d, extra packages: %d" % (len(main_names), len(extra_names)))
    print("  virtuals: %s" % ", ".join(VIRTUALS))
    print("  conflict pairs: %d, cycle pairs: %d" % (len(conflict_pairs), len(cycle_pairs)))


if __name__ == "__main__":
    main()
