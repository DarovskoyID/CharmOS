#include "tcp.hpp"

#include "../core/log.hpp"
#include "../platform/platform.hpp"
#include "../sched/sched.hpp"
#include "../sync/wait.hpp"
#include "ip.hpp"

namespace kernel::net {

namespace {

constexpr uint8_t kProtoTcp = 6;
constexpr uint8_t kFin = 0x01, kSyn = 0x02, kRst = 0x04, kPsh = 0x08, kAck = 0x10;
constexpr uint32_t kBuf = 16384;  // буферы отправки и приёма
constexpr uint32_t kMss = 1460;   // RFC 6691: 1500 − 40
constexpr uint32_t kConns = 64;
constexpr uint32_t kMaxBacklog = 16;
constexpr uint64_t kMs = 1'000'000;
constexpr uint64_t kInitialRto = 1000 * kMs;  // RFC 6298 (2.1)
// (2.4) — «SHOULD» 1 с; берём 200 мс, как распространённые стеки: иначе каждая
// потеря при малом окне (быстрой повторной передачи нет) стоит секунду.
constexpr uint64_t kMinRto = 200 * kMs;
constexpr uint64_t kMaxRto = 60'000 * kMs;    // (2.5)
constexpr uint64_t kTimeWait = 2'000 * kMs;   // 2·MSL (сокращено: тесты, нет RTC)
constexpr uint32_t kMaxRetries = 12;

enum class State : uint8_t {
  Closed, Listen, SynSent, SynRcvd, Established, FinWait1, FinWait2, CloseWait, Closing, LastAck, TimeWait
};

struct Ring {
  uint8_t data[kBuf];
  uint32_t start, size;  // кольцо байтов
  uint32_t free() const { return kBuf - size; }
  void push(const uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) data[(start + size + i) % kBuf] = p[i];
    size += n;
  }
  void peek(uint32_t off, uint8_t* p, uint32_t n) const {
    for (uint32_t i = 0; i < n; ++i) p[i] = data[(start + off + i) % kBuf];
  }
  void drop(uint32_t n) {
    start = (start + n) % kBuf;
    size -= n;
  }
  // Байты за концом данных (сборка сегментов не по порядку): off — от конца.
  void put_ahead(uint32_t off, const uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) data[(start + size + off + i) % kBuf] = p[i];
  }
};

constexpr uint32_t kOooRanges = 8;
struct Range {
  uint32_t begin, end;  // номера последовательности [begin, end)
};

struct Tcb {
  bool used;
  bool held;  // есть хендл (или ждёт accept)
  State state;
  uint16_t lport, rport;
  uint32_t rip;
  // Отправка: SND.UNA — начало snd (байт данных), FIN — после данных.
  uint32_t iss, snd_una, snd_nxt, snd_wnd, snd_wl1, snd_wl2;
  uint32_t snd_max;   // наибольший отправленный номер (после отката snd_nxt при повторе)
  uint32_t dup_acks;  // RFC 5681 §3.2
  uint32_t irs, rcv_nxt;
  Ring snd, rcv;
  Range ooo[kOooRanges];  // принятое не по порядку (данные уже в rcv за концом)
  uint32_t ooo_count;
  bool fin_queued, fin_sent, fin_rcvd;
  bool shut_read;
  Status error;  // Ok или ConnectionReset/Refused/TimedOut
  // RFC 6298
  uint64_t rto, srtt, rttvar;
  bool rtt_running;
  uint32_t rtt_seq;
  uint64_t rtt_start;
  uint64_t retx_at;  // 0 — таймер не взведён
  uint32_t retries;
  // RFC 5681
  uint32_t cwnd, ssthresh;
  uint64_t time_wait_until;
  // Слушатель: очередь готовых соединений.
  int parent;
  int backlog[kMaxBacklog];
  uint32_t backlog_len, backlog_max;
};

constinit sync::Mutex g_lock{"net.tcp"};
Tcb g_tcb[kConns];
constinit uint16_t g_ephemeral = 49152;
constinit uint32_t g_iss_clock = 0x43480000;
constinit bool g_timer = false;
constinit uint32_t g_drop_every = 0;
constinit uint32_t g_drop_count = 0;

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t* p) {
  return uint32_t{p[0]} << 24 | uint32_t{p[1]} << 16 | uint32_t{p[2]} << 8 | p[3];
}
void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}
void put32(uint8_t* p, uint32_t v) {
  put16(p, static_cast<uint16_t>(v >> 16));
  put16(p + 2, static_cast<uint16_t>(v));
}
// Сравнения последовательностей по модулю 2^32 (RFC 9293 §3.4).
bool lt(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) < 0; }
bool le(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) <= 0; }

