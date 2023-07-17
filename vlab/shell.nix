#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
{ pkgs ? import (builtins.fetchGit {
    name = "nixpkgs-release-22.11";
    url = https://github.com/nixos/nixpkgs/;
    ref = "refs/heads/release-22.11";
}) {} }:

let
  types-PyYAML = pkgs.python3Packages.buildPythonPackage rec {
    pname = "types-PyYAML";
    version = "6.0.7";
    format = "setuptools";

    src = pkgs.python3Packages.fetchPypi {
      inherit pname version;
      sha256 = "1vvxzgrajynrg0wyyy9nhcx7y6czqgika3q5msm3dn4m8ps0qj2r";
    };

    # Modules doesn't have tests
    doCheck = false;
  };
in
with pkgs;

multiStdenv.mkDerivation {
    name = "vlab";
    buildInputs = [
        # vlab script
        python3Packages.pyyaml

        # inside of the VMs
        rsyslog-light mount cpio ncurses iproute socat openssh busybox

        # wmediumd
        libnl pkg-config libconfig

        # for the pychecks
        mypy python3Packages.pylint

        # for coverage plugin
        lcov

        # for pychecks
        types-PyYAML
    ];
}
