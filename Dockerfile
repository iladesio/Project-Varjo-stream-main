FROM nvidia/cuda:12.6.1-devel-ubi8

# Copy in the container
COPY ./ /app

# Update and install dependencies
RUN dnf update -y && \
    dnf install -y wget make gcc gcc-c++ openssl-devel bzip2-devel unzip libffi-devel \
    zlib-devel curl cmake && \
    dnf clean all

# Install Python 3.8
RUN wget https://www.python.org/ftp/python/3.8.0/Python-3.8.0.tgz && \
    tar -xvf Python-3.8.0.tgz && \
    cd Python-3.8.0 && \
    ./configure --enable-optimizations && \
    make && \
    make altinstall && \
    cd .. && \
    rm -rf Python-3.8.0 Python-3.8.0.tgz

# Install pip
RUN /usr/local/bin/python3.8 -m ensurepip && \
    /usr/local/bin/python3.8 -m pip install --upgrade pip

# Install PyTorch and OpenCV
RUN /usr/local/bin/pip install torch opencv-python

# Cleaning cache
RUN dnf clean all
