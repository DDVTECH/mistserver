FROM alpine AS mist_build

RUN apk add --update git meson ninja gcc g++ linux-headers
ADD . /src/
RUN mkdir /build/; cd /build; meson setup /src --default-library=static -Dmbedtls:default_library=static -DDEBUG=3 -Dstrip=true; ninja install

FROM alpine
RUN apk add libstdc++
COPY --from=mist_build /usr/local/ /usr/local/

LABEL org.opencontainers.image.authors="Jaron Viëtor <jaron.vietor@ddvtech.com>"
EXPOSE 4242 8080 1935 5554
ENTRYPOINT ["MistController"]

