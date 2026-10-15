// Интерпретатор AML на настоящих DSDT: QEMU q35 и VMware (снятые с acpi=dump,
// tools/acpi-extract.py). Оборудование — заглушка, которая помнит записи.
#include <lib/acpi/aml.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "test.hpp"

using namespace lib::acpi::aml;

namespace {

std::vector<uint8_t> load_file(const std::string& name) {
  std::string path = std::string(CHARMOS_TEST_DATA) + "/acpi/" + name;
  FILE* f = std::fopen(path.c_str(), "rb");
  std::vector<uint8_t> d;
  if (f == nullptr) return d;
  int ch;
  while ((ch = std::fgetc(f)) != EOF) d.push_back(static_cast<uint8_t>(ch));
  std::fclose(f);
  return d;
}

std::map<uint64_t, uint64_t> g_mem;  // ключ — (space << 56) | адрес
int g_logs = 0;

uint64_t host_read(void*, Space s, uint64_t a, uint32_t, const PciAddress*) {
  auto it = g_mem.find((uint64_t{static_cast<uint8_t>(s)} << 56) | a);
  return it == g_mem.end() ? 0 : it->second;
}
void host_write(void*, Space s, uint64_t a, uint32_t, uint64_t v, const PciAddress*) {
  g_mem[(uint64_t{static_cast<uint8_t>(s)} << 56) | a] = v;
}
void host_log(void*, const char*) { ++g_logs; }

void* alloc(void*, size_t n) { return std::malloc(n); }

struct Loaded {
  std::vector<uint8_t> dsdt;
  Interpreter in;
  Error err;
};

void load(Loaded& l, const char* file) {
  l.dsdt = load_file(file);
  Host h{nullptr, host_read, host_write, host_log, nullptr, nullptr};
  l.in.init({alloc, nullptr}, {alloc, nullptr}, h);
  l.err = l.dsdt.empty() ? Error::BadTable : l.in.load_table(l.dsdt.data(), l.dsdt.size());
}

struct Count {
  Interpreter* in;
  int devices;
  int methods;
};

bool count_node(Node* n, void* ctx) {
  auto* c = static_cast<Count*>(ctx);
  if (c->in->type_of(n) == Type::Device) ++c->devices;
  if (c->in->type_of(n) == Type::Method) ++c->methods;
  return true;
}

// _PRT: пакет записей {Address, Pin, Source, SourceIndex}.
bool check_prt(Interpreter& in, Node* prt, int* entries) {
  Value v{};
  if (in.evaluate(prt, nullptr, 0, &v) != Error::Ok || v.type != Type::Package) return false;
  for (uint64_t i = 0; i < v.size; ++i) {
    const Value& e = v.items[i];
    if (e.type != Type::Package || e.size != 4) return false;
    if (e.items[0].type != Type::Integer || e.items[1].type != Type::Integer) return false;
    if ((e.items[0].integer & 0xffff) != 0xffff) return false;  // функция — любая (§6.2.13)
  }
  *entries = static_cast<int>(v.size);
  return true;
}

bool find_prt(Node* n, void* ctx) {
  auto* out = static_cast<Node**>(ctx);
  if (std::memcmp(n->name, "_PRT", 4) == 0 && *out == nullptr) *out = n;
  return true;
}

}  // namespace

TEST(aml_qemu_loads) {
  Loaded l;
  load(l, "qemu-q35-dsdt.aml");
  CHECK(l.err == Error::Ok);
  if (l.err != Error::Ok) {
    std::printf("  qemu: %s at 0x%zx\n", error_name(l.err), l.in.error_offset());
    return;
  }
  Count c{&l.in, 0, 0};
  l.in.walk(l.in.root(), &count_node, &c);
  std::printf("  qemu: %u nodes, %d devices, %d methods, %u-bit integers\n", l.in.node_count(), c.devices,
              c.methods, l.in.integer_width());
  CHECK(c.devices > 10 && c.methods > 10);
  CHECK(l.in.find("\\_SB.PCI0") != nullptr);
  // _S5 (§7.4.2): пакет, первый элемент — SLP_TYPa.
  Value s5{};
  CHECK(l.in.evaluate("\\_S5", &s5) == Error::Ok && s5.type == Type::Package && s5.size >= 1 &&
        s5.items[0].type == Type::Integer);
  // _HID PCI-корня — EISA ID (PNP0A08 у q35).
  Value hid{};
  CHECK(l.in.evaluate("\\_SB.PCI0._HID", &hid) == Error::Ok && hid.type == Type::Integer);
  CHECK(hid.integer == 0x080ad041);  // EisaId("PNP0A08")
  Node* prt = nullptr;
  l.in.walk(l.in.find("\\_SB.PCI0"), &find_prt, &prt);
  int entries = 0;
  CHECK(prt != nullptr && check_prt(l.in, prt, &entries) && entries >= 4);
  std::printf("  qemu: _PRT has %d entries\n", entries);
}

