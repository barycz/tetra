#!/usr/bin/env bash
# Runs inside tetra at 1280x720. Before an app takes a screenshot without
# the user picking it, xdg-desktop-portal asks through tetra-portal's
# access dialog (org.freedesktop.impl.portal.Access).
# Expected screenshot: a small window asking whether to allow taking
# screenshots, with a question icon, Deny and Allow (accent).
#
#   scripts/run-headless.sh -w 5 -- tests/headless/portal-access.sh
# wait: 5
set -eu

portal-request screenshot &
sleep 3
