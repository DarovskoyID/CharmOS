#include "tpm.hpp"

#include "../core/log.hpp"
#include "../mm/vmm.hpp"
#include "../platform/platform.hpp"
#include "../sync/wait.hpp"
#include <arch/x86_64/cpu.hpp>

namespace kernel::tpm {

namespace {
constinit sync::Mutex g_runtime_mutex{"tpm.runtime"};

// TCG PTP 1.07: локальность 0 по 0xFED4_0000 (§5.4, табл. 12), регистры CRB
// (§6.5.3.x): LOC_STATE 00h, LOC_CTRL 08h, LOC_STS 0Ch, INTF_ID 30h,
// CTRL_REQ 40h, CTRL_STS 44h, CTRL_START 4Ch, CMD_SIZE 58h, CMD_LADDR 5Ch,
// CMD_HADDR 60h, RSP_SIZE 64h, RSP_ADDR 68h, DATA_BUFFER 80h.
constexpr uint64_t kBase = 0xfed40000;
constexpr uint64_t kLocState = 0x00, kLocCtrl = 0x08, kLocSts = 0x0c, kIntfId = 0x30;
constexpr uint64_t kCtrlReq = 0x40, kCtrlSts = 0x44, kCtrlStart = 0x4c, kCmdSize = 0x58;
constexpr uint64_t kCmdLaddr = 0x5c, kCmdHaddr = 0x60, kRspSize = 0x64, kRspAddr = 0x68;
constexpr uint32_t kLocStateValid = 1u << 7, kLocStateAssigned = 1u << 1;  // табл. 37
constexpr uint32_t kLocCtrlRequest = 1u << 0;                             // табл. 38
constexpr uint32_t kLocStsGranted = 1u << 0;
constexpr uint32_t kReqCmdReady = 1u << 0, kReqGoIdle = 1u << 1;  // CTRL_REQ
constexpr uint32_t kStsError = 1u << 0, kStsIdle = 1u << 1;       // CTRL_STS
constexpr uint32_t kStart = 1u << 0;
constexpr uint32_t kIntfTypeCrb = 0x1;  // INTF_ID.InterfaceType (3:0) = 0001b — CRB активен
// Таймауты (§6.5.1.4): TIMEOUT_A 750 мс, TIMEOUT_C 200 мс; команды — с запасом.
constexpr uint64_t kTimeoutA = 750'000'000, kTimeoutC = 200'000'000, kCommandTimeout = 30'000'000'000ull;

// TPM 2.0 Library Part 2.
constexpr uint16_t kStNoSessions = 0x8001, kStSessions = 0x8002;
constexpr uint32_t kCcCreatePrimary = 0x131, kCcStartup = 0x144, kCcCreate = 0x153, kCcLoad = 0x157;
constexpr uint32_t kCcUnseal = 0x15e, kCcFlushContext = 0x165, kCcStartAuthSession = 0x176;
constexpr uint32_t kCcGetRandom = 0x17b, kCcPolicyPcr = 0x17f, kCcPolicyGetDigest = 0x189;
constexpr uint32_t kRhOwner = 0x40000001, kRhNull = 0x40000007, kRsPw = 0x40000009;
constexpr uint16_t kAlgKeyedHash = 0x0008, kAlgSymCipher = 0x0025, kAlgSha256 = 0x000b;
constexpr uint16_t kAlgAes = 0x0006, kAlgCfb = 0x0043, kAlgNull = 0x0010;
constexpr uint8_t kSePolicy = 0x01, kSeTrial = 0x03;
constexpr uint32_t kRcInitialize = 0x100;  // RC_VER1 + 0
// TPMA_OBJECT (табл. 37).
constexpr uint32_t kFixedTpm = 1u << 1, kFixedParent = 1u << 4, kSensitiveDataOrigin = 1u << 5;
constexpr uint32_t kUserWithAuth = 1u << 6, kNoDa = 1u << 10, kRestricted = 1u << 16, kDecrypt = 1u << 17;

constinit uint64_t g_regs = 0;
constinit bool g_present = false;
constinit uint32_t g_last_error = 0;
constinit uint64_t g_buffer = 0;  // буфер команд/ответов (адрес из CMD_LADDR)
constinit uint32_t g_buffer_size = 0;

uint32_t rd(uint64_t o) { return *reinterpret_cast<volatile uint32_t*>(g_regs + o); }
void wr(uint64_t o, uint32_t v) { *reinterpret_cast<volatile uint32_t*>(g_regs + o) = v; }

bool wait_bits(uint64_t reg, uint32_t mask, uint32_t want, uint64_t timeout) {
  uint64_t deadline = platform::now_ns() + timeout;
  while ((rd(reg) & mask) != want) {
    if (platform::now_ns() > deadline) return false;
    arch::x86_64::pause();  // до планировщика (загрузка) спать нельзя
  }
  return true;
}

// Команда: буфер собирается здесь (big-endian, Part 1 §18).
struct Cmd {
  uint8_t b[1024];
  uint32_t n;
  void u8(uint8_t v) { b[n++] = v; }
  void u16(uint16_t v) {
    u8(static_cast<uint8_t>(v >> 8));
    u8(static_cast<uint8_t>(v));
  }
  void u32(uint32_t v) {
    u16(static_cast<uint16_t>(v >> 16));
    u16(static_cast<uint16_t>(v));
  }
  void bytes(const uint8_t* p, size_t len) {
    for (size_t i = 0; i < len; ++i) u8(p[i]);
  }
  void tpm2b(const uint8_t* p, uint16_t len) {
    u16(len);
    bytes(p, len);
  }
  void begin(uint16_t tag, uint32_t cc) {
    n = 0;
    u16(tag);
    u32(0);  // commandSize — в end()
    u32(cc);
  }
  void end() {
    b[2] = static_cast<uint8_t>(n >> 24);
    b[3] = static_cast<uint8_t>(n >> 16);
    b[4] = static_cast<uint8_t>(n >> 8);
    b[5] = static_cast<uint8_t>(n);
  }
  // Область авторизации из одной сессии (Part 1 §18.6): пустые nonce и hmac.
  void auth(uint32_t session, uint8_t attrs) {
    u32(4 + 2 + 1 + 2);
    u32(session);
    u16(0);
    u8(attrs);
    u16(0);
  }
};

struct Rsp {
  uint8_t b[1024];
  uint32_t n, at;
  bool ok;
  uint8_t u8() {
    if (at + 1 > n) {
      ok = false;
      return 0;
    }
    return b[at++];
  }
  uint16_t u16() { return static_cast<uint16_t>(u8() << 8 | u8()); }
  uint32_t u32() { return static_cast<uint32_t>(u16()) << 16 | u16(); }
  // TPM2B целиком (с полем размера) — в out, для хранения как есть.
  bool raw2b(uint8_t* out, uint16_t cap, uint16_t* len) {
    if (at + 2 > n) return ok = false;
    uint16_t sz = static_cast<uint16_t>(b[at] << 8 | b[at + 1]);
    if (at + 2u + sz > n || 2u + sz > cap) return ok = false;
    for (uint32_t i = 0; i < 2u + sz; ++i) out[i] = b[at + i];
    *len = static_cast<uint16_t>(2u + sz);
    at += 2u + sz;
    return true;
  }
  bool skip2b() {
    uint16_t sz = u16();
    if (at + sz > n) return ok = false;
    at += sz;
    return ok;
  }
};

constinit Cmd g_cmd = {};
constinit Rsp g_rsp = {};

// Отправить g_cmd, ответ — в g_rsp (позиция — за заголовком). §6.5.3.x, рис. «CRB command flow».
bool execute() {
  g_cmd.end();
  if (g_cmd.n > g_buffer_size) return false;
  wr(kCtrlReq, kReqCmdReady);
  if (!wait_bits(kCtrlReq, kReqCmdReady, 0, kTimeoutC) || (rd(kCtrlSts) & (kStsIdle | kStsError)) != 0) {
    g_last_error = 0;
    return false;
  }
  auto* buf = reinterpret_cast<volatile uint8_t*>(g_buffer);
  for (uint32_t i = 0; i < g_cmd.n; ++i) buf[i] = g_cmd.b[i];
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  wr(kCtrlStart, kStart);
  bool done = wait_bits(kCtrlStart, kStart, 0, kCommandTimeout);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  g_rsp.n = 0;
  g_rsp.at = 0;
  g_rsp.ok = true;
  if (done) {
    uint32_t size = static_cast<uint32_t>(buf[2]) << 24 | static_cast<uint32_t>(buf[3]) << 16 |
                    static_cast<uint32_t>(buf[4]) << 8 | buf[5];
    if (size >= 10 && size <= sizeof g_rsp.b && size <= g_buffer_size) {
      for (uint32_t i = 0; i < size; ++i) g_rsp.b[i] = buf[i];
      g_rsp.n = size;
    }
  }
  wr(kCtrlReq, kReqGoIdle);
  (void)wait_bits(kCtrlReq, kReqGoIdle, 0, kTimeoutC);
  if (g_rsp.n == 0) {
    g_last_error = 0;
    return false;
  }
  (void)g_rsp.u16();  // tag
  (void)g_rsp.u32();  // size
  uint32_t rc = g_rsp.u32();
  if (rc != 0) {
    g_last_error = rc;
    return false;
  }
  return true;
}

void flush(uint32_t handle) {
  g_cmd.begin(kStNoSessions, kCcFlushContext);
  g_cmd.u32(handle);
  (void)execute();
}

// TPMT_PUBLIC первичного ключа хранения (sysarea-crypt.md §3): симметричный
// AES-128 CFB, restricted | decrypt — родитель для запечатанных объектов.
void put_primary_template(Cmd& c) {
  Cmd t{};
  t.u16(kAlgSymCipher);
  t.u16(kAlgSha256);
  t.u32(kFixedTpm | kFixedParent | kSensitiveDataOrigin | kUserWithAuth | kNoDa | kRestricted | kDecrypt);
  t.u16(0);  // authPolicy
  t.u16(kAlgAes);  // TPMT_SYM_DEF_OBJECT
  t.u16(128);
  t.u16(kAlgCfb);
  t.u16(0);  // unique (TPM2B_DIGEST)
  c.tpm2b(t.b, static_cast<uint16_t>(t.n));
}

// TPM2_CreatePrimary в Owner; handle первичного ключа.
bool create_primary(uint32_t* handle) {
  g_cmd.begin(kStSessions, kCcCreatePrimary);
  g_cmd.u32(kRhOwner);
  g_cmd.auth(kRsPw, 0);
  g_cmd.u16(4);  // TPM2B_SENSITIVE_CREATE: userAuth(0) + data(0)
  g_cmd.u16(0);
  g_cmd.u16(0);
  put_primary_template(g_cmd);
  g_cmd.u16(0);  // outsideInfo
  g_cmd.u32(0);  // creationPCR: count 0
  if (!execute()) return false;
  *handle = g_rsp.u32();
  return g_rsp.ok;
}

void put_pcr_selection(Cmd& c, uint32_t mask) {
  c.u32(1);  // TPML_PCR_SELECTION.count
  c.u16(kAlgSha256);
  c.u8(3);  // sizeofSelect: PCR 0–23
  c.u8(static_cast<uint8_t>(mask));
  c.u8(static_cast<uint8_t>(mask >> 8));
  c.u8(static_cast<uint8_t>(mask >> 16));
}

// Сессия политики (или пробная) с PolicyPCR по текущим значениям PCR.
bool policy_session(uint8_t type, uint32_t mask, uint32_t* session) {
  g_cmd.begin(kStNoSessions, kCcStartAuthSession);
  g_cmd.u32(kRhNull);  // tpmKey
  g_cmd.u32(kRhNull);  // bind
  uint8_t nonce[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};  // без соли и HMAC
  g_cmd.tpm2b(nonce, sizeof nonce);
  g_cmd.u16(0);  // encryptedSalt
  g_cmd.u8(type);
  g_cmd.u16(kAlgNull);  // symmetric
  g_cmd.u16(kAlgSha256);
  if (!execute()) return false;
  *session = g_rsp.u32();
  if (!g_rsp.ok) return false;
  g_cmd.begin(kStNoSessions, kCcPolicyPcr);
  g_cmd.u32(*session);
  g_cmd.u16(0);  // pcrDigest пуст — TPM берёт текущие значения
  put_pcr_selection(g_cmd, mask);
  if (!execute()) {
    flush(*session);
    return false;
  }
  return true;
}

}  // namespace

bool init() {
  auto m = mm::map_mmio(kBase, 0x1000);
  if (m.is_err()) return false;
  g_regs = m.value();
  uint32_t id = rd(kIntfId);
  if (id == 0xffffffff || (id & 0xf) != kIntfTypeCrb) {
    if (id != 0xffffffff && id != 0) kprint("tpm: interface {x} is not CRB (not supported yet)\n", id);
    return false;
  }
  if (!wait_bits(kLocState, kLocStateValid, kLocStateValid, kTimeoutA)) return false;
  if ((rd(kLocState) & kLocStateAssigned) == 0 || (rd(kLocSts) & kLocStsGranted) == 0) {
    wr(kLocCtrl, kLocCtrlRequest);
    if (!wait_bits(kLocSts, kLocStsGranted, kLocStsGranted, kTimeoutA)) return false;
  }
  uint64_t laddr = rd(kCmdLaddr) | uint64_t{rd(kCmdHaddr)} << 32;
  uint32_t csize = rd(kCmdSize);
  uint64_t raddr = *reinterpret_cast<volatile uint64_t*>(g_regs + kRspAddr);
  uint32_t rsize = rd(kRspSize);
  // Простой случай: команда и ответ в одном буфере внутри окна локальности (так у QEMU и fTPM).
  if (laddr != raddr || csize == 0 || csize > 0x1000 - 0x80 || laddr < kBase || laddr + csize > kBase + 0x1000) {
    kprint("tpm: unsupported CRB buffer layout\n");
    return false;
  }
  g_buffer = g_regs + (laddr - kBase);
  g_buffer_size = csize < rsize ? csize : rsize;
  g_present = true;
  // Прошивка обычно уже выполнила Startup; если нет — TPM_RC_INITIALIZE.
  uint8_t probe[8];
  if (!get_random(probe, sizeof probe) && g_last_error == kRcInitialize) {
    g_cmd.begin(kStNoSessions, kCcStartup);
    g_cmd.u16(0);  // TPM_SU_CLEAR
    (void)execute();
  }
  kprint("tpm: TPM 2.0 (CRB), buffer {} bytes\n", g_buffer_size);
  return true;
}

bool present() { return g_present; }
sync::Mutex& runtime_mutex() { return g_runtime_mutex; }
uint32_t last_error() { return g_last_error; }

bool get_random(uint8_t* out, size_t n) {
  while (n != 0) {
    g_cmd.begin(kStNoSessions, kCcGetRandom);
    g_cmd.u16(static_cast<uint16_t>(n < 32 ? n : 32));
    if (!execute()) return false;
    uint16_t got = g_rsp.u16();
    if (!g_rsp.ok || got == 0 || got > n || g_rsp.at + got > g_rsp.n) return false;
    for (uint16_t i = 0; i < got; ++i) out[i] = g_rsp.b[g_rsp.at + i];
    out += got;
    n -= got;
  }
  return true;
}

bool seal(const uint8_t* data, size_t n, uint32_t pcr_mask, Sealed* out) {
  if (!g_present || n > 128) return false;
  // Дайджест политики — пробной сессией (Part 1 §19.7.5).
  uint32_t trial;
  if (!policy_session(kSeTrial, pcr_mask, &trial)) return false;
  g_cmd.begin(kStNoSessions, kCcPolicyGetDigest);
  g_cmd.u32(trial);
  bool got = execute();
  uint8_t policy[64];
  uint16_t policy_len = g_rsp.u16();
  if (got) got = g_rsp.ok && policy_len <= sizeof policy && g_rsp.at + policy_len <= g_rsp.n;
  if (got) {
    for (uint16_t i = 0; i < policy_len; ++i) policy[i] = g_rsp.b[g_rsp.at + i];
  }
  flush(trial);
  if (!got) return false;

  uint32_t parent;
  if (!create_primary(&parent)) return false;
  g_cmd.begin(kStSessions, kCcCreate);
  g_cmd.u32(parent);
  g_cmd.auth(kRsPw, 0);
  g_cmd.u16(static_cast<uint16_t>(4 + n));  // TPM2B_SENSITIVE_CREATE
  g_cmd.u16(0);                             // userAuth
  g_cmd.tpm2b(data, static_cast<uint16_t>(n));
  Cmd t{};  // TPMT_PUBLIC запечатанного объекта
  t.u16(kAlgKeyedHash);
  t.u16(kAlgSha256);
  t.u32(kFixedTpm | kFixedParent | kNoDa);  // userWithAuth снят: только по политике
  t.tpm2b(policy, policy_len);
  t.u16(kAlgNull);  // TPMT_KEYEDHASH_SCHEME
  t.u16(0);         // unique
  g_cmd.tpm2b(t.b, static_cast<uint16_t>(t.n));
  g_cmd.u16(0);  // outsideInfo
  g_cmd.u32(0);  // creationPCR
  bool ok = execute();
  if (ok) {
    (void)g_rsp.u32();  // parameterSize
    ok = g_rsp.raw2b(out->priv, sizeof out->priv, &out->priv_size) &&
         g_rsp.raw2b(out->pub, sizeof out->pub, &out->pub_size);
  }
  flush(parent);
  return ok;
}

bool unseal(const uint8_t* pub, uint16_t pub_size, const uint8_t* priv, uint16_t priv_size, uint32_t pcr_mask,
            uint8_t* out, size_t n) {
  if (!g_present) return false;
  uint32_t parent;
  if (!create_primary(&parent)) return false;
  g_cmd.begin(kStSessions, kCcLoad);
  g_cmd.u32(parent);
  g_cmd.auth(kRsPw, 0);
  g_cmd.bytes(priv, priv_size);  // уже TPM2B (с размером)
  g_cmd.bytes(pub, pub_size);
  bool ok = execute();
  uint32_t item = ok ? g_rsp.u32() : 0;
  flush(parent);
  if (!ok || !g_rsp.ok) return false;
  uint32_t session;
  if (!policy_session(kSePolicy, pcr_mask, &session)) {
    flush(item);
    return false;
  }
  g_cmd.begin(kStSessions, kCcUnseal);
  g_cmd.u32(item);
  g_cmd.auth(session, 0);  // continueSession снят: сессия закроется сама
  ok = execute();
  if (ok) {
    (void)g_rsp.u32();  // parameterSize
    uint16_t len = g_rsp.u16();
    ok = g_rsp.ok && len == n && g_rsp.at + len <= g_rsp.n;
    for (uint16_t i = 0; ok && i < len; ++i) out[i] = g_rsp.b[g_rsp.at + i];
  } else {
    flush(session);
  }
  for (uint8_t& b : g_rsp.b) b = 0;  // в ответе был ключ
  flush(item);
  return ok;
}

}  // namespace kernel::tpm
