add_library(crypto STATIC src/sha512.cpp src/ed25519.cpp src/aes.cpp)
target_include_directories(crypto PUBLIC include)
# Measured TCG cold boot exceeded 60 seconds while encrypting a 15 MiB system
# area. Optimize the existing AES loops; retain the same algorithm and vectors.
set_source_files_properties(src/aes.cpp PROPERTIES COMPILE_OPTIONS "-O2")
