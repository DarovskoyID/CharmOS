#ifndef _CHARM_LIBC_H
#define _CHARM_LIBC_H
/* Расширения libc CharmOS: хендлы ядра под POSIX-интерфейсом. Запуск
   программ — только явный (docs/abi/process.md §1): ребёнок получает то,
   что ему передали. */
#include <stdint.h>
#include <sys/types.h>
/* Что передать ребёнку (флаги charm_spawn). */
#define CHARM_GIVE_ROOT 1u     /* корень ФС */
#define CHARM_GIVE_CONSOLE 2u  /* консоль (stdin/stdout/stderr) */
#define CHARM_GIVE_NET 4u      /* сеть */
#define CHARM_GIVE_LOG 8u      /* журнал */
#define CHARM_GIVE_JOB 16u     /* Job для запуска своих детей */
#define CHARM_GIVE_CWD 32u     /* текущий каталог (как строку окружения PWD) */
#define CHARM_GIVE_WM 64u      /* дверь компоновщика (wm.md §1) */
#define CHARM_GIVE_AUDIO 128u /* вывод PCM (audio.md) */
#define CHARM_GIVE_ALL 255u
struct charm_spawn_opts {
  uint32_t give;       /* CHARM_GIVE_* */
  uint32_t job;        /* хендл Job; 0 — свой */
  uint32_t priority;   /* 0 — как у вызывающего */
  uint32_t extra_count;
  const uint32_t* extra;        /* дополнительные хендлы (перемещаются) */
  const uint32_t* extra_roles;
};
#ifdef __cplusplus
extern "C" {
#endif
/* Запустить программу path с argv и envp (NULL — environ). Возвращает
   хендл Process или -1 (errno). */
int64_t charm_spawn(const char* path, char* const argv[], char* const envp[], const struct charm_spawn_opts* o);
/* Ждать завершения процесса (сигналы ожидание не прерывают); код — в *code. */
int charm_wait(uint32_t proc, int64_t* code);
int charm_kill(uint32_t proc);
/* Хендлы, полученные при запуске (0 — нет). */
uint32_t charm_root(void);
uint32_t charm_console(void);
uint32_t charm_net(void);
uint32_t charm_log(void);
uint32_t charm_job(void);
uint32_t charm_system(void); /* закрытая область (только pkgd) */
uint32_t charm_power(void);  /* питание машины (только init, power.md) */
uint32_t charm_display(void);  /* экран (init и компоновщик, display.md) */
uint32_t charm_input(void);    /* ввод (init и компоновщик, input.md) */
uint32_t charm_wm(void);       /* дверь компоновщика (wm.md §1) */
uint32_t charm_audio(void);    /* доступ к PCM (audio.md) */
/* i-й хендл стартового сообщения без роли (0 — нет). */
uint32_t charm_extra_handle(int i);
/* Забрать хендл прикладной роли >=0x10000; владение передаётся один раз. */
uint32_t charm_take_start_handle(uint32_t role);
/* Сумма ёмкостей живых блоков libc, с заголовками и округлением. */
size_t charm_heap_allocated_bytes(void);
/* Число открытых FD вызывающего процесса (включая FD выше soft limit). */
int charm_open_fd_count(void);
/* FD только для poll канала; interests: Read=1, Write=2. Исходный хендл заёмный. */
int charm_channel_watch_fd(uint32_t endpoint,uint32_t interests);
/* Хендл ядра за файловым дескриптором (0 — нет). */
uint32_t charm_fd_handle(int fd);
/* Импорт файла/каталога/сокета: владение переходит только при успехе. */
#define CHARM_FD_FILE 1
#define CHARM_FD_DIRECTORY 2
#define CHARM_FD_SOCKET 3
int charm_fd_from_handle(uint32_t h, int kind);
/* Сообщение в системный журнал (служба logd); уровень 0..7. */
int charm_log_write(uint32_t level, const char* text);
#ifdef __cplusplus
}
#endif
#endif
