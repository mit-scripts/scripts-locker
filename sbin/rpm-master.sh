#!/bin/bash
set -eu -o pipefail

export LC_ALL=C

echo "Entering correct directory..."
mkdir -p /mit/scripts/cron_scripts/rpm-sync/
cd /mit/scripts/cron_scripts/rpm-sync/

echo "Cleaning up environment..."
rm -rf ./*.rpmlist ./*.diff rpmlist.master missing.rpms

servers=$(finger @scripts-director.mit.edu | sed -n '/^FWM  2 /, /^[^ ]/ s/^  -> \([^:]*\):.*/\1/p')

for server in $servers; do
    echo "Connecting to $server..."
    { ssh "$USER@$server" /mit/scripts/sbin/rpmlist.sh 2>&1 >&3 | { grep -Fxv 'If you have trouble logging in, see http://scripts.mit.edu/faq/41/.' || [ $? -eq 1 ]; }; } 3>&1 >&2
done

echo "Creating master package list..."
cat ./*.rpmlist | sort | uniq > rpmlist.master

echo "Comparing scripts servers to overall rpm list..."
touch missing.rpms
for server in *.rpmlist; do
    diff -U3 "$server" rpmlist.master > "$server.diff" || :
    serverPretty=$(basename "$server" .rpmlist)
    echo "Server $serverPretty is missing:" >> missing.rpms
    grep "^+[^+]" "$server.diff" | cut -b 1 --complement >> missing.rpms
    echo >> missing.rpms
done

if egrep -qv '(missing)|(^$)' missing.rpms; then
    echo "Sending email..."
    mail -s "scripts.mit.edu servers are out of sync" root@scripts.mit.edu < missing.rpms
else
    echo "No email needs to be sent! scripts.mit.edu is up to date."
fi
