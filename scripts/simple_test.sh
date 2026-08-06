#!/usr/bin/env bash

seq 1 5 | xargs -P 10 -I{} bash -c '
    ./wirecommand --socket /tmp/wirecommand.sock LS /tmp
' _ {}