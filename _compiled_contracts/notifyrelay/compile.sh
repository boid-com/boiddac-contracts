#!/usr/bin/env bash

source `dirname $BASH_SOURCE`/common.sh
eosio-cpp -o `dirname $BASH_SOURCE`/$CONTRACT.wasm $CONTRACT.cpp -I.
