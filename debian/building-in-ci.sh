#!/bin/bash

# this script prints 'true' if any ancestor process name is any of $REGEXPS

REGEXPS="debci autopkgtest adt"

set -e

walk()
{
	pid=$1

	[ ! -r /proc/$pid/cmdline ] && exit 1

	name=$(ps -p $pid -o cmd | tail -1)
	for exp in $REGEXPS
	do
		if grep -e $exp <<< $name >/dev/null ; then
			echo true
			exit
		fi
	done

	ppid=$(ps -o ppid= $pid | tr -d ' ')
	walk $ppid
}

walk $$
