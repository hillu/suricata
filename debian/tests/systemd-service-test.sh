#!/bin/sh

set -ex

SERVICE="suricata.service"
ETC_SERVICE_FILE="/etc/systemd/system/${SERVICE}"
LIB_SERVICE_FILE="/lib/systemd/system/${SERVICE}"
CONFIG_FILE="/etc/suricata/suricata.yaml"

if [ ! -r "$LIB_SERVICE_FILE" ] ; then
	: ERROR unable to read $LIB_SERVICE_FILE
	exit 1
fi
if [ ! -w "$CONFIG_FILE" ] ; then
	: ERROR unable to write to $CONFIG_FILE
	exit 1
fi

systemctl_action()
{
	if ! systemctl $1 $SERVICE ; then
		journalctl -u $SERVICE
		return 1
	fi
	return 0
}

echo "
%YAML 1.1
---
default-rule-path: /etc/suricata/rules
rule-files:
 - tor.rules
 - http-events.rules
 - smtp-events.rules
 - dns-events.rules
 - tls-events.rules
classification-file: /etc/suricata/classification.config
reference-config-file: /etc/suricata/reference.config
default-log-dir: /var/log/suricata/
af-packet:
  - interface: lo
    cluster-id: 99
    cluster-type: cluster_flow
    defrag: yes
  - interface: default
    tpacket-v3: yes
    block-size: 131072
app-layer:
  protocols:
    ssh:
      enabled: yes
host-mode: auto
unix-command:
  enabled: yes
  filename: /var/run/suricata-command.socket
detect:
  profile: medium
  custom-values:
    toclient-groups: 3
    toserver-groups: 25
  sgh-mpm-context: auto
  inspection-recursion-limit: 3000
  grouping:
  profiling:
    grouping:
      dump-to-disk: false
      include-rules: false
      include-mpm-stats: false
mpm-algo: auto
spm-algo: auto
" > $CONFIG_FILE

#
# before start, package installation may start the daemon
#
if systemctl -q is-active $SERVICE ; then
	: WARNING initial service running, stopping now
	if ! systemctl_action stop ; then
		: ERROR cant stop initial service
		exit 1
	fi
fi

#
# First run of the daemon and basic checks
#
if ! systemctl_action start ; then
	: ERROR cant start the service
	exit 1
fi
sleep 10 # wait for service startup
systemctl status $SERVICE

#
# Restart the daemon
#
if ! systemctl_action restart ; then
	: ERROR unable to restart the service
	exit 1
fi

sleep 10 # wait for serive startup
if ! systemctl -q is-active $SERVICE ; then
	journalctl -u $SERVICE
	: ERROR service not active after restart
	exit 1
fi

#
# Reload the daemon
#
if ! systemctl_action reload ; then
	: ERROR unable to reload the service
	exit 1
fi

sleep 10 # wait for service reload
if ! systemctl -q is-active $SERVICE ; then
	journalctl -u $SERVICE
	: ERROR service not active after reload
	exit 1
fi

: INFO all tests OK
exit 0
