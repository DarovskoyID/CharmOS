# Дополнение libc: потоки для TLS (2026-10-17)

Статическая libc, без изменения syscall ABI. Дополнение v2 меняет layout
mutex/rwlock; все приложения пересобираются вместе с libc.
Нужны реальному crypto/thread_pthread.cc из закреплённого BoringSSL.

pthread_mutex_t v2: u32 state, int type, uintptr_t owner, u32 depth,
размер 24 (8-byte alignment). NORMAL=0, RECURSIVE=1. Атрибуты init/settype/
destroy; неизвестный type — EINVAL. Recursive owner может повторно lock/
trylock до UINT32_MAX (затем EAGAIN); чужой unlock — EPERM.
pthread_rwlock_t v2: gate mutex, condition, u32 readers, u32 writer, размер 40.
Статический initializer нулевой. Несколько читателей допускаются совместно;
писатель исключает остальных. Рекурсивное чтение допустимо. Приоритет новых
читателей при ожидающем писателе: нет гарантии ограниченного ожидания писателя.
try-вызовы не ждут, занятое состояние даёт EBUSY. Переполнение читателей —
EAGAIN. destroy занятого объекта — EBUSY. Чужой unlock/копирование активного
объекта не поддерживаются. Атрибуты только nullptr; process-shared нет.

pthread_key_t: u32 (7 бит slot + 25 бит generation), 128 активных ключей.
Повторное выделение не раскрывает старое значение ни в одном потоке.
После исчерпания generation slot больше не переиспользуется. Начальное
значение nullptr. delete не вызывает destructor; невалидный key — EINVAL,
getspecific для него возвращает nullptr. Ключи не меняют errno.
Возврат из entry и pthread_exit выполняют до четырёх проходов деструкторов,
сбросив значение до вызова. Callback вызывается без глобального mutex.
Process exit не обещает cleanup чужих живых потоков, отмены pthread пока нет.

sched_yield передаёт исполнение планировщику. pthread_cond_timedwait использует
абсолютный CLOCK_REALTIME deadline, переводимый в monotonic при входе;
изменение RTC во время ожидания не отслеживается. Неверный timespec — EINVAL,
поддерживаются только неотрицательные seconds и представимые u64 nanoseconds;
истечение — ETIMEDOUT; mutex вновь захвачен перед возвратом. Spurious wakeups
допустимы, caller обязан проверять predicate. Recursive mutex с depth > 1
для condition wait не поддерживается.

Источники семантики:
[TSD](https://man7.org/linux/man-pages/man3/pthread_key_create.3.html),
[delete](https://man7.org/linux/man-pages/man3/pthread_key_delete.3p.html),
[rdlock](https://man7.org/linux/man-pages/man3/pthread_rwlock_rdlock.3p.html),
[unlock](https://man7.org/linux/man-pages/man3/pthread_rwlock_unlock.3p.html).
Исходный код libc собственный; полное соответствие POSIX не заявляется.

## Static libc v3: native browser threads

All native programs must be rebuilt; no kernel thread ABI is changed.
pthread_attr_t is 16 bytes (size_t stack_size, int detach_state, padding).
Defaults: 256 KiB, joinable. Explicit sizes from PTHREAD_STACK_MIN=16 KiB
through the 1 GiB VMO limit are supported, rounded to pages at allocation.
Attributes can select joinable/detached state. Creation leaves output unchanged
on failure. Children wait at a startup gate until the parent publishes their
native Thread handle and pthread_t.

Detached threads are reaped after the real ThreadExited packet, releasing the
native handle, stack VMO and TCB. One bounded, process-lifetime reaper thread
and port are created lazily. Registration/reclamation are serialized so a
fast exiting or self-detaching child cannot reclaim a TCB its creator is
still publishing. Native port capacity can make detach fail EAGAIN; no
successful detach silently abandons stack ownership. The bootstrap main
thread has no owned Thread capability and detach returns ENOTSUP for it.
Join claims a joinable thread once, rejects self-join with EDEADLK and does
not free a running thread on a failed wait registration.

pthread_cond_t v3 is 8 bytes: u32 sequence + clockid_t clock. Static/default
conditions use CLOCK_REALTIME. condattr init/getclock/setclock/destroy support
REALTIME and MONOTONIC; other clocks are EINVAL. Monotonic timed waits use the
absolute native deadline directly. Realtime waits retain the documented UTC
translation on entry. Mutex/rwlock/TSD ownership and error codes are unchanged.
pthread_equal compares opaque pthread identities, without converting them to
kernel TIDs. Priority inheritance is unavailable on CharmOS; Chromium must
choose its non-inheritance lock implementation.
