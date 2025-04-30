.PHONY: exec* function* test-storage

function-debug: *.cpp *.hpp exec-debug
	./deploy-native.sh

function: *.cpp *.hpp exec
	./deploy-native.sh

function-debug-actionloop: *.cpp *.hpp exec-debug-actionloop
	./deploy.sh

function-actionloop: *.cpp *.hpp exec-actionloop
	./deploy.sh

exec-debug-actionloop: *.cpp *.hpp
	DOCKER_BUILDKIT=1 docker build -t hare1039/slsfs-datafunction:nightly \
                                   --build-arg debug=true \
							       --build-arg actionloop=true \
                                   -f Dockerfile .. && \
    docker run --rm hare1039/slsfs-datafunction:nightly cat /action/exec > exec && \
    chmod +x exec

exec-actionloop: *.cpp *.hpp
	DOCKER_BUILDKIT=1 docker build -t hare1039/slsfs-datafunction:nightly \
                                   --build-arg actionloop=true \
                                   -f Dockerfile .. && \
    docker run --rm hare1039/slsfs-datafunction:nightly cat /action/exec > exec && \
    chmod +x exec

exec-debug: *.cpp *.hpp
	DOCKER_BUILDKIT=1 docker build -t hare1039/slsfs-datafunction:nightly --progress=plain \
                                   --build-arg debug=true \
                                   -f Dockerfile .. && \
    docker run --rm hare1039/slsfs-datafunction:nightly cat /action/test-storage > test-storage && \
    chmod +x test-storage && \
    docker run --rm hare1039/slsfs-datafunction:nightly cat /action/exec > exec && \
    chmod +x exec

exec: *.cpp *.hpp
	DOCKER_BUILDKIT=1 docker build -t hare1039/slsfs-datafunction:nightly \
                                   -f Dockerfile .. && \
    docker run --rm hare1039/slsfs-datafunction:nightly cat /action/test-storage > test-storage && \
    chmod +x test-storage && \
    docker run --rm hare1039/slsfs-datafunction:nightly cat /action/exec > exec && \
    chmod +x exec

test-storage-debug: exec-debug

test-storage: exec
