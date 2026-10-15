# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/mnt/a/CharmOS")
  file(MAKE_DIRECTORY "/mnt/a/CharmOS")
endif()
file(MAKE_DIRECTORY
  "/mnt/a/CharmOS/build2/user"
  "/mnt/a/CharmOS/build2/user-prefix"
  "/mnt/a/CharmOS/build2/user-prefix/tmp"
  "/mnt/a/CharmOS/build2/user-prefix/src/user-stamp"
  "/mnt/a/CharmOS/build2/user-prefix/src"
  "/mnt/a/CharmOS/build2/user-prefix/src/user-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/mnt/a/CharmOS/build2/user-prefix/src/user-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/mnt/a/CharmOS/build2/user-prefix/src/user-stamp${cfgdir}") # cfgdir has leading slash
endif()
