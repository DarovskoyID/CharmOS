#include <lib/font/truetype.hpp>

namespace lib::font {
uint16_t Bytes::u16(size_t at) const {
  return has(at, 2) ? static_cast<uint16_t>((uint16_t{data[at]} << 8) | data[at + 1]) : 0;
}
uint32_t Bytes::u32(size_t at) const {
  return has(at, 4) ? (uint32_t{u16(at)} << 16) | u16(at + 2) : 0;
}
Bytes Bytes::slice(size_t at, size_t n) const { return has(at, n) ? Bytes{data + at, n} : Bytes{}; }

namespace {
constexpr uint32_t tag(char a, char b, char c, char d) {
  return (uint32_t{static_cast<uint8_t>(a)} << 24) | (uint32_t{static_cast<uint8_t>(b)} << 16) |
         (uint32_t{static_cast<uint8_t>(c)} << 8) | static_cast<uint8_t>(d);
}
Bytes table(Bytes f, uint32_t name) {
  for (size_t i = 0; i < f.u16(4); ++i) {
    size_t p = 12 + i * 16;
    if (f.u32(p) == name) return f.slice(f.u32(p + 8), f.u32(p + 12));
  }
  return {};
}
int32_t floor_i(double v) { int32_t i = static_cast<int32_t>(v); return v < i ? i - 1 : i; }
int32_t ceil_i(double v) { return -floor_i(-v); }
Point mid(Point a, Point b) { return {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, 1}; }
bool line(Workspace* w, Point a, Point b) {
  if (a.y == b.y) return true;
  if (w->edge_count == Workspace::kEdges) return false;
  w->edges[w->edge_count++] = {a.x, a.y, b.x, b.y};
  return true;
}
bool curve(Workspace* w, Point a, Point b, Point c, unsigned depth = 0) {
  double dx = a.x - 2 * b.x + c.x, dy = a.y - 2 * b.y + c.y;
  if (depth == 10 || dx * dx + dy * dy <= 0.0625) return line(w, a, c);
  Point ab = mid(a, b), bc = mid(b, c), m = mid(ab, bc);
  return curve(w, a, ab, m, depth + 1) && curve(w, m, bc, c, depth + 1);
}
bool edges(Workspace* w) {
  uint32_t first = 0;
  w->edge_count = 0;
  for (uint32_t ci = 0; ci < w->contour_count; ++ci) {
    uint32_t end = w->ends[ci];
    Point a = w->points[first], z = w->points[end];
    Point start = (a.flags & 1) != 0 ? a : ((z.flags & 1) != 0 ? z : mid(a, z));
    Point at = start;
    uint32_t i = first + ((a.flags & 1) != 0 ? 1u : 0u);
    while (i <= end) {
      Point b = w->points[i];
      if ((b.flags & 1) != 0) {
        if (!line(w, at, b)) return false;
        at = b;
        ++i;
      } else {
        Point c = i < end ? w->points[i + 1] : start;
        if ((c.flags & 1) == 0) {
          c = mid(b, c);
          ++i;
        } else {
          i += 2;
        }
        if (!curve(w, at, b, c)) return false;
        at = c;
      }
    }
    if (!line(w, at, start)) return false;
    first = end + 1;
  }
  return true;
}
}  // namespace

bool Face::open(Bytes f) {
  *this = {};
  if (f.data == nullptr || !f.has(0, 12) || f.u32(0) != 0x00010000 || !f.has(12, size_t{f.u16(4)} * 16)) return false;
  // All records must stay inside the file, including unknown tables.
  for (size_t i = 0; i < f.u16(4); ++i) {
    size_t p = 12 + i * 16;
    if (!f.has(f.u32(p + 8), f.u32(p + 12))) return false;
    for (size_t j = 0; j < i; ++j) if (f.u32(12 + j * 16) == f.u32(p)) return false;
  }
  head_ = table(f, tag('h','e','a','d'));
  hhea_ = table(f, tag('h','h','e','a'));
  hmtx_ = table(f, tag('h','m','t','x'));
  loca_ = table(f, tag('l','o','c','a'));
  glyf_ = table(f, tag('g','l','y','f'));
  Bytes maxp = table(f, tag('m','a','x','p')), cm = table(f, tag('c','m','a','p'));
  if (!head_.has(0, 54) || head_.u32(0) != 0x10000 || head_.u32(12) != 0x5f0f3cf5 ||
      !hhea_.has(0, 36) || hhea_.u32(0) != 0x10000 || !maxp.has(0, 32) || maxp.u32(0) != 0x10000 ||
      !cm.has(0, 4) || cm.u16(0) != 0 || glyf_.data == nullptr) return false;
  units_ = head_.u16(18); glyphs_ = maxp.u16(4); metrics_ = hhea_.u16(34);
  if (units_ < 16 || units_ > 16384 || glyphs_ == 0 || metrics_ == 0 || metrics_ > glyphs_ ||
      head_.u16(50) > 1 || head_.u16(52) != 0 || hhea_.u16(32) != 0) return false;
  long_loca_ = head_.u16(50) == 1;
  if (!hmtx_.has(0, size_t{metrics_} * 4 + static_cast<size_t>(glyphs_ - metrics_) * 2) ||
      !loca_.has(0, (size_t{glyphs_} + 1) * (long_loca_ ? 4u : 2u))) return false;
  uint32_t prev = 0;
  for (uint32_t i = 0; i <= glyphs_; ++i) {
    uint32_t off = location(i);
    if (off < prev || off > glyf_.size) return false;
    prev = off;
  }
  if (!cm.has(4, size_t{cm.u16(2)} * 8)) return false;
  int best = 0;
  for (size_t i = 0; i < cm.u16(2); ++i) {
    size_t p = 4 + i * 8;
    uint16_t platform = cm.u16(p), encoding = cm.u16(p + 2);
    if (!(platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10)))) continue;
    size_t off = cm.u32(p + 4);
    if (!cm.has(off, 4)) return false;
    uint16_t fmt = cm.u16(off);
    if (fmt != 4 && fmt != 12) continue;
    if (fmt == 12 && !cm.has(off, 16)) return false;
    size_t len = fmt == 4 ? cm.u16(off + 2) : cm.u32(off + 4);
    Bytes sub = cm.slice(off, len);
    if (sub.data == nullptr) return false;
    if (fmt == 4) {
      uint32_t n = sub.u16(6) / 2;
      if (!sub.has(0, 16) || n == 0 || (sub.u16(6) & 1) != 0 || !sub.has(16, size_t{n} * 8)) return false;
      uint32_t last = 0;
      for (uint32_t k = 0; k < n; ++k) {
        uint32_t end = sub.u16(14 + k * 2), start = sub.u16(16 + n * 2 + k * 2);
        size_t r = 16 + n * 6 + k * 2;
        if (start > end || (k != 0 && start <= last)) return false;
        if (sub.u16(r) != 0 && !sub.has(r + sub.u16(r), size_t{end - start + 1} * 2)) return false;
        last = end;
      }
      if (last != 0xffff) return false;
    } else {
      if (!sub.has(0, 16) || !sub.has(16, uint64_t{sub.u32(12)} * 12)) return false;
      uint32_t last = 0;
      for (uint32_t k = 0; k < sub.u32(12); ++k) {
        size_t q = 16 + size_t{k} * 12;
        uint32_t start = sub.u32(q), end = sub.u32(q + 4), id = sub.u32(q + 8);
        if (start > end || end > 0x10ffff || (k != 0 && start <= last) || id >= glyphs_ || end - start >= glyphs_ - id) return false;
        last = end;
      }
    }
    int score = fmt == 12 ? 2 : 1;
    if (score > best) { cmap_ = sub; best = score; }
  }
  return best != 0;
}

