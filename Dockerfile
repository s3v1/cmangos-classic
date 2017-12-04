#To build this image:
#
#   docker build -t ${PWD##*/}:latest .
#
#This Dockerfile will compile the mangosd and realmd servers. It will the use supervisor to run them
#You'll need a database and the run the basic setup scripts on it before starting this service
#Check the contrib/docker/readme.md for more info
FROM ubuntu:14.04
RUN echo 'debconf debconf/frontend select Noninteractive' | debconf-set-selections
RUN apt-get update
RUN apt-get install -y build-essential
RUN apt-get install -y libtool
RUN apt-get install -y gcc-4.8
RUN apt-get install -y g++-4.8
RUN apt-get install -y make
RUN apt-get install -y cmake3
RUN apt-get install -y libboost-all-dev
RUN apt-get install -y libssl-dev
RUN apt-get install -y libmysqlclient-dev
RUN apt-get install -y mysql-client
RUN apt-get install -y libbz2-dev
RUN apt-get install -y git
# supervisor \
#  curl \
#  libbz2-dev \
#&& rm -rf /var/lib/apt/lists/*

#RUN groupadd -r cmangos && useradd -r -g cmangos cmangos
#USER cmangos
#WORKDIR /home/cmangos

#make a build dir, get sources and compile/install 
COPY . .
RUN mkdir -p _build _install
WORKDIR _build
ENV CC=gcc-4.8 CXX=g++-4.8
ENV PCH_FLAG=OFF
RUN cmake -DCMAKE_INSTALL_PREFIX=../_install -DBUILD_EXTRACTORS=ON -DCMAKE_BUILD_TYPE=Release -DBUILD_PLAYERBOT=OFF -DPCH=$PCH_FLAG ..
RUN make -j4
RUN make install


##Clean out source and build files, we don't need them anymore
##RUN rm -rf /cmangos/src
##RUN rm -rf /cmangos/build
#
##Move to the newly created server directory to get ready for running it
#WORKDIR /cmangos/server
#
##copy default config files
#RUN cp etc/mangosd.conf.dist etc/mangosd.conf
#RUN cp etc/realmd.conf.dist etc/realmd.conf
#
##Set default environment values
#ENV GAMETYPE=0
#ENV MYSQL_HOST=mysql
#ENV MYSQL_PORT=3306
#ENV MYSQL_USER=mangos
#ENV MYSQL_PASSWORD=mangos
#
#
##Fetch shell scripts
#COPY ./contrib/docker/*.sh /cmangos/server/
#RUN chmod +x *.sh
##EXPOSE PORTS
#EXPOSE 3724 8085
#
#
#COPY ./contrib/docker/supervisord.conf /etc/supervisor/conf.d/supervisord.conf
#
##CMD ["sh -c './update-server-config.sh && bash'"]
#CMD ["/usr/bin/supervisord"]