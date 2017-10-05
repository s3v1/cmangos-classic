#To build this image:
#
#   docker build -t ${PWD##*/}:alpine -f alpine.dockerfile .
#
FROM alpine:3.1
RUN apk update
RUN apk add build-base boost-dev
RUN apk add cmake
RUN apk add mysql-dev
RUN apk add bzip2-dev
#RUN apk add clang

RUN mkdir -p /mangos
WORKDIR /mangos
#make a build dir, get sources and compile/install 
COPY . .
RUN mkdir -p _build _install
WORKDIR _build
#ENV CC=gcc-4.8 CXX=g++-4.8
ENV PCH_FLAG=OFF
RUN cmake -DCMAKE_INSTALL_PREFIX=../_install -DBUILD_EXTRACTORS=OFF -DBUILD_PLAYERBOT=OFF -DPCH=$PCH_FLAG ..
#RUN make -j$(nproc)
RUN make -j2
RUN make install