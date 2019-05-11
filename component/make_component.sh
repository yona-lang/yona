#!/usr/bin/env bash

COMPONENT_DIR="component_temp_dir"
LANGUAGE_PATH="$COMPONENT_DIR/jre/languages/abzu"
if [[ -f ../native/abzunative ]]; then
    INCLUDE_ABZUNATIVE="TRUE"
fi

rm -rf COMPONENT_DIR

mkdir -p "$LANGUAGE_PATH"
cp ../language/target/abzu.jar "$LANGUAGE_PATH"

mkdir -p "$LANGUAGE_PATH/launcher"
cp ../launcher/target/abzu-launcher.jar "$LANGUAGE_PATH/launcher/"

mkdir -p "$LANGUAGE_PATH/bin"
cp ../abzu $LANGUAGE_PATH/bin/
if [[ $INCLUDE_ABZUNATIVE = "TRUE" ]]; then
    cp ../native/abzunative $LANGUAGE_PATH/bin/
fi

mkdir -p "$COMPONENT_DIR/META-INF"
{
    echo "Bundle-Name: Simple Language";
    echo "Bundle-Symbolic-Name: abzu";
    echo "Bundle-Version: 1.0.0-SNAPSHOT";
    echo 'Bundle-RequireCapability: org.graalvm; filter:="(&(graalvm_version=19.0.0)(os_arch=amd64))"';
    echo "x-GraalVM-Polyglot-Part: True"
} > "$COMPONENT_DIR/META-INF/MANIFEST.MF"

(
cd $COMPONENT_DIR || exit 1
jar cfm ../abzu-component.jar META-INF/MANIFEST.MF .

echo "bin/abzu = ../jre/languages/abzu/bin/abzu" > META-INF/symlinks
if [[ $INCLUDE_ABZUNATIVE = "TRUE" ]]; then
    echo "bin/abzunative = ../jre/languages/abzu/bin/abzunative" >> META-INF/symlinks
fi
jar uf ../abzu-component.jar META-INF/symlinks

{
    echo "jre/languages/abzu/bin/abzu = rwxrwxr-x"
    echo "jre/languages/abzu/bin/abzunative = rwxrwxr-x"
} > META-INF/permissions
jar uf ../abzu-component.jar META-INF/permissions
)
rm -rf $COMPONENT_DIR