bool drop_now() {
  if (g_drop_every == 0) return false;
  // Псевдослучайно с вероятностью 1/n (LCG): строгая периодичность «каждый
  // n-й» резонирует с редкими повторами и даёт не потери, а многоминутные паузы.
  g_drop_count = g_drop_count * 1664525u + 1013904223u;
  bool d = (g_drop_count >> 16) % g_drop_every == 0;
  return d;
}

uint32_t rcv_wnd(const Tcb& t) { return t.rcv.free() > 0xffff ? 0xffff : t.rcv.free(); }

// Сегмент с данными snd[off..off+len) (под g_lock; ARP не ждём — поток
// интерфейса не должен ждать сам себя, потерю закроет повтор).
void emit(Tcb& t, uint32_t seq, uint8_t flags, uint32_t off, uint32_t len) {
  uint8_t seg[20 + 4 + kMss];
  __builtin_memset(seg, 0, 24);
  put16(seg, t.lport);
  put16(seg + 2, t.rport);
  put32(seg + 4, seq);
  put32(seg + 8, (flags & kAck) != 0 ? t.rcv_nxt : 0);
  uint32_t hlen = 20;
  if ((flags & kSyn) != 0) {  // MSS (RFC 9293 §3.7.1)
    seg[20] = 2, seg[21] = 4;
    put16(seg + 22, kMss);
    hlen = 24;
  }
  seg[12] = static_cast<uint8_t>((hlen / 4) << 4);
  seg[13] = flags;
  put16(seg + 14, static_cast<uint16_t>(rcv_wnd(t)));
  if (len != 0) t.snd.peek(off, seg + hlen, len);
  uint32_t src = config().ip;
  uint8_t pseudo[12];
  __builtin_memcpy(pseudo, &src, 4);
  __builtin_memcpy(pseudo + 4, &t.rip, 4);
  pseudo[8] = 0, pseudo[9] = kProtoTcp;
  put16(pseudo + 10, static_cast<uint16_t>(hlen + len));
  uint16_t first = static_cast<uint16_t>(~checksum(pseudo, 12));
  put16(seg + 16, checksum(seg, hlen + len, first));
  if (drop_now()) return;
  (void)send_ip(t.rip, kProtoTcp, seg, hlen + len, 0);
}

void send_rst(uint32_t dst, uint16_t sport, uint16_t dport, uint32_t seq, uint32_t ack, bool with_ack) {
  Tcb t = {};
  t.lport = sport, t.rport = dport, t.rip = dst, t.rcv_nxt = ack;
  emit(t, seq, static_cast<uint8_t>(kRst | (with_ack ? kAck : 0)), 0, 0);
}

void arm(Tcb& t) {
  if (t.retx_at == 0) t.retx_at = platform::now_ns() + t.rto;
}

void reset_tcb(Tcb& t) {
  bool held = t.held;
  int parent = t.parent;
  __builtin_memset(static_cast<void*>(&t), 0, sizeof t);
  t.used = held;  // объект живёт, пока есть хендл: состояние Closed с ошибкой
  t.held = held;
  t.parent = parent;
}

void close_tcb(Tcb& t, Status err) {
  if (t.state == State::Listen) {
    for (uint32_t i = 0; i < t.backlog_len; ++i) {
      Tcb& c = g_tcb[t.backlog[i]];
      c.held = false;
      if (c.state != State::Closed) close_tcb(c, Status::ConnectionReset);
      c.used = false;
    }
  }
  Status keep = err;
  reset_tcb(t);
  t.error = keep;
}

