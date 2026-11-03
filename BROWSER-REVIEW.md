# Нативный Chromium: изменения для ревью

Этот файл ведётся по ходу завершения нативного браузера CharmOS. Здесь фиксируются рабочие пакеты, архитектурные решения, проверки и коммиты.

## Исходная точка

- Последний существующий коммит: `9b836c7` (`M10: C++-only Chromium runtime, RuntimeQuery ABI 1.13 and libc fd v2`).
- Ветка `main` на этом коммите отправлена в новый remote `bottomweb`: `ssh://git@git.bottomweb.ru:2222/Darovskoy/CharmOS.git`.
- До начала этого продолжения рабочее дерево уже содержало большой незакоммиченный пакет M10: WM v2/v3, libc/SDK, Chromium base/runtime, Skia и native Ozone canvas. Он сохраняется и проверяется как исходный пакет, а не смешивается незаметно с новыми изменениями.
- По сохранённым логам исходного пакета пройдены `m10/window_managed`, `m10/canvas_runtime`, `m10/window_pixels`, `m10/graphics_runtime`, `browser/runtime_cpp`, `browser/base_runtime` и `m10/windows`.

## Журнал новых изменений

### Настройка продолжения

- Добавлен remote `bottomweb`; существующая история `main` отправлена на него.
- Создан этот файл ревью.

## Проверки новых изменений

### Проверка исходного пакета

- Полная сборка CharmOS: успешно; лог `build/browser-completion-baseline.log`.
- Host: **31/31 PASS**.
- QEMU: `m10/window_managed`, `m10/canvas_runtime`, `m10/window_pixels`, `m10/graphics_runtime`, `browser/runtime_cpp`, `browser/base_runtime`, `m10/windows` — **7/7 PASS**; лог `build/browser-completion-baseline-qemu.log`.

## Коммиты

- `1158560` — создан файл ревью, добавлен remote `bottomweb`, существующая история отправлена на оба сервера.
- Следующий коммит отдельно фиксирует проверенный унаследованный пакет WM v3, runtime и native canvas как baseline продолжения.
