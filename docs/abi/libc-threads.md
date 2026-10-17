# Дополнение libc: потоки для TLS (2026-10-17)

Новые C-интерфейсы, без изменения syscall ABI и существующих pthread типов.
Нужны реальному crypto/thread_pthread.cc из закреплённого BoringSSL.

pthread_rwlock_t: gate mutex, condition, u32 readers, u32 writer, размер 16.
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

Источники семантики:
[TSD](https://man7.org/linux/man-pages/man3/pthread_key_create.3.html),
[delete](https://man7.org/linux/man-pages/man3/pthread_key_delete.3p.html),
[rdlock](https://man7.org/linux/man-pages/man3/pthread_rwlock_rdlock.3p.html),
[unlock](https://man7.org/linux/man-pages/man3/pthread_rwlock_unlock.3p.html).
Исходный код libc собственный; полное соответствие POSIX не заявляется.