// Отправить, что разрешают окна (под g_lock).
void output(Tcb& t) {
  if (t.state != State::Established && t.state != State::CloseWait && t.state != State::FinWait1 &&
      t.state != State::LastAck && t.state != State::Closing) {
    return;
  }
  uint32_t window = t.snd_wnd < t.cwnd ? t.snd_wnd : t.cwnd;
  for (;;) {
    uint32_t in_flight = t.snd_nxt - t.snd_una;
    if (t.fin_sent && t.snd_nxt != t.snd_max) t.fin_sent = false;
    uint32_t sent_data = in_flight - (t.fin_sent ? 1 : 0);
    uint32_t avail = t.snd.size > sent_data ? t.snd.size - sent_data : 0;
    uint32_t room = window > in_flight ? window - in_flight : 0;
    uint32_t len = avail < room ? avail : room;
    if (len > kMss) len = kMss;
    // Нейгл (RFC 9293 §3.7.4): мелкий сегмент — только без неподтверждённых.
    if (len != 0 && len < kMss && in_flight != 0 && len == avail) len = 0;
    if (len == 0) {
      if (t.fin_queued && !t.fin_sent && avail == 0 && sent_data == t.snd.size) {
        emit(t, t.snd_nxt, kFin | kAck, 0, 0);
        t.snd_nxt += 1;
        if (lt(t.snd_max, t.snd_nxt)) t.snd_max = t.snd_nxt;
        t.fin_sent = true;
        if (t.state == State::Established) t.state = State::FinWait1;
        if (t.state == State::CloseWait) t.state = State::LastAck;
        arm(t);
      }
      return;
    }
    if (!t.rtt_running) {  // RFC 6298 (Karn): мерим один сегмент за раз
      t.rtt_running = true;
      t.rtt_seq = t.snd_nxt + len;
      t.rtt_start = platform::now_ns();
    }
    emit(t, t.snd_nxt, kAck | kPsh, sent_data, len);
    t.snd_nxt += len;
    if (lt(t.snd_max, t.snd_nxt)) t.snd_max = t.snd_nxt;
    arm(t);
  }
}

void retransmit(Tcb& t) {
  uint64_t now = platform::now_ns();
  if (++t.retries > kMaxRetries) {
    close_tcb(t, Status::TimedOut);
    return;
  }
  t.rto = t.rto * 2 > kMaxRto ? kMaxRto : t.rto * 2;  // (5.5)
  t.rtt_running = false;                              // Karn
  t.retx_at = now + t.rto;
  if (t.state == State::SynSent) {
    emit(t, t.iss, kSyn, 0, 0);
    return;
  }
  if (t.state == State::SynRcvd) {
    emit(t, t.iss, kSyn | kAck, 0, 0);
    return;
  }
  // RFC 5681 (4): после таймаута — ssthresh = max(FlightSize/2, 2·MSS), cwnd = 1·MSS.
  uint32_t flight = t.snd_max - t.snd_una;
  t.ssthresh = flight / 2 > 2 * kMss ? flight / 2 : 2 * kMss;
  t.cwnd = kMss;
  uint32_t data = t.snd.size;
  if (data != 0) {
    uint32_t len = data < kMss ? data : kMss;
    emit(t, t.snd_una, kAck | kPsh, 0, len);
    // snd_nxt откатывается: дальнейшие сегменты пойдут заново по окну.
    t.snd_nxt = t.snd_una + len;
    t.fin_sent = false;  // FIN уйдёт заново после данных
  } else if (t.fin_sent) {
    emit(t, t.snd_una, kFin | kAck, 0, 0);
  }
}

void timer(void*) {
  for (;;) {
    sched::sleep_ns(10 * kMs);
    sync::MutexGuard guard(g_lock);
    uint64_t now = platform::now_ns();
    for (Tcb& t : g_tcb) {
      if (!t.used) continue;
      if (t.state == State::TimeWait && now >= t.time_wait_until) {
        close_tcb(t, Status::Ok);
        if (!t.held) t.used = false;
        continue;
      }
      if (t.retx_at != 0 && now >= t.retx_at) retransmit(t);
      if (t.state == State::Closed && !t.held) t.used = false;
    }
  }
}

void ensure_timer() {
  if (!g_timer) {
    g_timer = true;
    (void)sched::spawn("tcp", &timer, nullptr);
  }
}

int alloc_tcb() {
  for (uint32_t i = 0; i < kConns; ++i) {
    if (!g_tcb[i].used) {
      Tcb& t = g_tcb[i];
      __builtin_memset(static_cast<void*>(&t), 0, sizeof t);
      t.used = true;
      t.parent = kNoConn;
      t.rto = kInitialRto;
      t.cwnd = 2 * kMss;  // RFC 5681 (3.1): IW
      t.ssthresh = 0xffff;
      return static_cast<int>(i);
    }
  }
  static bool warned = false;
  if (!warned) {
    warned = true;
    kprint("tcp: connection table full\n");
  }
  return kNoConn;
}

