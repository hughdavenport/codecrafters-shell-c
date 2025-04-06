#!/bin/sh
#
# This script is used to run your program on CodeCrafters
# 
# This runs after .codecrafters/compile.sh
#
# Learn more: https://codecrafters.io/program-interface

# Exit early if any commands fail
set -e

/tmp/shell-target "$@"
ret=$?
find /tmp -name 'custom_exe*' | grep '' && echo "Return code $ret"
exit $ret
