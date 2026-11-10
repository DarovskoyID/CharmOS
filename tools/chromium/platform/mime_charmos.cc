// CharmOS system MIME associations.
//
// CharmOS does not ship the XDG shared-mime-info database, so the platform
// lookup reports "unknown" and lets net::GetMimeTypeFromExtension's built-in
// table answer. This keeps downloads and resource loading consistent without a
// dependency on an external MIME database.
#include "net/base/platform_mime_util.h"

namespace net {

void PlatformMimeUtil::GetPlatformExtensionsForMimeType(
    std::string_view /*mime_type*/,
    std::unordered_set<base::FilePath::StringType>* /*extensions*/) const {}

bool PlatformMimeUtil::GetPlatformPreferredExtensionForMimeType(
    std::string_view /*mime_type*/,
    base::FilePath::StringType* /*extension*/) const {
  return false;
}

bool PlatformMimeUtil::GetPlatformMimeTypeFromExtension(
    const base::FilePath::StringType& /*ext*/,
    std::string* /*mime_type*/) const {
  return false;
}

}  // namespace net