bool port_in_use(uint16_t port) {
  for (const Tcb& t : g_tcb) {
    if (t.used && t.lport == port && t.state != State::Closed) return true;
  }
  return false;
}

uint32_t new_iss() { return g_iss_clock += 64000 + static_cast<uint32_t>(platform::now_ns() >> 12); }

void established_ack(Tcb& t, uint32_t ack, uint32_t wnd, uint32_t seq, uint32_t dlen) {
  // Повторный ACK (RFC 5681 §2): без данных, то же окно, есть неподтверждённое.
  if (ack == t.snd_una && dlen == 0 && wnd == t.snd_wnd && t.snd_max != t.snd_una) {
    if (++t.dup_acks == 3 && t.snd.size != 0) {
      // §3.2: быстрая повторная передача.
      uint32_t flight = t.snd_max - t.snd_una;
      t.ssthresh = flight / 2 > 2 * kMss ? flight / 2 : 2 * kMss;
      uint32_t len = t.snd.size < kMss ? t.snd.size : kMss;
      emit(t, t.snd_una, kAck | kPsh, 0, len);
      t.cwnd = t.ssthresh + 3 * kMss;
      t.rtt_running = false;
    }
    return;
  }
  if (lt(t.snd_una, ack) && le(ack, t.snd_max)) {
    if (t.dup_acks >= 3) t.cwnd = t.ssthresh;  // выход из быстрого восстановления
    t.dup_acks = 0;
    if (lt(t.snd_nxt, ack)) t.snd_nxt = ack;  // подтверждено отправленное до отката
    uint32_t acked = ack - t.snd_una;
    uint32_t data_acked = acked;
    if (t.fin_sent && ack == t.snd_max) data_acked -= 1;  // подтверждён наш FIN
    if (data_acked > t.snd.size) data_acked = t.snd.size;
    t.snd.drop(data_acked);
    t.snd_una = ack;
    t.retries = 0;
    // RTT (RFC 6298 §2).
    if (t.rtt_running && le(t.rtt_seq, ack)) {
      uint64_t r = platform::now_ns() - t.rtt_start;
      if (t.srtt == 0) {
        t.srtt = r;
        t.rttvar = r / 2;
      } else {
        uint64_t d = t.srtt > r ? t.srtt - r : r - t.srtt;
        t.rttvar = (3 * t.rttvar + d) / 4;
        t.srtt = (7 * t.srtt + r) / 8;
      }
      uint64_t rto = t.srtt + (4 * t.rttvar > 10 * kMs ? 4 * t.rttvar : 10 * kMs);
      t.rto = rto < kMinRto ? kMinRto : rto > kMaxRto ? kMaxRto : rto;
      t.rtt_running = false;
    }
    // RFC 5681 §3.1: медленный старт / избегание перегрузки.
    if (t.cwnd < t.ssthresh) {
      t.cwnd += acked < kMss ? acked : kMss;
    } else {
      t.cwnd += kMss * kMss / (t.cwnd != 0 ? t.cwnd : 1) + 1;
    }
    if (t.cwnd > 64 * 1024) t.cwnd = 64 * 1024;
    t.retx_at = t.snd_una == t.snd_max ? 0 : platform::now_ns() + t.rto;  // (5.2), (5.3)
  }
  // Окно (RFC 9293 §3.10.7.4, пятый шаг).
  if (lt(t.snd_wl1, seq) || (t.snd_wl1 == seq && le(t.snd_wl2, ack))) {
    t.snd_wnd = wnd;
    t.snd_wl1 = seq;
    t.snd_wl2 = ack;
  }
}

}  // namespace

void tcp_set_drop(uint32_t every_n) {
  sync::MutexGuard guard(g_lock);
  g_drop_every = every_n;
  g_drop_count = 0;
}

