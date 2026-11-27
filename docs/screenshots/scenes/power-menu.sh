#!/usr/bin/env bash
# The launcher's power menu (logind is a fake that offers every action).
# logind: fake
# wait: 4
set -eu
sleep 1.2
tetra-sendkeys super
sleep 0.8
tetra-pointer click 895 636
sleep 0.4
tetra-pointer move 840 564
