#!/bin/sh
#
# Use this script to run your program LOCALLY.
#
# Note: Changing this script WILL NOT affect how CodeCrafters runs your program.
#
# Learn more: https://codecrafters.io/program-interface

set -e # Exit early if any commands fail

# Copied from .codecrafters/compile.sh
#
# - Edit this to change how your program compiles locally
# - Edit .codecrafters/compile.sh to change how your program compiles remotely
(
  cd "$(dirname "$0")" # Ensure compile steps are run within the repository directory
  cc="${CC:-}"
  [ -z "$cc" ] && for compiler in tcc gcc clang; do
      [ -z "$cc" ] && cc=$(command -v $compiler) || continue
      break
  done
  echo "cc = ${cc:-cc}" >&2
  CFLAGS="-Wall -Wextra -Wpedantic -Werror"
  CFLAGS="$CFLAGS -ggdb"
  CFLAGS="$CFLAGS -lcurl -lcrypto"
  CFLAGS="$CFLAGS -Wno-gnu-zero-variadic-macro-arguments"
  tmpdir=$(mktemp -d)
  echo "int main(void) { return 0; }" > "$tmpdir/test.c"
  ${cc:-cc} -fsanitize=address "$tmpdir/test.c" -o "$tmpFile" 2>/dev/null && CFLAGS="${CFLAGS:-} -fsanitize=address"
  rm "$tmpdir/test.c"
  rmdir "$tmpdir"
  echo "cflags = $CFLAGS" >&2
  ${cc:-cc} $CFLAGS app/*.c -o "/tmp/shell-target"
)

# Copied from .codecrafters/run.sh
#
# - Edit this to change how your program runs locally
# - Edit .codecrafters/run.sh to change how your program runs remotely
exec /tmp/shell-target "$@"
