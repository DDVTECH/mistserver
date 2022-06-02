PLATFORMS = [
    {"os": "linux", "arch": "amd64"},
    {"os": "linux", "arch": "arm64"},
    {"os": "darwin", "arch": "amd64"},
    {"os": "darwin", "arch": "arm64"},
]

DOCKER_BUILDS = {
    "arch": ["amd64", "arm64"],
    "release": ["static", "shared"],
    "strip": ["true", "false"],
}

TRIGGER_CONDITION = {
    "event": [
        "push",
        # "pull_request",
        "tag",
    ]
}


def get_docker_tags(repo, prefix, branch, commit, debug):
    tags = [
        branch.replace("/", "-"),
        commit,
        commit[:8],
    ]
    if branch == "catalyst":
        tags.append("latest")
    suffix = "-debug" if debug else ""
    return ["%s:%s-%s%s" % (repo, prefix, tag, suffix) for tag in tags]


def docker_image_pipeline(arch, release, stripped, build_context):
    debug = stripped == "false"
    image_tags = get_docker_tags(
        "livepeerci/mistserver",
        release,
        build_context.branch,
        build_context.commit,
        debug,
    )
    return {
        "kind": "pipeline",
        "name": "docker-%s-%s-%s" % (arch, release, "debug" if debug else "strip"),
        "type": "exec",
        "platform": {
            "os": "linux",
            "arch": arch,
        },
        "node": {
            "os": "linux",
            "arch": arch,
        },
        "steps": [
            {
                "name": "build",
                "commands": [
                    "docker buildx build --target=mist --build-arg BUILD_TARGET={} --build-arg STRIP_BINARIES={} --tag {} .".format(
                        release,
                        stripped,
                        " --tag ".join(image_tags),
                    ),
                ],
                "when": TRIGGER_CONDITION,
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
                "when": TRIGGER_CONDITION,
            },
            {
                "name": "push",
                "commands": ["docker push %s" % (tag,) for tag in image_tags],
                "when": TRIGGER_CONDITION,
            },
        ],
    }


def binaries_pipeline(platform, build_context):
    branch_name = build_context.branch.replace("/", "-")
    return {
        "kind": "pipeline",
        "name": "build-%s-%s" % (platform["os"], platform["arch"]),
        "type": "exec",
        "platform": {
            "os": platform["os"],
            "arch": platform["arch"],
        },
        "node": {
            "os": platform["os"],
            "arch": platform["arch"],
        },
        "workspace": {"path": "drone/mistserver"},
        "steps": [
            {
                "name": "dependencies",
                "commands": [
                    "env",
                    "uname -a",
                    'export CI_PATH="$(realpath ..)"',
                    "git clone https://github.com/cisco/libsrtp.git $CI_PATH/libsrtp",
                    "git clone -b dtls_srtp_support --depth=1 https://github.com/livepeer/mbedtls.git $CI_PATH/mbedtls",
                    "git clone https://github.com/Haivision/srt.git $CI_PATH/srt",
                    "mkdir -p $CI_PATH/libsrtp/build $CI_PATH/mbedtls/build $CI_PATH/srt/build $CI_PATH/compiled",
                    "cd $CI_PATH/libsrtp/build/ && cmake -DCMAKE_INSTALL_PREFIX=$CI_PATH/compiled .. && make -j $(nproc) install",
                    'export PKG_CONFIG_PATH="$CI_PATH/compiled/lib/pkgconfig" && export LD_LIBRARY_PATH="$CI_PATH/compiled/lib" && export C_INCLUDE_PATH="$CI_PATH/compiled/include"',
                    "cd $CI_PATH/mbedtls/build/ && cmake -DCMAKE_INSTALL_PREFIX=$CI_PATH/compiled .. && make -j $(nproc) install VERBOSE=1",
                    "cd $CI_PATH/srt/build/ && cmake -DCMAKE_INSTALL_PREFIX=$CI_PATH/compiled -D USE_ENCLIB=mbedtls -D ENABLE_SHARED=false .. && make -j $(nproc) install",
                ],
                "when": TRIGGER_CONDITION,
            },
            {
                "name": "binaries",
                "commands": [
                    'export CI_PATH="$(realpath ..)"',
                    'export PKG_CONFIG_PATH="$CI_PATH/compiled/lib/pkgconfig" && export LD_LIBRARY_PATH="$CI_PATH/compiled/lib" && export C_INCLUDE_PATH="$CI_PATH/compiled/include"',
                    "mkdir -p build/",
                    "cd build && cmake -DPERPETUAL=1 -DLOAD_BALANCE=1 -DCMAKE_INSTALL_PREFIX=$CI_PATH/bin -DCMAKE_PREFIX_PATH=$CI_PATH/compiled -DCMAKE_BUILD_TYPE=RelWithDebInfo ..",
                    "make -j $(nproc) && make install",
                ],
                "when": TRIGGER_CONDITION,
            },
            {
                "name": "upload",
                "commands": [
                    'export CI_PATH="$(realpath ..)"',
                    "cd $CI_PATH/bin",
                    "tar -czvf livepeer-mistserver-%s-%s.tar.gz ./*"
                    % (platform["os"], platform["arch"]),
                    "gsutil cp ./livepeer-mistserver-%s-%s.tar.gz gs://$GCLOUD_BUCKET/mistserver/%s"
                    % (platform["os"], platform["arch"], branch_name),
                ],
                "when": TRIGGER_CONDITION,
            },
        ],
    }


def get_context(context):
    """Template pipeline to get information about build context."""
    return {
        "kind": "pipeline",
        "type": "exec",
        "name": "context",
        "steps": [{"name": "print", "commands": ["echo '%s'" % (context,)]}],
    }


def main(context):
    if context.build.event == "tag":
        return [{}]
    manifest = [
        docker_image_pipeline(arch, release, stripped, context.build)
        for arch in DOCKER_BUILDS["arch"]
        for release in DOCKER_BUILDS["release"]
        for stripped in DOCKER_BUILDS["strip"]
    ]
    for platform in PLATFORMS:
        manifest.append(binaries_pipeline(platform, context.build))
    return manifest
