include(hunter_config)

hunter_config(absl VERSION 20200923.2 CMAKE_ARGS CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD})
hunter_config(Boost VERSION 1.70.0-p1 CMAKE_ARGS B2_CXXFLAGS=${B2_CXXFLAGS})
hunter_config(Catch VERSION 2.2.1)
hunter_config(OpenSSL VERSION 1.1.1c)
hunter_config(Protobuf VERSION 3.5.2-p1)
hunter_config(CURL VERSION 7.60.0-p2)
hunter_config(benchmark VERSION 1.3.0)
hunter_config(gRPC VERSION 1.10.0-p2)
hunter_config(GTest VERSION 1.10.0-p1)
hunter_config(gflags VERSION 2.2.1 CMAKE_ARGS)
hunter_config(glog VERSION 0.4.0)
hunter_config(ZLIB VERSION 1.2.8-p3 CMAKE_ARGS CMAKE_POSITION_INDEPENDENT_CODE=TRUE)
hunter_config(c-ares VERSION 1.14.0-p0 CMAKE_ARGS CARES_STATIC=TRUE CARES_SHARED=FALSE CARES_STATIC_PIC=TRUE)
hunter_config(BZip VERSION 1.0.6-p4)
hunter_config(double-conversion VERSION 3.0.0)
hunter_config(jemalloc VERSION 5.0.1 CONFIGURATION_TYPES Release)
hunter_config(brpc VERSION 2020.07.31.3)
hunter_config(braft VERSION 2020.07.31.1)
hunter_config(flatbuffers VERSION 1.10.0.li1 CMAKE_ARGS
  FLATBUFFERS_BUILD_FLATC=ON
  CMAKE_CXX_FLAGS=-std=c++11 # NB: We have to do this because Flatbuffers has a bug where it will skip setting some
                             # flags if CMAKE_TOOLCHAIN_FILE is defined.
  )
hunter_config(rocksdb VERSION 6.8.1 CMAKE_ARGS
  WITH_GFLAGS=OFF
  WITH_TESTS=OFF
  WITH_TOOLS=OFF
  WITH_BENCHMARK_TOOLS=OFF
  )
hunter_config(leveldb VERSION 1.20 CMAKE_ARGS
  CMAKE_CXX_STANDARD=11
  CMAKE_CXX_STANDARD_REQUIRED=ON
  CMAKE_CXX_EXTENSIONS=OFF
  )
hunter_config(CLI11 VERSION 1.8.0)
hunter_config(fu2 VERSION 4.1.0.1)
