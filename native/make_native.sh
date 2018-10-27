#!/usr/bin/env bash
if [[ $ABZU_BUILD_NATIVE == "false" ]]; then
    echo "Skipping the native image build because ABZU_BUILD_NATIVE is set to false."
    exit 0
fi
$JAVA_HOME/bin/native-image --tool:truffle -H:MaxRuntimeCompileMethods=1200 \
    -cp ../language/target/abzu.jar:../launcher/target/launcher-0.1-SNAPSHOT.jar \
    abzu.Launcher \
    abzunative