uint32_t Face::location(uint32_t id) const { return long_loca_ ? loca_.u32(size_t{id} * 4) : uint32_t{loca_.u16(size_t{id} * 2)} * 2; }
uint16_t Face::glyph(uint32_t cp) const {
  if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
  uint32_t id = 0;
  if (cmap_.u16(0) == 12) {
    uint32_t lo = 0, hi = cmap_.u32(12);
    while (lo < hi) {
      uint32_t m = lo + (hi - lo) / 2;
      size_t p = 16 + size_t{m} * 12;
      if (cp < cmap_.u32(p)) hi = m;
      else if (cp > cmap_.u32(p + 4)) lo = m + 1;
      else { id = cmap_.u32(p + 8) + cp - cmap_.u32(p); break; }
    }
  } else if (cmap_.u16(0) == 4 && cp <= 0xffff) {
    uint32_t n = cmap_.u16(6) / 2;
    for (uint32_t i = 0; i < n; ++i) {
      if (cp > cmap_.u16(14 + i * 2)) continue;
      uint32_t start = cmap_.u16(16 + n * 2 + i * 2);
      if (cp < start) return 0;
      uint16_t delta = cmap_.u16(16 + n * 4 + i * 2);
      size_t p = 16 + n * 6 + i * 2;
      uint16_t range = cmap_.u16(p);
      id = range == 0 ? cp : cmap_.u16(p + range + size_t{cp - start} * 2);
      if (range == 0 || id != 0) id = (id + delta) & 0xffff;
      break;
    }
  }
  return id < glyphs_ ? static_cast<uint16_t>(id) : 0;
}
int32_t Face::advance(uint16_t id, uint32_t ppem) const {
  if (units_ == 0 || id >= glyphs_ || ppem > 256) return 0;
  uint32_t metric = id < metrics_ ? id : uint32_t{metrics_} - 1;
  return static_cast<int32_t>((uint32_t{hmtx_.u16(metric * 4)} * ppem + units_ / 2u) / units_);
}
int32_t Face::ascent(uint32_t ppem) const {
  return units_ == 0 || ppem > 256 ? 0 : ceil_i(static_cast<double>(hhea_.i16(4)) * ppem / units_);
}

