#ifndef WIRECOMMAND_SERVER_TCP_H
#define WIRECOMMAND_SERVER_TCP_H

#include <signal.h>
#include <stdint.h>

/*
 * Run an IPv4 TCP server with one joined worker per connected client.
 * The caller owns bind_address and stop_requested, and keeps both valid until
 * this function returns.
 */
int wc_server_tcp_run(const char *bind_address, uint16_t port,
                      const volatile sig_atomic_t *stop_requested);

#endif
