#ifndef BUILD_CHARMOS_FILESYSTEM_CHARMOS_H_
#define BUILD_CHARMOS_FILESYSTEM_CHARMOS_H_

#include <filesystem>
#include <system_error>

namespace std::filesystem {

bool create_directories(const path& directory);
bool create_directories(const path& directory, std::error_code& error);
bool remove(const path& file);
bool remove(const path& file, std::error_code& error);

}  // namespace std::filesystem

#endif  // BUILD_CHARMOS_FILESYSTEM_CHARMOS_H_
