#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
recover="${1:-$repo_root/recover.sh}"
tmp_root="$(mktemp -d)"
mock_bin="$tmp_root/bin"
real_awk="$(command -v awk)"
mkdir -p "$mock_bin"
trap 'rm -rf "$tmp_root"' EXIT

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

assert_file_contains() {
    local file=$1
    local expected=$2
    grep -Fq -- "$expected" "$file" || fail "Expected '$expected' in $file"
}

assert_no_temp_files() {
    local config_dir=$1
    if find "$config_dir" -maxdepth 1 -type f -name 'plasma-org.kde.plasma.desktop-appletsrc.tmp.*' -print -quit | grep -q .; then
        fail "Temporary Plasma config was left behind in $config_dir"
    fi
}

make_fixture() {
    cat <<'EOF'
[Containments][1]
activity=
plugin=ua.bezeza.alveus
wallpaperplugin=org.kde.image

[Containments][1][Applets][2]
plugin=org.kde.plasma.digitalclock
geometry=1

[Containments][1][Applets][3]
plugin=ua.bezeza.alveus
geometry=2

[Containments][1][Applets][3][Configuration]
title=Alveus widget data

[Containments][1][Applets][4]
plugin=org.kde.plasma.folder
geometry=3

[Containments][2]
plugin=org.kde.desktopcontainment
wallpaperplugin=org.kde.image
EOF
}

new_case() {
    CASE_DIR="$tmp_root/$1"
    mkdir -p "$CASE_DIR/config" "$CASE_DIR/home"
    CFG="$CASE_DIR/config/plasma-org.kde.plasma.desktop-appletsrc"
    make_fixture > "$CFG"
    cp "$CFG" "$CASE_DIR/original"
    LOG="$CASE_DIR/commands.log"
    : > "$LOG"
}

run_recover() {
    local case_dir=$1
    shift
    env \
        PATH="$mock_bin:$PATH" \
        HOME="$case_dir/home" \
        XDG_CONFIG_HOME="$case_dir/config" \
        MOCK_LOG="$case_dir/commands.log" \
        REAL_AWK="$real_awk" \
        "$@" \
        "$recover"
}

assert_backup_matches_original() {
    local backup
    backup="$(find "$CASE_DIR/config" -maxdepth 1 -type f -name 'plasma-org.kde.plasma.desktop-appletsrc.before-recover-*' -print -quit)"
    [[ -n "$backup" ]] || fail "Timestamped backup was not created"
    cmp -s "$CASE_DIR/original" "$backup" || fail "Backup does not match the original config"
}

cat > "$mock_bin/systemctl" <<'EOF'
#!/usr/bin/env bash
printf 'systemctl %s\n' "$*" >> "${MOCK_LOG:?}"
if [[ "${1:-}" == "--user" && "${2:-}" == "is-active" ]]; then
    [[ "${MOCK_SYSTEMCTL_ACTIVE:-1}" == "1" ]]
    exit $?
fi
if [[ "${1:-}" == "--user" && "${2:-}" == "stop" ]]; then
    [[ "${MOCK_STOP_FAIL:-0}" != "1" ]]
    exit $?
fi
if [[ "${1:-}" == "--user" && "${2:-}" == "start" ]]; then
    [[ "${MOCK_START_FAIL:-0}" != "1" ]]
    exit $?
fi
exit 0
EOF

cat > "$mock_bin/kquitapp6" <<'EOF'
#!/usr/bin/env bash
printf 'kquitapp6 %s\n' "$*" >> "${MOCK_LOG:?}"
EOF

cat > "$mock_bin/kstart" <<'EOF'
#!/usr/bin/env bash
printf 'kstart %s\n' "$*" >> "${MOCK_LOG:?}"
exit "${MOCK_KSTART_STATUS:-0}"
EOF

cat > "$mock_bin/sleep" <<'EOF'
#!/usr/bin/env bash
printf 'sleep %s\n' "$*" >> "${MOCK_LOG:?}"
EOF

cat > "$mock_bin/awk" <<'EOF'
#!/usr/bin/env bash
if [[ "${MOCK_AWK_FAIL:-0}" == "1" ]]; then
    exit 42
