FROM alpine AS mist_build

RUN apk add --update git meson ninja gcc g++ linux-headers
ADD . /src/
RUN mkdir /build/; cd /build; meson setup /src -DDEBUG=3; ninja install
RUN cp /build/subprojects/*/lib*.so /usr/local/lib/
RUN cp /build/subprojects/librist/librist.so.4 /usr/local/lib/
RUN strip /usr/local/bin/* /usr/local/lib/*.so

FROM alpine
COPY --from=mist_build /usr/local/ /usr/local/
RUN apk add libstdc++

LABEL org.opencontainers.image.authors="Jaron Viëtor <jaron.vietor@ddvtech.com>"
EXPOSE 4242 8080 1935 5554
ENTRYPOINT ["MistController"]

