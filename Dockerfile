# Multi-arch build for armv7 and aarch64_cortex-a53
# Ubuntu 20.04 has library versions compatible with the RM520
# ---- Builder for armv7 ----
#FROM arm32v7/ubuntu:20.04
FROM ghcr.io/natecarlson/rm520-modem-buildenv:main


#FROM --platform=linux/arm/v7 debian:bullseye AS build-armv7

# Add apt config to only install direct requirements
#COPY 99minimal-apt-installs /etc/apt/apt.conf.d/
# ADD entry-point.sh /opt/entry-point.sh

RUN mkdir -p /opt/builds
RUN mkdir -p /opt/rm520
WORKDIR /opt/rm520
RUN pwd
RUN git clone https://github.com/clndwhr/network_monitor.git
RUN pwd && ls -lthra
# RUN cd network_monitor \
#         && make \
#         && chmod 755 /opt/entry-point.sh \
#         && chown 1000:1000 /opt/entry-point.sh 
# RUN pwd && ls -lthra
# RUN mv /opt/rm520/network_monitor/ /opt/rm520/network_monitor/network_monitor-${buildTarget}

# ENV LC_ALL=en_US.UTF-8

CMD ["bash"]
# ENTRYPOINT ["bash", "/opt/entry-point.sh"]

# # ---- Builder for aarch64_cortex-a53 ----
# FROM --platform=linux/arm64 debian:bullseye AS build-arm64

# RUN apt-get update && \
#     apt-get install -y build-essential linux-headers-arm64-cross gcc-aarch64-linux-gnu make

# WORKDIR /src
# COPY . .

# RUN make clean
# RUN make CC=aarch64-linux-gnu-gcc

# # ---- Output stage (example: armv7) ----
# FROM debian:bullseye AS output
# WORKDIR /out

# # Change to build-arm64 for aarch64_cortex-a53 output
# COPY --from=build-armv7 /src/netmon /out/netmon-armv7
# COPY --from=build-armv7 /src/netmon_proc.ko /out/netmon_proc-armv7.ko

# COPY --from=build-arm64 /src/netmon /out/netmon-aarch64
# COPY --from=build-arm64 /src/netmon_proc.ko /out/netmon_proc-aarch64.ko

# # Entrypoint for demonstration (not required)
# CMD ["ls", "-l", "/out"]