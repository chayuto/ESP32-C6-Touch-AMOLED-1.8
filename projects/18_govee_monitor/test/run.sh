#!/bin/sh
# Host-side unit tests for history.c — no board, no ESP-IDF required.
#
# history.c's ring/rollover logic is driven through its *_at() entry points,
# which take an explicit clock, so a 24 h window can be exercised in
# microseconds instead of waiting a day on hardware. The stub/ headers satisfy
# the handful of ESP-IDF symbols it touches.
#
#   ./run.sh
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -O1 \
   -I"$DIR/stub" -I"$DIR/../main" \
   "$DIR/test_history.c" "$DIR/../main/history.c" \
   -o "$OUT/test_history"

cc -std=c11 -Wall -Wextra -Werror -O1 \
   -I"$DIR/stub" -I"$DIR/../main" \
   "$DIR/test_uuid7.c" "$DIR/../main/uuid7.c" \
   -o "$OUT/test_uuid7"
"$OUT/test_uuid7"
"$OUT/test_history"
