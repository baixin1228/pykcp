#!/bin/bash
sudo meson build
sudo meson build --reconfigure
sudo meson test -C build --verbose $*
