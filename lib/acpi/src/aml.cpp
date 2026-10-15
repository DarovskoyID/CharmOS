// Интерпретатор AML. Грамматика и коды — ACPI 6.5 §20.2–§20.3; смысл
// операторов — §19.6; пространство имён и правила поиска имён — §5.3.
#include <lib/acpi/aml.hpp>

namespace lib::acpi::aml {

using lib::core::StringView;

const char* error_name(Error e) {
  switch (e) {
    case Error::Ok: return "ok";
    case Error::NoMemory: return "no memory";
    case Error::BadTable: return "bad table";
    case Error::BadOpcode: return "bad opcode";
    case Error::Truncated: return "truncated";
    case Error::NotFound: return "not found";
    case Error::BadType: return "bad type";
    case Error::BadArgs: return "bad arguments";
    case Error::DivideByZero: return "divide by zero";
    case Error::OutOfRange: return "out of range";
    case Error::Fatal: return "fatal";
    case Error::TooDeep: return "too deep";
    case Error::Unsupported: return "unsupported";
  }
  return "?";
}

// --- арены ---------------------------------------------------------------------------------

struct Chunk {
  Chunk* next;
  size_t size;
};

void* Interpreter::Arena::get(size_t size) {
  size = (size + 15) & ~size_t{15};
  if (size > left) {
    size_t want = size + sizeof(Chunk) + 16 > 64 * 1024 ? size + sizeof(Chunk) + 16 : 64 * 1024;
    Chunk* c = nullptr;
    // Свободный кусок после reset — если подходит.
    for (Chunk** it = &chunks; *it != nullptr; it = &(*it)->next) {
      if ((*it)->size >= want) {
        c = *it;
        *it = c->next;
        break;
      }
    }
    if (c == nullptr) {
      c = static_cast<Chunk*>(src.alloc(src.ctx, want));
      if (c == nullptr) return nullptr;
      c->size = want;
    }
    c->next = used;
    used = c;
    cur = reinterpret_cast<uint8_t*>(c) + ((sizeof(Chunk) + 15) & ~size_t{15});
    left = c->size - ((sizeof(Chunk) + 15) & ~size_t{15});
  }
  void* p = cur;
  cur += size;
  left -= size;
  for (size_t i = 0; i < size; ++i) static_cast<uint8_t*>(p)[i] = 0;
  return p;
}

void Interpreter::Arena::reset() {
  while (used != nullptr) {
    Chunk* c = used;
    used = c->next;
    c->next = chunks;
    chunks = c;
  }
  cur = nullptr;
  left = 0;
}

// --- объекты -------------------------------------------------------------------------------

enum class FieldKind : uint8_t { Normal, Index, Bank };

struct Object {
  Type type;
  Value value;  // данные Name, а также Integer-«константы»
  // Method
  const uint8_t* code;
  size_t code_len;
  uint8_t argc;
  bool native_osi;
  // OpRegion
  Space space;
  uint64_t offset, length;
  // FieldUnit / BufferField
  FieldKind kind;
  Node* region;      // Normal/Bank: регион
  Node* index_reg;   // Index: поле индекса
  Node* data_reg;    // Index: поле данных
  Node* bank_reg;    // Bank: поле банка
  uint64_t bank_value;
  uint64_t bit_offset, bit_length;
  uint8_t access;    // 0 Any, 1 Byte, 2 Word, 3 DWord, 4 QWord, 5 Buffer
  uint8_t update;    // 0 Preserve, 1 WriteAsOnes, 2 WriteAsZeros
  Value* buffer;     // BufferField: буфер-источник (значение в пространстве имён)
};

namespace {

constexpr uint32_t kMaxDepth = 32;
constexpr uint32_t kMaxLoops = 1u << 20;  // защита от бесконечного While

enum class Flow : uint8_t { Normal, Break, Continue, Return };

struct Frame {
  Value args[7];
  Value locals[8];
  Value ret;
  Node* scope;
};

struct Ctx {
  Interpreter* in;
  const uint8_t* p;
  Frame* frame;   // nullptr — загрузка таблицы
  Node* scope;
  Flow flow;
  Error err;
};

bool fail(Ctx& c, Error e) {
  if (c.err == Error::Ok) c.err = e;
  return false;
}

bool name_char(uint8_t b) { return (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_'; }
bool lead_char(uint8_t b) { return (b >= 'A' && b <= 'Z') || b == '_'; }

// Разобранный NameString (§20.2.2).
struct Path {
  bool root;
  uint32_t up;     // число '^'
  uint32_t count;  // сегментов
  const uint8_t* segs;
};

bool is_name_start(uint8_t b) { return b == '\\' || b == '^' || b == 0x2e || b == 0x2f || lead_char(b); }

}  // namespace

// Реализация как набор свободных функций с доступом к полям Interpreter.
struct Impl {
  static uint64_t mask(Ctx& c, uint64_t v) { return c.in->int_bits_ == 32 ? v & 0xffffffffull : v; }
  static uint64_t ones(Ctx& c) { return mask(c, ~uint64_t{0}); }

  // --- примитивы чтения ----------------------------------------------------------------------

  static bool need(Ctx& c, const uint8_t* end, size_t n) {
    if (static_cast<size_t>(end - c.p) < n) return fail(c, Error::Truncated);
    return true;
  }

  // §20.2.4: возвращает конец пакета (от позиции первого байта длины).
  static bool pkglength(Ctx& c, const uint8_t* end, const uint8_t** pkg_end) {
    const uint8_t* start = c.p;
    if (!need(c, end, 1)) return false;
    uint8_t lead = *c.p++;
    uint32_t follow = lead >> 6;
    uint64_t len;
    if (follow == 0) {
      len = lead & 0x3f;
    } else {
      if (!need(c, end, follow)) return false;
      len = lead & 0x0f;
      for (uint32_t i = 0; i < follow; ++i) len |= uint64_t{*c.p++} << (4 + 8 * i);
    }
    if (len > static_cast<uint64_t>(end - start)) return fail(c, Error::Truncated);
    *pkg_end = start + len;
    return true;
  }

  static bool name_string(Ctx& c, const uint8_t* end, Path* out) {
    *out = {};
    if (!need(c, end, 1)) return false;
    if (*c.p == '\\') {
      out->root = true;
      ++c.p;
    } else {
      while (c.p < end && *c.p == '^') {
        ++out->up;
        ++c.p;
      }
    }
    if (!need(c, end, 1)) return false;
    uint8_t b = *c.p;
    if (b == 0x00) {  // NullName
      ++c.p;
      return true;
    }
    if (b == 0x2e) {
      ++c.p;
      out->count = 2;
    } else if (b == 0x2f) {
      ++c.p;
      if (!need(c, end, 1)) return false;
      out->count = *c.p++;
    } else {
      out->count = 1;
    }
    if (!need(c, end, size_t{4} * out->count)) return false;
    out->segs = c.p;
    for (uint32_t i = 0; i < 4 * out->count; ++i) {
      if (!name_char(c.p[i])) return fail(c, Error::BadOpcode);
    }
    c.p += 4 * out->count;
    return true;
  }

  // --- пространство имён ------------------------------------------------------------------

  static bool seg_eq(const char* a, const uint8_t* b) {
    return a[0] == static_cast<char>(b[0]) && a[1] == static_cast<char>(b[1]) && a[2] == static_cast<char>(b[2]) &&
           a[3] == static_cast<char>(b[3]);
  }

  static Node* child(Node* n, const uint8_t* seg) {
    for (Node* k = n->child; k != nullptr; k = k->next) {
      if (seg_eq(k->name, seg)) return k;
    }
    return nullptr;
  }

  static Node* new_node(Interpreter* in, Node* parent, const uint8_t* seg) {
    auto* n = static_cast<Node*>(in->perm_.get(sizeof(Node)));
    if (n == nullptr) return nullptr;
    for (int i = 0; i < 4; ++i) n->name[i] = static_cast<char>(seg[i]);
    n->parent = parent;
    if (parent != nullptr) {
      // В конец списка: порядок обхода — порядок объявления.
      Node** it = &parent->child;
      while (*it != nullptr) it = &(*it)->next;
      *it = n;
    }
    ++in->nodes_;
    return n;
  }

  // Узел, от которого отсчитывается путь (корень или scope с подъёмами).
  static Node* base(Interpreter* in, Node* scope, const Path& p) {
    if (p.root) return in->root_;
    Node* n = scope;
    for (uint32_t i = 0; i < p.up; ++i) {
      if (n->parent == nullptr) return nullptr;
      n = n->parent;
    }
    return n;
  }

  // §5.3: одиночный сегмент без префиксов ищется вверх по областям.
  static Node* resolve(Interpreter* in, Node* scope, const Path& p) {
    Node* b = base(in, scope, p);
    if (b == nullptr) return nullptr;
    // Только префиксы (\ или ^) и NullName — сам базовый узел: Scope(\).
    if (p.count == 0) return p.root || p.up != 0 ? b : nullptr;
    if (p.count == 1 && !p.root && p.up == 0) {
      for (Node* s = b; s != nullptr; s = s->parent) {
        if (Node* k = child(s, p.segs)) return k;
      }
      return nullptr;
    }
    Node* n = b;
    for (uint32_t i = 0; i < p.count && n != nullptr; ++i) n = child(n, p.segs + 4 * i);
    return n;
  }

  // Создать узел по пути (промежуточные — найти, недостающие — создать как Scope).
  static Node* create(Ctx& c, const Path& p) {
    Node* n = base(c.in, c.scope, p);
    if (n != nullptr && p.count == 0 && (p.root || p.up != 0)) return n;
    if (n == nullptr || p.count == 0) {
      fail(c, Error::NotFound);
      return nullptr;
    }
    for (uint32_t i = 0; i < p.count; ++i) {
      Node* k = child(n, p.segs + 4 * i);
      if (k == nullptr) k = new_node(c.in, n, p.segs + 4 * i);
      if (k == nullptr) {
        fail(c, Error::NoMemory);
        return nullptr;
      }
      n = k;
    }
    return n;
  }

  static Object* new_object(Ctx& c, Type t) {
    auto* o = static_cast<Object*>(c.in->perm_.get(sizeof(Object)));
    if (o == nullptr) {
      fail(c, Error::NoMemory);
      return nullptr;
    }
    o->type = t;
    return o;
  }

  // --- значения ---------------------------------------------------------------------------

  static Interpreter::Arena& arena(Ctx& c, bool permanent) { return permanent ? c.in->perm_ : c.in->scratch_; }

  static bool copy(Ctx& c, Value* dst, const Value& src, bool permanent) {
    *dst = src;
    if (src.type == Type::String || src.type == Type::NameRef) {
      auto* s = static_cast<char*>(arena(c, permanent).get(src.size + 1));
      if (s == nullptr) return fail(c, Error::NoMemory);
      for (uint64_t i = 0; i < src.size; ++i) s[i] = src.str[i];
      dst->str = s;
    } else if (src.type == Type::Buffer) {
      auto* d = static_cast<uint8_t*>(arena(c, permanent).get(src.size != 0 ? src.size : 1));
      if (d == nullptr) return fail(c, Error::NoMemory);
      for (uint64_t i = 0; i < src.size; ++i) d[i] = src.data[i];
      dst->data = d;
    } else if (src.type == Type::Package) {
      auto* items = static_cast<Value*>(arena(c, permanent).get(sizeof(Value) * (src.size != 0 ? src.size : 1)));
      if (items == nullptr) return fail(c, Error::NoMemory);
      for (uint64_t i = 0; i < src.size; ++i) {
        if (!copy(c, &items[i], src.items[i], permanent)) return false;
      }
      dst->items = items;
    }
    return true;
  }

  static Value integer(uint64_t v) {
    Value r{};
    r.type = Type::Integer;
    r.integer = v;
    return r;
  }

  static int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
  }

  // Неявное преобразование в Integer (§19.3.5).
  static bool to_int(Ctx& c, const Value& v, uint64_t* out) {
    switch (v.type) {
      case Type::Integer:
        *out = v.integer;
        return true;
      case Type::Buffer: {
        uint64_t r = 0;
        uint64_t n = v.size < 8 ? v.size : 8;
        for (uint64_t i = 0; i < n; ++i) r |= uint64_t{v.data[i]} << (8 * i);
        *out = mask(c, r);
        return true;
      }
      case Type::String: {
        uint64_t r = 0;
        uint64_t i = 0;
        if (v.size >= 2 && v.str[0] == '0' && (v.str[1] == 'x' || v.str[1] == 'X')) i = 2;
        for (; i < v.size; ++i) {
          int d = hex_digit(v.str[i]);
          if (d < 0) break;
          r = (r << 4) | static_cast<uint64_t>(d);
        }
        *out = mask(c, r);
        return true;
      }
      default:
        return fail(c, Error::BadType);
    }
  }

  // --- поля ---------------------------------------------------------------------------------

  static uint32_t access_bits(const Object* f) {
    switch (f->access) {
      case 2: return 16;
      case 3: return 32;
      case 4: return 64;
      case 0: {
        // AnyAcc: самая широкая из 8/16/32, в выровненную единицу которой поле влезает.
        constexpr uint32_t kWidths[2] = {32, 16};
        for (uint32_t w : kWidths) {
          if (f->bit_offset / w == (f->bit_offset + f->bit_length - 1) / w) return w;
        }
        return 8;
      }
      default: return 8;
    }
  }

  // PCI-адрес региона: _ADR устройства над регионом, _BBN и _SEG корневого моста.
  static void pci_address(Ctx& c, Node* region, PciAddress* out) {
    *out = {};
    Node* dev = region->parent;
    uint64_t v;
    static const uint8_t kAdr[4] = {'_', 'A', 'D', 'R'}, kBbn[4] = {'_', 'B', 'B', 'N'}, kSeg[4] = {'_', 'S', 'E', 'G'};
    if (dev != nullptr) {
      if (Node* adr = child(dev, kAdr); adr != nullptr && value_int(c, adr, &v)) {
        out->device = static_cast<uint8_t>(v >> 16);
        out->function = static_cast<uint8_t>(v);
      }
      for (Node* up = dev; up != nullptr; up = up->parent) {
        if (Node* bbn = child(up, kBbn); bbn != nullptr && value_int(c, bbn, &v)) {
          out->bus = static_cast<uint8_t>(v);
          if (Node* seg = child(up, kSeg); seg != nullptr && value_int(c, seg, &v)) {
            out->segment = static_cast<uint16_t>(v);
          }
          break;
        }
      }
    }
  }

  static bool value_int(Ctx& c, Node* n, uint64_t* out) {
    Value v{};
    Ctx sub = c;
    sub.err = Error::Ok;
    if (!read_object(sub, n, &v)) return false;
    return to_int(sub, v, out);
  }

  // Единица доступа к пространству: адрес в байтах, ширина в битах.
  static bool unit_read(Ctx& c, Object* f, uint64_t byte, uint32_t width, uint64_t* out) {
    if (f->kind == FieldKind::Index) {
      // §19.6.64: в индексный регистр — смещение единицы, затем данные.
      if (!write_field(c, f->index_reg->object, integer(byte))) return false;
      Value v{};
      if (!read_field(c, f->data_reg->object, &v)) return false;
      return to_int(c, v, out);
    }
    if (f->kind == FieldKind::Bank) {
      if (!write_field(c, f->bank_reg->object, integer(f->bank_value))) return false;
    }
    Object* r = f->region->object;
    if (r == nullptr || r->type != Type::OpRegion) return fail(c, Error::BadType);
    PciAddress pci{};
    if (r->space == Space::PciConfig) pci_address(c, f->region, &pci);
    if (r->space != Space::SystemMemory && r->space != Space::SystemIO && r->space != Space::PciConfig) {
      *out = 0;  // EC, SMBus и пр. — не поддержаны: читается 0
      return true;
    }
    *out = c.in->host_.read(c.in->host_.ctx, r->space, r->offset + byte, width,
                            r->space == Space::PciConfig ? &pci : nullptr);
    return true;
  }

  static bool unit_write(Ctx& c, Object* f, uint64_t byte, uint32_t width, uint64_t value) {
    if (f->kind == FieldKind::Index) {
      if (!write_field(c, f->index_reg->object, integer(byte))) return false;
      return write_field(c, f->data_reg->object, integer(value));
    }
    if (f->kind == FieldKind::Bank) {
      if (!write_field(c, f->bank_reg->object, integer(f->bank_value))) return false;
    }
    Object* r = f->region->object;
    if (r == nullptr || r->type != Type::OpRegion) return fail(c, Error::BadType);
    if (r->space != Space::SystemMemory && r->space != Space::SystemIO && r->space != Space::PciConfig) return true;
    PciAddress pci{};
    if (r->space == Space::PciConfig) pci_address(c, f->region, &pci);
    c.in->host_.write(c.in->host_.ctx, r->space, r->offset + byte, width, value,
                      r->space == Space::PciConfig ? &pci : nullptr);
    return true;
  }

  static bool read_field(Ctx& c, Object* f, Value* out) {
    if (f == nullptr) return fail(c, Error::BadType);
    if (f->type == Type::BufferField) {
      uint64_t r = 0;
      uint64_t bits = f->bit_length;
      if (bits > 64) {
        // Больше 64 бит — буфер.
        uint64_t n = (bits + 7) / 8;
        auto* d = static_cast<uint8_t*>(c.in->scratch_.get(n));
        if (d == nullptr) return fail(c, Error::NoMemory);
        for (uint64_t i = 0; i < bits; ++i) {
          uint64_t sb = f->bit_offset + i;
          if (sb / 8 < f->buffer->size && ((f->buffer->data[sb / 8] >> (sb % 8)) & 1) != 0) d[i / 8] |= uint8_t(1u << (i % 8));
        }
        *out = {};
        out->type = Type::Buffer;
        out->data = d;
        out->size = n;
        return true;
      }
      for (uint64_t i = 0; i < bits; ++i) {
        uint64_t sb = f->bit_offset + i;
        if (sb / 8 < f->buffer->size && ((f->buffer->data[sb / 8] >> (sb % 8)) & 1) != 0) r |= uint64_t{1} << i;
      }
      *out = integer(r);
      return true;
    }
    if (f->type != Type::FieldUnit) return fail(c, Error::BadType);
    uint32_t w = access_bits(f);
    if (f->bit_length > 64) {
      uint64_t n = (f->bit_length + 7) / 8;
      auto* d = static_cast<uint8_t*>(c.in->scratch_.get(n));
      if (d == nullptr) return fail(c, Error::NoMemory);
      for (uint64_t i = 0; i < f->bit_length; i += 8) {
        Object part = *f;
        part.bit_offset = f->bit_offset + i;
        part.bit_length = f->bit_length - i < 8 ? f->bit_length - i : 8;
        part.access = 1;
        Value b{};
        if (!read_field(c, &part, &b)) return false;
        d[i / 8] = static_cast<uint8_t>(b.integer);
      }
      *out = {};
      out->type = Type::Buffer;
      out->data = d;
      out->size = n;
      return true;
    }
    uint64_t result = 0;
    uint64_t got = 0;
    uint64_t bit = f->bit_offset;
    while (got < f->bit_length) {
      uint64_t unit = bit / w * (w / 8);  // байт начала единицы
      uint64_t inbit = bit % w;
      uint64_t take = w - inbit < f->bit_length - got ? w - inbit : f->bit_length - got;
      uint64_t raw;
      if (!unit_read(c, f, unit, w, &raw)) return false;
      uint64_t part = (raw >> inbit) & (take == 64 ? ~uint64_t{0} : (uint64_t{1} << take) - 1);
      result |= part << got;
      got += take;
      bit += take;
    }
    *out = integer(result);
    return true;
  }

  static bool write_field(Ctx& c, Object* f, const Value& v) {
    if (f == nullptr) return fail(c, Error::BadType);
    uint64_t value = 0;
    if (v.type == Type::Buffer || v.type == Type::String) {
      // Буфер в поле до 64 бит — младшие байты; длиннее — побайтно.
      if (f->bit_length > 64 && v.type == Type::Buffer) {
        for (uint64_t i = 0; i < f->bit_length; i += 8) {
          Object part = *f;
          part.bit_offset = f->bit_offset + i;
          part.bit_length = f->bit_length - i < 8 ? f->bit_length - i : 8;
          part.access = f->type == Type::FieldUnit ? 1 : part.access;
          uint64_t b = i / 8 < v.size ? v.data[i / 8] : 0;
          if (!write_field(c, &part, integer(b))) return false;
        }
        return true;
      }
    }
    if (!to_int(c, v, &value)) return false;
    if (f->type == Type::BufferField) {
      for (uint64_t i = 0; i < f->bit_length && i < 64; ++i) {
        uint64_t sb = f->bit_offset + i;
        if (sb / 8 >= f->buffer->size) break;
        uint8_t m = static_cast<uint8_t>(1u << (sb % 8));
        if (((value >> i) & 1) != 0) {
          f->buffer->data[sb / 8] |= m;
        } else {
          f->buffer->data[sb / 8] &= static_cast<uint8_t>(~m);
        }
      }
      return true;
    }
    if (f->type != Type::FieldUnit) return fail(c, Error::BadType);
    uint32_t w = access_bits(f);
    uint64_t done = 0;
    uint64_t bit = f->bit_offset;
    while (done < f->bit_length) {
      uint64_t unit = bit / w * (w / 8);
      uint64_t inbit = bit % w;
      uint64_t take = w - inbit < f->bit_length - done ? w - inbit : f->bit_length - done;
      uint64_t fmask = (take == 64 ? ~uint64_t{0} : (uint64_t{1} << take) - 1) << inbit;
      uint64_t raw = 0;
      if (fmask != (w == 64 ? ~uint64_t{0} : (uint64_t{1} << w) - 1)) {
        // Неполная единица: Preserve — прочитать, WriteAsOnes/Zeros — заполнить.
        if (f->update == 0) {
          if (!unit_read(c, f, unit, w, &raw)) return false;
        } else if (f->update == 1) {
          raw = ~uint64_t{0};
        }
      }
      uint64_t part = (value >> done) << inbit;
      raw = (raw & ~fmask) | (part & fmask);
      if (w < 64) raw &= (uint64_t{1} << w) - 1;
      if (!unit_write(c, f, unit, w, raw)) return false;
      done += take;
      bit += take;
    }
    return true;
  }

  // --- объекты как значения ---------------------------------------------------------------

  // Значение именованного объекта (поля читаются, данные — как есть).
  static bool read_object(Ctx& c, Node* n, Value* out) {
    Object* o = n->object;
    if (o == nullptr) {
      *out = {};
      out->type = Type::Scope;
      out->node = n;
      return true;
    }
    switch (o->type) {
      case Type::FieldUnit:
      case Type::BufferField:
        return read_field(c, o, out);
      case Type::Integer:
      case Type::String:
      case Type::Buffer:
      case Type::Package:
      case Type::Uninitialized:
        *out = o->value;
        return true;
      default:
        // Device, Mutex и пр. — ссылка на сам объект.
        *out = {};
        out->type = o->type;
        out->node = n;
        return true;
    }
  }

  // --- цели (Target, SuperName) ------------------------------------------------------------

  enum class TKind : uint8_t { None, Local, Arg, Node, Ref, Debug };
  struct Target {
    TKind kind;
    uint32_t index;
    Node* node;
    Value* ref;  // элемент пакета/буфера или переменная, на которую ссылаются
  };

  static bool super_name(Ctx& c, const uint8_t* end, Target* t, bool allow_missing = false) {
    *t = {};
    if (!need(c, end, 1)) return false;
    uint8_t op = *c.p;
    if (op == 0x00) {
      ++c.p;
      return true;  // NullName — без цели
    }
    if (op >= 0x60 && op <= 0x67) {
      ++c.p;
      t->kind = TKind::Local;
      t->index = op - 0x60u;
      return true;
    }
    if (op >= 0x68 && op <= 0x6e) {
      ++c.p;
      t->kind = TKind::Arg;
      t->index = op - 0x68u;
      return true;
    }
    if (op == 0x5b && end - c.p >= 2 && c.p[1] == 0x31) {
      c.p += 2;
      t->kind = TKind::Debug;
      return true;
    }
    if (op == 0x71 || op == 0x83 || op == 0x88) {
      // RefOf / DerefOf / Index как цель — ссылка.
      Value v{};
      if (!termarg(c, end, &v)) return false;
      return ref_target(c, v, t);
    }
    if (is_name_start(op)) {
      Path p;
      if (!name_string(c, end, &p)) return false;
      Node* n = resolve(c.in, c.scope, p);
      if (n == nullptr) {
        if (allow_missing) return true;
        return fail(c, Error::NotFound);
      }
      // Метод как SuperName — его вызов (§20.2.5: MethodInvocation — UserTermObj).
      if (n->object != nullptr && n->object->type == Type::Method) {
        Value v{};
        if (!invoke(c, end, n, &v)) return false;
        return ref_target(c, v, t);
      }
      t->kind = TKind::Node;
      t->node = n;
      return true;
    }
    return fail(c, Error::BadOpcode);
  }

  static bool ref_target(Ctx& c, const Value& v, Target* t) {
    if (v.type != Type::Reference) return fail(c, Error::BadType);
    if (v.target != nullptr) {
      t->kind = TKind::Ref;
      t->ref = v.target;
    } else {
      t->kind = TKind::Node;
      t->node = v.node;
    }
    return true;
  }

  static Value* var(Ctx& c, const Target& t) {
    if (c.frame == nullptr) return nullptr;
    return t.kind == TKind::Local ? &c.frame->locals[t.index] : &c.frame->args[t.index];
  }

  static bool read_target(Ctx& c, const Target& t, Value* out) {
    switch (t.kind) {
      case TKind::Local:
      case TKind::Arg: {
        Value* v = var(c, t);
        if (v == nullptr) return fail(c, Error::BadOpcode);
        // Arg со ссылкой — через неё.
        if (t.kind == TKind::Arg && v->type == Type::Reference) return deref(c, *v, out);
        *out = *v;
        return true;
      }
      case TKind::Node: return read_object(c, t.node, out);
      case TKind::Ref: *out = *t.ref; return true;
      default: *out = integer(0); return true;
    }
  }

  static bool deref(Ctx& c, const Value& r, Value* out) {
    if (r.type != Type::Reference) return fail(c, Error::BadType);
    if (r.target != nullptr) {
      *out = *r.target;
      return true;
    }
    return read_object(c, r.node, out);
  }

  // Store (§19.6.132): в именованные данные — с преобразованием к их типу.
  static bool store(Ctx& c, const Target& t, const Value& v) {
    switch (t.kind) {
      case TKind::None: return true;
      case TKind::Debug: {
        if (c.in->host_.log != nullptr) {
          char buf[96];
          if (v.type == Type::String) {
            size_t n = v.size < sizeof buf - 1 ? v.size : sizeof buf - 1;
            for (size_t i = 0; i < n; ++i) buf[i] = v.str[i];
            buf[n] = 0;
          } else {
            uint64_t x = v.type == Type::Integer ? v.integer : 0;
            int k = 0;
            const char* pre = "Debug: 0x";
            while (*pre != 0) buf[k++] = *pre++;
            for (int s = 60; s >= 0; s -= 4) buf[k++] = "0123456789abcdef"[(x >> s) & 15];
            buf[k] = 0;
          }
          c.in->host_.log(c.in->host_.ctx, buf);
        }
        return true;
      }
      case TKind::Local:
      case TKind::Arg: {
        Value* dst = var(c, t);
        if (dst == nullptr) return fail(c, Error::BadOpcode);
        if (t.kind == TKind::Arg && dst->type == Type::Reference) {
          Target via{};
          if (!ref_target(c, *dst, &via)) return false;
          return store(c, via, v);
        }
        return copy(c, dst, v, false);
      }
      case TKind::Ref: {
        Value* dst = t.ref;
        // Элемент буфера (Index на буфер) — байт.
        if (dst->type == Type::Integer && dst->node == reinterpret_cast<Node*>(1)) {
          uint64_t x;
          if (!to_int(c, v, &x)) return false;
          *dst->data = static_cast<uint8_t>(x);
          dst->integer = x & 0xff;
          return true;
        }
        return copy(c, dst, v, true);
      }
      case TKind::Node: {
        Object* o = t.node->object;
        if (o == nullptr) return fail(c, Error::BadType);
        if (o->type == Type::FieldUnit || o->type == Type::BufferField) return write_field(c, o, v);
        if (o->type == Type::Integer) {
          uint64_t x;
          if (!to_int(c, v, &x)) return false;
          o->value = integer(mask(c, x));
          return true;
        }
        if (o->type == Type::Buffer && v.type == Type::Buffer) {
          // В существующий буфер — с сохранением длины (§19.3.5).
          for (uint64_t i = 0; i < o->value.size; ++i) o->value.data[i] = i < v.size ? v.data[i] : 0;
          return true;
        }
        if (o->type == Type::Buffer && v.type == Type::Integer) {
          for (uint64_t i = 0; i < o->value.size; ++i) o->value.data[i] = i < 8 ? static_cast<uint8_t>(v.integer >> (8 * i)) : 0;
          return true;
        }
        Value tmp{};
        if (!copy(c, &tmp, v, true)) return false;
        o->value = tmp;
        o->type = tmp.type == Type::Reference ? Type::Integer : tmp.type;
        return true;
      }
    }
    return true;
  }

  static bool target(Ctx& c, const uint8_t* end, Target* t) { return super_name(c, end, t); }

  // --- вызов метода -------------------------------------------------------------------------

  static bool invoke(Ctx& c, const uint8_t* end, Node* n, Value* out) {
    Object* m = n->object;
    Frame f{};
    for (uint32_t i = 0; i < m->argc; ++i) {
      Value a{};
      if (!termarg(c, end, &a)) return false;
      if (!copy(c, &f.args[i], a, false)) return false;
    }
    return call(c, n, &f, out);
  }

  static bool call(Ctx& c, Node* n, Frame* f, Value* out) {
    Object* m = n->object;
    if (m->native_osi) {
      // \_OSI (§5.7.2): отвечаем «да» на строки Windows и на возможности ACPI.
      const Value& a = f->args[0];
      bool yes = false;
      if (a.type == Type::String) {
        StringView s(a.str, a.size);
        yes = s.starts_with("Windows") || s == StringView("Module Device") || s == StringView("Processor Device") ||
              s == StringView("3.0 Thermal Model") || s == StringView("Extended Address Space Descriptor");
      }
      *out = integer(yes ? ones(c) : 0);
      return true;
    }
    if (c.in->depth_ >= kMaxDepth) return fail(c, Error::TooDeep);
    ++c.in->depth_;
    Ctx sub{};
    sub.in = c.in;
    sub.p = m->code;
    sub.frame = f;
    sub.scope = n;
    f->ret = integer(0);
    bool ok = termlist(sub, m->code + m->code_len);
    --c.in->depth_;
    if (!ok) {
      c.err = sub.err;
      return false;
    }
    *out = f->ret;
    return true;
  }

  // --- данные --------------------------------------------------------------------------------

  static bool read_le(Ctx& c, const uint8_t* end, uint32_t n, uint64_t* out) {
    if (!need(c, end, n)) return false;
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; ++i) v |= uint64_t{c.p[i]} << (8 * i);
    c.p += n;
    *out = v;
    return true;
  }

