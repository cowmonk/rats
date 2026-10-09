#!/bin/sh
# tests/run.sh: integration test for serva + svc
#
# Copies ./ssv into a sandbox and drives serva with svc-test.
# No root needed: everything lives under tests/.

set -u
cd "$(dirname "$0")"

SANDBOX="$PWD/sandbox/$$"
SOCK="$SANDBOX/serva.sock"
SERVA="$SANDBOX/serva-test"
SVC="$SANDBOX/svc-test"
LOG="$SANDBOX/serva.log"
CONTROL_PROBE="$SANDBOX/control-test"
export CONTROL_PROBE
PASS=0
FAIL=0

say()  { printf '%s\n' "$*"; }
ok()   { PASS=$((PASS + 1)); say "ok   - $1"; }
bad()  { FAIL=$((FAIL + 1)); say "FAIL - $1"; }

cleanup() {
	if [ -n "${SERVA_PID:-}" ]; then
		kill "$SERVA_PID" 2>/dev/null
		wait "$SERVA_PID" 2>/dev/null
	fi
	rm -f "$SOCK"
}
trap cleanup 0
trap 'exit 1' HUP INT TERM

# --- setup sandbox -----------------------------------------------------------
umask 077
mkdir -p "$PWD/sandbox" || exit 1
mkdir "$SANDBOX" || exit 1
cp -R ssv "$SANDBOX/ssv" || exit 1

for i in 1 2 3 4 5 6 7 8; do
	service="$SANDBOX/ssv/default/status$i"
	mkdir -p "$service/need" "$service/after" || exit 1
	cp ssv/default/oneshot/run "$service/run" || exit 1
	touch "$service/down"
	for j in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
		dep=$(printf 'dep%02d_%057d' "$j" 0)
		touch "$service/need/$dep" "$service/after/$dep"
	done
done

mkdir "$SANDBOX/ssv/default/fdprobe" || exit 1
cat > "$SANDBOX/ssv/default/fdprobe/run" <<'EOF'
#!/bin/sh
"$CONTROL_PROBE" >probe.out
exec sleep 30
EOF
cat > "$SANDBOX/ssv/default/fdprobe/log" <<'EOF'
#!/bin/sh
"$CONTROL_PROBE" >logger.out
exec cat >/dev/null
EOF
touch "$SANDBOX/ssv/default/fdprobe/down" "$SANDBOX/ssv/default/fdprobe/once"
chmod +x "$SANDBOX/ssv/default/fdprobe/run" "$SANDBOX/ssv/default/fdprobe/log"

# --- build + launch ----------------------------------------------------------
"${MAKE:-make}" -s TEST_DIR="$SANDBOX" all || { say "build failed"; exit 1; }

(umask 000; exec "$SERVA") >"$LOG" 2>&1 &
SERVA_PID=$!

for i in 1 2 3 4 5 6 7 8 9 10; do
	[ -S "$SOCK" ] && break
	sleep 1
done
[ -S "$SOCK" ] || { bad "serva socket never appeared"; exit 1; }
sleep 2   # let services start and crashme restart

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

out=$("$SVC" -d sleeper 2>&1) && sleep 1 && out=$("$SVC" -s sleeper)
case $out in
*DOWN*) ok "svc -d brings sleeper down" ;;
*) bad "sleeper should be DOWN after svc -d: $out" ;;
esac

out=$("$SVC" -s depender)
case $out in
*WAIT*) ok "depender cascades to WAIT when sleeper stops" ;;
*) bad "depender should be WAIT after sleeper down: $out" ;;
esac

"$SVC" -u sleeper >/dev/null && sleep 1
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

out=$("$SVC" -r sleeper 2>&1) && sleep 1
runs=$("$SVC" -s sleeper | sed -n 's/.*runs=\([0-9]*\).*/\1/p')
[ "${runs:-0}" -ge 2 ] && ok "svc -r restarts sleeper (runs=$runs)" \
	|| bad "svc -r did not restart sleeper: runs=${runs:-?}"

out=$("$SVC" -s)
rows=$(printf '%s\n' "$out" | sed -n '/default\/status[1-8]/p' | wc -l)
[ "$rows" -eq 8 ] && ok "large status response includes every service" \
	|| bad "large status response lost services: rows=$rows"

longname=$(printf '%05000d' 0)
if out=$("$SVC" -u "$longname" 2>&1); then
	bad "oversized service name should fail"
else
	case $out in
	*'command too long'*) ok "oversized service name is rejected safely" ;;
	*) bad "unexpected error for oversized service name: $out" ;;
	esac
fi

out=$("$SVC" -s sleeper depender)
case $out in
*default/sleeper*RUN*default/depender*RUN*) ok "svc handles multiple service names" ;;
*) bad "svc did not return both services: $out" ;;
esac

if "$SVC" -s nosuchsvc >/dev/null; then
	bad "unknown service should return failure"
else
	ok "unknown service returns failure"
fi

"$CONTROL_PROBE" -mode && ok "control socket has mode 0600 under umask 000" \
	|| bad "control socket permissions are too broad"

"$SVC" -u fdprobe >/dev/null
sleep 1
for file in probe.out logger.out; do
	out=$(cat "$SANDBOX/ssv/default/fdprobe/$file" 2>/dev/null)
	case $out in
	'OK: no supervisor descriptors inherited') ok "$file: supervisor descriptors closed and SIGPIPE restored" ;;
	*) bad "$file: unexpected inherited state: $out" ;;
	esac
done

if "$CONTROL_PROBE" -disconnect && "$SVC" -s sleeper >/dev/null; then
	ok "serva survives a client disconnect during its response"
else
	bad "serva died after a client disconnected"
fi

# --- summary -----------------------------------------------------------------
say ""
say "passed: $PASS  failed: $FAIL"
say "test files: $SANDBOX"
[ "$FAIL" -eq 0 ]
