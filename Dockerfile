FROM ghcr.io/wiiu-env/devkitppc:20230621

ENV PATH=$DEVKITPPC/bin:$PATH \
    WUT_ROOT=$DEVKITPRO/wut

RUN git clone --recursive https://github.com/yawut/libromfs-wiiu --single-branch && \
    cd libromfs-wiiu && \
    make -j$(nproc) && \
    make install

RUN git config --global --add safe.directory /project

WORKDIR /project
