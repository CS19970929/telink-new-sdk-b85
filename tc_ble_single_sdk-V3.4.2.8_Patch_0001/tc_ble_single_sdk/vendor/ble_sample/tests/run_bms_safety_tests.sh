#!/bin/sh
set -eu

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SRC_DIR=$(CDPATH= cd -- "$TEST_DIR/.." && pwd)
BUILD_DIR=${TMPDIR:-/tmp}/bms_safety_host_test
mkdir -p "$BUILD_DIR"

COMMON_SRC="$SRC_DIR/bms_measurement.c $SRC_DIR/bms_actuator.c \
$SRC_DIR/bms_sw_protection.c $SRC_DIR/bms_diagnostics.c \
$SRC_DIR/bms_fet_monitor.c $SRC_DIR/bms_fuse.c $SRC_DIR/bms_supervisor.c"
COMMON_FLAGS="-std=c99 -Wall -Wextra -Werror -pedantic -I$SRC_DIR \
-DBMS_POWER_PATH_HW_VERIFIED_ENABLE=1"

cc $COMMON_FLAGS $COMMON_SRC "$TEST_DIR/bms_safety_host_test.c" \
  -o "$BUILD_DIR/default_test"
"$BUILD_DIR/default_test"

cc $COMMON_FLAGS -DBMS_FUSE_AUTO_TRIGGER_ENABLE=1 \
  -DBMS_FUSE_HW_FEEDBACK_ENABLE=1 -DBMS_FUSE_MAX_PULSE_MS=100 \
  -DTEST_FUSE_AUTO=1 $COMMON_SRC "$TEST_DIR/bms_safety_host_test.c" \
  -o "$BUILD_DIR/fuse_test"
"$BUILD_DIR/fuse_test"
