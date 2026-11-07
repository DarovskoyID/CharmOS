#ifndef CHARMOS_OZONE_PLATFORM_H_
#define CHARMOS_OZONE_PLATFORM_H_

#include "ui/ozone/public/ozone_platform.h"

namespace gfx { class ClientNativePixmapFactory; }
namespace ui {
OzonePlatform* CreateOzonePlatformCharmos();
gfx::ClientNativePixmapFactory* CreateClientNativePixmapFactoryCharmos();
}  // namespace ui
#endif
