#!/usr/bin/env bash

COMPONENT_DIR="component_temp_dir"
LANGUAGE_PATH="$COMPONENT_DIR/jre/languages/yatta"
if [[ -f ../native/yattanative ]]; then
    INCLUDE_YATTANATIVE="TRUE"
fi

rm -rf COMPONENT_DIR

mkdir -p "$LANGUAGE_PATH"
cp ../language/target/yatta.jar "$LANGUAGE_PATH"

mkdir -p "$LANGUAGE_PATH/launcher"
cp ../launcher/target/yatta-launcher.jar "$LANGUAGE_PATH/launcher/"

mkdir -p "$LANGUAGE_PATH/bin"
cp ../yatta $LANGUAGE_PATH/bin/
if [[ $INCLUDE_YATTANATIVE = "TRUE" ]]; then
    cp ../native/yattanative $LANGUAGE_PATH/bin/
fi

mkdir -p "$COMPONENT_DIR/META-INF"
{
    echo "Bundle-Name: Simple Language";
    echo "Bundle-Symbolic-Name: yatta";
    echo "Bundle-Version: 1.0.0-SNAPSHOT";
    echo 'Bundle-RequireCapability: org.graalvm; filter:="(&(graalvm_version=19.3.1)(os_arch=amd64))"';
    echo "x-GraalVM-Polyglot-Part: True"
} > "$COMPONENT_DIR/META-INF/MANIFEST.MF"

(
cd $COMPONENT_DIR || exit 1
jar cfm ../yatta-component.jar META-INF/MANIFEST.MF .

echo "bin/yatta = ../jre/languages/yatta/bin/yatta" > META-INF/symlinks
if [[ $INCLUDE_YATTANATIVE = "TRUE" ]]; then
    echo "bin/yattanative = ../jre/languages/yatta/bin/yattanative" >> META-INF/symlinks
fi
jar uf ../yatta-component.jar META-INF/symlinks

{
    echo "jre/languages/yatta/bin/yatta = rwxrwxr-x"
    echo "jre/languages/yatta/bin/yattanative = rwxrwxr-x"
} > META-INF/permissions
jar uf ../yatta-component.jar META-INF/permissions
)
rm -rf $COMPONENT_DIR
