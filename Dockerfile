# Palm OS toolchain: prc-tools-remix (m68k-palmos-gcc), PilRC, Palm OS SDKs.
FROM --platform=linux/amd64 debian:bookworm

ARG PRC_TOOLS=https://github.com/jichu4n/prc-tools-remix/releases/download/v2.3.6/prc-tools-remix_2.3.6.bookworm_amd64.deb
ARG PILRC=https://github.com/jichu4n/pilrc/releases/download/2.3.91/pilrc_3.2.91.bookworm_amd64.deb
ARG SDKS=https://github.com/jichu4n/palm-os-sdk/archive/master.zip

RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl unzip make \
 && cd /tmp \
 && curl -sSLo prc-tools.deb "$PRC_TOOLS" \
 && curl -sSLo pilrc.deb "$PILRC" \
 && apt-get install -y --no-install-recommends ./prc-tools.deb ./pilrc.deb \
 && curl -sSLo sdk.zip "$SDKS" \
 && unzip -q sdk.zip \
 && mv palm-os-sdk-master /opt/palmdev \
 && palmdev-prep -d sdk-4 \
 && rm -rf /tmp/* /var/lib/apt/lists/*

WORKDIR /src
CMD ["make"]
