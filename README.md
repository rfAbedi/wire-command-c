# WireCommand

WireCommand is an interview-oriented C17/Linux client-server project. It
provides these server versions on one evolving mainline:

- `wirecommand-uds`: a single-threaded, `poll()`-based UNIX-domain server.
- `wirecommand-uds-threaded`: a learning version with one worker per UDS
  client.
- `wirecommand-tcp`: a TCP server with one worker thread per client.

The transports share bounded buffering, binary protocol, command, logging,
and socket utility modules. The code favors direct, small POSIX
implementations that can be understood and explained within the three-day
project schedule.

## Requirements

A Debian-based Linux environment with a C17 compiler, POSIX threads, and
`make` is required. Run `./setup.sh` to check required tools and receive
installation guidance. The script never invokes `sudo`.

## Build and test

```sh
make
make test
make integration-test
make check
make asan
make ubsan
make coverage
make clean
```

`make` builds the client, both UDS versions, and the TCP server. The test
targets run the unit suite and all real-server integration suites. Packaging is
deferred until the final milestone. Sanitizer builds reuse this small Makefile
with sanitizer compiler flags.

## Continuous integration

The `Jenkinsfile` defines CI only; there is no deployment destination. Jenkins
checks out the repository and calls the same Makefile targets used locally for
the normal build, unit tests, integration tests, AddressSanitizer, and
UndefinedBehaviorSanitizer. Each stage has a timeout, failures stop the
pipeline, and build/test logs plus the client and server binaries are archived.

The pipeline always calls `make ci-clean` after archiving available logs. This
keeps Jenkins-specific cleanup in the Makefile without adding compiler commands
to the pipeline.

## UDS server

Start the server with its default socket, or choose a socket and log level:

```sh
./wirecommand-uds
./wirecommand-uds --socket /tmp/example.sock --log-level debug
```

## Manual client

In one terminal, start the server:

```sh
./wirecommand-uds --socket /tmp/wirecommand-demo.sock --log-level debug
```

In another terminal, send commands with the `wirecommand` client:

```sh
./wirecommand --socket /tmp/wirecommand-demo.sock PWD
./wirecommand --socket /tmp/wirecommand-demo.sock LS /tmp
./wirecommand --socket /tmp/wirecommand-demo.sock CAT /etc/hostname
```

The client converts the command to the binary request representation:

```text
PWD request:  00 06  00 02  00 00
              size   type   argument-size
```

It reads the two-byte response length, waits for the complete response, and
writes the response data directly to standard output. It does not add a
newline, which keeps `CAT` safe for binary files. For easier terminal viewing:

```sh
./wirecommand --socket /tmp/wirecommand-demo.sock PWD; echo
```

The same client can connect to the TCP server with `--host` and `--port`.

## Thread-per-client UDS server

The original poll server remains available unchanged. Start the threaded
learning version with:

```sh
./wirecommand-uds-threaded
./wirecommand-uds-threaded --socket /tmp/threaded.sock --log-level debug
```

The main thread accepts connections and starts one worker for each client. A
worker owns that client descriptor and its input/output buffers until it exits.
Requests from one client remain sequential, while different clients can run
commands concurrently.

Workers use `poll()` with a short timeout on their own descriptor. This lets
them notice the synchronized shutdown state without another thread closing
their socket. Only the main thread reads the signal flag. Completed workers are
joined before their fixed table slots are reused, and all remaining workers are
joined during shutdown.

One mutex protects only worker lifecycle fields. It is never held during
socket I/O, command execution, or logging. Logging remains mutex-free as
requested; timestamp conversion uses thread-safe `localtime_r()`.

## Thread-per-client TCP server

The TCP version binds to loopback port 9090 by default. The bind address must
currently be a numeric IPv4 address:

```sh
./wirecommand-tcp
./wirecommand-tcp --bind 0.0.0.0 --port 8080 --log-level debug
```

Manual client examples:

```sh
./wirecommand --host 127.0.0.1 --port 9090 PWD; echo
./wirecommand --host 127.0.0.1 --port 9090 LS /tmp
./wirecommand --host 127.0.0.1 --port 9090 CAT /etc/hostname
```

The main TCP thread owns the listening descriptor and the worker table. Each
accepted client transfers to one worker, which owns the descriptor and its two
bounded buffers until it exits. Requests remain ordered within one connection,
while separate workers execute different clients concurrently.

Completed workers are joined before their slots are reused. On `SIGINT` or
`SIGTERM`, only the main thread handles the signal. It publishes shutdown under
the lifecycle mutex, closes the listener, and joins every worker. Workers poll
with a short timeout so they can observe shutdown without another thread
closing their client descriptor.

The main function installs `SIGINT` and `SIGTERM` handlers, then calls the
server loop. The loop accepts nonblocking clients, uses `poll()` for readiness,
parses complete requests into the FIFO queue, and dispatches them in order.
Each client owns one bounded input buffer and one bounded output buffer.
The shared request queue is also bounded at 256 entries; a client that exceeds
that resource limit is disconnected instead of allowing unbounded allocation.

Malformed input closes only that client. Disconnect cleanup removes that
client's queued requests before closing its descriptor. Shutdown closes every
client and the listener and unlinks the socket path. The signal handler itself
only sets a `sig_atomic_t` flag; cleanup stays in normal program flow.

