PLATFORMS = [
    {"os": "linux", "arch": "amd64"},
    {"os": "linux", "arch": "arm64"},
    {"os": "darwin", "arch": "amd64"},
    {"os": "darwin", "arch": "arm64"},
]

DOCKER_REPOSITORY = "livepeerci/mistserver"

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


def get_environment(*names):
    env = {}
    for name in names:
        env[name] = {"from_secret": name}
    return env


def get_docker_tags(repo, prefix, debug, build_info, *args, arch=""):
    branch = build_info.branch
    commit = build_info.commit

    tags = [
        branch.replace("/", "-"),
        commit,
        commit[:8],
    ] + list(args)
    if branch in ("catalyst", "catalyst-updates", "catalyst-updates-drone"):
        tags.append("latest")
    suffix = "-debug" if debug else ""
    if arch != "":
        arch = "-" + arch
    return ["%s:%s-%s%s%s" % (repo, prefix, tag, suffix, arch) for tag in tuple(tags)]


def docker_image_pipeline(arch, release, stripped, context):
    build_info = context.build
    version = (build_info.event == "tag" and build_info.ref) or build_info.commit

    debug = stripped == "false"
    image_tags = get_docker_tags(
        DOCKER_REPOSITORY,
        release,
        debug,
        build_info,
        version,
        arch=arch,
    )

    return {
        "kind": "pipeline",
        "name": "docker-%s-%s-%s" % (arch, release, "debug" if debug else "strip"),
        "type": "exec",
        "trigger": TRIGGER_CONDITION,
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
                "name": "login",
                "commands": [
                    "docker login -u $DOCKERHUB_USERNAME -p $DOCKERHUB_PASSWORD",
                ],
                "environment": get_environment(
                    "DOCKERHUB_USERNAME",
                    "DOCKERHUB_PASSWORD",
                ),
            },
            {
                "name": "build",
                "commands": [
                    "docker buildx build --progress=plain --target=mist "
                    + "--build-arg BUILD_TARGET={} --build-arg STRIP_BINARIES={} --build-arg BUILD_VERSION={} --tag {} ".format(
                        release,
                        stripped,
                        version,
                        " --tag ".join(image_tags),
                    )
                    + "--push .",
                ],
            },
        ],
    }


def docker_manifest_pipeline(release, stripped, context):
    build_info = context.build
    version = (build_info.event == "tag" and build_info.ref) or build_info.commit

    debug = stripped == "false"
    image_tags = get_docker_tags(
        DOCKER_REPOSITORY,
        release,
        debug,
        build_info,
        version,
        arch="",
    )

    arch_image_tags = {
        image_tag: {arch: "%s-%s" % (image_tag, arch) for arch in DOCKER_BUILDS["arch"]}
        for image_tag in image_tags
    }

    manifest_commands = []
    for tag, arch_tag_info in arch_image_tags.items():
        manifest_commands.append(
            "docker manifest create %s --amend %s"
            % (tag, " --amend ".join(arch_tag_info.values()))
        )
        for arch, arch_tag in arch_tag_info.items():
            manifest_commands.append(
                "docker manifest annotate --arch %s %s %s" % (arch, tag, arch_tag)
            )
        manifest_commands.append("docker manifest push %s" % (tag,))

    return {
        "kind": "pipeline",
        "name": "docker-manifest-%s-%s" % (release, "debug" if debug else "strip"),
        "type": "exec",
        "trigger": TRIGGER_CONDITION,
        "depends_on": [
            "docker-%s-%s-%s" % (arch, release, "debug" if debug else "strip")
            for arch in DOCKER_BUILDS["arch"]
        ],
        "platform": {
            "os": "linux",
            "arch": "amd64",
        },
        "node": {
            "os": "linux",
            "arch": "amd64",
        },
        "steps": [
            {
                "name": "login",
                "commands": [
                    "docker login -u $DOCKERHUB_USERNAME -p $DOCKERHUB_PASSWORD",
                ],
                "environment": get_environment(
                    "DOCKERHUB_USERNAME",
                    "DOCKERHUB_PASSWORD",
                ),
            },
            {
                "name": "manifest",
                "commands": manifest_commands,
                "environment": {
                    "DOCKER_CLI_EXPERIMENTAL": "enabled",
                },
            },
        ],
    }