void tcp_input(uint32_t src, uint32_t dst, const uint8_t* seg, uint32_t len) {
  if (len < 20) return;
  uint32_t hlen = (seg[12] >> 4) * 4u;
  if (hlen < 20 || hlen > len) return;
  uint8_t pseudo[12];
  __builtin_memcpy(pseudo, &src, 4);
  __builtin_memcpy(pseudo + 4, &dst, 4);
  pseudo[8] = 0, pseudo[9] = kProtoTcp;
  put16(pseudo + 10, static_cast<uint16_t>(len));
  if (checksum(seg, len, static_cast<uint16_t>(~checksum(pseudo, 12))) != 0) return;
  uint16_t sport = be16(seg), dport = be16(seg + 2);
  uint32_t seq = be32(seg + 4), ack = be32(seg + 8);
  uint8_t flags = seg[13];
  uint32_t wnd = be16(seg + 14);
  const uint8_t* data = seg + hlen;
  uint32_t dlen = len - hlen;
  uint32_t seg_len = dlen + ((flags & kSyn) != 0 ? 1 : 0) + ((flags & kFin) != 0 ? 1 : 0);

  sync::MutexGuard guard(g_lock);
  if (drop_now()) return;
  // Поиск: точное соединение, затем слушатель.
  Tcb* t = nullptr;
  Tcb* listener = nullptr;
  for (Tcb& c : g_tcb) {
    if (!c.used || c.lport != dport) continue;
    if (c.state != State::Listen && c.state != State::Closed && c.rip == src && c.rport == sport) t = &c;
    if (c.state == State::Listen) listener = &c;
  }
  if (t == nullptr && listener != nullptr) {
    // §3.10.7.2: LISTEN.
    if ((flags & kRst) != 0) return;
    if ((flags & kAck) != 0) {
      send_rst(src, dport, sport, ack, 0, false);
      return;
    }
    if ((flags & kSyn) == 0 || listener->backlog_len >= listener->backlog_max) return;
    int ci = alloc_tcb();
    if (ci == kNoConn) return;
    Tcb& c = g_tcb[ci];
    c.state = State::SynRcvd;
    c.lport = dport, c.rport = sport, c.rip = src;
    c.irs = seq, c.rcv_nxt = seq + 1;
    c.iss = new_iss();
    c.snd_una = c.iss, c.snd_nxt = c.iss + 1, c.snd_max = c.iss + 1;
    c.snd_wnd = wnd, c.snd_wl1 = seq, c.snd_wl2 = c.iss;
    c.parent = static_cast<int>(listener - g_tcb);
    emit(c, c.iss, kSyn | kAck, 0, 0);
    arm(c);
    return;
  }
  if (t == nullptr) {
    // §3.10.7.1: CLOSED.
    if ((flags & kRst) != 0) return;
    if ((flags & kAck) != 0) {
      send_rst(src, dport, sport, ack, 0, false);
    } else {
      send_rst(src, dport, sport, 0, seq + seg_len, true);
    }
    return;
  }
  Tcb& c = *t;
  if (c.state == State::SynSent) {
    // §3.10.7.3.
    bool ack_ok = false;
    if ((flags & kAck) != 0) {
      if (le(ack, c.iss) || lt(c.snd_nxt, ack)) {
        if ((flags & kRst) == 0) send_rst(src, dport, sport, ack, 0, false);
        return;
      }
      ack_ok = lt(c.snd_una, ack) && le(ack, c.snd_nxt);
    }
    if ((flags & kRst) != 0) {
      if (ack_ok) close_tcb(c, Status::ConnectionRefused);
      return;
    }
    if ((flags & kSyn) == 0) return;
    c.irs = seq;
    c.rcv_nxt = seq + 1;
    if (ack_ok) c.snd_una = ack;
    c.snd_wnd = wnd, c.snd_wl1 = seq, c.snd_wl2 = ack;
    if (lt(c.iss, c.snd_una)) {
      c.state = State::Established;
      c.retx_at = 0;
      c.retries = 0;
      emit(c, c.snd_nxt, kAck, 0, 0);
      output(c);
    } else {
      c.state = State::SynRcvd;
      emit(c, c.iss, kSyn | kAck, 0, 0);
    }
    return;
  }

  // §3.10.7.4, первый шаг: приемлемость сегмента.
  uint32_t rw = rcv_wnd(c);
  bool acceptable;
  if (seg_len == 0) {
    acceptable = rw == 0 ? seq == c.rcv_nxt : le(c.rcv_nxt, seq) && lt(seq, c.rcv_nxt + rw);
  } else {
    acceptable = rw != 0 && ((le(c.rcv_nxt, seq) && lt(seq, c.rcv_nxt + rw)) ||
                             (le(c.rcv_nxt, seq + seg_len - 1) && lt(seq + seg_len - 1, c.rcv_nxt + rw)));
  }
  if (!acceptable) {
    if ((flags & kRst) == 0) emit(c, c.snd_nxt, kAck, 0, 0);
    return;
  }
  // Второй: RST.
  if ((flags & kRst) != 0) {
    if (seq != c.rcv_nxt) {  // RFC 5961: только точное попадание сбрасывает
      emit(c, c.snd_nxt, kAck, 0, 0);
      return;
    }
    if (c.state == State::SynRcvd && c.parent != kNoConn && !c.held) {
      c.used = false;  // соединение не дошло до accept
      return;
    }
    close_tcb(c, Status::ConnectionReset);
    return;
  }
  // Четвёртый: SYN в синхронизированном состоянии — RFC 5961: «вызывающий» ACK.
  if ((flags & kSyn) != 0) {
    emit(c, c.snd_nxt, kAck, 0, 0);
    return;
  }
  // Пятый: ACK.
  if ((flags & kAck) == 0) return;
  if (c.state == State::SynRcvd) {
    if (!(lt(c.snd_una, ack) && le(ack, c.snd_nxt))) {
      send_rst(src, dport, sport, ack, 0, false);
      return;
    }
    c.state = State::Established;
    c.snd_una = ack;
    c.snd_wnd = wnd, c.snd_wl1 = seq, c.snd_wl2 = ack;
    c.retx_at = 0;
    c.retries = 0;
    if (c.parent != kNoConn) {
      Tcb& l = g_tcb[c.parent];
      if (l.used && l.state == State::Listen && l.backlog_len < l.backlog_max) {
        c.held = true;  // ждёт accept
        l.backlog[l.backlog_len++] = static_cast<int>(&c - g_tcb);
      } else {
        send_rst(src, dport, sport, c.snd_nxt, 0, false);
        c.used = false;
        return;
      }
    }
  }
  if (lt(c.snd_max, ack)) {  // подтверждение того, что не отправлено
    emit(c, c.snd_nxt, kAck, 0, 0);
    return;
  }
  established_ack(c, ack, wnd, seq, dlen);
  bool fin_acked = c.fin_sent && c.snd_una == c.snd_max;
  if (c.state == State::FinWait1 && fin_acked) c.state = State::FinWait2;
  if (c.state == State::Closing && fin_acked) {
    c.state = State::TimeWait;
    c.time_wait_until = platform::now_ns() + kTimeWait;
  }
  if (c.state == State::LastAck && fin_acked) {
    close_tcb(c, Status::Ok);
    if (!c.held) c.used = false;
    return;
  }
  // Седьмой: данные — только по порядку (вне порядка ждём повтора).
  bool need_ack = false;
  if (dlen != 0 && (c.state == State::Established || c.state == State::FinWait1 ||
                    c.state == State::FinWait2)) {
    if (seq == c.rcv_nxt) {
      uint32_t take = dlen < c.rcv.free() ? dlen : c.rcv.free();
      if (!c.shut_read) c.rcv.push(data, take);
      c.rcv_nxt += take;
      dlen = take;
    } else if (lt(c.rcv_nxt, seq) && !c.shut_read) {
      // Не по порядку: сохранить в окне, если помещается целиком.
      uint32_t ahead = seq - c.rcv_nxt;
      if (ahead + dlen <= c.rcv.free() && c.ooo_count < kOooRanges) {
        c.rcv.put_ahead(ahead, data, dlen);
        c.ooo[c.ooo_count++] = {seq, seq + dlen};
      }
      dlen = 0;  // FIN из такого сегмента не принимаем, пока дыра не закрыта
    } else if (lt(seq, c.rcv_nxt) && lt(c.rcv_nxt, seq + dlen)) {
      uint32_t skip = c.rcv_nxt - seq;
      uint32_t rest = dlen - skip;
      uint32_t take = rest < c.rcv.free() ? rest : c.rcv.free();
      if (!c.shut_read) c.rcv.push(data + skip, take);
      c.rcv_nxt += take;
    }
    need_ack = true;
  }
  // Дыра закрылась: присоединить сохранённые куски.
  for (bool moved = true; moved && c.ooo_count != 0;) {
    moved = false;
    for (uint32_t i = 0; i < c.ooo_count; ++i) {
      Range r = c.ooo[i];
      if (le(r.end, c.rcv_nxt)) {  // уже покрыт
        c.ooo[i] = c.ooo[--c.ooo_count];
        moved = true;
        break;
      }
      if (le(r.begin, c.rcv_nxt)) {
        c.rcv.size += r.end - c.rcv_nxt;
        c.rcv_nxt = r.end;
        c.ooo[i] = c.ooo[--c.ooo_count];
        moved = true;
        break;
      }
    }
  }
  // Восьмой: FIN (только когда все данные до него приняты).
  if ((flags & kFin) != 0 && seq + dlen == c.rcv_nxt && !c.fin_rcvd) {
    c.rcv_nxt += 1;
    c.fin_rcvd = true;
    need_ack = true;
    if (c.state == State::SynRcvd || c.state == State::Established) {
      c.state = State::CloseWait;
    } else if (c.state == State::FinWait1) {
      if (fin_acked) {
        c.state = State::TimeWait;
        c.time_wait_until = platform::now_ns() + kTimeWait;
      } else {
        c.state = State::Closing;
      }
    } else if (c.state == State::FinWait2) {
      c.state = State::TimeWait;
      c.time_wait_until = platform::now_ns() + kTimeWait;
    }
  }
  if (need_ack) emit(c, c.snd_nxt, kAck, 0, 0);
  output(c);
}

