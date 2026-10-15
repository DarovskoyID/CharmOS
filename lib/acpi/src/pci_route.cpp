#include <lib/acpi/pci_route.hpp>

namespace lib::acpi {

namespace {

using aml::Node;
using aml::Type;
using aml::Value;

bool is_bridge_id(const Value& v) {
  if (v.type == Type::Integer) return v.integer == eisa_id("PNP0A03") || v.integer == eisa_id("PNP0A08");
  if (v.type == Type::String) {
    lib::core::StringView s(v.str, v.size);
    return s == lib::core::StringView("PNP0A03") || s == lib::core::StringView("PNP0A08");
  }
  return false;
}

struct BridgeSearch {
  aml::Interpreter* in;
  Node* found;
};

bool search(Node* n, void* ctx) {
  auto* s = static_cast<BridgeSearch*>(ctx);
  if (s->found != nullptr) return false;
  if (s->in->type_of(n) != Type::Device) return true;
  static const char* const kIds[2] = {"_HID", "_CID"};
  for (const char* id : kIds) {
    Node* k = s->in->find(lib::core::StringView(id, 4), n);
    if (k == nullptr || k->parent != n) continue;
    Value v{};
    if (s->in->evaluate(k, nullptr, 0, &v) != aml::Error::Ok) continue;
    bool hit = is_bridge_id(v);
    // _CID может быть пакетом идентификаторов.
    for (uint64_t i = 0; !hit && v.type == Type::Package && i < v.size; ++i) hit = is_bridge_id(v.items[i]);
    if (hit) {
      s->found = n;
      return false;
    }
  }
  return true;
}

// Первый дескриптор прерывания в буфере ресурсов (_CRS).
bool parse_irq(const Value& buf, IntxRoute* out) {
  if (buf.type != Type::Buffer) return false;
  uint64_t i = 0;
  while (i < buf.size) {
    uint8_t tag = buf.data[i];
    if ((tag & 0x80) == 0) {
      uint8_t name = (tag >> 3) & 0x0f;
      uint8_t len = tag & 7;
      if (i + 1 + len > buf.size) return false;
      if (name == 0x0f) return false;  // End Tag
      if (name == 0x04 && len >= 2) {  // IRQ (§6.4.2.1)
        uint16_t mask = static_cast<uint16_t>(buf.data[i + 1] | buf.data[i + 2] << 8);
        if (mask == 0) return false;
        uint8_t info = len >= 3 ? buf.data[i + 3] : 0x01;  // без байта — фронт, активный высокий
        out->irq = static_cast<uint32_t>(__builtin_ctz(mask));
        out->isa = true;
        out->level = (info & 0x01) == 0;
        out->active_low = (info & 0x08) != 0;
        return true;
      }
      i += 1 + len;
    } else {
      if (i + 3 > buf.size) return false;
      uint8_t name = tag & 0x7f;
      uint16_t len = static_cast<uint16_t>(buf.data[i + 1] | buf.data[i + 2] << 8);
      if (i + 3 + len > buf.size) return false;
      if (name == 0x09 && len >= 6) {  // Extended Interrupt (§6.4.3.6)
        uint8_t flags = buf.data[i + 3];
        if (buf.data[i + 4] == 0) return false;
        const uint8_t* n = buf.data + i + 5;
        out->irq = static_cast<uint32_t>(n[0] | n[1] << 8 | n[2] << 16) | static_cast<uint32_t>(n[3]) << 24;
        out->isa = false;
        out->level = (flags & 0x02) == 0;
        out->active_low = (flags & 0x04) != 0;
        return true;
      }
      i += 3 + len;
    }
  }
  return false;
}

}  // namespace

uint32_t eisa_id(const char id[7]) {
  // Три буквы по 5 бит (A = 1), затем 4 шестнадцатеричные цифры; байты — big-endian в 32 битах LE.
  auto hex = [](char ch) -> uint32_t {
    return ch <= '9' ? static_cast<uint32_t>(ch - '0') : static_cast<uint32_t>(ch - 'A' + 10);
  };
  uint32_t v = (static_cast<uint32_t>(id[0] - '@') & 0x1f) << 26 | (static_cast<uint32_t>(id[1] - '@') & 0x1f) << 21 |
               (static_cast<uint32_t>(id[2] - '@') & 0x1f) << 16 | hex(id[3]) << 12 | hex(id[4]) << 8 |
               hex(id[5]) << 4 | hex(id[6]);
  return __builtin_bswap32(v);
}

Node* find_root_bridge(aml::Interpreter& in) {
  BridgeSearch s{&in, nullptr};
  Node* sb = in.find("\\_SB");
  in.walk(sb != nullptr ? sb : in.root(), &search, &s);
  return s.found;
}

bool route_intx(aml::Interpreter& in, Node* bridge, uint8_t device, uint8_t pin, IntxRoute* out) {
  if (bridge == nullptr) return false;
  Node* prt = in.find("_PRT", bridge);
  if (prt == nullptr || prt->parent != bridge) return false;
  Value table{};
  if (in.evaluate(prt, nullptr, 0, &table) != aml::Error::Ok || table.type != Type::Package) return false;
  for (uint64_t i = 0; i < table.size; ++i) {
    const Value& e = table.items[i];
    if (e.type != Type::Package || e.size < 4) continue;
    const Value& addr = e.items[0];
    const Value& p = e.items[1];
    if (addr.type != Type::Integer || p.type != Type::Integer) continue;
    if (((addr.integer >> 16) & 0xffff) != device || p.integer != pin) continue;
    const Value& src = e.items[2];
    const Value& idx = e.items[3];
    bool no_source = (src.type == Type::Integer && src.integer == 0) || src.type == Type::Uninitialized;
    if (no_source) {
      // §6.2.13: источник 0 — индекс это GSI, по уровню, активный низкий.
      if (idx.type != Type::Integer) return false;
      out->irq = static_cast<uint32_t>(idx.integer);
      out->isa = false;
      out->level = true;
      out->active_low = true;
      return true;
    }
    Node* link = in.resolve_ref(src, bridge);
    if (link == nullptr) return false;
    Node* crs = in.find("_CRS", link);
    if (crs == nullptr || crs->parent != link) return false;
    Value res{};
    if (in.evaluate(crs, nullptr, 0, &res) != aml::Error::Ok) return false;
    return parse_irq(res, out);
  }
  return false;
}

}  // namespace lib::acpi