TEST(aml_vmware_loads) {
  Loaded l;
  load(l, "vmware-dsdt.aml");
  CHECK(l.err == Error::Ok);
  if (l.err != Error::Ok) {
    std::printf("  vmware: %s\n", error_name(l.err));
    return;
  }
  Count c{&l.in, 0, 0};
  l.in.walk(l.in.root(), &count_node, &c);
  std::printf("  vmware: %u nodes, %d devices, %d methods, %u-bit integers\n", l.in.node_count(), c.devices,
              c.methods, l.in.integer_width());
  CHECK(c.devices > 10 && c.methods > 10);
  Value s5{};
  CHECK(l.in.evaluate("\\_S5", &s5) == Error::Ok && s5.type == Type::Package);
  Node* prt = nullptr;
  l.in.walk(l.in.find("\\_SB"), &find_prt, &prt);
  int entries = 0;
  bool ok = prt != nullptr && check_prt(l.in, prt, &entries);
  CHECK(ok);
  char path[128];
  if (prt != nullptr) l.in.path_of(prt, path, sizeof path);
  std::printf("  vmware: %s has %d entries\n", prt != nullptr ? path : "(no _PRT)", entries);
}

TEST(aml_methods_and_fields) {
  // Ручная таблица: Name(INT, 5); OperationRegion(R, SystemIO, 0x80, 4);
  // Field(R, ByteAcc, NoLock, Preserve){ F8, 8, F4, 4 }
  // Method(M, 1){ Store(Add(Arg0, INT), F8); Return(Multiply(F8, 2)) }
  std::vector<uint8_t> t(36, 0);
  std::memcpy(t.data(), "SSDT", 4);
  t[8] = 2;
  const uint8_t body[] = {
      0x08, 'I', 'N', 'T', '_', 0x0a, 0x05,                          // Name(INT_, 5)
      0x5b, 0x80, 'R', '_', '_', '_', 0x01, 0x0a, 0x80, 0x0a, 0x04,  // OpRegion(R___, IO, 0x80, 4)
      0x5b, 0x81, 0x10, 'R', '_', '_', '_', 0x01,                    // Field(R___, ByteAcc)
      'F', '8', '_', '_', 0x08, 'F', '4', '_', '_', 0x04,
      0x14, 0x1b, 'M', '_', '_', '_', 0x01,                          // Method(M___, 1)
      0x70, 0x72, 0x68, 'I', 'N', 'T', '_', 0x00, 'F', '8', '_', '_',  // Store(Add(Arg0, INT_), F8__)
      0xa4, 0x77, 'F', '8', '_', '_', 0x0a, 0x02, 0x00,              // Return(Multiply(F8__, 2))
  };
  t.insert(t.end(), body, body + sizeof body);
  uint32_t len = static_cast<uint32_t>(t.size());
  std::memcpy(t.data() + 4, &len, 4);
  Interpreter in;
  Host h{nullptr, host_read, host_write, host_log, nullptr, nullptr};
  in.init({alloc, nullptr}, {alloc, nullptr}, h);
  CHECK(in.load_table(t.data(), t.size()) == Error::Ok);
  Value arg{};
  arg.type = Type::Integer;
  arg.integer = 10;
  Value r{};
  CHECK(in.evaluate(in.find("\\M___"), &arg, 1, &r) == Error::Ok && r.type == Type::Integer && r.integer == 30);
  CHECK(g_mem[(uint64_t{1} << 56) | 0x80] == 15);  // F8 записано в порт 0x80
  Value f4{};
  g_mem[(uint64_t{1} << 56) | 0x81] = 0xab;
  CHECK(in.evaluate("\\F4__", &f4) == Error::Ok && f4.integer == 0x0b);  // младшие 4 бита байта 0x81
}
