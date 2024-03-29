#!/usr/bin/env bash
if [[ $YONA_BUILD_NATIVE == "false" ]]; then
    echo "Skipping the native image build because YONA_BUILD_NATIVE is set to false."
    exit 0
fi
"$JAVA_HOME"/bin/native-image \
    --enable-http --enable-https --enable-all-security-services --report-unsupported-elements-at-runtime \
    --macro:truffle --no-fallback --initialize-at-build-time --language:regex --language:js \
    -H:ReflectionConfigurationFiles=reflection-config.json -H:ResourceConfigurationFiles=resource-config.json \
    -H:IncludeResourceBundles=net.sourceforge.argparse4j.internal.ArgumentParserImpl \
    -cp ../language/target/language.jar:../launcher/target/yona-launcher.jar:$JAVA_HOME/lib/src.zip:$JAVA_HOME/lib/graalvm/launcher-common.jar \
    yona.Launcher \
    yonanative
