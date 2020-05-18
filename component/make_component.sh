#!/usr/bin/env bash

readonly COMPONENT_DIR="component_temp_dir"
readonly LANGUAGE_PATH="$COMPONENT_DIR/languages/yatta"
readonly CURRENT_TAG=$(git describe --tags --exact-match || echo "SNAPSHOT")
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
if [[ $INCLUDE_YATTANATIVE -eq "TRUE" ]]; then
    cp ../native/yattanative $LANGUAGE_PATH/bin/
fi

cp -R ../language/lib-yatta "$LANGUAGE_PATH"

touch "$LANGUAGE_PATH/native-image.properties"

mkdir -p "$COMPONENT_DIR/META-INF"
{
    echo "Bundle-Name: yatta";
    echo "Bundle-Symbolic-Name: yatta";
    echo "Bundle-Version: 1.0.0-$CURRENT_TAG";
    echo 'Bundle-RequireCapability: org.graalvm; filter:="(&(graalvm_version=20.0.0)(os_arch=amd64))"';
    echo "x-GraalVM-Polyglot-Part: True"
} > "$COMPONENT_DIR/META-INF/MANIFEST.MF"

(
cd $COMPONENT_DIR || exit 1
jar cfm ../yatta-component.jar META-INF/MANIFEST.MF .

echo "bin/yatta = ../languages/yatta/bin/yatta" > META-INF/symlinks
if [[ $INCLUDE_YATTANATIVE -eq "TRUE" ]]; then
    echo "bin/yattanative = ../languages/yatta/bin/yattanative" >> META-INF/symlinks
fi
jar uf ../yatta-component.jar META-INF/symlinks

{
    echo "languages/yatta/bin/yatta = rwxrwxr-x"
    echo "languages/yatta/bin/yattanative = rwxrwxr-x"
} > META-INF/permissions
jar uf ../yatta-component.jar META-INF/permissions
)
rm -rf $COMPONENT_DIR
