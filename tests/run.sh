#!/bin/sh
# tests/run.sh: integration test for serva + svc
#
# Boots a sandboxed serva against ./ssv and drives it with svc-test.
# No root needed: everything lives under tests/.

set -u
cd "$(dirname "$0")"

SOCK="$PWD/serva.sock"
SERVA=./serva-test
SVC=./svc-test
LOG=serva.log
PASS=0
FAIL=0

say()  { printf '%s\n' "$*"; }
ok()   { PASS=$((PASS + 1)); say "ok   - $1"; }
bad()  { FAIL=$((FAIL + 1)); say "FAIL - $1"; }

cleanup() {
	[ -n "${SERVA_PID:-}" ] && kill "$SERVA_PID" 2>/dev/null
	wait 2>/dev/null
	rm -f "$SOCK"
}
trap cleanup EXIT

# --- setup sandbox -----------------------------------------------------------
rm -rf ssv
mkdir -p ssv/boot/crashme ssv/default/sleeper ssv/default/oneshot ssv/default/depender/need

cat > ssv/boot/crashme/run <<'EOF'
#!/bin/sh
echo "crashme starting"
exit 42
EOF

cat > ssv/default/sleeper/run <<'EOF'
#!/bin/sh
echo "sleeper starting"
trap 'exit 0' TERM
while :; do sleep 1; done
EOF

cat > ssv/default/oneshot/run <<'EOF'
#!/bin/sh
echo "oneshot ran"
touch "$PWD.marker" 2>/dev/null || true
exit 0
EOF
touch ssv/default/oneshot/once

cat > ssv/default/depender/run <<'EOF'
#!/bin/sh
trap 'exit 0' TERM
while :; do sleep 1; done
EOF
touch ssv/default/depender/need/sleeper

chmod +x ssv/*/*/run

# --- build + launch ----------------------------------------------------------
make -s all || { say "build failed"; exit 1; }

"$SERVA" >"$LOG" 2>&1 &
SERVA_PID=$!

for i in 1 2 3 4 5 6 7 8 9 10; do
	[ -S "$SOCK" ] && break
	sleep 0.2
done
[ -S "$SOCK" ] || { bad "serva socket never appeared"; exit 1; }
sleep 1   # let services start (and crashme crash once)

# --- tests -------------------------------------------------------------------
out=$("$SVC" -s)
case $out in
*default/sleeper*RUN*) ok "svc -s lists sleeper as RUN" ;;
*) bad "svc -s should show sleeper RUN, got: $out" ;;
esac

out=$("$SVC" -s sleeper)
case $out in
*sleeper*RUN*runs=1*) ok "svc -s <name> shows single service with runs=1" ;;
*) bad "svc -s sleeper unexpected: $out" ;;
esac

out=$("$SVC" -s crashme)
case $out in
*exit=42*) ok "svc -s crashme records exit code 42" ;;
*) bad "svc -s crashme missing exit=42: $out" ;;
esac

out=$("$SVC" -s crashme)
runs=$(printf '%s' "$out" | sed -n 's/.*runs=\([0-9]*\).*/\1/p')
[ "${runs:-0}" -ge 2 ] && ok "crashme restarted with backoff (runs=$runs)" \
	|| bad "crashme should have restarted, runs=${runs:-?}"

out=$("$SVC" -s depender)
case $out in
*RUN*) ok "depender started once sleeper was running" ;;
*) bad "depender should be RUN after dep met: $out" ;;
esac

out=$("$SVC" -s oneshot)
case $out in
*DONE*) ok "oneshot reports DONE after exit" ;;
*) bad "oneshot should be DONE: $out" ;;
esac

out=$("$SVC" -d sleeper 2>&1) && sleep 0.5 && out=$("$SVC" -s sleeper)
case $out in
*DOWN*) ok "svc -d brings sleeper down" ;;
*) bad "sleeper should be DOWN after svc -d: $out" ;;
esac

out=$("$SVC" -s depender)
case $out in
*WAIT*) ok "depender cascades to WAIT when sleeper stops" ;;
*) bad "depender should be WAIT after sleeper down: $out" ;;
esac

"$SVC" -u sleeper >/dev/null && sleep 0.5
out=$("$SVC" -s depender)
case $out in
*RUN*) ok "depender restarts when dep comes back" ;;
*) bad "depender should be RUN again: $out" ;;
esac

out=$("$SVC" -s nosuchsvc 2>&1)
case $out in
*ERR*) ok "svc -s on unknown service returns ERR" ;;
*) bad "expected ERR for unknown service: $out" ;;
esac

out=$("$SVC" -r sleeper 2>&1) && sleep 0.5
runs=$("$SVC" -s sleeper | sed -n 's/.*runs=\([0-9]*\).*/\1/p')
[ "${runs:-0}" -ge 2 ] && ok "svc -r restarts sleeper (runs=$runs)" \
	|| bad "svc -r did not restart sleeper: runs=${runs:-?}"

# --- summary -----------------------------------------------------------------
say ""
say "passed: $PASS  failed: $FAIL"
[ "$FAIL" -eq 0 ]
