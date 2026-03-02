ARG UBUNTU_VERSION=25.10
FROM ubuntu:${UBUNTU_VERSION}

ARG NATIVE_BINARY=.pio/build/native/program

RUN apt-get update \
    && apt-get install -y --no-install-recommends socat ca-certificates libavahi-client3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY ${NATIVE_BINARY} /usr/local/bin/openevse
COPY docker/native/entrypoint.sh /entrypoint.sh

RUN chmod +x /usr/local/bin/openevse /entrypoint.sh

ENV RAPI_PTY_PATH=/tmp/rapi_pty
ENV EMULATOR_HOST=localhost
ENV EMULATOR_PORT=8023

ENTRYPOINT ["/entrypoint.sh"]
CMD []
