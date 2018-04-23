FROM ubuntu:latest

# grab first dependencies from apt
RUN apt-get update && \
apt-get install -y cmake libuv1 libuv1-dev libjansson4 libjansson-dev \
libzip4 libzip-dev git clang automake autoconf libtool libx264-dev libopus-dev \
yasm libpng-dev libjpeg-turbo8-dev gconf-service libasound2 libatk1.0-0 \
libcairo2 libcups2 libdbus-1-3 libfontconfig1 libfreetype6 libgconf-2-4 \
pkg-config curl libcurl4-gnutls-dev libpulse-dev pulseaudio alsa-utils \
gettext autopoint bison flex libfaac-dev librtmp-dev libfaad-dev gtk-doc-tools \
openssl libssl-dev ca-certificates libvpx-dev libogg-dev \
tcpdump net-tools graphviz && \
curl https://dl-ssl.google.com/linux/linux_signing_key.pub | apt-key add - && \
echo 'deb [arch=amd64] http://dl.google.com/linux/chrome/deb/ stable main' | \
tee /etc/apt/sources.list.d/google-chrome.list && apt-get update && \
apt-get -f install && apt-get install -y google-chrome-stable && \
curl -sL https://deb.nodesource.com/setup_8.x | bash - && \
apt-get install -y nodejs && rm -rf /var/lib/apt/lists/*

# Create app directory
RUN mkdir -p /var/lib/ichabod/ext
WORKDIR /var/lib/ichabod/ext

ARG gst_version=1.14

RUN git clone https://github.com/cisco/libsrtp.git && \
cd libsrtp && git checkout v2.1.0 && ./configure && \
make && make install && cd .. && rm -rf libsrtp

RUN git clone https://github.com/GStreamer/gstreamer.git && \
cd gstreamer && \
git checkout ${gst_version} && \
./autogen.sh --disable-gtk-doc && ./configure && \
make -j$(nproc) && \
make install && \
cd .. && rm -rf gstreamer

# libnice needs gstreamer installed to build gst nicesrc/nicesink
RUN git clone https://github.com/libnice/libnice.git && \
cd libnice && git checkout 0.1.14 && ./autogen.sh --with-gstreamer && \
make -j$(nproc) && make install && \
cd .. && rm -rf libnice

RUN git clone https://github.com/GStreamer/gst-plugins-base.git && \
cd gst-plugins-base && \
git checkout ${gst_version} && \
./autogen.sh --disable-gtk-doc && ./configure && \
make -j$(nproc) && \
make install && \
cd .. && rm -rf gst-plugins-base

RUN git clone https://github.com/GStreamer/gst-plugins-good.git && \
cd gst-plugins-good && \
git checkout ${gst_version} && \
./autogen.sh --disable-gtk-doc && ./configure && \
make -j$(nproc) && \
make install && \
cd .. && rm -rf gst-plugins-good

RUN git clone https://github.com/GStreamer/gst-plugins-bad.git && \
cd gst-plugins-bad && \
git checkout ${gst_version} && \
./autogen.sh --disable-gtk-doc && ./configure && \
make -j$(nproc) && \
make install && \
cd .. && rm -rf gst-plugins-bad

RUN git clone https://github.com/GStreamer/gst-plugins-ugly.git && \
cd gst-plugins-ugly && \
git checkout ${gst_version} && \
./autogen.sh --disable-gtk-doc && ./configure && \
make -j$(nproc) && \
make install && \
cd .. && rm -rf gst-plugins-ugly

RUN git clone https://github.com/GStreamer/gst-libav.git && \
cd gst-libav && \
git checkout ${gst_version} && \
./autogen.sh --disable-gtk-doc && ./configure && \
make -j$(nproc) && \
make install && \
cd .. && rm -rf gst-libav

# Build deps: 3 of 3 - zmq
ARG zmq_version=4.2.5
RUN curl -L https://github.com/zeromq/libzmq/releases/download/v${zmq_version}/zeromq-${zmq_version}.tar.gz | tar xz && \
cd zeromq-${zmq_version} && ./autogen.sh && ./configure && make -j$(nproc) && \
make install && cd .. && rm -rf zeromq-${zmq_version}

WORKDIR /var/lib/ichabod

# Copy app source
COPY CMakeLists.txt /var/lib/ichabod/CMakeLists.txt
COPY gst_ichabod /var/lib/ichabod/gst_ichabod

#
# build ichabod binary
RUN mkdir -p /var/lib/ichabod/build /var/lib/ichabod/bin && \
cd /var/lib/ichabod/build && \
cmake .. && \
make && mv ichabod ../bin && cd .. && \
rm -rf ichabod gst_ichabod build CMakeLists.txt

ENV LD_LIBRARY_PATH=/usr/local/lib
ENV PATH=${PATH}:/var/lib/ichabod/bin
ENV ICHABOD=/var/lib/ichabod/bin/ichabod
