# Статический C++ runtime для браузера (v1)

Цель: x86-64 ELF, один статический образ процесса, C++ без exceptions/RTTI.
Это подмножество Itanium C++ ABI, не обещание динамической загрузки DSO.

После установки FS, startup handles и stdio libc вызывает preinit_array,
затем init_array в порядке linker priority, затем main. exit сначала
выполняет общий LIFO реестр atexit/__cxa_atexit, затем fini_array в обратном
порядке и flush stdio. _Exit/abort не выполняют пользовательские callbacks.
fini_array запускается максимум один раз, включая повторный вход из callback.
Реестр до 4096 записей, регистрация сверх лимита возвращает -1.
__cxa_finalize(dso) вызывает только подходящие callbacks, nullptr — все;
запись снимается под lock до вызова, повторный finalize безопасен.

8-byte guard статической инициализации: первый байт completion, u32 по
offset 4 — futex pending. acquire возвращает 1 единственному initializer,
остальные ждут; release публикует completion и будит всех, abort разрешает
повтор. Рекурсивная инициализация того же static не поддерживается.

operator new/new[] и delete/delete[] используют собственную malloc/free.
Нулевой размер выделяет минимум один байт. При OOM throwing формы abort
(сборка без exceptions), nothrow формы возвращают nullptr. Aligned формы
проверяют power-of-two alignment и переполнение, хранят исходный allocation
перед выровненным указателем; освобождать его нужно matching aligned delete.
Поддерживаются sized delete. Dynamic loader не реализуется.
Дополнение v1.1: собственные std::set_new_handler/get_new_handler атомарны;
при OOM все формы new вызывают текущий callback и повторяют allocation.
Если handler отсутствует, throwing формы abort, nothrow — nullptr.
Handler вызывается без heap lock; исключения из callback не поддерживаются.
std::nothrow и new-handler принадлежат своей libc; дубли upstream исключены.
pure/deleted virtual вызов завершает процесс через abort.

## GN bootstrap profile (2026-10-17)

Для Chromium target_os=charmos, target_cpu=x64, __charmos__ и IS_CHARMOS /
IS_POSIX. IS_LINUX/IS_ANDROID выключены. Компилятор Generic ELF, собственные
C-заголовки, bundled libc++ с namespace __Cr. Ядро/loader не меняют ABI.
Для upstream runtime выбирается штатный no-exception профиль libc++abi,
исключаются exception storage/personality и дубли собственных guard/virtual
handlers. RTTI разрешён внутри upstream ABI runtime; приложения остаются
без RTTI/исключений. Compiler TLS, dynamic/interpreter segments/sections и
ET_DYN отвергаются post-link проверкой, а не загружаются как будто поддержаны.

platform_smoke использует только два upstream units (algorithm, verbose_abort)
и native libc. Его vector/sort/ownership/atomics тест не является проверкой
полного libc++, std::locale, filesystem, Chromium base или браузерного окна.
full_runtime — отдельная обычная executable target со штатными зависимостями
Chromium: недостающие locale/math/fenv интерфейсы остаются ошибками сборки.

Ссылки на контракт upstream:
[Itanium C++ ABI](https://itanium-cxx-abi.github.io/cxx-abi/abi.html).