  // Package / VarPackage (§20.2.5.4): элементы — данные или имена.
  static bool package(Ctx& c, const uint8_t* end, bool var_count, Value* out) {
    const uint8_t* pend;
    if (!pkglength(c, end, &pend)) return false;
    uint64_t count;
    if (var_count) {
      Value n{};
      if (!termarg(c, pend, &n) || !to_int(c, n, &count)) return false;
    } else {
      if (!need(c, pend, 1)) return false;
      count = *c.p++;
    }
    if (count > 4096) return fail(c, Error::OutOfRange);
    auto* items = static_cast<Value*>(c.in->scratch_.get(sizeof(Value) * (count != 0 ? count : 1)));
    if (items == nullptr) return fail(c, Error::NoMemory);
    uint64_t i = 0;
    while (c.p < pend) {
      Value v{};
      if (is_name_start(*c.p)) {
        // Имя в пакете — ссылка, не вызов (§19.6.101).
        const uint8_t* at = c.p;
        Path p;
        if (!name_string(c, pend, &p)) return false;
        v.type = Type::NameRef;
        v.node = resolve(c.in, c.scope, p);
        v.str = reinterpret_cast<const char*>(at);
        v.size = static_cast<uint64_t>(c.p - at);
      } else if (!termarg(c, pend, &v)) {
        return false;
      }
      if (i < count) items[i] = v;
      ++i;
    }
    c.p = pend;
    *out = {};
    out->type = Type::Package;
    out->items = items;
    out->size = count;
    return true;
  }

