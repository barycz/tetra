#!/usr/bin/env bash
# Runs inside tetra with two 1280x720 outputs and a private sound server (PipeWire with
# virtual devices: Speakers and Headphones at 50%, Microphone at 80%).
#   tetra-settings sound
#   Output device (933,160), "Headphones" (829,222) -> the default output
#   Volume slider (1004,223)                         -> 71%
#   Microphone mute (797,379)                        -> microphone muted
#   ask the sound server (pactl) and report it as a notification
#   click the second display's volume icon (2461,695) -> the shell's popup there
# Expected screenshot: Output device "Headphones" at 71%, the microphone
# button crossed out (muted); the notification shows "headphones",
# "Volume: front-left: ... 71% ...", "Mute: yes"; on the right display the
# volume popup shows "Headphones" and 71%.
#
#   scripts/run-headless.sh -a -n 2 -w 7 -- tests/headless/settings-sound.sh
# wait: 7
# outputs: 2
# audio: pipewire
set -eu

tetra-settings sound &
sleep 2
tetra-pointer click 933 160
sleep 0.4
tetra-pointer click 829 222
sleep 0.5
tetra-pointer click 1004 223
sleep 0.4
tetra-pointer click 797 379
sleep 0.6
notify-send "Sound server" "$(pactl get-default-sink); $(pactl get-sink-volume headphones | head -1 | tr -s ' '); $(pactl get-source-mute microphone)"
sleep 0.6
tetra-pointer click 2461 695
sleep 0.5
