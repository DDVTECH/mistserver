ARG	STRIP_BINARIES="true"
ARG	BUILD_VERSION="Unknown"
ARG	BUILD_TARGET

FROM	ubuntu:20.04	as	mist-base

ENV	DEBIAN_FRONTEND=noninteractive

WORKDIR	/src

RUN	apt update -yq \
	&& apt install -yqq build-essential cmake git

RUN	git clone https://github.com/cisco/libsrtp.git \
	&& mkdir -p libsrtp/build \
	&& cd libsrtp/build \
	&& cmake -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=/src/compiled -D CMAKE_C_FLAGS="-fPIC" .. \
	&& make -j$(nproc) install \
	&& cd /src \
	&& git clone -b dtls_srtp_support --depth=1 https://github.com/livepeer/mbedtls.git \
	&& cd mbedtls \
	&& mkdir build \
	&& cd build \
	&& cmake -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=/src/compiled -D CMAKE_C_FLAGS="-fPIC" .. \
	&& make -j$(nproc) install \
	&& cd /src \
	&& git clone https://github.com/Haivision/srt.git \
	&& mkdir -p srt/build \
	&& cd srt/build \
	&& cmake .. -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=/src/compiled -D USE_ENCLIB=mbedtls -D ENABLE_SHARED=false -D CMAKE_POSITION_INDEPENDENT_CODE=on \
	&& make -j$(nproc) install

ENV	LD_LIBRARY_PATH="/src/compiled/lib" \
	C_INCLUDE_PATH="/src/compiled/include"

COPY	.	.

ARG	BUILD_VERSION
ENV	BUILD_VERSION="${BUILD_VERSION}"

RUN	mkdir -p /src/build/ \
	&& cd /src/build/ \
	&& echo "${BUILD_VERSION}" > BUILD_VERSION

FROM	mist-base	as	mist-static-build

WORKDIR	/src/build

RUN	cmake \
	  -DPERPETUAL=1 \
	  -DDEBUG=3 \
	  -DLOAD_BALANCE=1 \
	  -DNOLLHLS=1 \
	  -DCMAKE_C_FLAGS="-fPIC" \
	  -DCMAKE_INSTALL_PREFIX=/opt \
	  -DCMAKE_PREFIX_PATH=/src/compiled \
	  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
	  -DNORIST=yes \
	  .. \
	&& make -j$(nproc) \
	&& make install

ARG	STRIP_BINARIES

RUN	if [ "$STRIP_BINARIES" = "true" ]; then strip -s /opt/bin/*; fi

FROM	mist-base	as	mist-shared-build

WORKDIR	/src/build

RUN	cmake \
	  -DPERPETUAL=1 \
	  -DLOAD_BALANCE=1 \
	  -DNOLLHLS=1 \
	  -DCMAKE_INSTALL_PREFIX=/opt \
	  -DCMAKE_PREFIX_PATH=/src/compiled \
	  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
	  -DBUILD_SHARED_LIBS=yes \
	  -DCMAKE_C_FLAGS="-fPIC" \
	  -DNORIST=yes \
	  .. \
	&& make -j$(nproc) \
	&& make install

ARG	STRIP_BINARIES

RUN	if [ "$STRIP_BINARIES" = "true" ]; then strip -s /opt/bin/* /opt/lib/*; fi

FROM	mist-${BUILD_TARGET}-build	as	mist-build

FROM	ubuntu:20.04	AS	mist

LABEL	maintainer="Amritanshu Varshney <amritanshu+github@livepeer.org>"

ARG	STRIP_BINARIES

ENV	DEBIAN_FRONTEND=noninteractive

# Needed for working TLS
RUN	apt update \
	&& apt install -yqq ca-certificates musl "$(if [ "$STRIP_BINARIES" != "true" ]; then echo "gdb"; fi)" \
	&& rm -rf /var/lib/apt/lists/*

COPY --from=mist-build	/opt/	/usr/

ARG	BUILD_VERSION
ENV	BUILD_VERSION="${BUILD_VERSION}"

EXPOSE	1935 4242 8080 8889/udp

ENTRYPOINT	["/usr/bin/MistController"]