  static bool buffer(Ctx& c, const uint8_t* end, Value* out) {
    const uint8_t* bend;
    if (!pkglength(c, end, &bend)) return false;
    Value n{};
    uint64_t size;
    if (!termarg(c, bend, &n) || !to_int(c, n, &size)) return false;
    if (size > (1u << 20)) return fail(c, Error::OutOfRange);
    uint64_t init = static_cast<uint64_t>(bend - c.p);
    auto* d = static_cast<uint8_t*>(c.in->scratch_.get(size > init ? size : (init != 0 ? init : 1)));
    if (d == nullptr) return fail(c, Error::NoMemory);
    for (uint64_t i = 0; i < init && i < size; ++i) d[i] = c.p[i];
    c.p = bend;
    *out = {};
    out->type = Type::Buffer;
    out->data = d;
    out->size = size;
    return true;
  }

  // --- выражения ----------------------------------------------------------------------------

  static bool int_arg(Ctx& c, const uint8_t* end, uint64_t* out) {
    Value v{};
    return termarg(c, end, &v) && to_int(c, v, out);
  }

  static bool result(Ctx& c, const uint8_t* end, uint64_t value, Value* out) {
    *out = integer(mask(c, value));
    Target t;
    if (!target(c, end, &t)) return false;
    return store(c, t, *out);
  }

