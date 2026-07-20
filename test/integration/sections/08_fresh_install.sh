#!/usr/bin/env bash
# Fresh install and DB upgrade (sections 22-23)
set -euo pipefail

run_section() {

echo "=== 22. fresh-install from an empty system ==="
FRESH_DB="$TMPDIR/fresh-$$.db"
rm -f "$FRESH_DB"
if $MEOW --db-path "$FRESH_DB" install hello >/dev/null 2>&1; then
    if $MEOW --db-path "$FRESH_DB" installed | grep -q hello && \
       $MEOW --db-path "$FRESH_DB" verify | grep -q "verified" && \
       $MEOW --db-path "$FRESH_DB" doctor --security >/dev/null 2>&1; then
        echo "  PASS: fresh install -> installed -> verify -> doctor --security"
        pass=$((pass + 1))
    else
        echo "  FAIL: fresh-install chain did not complete cleanly"
        fail=$((fail + 1))
    fi
else
    echo "  FAIL: fresh install failed"
    fail=$((fail + 1))
fi
rm -f "$FRESH_DB"

echo ""
echo "=== 23. upgrade from a v0.3.0 database ==="
UPG_DB="$TMPDIR/upgrade-$$.db"
rm -f "$UPG_DB"
python3 - "$UPG_DB" <<'PY'
import sqlite3, sys
db = sys.argv[1]
c = sqlite3.connect(db)
c.executescript("""
CREATE TABLE packages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    version TEXT NOT NULL,
    architecture TEXT NOT NULL,
    install_time INTEGER NOT NULL
);
CREATE TABLE files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    package_id INTEGER NOT NULL,
    path TEXT NOT NULL,
    size INTEGER DEFAULT 0,
    FOREIGN KEY (package_id) REFERENCES packages(id) ON DELETE CASCADE
);
CREATE TABLE package_deps (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    package_id INTEGER NOT NULL,
    dep_name TEXT NOT NULL,
    FOREIGN KEY (package_id) REFERENCES packages(id) ON DELETE CASCADE
);
CREATE TABLE package_provides (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    package_id INTEGER NOT NULL,
    provide_name TEXT NOT NULL,
    FOREIGN KEY (package_id) REFERENCES packages(id) ON DELETE CASCADE
);
""")
c.execute("INSERT INTO packages (name,version,architecture,install_time) VALUES ('legacy-pkg','1.0.0','AMD64',1704067200)")
c.execute("INSERT INTO files (package_id,path,size) VALUES (1,'/usr/bin/legacy',42)")
c.commit(); c.close()
PY
installed_ok=$($MEOW --db-path "$UPG_DB" installed 2>&1 | grep -q legacy-pkg && echo yes || echo no)
verify_ok=$($MEOW --db-path "$UPG_DB" verify 2>&1 | grep -q "legacy-pkg" && echo yes || echo no)
doc_out=$($MEOW --db-path "$UPG_DB" doctor 2>&1 || true)
doctor_ok=$(echo "$doc_out" | grep -q "database" && echo yes || echo no)
if [ "$installed_ok" = "yes" ] && [ "$verify_ok" = "yes" ] && [ "$doctor_ok" = "yes" ]; then
    echo "  PASS: v0.3 DB migrated, legacy-pkg preserved, verify + doctor ran"
    pass=$((pass + 1))
else
    echo "  FAIL: v0.3 -> v0.4 upgrade lost data or failed (installed=$installed_ok verify=$verify_ok doctor=$doctor_ok)"
    echo "    UPG_DB exists: $(test -f "$UPG_DB" && echo yes || echo no)"
    echo "    doctor output: $(echo "$doc_out" | tr '\n' '|')"
    fail=$((fail + 1))
fi
rm -f "$UPG_DB"

}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    . "$(cd "$(dirname "$0")/../../.." && pwd)/test/integration/common.sh"
    mkdir -p ~/.config/meow/keys
    cp "$KEYS_DIR/meow-release.pub" ~/.config/meow/keys/meow-release.pem
    cleanup
    bootstrapArtifacts
    run_section
    echo "Results: $pass passed, $fail failed"
    [ "$fail" -eq 0 ] || exit 1
fi
