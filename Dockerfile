FROM ghcr.io/graalvm/graalvm-ce:ol8-java17-22

MAINTAINER Adam Kovari <kovariadam@gmail.com>

RUN yum install -y git
#RUN gu install native-image

ARG MAVEN_VERSION=3.8.2
ARG SHA=b0bf39460348b2d8eae1c861ced6c3e8a077b6e761fb3d4669be5de09490521a74db294cf031b0775b2dfcd57bd82246e42ce10904063ef8e3806222e686f222
ARG BASE_URL=https://apache.osuosl.org/maven/maven-3/${MAVEN_VERSION}/binaries

# 5- Create the directories, download maven, validate the download, install it, remove downloaded file and set links
RUN mkdir -p /usr/share/maven /usr/share/maven/ref \
  && echo "Downlaoding maven" \
  && curl -fsSL -o /tmp/apache-maven.tar.gz ${BASE_URL}/apache-maven-${MAVEN_VERSION}-bin.tar.gz \
  \
  && echo "Checking download hash" \
  && echo "${SHA}  /tmp/apache-maven.tar.gz" | sha512sum -c - \
  \
  && echo "Unziping maven" \
  && tar -xzf /tmp/apache-maven.tar.gz -C /usr/share/maven --strip-components=1 \
  \
  && echo "Cleaning and setting links" \
  && rm -f /tmp/apache-maven.tar.gz \
  && ln -s /usr/share/maven/bin/mvn /usr/bin/mvn

ENV MAVEN_HOME /usr/share/maven
ENV MAVEN_CONFIG "$USER_HOME_DIR/.m2"

RUN git clone https://github.com/yona-lang/yona.git /yona
RUN cd yona/; mvn -B dependency:resolve
RUN cd /yona/; mvn -B package -DskipTests
RUN gu install -L /yona/component/yona-component.jar

WORKDIR /sources

ENTRYPOINT ["/opt/graalvm-ce-java17-22.0.0.2/bin/yona"]
