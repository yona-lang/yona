#!/usr/bin/env pwsh

$version = "0.8.1-SNAPSHOT"

function localDockerBuild() {
    hilite docker build -t akovari/yona:latest -t akovari/yona:$version -f Dockerfile.local .
}

function dockerPush() {
    hilite docker push akovari/yona:latest
    hilite docker push akovari/yona:$version
}

function run([string[]]$programArgs) {
    hilite ./yona $programArgs
}

function mvnFastBuild() {
    hilite mvn package -DskipTests -pl '!native' -pl '!component'
}

function mvnPackage {
    hilite mvn package -DexcludedGroups=slow
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
    default { Write-Error "unknown option $($args[0])" }
}
