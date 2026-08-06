#!/usr/bin/env bash

set -uo pipefail

request_count=1000
concurrency=64
request_timeout=30
project_dir="$(pwd -P)"
keep_results=0

usage() {
    cat <<'EOF'
Usage: wirecommand_load_test.sh [OPTIONS]

Stress-test wirecommand-uds with many concurrent PWD clients.

Options:
  -p, --project-dir DIR   Directory containing the WireCommand binaries
  -n, --requests N        Total requests (default: 1000)
  -c, --concurrency N     Simultaneous clients, maximum 64 (default: 64)
  -t, --timeout SECONDS   Timeout for each client request (default: 30)
      --keep-results      Keep responses and logs after a successful test
  -h, --help              Show this help

Example:
  ./wirecommand_load_test.sh -p ./wirecommand-1.0.0
EOF
}

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 2
}

is_positive_integer() {
    [[ $1 =~ ^[1-9][0-9]*$ ]]
}

while (($# > 0)); do
    case $1 in
        -p|--project-dir)
            (($# >= 2)) || die "$1 requires a directory"
            project_dir=$2
            shift 2
            ;;
        -n|--requests)
            (($# >= 2)) || die "$1 requires a number"
            request_count=$2
            shift 2
            ;;
        -c|--concurrency)
            (($# >= 2)) || die "$1 requires a number"
            concurrency=$2
            shift 2
            ;;
        -t|--timeout)
            (($# >= 2)) || die "$1 requires a number"
            request_timeout=$2
            shift 2
            ;;
        --keep-results)
            keep_results=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

is_positive_integer "$request_count" || die "requests must be a positive integer"
is_positive_integer "$concurrency" || die "concurrency must be a positive integer"
is_positive_integer "$request_timeout" || die "timeout must be a positive integer"
((concurrency <= 64)) || die "concurrency must not exceed the server limit of 64 clients"

project_dir=$(cd -- "$project_dir" 2>/dev/null && pwd -P) ||
    die "project directory does not exist"

server_bin="$project_dir/wirecommand-uds"
client_bin="$project_dir/wirecommand"

[[ -x $server_bin ]] || die "missing executable: $server_bin (run make first)"
[[ -x $client_bin ]] || die "missing executable: $client_bin (run make first)"
command -v timeout >/dev/null 2>&1 || die "timeout command is required"
command -v xargs >/dev/null 2>&1 || die "xargs command is required"

temporary_base=${TMPDIR:-/tmp}
if [[ ! -d $temporary_base || ! -w $temporary_base ]]; then
    temporary_base=$project_dir
fi

run_dir=$(mktemp -d "$temporary_base/wirecommand-load.XXXXXX") ||
    die "could not create a temporary results directory"
touch "$run_dir/.wirecommand-load-test"

socket_path="$run_dir/server.sock"
server_log="$run_dir/server.log"
server_pid=''
test_passed=0

stop_server() {
    local attempt

    [[ -n $server_pid ]] || return 0
    kill -0 "$server_pid" 2>/dev/null || return 0

    kill -INT "$server_pid" 2>/dev/null || true
    for attempt in {1..50}; do
        kill -0 "$server_pid" 2>/dev/null || break
        sleep 0.1
    done

    if kill -0 "$server_pid" 2>/dev/null; then
        kill -TERM "$server_pid" 2>/dev/null || true
    fi
    wait "$server_pid" 2>/dev/null || true
}

cleanup() {
    stop_server
    rm -f -- "$socket_path"

    if ((test_passed == 1 && keep_results == 0)); then
        if [[ -f $run_dir/.wirecommand-load-test ]]; then
            rm -rf -- "$run_dir"
        fi
    else
        printf 'Results kept in: %s\n' "$run_dir"
    fi
}

trap cleanup EXIT
trap 'exit 130' INT TERM

(
    cd -- "$project_dir" || exit 1
    exec "$server_bin" --socket "$socket_path" --log-level info
) >"$server_log" 2>&1 &
server_pid=$!

server_ready=0
for _ in {1..100}; do
    if [[ -S $socket_path ]]; then
        server_ready=1
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        printf 'Server exited during startup. Log:\n' >&2
        sed -n '1,120p' "$server_log" >&2
        exit 1
    fi
    sleep 0.05
done

if ((server_ready == 0)); then
    printf 'ERROR: server socket was not ready within 5 seconds\n' >&2
    exit 1
fi

# wc_command_pwd() executes "pwd", whose output includes a trailing newline.
(
    cd -- "$project_dir" || exit 1
    printf '%s' "$project_dir"
) >"$run_dir/expected"

export client_bin socket_path request_timeout run_dir

printf 'Running %d requests with %d concurrent clients...\n' \
    "$request_count" "$concurrency"

seq 1 "$request_count" | xargs -P "$concurrency" -I{} bash -c '
    request_id=$1
    response_file="$run_dir/response.$request_id"
    error_file="$run_dir/error.$request_id"
    status_file="$run_dir/status.$request_id"

    if timeout "${request_timeout}s" \
        "$client_bin" --socket "$socket_path" PWD \
        >"$response_file" 2>"$error_file"
    then
        printf "PASS\n" >"$status_file"
    else
        exit_code=$?
        printf "FAIL %d\n" "$exit_code" >"$status_file"
    fi
' _ {}

completed=0
failed=0
incorrect=0

for request_id in $(seq 1 "$request_count"); do
    status_file="$run_dir/status.$request_id"
    response_file="$run_dir/response.$request_id"

    if [[ -f $status_file ]]; then
        ((completed += 1))
        if ! grep -qx 'PASS' "$status_file"; then
            ((failed += 1))
        fi
    else
        ((failed += 1))
    fi

    if [[ ! -f $response_file ]] ||
       ! cmp -s "$run_dir/expected" "$response_file"; then
        ((incorrect += 1))
    fi
done

post_load='FAIL'
if timeout "${request_timeout}s" \
    "$client_bin" --socket "$socket_path" PWD \
    >"$run_dir/post-load.response" 2>"$run_dir/post-load.error" &&
   cmp -s "$run_dir/expected" "$run_dir/post-load.response"
then
    post_load='PASS'
fi

server_alive='NO'
if kill -0 "$server_pid" 2>/dev/null; then
    server_alive='YES'
fi

queue_full_events=0
if [[ -f $server_log ]]; then
    queue_full_events=$(grep -c 'request_queue_full' "$server_log" || true)
fi

printf '\nCompleted:           %d/%d\n' "$completed" "$request_count"
printf 'Failed clients:      %d\n' "$failed"
printf 'Incorrect responses: %d\n' "$incorrect"
printf 'Queue-full events:   %d\n' "$queue_full_events"
printf 'Server still alive:  %s\n' "$server_alive"
printf 'Post-load request:   %s\n' "$post_load"

if ((completed == request_count &&
     failed == 0 &&
     incorrect == 0 &&
     queue_full_events == 0)) &&
   [[ $server_alive == YES && $post_load == PASS ]]
then
    test_passed=1
    printf '\nRESULT: PASS\n'
    exit 0
fi

printf '\nRESULT: FAIL\n' >&2
exit 1

