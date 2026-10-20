#!/usr/bin/env bash
# Lua event hooks (arc.*.lua): all the documented events fire in order.
# Usage: lua.sh <arc-ex>
set -euo pipefail

arc_ex=$(realpath "$1")

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fa-lua.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"

cat >arc.test.lua <<'LUA'
local function log(line)
  local f = io.open("events.log", "a")
  f:write(line .. "\n")
  f:close()
end
onProgramStart(function(p) log("ProgramStart") end)
onCommandStart(function(p) log("CommandStart:" .. tostring(p.command)) end)
onArchiveStart(function(p) log("ArchiveStart:" .. tostring(p.arcname)) end)
onArchiveDone(function(p) log("ArchiveDone") end)
onCommandDone(function(p) log("CommandDone") end)
onProgramDone(function(p) log("ProgramDone:" .. tostring(p.exitcode)) end)
LUA

mkdir src
printf 'x\n' >src/a.txt
"$arc_ex" a test.arc src -mlzma >/dev/null
"$arc_ex" t test.arc >/dev/null

expected="ProgramStart
CommandStart:a
ArchiveStart:test.arc
ArchiveDone
CommandDone
ProgramDone:0
ProgramStart
ProgramDone:0"
actual=$(tr -d "\r" < events.log)
if [ "$actual" != "$expected" ]; then
    echo "FAIL: events differ"
    echo "--- expected ---"; echo "$expected"
    echo "--- actual ---"; echo "$actual"
    exit 1
fi

echo "lua OK"
