PLATFORMS = [
    {"os": "linux", "arch": "amd64"},
    {"os": "darwin", "arch": "amd64"},
]


def get_docker_tags(repo, prefix):
    tags = ["latest", "catalyst"]
    return ["%s:%s-%s" % (repo, prefix, tag) for tag in tags]


def docker_image_pipeline():
    image_tags = get_docker_tags("livepeerci/mistserver", "debug")
    return {
        "kind": "pipeline",
        "name": "docker",
        "type": "exec",
        "platform": {
            "os": "linux",
            "arch": "amd64",
        },
        "steps": [
            {
                "name": "build",
                "commands": [
                    "docker buildx build --target=mist-debug-release --tag {} .".format(
                        " --tag ".join(image_tags),
                    ),
                ],
            },
            {
                "name": "login",
                "commands": [
                    "docker login -u $DOCKERHUB_USERNAME -p $DOCKERHUB_PASSWORD",
                ],
                "environment": {
                    "DOCKERHUB_USERNAME": {"from_secret": "DOCKERHUB_USERNAME"},
                    "DOCKERHUB_PASSWORD": {"from_secret": "DOCKERHUB_PASSWORD"},
                },
            },
            {
                "name": "push",
                "commands": ["docker push %s" % (tag,) for tag in image_tags],
            },
        ],
    }


def binaries_pipeline(platform):
    return {
        "kind": "pipeline",
        "name": "build-%s-%s" % (platform["os"], platform["arch"]),
        "type": "exec",
        "platform": {
            "os": platform["os"],
            "arch": platform["arch"],
        },
        "workspace": {"path": "drone/mistserver"},
        "steps": [
            {
                "name": "dependencies",
                "commands": [
                    'export CI_PATH="$(realpath ..)"',
                    "git clone https://github.com/cisco/libsrtp.git $CI_PATH/libsrtp",
                    "git clone -b dtls_srtp_support --depth=1 https://github.com/livepeer/mbedtls.git $CI_PATH/mbedtls",
                    "git clone https://github.com/Haivision/srt.git $CI_PATH/srt",
                    "mkdir -p $CI_PATH/libsrtp/build $CI_PATH/mbedtls/build $CI_PATH/srt/build $CI_PATH/compiled",
                    "cd $CI_PATH/libsrtp/build/ && cmake -DCMAKE_INSTALL_PATH=$CI_PATH/compiled .. && make -j $(nproc) install",
                    "cd $CI_PATH/mbedtls/build/ && cmake -DCMAKE_INSTALL_PATH=$CI_PATH/compiled .. && make -j $(nproc) install",
                    "cd $CI_PATH/srt/build/ && cmake -DCMAKE_INSTALL_PATH=$CI_PATH/compiled -D USE_ENCLIB=mbedtls -D ENABLE_SHARED=false .. && make -j $(nproc) install",
                ],
            },
            {
                "name": "binaries",
                "commands": [
                    'export LD_LIBRARY_PATH="$(realpath ..)/compiled/lib" && export C_INCLUDE_PATH="$(realpath ..)/compiled/include" && export CI_PATH=$(realpath ..)',
                    "mkdir -p build/",
                    "cd build && cmake -DPERPETUAL=1 -DLOAD_BALANCE=1 -DCMAKE_INSTALL_PREFIX=$CI_PATH/bin -DCMAKE_PREFIX_PATH=$CI_PATH/compiled -DCMAKE_BUILD_TYPE=RelWithDebInfo ..",
                    "make -j $(nproc) && make install",
                ],
            },
        ],
    }


def get_context(context):
    return {
        "kind": "pipeline",
        "type": "exec",
        "name": "context",
        "steps": [{"name": "print", "commands": ["echo '%s'" % (context,)]}],
    }


def main(context):
    if context.build.event == "tag":
        return [{}]
    manifest = [docker_image_pipeline(), get_context(context)]
    for platform in PLATFORMS:
        manifest.append(binaries_pipeline(platform))
    return manifest
