#
# SPDX-FileCopyrightText: Milian Wolff <milian.wolff@kdab.com>
# SPDX-FileCopyrightText: 2024 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
#
# SPDX-License-Identifier: GPL-2.0-or-later
#

#
# NOTE: this relies on the docker image also used by hotspot, see:
#       https://github.com/KDAB/hotspot/tree/master/scripts/appimage
#
FROM ghcr.io/kdab/kdesrc-build:latest

RUN dnf update -y && dnf install -y epel-release && dnf update -y && dnf install -y libunwind-devel zstd
