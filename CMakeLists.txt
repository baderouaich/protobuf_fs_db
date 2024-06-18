cmake_minimum_required(VERSION 3.20)
project(protobuf_fs_db)

# apt install protobuf-compiler (for linux)
include(FindProtobuf)
find_package(Protobuf REQUIRED)

file(GLOB PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/proto/*.proto")
# https://cmake.org/cmake/help/git-master/module/FindProtobuf.html
protobuf_generate_cpp(ProtoSources ProtoHeaders ${PROTO_FILES})
include_directories(${CMAKE_CURRENT_BINARY_DIR})


add_executable(${PROJECT_NAME} tests.cpp ${ProtoSources} ${ProtoHeaders})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_link_libraries(${PROJECT_NAME} PRIVATE protobuf::libprotobuf)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  DB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Database"
)
