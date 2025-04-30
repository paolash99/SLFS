
#FROM hare1039/dockerskeleton:latest AS builder

#FROM openwhisk/dockerskeleton
FROM openwhisk/dockerskeleton:latest

RUN apk update && \
    apk add --no-cache --virtual .build-deps \
                                 bzip2-dev \
                                 g++ \
                                 libc-dev \
                                 make

ENV FLASK_PROXY_PORT 8080

ADD libslsfs /action/libslsfs

ADD app /action/

RUN cd /action && time g++ -O3 -std=c++17 -o exec *.cpp

CMD ["/bin/bash", "-c", "cd actionProxy && python -u actionproxy.py"]
