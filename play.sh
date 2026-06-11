#!/usr/bin/env bash
# Launch OpenOMF (OMF 2097 remake).
# The binary resolves resources from ./build/resources relative to its own
# location, where both the OpenOMF resources and the original OMF2097 data
# files live.
set -euo pipefail
cd "$(dirname "$0")/build"
exec ./openomf "$@"
