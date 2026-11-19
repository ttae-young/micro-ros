#!/usr/bin/env bash
set -eo pipefail
bridge_host="${1:?Windows bridge IPv4 address is required}"
link_pid=''
agent_pid=''
cleanup() {
    trap - EXIT INT TERM
    if [[ -n "$agent_pid" ]]; then kill "$agent_pid" 2>/dev/null || true; wait "$agent_pid" 2>/dev/null || true; fi
    if [[ -n "$link_pid" ]]; then kill "$link_pid" 2>/dev/null || true; wait "$link_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
if [[ -e /tmp/ttyMICROROS ]]; then
    echo 'An existing /tmp/ttyMICROROS link is active. Stop the old link/Agent session first.' >&2
    exit 1
fi
source /opt/ros/humble/setup.bash
source "$HOME/microros_agent_ws/install/local_setup.bash"
socat -d -d pty,link=/tmp/ttyMICROROS,raw,echo=0 "tcp:${bridge_host}:8888" &
link_pid=$!
ready=0
for ((i=0; i<100; i++)); do
    if ! kill -0 "$link_pid" 2>/dev/null; then echo 'Serial link failed.' >&2; exit 1; fi
    if [[ -e /tmp/ttyMICROROS ]]; then ready=1; break; fi
    sleep 0.1
done
if [[ $ready != 1 ]]; then echo 'Serial link startup timeout.' >&2; exit 1; fi
ros2 run micro_ros_agent micro_ros_agent serial --dev /tmp/ttyMICROROS -b 115200 -v4 &
agent_pid=$!
# If either process exits, clean up the other one too.
wait -n "$link_pid" "$agent_pid"
