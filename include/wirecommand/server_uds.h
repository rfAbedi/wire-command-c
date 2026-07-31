#ifndef WIRECOMMAND_SERVER_UDS_H
#define WIRECOMMAND_SERVER_UDS_H

#include <signal.h>

/*
 * Run until stop_requested becomes nonzero. The caller owns socket_path and
 * stop_requested and keeps both valid until this function returns. The server
 * owns every socket it opens and removes the socket path before returning.
 */
int wc_server_uds_run(const char *socket_path,
                      const volatile sig_atomic_t *stop_requested);

#endif