Status tcp_listen(uint16_t port, uint32_t backlog, int* out) {
  ensure_timer();
  sync::MutexGuard guard(g_lock);
  if (port == 0 || port_in_use(port)) return Status::AddressInUse;
  int i = alloc_tcb();
  if (i == kNoConn) return Status::NoMemory;
  Tcb& t = g_tcb[i];
  t.state = State::Listen;
  t.lport = port;
  t.held = true;
  t.backlog_max = backlog == 0 ? 1 : backlog > kMaxBacklog ? kMaxBacklog : backlog;
  *out = i;
  return Status::Ok;
}

Status tcp_accept(int l, uint64_t deadline, int* out, uint32_t* peer_ip, uint16_t* peer_port) {
  for (;;) {
    {
      sync::MutexGuard guard(g_lock);
      Tcb& t = g_tcb[l];
      if (!t.used || t.state != State::Listen) return Status::InvalidArgs;
      if (t.backlog_len != 0) {
        int c = t.backlog[0];
        for (uint32_t i = 1; i < t.backlog_len; ++i) t.backlog[i - 1] = t.backlog[i];
        --t.backlog_len;
        g_tcb[c].parent = kNoConn;
        *out = c;
        *peer_ip = g_tcb[c].rip;
        *peer_port = g_tcb[c].rport;
        return Status::Ok;
      }
    }
    if (deadline == 0) return Status::WouldBlock;
    if (sched::interrupted()) return Status::Interrupted;
    if (platform::now_ns() >= deadline) return Status::TimedOut;
    sched::sleep_ns(kMs);
  }
}

