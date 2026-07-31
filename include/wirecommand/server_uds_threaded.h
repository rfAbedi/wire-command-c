#ifndef WIRECOMMAND_SERVER_UDS_THREADED_H
#define WIRECOMMAND_SERVER_UDS_THREADED_H

#include <signal.h>

/*
 * Run a UDS server with one joined worker thread per connected client.
 * The caller keeps ownership of socket_path and stop_requested. Both values
 * must remain valid until this function returns.
 */
int wc_server_uds_threaded_run(
    const char *socket_path,
    const volatile sig_atomic_t *stop_requested);

#endif
