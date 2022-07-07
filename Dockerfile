ARG	STRIP_BINARIES="true"
ARG	BUILD_VERSION="Unknown"
ARG	BUILD_TARGET

FROM	ubuntu:20.04	as	mist-base

ENV	DEBIAN_FRONTEND=noninteractive

WORKDIR	/src

RUN	apt update -yq \
	&& apt install -yqq build-essential cmake git python3-pip \
	&& pip3 install -U meson ninja

COPY	.	.

ARG	BUILD_VERSION
ENV	BUILD_VERSION="${BUILD_VERSION}"

RUN	echo "${BUILD_VERSION}" > BUILD_VERSION

FROM	mist-base	as	mist-static-build

WORKDIR	/src

RUN	meson setup -DNORIST=true -DLOAD_BALANCE=true -Dprefix=/opt --default-library static build \
	&& cd build \
	&& ninja \
	&& ninja install

ARG	STRIP_BINARIES

RUN	if [ "$STRIP_BINARIES" = "true" ]; then find /opt/bin /opt/lib -type f -executable -exec strip -s {} \+; fi

FROM	mist-base	as	mist-shared-build

WORKDIR	/src

RUN	meson setup -DNORIST=true -DLOAD_BALANCE=true -Dprefix=/opt build \
	&& cd build \
	&& ninja \
	&& ninja install

ARG	STRIP_BINARIES

RUN	if [ "$STRIP_BINARIES" = "true" ]; then find /opt/bin /opt/lib -type f -executable -exec strip -s {} \+; fi

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