### Ownership summary

| Resource | Owner | Released by |
| --- | --- | --- |
| Listening descriptor | UDS server loop | Server shutdown cleanup |
| Client descriptor | One client-table entry | `wc_uds_remove_client()` |
| Client input/output buffers | Same client-table entry | `wc_uds_remove_client()` |
| Parsed request view | Client input buffer | Invalid after input consumption |
| Queued request and argument copy | Request queue, then dispatcher | `wc_queued_request_destroy()` |
| Command result data | Server dispatcher | `wc_command_result_destroy()` |

Repeated-connect integration coverage compares `/proc/<server-pid>/fd` before
and after 50 short connections. This checks that client descriptors are not
leaked while the server remains running.

## Logging

Logs are diagnostic output written only to standard error:

```text
[<timestamp>] <LEVEL> <MODULE>: event=<EVENT> key=value ...
```

The available levels are `ERROR`, `WARN`, `INFO`, `DEBUG`, and `TRACE`, and
the default is `INFO`. Each server accepts `--log-level`. Logging is best-effort
and preserves `errno`. It has no application-level mutex as requested. Each
record uses one `fprintf()` call, and timestamp conversion uses `localtime_r()`.

## Architecture

Public interfaces live under `include/wirecommand/` and use the `wc_` prefix.
Core implementations live in `src/`; unit and integration tests are separate.
The protocol layer will have no filesystem or socket access, the command layer
will have no wire or socket access, and server orchestration will connect those
layers. The final project uses C17 and POSIX socket facilities. The UDS release
keeps the deliberately simple shell-backed command module described below.
This is an accepted deviation from the original direct-filesystem-API plan.

Code is kept beginner-readable: functions are short, names describe their
purpose, and internal helpers are `static`. New abstractions are added only
when a current requirement needs them. Each milestone handoff explains the
call flow, unfamiliar C syntax, and why the implementation is no simpler.

## Protocol

The recruitment PDF and the explicit project contract disagree on the request
header. The explicit contract is authoritative.

Requests use this layout:

```text
+----------------+----------------+----------------------+-------------------+
| size (2 bytes) | type (2 bytes) | argument size (2 B)  | argument          |
+----------------+----------------+----------------------+-------------------+
```

The request types are `LS=1`, `PWD=2`, and `CAT=3`. Every integer uses network
byte order. Request size includes the six-byte header, while argument size is
only the number of raw argument bytes. The two lengths must satisfy:

```text
request size = 6 + argument size
```

Responses intentionally have a different layout:

```text
+----------------------+--------------------------+
| data size (2 bytes)  | response data (variable) |
+----------------------+--------------------------+
```

Response size counts only response-data bytes and does not include the two-byte
header. A zero-length response is encoded as `00 00`. Arguments and response
data are not null-terminated and may contain binary bytes.

The assignment defines no response status or error frame. WireCommand therefore
does not add one. If a command error is returned as text, it is ordinary
response data and cannot be distinguished from successful text by examining
the protocol frame alone.

The protocol API supports both directions:

```text
Client sends request:     wc_protocol_encode_request()
Server receives request:  wc_protocol_parse_request()
Server sends response:    wc_protocol_encode_response()
Client receives response: wc_protocol_parse_response()
```

Both parsers return a view into the caller's input buffer. The caller must
process or copy that view before consuming or growing the input buffer. The
request and response encoders remain separate because their headers and length
meanings differ. They share only the private two-byte integer helpers.

## Commands

The command layer uses `popen()`. It runs `ls -1`, `pwd`, and `cat`, captures
bounded standard output, and checks the child exit status. Client path
arguments are single-quoted so shell metacharacters are treated as path bytes.
`CAT` still requires the assignment-defined absolute filename, and its result
may contain binary bytes.

This version depends on the Debian command-line tools and reports a failed
child command as `EIO`; it cannot recover errors such as `ENOENT` from the
child process. That tradeoff keeps the three-day implementation smaller and
has been accepted for this project version.

Each command receives a maximum result size. Oversized output fails with
`EMSGSIZE` instead of being truncated. A successful `struct wc_command_result`
owns its data until `wc_command_result_destroy()` is called. Failures return
`-1` with `errno` and no result allocation. The protocol has no error status,
so server orchestration must decide which ordinary response bytes represent a
command error.

## Request queue

The UDS server uses a typed FIFO queue. Enqueue copies the parsed request
argument into the same allocation as its queue node, so consuming the client's
input buffer cannot invalidate queued work. Dequeue transfers ownership of the
request to the caller, which releases it with `wc_queued_request_destroy()`.

Each request records its client descriptor. When a client disconnects,
`wc_request_queue_discard_client()` removes and frees all work belonging to
that descriptor. The queue itself performs no socket operations and emits no
logs; those actions belong to server orchestration.

## Release plan

- `v0.1.0-uds`: verified poll-based UNIX-domain server.
- `v0.2.0-ci`: Jenkins CI, added immediately after the UDS release.
- `v1.0.0`: verified threaded TCP server and final delivery.

Commits and annotated tags are created only after explicit approval.

The UDS transport is implemented and hardened. With the shell-command
deviation accepted, it is ready to be proposed as `v0.1.0-uds`.
