FROM	ubuntu:20.04	as	mist-build

ENV	DEBIAN_FRONTEND=noninteractive

WORKDIR	/src

RUN	apt update -yq \
	&& apt install -yqq build-essential cmake git

RUN	git clone https://github.com/cisco/libsrtp.git \
	&& mkdir -p libsrtp/build \
	&& cd libsrtp/build \
	&& cmake -DCMAKE_INSTALL_PREFIX=/src/compiled .. \
	&& make -j$(nproc) install \
	&& cd /src \
	&& git clone -b dtls_srtp_support --depth=1 https://github.com/livepeer/mbedtls.git \
	&& cd mbedtls \
	&& mkdir build \
	&& cd build \
	&& cmake -DCMAKE_INSTALL_PREFIX=/src/compiled .. \
	&& make -j$(nproc) install \
	&& cd /src \
	&& git clone https://github.com/Haivision/srt.git \
	&& mkdir -p srt/build \
	&& cd srt/build \
	&& cmake .. -DCMAKE_INSTALL_PREFIX=/src/compiled -D USE_ENCLIB=mbedtls -D ENABLE_SHARED=false \
	&& make -j$(nproc) install

ENV	LD_LIBRARY_PATH="/src/compiled/lib" \
	C_INCLUDE_PATH="/src/compiled/include"

COPY	.	.

RUN	mkdir -p build/ \
	&& cd build/ \
	&& cmake .. -DPERPETUAL=1 -DLOAD_BALANCE=1 -DCMAKE_INSTALL_PREFIX=/opt -DCMAKE_PREFIX_PATH=/src/compiled -DCMAKE_BUILD_TYPE=RelWithDebInfo \
	&& make -j$(nproc) \
	&& make install

FROM	ubuntu:20.04	AS	strip-binaries

ENV	DEBIAN_FRONTEND=noninteractive

RUN	apt update -yq \
	&& apt install -yqq gcc

WORKDIR	/opt/bin/

COPY	--from=mist-build /opt/bin/ ./

RUN	strip /opt/bin/*

FROM	ubuntu:20.04	AS	mist-debug-release

LABEL	maintainer="Amritanshu Varshney <amritanshu+github@livepeer.org>"

ENV	DEBIAN_FRONTEND=noninteractive

# Needed for working TLS
RUN	apt-get update \
	&& apt-get install -yqq ca-certificates musl \
	&& rm -rf /var/lib/apt/lists/*

COPY --from=mist-build	/opt/bin/	/usr/bin/

EXPOSE	1935 4242 8080 8889/udp

ENTRYPOINT	["/usr/bin/MistController"]

FROM	ubuntu:20.04	AS	mist-strip-release

ENV	DEBIAN_FRONTEND=noninteractive

LABEL	maintainer="Amritanshu Varshney <amritanshu+github@livepeer.org>"

# Needed for working TLS
RUN	apt-get update \
	&& apt-get install -yqq ca-certificates musl \
	&& rm -rf /var/lib/apt/lists/*

COPY --from=strip-binaries	/opt/bin/	/usr/bin/

EXPOSE	1935 4242 8080 8889/udp

ENTRYPOINT	["/usr/bin/MistController"]