def binaries_pipeline(context, platform):
    version = (
        context.build.event == "tag" and context.build.ref
    ) or context.build.commit

    dependency_setup_commands = [
        "set -e",
        'export CI_PATH="$(realpath ..)"',
        "git clone https://github.com/cisco/libsrtp.git $CI_PATH/libsrtp",
        "git clone -b dtls_srtp_support --depth=1 https://github.com/livepeer/mbedtls.git $CI_PATH/mbedtls",
        "git clone https://github.com/Haivision/srt.git $CI_PATH/srt",
        "mkdir -p $CI_PATH/libsrtp/build $CI_PATH/mbedtls/build $CI_PATH/srt/build $CI_PATH/compiled",
        "cd $CI_PATH/libsrtp/build/ && cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CI_PATH/compiled .. && make -j $(nproc) install",
        'export PKG_CONFIG_PATH="$CI_PATH/compiled/lib/pkgconfig" && export LD_LIBRARY_PATH="$CI_PATH/compiled/lib" && export C_INCLUDE_PATH="$CI_PATH/compiled/include"',
        "cd $CI_PATH/mbedtls/build/ && cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CI_PATH/compiled .. && make -j $(nproc) install VERBOSE=1",
        "cd $CI_PATH/srt/build/ && cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$CI_PATH/compiled -DCMAKE_PREFIX_PATH=$CI_PATH/compiled -DUSE_ENCLIB=mbedtls -DENABLE_SHARED=false .. && make -j $(nproc) install",
    ]

    return {
        "kind": "pipeline",
        "name": "build-%s-%s" % (platform["os"], platform["arch"]),
        "type": "exec",
        "trigger": TRIGGER_CONDITION,
        "platform": {
            "os": platform["os"],
            "arch": platform["arch"],
        },
        "node": {
            "os": platform["os"],
            "arch": platform["arch"],
        },
        "workspace": {"path": "drone/mistserver"},
        "clone": {"depth": 0},
        "steps": [
            {
                "name": "dependencies",
                "commands": dependency_setup_commands,
            },
            {
                "name": "binaries",
                "commands": [
                    'export CI_PATH="$(realpath ..)"',
                    'export PKG_CONFIG_PATH="$CI_PATH/compiled/lib/pkgconfig" && export LD_LIBRARY_PATH="$CI_PATH/compiled/lib" && export C_INCLUDE_PATH="$CI_PATH/compiled/include"',
                    "mkdir -p build/ && echo {} | tee build/BUILD_VERSION".format(
                        version,
                    ),
                    'cd build && cmake -DDEBUG=3 -DPERPETUAL=1 -DLOAD_BALANCE=1 -DNOLLHLS=1 -DCMAKE_INSTALL_PREFIX="$CI_PATH" -DCMAKE_PREFIX_PATH="$CI_PATH/compiled" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNORIST=yes -DNOLLHLS=1 ..',
                    "make -j $(nproc) && make install",
                ],
            },
            {
                "name": "compress",
                "commands": [
                    'export CI_PATH="$(realpath ..)"',
                    "cd $CI_PATH/bin/",
                    "tar -czvf livepeer-mistserver-%s-%s.tar.gz ./*"
                    % (platform["os"], platform["arch"]),
                ],
            },
            {
                "name": "upload",
                "commands": [
                    'scripts/upload_build.sh -d "$(realpath ..)/bin" "livepeer-mistserver-%s-%s.tar.gz"'
                    % (platform["os"], platform["arch"]),
                ],
                "environment": get_environment(
                    "GCLOUD_KEY",
                    "GCLOUD_SECRET",
                    "GCLOUD_BUCKET",
                ),
            },
        ],
    }


