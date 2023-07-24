FROM ghcr.io/wiiu-env/devkitppc:20230402

ENV PATH=$DEVKITPPC/bin:$PATH \
 WUT_ROOT=$DEVKITPRO/wut

RUN git config --global --add safe.directory /project

WORKDIR /project
