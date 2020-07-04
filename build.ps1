#!/usr/bin/env pwsh

$version = "0.8.1-SNAPSHOT"

function localDockerBuild() {
    docker build -t akovari/yatta:latest -t akovari/yatta:$version -f Dockerfile.local .
}

function dockerPush() {
    docker push akovari/yatta:latest
    docker push akovari/yatta:$version
}

switch($args[0]) {
    "build-local-docker" { localDockerBuild }
    "push-docker" { dockerPush }
    default { Write-Error "unknown option $($args[0])" }
}
