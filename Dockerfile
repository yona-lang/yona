FROM oracle/graalvm-ce:20.1.0-java11

MAINTAINER Adam Kovari <kovariadam@gmail.com>

RUN gu install native-image
RUN gu install -ur https://github.com/yatta-lang/yatta/releases/latest/download/yatta-component.jar

RUN alternatives --remove yatta /opt/graalvm-ce-java11-20.1.0//bin/yatta

WORKDIR /sources

ENTRYPOINT "/opt/graalvm-ce-java11-20.1.0/bin/yatta"