def checksum_pipeline(context):
    build_context = context.build
    commit = build_context.commit
    checksum_file = "{}_checksums.txt".format(commit)

    download_commands = [
        'export CI_PATH="$(realpath ..)"',
        'mkdir -p "$CI_PATH/download"',
        'cd "$CI_PATH/download" && pwd',
    ]
    for platform in PLATFORMS:
        download_commands.append(
            "wget -q https://build.livepeer.live/mistserver/{}/livepeer-mistserver-{}-{}.tar.gz".format(
                commit,
                platform["os"],
                platform["arch"],
            )
        )

    return {
        "kind": "pipeline",
        "name": "checksum",
        "type": "exec",
        "trigger": TRIGGER_CONDITION,
        "depends_on": [
            "build-{}-{}".format(platform["os"], platform["arch"])
            for platform in PLATFORMS
        ],
        "platform": {
            "os": "linux",
            "arch": "amd64",
        },
        "node": {
            "os": "linux",
            "arch": "amd64",
        },
        "steps": [
            {
                "name": "download",
                "commands": download_commands,
            },
            {
                "name": "checksum",
                "commands": [
                    'cd "$(realpath ..)/download"',
                    "sha256sum * > {}".format(checksum_file),
                ],
            },
            {
                "name": "upload",
                "commands": [
                    'scripts/upload_build.sh -d "$(realpath ..)/download" "{}"'.format(
                        checksum_file,
                    ),
                ],
                "environment": get_environment(
                    "GCLOUD_KEY",
                    "GCLOUD_SECRET",
                    "GCLOUD_BUCKET",
                ),
            },
        ],
    }


def branch_manifest_pipeline(context):
    clean_branch = context.build.branch.replace("/", "-")

    builds = {}
    src_filenames = {}
    for platform in PLATFORMS:
        key = "{}-{}".format(platform["os"], platform["arch"])
        filename = "livepeer-mistserver-{}.tar.gz".format(key)
        url = "https://build.livepeer.live/mistserver/{}/{}".format(
            context.build.commit,
            filename,
        )
        builds[key] = url
        src_filenames[key] = filename

    output_manifest = {
        "builds": builds,
        "commit": context.build.commit,
        "ref": context.build.ref,
        "branch": context.build.branch,
        "srcFilenames": src_filenames,
    }

    return {
        "kind": "pipeline",
        "name": "manifest",
        "type": "exec",
        "trigger": TRIGGER_CONDITION,
        "depends_on": ["checksum"],
        "platform": {
            "os": "linux",
            "arch": "amd64",
        },
        "node": {
            "os": "linux",
            "arch": "amd64",
        },
        "steps": [
            {
                "name": "manifest",
                "commands": [
                    "echo '{}' > \"$(realpath ..)/{}.json\"".format(
                        output_manifest,
                        clean_branch,
                    )
                ],
            },
            {
                "name": "upload",
                "commands": [
                    'scripts/upload_build.sh -f -r -d "$(realpath ..)" "{}.json"'.format(
                        clean_branch,
                    ),
                ],
                "environment": get_environment(
                    "GCLOUD_KEY",
                    "GCLOUD_SECRET",
                    "GCLOUD_BUCKET",
                ),
            },
            {
                "name": "notification",
                "commands": [
                    "curl -X POST https://holy-bread-207a.livepeer.workers.dev/",
                ],
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
        docker_image_pipeline(arch, release, stripped, context)
        for arch in DOCKER_BUILDS["arch"]
        for release in DOCKER_BUILDS["release"]
        for stripped in DOCKER_BUILDS["strip"]
    ]
    manifest += [
        docker_manifest_pipeline(release, stripped, context)
        for release in DOCKER_BUILDS["release"]
        for stripped in DOCKER_BUILDS["strip"]
    ]
    for platform in PLATFORMS:
        manifest.append(binaries_pipeline(context, platform))
    manifest.append(checksum_pipeline(context))
    manifest.append(branch_manifest_pipeline(context))
    return manifest
