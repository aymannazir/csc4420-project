#!/bin/bash

FS_FILE="test_fs.img"
SIMFS="./simfs"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Helper function to run a command, evaluate its exit code, and check stdout
run_test() {
    local test_name="$1"
    local expected_exit="$2"
    local expected_stdout="$3"
    local cmd_input="$4"  
    shift 4
    local cmd=("$@")

    echo -n "Running: $test_name ... "

    if [ -n "$cmd_input" ]; then
        echo -n "$cmd_input" | "${cmd[@]}" > tmp_stdout 2> tmp_stderr
    else
        "${cmd[@]}" > tmp_stdout 2> tmp_stderr
    fi
    local actual_exit=$?
    local actual_stdout=$(cat tmp_stdout)

    if [ "$actual_exit" -ne "$expected_exit" ]; then
        echo -e "${RED}FAILED${NC} (Exit code mismatch)"
        echo "  Command: ${cmd[*]}"
        echo "  Expected Exit Code: $expected_exit, Got: $actual_exit"
        echo "  Stderr output: $(cat tmp_stderr)"
        return 1
    fi

    if [ "$expected_stdout" != "IGNORE" ] && [ "$actual_stdout" != "$expected_stdout" ]; then
        echo -e "${RED}FAILED${NC} (Output mismatch)"
        echo "  Command: ${cmd[*]}"
        echo "  Expected Output: '$expected_stdout'"
        echo "  Actual Output: '$actual_stdout'"
        return 1
    fi

    echo -e "${GREEN}PASSED${NC}"
    return 0
}

# --- Initialization ---
echo "Initializing fresh file system..."
$SIMFS -f $FS_FILE initfs > /dev/null 2>&1

# --- 1) Testing file name lengths ---
#run_test "Create valid file" 0 "IGNORE" "" $SIMFS -f $FS_FILE createfile "shortname1"
run_test "[1] Create valid file" 0 "IGNORE" "" $SIMFS -f $FS_FILE createfile "shortname"
run_test "[2] Create exactly 11 char file" 0 "IGNORE" "" $SIMFS -f $FS_FILE createfile "12345678901"
run_test "[3] Create too long file (>11)" 1 "IGNORE" "" $SIMFS -f $FS_FILE createfile "thisnameistoolong" 

# --- 2) Keep creating files until out of space (MAXFILES) ---
echo "Filling up file slots..."
for i in {1..6}; do
    $SIMFS -f $FS_FILE createfile "fill$i" > /dev/null 2>&1
done
run_test "[4] Create file exceeding MAXFILES" 1 "IGNORE" "" $SIMFS -f $FS_FILE createfile "toomany"

# --- 3) Delete files and check again ---
run_test "[5] Delete a file to free space" 0 "IGNORE" "" $SIMFS -f $FS_FILE deletefile "fill1"
run_test "[6] Create file after deletion" 0 "IGNORE" "" $SIMFS -f $FS_FILE createfile "newfile"

# --- 4) Read/Write where offset is bigger than length ---
run_test "[7] Write with offset > file size" 1 "IGNORE" "data" $SIMFS -f $FS_FILE writefile "shortname" 10 4
run_test "[8] Read with offset > file size" 1 "IGNORE" "" $SIMFS -f $FS_FILE readfile "shortname" 10 4

# --- 5) Valid Read/Write matching expectations ---
run_test "[9] Valid write to file" 0 "IGNORE" "Hello, World!" $SIMFS -f $FS_FILE writefile "shortname" 0 13
run_test "[10] Valid read from file" 0 "Hello, World!" "" $SIMFS -f $FS_FILE readfile "shortname" 0 13

# --- 6) Overwriting part of the file and verifying ---
run_test "[11] Overwrite middle of file" 0 "IGNORE" "Unix!" $SIMFS -f $FS_FILE writefile "shortname" 7 5
run_test "[12] Verify overwritten file" 0 "Hello, Unix!!" "" $SIMFS -f $FS_FILE readfile "shortname" 0 13

# --- 8) The "All or Nothing" Atomic Write ---
# Adjust the length (e.g., 9999) to ensure it exceeds your test system's MAXBLOCKS capacity.
run_test "[13] Atomic write exceeding capacity" 1 "IGNORE" "giant_data_string..." $SIMFS -f $FS_FILE writefile "shortname" 0 99999

# --- 9) Non-existent file tests ---
run_test "[14] Delete non-existent file" 1 "IGNORE" "" $SIMFS -f $FS_FILE deletefile "ghostfile"
run_test "[15] Read non-existent file" 1 "IGNORE" "" $SIMFS -f $FS_FILE readfile "ghostfile" 0 5

# --- Cleanup ---
rm -f tmp_stdout tmp_stderr
echo "Testing complete."
