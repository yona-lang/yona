#!/usr/bin/env pwsh

$version = "0.8.4-SNAPSHOT"

function localDockerBuild() {
    docker build -t akovari/yona:latest -t akovari/yona:$version -f Dockerfile.local .
}

function dockerPush() {
    docker push akovari/yona:latest
    docker push akovari/yona:$version
}

function run([string[]]$programArgs) {
    ./yona $programArgs
}

function mvnFastBuild() {
    mvn package -DskipTests -pl '!native' -pl '!component'
}

function mvnPackage {
    mvn package -DexcludedGroups=slow
}

function mvnRelease {
    mvn release:update-versions -DautoVersionSubmodules=true
}

[string[]]$programArgs = @()

if ($args.Length -gt 1) {
    $programArgs = $args[1..($args.Length-1)]
}

switch($args[0]) {
    "build-local-docker" { localDockerBuild }
    "push-docker" { dockerPush }
    "run" { run($programArgs) }
    "package" { mvnPackage }
    "local-launcher" { mvnFastBuild; run($programArgs) }
    "release" { release }
    default { Write-Error "unknown option $($args[0])" }
}