bool Face::outline(uint16_t id, Workspace* w, uint32_t depth) const {
  if (depth > 16 || id >= glyphs_) return false;
  uint32_t a = location(id), b = location(uint32_t{id} + 1);
  if (a == b) return true;
  Bytes g = glyf_.slice(a, b - a);
  if (!g.has(0, 10)) return false;
  int32_t nc = g.i16(0);
  uint32_t base = w->point_count;
  if (nc >= 0) {
    if (nc == 0) return true;
    uint32_t ncont = static_cast<uint32_t>(nc);
    if (ncont > Workspace::kContours - w->contour_count || !g.has(10, size_t{ncont} * 2 + 2)) return false;
    uint32_t count = uint32_t{g.u16(10 + (ncont - 1) * 2)} + 1;
    if (count > Workspace::kPoints - base) return false;
    uint32_t prev = 0;
    for (uint32_t i = 0; i < ncont; ++i) {
      uint32_t e = g.u16(10 + i * 2);
      if ((i != 0 && e <= prev) || e >= count) return false;
      w->ends[w->contour_count++] = base + e;
      prev = e;
    }
    size_t p = 10 + size_t{ncont} * 2;
    uint16_t ins = g.u16(p); p += 2;
    if (!g.has(p, ins)) return false;
    p += ins;  // Deliberately unhinted: never execute font bytecode.
    for (uint32_t i = 0; i < count;) {
      if (!g.has(p, 1)) return false;
      uint8_t flags = g.data[p++];
      uint32_t repeat = 1;
      if ((flags & 8) != 0) {
        if (!g.has(p, 1)) return false;
        repeat += g.data[p++];
      }
      if (repeat > count - i) return false;
      while (repeat-- != 0) w->points[base + i++].flags = flags;
    }
    for (unsigned axis = 0; axis < 2; ++axis) {
      int32_t pos = 0;
      uint8_t short_bit = axis == 0 ? 2 : 4, same_bit = axis == 0 ? 16 : 32;
      for (uint32_t i = 0; i < count; ++i) {
        Point& pt = w->points[base + i];
        int32_t delta = 0;
        if ((pt.flags & short_bit) != 0) {
          if (!g.has(p, 1)) return false;
          delta = g.data[p++];
          if ((pt.flags & same_bit) == 0) delta = -delta;
        } else if ((pt.flags & same_bit) == 0) {
          if (!g.has(p, 2)) return false;
          delta = g.i16(p); p += 2;
        }
        pos += delta;
        if (pos < -32768 || pos > 32767) return false;
        if (axis == 0) pt.x = pos; else pt.y = pos;
      }
    }
    w->point_count += count;
    return true;
  }
  size_t p = 10;
  uint16_t flags = 0;
  unsigned components = 0;
  do {
    if (++components > 256 || !g.has(p, 4)) return false;
    flags = g.u16(p); uint16_t child = g.u16(p + 2); p += 4;
    bool words = (flags & 1) != 0, xy = (flags & 2) != 0;
    if (!g.has(p, words ? 4u : 2u)) return false;
    int32_t arg1, arg2;
    if (words) {
      arg1 = xy ? g.i16(p) : g.u16(p); arg2 = xy ? g.i16(p + 2) : g.u16(p + 2); p += 4;
    } else {
      arg1 = xy ? static_cast<int8_t>(g.data[p]) : g.data[p];
      arg2 = xy ? static_cast<int8_t>(g.data[p + 1]) : g.data[p + 1]; p += 2;
    }
    double xx = 1, yy = 1, yx = 0, xyc = 0;
    unsigned transform = flags & (8 | 64 | 128);
    if (transform != 0) {
      size_t bytes = transform == 8 ? 2u : (transform == 64 ? 4u : 8u);
      if ((transform != 8 && transform != 64 && transform != 128) || !g.has(p, bytes)) return false;
      xx = static_cast<double>(g.i16(p)) / 16384;
      if (transform == 8) yy = xx;
      else if (transform == 64) yy = static_cast<double>(g.i16(p + 2)) / 16384;
      else { yx = static_cast<double>(g.i16(p + 2)) / 16384; xyc = static_cast<double>(g.i16(p + 4)) / 16384; yy = static_cast<double>(g.i16(p + 6)) / 16384; }
      p += bytes;
    }
    uint32_t begin = w->point_count;
    if (!outline(child, w, depth + 1)) return false;
    for (uint32_t i = begin; i < w->point_count; ++i) {
      Point& pt = w->points[i]; double x = pt.x, y = pt.y;
      pt.x = xx * x + xyc * y; pt.y = yx * x + yy * y;
    }
    double dx = arg1, dy = arg2;
    if (xy) {
      if ((flags & 0x1800) == 0x1800) return false;
      if ((flags & 0x800) != 0) { dx = xx * arg1 + xyc * arg2; dy = yx * arg1 + yy * arg2; }
    } else {
      if (static_cast<uint32_t>(arg1) >= begin - base || static_cast<uint32_t>(arg2) >= w->point_count - begin) return false;
      dx = w->points[base + static_cast<uint32_t>(arg1)].x - w->points[begin + static_cast<uint32_t>(arg2)].x;
      dy = w->points[base + static_cast<uint32_t>(arg1)].y - w->points[begin + static_cast<uint32_t>(arg2)].y;
    }
    for (uint32_t i = begin; i < w->point_count; ++i) {
      Point& pt = w->points[i]; pt.x += dx; pt.y += dy;
      if (pt.x < -1e6 || pt.x > 1e6 || pt.y < -1e6 || pt.y > 1e6) return false;
    }
  } while ((flags & 32) != 0);
  if ((flags & 256) != 0 && (!g.has(p, 2) || !g.has(p + 2, g.u16(p)))) return false;
  return true;
}

