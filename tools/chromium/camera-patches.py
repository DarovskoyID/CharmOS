"""No-camera video capture factory for CharmOS.

Upstream returns nullptr on platforms without a capture backend. A page that
asks for the camera (YouTube does, on load) then dereferences it and the
renderer dies. CharmOS has no camera driver yet, so the factory enumerates no
devices; a page gets a normal "no camera" answer instead of a crash.
"""

CAPTURE_FACTORY = "media/capture/video/create_video_capture_device_factory.cc"
CAPTURE_FACTORY_DIGEST = "a0337caead1c63e35a364a8d3eba6afb041a0ebcb77620d0d61839d4e5f2fbed"

NO_CAMERA_BEFORE = """  NOTIMPLEMENTED();
  return nullptr;
"""
NO_CAMERA_AFTER = """  // CharmOS has no camera driver yet: enumerate no devices instead of
  // returning nullptr, which crashed any page that asked for the camera.
  class NoCameraFactory : public VideoCaptureDeviceFactory {
   public:
    VideoCaptureErrorOrDevice CreateDevice(
        const VideoCaptureDeviceDescriptor&) override {
      return VideoCaptureErrorOrDevice(
          VideoCaptureError::kErrorFakeDeviceIntentionallyEmittingErrorEvent);
    }
    void GetDevicesInfo(GetDevicesInfoCallback callback) override {
      std::move(callback).Run({});
    }
  };
  return std::make_unique<NoCameraFactory>();
"""


def camera_patches():
    replacements = [(NO_CAMERA_BEFORE, NO_CAMERA_AFTER)]
    return {CAPTURE_FACTORY: (CAPTURE_FACTORY_DIGEST, replacements)}