  static int compare(const Value& a, const Value& b) {
    // Строки и буферы — побайтно, затем по длине.
    const uint8_t* x = a.type == Type::String ? reinterpret_cast<const uint8_t*>(a.str) : a.data;
    const uint8_t* y = b.type == Type::String ? reinterpret_cast<const uint8_t*>(b.str) : b.data;
    uint64_t n = a.size < b.size ? a.size : b.size;
    for (uint64_t i = 0; i < n; ++i) {
      if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return a.size == b.size ? 0 : (a.size < b.size ? -1 : 1);
  }

  // Логические сравнения (§19.6.69–74): тип по первому операнду.
  static bool lcompare(Ctx& c, const uint8_t* end, int* cmp) {
    Value a{}, b{};
    if (!termarg(c, end, &a) || !termarg(c, end, &b)) return false;
    if (a.type == Type::String || a.type == Type::Buffer) {
      Value bb = b;
      if (b.type == Type::Integer) {
        // Integer к типу первого операнда — как буфер из 8 байт.
        auto* d = static_cast<uint8_t*>(c.in->scratch_.get(8));
        if (d == nullptr) return fail(c, Error::NoMemory);
        for (int i = 0; i < 8; ++i) d[i] = static_cast<uint8_t>(b.integer >> (8 * i));
        bb = {};
        bb.type = Type::Buffer;
        bb.data = d;
        bb.size = c.in->int_bits_ / 8;
      }
      *cmp = compare(a, bb);
      return true;
    }
    uint64_t x, y;
    if (!to_int(c, a, &x) || !to_int(c, b, &y)) return false;
    *cmp = x == y ? 0 : (x < y ? -1 : 1);
    return true;
  }

  static bool to_buffer(Ctx& c, const Value& v, Value* out) {
    if (v.type == Type::Buffer) {
      *out = v;
      return true;
    }
    uint64_t n = v.type == Type::String ? v.size + 1 : c.in->int_bits_ / 8;
    auto* d = static_cast<uint8_t*>(c.in->scratch_.get(n));
    if (d == nullptr) return fail(c, Error::NoMemory);
    if (v.type == Type::String) {
      for (uint64_t i = 0; i < v.size; ++i) d[i] = static_cast<uint8_t>(v.str[i]);
    } else {
      uint64_t x;
      if (!to_int(c, v, &x)) return false;
      for (uint64_t i = 0; i < n; ++i) d[i] = static_cast<uint8_t>(x >> (8 * i));
    }
    *out = {};
    out->type = Type::Buffer;
    out->data = d;
    out->size = n;
    return true;
  }

  static bool make_string(Ctx& c, const char* s, uint64_t n, Value* out) {
    auto* d = static_cast<char*>(c.in->scratch_.get(n + 1));
    if (d == nullptr) return fail(c, Error::NoMemory);
    for (uint64_t i = 0; i < n; ++i) d[i] = s[i];
    *out = {};
    out->type = Type::String;
    out->str = d;
    out->size = n;
    return true;
  }

  static bool to_hex_string(Ctx& c, const Value& v, Value* out) {
    char buf[512];
    uint64_t n = 0;
    auto hex = [&](uint64_t x, int digits) {
      buf[n++] = '0';
      buf[n++] = 'x';
      for (int s = (digits - 1) * 4; s >= 0; s -= 4) buf[n++] = "0123456789ABCDEF"[(x >> s) & 15];
    };
    if (v.type == Type::String) return make_string(c, v.str, v.size, out);
    if (v.type == Type::Buffer) {
      for (uint64_t i = 0; i < v.size && n + 5 < sizeof buf; ++i) {
        if (i != 0) buf[n++] = ',';
        hex(v.data[i], 2);
      }
      return make_string(c, buf, n, out);
    }
    uint64_t x;
    if (!to_int(c, v, &x)) return false;
    hex(x, c.in->int_bits_ / 4);
    return make_string(c, buf, n, out);
  }

  static bool to_decimal_string(Ctx& c, const Value& v, Value* out) {
    if (v.type == Type::String) return make_string(c, v.str, v.size, out);
    uint64_t x;
    if (!to_int(c, v, &x)) return false;
    char rev[24], buf[24];
    int n = 0;
    do {
      rev[n++] = static_cast<char>('0' + x % 10);
      x /= 10;
    } while (x != 0);
    for (int i = 0; i < n; ++i) buf[i] = rev[n - 1 - i];
    return make_string(c, buf, static_cast<uint64_t>(n), out);
  }

  static bool concat(Ctx& c, const Value& a, const Value& b, Value* out) {
    if (a.type == Type::String) {
      Value bs{};
      if (b.type == Type::String) {
        bs = b;
      } else if (!to_hex_string(c, b, &bs)) {
        return false;
      }
      auto* d = static_cast<char*>(c.in->scratch_.get(a.size + bs.size + 1));
      if (d == nullptr) return fail(c, Error::NoMemory);
      for (uint64_t i = 0; i < a.size; ++i) d[i] = a.str[i];
      for (uint64_t i = 0; i < bs.size; ++i) d[a.size + i] = bs.str[i];
      *out = {};
      out->type = Type::String;
      out->str = d;
      out->size = a.size + bs.size;
      return true;
    }
    Value ab{}, bb{};
    if (!to_buffer(c, a, &ab) || !to_buffer(c, b, &bb)) return false;
    auto* d = static_cast<uint8_t*>(c.in->scratch_.get(ab.size + bb.size + 1));
    if (d == nullptr) return fail(c, Error::NoMemory);
    for (uint64_t i = 0; i < ab.size; ++i) d[i] = ab.data[i];
    for (uint64_t i = 0; i < bb.size; ++i) d[ab.size + i] = bb.data[i];
    *out = {};
    out->type = Type::Buffer;
    out->data = d;
    out->size = ab.size + bb.size;
    return true;
  }

  static uint32_t type_code(const Value& v) {
    switch (v.type) {
      case Type::Reference: return v.node != nullptr && v.node->object != nullptr ? static_cast<uint32_t>(v.node->object->type) : 0;
      case Type::NameRef:
      case Type::Scope: return 0;
      default: return static_cast<uint32_t>(v.type);
    }
  }

  // Index (§19.6.63): ссылка на элемент пакета или байт буфера/строки.
  static bool index(Ctx& c, const uint8_t* end, Value* out) {
    // Источник берётся как ссылка: Index(PKG, 1) должен менять сам PKG.
    Value src{};
    Value* src_ptr = nullptr;
    if (c.p < end && is_name_start(*c.p)) {
      const uint8_t* save = c.p;
      Path p;
      if (!name_string(c, end, &p)) return false;
      Node* n = resolve(c.in, c.scope, p);
      if (n != nullptr && n->object != nullptr && n->object->type != Type::Method &&
          n->object->type != Type::FieldUnit && n->object->type != Type::BufferField) {
        src_ptr = &n->object->value;
        src = *src_ptr;
      } else {
        c.p = save;
        if (!termarg(c, end, &src)) return false;
      }
    } else if (c.p < end && *c.p >= 0x60 && *c.p <= 0x6e && c.frame != nullptr) {
      uint8_t op = *c.p++;
      src_ptr = op <= 0x67 ? &c.frame->locals[op - 0x60] : &c.frame->args[op - 0x68];
      if (src_ptr->type == Type::Reference) {
        if (src_ptr->target != nullptr) {
          src_ptr = src_ptr->target;
        } else if (src_ptr->node->object != nullptr) {
          src_ptr = &src_ptr->node->object->value;
        }
      }
      src = *src_ptr;
    } else if (!termarg(c, end, &src)) {
      return false;
    }
    uint64_t i;
    if (!int_arg(c, end, &i)) return false;
    *out = {};
    out->type = Type::Reference;
    if (src.type == Type::Package) {
      if (i >= src.size) return fail(c, Error::OutOfRange);
      out->target = &src.items[i];
    } else if (src.type == Type::Buffer || src.type == Type::String) {
      if (i >= src.size) return fail(c, Error::OutOfRange);
      // Элемент буфера — временное Integer со ссылкой на байт (метка node = 1).
      auto* e = static_cast<Value*>(c.in->scratch_.get(sizeof(Value)));
      if (e == nullptr) return fail(c, Error::NoMemory);
      uint8_t* bytes = src.type == Type::Buffer ? src.data : reinterpret_cast<uint8_t*>(const_cast<char*>(src.str));
      *e = integer(bytes[i]);
      e->data = bytes + i;
      e->node = reinterpret_cast<Node*>(1);
      out->target = e;
    } else {
      return fail(c, Error::BadType);
    }
    (void)src_ptr;
    Target t;
    if (!target(c, end, &t)) return false;
    return store(c, t, *out);
  }

  static bool match_one(Ctx& c, uint8_t op, const Value& e, uint64_t x, bool* ok) {
    if (op == 0) {
      *ok = true;
      return true;
    }
    uint64_t v;
    if (e.type != Type::Integer && e.type != Type::String && e.type != Type::Buffer) {
      *ok = false;
      return true;
    }
    if (!to_int(c, e, &v)) return false;
    switch (op) {
      case 1: *ok = v == x; break;
      case 2: *ok = v <= x; break;
      case 3: *ok = v < x; break;
      case 4: *ok = v >= x; break;
      case 5: *ok = v > x; break;
      default: *ok = false; break;
    }
    return true;
  }

  static bool create_field_op(Ctx& c, const uint8_t* end, uint64_t bits, bool bit_index, bool explicit_bits) {
    // Источник — буфер в пространстве имён (поле должно менять его самого).
    Value* buf = nullptr;
    if (c.p < end && is_name_start(*c.p)) {
      Path p;
      if (!name_string(c, end, &p)) return false;
      Node* n = resolve(c.in, c.scope, p);
      if (n == nullptr || n->object == nullptr || n->object->type != Type::Buffer) return fail(c, Error::BadType);
      buf = &n->object->value;
    } else if (c.p < end && *c.p >= 0x60 && *c.p <= 0x6e && c.frame != nullptr) {
      uint8_t op = *c.p++;
      buf = op <= 0x67 ? &c.frame->locals[op - 0x60] : &c.frame->args[op - 0x68];
      if (buf->type == Type::Reference && buf->node != nullptr && buf->node->object != nullptr) buf = &buf->node->object->value;
      if (buf->type != Type::Buffer) return fail(c, Error::BadType);
    } else {
      Value v{};
      if (!termarg(c, end, &v) || v.type != Type::Buffer) return fail(c, Error::BadType);
      buf = static_cast<Value*>(c.in->scratch_.get(sizeof(Value)));
      if (buf == nullptr) return fail(c, Error::NoMemory);
      *buf = v;
    }
    uint64_t idx;
    if (!int_arg(c, end, &idx)) return false;
    if (explicit_bits && !int_arg(c, end, &bits)) return false;
    Path p;
    if (!name_string(c, end, &p)) return false;
    Node* n = create(c, p);
    if (n == nullptr) return false;
    Object* o = new_object(c, Type::BufferField);
    if (o == nullptr) return false;
    o->buffer = buf;
    o->bit_offset = bit_index ? idx : idx * 8;
    o->bit_length = bits;
    n->object = o;
    return true;
  }

  // TermArg: данные, переменные, выражения, имена (§20.2.5).
  static bool termarg(Ctx& c, const uint8_t* end, Value* out) {
    if (!need(c, end, 1)) return false;
    uint8_t op = *c.p;
    if (is_name_start(op)) {
      Path p;
      if (!name_string(c, end, &p)) return false;
      Node* n = resolve(c.in, c.scope, p);
      if (n == nullptr) return fail(c, Error::NotFound);
      if (n->object != nullptr && n->object->type == Type::Method) return invoke(c, end, n, out);
      return read_object(c, n, out);
    }
    ++c.p;
    switch (op) {
      case 0x00: *out = integer(0); return true;
      case 0x01: *out = integer(1); return true;
      case 0xff: *out = integer(ones(c)); return true;
      case 0x0a: { uint64_t v; if (!read_le(c, end, 1, &v)) return false; *out = integer(v); return true; }
      case 0x0b: { uint64_t v; if (!read_le(c, end, 2, &v)) return false; *out = integer(v); return true; }
      case 0x0c: { uint64_t v; if (!read_le(c, end, 4, &v)) return false; *out = integer(v); return true; }
      case 0x0e: { uint64_t v; if (!read_le(c, end, 8, &v)) return false; *out = integer(mask(c, v)); return true; }
      case 0x0d: {
        const uint8_t* s = c.p;
        while (c.p < end && *c.p != 0) ++c.p;
        if (c.p >= end) return fail(c, Error::Truncated);
        *out = {};
        out->type = Type::String;
        out->str = reinterpret_cast<const char*>(s);
        out->size = static_cast<uint64_t>(c.p - s);
        ++c.p;
        return true;
      }
      case 0x11: return buffer(c, end, out);
      case 0x12: return package(c, end, false, out);
      case 0x13: return package(c, end, true, out);
      case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
      case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: {
        if (c.frame == nullptr) return fail(c, Error::BadOpcode);
        Target t{op <= 0x67 ? TKind::Local : TKind::Arg, op <= 0x67 ? op - 0x60u : op - 0x68u, nullptr, nullptr};
        return read_target(c, t, out);
      }
      case 0x70: {  // Store
        Value v{};
        if (!termarg(c, end, &v)) return false;
        Target t;
        if (!super_name(c, end, &t)) return false;
        *out = v;
        return store(c, t, v);
      }
      case 0x71: {  // RefOf
        Target t;
        if (!super_name(c, end, &t)) return false;
        *out = {};
        out->type = Type::Reference;
        if (t.kind == TKind::Node) {
          out->node = t.node;
        } else if (t.kind == TKind::Ref) {
          out->target = t.ref;
        } else if (t.kind == TKind::Local || t.kind == TKind::Arg) {
          out->target = var(c, t);
        }
        return true;
      }
      case 0x72: case 0x74: case 0x77: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
      case 0x85: {
        uint64_t a, b;
        if (!int_arg(c, end, &a) || !int_arg(c, end, &b)) return false;
        uint64_t r = 0;
        switch (op) {
          case 0x72: r = a + b; break;
          case 0x74: r = a - b; break;
          case 0x77: r = a * b; break;
          case 0x79: r = b >= 64 ? 0 : a << b; break;
          case 0x7a: r = b >= 64 ? 0 : a >> b; break;
          case 0x7b: r = a & b; break;
          case 0x7c: r = ~(a & b); break;
          case 0x7d: r = a | b; break;
          case 0x7e: r = ~(a | b); break;
          case 0x7f: r = a ^ b; break;
          case 0x85:
            if (b == 0) return fail(c, Error::DivideByZero);
            r = a % b;
            break;
        }
        return result(c, end, r, out);
      }
      case 0x73: {  // Concatenate
        Value a{}, b{};
        if (!termarg(c, end, &a) || !termarg(c, end, &b)) return false;
        if (!concat(c, a, b, out)) return false;
        Target t;
        return target(c, end, &t) && store(c, t, *out);
      }
      case 0x84: {  // ConcatenateResTemplate: без конечных EndTag (0x79 xx)
        Value a{}, b{};
        if (!termarg(c, end, &a) || !termarg(c, end, &b)) return false;
        Value ab{}, bb{};
        if (!to_buffer(c, a, &ab) || !to_buffer(c, b, &bb)) return false;
        uint64_t la = ab.size >= 2 && ab.data[ab.size - 2] == 0x79 ? ab.size - 2 : ab.size;
        uint64_t lb = bb.size >= 2 && bb.data[bb.size - 2] == 0x79 ? bb.size - 2 : bb.size;
        auto* d = static_cast<uint8_t*>(c.in->scratch_.get(la + lb + 2));
        if (d == nullptr) return fail(c, Error::NoMemory);
        for (uint64_t i = 0; i < la; ++i) d[i] = ab.data[i];
        for (uint64_t i = 0; i < lb; ++i) d[la + i] = bb.data[i];
        d[la + lb] = 0x79;
        d[la + lb + 1] = 0;
        *out = {};
        out->type = Type::Buffer;
        out->data = d;
        out->size = la + lb + 2;
        Target t;
        return target(c, end, &t) && store(c, t, *out);
      }
      case 0x75: case 0x76: {  // Increment / Decrement
        Target t;
        if (!super_name(c, end, &t)) return false;
        Value v{};
        uint64_t x;
        if (!read_target(c, t, &v) || !to_int(c, v, &x)) return false;
        *out = integer(mask(c, op == 0x75 ? x + 1 : x - 1));
        return store(c, t, *out);
      }
      case 0x78: {  // Divide
        uint64_t a, b;
        if (!int_arg(c, end, &a) || !int_arg(c, end, &b)) return false;
        if (b == 0) return fail(c, Error::DivideByZero);
        Target rem, quo;
        if (!target(c, end, &rem) || !target(c, end, &quo)) return false;
        if (!store(c, rem, integer(a % b))) return false;
        *out = integer(a / b);
        return store(c, quo, *out);
      }
      case 0x80: {  // Not
        uint64_t a;
        if (!int_arg(c, end, &a)) return false;
        return result(c, end, ~a, out);
      }
      case 0x81: case 0x82: {  // FindSetLeftBit / FindSetRightBit
        uint64_t a;
        if (!int_arg(c, end, &a)) return false;
        a = mask(c, a);
        uint64_t r = 0;
        if (a != 0) r = op == 0x81 ? 64 - static_cast<uint64_t>(__builtin_clzll(a)) : static_cast<uint64_t>(__builtin_ctzll(a)) + 1;
        return result(c, end, r, out);
      }
      case 0x83: {  // DerefOf
        Value r{};
        if (!termarg(c, end, &r)) return false;
        if (r.type == Type::String) {
          // DerefOf("\\NAME") — объект по имени-строке.
          Ctx sub = c;
          Node* n = c.in->find(StringView(r.str, r.size), c.scope);
          if (n == nullptr) return fail(c, Error::NotFound);
          (void)sub;
          return read_object(c, n, out);
        }
        if (r.type == Type::NameRef) {
          if (r.node == nullptr) return fail(c, Error::NotFound);
          return read_object(c, r.node, out);
        }
        return deref(c, r, out);
      }
      case 0x86: {  // Notify — уведомления ОС не обрабатываются
        Target t;
        uint64_t v;
        return super_name(c, end, &t) && int_arg(c, end, &v);
      }
      case 0x87: {  // SizeOf
        Target t;
        if (!super_name(c, end, &t)) return false;
        Value v{};
        if (!read_target(c, t, &v)) return false;
        if (v.type == Type::Reference && !deref(c, v, &v)) return false;
        if (v.type != Type::String && v.type != Type::Buffer && v.type != Type::Package) return fail(c, Error::BadType);
        *out = integer(v.size);
        return true;
      }
      case 0x88: return index(c, end, out);
      case 0x89: {  // Match
        Value pkg{};
        if (!termarg(c, end, &pkg) || pkg.type != Type::Package) return fail(c, Error::BadType);
        if (!need(c, end, 1)) return false;
        uint8_t op1 = *c.p++;
        uint64_t x1;
        if (!int_arg(c, end, &x1) || !need(c, end, 1)) return false;
        uint8_t op2 = *c.p++;
        uint64_t x2, start;
        if (!int_arg(c, end, &x2) || !int_arg(c, end, &start)) return false;
        *out = integer(ones(c));
        for (uint64_t i = start; i < pkg.size; ++i) {
          bool a, b;
          if (!match_one(c, op1, pkg.items[i], x1, &a) || !match_one(c, op2, pkg.items[i], x2, &b)) return false;
          if (a && b) {
            *out = integer(i);
            break;
          }
        }
        return true;
      }
      case 0x8a: return create_field_op(c, end, 32, false, false);
      case 0x8b: return create_field_op(c, end, 16, false, false);
      case 0x8c: return create_field_op(c, end, 8, false, false);
      case 0x8d: return create_field_op(c, end, 1, true, false);
      case 0x8f: return create_field_op(c, end, 64, false, false);
      case 0x8e: {  // ObjectType
        Target t;
        if (!super_name(c, end, &t)) return false;
        if (t.kind == TKind::Node && t.node->object != nullptr) {
          *out = integer(static_cast<uint32_t>(t.node->object->type));
          return true;
        }
        Value v{};
        if (!read_target(c, t, &v)) return false;
        *out = integer(type_code(v));
        return true;
      }
      case 0x90: case 0x91: {
        uint64_t a, b;
        if (!int_arg(c, end, &a) || !int_arg(c, end, &b)) return false;
        bool r = op == 0x90 ? (a != 0 && b != 0) : (a != 0 || b != 0);
        *out = integer(r ? ones(c) : 0);
        return true;
      }
      case 0x92: {  // LNot или LNotEqual/LLessEqual/LGreaterEqual
        if (c.p < end && (*c.p == 0x93 || *c.p == 0x94 || *c.p == 0x95)) {
          uint8_t sub = *c.p++;
          int cmp;
          if (!lcompare(c, end, &cmp)) return false;
          bool r = sub == 0x93 ? cmp != 0 : sub == 0x94 ? cmp <= 0 : cmp >= 0;
          *out = integer(r ? ones(c) : 0);
          return true;
        }
        uint64_t a;
        if (!int_arg(c, end, &a)) return false;
        *out = integer(a == 0 ? ones(c) : 0);
        return true;
      }
      case 0x93: case 0x94: case 0x95: {
        int cmp;
        if (!lcompare(c, end, &cmp)) return false;
        bool r = op == 0x93 ? cmp == 0 : op == 0x94 ? cmp > 0 : cmp < 0;
        *out = integer(r ? ones(c) : 0);
        return true;
      }
      case 0x96: {  // ToBuffer
        Value v{};
        if (!termarg(c, end, &v) || !to_buffer(c, v, out)) return false;
        Target t;
        return target(c, end, &t) && store(c, t, *out);
      }
      case 0x97: case 0x98: {  // ToDecimalString / ToHexString
        Value v{};
        if (!termarg(c, end, &v)) return false;
        if (!(op == 0x97 ? to_decimal_string(c, v, out) : to_hex_string(c, v, out))) return false;
        Target t;
        return target(c, end, &t) && store(c, t, *out);
      }
      case 0x99: {  // ToInteger
        Value v{};
        uint64_t x;
        if (!termarg(c, end, &v) || !to_int(c, v, &x)) return false;
        return result(c, end, x, out);
      }
      case 0x9c: {  // ToString(buffer, length)
        Value v{};
        uint64_t len;
        if (!termarg(c, end, &v) || !int_arg(c, end, &len)) return false;
        Value b{};
        if (!to_buffer(c, v, &b)) return false;
        uint64_t n = 0;
        while (n < b.size && n < len && b.data[n] != 0) ++n;
        if (!make_string(c, reinterpret_cast<const char*>(b.data), n, out)) return false;
        Target t;
        return target(c, end, &t) && store(c, t, *out);
      }
      case 0x9d: {  // CopyObject
        Value v{};
        if (!termarg(c, end, &v)) return false;
        Target t;
        if (!super_name(c, end, &t)) return false;
        *out = v;
        if (t.kind == TKind::Node && t.node->object != nullptr) {
          Value tmp{};
          if (!copy(c, &tmp, v, true)) return false;
          t.node->object->type = tmp.type;
          t.node->object->value = tmp;
          return true;
        }
        return store(c, t, v);
      }
      case 0x9e: {  // Mid
        Value v{};
        uint64_t idx, len;
        if (!termarg(c, end, &v) || !int_arg(c, end, &idx) || !int_arg(c, end, &len)) return false;
        if (v.type != Type::String && v.type != Type::Buffer) return fail(c, Error::BadType);
        uint64_t from = idx < v.size ? idx : v.size;
        uint64_t n = v.size - from < len ? v.size - from : len;
        if (v.type == Type::String) {
          if (!make_string(c, v.str + from, n, out)) return false;
        } else {
          auto* d = static_cast<uint8_t*>(c.in->scratch_.get(n != 0 ? n : 1));
          if (d == nullptr) return fail(c, Error::NoMemory);
          for (uint64_t i = 0; i < n; ++i) d[i] = v.data[from + i];
          *out = {};
          out->type = Type::Buffer;
          out->data = d;
          out->size = n;
        }
        Target t;
        return target(c, end, &t) && store(c, t, *out);
      }
      case 0x5b: return ext_expression(c, end, out);
      default:
        return fail(c, Error::BadOpcode);
    }
  }

  static bool ext_expression(Ctx& c, const uint8_t* end, Value* out) {
    if (!need(c, end, 1)) return false;
    uint8_t op = *c.p++;
    switch (op) {
      case 0x12: {  // CondRefOf
        Target t;
        if (!super_name(c, end, &t, true)) return false;
        bool exists = t.kind != TKind::None;
        Target dst;
        if (!target(c, end, &dst)) return false;
        if (exists) {
          Value r{};
          r.type = Type::Reference;
          r.node = t.kind == TKind::Node ? t.node : nullptr;
          r.target = t.kind == TKind::Ref ? t.ref : nullptr;
          if (!store(c, dst, r)) return false;
        }
        *out = integer(exists ? ones(c) : 0);
        return true;
      }
      case 0x13: return create_field_op(c, end, 0, true, true);  // CreateField
      case 0x23: {  // Acquire: мьютексы не нужны — исполнение однопоточное
        Target t;
        if (!super_name(c, end, &t) || !need(c, end, 2)) return false;
        c.p += 2;
        *out = integer(0);
        return true;
      }
      case 0x25: {  // Wait
        Target t;
        uint64_t v;
        if (!super_name(c, end, &t) || !int_arg(c, end, &v)) return false;
        *out = integer(0);
        return true;
      }
      case 0x28: case 0x29: {  // FromBCD / ToBCD
        uint64_t a;
        if (!int_arg(c, end, &a)) return false;
        uint64_t r = 0, m = 1;
        if (op == 0x28) {
          for (; a != 0; a >>= 4, m *= 10) r += (a & 15) * m;
        } else {
          for (int s = 0; a != 0; a /= 10, s += 4) r |= (a % 10) << s;
        }
        return result(c, end, r, out);
      }
      case 0x30: *out = integer(2); return true;  // Revision: ревизия интерпретатора
      case 0x31: *out = integer(0); return true;  // Debug как значение
      case 0x33: *out = integer(0); return true;  // Timer — счётчика 100 нс нет
      case 0x1f: case 0x20: return fail(c, Error::Unsupported);  // LoadTable / Load
      default: return fail(c, Error::BadOpcode);
    }
  }

  // --- термы ---------------------------------------------------------------------------------

  static bool field_list(Ctx& c, const uint8_t* end, Object proto) {
    uint64_t bit = 0;
    while (c.p < end) {
      uint8_t b = *c.p;
      if (b == 0x00) {  // ReservedField
        ++c.p;
        const uint8_t* save = c.p;
        uint64_t len = 0;
        // PkgLength здесь — число бит, а не длина в байтах.
        uint8_t lead = *c.p++;
        uint32_t follow = lead >> 6;
        len = follow == 0 ? (lead & 0x3f) : (lead & 0x0f);
        if (!need(c, end, follow)) return false;
        for (uint32_t i = 0; i < follow; ++i) len |= uint64_t{*c.p++} << (4 + 8 * i);
        (void)save;
        bit += len;
      } else if (b == 0x01) {  // AccessField
        if (!need(c, end, 3)) return false;
        proto.access = c.p[1] & 0x0f;
        c.p += 3;
      } else if (b == 0x03) {  // ExtendedAccessField
        if (!need(c, end, 4)) return false;
        proto.access = c.p[1] & 0x0f;
        c.p += 4;
      } else if (b == 0x02) {  // ConnectField
        ++c.p;
        if (c.p < end && *c.p == 0x11) {
          ++c.p;
          const uint8_t* bend;
          if (!pkglength(c, end, &bend)) return false;
          c.p = bend;
        } else {
          Path p;
          if (!name_string(c, end, &p)) return false;
        }
      } else if (lead_char(b)) {
        if (!need(c, end, 4)) return false;
        const uint8_t* seg = c.p;
        c.p += 4;
        uint8_t lead = *c.p++;
        uint32_t follow = lead >> 6;
        uint64_t len = follow == 0 ? (lead & 0x3f) : (lead & 0x0f);
        if (!need(c, end, follow)) return false;
        for (uint32_t i = 0; i < follow; ++i) len |= uint64_t{*c.p++} << (4 + 8 * i);
        Node* n = child(c.scope, seg);
        if (n == nullptr) n = new_node(c.in, c.scope, seg);
        if (n == nullptr) return fail(c, Error::NoMemory);
        Object* o = new_object(c, Type::FieldUnit);
        if (o == nullptr) return false;
        *o = proto;
        o->type = Type::FieldUnit;
        o->bit_offset = bit;
        o->bit_length = len;
        n->object = o;
        bit += len;
      } else {
        return fail(c, Error::BadOpcode);
      }
    }
    return true;
  }

  // Поле создаётся в той области, где стоит Field (§19.6.48), а не у региона.
  static bool field_op(Ctx& c, const uint8_t* end, FieldKind kind) {
    const uint8_t* fend;
    if (!pkglength(c, end, &fend)) return false;
    Object proto{};
    proto.kind = kind;
    Path p1;
    if (!name_string(c, fend, &p1)) return false;
    Node* n1 = resolve(c.in, c.scope, p1);
    if (n1 == nullptr) return fail(c, Error::NotFound);
    if (kind == FieldKind::Normal) {
      proto.region = n1;
    } else if (kind == FieldKind::Index) {
      Path p2;
      if (!name_string(c, fend, &p2)) return false;
      Node* n2 = resolve(c.in, c.scope, p2);
      if (n2 == nullptr) return fail(c, Error::NotFound);
      proto.index_reg = n1;
      proto.data_reg = n2;
    } else {
      Path p2;
      if (!name_string(c, fend, &p2)) return false;
      Node* bank = resolve(c.in, c.scope, p2);
      if (bank == nullptr) return fail(c, Error::NotFound);
      proto.region = n1;
      proto.bank_reg = bank;
      if (!int_arg(c, fend, &proto.bank_value)) return false;
    }
    if (!need(c, fend, 1)) return false;
    uint8_t flags = *c.p++;
    proto.access = flags & 0x0f;
    proto.update = (flags >> 5) & 3;
    bool ok = field_list(c, fend, proto);
    c.p = fend;
    return ok;
  }

  static bool scope_like(Ctx& c, const uint8_t* end, Type type, uint32_t skip_bytes) {
    const uint8_t* send;
    if (!pkglength(c, end, &send)) return false;
    Path p;
    if (!name_string(c, send, &p)) return false;
    Node* n = type == Type::Scope ? resolve(c.in, c.scope, p) : create(c, p);
    if (n == nullptr) {
      // Scope к несуществующему пути — создать (как делают реализации).
      c.err = Error::Ok;
      n = create(c, p);
      if (n == nullptr) return false;
    }
    if (type != Type::Scope && n->object == nullptr) {
      Object* o = new_object(c, type);
      if (o == nullptr) return false;
      n->object = o;
    }
    if (!need(c, send, skip_bytes)) return false;
    c.p += skip_bytes;
    Node* saved = c.scope;
    c.scope = n;
    bool ok = termlist(c, send);
    c.scope = saved;
    c.p = send;
    return ok;
  }

  static bool term(Ctx& c, const uint8_t* end) {
    uint8_t op = *c.p;
    switch (op) {
      case 0x06: {  // Alias
        ++c.p;
        Path src, dst;
        if (!name_string(c, end, &src) || !name_string(c, end, &dst)) return false;
        Node* s = resolve(c.in, c.scope, src);
        Node* d = create(c, dst);
        if (d == nullptr) return false;
        if (s != nullptr) d->object = s->object;
        return true;
      }
      case 0x08: {  // Name
        ++c.p;
        Path p;
        if (!name_string(c, end, &p)) return false;
        Node* n = create(c, p);
        if (n == nullptr) return false;
        Value v{};
        if (!termarg(c, end, &v)) return false;
        Object* o = new_object(c, v.type == Type::Package || v.type == Type::String || v.type == Type::Buffer ? v.type
                                                                                                             : Type::Integer);
        if (o == nullptr) return false;
        if (!copy(c, &o->value, v, true)) return false;
        if (v.type == Type::Integer) o->value = integer(mask(c, v.integer));
        n->object = o;
        return true;
      }
      case 0x10: ++c.p; return scope_like(c, end, Type::Scope, 0);
      case 0x14: {  // Method
        ++c.p;
        const uint8_t* mend;
        if (!pkglength(c, end, &mend)) return false;
        Path p;
        if (!name_string(c, mend, &p)) return false;
        Node* n = create(c, p);
        if (n == nullptr) return false;
        if (!need(c, mend, 1)) return false;
        uint8_t flags = *c.p++;
        Object* o = new_object(c, Type::Method);
        if (o == nullptr) return false;
        o->argc = flags & 7;
        o->code = c.p;
        o->code_len = static_cast<size_t>(mend - c.p);
        n->object = o;
        ++c.in->methods_;
        c.p = mend;
        return true;
      }
      case 0x15: {  // External: объявление для компилятора — пропускаем
        ++c.p;
        Path p;
        if (!name_string(c, end, &p) || !need(c, end, 2)) return false;
        c.p += 2;
        return true;
      }
      case 0x86: case 0x8a: case 0x8b: case 0x8c: case 0x8d: case 0x8f: {
        Value v{};
        return termarg(c, end, &v);
      }
      case 0x9f: ++c.p; c.flow = Flow::Continue; return true;
      case 0xa5: ++c.p; c.flow = Flow::Break; return true;
      case 0xa3: case 0xcc: ++c.p; return true;  // Noop, BreakPoint
      case 0xa4: {  // Return
        ++c.p;
        Value v{};
        if (c.p < end && *c.p != 0x00 && !termarg(c, end, &v)) return false;
        if (c.p < end && *c.p == 0x00 && v.type == Type::Uninitialized) {
          ++c.p;
          v = integer(0);
        }
        if (c.frame != nullptr) {
          Value r{};
          if (!copy(c, &r, v, false)) return false;
          c.frame->ret = r;
        }
        c.flow = Flow::Return;
        return true;
      }
      case 0xa0: {  // If [Else]
        ++c.p;
        const uint8_t* iend;
        if (!pkglength(c, end, &iend)) return false;
        uint64_t pred;
        if (!int_arg(c, iend, &pred)) return false;
        bool ok = true;
        if (pred != 0) ok = termlist(c, iend);
        c.p = iend;
        if (!ok) return false;
        if (c.p < end && *c.p == 0xa1) {
          ++c.p;
          const uint8_t* eend;
          if (!pkglength(c, end, &eend)) return false;
          if (pred == 0 && c.flow == Flow::Normal) ok = termlist(c, eend);
          c.p = eend;
        }
        return ok;
      }
      case 0xa1: {  // Else без If — пропустить
        ++c.p;
        const uint8_t* eend;
        if (!pkglength(c, end, &eend)) return false;
        c.p = eend;
        return true;
      }
      case 0xa2: {  // While
        ++c.p;
        const uint8_t* wend;
        if (!pkglength(c, end, &wend)) return false;
        const uint8_t* cond = c.p;
        for (uint32_t loops = 0;; ++loops) {
          if (loops > kMaxLoops) return fail(c, Error::TooDeep);
          c.p = cond;
          uint64_t pred;
          if (!int_arg(c, wend, &pred)) return false;
          if (pred == 0) break;
          if (!termlist(c, wend)) return false;
          if (c.flow == Flow::Break) {
            c.flow = Flow::Normal;
            break;
          }
          if (c.flow == Flow::Continue) c.flow = Flow::Normal;
          if (c.flow == Flow::Return) break;
        }
        c.p = wend;
        return true;
      }
      case 0x5b: {
        if (!need(c, end, 2)) return false;
        uint8_t ext = c.p[1];
        switch (ext) {
          case 0x01: case 0x02: {  // Mutex, Event
            c.p += 2;
            Path p;
            if (!name_string(c, end, &p)) return false;
            Node* n = create(c, p);
            if (n == nullptr) return false;
            if (ext == 0x01) {
              if (!need(c, end, 1)) return false;
              ++c.p;
            }
            Object* o = new_object(c, ext == 0x01 ? Type::Mutex : Type::Event);
            if (o == nullptr) return false;
            n->object = o;
            return true;
          }
          case 0x80: {  // OperationRegion
            c.p += 2;
            Path p;
            if (!name_string(c, end, &p) || !need(c, end, 1)) return false;
            uint8_t space = *c.p++;
            Node* n = create(c, p);
            if (n == nullptr) return false;
            uint64_t off, len;
            if (!int_arg(c, end, &off) || !int_arg(c, end, &len)) return false;
            Object* o = new_object(c, Type::OpRegion);
            if (o == nullptr) return false;
            o->space = static_cast<Space>(space);
            o->offset = off;
            o->length = len;
            n->object = o;
            return true;
          }
          case 0x81: c.p += 2; return field_op(c, end, FieldKind::Normal);
          case 0x86: c.p += 2; return field_op(c, end, FieldKind::Index);
          case 0x87: c.p += 2; return field_op(c, end, FieldKind::Bank);
          case 0x82: c.p += 2; return scope_like(c, end, Type::Device, 0);
          case 0x83: c.p += 2; return scope_like(c, end, Type::Processor, 6);  // устаревший Processor (ProcID, PblkAddr, PblkLen)
          case 0x84: c.p += 2; return scope_like(c, end, Type::PowerResource, 3);
          case 0x85: c.p += 2; return scope_like(c, end, Type::ThermalZone, 0);
          case 0x88: {  // DataRegion: таблица по сигнатуре — не поддержана, объект пустой
            c.p += 2;
            Path p;
            if (!name_string(c, end, &p)) return false;
            Node* n = create(c, p);
            if (n == nullptr) return false;
            Value a{}, b{}, d{};
            if (!termarg(c, end, &a) || !termarg(c, end, &b) || !termarg(c, end, &d)) return false;
            Object* o = new_object(c, Type::OpRegion);
            if (o == nullptr) return false;
            o->space = static_cast<Space>(0xff);
            n->object = o;
            return true;
          }
          case 0x21: case 0x22: {  // Stall / Sleep
            c.p += 2;
            uint64_t t;
            if (!int_arg(c, end, &t)) return false;
            if (ext == 0x21 && c.in->host_.stall_us != nullptr) c.in->host_.stall_us(c.in->host_.ctx, t);
            if (ext == 0x22 && c.in->host_.sleep_ms != nullptr) c.in->host_.sleep_ms(c.in->host_.ctx, t);
            return true;
          }
          case 0x24: case 0x26: case 0x27: {  // Signal, Reset, Release
            c.p += 2;
            Target t;
            return super_name(c, end, &t);
          }
          case 0x32: {  // Fatal
            c.p += 2;
            if (!need(c, end, 5)) return false;
            c.p += 5;
            uint64_t a;
            if (!int_arg(c, end, &a)) return false;
            return fail(c, Error::Fatal);
          }
          default: {
            Value v{};
            return termarg(c, end, &v);
          }
        }
      }
      default: {
        // Выражение или вызов метода как оператор — значение отбрасывается.
        Value v{};
        return termarg(c, end, &v);
      }
    }
  }

  static bool termlist(Ctx& c, const uint8_t* end) {
    while (c.p < end) {
      if (!term(c, end)) return false;
      if (c.flow != Flow::Normal) return true;
    }
    return true;
  }
};

// --- Interpreter -----------------------------------------------------------------------------

void Interpreter::init(Allocator permanent, Allocator scratch, Host host) {
  perm_ = {};
  scratch_ = {};
  perm_.src = permanent;
  scratch_.src = scratch;
  host_ = host;
  nodes_ = methods_ = depth_ = 0;
  int_bits_ = 64;
  static const uint8_t kRoot[4] = {'\\', '_', '_', '_'};
  root_ = Impl::new_node(this, nullptr, kRoot);
  if (root_ == nullptr) return;
  // §5.3.1: предопределённые области и объекты.
  Ctx c{};
  c.in = this;
  c.scope = root_;
  auto make = [&](const char* name, Type t) -> Node* {
    Node* n = Impl::new_node(this, root_, reinterpret_cast<const uint8_t*>(name));
    if (n != nullptr && t != Type::Scope) n->object = Impl::new_object(c, t);
    return n;
  };
  make("_GPE", Type::Scope);
  make("_PR_", Type::Scope);
  make("_SB_", Type::Scope);
  make("_SI_", Type::Scope);
  make("_TZ_", Type::Scope);
  make("_GL_", Type::Mutex);
  if (Node* rev = make("_REV", Type::Integer)) rev->object->value = Impl::integer(2);
  if (Node* os = make("_OS_", Type::String)) {
    static const char kOs[] = "Microsoft Windows NT";
    os->object->value.type = Type::String;
    os->object->value.str = kOs;
    os->object->value.size = sizeof kOs - 1;
  }
  if (Node* osi = make("_OSI", Type::Method)) {
    osi->object->argc = 1;
    osi->object->native_osi = true;
  }
}

Error Interpreter::load_table(const uint8_t* table, size_t size) {
  if (root_ == nullptr) return Error::NoMemory;
  if (size < 36) return Error::BadTable;
  uint32_t len = uint32_t{table[4]} | uint32_t{table[5]} << 8 | uint32_t{table[6]} << 16 | uint32_t{table[7]} << 24;
  if (len > size || len < 36) return Error::BadTable;
  // §5.2.11.1: ревизия DSDT < 2 — целые 32-битные.
  if (table[0] == 'D' && table[1] == 'S' && table[2] == 'D' && table[3] == 'T') int_bits_ = table[8] < 2 ? 32 : 64;
  Ctx c{};
  c.in = this;
  c.p = table + 36;
  c.scope = root_;
  bool ok = Impl::termlist(c, table + len);
  error_offset_ = ok ? 0 : static_cast<size_t>(c.p - table);
  scratch_.reset();
  return ok ? Error::Ok : c.err;
}

Node* Interpreter::find(StringView path, Node* scope) const {
  if (root_ == nullptr) return nullptr;
  Node* n = scope != nullptr ? scope : root_;
  size_t i = 0;
  if (i < path.size() && path[i] == '\\') {
    n = root_;
    ++i;
  }
  while (i < path.size() && path[i] == '^') {
    if (n->parent != nullptr) n = n->parent;
    ++i;
  }
  bool single = true;
  for (size_t k = i; k < path.size(); ++k) {
    if (path[k] == '.') single = false;
  }
  bool searchable = single && scope != nullptr && path.size() > 0 && path[0] != '\\' && path[0] != '^';
  while (i < path.size()) {
    uint8_t seg[4] = {'_', '_', '_', '_'};
    size_t k = 0;
    while (i < path.size() && path[i] != '.') {
      if (k < 4) seg[k++] = static_cast<uint8_t>(path[i]);
      ++i;
    }
    if (i < path.size()) ++i;
    Node* next = Impl::child(n, seg);
    if (next == nullptr && searchable) {
      for (Node* s = n->parent; s != nullptr && next == nullptr; s = s->parent) next = Impl::child(s, seg);
    }
    if (next == nullptr) return nullptr;
    n = next;
  }
  return n;
}

Error Interpreter::evaluate(Node* node, const Value* args, uint32_t argc, Value* out) {
  *out = {};
  if (node == nullptr) return Error::NotFound;
  Ctx c{};
  c.in = this;
  c.scope = node->parent != nullptr ? node->parent : root_;
  Value tmp{};
  bool ok;
  if (node->object != nullptr && node->object->type == Type::Method) {
    if (argc > node->object->argc) return Error::BadArgs;
    Frame f{};
    for (uint32_t i = 0; i < argc; ++i) f.args[i] = args[i];
    ok = Impl::call(c, node, &f, &tmp);
  } else {
    ok = Impl::read_object(c, node, &tmp);
  }
  if (ok) ok = Impl::copy(c, out, tmp, true);
  scratch_.reset();
  return ok ? Error::Ok : (c.err != Error::Ok ? c.err : Error::BadType);
}

Error Interpreter::evaluate(StringView path, Value* out, Node* scope) {
  Node* n = find(path, scope);
  if (n == nullptr) {
    *out = {};
    return Error::NotFound;
  }
  return evaluate(n, nullptr, 0, out);
}

void Interpreter::walk(Node* from, bool (*fn)(Node* n, void* ctx), void* ctx) const {
  if (from == nullptr) return;
  // Без рекурсии: по ссылкам parent/next.
  Node* n = from;
  for (;;) {
    if (!fn(n, ctx)) return;
    if (n->child != nullptr) {
      n = n->child;
      continue;
    }
    while (n != from && n->next == nullptr) n = n->parent;
    if (n == from) return;
    n = n->next;
  }
}

Type Interpreter::type_of(const Node* n) const {
  return n == nullptr || n->object == nullptr ? Type::Scope : n->object->type;
}

size_t Interpreter::path_of(const Node* n, char* buf, size_t cap) const {
  const Node* chain[64];
  size_t depth = 0;
  for (const Node* k = n; k != nullptr && k != root_ && depth < 64; k = k->parent) chain[depth++] = k;
  size_t len = 0;
  auto put = [&](char ch) {
    if (len + 1 < cap) buf[len] = ch;
    ++len;
  };
  put('\\');
  for (size_t i = depth; i > 0; --i) {
    for (int j = 0; j < 4; ++j) put(chain[i - 1]->name[j]);
    if (i != 1) put('.');
  }
  if (cap != 0) buf[len < cap ? len : cap - 1] = 0;
  return len;
}

}  // namespace lib::acpi::aml