bool Face::rasterize(uint16_t id, uint32_t ppem, Bitmap* out, Workspace* w) const {
  if (out == nullptr || w == nullptr || out->pixels == nullptr || units_ == 0 || ppem == 0 || ppem > 256) return false;
  uint32_t capacity_w = out->width, capacity_h = out->height;
  if (out->stride < capacity_w || capacity_w > 1024 || capacity_h > 1024) return false;
  out->width = out->height = 0; out->left = out->top = 0; out->advance = advance(id, ppem);
  w->point_count = w->contour_count = w->edge_count = 0;
  if (!outline(id, w, 0)) return false;
  if (w->point_count == 0) return true;
  double scale = static_cast<double>(ppem) / units_, xmin = 1e9, ymin = 1e9, xmax = -1e9, ymax = -1e9;
  for (uint32_t i = 0; i < w->point_count; ++i) {
    Point& pt = w->points[i]; pt.x *= scale; pt.y *= -scale;
    if (pt.x < xmin) xmin = pt.x; if (pt.x > xmax) xmax = pt.x;
    if (pt.y < ymin) ymin = pt.y; if (pt.y > ymax) ymax = pt.y;
  }
  int32_t left = floor_i(xmin), top = floor_i(ymin), width = ceil_i(xmax) - left, height = ceil_i(ymax) - top;
  if (width < 0 || height < 0 || static_cast<uint32_t>(width) > capacity_w || static_cast<uint32_t>(height) > capacity_h) return false;
  out->left = left; out->top = top; out->width = static_cast<uint32_t>(width); out->height = static_cast<uint32_t>(height);
  for (uint32_t i = 0; i < w->point_count; ++i) { w->points[i].x -= left; w->points[i].y -= top; }
  if (!edges(w)) return false;
  // 4x4 sample centers, non-zero winding; half-open edges count shared vertices once.
  for (uint32_t y = 0; y < out->height; ++y) {
    uint8_t counts[1024]{};
    for (uint32_t sy = 0; sy < 4; ++sy) {
      double scan = y + (sy + 0.5) / 4;
      uint32_t n = 0;
      for (uint32_t i = 0; i < w->edge_count; ++i) {
        const Edge& e = w->edges[i];
        if (!((e.y0 <= scan && scan < e.y1) || (e.y1 <= scan && scan < e.y0))) continue;
        Crossing c{e.x0 + (scan - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0), e.y1 > e.y0 ? 1 : -1};
        uint32_t at = n++;
        while (at > 0 && w->crossings[at - 1].x > c.x) { w->crossings[at] = w->crossings[at - 1]; --at; }
        w->crossings[at] = c;
      }
      int winding = 0;
      for (uint32_t i = 0; i + 1 < n; ++i) {
        winding += w->crossings[i].winding;
        if (winding == 0) continue;
        int32_t x0 = ceil_i(w->crossings[i].x * 4 - 0.5), x1 = ceil_i(w->crossings[i + 1].x * 4 - 0.5);
        if (x0 < 0) x0 = 0; if (x1 > width * 4) x1 = width * 4;
        for (int32_t x = x0; x < x1; ++x) ++counts[static_cast<uint32_t>(x) / 4];
      }
    }
    for (uint32_t x = 0; x < out->width; ++x) out->pixels[size_t{y} * out->stride + x] = static_cast<uint8_t>((uint32_t{counts[x]} * 255 + 8) / 16);
  }
  return true;
}

uint32_t utf8_next(const char** text) {
  const auto* s = reinterpret_cast<const uint8_t*>(*text);
  uint32_t cp = s[0];
  if (cp == 0) return 0;
  ++*text;
  if (cp < 128) return cp;
  unsigned n = cp >= 0xc2 && cp <= 0xdf ? 2u : (cp >= 0xe0 && cp <= 0xef ? 3u : (cp >= 0xf0 && cp <= 0xf4 ? 4u : 0u));
  if (n == 0) return 0xfffd;
  cp &= (1u << (7 - n)) - 1;
  for (unsigned i = 1; i < n; ++i) {
    if (s[i] == 0 || (s[i] & 0xc0) != 0x80) return 0xfffd;
    cp = (cp << 6) | (s[i] & 0x3f);
  }
  if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
      cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0xfffd;
  *text += n - 1;
  return cp;
}
}  // namespace lib::font
