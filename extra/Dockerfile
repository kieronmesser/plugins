FROM debian:wheezy

ENV PAPILLON_INSTALL_DIR=/opt/Papillon
ENV LD_LIBRARY_PATH=/root/ffmpeg_build/lib:${PAPILLON_INSTALL_DIR}/lib

RUN apt-get update
RUN apt-get install -qy wget git mercurial sudo
RUN apt-get install -qy autoconf automake build-essential libtool gcc g++ 

ADD ./extra/buildFFmpegOnDebian.sh /root/
RUN /root/buildFFmpegOnDebian.sh

RUN mkdir /root/papillon
ADD Papillon-3.0.0.8626-debian_7_x64.deb /root/papillon
RUN apt-get install -qy gir1.2-gstreamer-0.10 gstreamer0.10-ffmpeg \
  gstreamer0.10-gconf gstreamer0.10-nice gstreamer0.10-plugins-bad \
  gstreamer0.10-plugins-base gstreamer0.10-plugins-good \
  gstreamer0.10-plugins-ugly gstreamer0.10-pulseaudio gstreamer0.10-x \
  libgstreamer-plugins-bad0.10-0 libgstreamer-plugins-base0.10-0 \
  libgstreamer0.10-0 libgsl0ldbl libatlas3-base libtbb-dev libtbb2
RUN dpkg -i /root/papillon/Papillon-3.0.0.8626-debian_7_x64.deb; apt-get install -f

RUN mkdir /root/papillon/plugins
ADD ./cmake/ /root/papillon/plugins/cmake/
ADD ./video/ /root/papillon/plugins/video/
ADD ./CMakeLists.txt /root/papillon/plugins/
RUN cd /root/papillon/plugins/; mkdir build; cd build; cmake .. -DFFMPEG_ROOT=/root/ffmpeg_build; make; sudo make install

RUN cp /opt/Papillon/examples/ExampleFaceLog2.cpp /opt/Papillon/examples/ExampleFaceLog2.cpp.bak
RUN sed -i 's/opencv/ffmpeg/' /opt/Papillon/examples/ExampleFaceLog2.cpp
RUN cd /opt/Papillon/examples/; cmake . ; make

CMD ["/root/Papillon/bin/ExampleFaceLog2","-nd"]