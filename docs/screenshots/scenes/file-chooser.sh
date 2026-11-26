#!/usr/bin/env bash
# tetra's file chooser, as GTK, Qt and Firefox see it (FileChooser portal).
# wait: 5
set -eu
home="$XDG_RUNTIME_DIR/home" # the portal's HOME in headless runs
dir="$home/Documents"
mkdir -p "$home/Desktop" "$home/Downloads" "$home/Music" "$home/Pictures" "$dir/Invoices" "$dir/Travel"
seq 1 9000 >"$dir/Quarterly report.odt"
seq 1 300 >"$dir/notes.md"
seq 1 40000 >"$dir/budget.ods"
head -c 2400000 /dev/zero >"$dir/scan.pdf"
portal-request open --multiple --folder "$dir" --filter "Documents:*.odt;*.ods;*.pdf;*.md" \
    --filter "All files:*" --title "Attach files" >/dev/null &
sleep 2.5
for _ in 1 2 3; do
    tetra-sendkeys Down
    sleep 0.2
done
sleep 0.3
