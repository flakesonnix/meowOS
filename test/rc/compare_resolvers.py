#!/usr/bin/env python3
"""Resolver parity comparison tool for meow release validation.

Runs a set of install scenarios under BOTH resolvers (legacy and sat) against a
generated realistic repository, then compares the outcomes:

  * resolved install set (package names)
  * selected versions (name=version)
  * selected providers (which concrete package satisfies each virtual)
  * install ordering (from history)
  * diagnostics (error category) for failing scenarios
  * execution time and peak memory

Differences are classified as:
  * EXPECTED  - documented semantic divergences (e.g. virtual-provider
               substitution: the closure differs only because a different
               concrete package satisfies a virtual; versions of shared
               packages still match). Per docs/sat-default-criteria.md the SAT
               resolver picks providers explicitly while legacy may differ.
  * UNEXPECTED - a real regression: a package present in one closure but
               missing in the other for non-provider reasons, a version
               mismatch on a shared package, or a success/failure disagreement,
               or a different failure category.

Usage:
  compare_resolvers.py --meow build/meow --meta test/rc/rc-meta.json \
      [--scenario success_small] [--scenario success_mid] ...

Needs the trusted public key installed in ~/.config/meow/keys (the runner does
this). Does not modify production code.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile


def run(cmd, env=None, timeout=600):
    p = subprocess.run(
        cmd, capture_output=True, text=True, env=env, timeout=timeout
    )
    return p.returncode, p.stdout, p.stderr


def time_run(cmd, env, timeout):
    """Return (rc, stdout, stderr, wall_seconds, max_rss_kb)."""
    wall = None
    rss = None
    # Prefer GNU time for RSS; also measure wall ourselves.
    import time
    t0 = time.time()
    # Try /usr/bin/time -v for RSS.
    time_cmd = None
    for cand in ("/usr/bin/time", "time"):
        if os.path.exists(cand) or subprocess.run(
            ["which", cand], capture_output=True
        ).returncode == 0:
            time_cmd = cand
            break
    if time_cmd in ("/usr/bin/time", "time") and os.path.exists("/usr/bin/time"):
        tc = ["/usr/bin/time", "-v"] + cmd
        p = subprocess.run(
            tc, capture_output=True, text=True, env=env, timeout=timeout
        )
        rc, out, err = p.returncode, p.stdout, p.stderr
        m = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", err)
        if m:
            rss = int(m.group(1))
        # GNU time writes program stdout to err sometimes; merge.
        if not out and "Command being timed" in err:
            # extract real stdout after the separator
            parts = err.split("Command being timed", 1)
            if len(parts) > 1:
                out = parts[1]
    else:
        p = subprocess.run(
            cmd, capture_output=True, text=True, env=env, timeout=timeout
        )
        rc, out, err = p.returncode, p.stdout, p.stderr
    wall = time.time() - t0
    return rc, out, err, wall, rss


def installed_set(meow, cfg, db, env):
    rc, out, err = run(
        [meow, "--db-path", db, "--config", cfg, "installed"], env=env
    )
    names = []
    for line in out.splitlines():
        line = line.strip()
        if line and not line.startswith("["):
            names.append(line)
    return set(names), err


def version_map(meow, cfg, db, names, env):
    vm = {}
    for n in sorted(names):
        rc, out, err = run(
            [meow, "--db-path", db, "--config", cfg, "info", n], env=env
        )
        m = re.search(r"^Version\s+(\S+)", out, re.MULTILINE)
        if m:
            vm[n] = m.group(1)
        else:
            vm[n] = "?"
    return vm


def install_order(meow, cfg, db, env):
    rc, out, err = run(
        [meow, "--db-path", db, "--config", cfg, "history"], env=env
    )
    order = []
    for line in out.splitlines():
        m = re.match(r"^\S+ \S+ (\S+)", line)
        if m:
            order.append(m.group(1))
    return order


def error_category(err):
    # meow prints resolution failures as:
    #   resolution failed:
    #     <reason>
    # Capture the indented reason verbatim.
    m = re.search(r"resolution failed:\s*\n\s*([^\n]+)", err)
    reason = m.group(1).strip() if m else (err.strip().splitlines()[-1].strip() if err.strip() else "unknown")
    rl = reason.lower()
    if "cycle detected" in rl:
        return ("Cycle", reason)
    if "no version" in rl and "satisfies constraints" in rl:
        return ("VersionConstraint", reason)
    if "conflict" in rl:
        return ("Conflict", reason)
    if "unsatisf" in rl or "cannot resolve" in rl:
        return ("Unsatisfiable", reason)
    if "not found" in rl:
        return ("NotFound", reason)
    return ("Other", reason)


def classify(legacy, sat, virtuals):
    """Compare two outcome dicts; return (unexpected, expected, notes)."""
    unexpected = []
    expected = []
    notes = []

    # Success/failure agreement
    sat_cat = sat["category"][0] if isinstance(sat["category"], tuple) else sat["category"]
    leg_cat = legacy["category"][0] if isinstance(legacy["category"], tuple) else legacy["category"]
    if legacy["rc"] == 0 and sat["rc"] != 0:
        # Legacy succeeding where SAT fails is a divergence. Version-constraint
        # enforcement is a *documented* divergence (legacy ignores constraints
        # that SAT enforces), so treat that as expected; everything else is a
        # real regression worth investigating.
        if sat_cat == "VersionConstraint":
            expected.append(
                "legacy ignores version constraints that SAT enforces"
                " (documented divergence): %s"
                % (sat["category"][1] if isinstance(sat["category"], tuple) else sat["category"])
            )
            # The resulting closure difference is a consequence of this
            # divergence; do not also flag it as an unexpected regression.
            return unexpected, expected, notes
        elif sat_cat == "Conflict":
            expected.append(
                "legacy ignores conflicts that SAT enforces"
                " (documented divergence): %s"
                % (sat["category"][1] if isinstance(sat["category"], tuple) else sat["category"])
            )
            return unexpected, expected, notes
        else:
            unexpected.append(
                "legacy succeeded but sat failed (%s): %s"
                % (sat_cat, sat["category"][1] if isinstance(sat["category"], tuple) else sat["category"])
            )
    elif sat["rc"] == 0 and legacy["rc"] != 0:
        unexpected.append("sat succeeded but legacy failed (%s)" % leg_cat)
    elif legacy["rc"] != 0 and sat["rc"] != 0:
        if leg_cat != sat_cat:
            unexpected.append(
                "failure categories differ: legacy=%s sat=%s"
                % (leg_cat, sat_cat)
            )
        else:
            notes.append("both failed (%s) - consistent" % leg_cat)
        return unexpected, expected, notes

    # Both succeeded: compare closures + versions
    lc, sc = legacy["closure"], sat["closure"]
    if lc != sc:
        only_l = lc - sc
        only_s = sc - lc
        # Is the difference purely virtual-provider substitution?
        def providers_of(names):
            prov = set()
            for v, ps in virtuals.items():
                for p in ps:
                    if p in names:
                        prov.add(v)
            return prov
        # map: for each name, which virtuals it provides
        name_to_virtual = {}
        for v, ps in virtuals.items():
            for p in ps:
                name_to_virtual.setdefault(p, []).append(v)
        pure_provider = True
        for nm in only_l | only_s:
            if nm not in name_to_virtual:
                pure_provider = False
                break
        if pure_provider and (only_l or only_s):
            expected.append(
                "virtual provider substitution: only-legacy=%s only-sat=%s"
                % (sorted(only_l), sorted(only_s))
            )
        else:
            unexpected.append(
                "closure mismatch: only-legacy=%s only-sat=%s"
                % (sorted(only_l), sorted(only_s))
            )
    # version agreement on shared packages
    common = set(legacy["versions"]) & set(sat["versions"])
    for n in sorted(common):
        if legacy["versions"][n] != sat["versions"][n]:
            unexpected.append(
                "version mismatch for %s: legacy=%s sat=%s"
                % (n, legacy["versions"][n], sat["versions"][n])
            )
    # ordering note (not a regression by itself)
    if legacy["order"] != sat["order"]:
        notes.append(
            "install ordering differs (closures equal): "
            "legacy[%d] sat[%d]" % (len(legacy["order"]), len(sat["order"]))
        )
    return unexpected, expected, notes


def scenario_result(meow, cfg, db, roots, env, timeout):
    rc, out, err, wall, rss = time_run(
        [meow, "--db-path", db, "--config", cfg, "install"] + roots,
        env, timeout,
    )
    res = {"rc": rc, "wall": wall, "rss": rss, "category": error_category(err), "stderr": err}
    if rc == 0:
        closure, _ = installed_set(meow, cfg, db, env)
        versions = version_map(meow, cfg, db, closure, env)
        order = install_order(meow, cfg, db, env)
        res.update({"closure": closure, "versions": versions, "order": order})
    else:
        res["closure"] = set()
        res["versions"] = {}
        res["order"] = []
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--meow", default="build/meow")
    ap.add_argument("--meta", default="test/rc/rc-meta.json")
    ap.add_argument("--scenario", action="append",
                    choices=["success_small", "success_mid",
                             "fail_conflict", "fail_version", "fail_cycle"],
                    default=None)
    ap.add_argument("--timeout", type=int, default=600)
    args = ap.parse_args()

    with open(args.meta) as f:
        meta = json.load(f)
    virtuals = {k: set(v) for k, v in meta["virtuals"].items()}
    cfg = os.path.abspath(meta["config"])
    meow = os.path.abspath(args.meow)

    scenarios = args.scenario or [
        "success_small", "success_mid",
        "fail_conflict", "fail_version", "fail_cycle",
    ]

    roots = {}
    roots["success_small"] = meta["scenarios"]["success_small"]
    roots["success_mid"] = meta["scenarios"]["success_mid"]
    roots["fail_conflict"] = [meta["conflict_root"]]
    roots["fail_version"] = [meta["impossible_root"]]
    roots["fail_cycle"] = [meta["cycle_root"]]

    # trust store for the generated key
    home = tempfile.mkdtemp(prefix="rc-home-")
    keysdir = os.path.join(home, ".config", "meow", "keys")
    os.makedirs(keysdir, exist_ok=True)
    pub = "test/keys/meow-release.pub"
    if os.path.exists(pub):
        import shutil
        shutil.copy(pub, os.path.join(keysdir, "meow-release.pem"))
    env = dict(os.environ)
    env["HOME"] = home

    total_unexpected = 0
    total_expected = 0
    print("=" * 72)
    print("meow resolver parity comparison")
    print("config: %s" % cfg)
    print("=" * 72)
    for sc in scenarios:
        r = roots[sc]
        print("\n--- scenario: %s (%d roots) ---" % (sc, len(r)))
        res = {}
        for engine in ("legacy", "sat"):
            db = os.path.join(home, "db-%s-%s" % (sc, engine))
            if os.path.exists(db):
                subprocess.run(["rm", "-rf", db], check=False)
            e = dict(env)
            e["MEOW_RESOLVER"] = engine
            res[engine] = scenario_result(meow, cfg, db, r, e, args.timeout)
        unexp, exp, notes = classify(res["legacy"], res["sat"], virtuals)
        total_unexpected += len(unexp)
        total_expected += len(exp)
        for engine in ("legacy", "sat"):
            o = res[engine]
            cat = o["category"][0] if isinstance(o["category"], tuple) else o["category"]
            status = "OK" if o["rc"] == 0 else "FAIL(%s)" % cat
            print("  %-6s %-12s closure=%d  wall=%.2fs  rss=%s"
                  % (engine, status, len(o["closure"]),
                     o["wall"], o["rss"]))
        if exp:
            for x in exp:
                print("    EXPECTED: %s" % x)
        if notes:
            for x in notes:
                print("    NOTE: %s" % x)
        if unexp:
            for x in unexp:
                print("    UNEXPECTED: %s" % x)
            # dump raw stderr so the divergence is inspectable
            for engine in ("legacy", "sat"):
                if res[engine]["rc"] != 0:
                    print("      [%s] %s"
                          % (engine,
                             " | ".join(
                                 l for l in res[engine]["stderr"].splitlines()
                                 if l.strip() and "INFO" not in l and "WARN" not in l
                             )[:4]))
        else:
            print("    -> parity: clean")

    print("\n" + "=" * 72)
    if total_unexpected == 0:
        verdict = "PASS"
        msg = "no unexpected regressions between resolvers"
        if total_expected:
            msg += " (%d documented divergence(s) noted)" % total_expected
        print("RESULT: %s - %s" % (verdict, msg))
        sys.exit(0)
    else:
        print("RESULT: FAIL - %d unexpected regression(s)" % total_unexpected)
        sys.exit(1)


if __name__ == "__main__":
    main()
