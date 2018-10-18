#!/usr/bin/env bash

COMPONENT_DIR="component_temp_dir"
LANGUAGE_PATH="$COMPONENT_DIR/jre/languages/abzu"

rm -rf COMPONENT_DIR

mkdir -p "$LANGUAGE_PATH"
cp ../abzu/target/abzu-0.1-SNAPSHOT.jar "$LANGUAGE_PATH"

mkdir -p "$LANGUAGE_PATH/launcher"
cp ../launcher/target/launcher-0.1-SNAPSHOT.jar "$LANGUAGE_PATH/launcher/"

mkdir -p "$LANGUAGE_PATH/bin"
cp -r ../abzu ${LANGUAGE_PATH}/bin/

mkdir -p "$COMPONENT_DIR/META-INF"
MANIFEST="$COMPONENT_DIR/META-INF/MANIFEST.MF"
touch "$MANIFEST"
echo "Bundle-Name: Abzu Language" >> "$MANIFEST"
echo "Bundle-Symbolic-Name: abzu" >> "$MANIFEST"
echo "Bundle-Version: 0.1-SNAPSHOT" >> "$MANIFEST"
echo 'Bundle-RequireCapability: org.graalvm; filter:="(graalvm_version=1.0.0-rc7)"' >> "$MANIFEST"
echo "x-GraalVM-Polyglot-Part: True" >> "$MANIFEST"

cd ${COMPONENT_DIR}
jar cfm ../abzu-component.jar META-INF/MANIFEST.MF .

echo "bin/abzu = ../jre/languages/abzu/bin/abzu" > META-INF/symlinks
jar uf ../abzu-component.jar META-INF/symlinks

echo "jre/languages/abzu/bin/abzu = rwxrwxr-x" > META-INF/permissions
jar uf ../abzu-component.jar META-INF/permissions
cd ..
rm -rf ${COMPONENT_DIR}
