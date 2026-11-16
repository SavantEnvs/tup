#!/usr/bin/env bash
#
# mayhem/test.sh — RUN tup's own functional test suite (test/t*.sh, built by mayhem/build.sh).
# exit 0 = pass. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this.
#
# Runs the pre-built tests only (never compiles) and reports a CTRF summary.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# Upstream's suite: ~1000 shell scripts, each builds a throw-away tup repo, runs the real `tup` binary
# (build.sh leaves the normal-flags build at $SRC/tup, found by test/tup.sh as ../tup) and asserts on the
# resulting database / build outputs / error messages.
[ -x "$SRC/tup" ] && [ -f "$SRC/tup-ldpreload.so" ] || { echo "test.sh: $SRC/tup missing - run mayhem/build.sh first" >&2; emit_ctrf "tup-test" 0 1 0; exit 1; }

# The base image exports CC/CXX; several tests assume an unset CC (t2227 imports it).
unset CC CXX CFLAGS CXXFLAGS LDFLAGS

# t4200/t4201 exercise tup's ccache integration and need a `ccache` binary on PATH; adding one would make the
# patch grader disable its own compiler cache (SPEC §6.2 item 18), so these two are skipped (reported as skipped).
skip="t4200-ccache.sh t4201-ccache2.sh"

cd "$SRC/test"
files=""
nskip=0
for f in t[0-9]*.sh; do
  case " $skip " in *" $f "*) nskip=$((nskip+1)); continue ;; esac
  files="$files $f"
done

logdir="$(mktemp -d)"
export tupos="$(uname -s)"
passed=0
failed=0
failed_names=""
for f in $files; do
  if timeout 600 sh -e "./$f" > "$logdir/$f.log" 2>&1; then
    passed=$((passed+1)); rm -f "$logdir/$f.log"
  else
    failed=$((failed+1)); failed_names="$failed_names $f"
    tail -n 15 "$logdir/$f.log" >&2
  fi
done
[ -z "$failed_names" ] || echo "test.sh: failed:$failed_names" >&2
rm -rf "$logdir"
emit_ctrf "tup-test" "$passed" "$failed" "$nskip"
