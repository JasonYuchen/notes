# Using Cpp Driver

[tutorial](https://www.mongodb.com/docs/languages/cpp/cpp-driver/current/installation/linux/#std-label-cpp-installation-linux)

```
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DMONGOCXX_OVERRIDE_DEFAULT_INSTALL_PREFIX=OFF \
    -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_CXX_STANDARD=17 \
    -DBUILD_SHARED_AND_STATIC_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DENABLE_TESTS=OFF
```

