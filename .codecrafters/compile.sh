#!/bin/sh
#
# This script is used to compile your program on CodeCrafters
# 
# This runs before .codecrafters/run.sh
#
# Learn more: https://codecrafters.io/program-interface

# Exit early if any commands fail
set -e

gcc -ggdb -fsanitize=address -Wall -Wpedantic app/*.c -o /tmp/shell-target || \
    gcc -ggdb -Wall -Wpedantic app/*.c -o /tmp/shell-target
