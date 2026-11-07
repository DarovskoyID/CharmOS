#include "crypto/unexportable_key.h"

namespace crypto {
// CharmOS has no TPM-backed provider. Do not silently substitute exportable
// software keys for hardware-protected keys, including the testing provider.
std::unique_ptr<UnexportableKeyProvider> GetSoftwareUnsecureUnexportableKeyProvider() {
  return nullptr;
}
}  // namespace crypto
