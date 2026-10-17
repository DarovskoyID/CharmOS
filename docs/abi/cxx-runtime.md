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
Поддерживаются sized delete. new_handler и dynamic loader не реализуются.
pure/deleted virtual вызов завершает процесс через abort.

Ссылки на контракт upstream:
[Itanium C++ ABI](https://itanium-cxx-abi.github.io/cxx-abi/abi.html).