Status tcp_connect(uint32_t ip, uint16_t port, uint64_t deadline, int* out) {
  ensure_timer();
  int i;
  {
    sync::MutexGuard guard(g_lock);
    i = alloc_tcb();
    if (i == kNoConn) return Status::NoMemory;
    Tcb& t = g_tcb[i];
    t.held = true;
    do {
      t.lport = g_ephemeral++;
      if (g_ephemeral == 0) g_ephemeral = 49152;
    } while (port_in_use(t.lport));
    t.rip = ip, t.rport = port;
    t.iss = new_iss();
    t.snd_una = t.iss, t.snd_nxt = t.iss + 1, t.snd_max = t.iss + 1;
    t.state = State::SynSent;
  }
  // ARP — до отправки SYN, вне блокировки (поток интерфейса не должен ждать нас).
  (void)resolve_route(ip, deadline < platform::now_ns() + 2000 * kMs ? deadline : platform::now_ns() + 2000 * kMs);
  {
    sync::MutexGuard guard(g_lock);
    Tcb& t = g_tcb[i];
    emit(t, t.iss, kSyn, 0, 0);
    arm(t);
  }
  *out = i;
  for (;;) {
    {
      sync::MutexGuard guard(g_lock);
      Tcb& t = g_tcb[i];
      if (t.state == State::Established) return Status::Ok;
      if (t.state == State::Closed) return t.error != Status::Ok ? t.error : Status::ConnectionRefused;
    }
    if (sched::interrupted()) return Status::Interrupted;
    if (platform::now_ns() >= deadline) return Status::TimedOut;
    sched::sleep_ns(kMs);
  }
}

