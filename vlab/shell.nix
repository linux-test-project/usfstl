#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
{ pkgs ? import (builtins.fetchGit {
    name = "nixpkgs-unstable-2019-11-28";
    url = https://github.com/nixos/nixpkgs/;
    rev = "8bb98968edf2769ccf059153ec55f267f13b9afe";
}) {} }:

with pkgs;

multiStdenv.mkDerivation {
    name = "vlab";
    buildInputs = [
        # vlab script
        python3Packages.attrs python3Packages.pyyaml

        # inside of the VMs
        mount cpio ncurses iproute socat openssh busybox

        # wmediumd
        libnl pkg-config libconfig

        # for the pychecks
        mypy python3Packages.pylint

        # for coverage plugin
        lcov
    ];
}
