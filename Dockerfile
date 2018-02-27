FROM ubuntu:latest

# grab first dependencies from apt
RUN apt-get update && \
apt-get install -y cmake libuv1 libuv1-dev libjansson4 libjansson-dev \
libzip4 libzip-dev git clang automake autoconf libtool libx264-dev libopus-dev \
yasm libpng-dev libjpeg-turbo8-dev gconf-service libasound2 libatk1.0-0 \
libcairo2 libcups2 libdbus-1-3 libfontconfig1 libfreetype6 libgconf-2-4 \
pkg-config curl libcurl4-gnutls-dev libpulse-dev pulseaudio alsa-utils \
gettext autopoint bison flex libfaac-dev librtmp-dev \
ca-certificates && \
curl -O https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb && \
dpkg -i google-chrome-stable_current_amd64.deb; apt-get -fy install && \
curl -O https://dl.google.com/linux/direct/google-chrome-beta_current_amd64.deb && \
dpkg -i google-chrome-beta_current_amd64.deb; apt-get -fy install && \
curl -O https://dl.google.com/linux/direct/google-chrome-unstable_current_amd64.deb && \
dpkg -i google-chrome-unstable_current_amd64.deb; apt-get -fy install && \
curl -sL https://deb.nodesource.com/setup_8.x | bash - && \
apt-get install nodejs && rm -rf /var/lib/apt/lists/*

# Create app directory
RUN mkdir -p /var/lib/ichabod/ext
WORKDIR /var/lib/ichabod/ext

ARG gst_version=1.12.4

RUN git clone https://github.com/GStreamer/gstreamer.git && \
cd gstreamer && \
git checkout ${gst_version} && \
./autogen.sh --disable-gtk-doc && ./configure && \
make -j4 && \
make install && \
cd .. && rm -rf gstreamer

RUN git clone https://github.com/GStreamer/gst-plugins-base.git && \
cd gst-plugins-base && \
git checkout ${gst_version} && \
./autogen.sh --disable-gtk-doc && ./configure && \
make -j4 && \
make install && \
cd .. && rm -rf gst-plugins-base

RUN git clone https://github.com/GStreamer/gst-plugins-good.git && \
cd gst-plugins-good && \
git checkout ${gst_version} && \
./autogen.sh --disable-gtk-doc && ./configure && \
make -j4 && \
make install && \
cd .. && rm -rf gst-plugins-good

RUN git clone https://github.com/GStreamer/gst-plugins-bad.git && \
cd gst-plugins-bad && \
git checkout ${gst_version} && \
./autogen.sh --disable-gtk-doc && ./configure && \
make -j4 && \
make install && \
cd .. && rm -rf gst-plugins-bad

RUN git clone https://github.com/GStreamer/gst-plugins-ugly.git && \
cd gst-plugins-ugly && \
git checkout ${gst_version} && \
./autogen.sh --disable-gtk-doc && ./configure && \
make -j4 && \
make install && \
cd .. && rm -rf gst-plugins-ugly

# Build deps: 3 of 3 - zmq
RUN curl -L https://github.com/zeromq/libzmq/releases/download/v4.2.1/zeromq-4.2.1.tar.gz | tar xz && \
cd zeromq-4.2.1 && ./autogen.sh && ./configure && make -j4 && make install && \
cd .. && rm -rf zeromq-4.2.1

WORKDIR /var/lib/ichabod

# Copy app source
COPY CMakeLists.txt /var/lib/ichabod/CMakeLists.txt
COPY gst_ichabod /var/lib/ichabod/gst_ichabod
#
# build barc binary
RUN mkdir -p /var/lib/ichabod/build /var/lib/ichabod/bin && \
cd /var/lib/ichabod/build && \
cmake .. && \
make && mv ichabod ../bin && cd .. && \
rm -rf ichabod build CMakeLists.txt

ENV LD_LIBRARY_PATH=/usr/local/lib
ENV PATH=${PATH}:/var/lib/ichabod/bin
ENV ICHABOD=/var/lib/ichabod/bin/ichabod