Status tcp_send(int c, const void* buf, uint32_t len, uint64_t deadline, uint32_t* sent) {
  *sent = 0;
  const auto* p = static_cast<const uint8_t*>(buf);
  for (;;) {
    {
      sync::MutexGuard guard(g_lock);
      Tcb& t = g_tcb[c];
      if (t.state == State::Closed) return t.error != Status::Ok ? t.error : Status::PeerClosed;
      if (t.fin_queued || (t.state != State::Established && t.state != State::CloseWait)) {
        return Status::PeerClosed;
      }
      uint32_t n = len - *sent < t.snd.free() ? len - *sent : t.snd.free();
      if (n != 0) {
        t.snd.push(p + *sent, n);
        *sent += n;
        output(t);
      }
      if (*sent == len) return Status::Ok;
    }
    if (*sent != 0 && deadline == 0) return Status::Ok;
    if (deadline == 0) return Status::WouldBlock;
    if (sched::interrupted()) return *sent != 0 ? Status::Ok : Status::Interrupted;
    if (platform::now_ns() >= deadline) return *sent != 0 ? Status::Ok : Status::TimedOut;
    sched::sleep_ns(kMs);
  }
}

Status tcp_recv(int c, void* buf, uint32_t cap, uint64_t deadline, uint32_t* got) {
  *got = 0;
  for (;;) {
    {
      sync::MutexGuard guard(g_lock);
      Tcb& t = g_tcb[c];
      if (t.rcv.size != 0) {
        uint32_t n = cap < t.rcv.size ? cap : t.rcv.size;
        uint32_t before = rcv_wnd(t);
        t.rcv.peek(0, static_cast<uint8_t*>(buf), n);
        t.rcv.drop(n);
        *got = n;
        // Окно открылось заметно — сообщить (RFC 9293 §3.8.6.2.2, SWS).
        if (before < kMss && rcv_wnd(t) >= kMss && t.state != State::Closed) emit(t, t.snd_nxt, kAck, 0, 0);
        return Status::Ok;
      }
      if (t.fin_rcvd || t.shut_read) return Status::Ok;  // конец потока
      if (t.state == State::Closed) return t.error != Status::Ok ? t.error : Status::Ok;
    }
    if (deadline == 0) return Status::WouldBlock;
    if (sched::interrupted()) return Status::Interrupted;
    if (platform::now_ns() >= deadline) return Status::TimedOut;
    sched::sleep_ns(kMs);
  }
}

Status tcp_shutdown(int c, bool read, bool write) {
  sync::MutexGuard guard(g_lock);
  Tcb& t = g_tcb[c];
  if (read) t.shut_read = true;
  if (write && !t.fin_queued &&
      (t.state == State::Established || t.state == State::CloseWait || t.state == State::SynRcvd)) {
    t.fin_queued = true;
    output(t);
  }
  return Status::Ok;
}

void tcp_release(int c) {
  sync::MutexGuard guard(g_lock);
  Tcb& t = g_tcb[c];
  t.held = false;
  if (t.state == State::Listen || t.state == State::SynSent || t.state == State::Closed) {
    close_tcb(t, Status::Ok);
    t.used = false;
    return;
  }
  if (!t.fin_queued && (t.state == State::Established || t.state == State::CloseWait)) {
    t.fin_queued = true;
    output(t);
  }
  // Дальше соединение доводит таймер (TIME-WAIT, LAST-ACK) и освобождает.
}

bool tcp_readable(int c) {
  sync::MutexGuard guard(g_lock);
  const Tcb& t = g_tcb[c];
  return t.rcv.size != 0 || t.fin_rcvd || t.state == State::Closed || t.backlog_len != 0;
}

bool tcp_writable(int c) {
  sync::MutexGuard guard(g_lock);
  const Tcb& t = g_tcb[c];
  return t.state == State::Closed || (t.snd.free() != 0 && t.state == State::Established);
}

bool tcp_local(int c, uint16_t* port) {
  sync::MutexGuard guard(g_lock);
  *port = g_tcb[c].lport;
  return g_tcb[c].used;
}

}  // namespace kernel::net
