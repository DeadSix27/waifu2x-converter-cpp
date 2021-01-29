FROM debian:buster-slim as builder

LABEL maintainer="Taro Matsuzawa"

WORKDIR /

# apt-get install
RUN set -ex \
    && apt-get update

RUN set -ex \
    && apt-get install -y --no-install-recommends \
      g++ \
      cmake \
      git \
      ca-certificates \
      beignet-opencl-icd \
      mesa-opencl-icd \
      ocl-icd-opencl-dev \
      libopencv-dev

RUN set -ex \
    && apt-get install -y --no-install-recommends \
      build-essential

RUN set -ex \
    && mkdir -p /usr/src \
    && cd /usr/src \
    && git clone https://github.com/DeadSix27/waifu2x-converter-cpp.git \
    && cd waifu2x-converter-cpp \
    && mkdir out \
    && cd out \
    && cmake .. \
    && make -j$(nproc) \
    && make install \
    && cd / \
    && rm -fr /usr/src/waifu2x-converter-cpp

FROM debian:buster-slim

RUN set -ex \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
      beignet-opencl-icd \
      mesa-opencl-icd \
      ocl-icd-libopencl1 \
      libopencv-core3.2 \
      libopencv-imgcodecs3.2 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local /usr/local

# Minimal command line test.
RUN set -ex \
    && ldconfig \
    && waifu2x-converter-cpp -h

COPY docker-entrypoint.sh /usr/local/bin
ENTRYPOINT ["docker-entrypoint.sh"]