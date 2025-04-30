# Setup step

Prepare docker. Or use conan to build dependencies.

# compile
```
make from-docker
```

Binary will generate to `/final/build-release/bin/run`;

# RUN
```
docker run -it --rm --name tst -p 12000:12000 `hare1039/transport:0.0.1`
```

# RUN client to interact with the proxy
Change `client.cpp` to test
```
docker exec -it tst /final/build-release/bin/client
```

# About design:
- Most of the logics are in `main.cpp`.
- Any function start with `start` is a async (defer) call
- The code to ensure auto-recovery of proxy is present in zookeeper.hpp under start_check_alive(). We maintain a liveliness flag in the zookeeper associated with each proxy to maintain if the proxy is alive(1), dead(0), launching(-1).

# Zookeeper debug:
- Install apache-zookeeper within your virtual machine.
- Cd to apache-zookeeper/bin
- Run zkCli.sh
- Commands: ls /slsfs/proxy, get /slsfs/proxy/proxyId
