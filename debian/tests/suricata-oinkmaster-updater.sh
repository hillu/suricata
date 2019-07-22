#!/bin/sh

curl -m 30 -fSs 'https://rules.emergingthreats.net' > /dev/null || exit 77
suricata-oinkmaster-updater