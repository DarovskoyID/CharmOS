add_library(crypto STATIC src/sha512.cpp src/ed25519.cpp)
target_include_directories(crypto PUBLIC include)
