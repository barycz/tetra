#!/usr/bin/env bash
# The login screen (tetra --greeter for greetd), with made-up users and a
# fake greetd; a password half typed.
# greeter: yes
# logind: fake
# wait: 3
set -eu
sleep 1.5
tetra-sendkeys --type 'tetra-te'
