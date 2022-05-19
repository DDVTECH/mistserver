def docker_pipeline_steps():
    return [
        {
            "name": "build",
            "commands": [
                "docker login -u ${DOCKERHUB_USERNAME} -p ${DOCKERHUB_PASSWORD}",
                "docker buildx build --target=mist-debug-release --tag livepeerci/mistserver:debug-latest --tag livepeerci/mistserver:debug-$(date -u +'%Y%m%d%H%M%S') --tag livepeerci/mistserver:debug-catalyst .",
            ],
            "environment": {
                "DOCKERHUB_USERNAME": {"from_secret": "DOCKERHUB_USERNAME"},
                "DOCKERHUB_PASSWORD": {"from_secret": "DOCKERHUB_PASSWORD"},
            },
        },
    ]


def main(context):
    print(context)
    if context.build.event == "tag":
        return [{}]
    return [
        {
            "kind": "pipeline",
            "name": "docker",
            "type": "exec",
            "steps": docker_pipeline_steps(),
        }
    ]
