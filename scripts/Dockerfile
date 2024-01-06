FROM ubuntu

RUN mkdir /app && apt update -y && apt full-upgrade -y
RUN apt install -y cmake upx-ucl wget xz-utils
RUN wget -qO- https://github.com/widgetii/ct-ng-builds/releases/download/2020-05-02/$TOOLCHAIN.tar.xz | tar xfJ - -C /opt

ENV PATH=/opt/$TOOLCHAIN/bin:$PATH

WORKDIR /app
