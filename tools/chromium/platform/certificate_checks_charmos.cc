// Offline guest regression for the real Chromium/BoringSSL certificate path.
// Test anchors are instance-local; nothing is added to production trust.
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/memory_mapped_file.h"
#include "base/memory/ref_counted.h"
#include "net/base/net_errors.h"
#include "net/cert/cert_status_flags.h"
#include "net/cert/cert_net_fetcher.h"
#include "net/cert/cert_verify_proc.h"
#include "net/cert/cert_verify_result.h"
#include "net/cert/crl_set.h"
#include "net/cert/ct_policy_enforcer.h"
#include "net/cert/do_nothing_ct_verifier.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util.h"
#include "net/log/net_log_with_source.h"
#include "third_party/boringssl/src/include/openssl/pool.h"
#include "third_party/boringssl/src/pki/cert_errors.h"
#include "third_party/boringssl/src/pki/parsed_certificate.h"
#include <stdio.h>
#include <charm/libc.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

namespace {
bool CheckMappedFiles() {
  base::FilePath path("/bin/net-test-ok.pem");
  auto original = base::ReadFileToBytes(path);
  if (!original || original->size() < 64) return false;
  int descriptors = charm_open_fd_count();
  void* mapping_base = nullptr;
  {
    base::MemoryMappedFile mapped;
    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
    if (!mapped.Initialize(std::move(file), {17, 31}) || mapped.length() != 31 ||
        memcmp(mapped.data(), original->data() + 17, 31)) return false;
    mapping_base = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mapped.data()) & ~uintptr_t{4095});
  }
  // Destroying an unaligned view must remove the actual original mapping.
  errno = 0;
  if (madvise(mapping_base, 1, MADV_NORMAL) != -1 || errno != ENOMEM) return false;
  {
    base::MemoryMappedFile copy;
    if (!copy.Initialize(path, base::MemoryMappedFile::READ_WRITE_COPY)) return false;
    copy.data()[0] ^= 1;
    auto unchanged = base::ReadFileToBytes(path);
    if (!unchanged || *unchanged != *original) return false;
  }
  {
    base::MemoryMappedFile rejected;
    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ | base::File::FLAG_WRITE);
    if (!file.IsValid() || rejected.Initialize(std::move(file), {0, 8192},
                                               base::MemoryMappedFile::READ_WRITE_EXTEND)) return false;
    base::File unchanged(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
    if (!unchanged.IsValid() || unchanged.GetLength() != static_cast<int64_t>(original->size())) return false;
  }
  if (charm_open_fd_count() != descriptors) return false;
  printf("Native files: private snapshots, unaligned views, cleanup and shared-write rejection ok\n");
  return true;
}
scoped_refptr<net::X509Certificate> Load(const char* path) {
  auto bytes = base::ReadFileToBytes(base::FilePath(path));
  if (!bytes) return nullptr;
  auto certificates = net::X509Certificate::CreateCertificateListFromBytes(
      *bytes, net::X509Certificate::FORMAT_AUTO);
  return certificates.size() == 1 ? certificates[0] : nullptr;
}
scoped_refptr<net::CertVerifyProc> Verifier(net::CertVerifyProc::InstanceParams params) {
  return net::CertVerifyProc::CreateBuiltinWithChromeRootStore(
      nullptr, net::CRLSet::BuiltinCRLSet(),
      std::make_unique<net::DoNothingCTVerifier>(),
      base::MakeRefCounted<net::DefaultCTPolicyEnforcer>(),
      nullptr, nullptr, std::move(params), std::nullopt);
}
int Verify(net::CertVerifyProc* verifier, net::X509Certificate* certificate,
           const char* host, net::CertVerifyResult* result) {
  return verifier->Verify(certificate, host, "", "",
      net::CertVerifyProc::VERIFY_DISABLE_NETWORK_FETCHES, result,
      net::NetLogWithSource());
}
}
bool CheckNativeCertificates() {
  if (!CheckMappedFiles()) { printf("Native files: mapping checks failed\n"); return false; }
  auto leaf = Load("/bin/net-test-ok.pem");
  auto root = Load("/bin/net-test-root.pem");
  auto expired = Load("/bin/net-test-expired.pem");
  if (!leaf || !root || !expired) {
    printf("Native certificates: fixture parsing failed\n");
    return false;
  }
  auto production = Verifier({});
  net::CertVerifyResult result;
  if (Verify(production.get(), leaf.get(), "127.0.0.1", &result) == net::OK ||
      !(result.cert_status & net::CERT_STATUS_AUTHORITY_INVALID)) {
    printf("Native certificates: untrusted issuer was not rejected\n");
    return false;
  }
  bssl::CertErrors errors;
  auto parsed_root = bssl::ParsedCertificate::Create(bssl::UpRef(root->cert_buffer()),
      net::x509_util::DefaultParseCertificateOptions(), &errors);
  if (!parsed_root) return false;
  net::CertVerifyProc::InstanceParams params;
  params.additional_trust_anchors.push_back(parsed_root);
  auto trusted = Verifier(std::move(params));
  int valid = Verify(trusted.get(), leaf.get(), "127.0.0.1", &result);
  if (valid != net::OK) {
    printf("Native certificates: trusted chain failed %d status %u\n", valid, result.cert_status);
    return false;
  }
  if (Verify(trusted.get(), leaf.get(), "wrong.example", &result) == net::OK ||
      !(result.cert_status & net::CERT_STATUS_COMMON_NAME_INVALID)) return false;
  if (Verify(trusted.get(), expired.get(), "127.0.0.1", &result) == net::OK ||
      !(result.cert_status & net::CERT_STATUS_DATE_INVALID)) return false;
  auto* buffer = leaf->cert_buffer();
  std::vector<uint8_t> damaged(CRYPTO_BUFFER_data(buffer),
                             CRYPTO_BUFFER_data(buffer) + CRYPTO_BUFFER_len(buffer));
  damaged.back() ^= 1; // DER remains parseable; the actual signature is invalid.
  auto tampered = net::X509Certificate::CreateFromBytes(damaged);
  if (!tampered || Verify(trusted.get(), tampered.get(), "127.0.0.1", &result) == net::OK)
    return false;
  printf("Native certificates: trusted chain, issuer, hostname, expiry and signature ok\n");
  return true;
}
