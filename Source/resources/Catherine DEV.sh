#!/bin/sh
exec "$(dirname "$(readlink -f "$0")")/catherine" --dev-menu "$@"
