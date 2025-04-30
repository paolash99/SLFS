# Setup step

Prepare docker. Or use conan to build dependencies.

# compile
```
make from-docker
```

Binary will generate to `/final/build-release/bin/run`;

# RUN
```
docker run -it --rm --name tst -p 12000:12000 hare1039/transport:0.0.1
```

# RUN client to interact with the proxy
Change `client.cpp` to test
```
docker exec -it tst /final/build-release/bin/client
```

# About design:
- Most of the logics are in `main.cpp`.
- Any function start with `start` is a async (defer) call