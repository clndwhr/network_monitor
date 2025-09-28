# Multi-arch build for armv7 and aarch64_cortex-a53
# Ubuntu 20.04 has library versions compatible with the RM520
#FROM arm32v7/ubuntu:20.04
FROM --platform=linux/arm/v6 ghcr.io/natecarlson/rm520-modem-buildenv:main AS build-armv7


# ---- Builder for armv7 ----
#FROM --platform=linux/arm/v7 debian:bullseye AS build-armv7

# Add apt config to only install direct requirements
#COPY 99minimal-apt-installs /etc/apt/apt.conf.d/
ADD entry-point.sh /opt/entry-point.sh

# RUN apt-get update && \
#     apt-get install -y build-essential linux-headers-armhf-cross gcc-arm-linux-gnueabihf make


RUN mkdir -p /opt/builds && mkdir -p /opt/rm520 \
        && cd /opt/rm520 \
        && git clone https://github.com/clndwhr/sms_tool.git \
        && cd sms_tool/for_modem_AP \
        && sed -i -e "s/VERSION .*/VERSION \"$(date +%Y.%-m).${version}-APmod-iamromulan\"/" sms_main.c \
        && sed -i -e "s/sms_tool .* AP mod by iamromulan/sms_tool $(date +%Y.%-m).${version} AP mod by iamromulan/" sms_main.c \
        && make \
        && chmod 755 /opt/entry-point.sh \
        && chown 1000:1000 /opt/entry-point.sh \
        && mv /opt/rm520/sms_tool/for_modem_AP/sms_tool /opt/rm520/sms_tool/for_modem_AP/sms_tool-${buildTarget}

ENV LC_ALL=en_US.UTF-8

#CMD ["bash"]
ENTRYPOINT ["bash", "/opt/entry-point.sh"]


WORKDIR /src
COPY . .

RUN make clean
RUN make CC=arm-linux-gnueabihf-gcc

# ---- Builder for aarch64_cortex-a53 ----
FROM --platform=linux/arm64 debian:bullseye AS build-arm64

RUN apt-get update && \
    apt-get install -y build-essential linux-headers-arm64-cross gcc-aarch64-linux-gnu make

WORKDIR /src
COPY . .

RUN make clean
RUN make CC=aarch64-linux-gnu-gcc

# ---- Output stage (example: armv7) ----
FROM debian:bullseye AS output
WORKDIR /out

# Change to build-arm64 for aarch64_cortex-a53 output
COPY --from=build-armv7 /src/netmon /out/netmon-armv7
COPY --from=build-armv7 /src/netmon_proc.ko /out/netmon_proc-armv7.ko

COPY --from=build-arm64 /src/netmon /out/netmon-aarch64
COPY --from=build-arm64 /src/netmon_proc.ko /out/netmon_proc-aarch64.ko

# Entrypoint for demonstration (not required)
CMD ["ls", "-l", "/out"]