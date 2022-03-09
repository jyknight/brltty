#!/bin/bash

installTree="$(dirname "${BASH_SOURCE[0]}")"
installTree="$(realpath --no-symlinks -- "${installTree}")"
readonly installTree="$(dirname "${installTree}")"
. "${installTree}/bin/brltty-prologue.sh"

readonly ldConfigurationFile="/etc/ld.so.conf.d/brlapi.conf"
readonly apiLinksDirectory="${installTree}/lib"
readonly canuteSystemdUnit="brltty@canute.path"

brlttyPackageProperty() {
   local resultVariable="${1}"
   local name="${2}"

   local result
   result="$(PKG_CONFIG_PATH="${installTree}/lib/pkgconfig" pkg-config "--variable=${name}" -- brltty)" || return 1

   setVariable "${resultVariable}" "${result}"
   return 0
}

canuteSetServer() {
   export BRLAPI_HOST=":canute"
}

