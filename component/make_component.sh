#!/usr/bin/env bash

readonly COMPONENT_DIR="component_temp_dir"
readonly LANGUAGE_PATH="$COMPONENT_DIR/languages/yona"
readonly CURRENT_TAG=$(git describe --tags --exact-match || echo "SNAPSHOT")
if [[ -f ../native/yonanative ]]; then
  INCLUDE_YONANATIVE="TRUE"
fi

rm -rf COMPONENT_DIR

mkdir -p "$LANGUAGE_PATH"
cp ../language/target/language.jar "$LANGUAGE_PATH"

mkdir -p "$LANGUAGE_PATH/launcher"
cp ../launcher/target/yona-launcher.jar "$LANGUAGE_PATH/launcher/"

mkdir -p "$LANGUAGE_PATH/bin"
cp ../yona $LANGUAGE_PATH/bin/
if [[ $INCLUDE_YONANATIVE == "TRUE" ]]; then
  cp ../native/yonanative $LANGUAGE_PATH/bin/
fi

cp -R ../language/lib-yona "$LANGUAGE_PATH"
cp -R ../yona.nanorc "$LANGUAGE_PATH"

touch "$LANGUAGE_PATH/native-image.properties"

{
  echo "Requires = language:regex"
  echo "JavaArgs = -Xmx3G \\"
  echo "           -Dpolyglot.image-build-time.PreinitializeContexts=yona"
} >"$LANGUAGE_PATH/native-image.properties"

mkdir -p "$COMPONENT_DIR/META-INF"
{
  echo "Bundle-Name: yona"
  echo "Bundle-Symbolic-Name: yona"
  echo "Bundle-Version: 0.8.1-$CURRENT_TAG"
  echo 'Bundle-RequireCapability: org.graalvm; filter:="(&(graalvm_version=20.2.0)(os_arch=amd64))"'
  echo "x-GraalVM-Polyglot-Part: True"
} >"$COMPONENT_DIR/META-INF/MANIFEST.MF"

(
  cd $COMPONENT_DIR || exit 1
  jar cfm ../yona-component.jar META-INF/MANIFEST.MF .

  echo "bin/yona = ../languages/yona/bin/yona" >META-INF/symlinks
  if [[ $INCLUDE_YONANATIVE == "TRUE" ]]; then
    echo "bin/yonanative = ../languages/yona/bin/yonanative" >>META-INF/symlinks
  fi
  jar uf ../yona-component.jar META-INF/symlinks

  echo "languages/yona/bin/yona = rwxrwxr-x" >META-INF/permissions
  if [[ $INCLUDE_YONANATIVE == "TRUE" ]]; then
    echo "languages/yona/bin/yonanative = rwxrwxr-x" >META-INF/permissions
  fi

  jar uf ../yona-component.jar META-INF/permissions
)
rm -rf $COMPONENT_DIR
