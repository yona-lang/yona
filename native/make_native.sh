#!/usr/bin/env bash
if [[ $YATTA_BUILD_NATIVE == "false" ]]; then
    echo "Skipping the native image build because YATTA_BUILD_NATIVE is set to false."
    exit 0
fi
"$JAVA_HOME"/bin/native-image \
    --enable-http --enable-https --enable-all-security-services \
    --macro:truffle --no-fallback --initialize-at-build-time \
    -cp ../language/target/yatta.jar:../launcher/target/launcher-0.1-SNAPSHOT.jar:$JAVA_HOME/lib/src.zip \
    yatta.Launcher \
    yattanative
