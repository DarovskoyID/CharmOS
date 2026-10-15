// Проверка libc (M8): строки, числа, printf, куча, окружение, файлы и
// каталоги на диске, pthread. Запуск — ktest=libc (корень ФС в роли Root).
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c)                                                  \
  do {                                                            \
    if (!(c)) {                                                   \
      printf("libc: FAILED line %d: %s\n", __LINE__, #c);         \
      exit(__LINE__);                                             \
    }                                                             \
  } while (0)

namespace {

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
long g_counter = 0;
int g_ready = 0;

void* worker(void* arg) {
  long id = reinterpret_cast<long>(arg);
  for (int i = 0; i < 5000; ++i) {
    pthread_mutex_lock(&g_mutex);
    ++g_counter;
    pthread_mutex_unlock(&g_mutex);
  }
  pthread_mutex_lock(&g_mutex);
  ++g_ready;
  pthread_cond_broadcast(&g_cond);
  pthread_mutex_unlock(&g_mutex);
  errno = static_cast<int>(id);  // errno — у каждого потока свой
  return reinterpret_cast<void*>(id * 10);
}

}  // namespace

extern "C" int main(int argc, char** argv) {
  CHECK(argc == 2 && strcmp(argv[0], "libc_test") == 0 && strcmp(argv[1], "arg1") == 0);
  CHECK(strcmp(getenv("HOME"), "/home") == 0 && getenv("NOPE") == nullptr);
  CHECK(setenv("X", "1", 0) == 0 && strcmp(getenv("X"), "1") == 0);
  CHECK(setenv("X", "2", 1) == 0 && strcmp(getenv("X"), "2") == 0);

  // Строки и числа.
  char buf[128];
  CHECK(strlen("hello") == 5 && strcmp("a", "b") < 0 && strncmp("abc", "abd", 2) == 0);
  CHECK(strstr("hello world", "wor") != nullptr && strchr("abc", 'c') != nullptr && strrchr("a/b/c", '/')[1] == 'c');
  CHECK(strtol("-42", nullptr, 10) == -42 && strtoul("0x1f", nullptr, 0) == 31 && atoi("  17x") == 17);
  snprintf(buf, sizeof buf, "%d|%5s|%-3d|%04x|%lu|%c|%%|%.2f|%p", -7, "ab", 5, 255, 1234567890123ul, 'z', 3.14159,
           reinterpret_cast<void*>(0x10));
  CHECK(strcmp(buf, "-7|   ab|5  |00ff|1234567890123|z|%|3.14|0x10") == 0);
  CHECK(snprintf(buf, 4, "%s", "abcdef") == 6 && strcmp(buf, "abc") == 0);
  char tok[] = "a b  c";
  char* save;
  CHECK(strcmp(strtok_r(tok, " ", &save), "a") == 0 && strcmp(strtok_r(nullptr, " ", &save), "b") == 0);

  // Куча: много мелких и крупные блоки.
  void* ptrs[500];
  for (int i = 0; i < 500; ++i) {
    ptrs[i] = malloc(static_cast<size_t>(i * 37 + 1));
    CHECK(ptrs[i] != nullptr);
    memset(ptrs[i], i & 0xff, static_cast<size_t>(i * 37 + 1));
  }
  for (int i = 0; i < 500; ++i) {
    CHECK(static_cast<unsigned char*>(ptrs[i])[i * 37] == (i & 0xff));
    free(ptrs[i]);
  }
  char* big = static_cast<char*>(malloc(3 << 20));
  CHECK(big != nullptr);
  big[(3 << 20) - 1] = 1;
  big = static_cast<char*>(realloc(big, 4 << 20));
  CHECK(big != nullptr && big[(3 << 20) - 1] == 1);
  free(big);
  printf("libc: strings, printf and heap ok\n");

  // Файлы и каталоги.
  CHECK(mkdir("/tmp_libc", 0755) == 0);
  CHECK(chdir("/tmp_libc") == 0);
  CHECK(getcwd(buf, sizeof buf) != nullptr && strcmp(buf, "/tmp_libc") == 0);
  FILE* f = fopen("note.txt", "w");
  CHECK(f != nullptr);
  CHECK(fprintf(f, "line %d\n", 1) == 7);
  fputs("line 2\n", f);
  CHECK(fclose(f) == 0);
  f = fopen("/tmp_libc/../tmp_libc/note.txt", "a");
  CHECK(f != nullptr && fputs("line 3\n", f) == 0 && fclose(f) == 0);
  f = fopen("note.txt", "r");
  CHECK(f != nullptr);
  CHECK(fgets(buf, sizeof buf, f) != nullptr && strcmp(buf, "line 1\n") == 0);
  CHECK(fgets(buf, sizeof buf, f) != nullptr && fgets(buf, sizeof buf, f) != nullptr && strcmp(buf, "line 3\n") == 0);
  CHECK(fgets(buf, sizeof buf, f) == nullptr && feof(f));
  fclose(f);
  struct stat st;
  CHECK(stat("note.txt", &st) == 0 && st.st_size == 21 && S_ISREG(st.st_mode));
  CHECK(stat("/tmp_libc", &st) == 0 && S_ISDIR(st.st_mode));
  CHECK(open("missing", O_RDONLY) < 0 && errno == ENOENT);
  int fd = open("note.txt", O_RDWR);
  CHECK(fd >= 0 && lseek(fd, 5, SEEK_SET) == 5 && read(fd, buf, 1) == 1 && buf[0] == '1');
  close(fd);
  DIR* d = opendir("/tmp_libc");
  CHECK(d != nullptr);
  int seen = 0;
  while (struct dirent* e = readdir(d)) seen += strcmp(e->d_name, "note.txt") == 0 && e->d_type == DT_REG;
  closedir(d);
  CHECK(seen == 1);
  CHECK(unlink("note.txt") == 0 && chdir("/") == 0 && rmdir("/tmp_libc") == 0);
  printf("libc: files and directories ok\n");

  // Потоки.
  pthread_t t[4];
  errno = 0;
  for (long i = 0; i < 4; ++i) CHECK(pthread_create(&t[i], nullptr, worker, reinterpret_cast<void*>(i + 1)) == 0);
  pthread_mutex_lock(&g_mutex);
  while (g_ready < 4) pthread_cond_wait(&g_cond, &g_mutex);
  pthread_mutex_unlock(&g_mutex);
  for (long i = 0; i < 4; ++i) {
    void* ret;
    CHECK(pthread_join(t[i], &ret) == 0 && reinterpret_cast<long>(ret) == (i + 1) * 10);
  }
  CHECK(g_counter == 20000 && errno == 0);
  printf("libc: pthread ok\n");
  return 0;
}
