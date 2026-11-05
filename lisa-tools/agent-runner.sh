#!/bin/bash
# Lisa agent job runner. Executes job scripts dropped into agent-queue/*.job,
# writes output to *.out and exit code to *.rc. Stop with Ctrl-C.
# Only jobs written by the assistant into this project folder are executed.
Q="$(cd "$(dirname "$0")" && pwd)/agent-queue"
mkdir -p "$Q"
export PATH="/opt/homebrew/bin:/usr/local/bin:$HOME/Library/Android/sdk/platform-tools:$PATH"
echo "Lisa agent runner watching $Q (pid $$)"
while true; do
  for j in "$Q"/*.job; do
    [ -e "$j" ] || continue
    b="${j%.job}"
    mv "$j" "$b.running"
    echo "[$(date +%H:%M:%S)] running $(basename "$b")"
    ( cd "$Q/../.." && bash "$b.running" ) > "$b.out" 2>&1
    echo $? > "$b.rc"
    mv "$b.running" "$b.done"
    echo "[$(date +%H:%M:%S)] finished $(basename "$b") rc=$(cat "$b.rc")"
  done
  sleep 1
done
