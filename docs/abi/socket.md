# ABI: сеть и сокеты (syscall ABI v1.4)

**Статус: ПРОЕКТ (2026-10-14), не согласован.** Минорное расширение v1.3.
Основа — решения автора от 2026-10-14: доступ к сети — хендл `Net`;
сокеты — свои вызовы, близкие к BSD; IPv4 (IPv6 — после M9).

## 1. Объекты

| Объект | Что это | Откуда |
|---|---|---|
| `Net` | право пользоваться сетью | стартовое сообщение `"net"` (как `"root"` в fs.md §2) |
| `Socket` | UDP-сокет, TCP-слушатель или TCP-соединение | `socket_open`, `socket_accept` |

Права `Net` (сужаются через `handle_duplicate`):

| Бит | Имя | Разрешает |
|---|---|---|
| 0–4 | общие (`Duplicate`, `Transfer`, `Read`, `Write`, `Map`) | как в ipc.md |
| 8 | `NetConnect` | исходящие TCP-соединения и UDP-отправку |
| 9 | `NetListen` | слушать TCP / принимать UDP на порту ≥ 1024 |
| 10 | `NetListenLow` | то же для портов < 1024 |
| 11 | `NetResolve` | `dns_resolve` |

## 2. Адрес

`SockAddr` (8 байт): `uint32_t ipv4` (порядок байт сети), `uint16_t port`
(порядок хоста), `uint16_t reserved` = 0.

## 3. Вызовы v1.4

| № | Имя | Аргументы | `RDX` при успехе |
|---|---|---|---|
| 32 | `socket_open` | `net, type` (1 — TCP, 2 — UDP) | хендл `Socket` |
| 33 | `socket_bind` | `sock, const SockAddr*` | — |
| 34 | `socket_listen` | `sock, backlog` (1..128) | — |
| 35 | `socket_accept` | `sock, SockAddr* peer, deadline` | хендл нового `Socket` |
| 36 | `socket_connect` | `sock, const SockAddr*, deadline` | — |
| 37 | `socket_send` | `sock, const void* buf, len, const SockAddr* to` | отправлено байт |
| 38 | `socket_recv` | `sock, void* buf, cap, SockAddr* from, deadline` | получено байт |
| 39 | `socket_shutdown` | `sock, how` (1 — чтение, 2 — запись, 3 — оба) | — |
| 40 | `net_info` | `net, NetInfo* out` | — |
| 41 | `dns_resolve` | `net, const char* name, len, deadline` | IPv4 (порядок сети) |

- `to` и `from` — только для UDP (у TCP — `nullptr`). `bind` без вызова —
  временный порт 49152–65535.
- TCP `send` возвращает, когда байты приняты в буфер отправки (не
  доставлены); при полном буфере ждёт. `recv` = 0 — собеседник закрыл
  передачу. UDP `send` — одна датаграмма до 1472 байт; `recv` — одна
  датаграмма (лишнее отбрасывается, `RDX` — её полная длина).
- Сроки — абсолютные, как везде (`kInfiniteDeadline` — без срока, 0 — не
  ждать: `WouldBlock`).
- Готовность без блокировки — через порт: `object_bind(port, sock, key)`;
  пакеты `kPacketSocketReadable = 4` (есть данные, входящее соединение или
  конец), `kPacketSocketWritable = 5`.
- Закрытие последнего хендла — `close` в смысле BSD (TCP: FIN, затем
  ядро доводит закрытие само).

### 3.1. `NetInfo` (32 байта)

`uint32_t ipv4, netmask, gateway, dns` (порядок сети; 0 — нет адреса,
DHCP ещё не ответил), `uint8_t mac[6]`, `uint16_t mtu`, `uint32_t flags`
(бит 0 — линия есть), `uint32_t reserved`.

## 4. Новые коды статуса

| Код | Имя | Когда |
|---|---|---|
| 18 | `ConnectionRefused` | RST в ответ на SYN; ICMP «порт недоступен» для UDP |
| 19 | `ConnectionReset` | соединение сброшено |
| 20 | `NetUnreachable` | нет адреса, маршрута или линии |
| 21 | `AddressInUse` | порт занят |

## 5. Что не входит в v1.4

IPv6, raw-сокеты, multicast, опции сокетов (кроме значений по умолчанию:
Nagle включён, keepalive выключен), несколько интерфейсов с маршрутизацией
между ними, TLS (это библиотека в юзерспейсе, не ядро).
