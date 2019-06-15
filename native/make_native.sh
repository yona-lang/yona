#!/usr/bin/env bash
if [[ $YATTA_BUILD_NATIVE == "false" ]]; then
    echo "Skipping the native image build because YATTA_BUILD_NATIVE is set to false."
    exit 0
fi
"$JAVA_HOME"/bin/native-image --macro:truffle -H:MaxRuntimeCompileMethods=1200 \
    --initialize-at-build-time=yatta \
    -cp ../language/target/yatta.jar:../launcher/target/launcher-0.1-SNAPSHOT.jar \
    yatta.Launcher \
    yattanative
