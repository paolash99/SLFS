
#.PHONY function function-debug exec-debug exec

function-debug: *.cpp
	DOCKER_BUILDKIT=1 CPPBUILDARGS="-DAS_ACTIONLOOP -ggdb" ./push.sh && ./deploy.sh

function: *.cpp
	DOCKER_BUILDKIT=1 CPPBUILDARGS="-DAS_ACTIONLOOP -O3 -DNDEBUG" ./push.sh && ./deploy.sh

exec-debug: *.cpp
	DOCKER_BUILDKIT=1 CPPBUILDARGS="-ggdb" ./push.sh && chmod +x exec

exec: *.cpp
	DOCKER_BUILDKIT=1 CPPBUILDARGS="-O3 -DNDEBUG" ./push.sh && chmod +x exec

deploy:
	wsk -i action update hare1039/slsfs-actionproxy --docker 'openwhisk/actionloop-base:nightly'

main.zip:
	docker run -i --rm 'action-golang-v1.16:nightly' -compile main <$(SRC) >$(ZIP)
