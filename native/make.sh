#!/usr/bin/env bash

${JAVA_HOME}/bin/native-image --tool:truffle --class-path ../abzu/target/abzu-0.1-SNAPSHOT.jar:../launcher/target/launcher-0.1-SNAPSHOT.jar abzu.Launcher native