fi
if [[ "${MOCK_AWK_INVALID:-0}" == "1" ]]; then
    printf 'plugin=ua.bezeza.alveus\n'
    exit 0
fi
if [[ "${MOCK_AWK_TERM_PARENT:-0}" == "1" ]]; then
    kill -TERM "$PPID"
fi
exec "$REAL_AWK" "$@"
EOF
chmod +x "$mock_bin/"*

# Normal systemd path: convert the desktop containment, remove the Alveus
# widget and its subsection, preserve unrelated widgets, and restart Plasma.
new_case normal
run_recover "$CASE_DIR" MOCK_SYSTEMCTL_ACTIVE=1
[[ "$(grep -c '^plugin=org.kde.desktopcontainment$' "$CFG")" -eq 2 ]] || fail "Desktop containment was not converted"
! grep -q '^plugin=ua.bezeza.alveus$' "$CFG" || fail "Alveus plugin reference remains"
! grep -Fq '[Containments][1][Applets][3]' "$CFG" || fail "Alveus widget section remains"
assert_file_contains "$CFG" 'plugin=org.kde.plasma.digitalclock'
assert_file_contains "$CFG" 'plugin=org.kde.plasma.folder'
assert_file_contains "$LOG" 'systemctl --user stop plasma-plasmashell.service'
assert_file_contains "$LOG" 'systemctl --user start plasma-plasmashell.service'
assert_backup_matches_original
assert_no_temp_files "$CASE_DIR/config"

# Fallback path: when the systemd user service is not active, use kquitapp6
# and kstart, still preserving the transformed config and backup.
new_case fallback
run_recover "$CASE_DIR" MOCK_SYSTEMCTL_ACTIVE=0
assert_file_contains "$LOG" 'kquitapp6 plasmashell'
assert_file_contains "$LOG" 'sleep 1'
assert_file_contains "$LOG" 'kstart plasmashell'
assert_backup_matches_original
assert_no_temp_files "$CASE_DIR/config"

# AWK failure must leave the live config byte-for-byte unchanged and still
# attempt to restart the shell after it was stopped.
new_case awk-failure
if run_recover "$CASE_DIR" MOCK_SYSTEMCTL_ACTIVE=1 MOCK_AWK_FAIL=1; then
    fail "Expected the AWK failure scenario to return non-zero"
else
    status=$?
    [[ "$status" -eq 42 ]] || fail "Expected AWK exit status 42, got $status"
fi
cmp -s "$CASE_DIR/original" "$CFG" || fail "Live config changed after AWK failure"
assert_file_contains "$LOG" 'systemctl --user start plasma-plasmashell.service'
assert_backup_matches_original
assert_no_temp_files "$CASE_DIR/config"

# Validation failure must also leave the original config untouched.
new_case validation-failure
if run_recover "$CASE_DIR" MOCK_SYSTEMCTL_ACTIVE=1 MOCK_AWK_INVALID=1; then
    fail "Expected invalid AWK output to be rejected"
fi
cmp -s "$CASE_DIR/original" "$CFG" || fail "Live config changed after validation failure"
assert_file_contains "$LOG" 'systemctl --user start plasma-plasmashell.service'
assert_backup_matches_original
assert_no_temp_files "$CASE_DIR/config"

# A termination signal during transformation must run the same restart and
# temporary-file cleanup path. The mock AWK sends SIGTERM to its parent shell.
new_case interrupted
if run_recover "$CASE_DIR" MOCK_SYSTEMCTL_ACTIVE=1 MOCK_AWK_TERM_PARENT=1; then
    fail "Expected the interrupted scenario to return non-zero"
else
    status=$?
    [[ "$status" -eq 143 ]] || fail "Expected SIGTERM exit status 143, got $status"
fi
cmp -s "$CASE_DIR/original" "$CFG" || fail "Live config changed after interruption"
assert_file_contains "$LOG" 'systemctl --user start plasma-plasmashell.service'
assert_backup_matches_original
assert_no_temp_files "$CASE_DIR/config"

printf 'Recovery script tests passed.\n'
