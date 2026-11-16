# Мастер-промпт дизайна «Пульс»

Промпт рассчитан на генерацию новых экранов и аккуратное редактирование
существующих макетов. Перед использованием заменить блок `<ЭКРАН>` описанием
нужной страницы.

```text
Use case: ui-mockup
Asset type: high-fidelity native desktop browser interface for CharmOS

Primary request:
Create a production-quality design mockup for the native CharmOS browser named
«Пульс». This is a design concept only, not an implementation.

<ЭКРАН>

Visual language:
- Calm industrial minimalism native to CharmOS.
- Near-black desktop and surfaces: #0B0B0C, #121214, #181819.
- Warm-white primary text #F1EEE7 and muted gray text #918C83.
- Amber-yellow accent #FFD31A and restrained red #F23A3A only for danger.
- Thin warm-gray borders #3A3424, crisp one-pixel dividers.
- Space Grotesk-like geometric typography; JetBrains Mono only for genuinely
  technical values, never as decorative pseudo-technical text.
- Flat, precise geometry with restrained depth. No gradients, glassmorphism,
  glossy effects, excessive glow, neon cyberpunk, blue-purple styling or HUD.
- No decorative codes, coordinates, corner brackets, measuring ticks, slash
  marks or ornamental lines around headings.

Browser identity:
- The «Пульс» product mark is a ringed planet constructed from a small set of
  crisp triangular facets, in amber-yellow and warm orange.
- The planet is a brand mark. Never use it as the search-engine indicator.
- The operating-system panel may use the same triangular planet next to CHARM.

Browser toolbar:
- Exact order: back arrow, forward arrow, search-engine indicator, reload,
  address field.
- The search-engine indicator is the authentic small multicolor Google G or
  the red Yandex Я, depending on the selected engine.
- The search-engine indicator has no arrow, chevron or dropdown menu.
- Search-engine selection exists only in Settings → Search.
- To the right of the address field: bookmark, downloads, account and menu.

Window controls:
- Use a coherent original CharmOS glyph family instead of Windows — □ × and
  instead of macOS colored circles.
- Minimize: a short horizontal stroke bending downward at the center into a
  small V-notch.
- Full screen: four open corner chevrons around an empty diamond-shaped center.
- Close: two angular door leaves pointing inward, visually like ›‹ with a small
  central gap. On hover it may receive a restrained red accent.
- Each control has an invisible 36×36 pointer target and never changes size on
  hover. Tab-close X icons are separate and remain conventional.

New-tab page behavior:
- Preserve the subtle black background with thin amber orbital lines.
- Do not add coordinate labels, measuring ticks or space-themed annotations.
- Show the triangular ringed-planet mark, title «Пульс», central search field,
  editable shortcut cards and a dashed «Добавить» card.
- Include «Настроить фон» and a compact protection status.
- Users can select a built-in background or their own file.

Internal pages:
- Use a stable left navigation rail and a spacious main content panel.
- Headings such as «Основные», «Ваш аккаунт» and «Персонализация» stand cleanly
  with whitespace and no decoration above or around them.
- Prefer plain rows and functional separators over excessive floating cards.
- Cards may be used only for real grouped objects such as devices, profile
  summaries or new-tab shortcuts.

Composition:
- Full 16:10 CharmOS desktop screenshot, straight-on orthographic view.
- Browser fills most of the screen beneath the system panel.
- Realistic desktop proportions, aligned spacing and legible Russian labels.
- Preserve consistency with the supplied reference mockup.

Constraints:
- No Chrome or Firefox visual clone.
- No Windows 11 or macOS window styling.
- No decorative UI noise and no watermark.
- Do not replace Google/Yandex marks with the «Пульс» planet.
- Render all specified Russian interface text exactly.
```

## Блоки экранов

### Новая вкладка

```text
<ЭКРАН>: Design the «Новая вкладка» page. Show shortcut cards «Документы»,
«Новости», «Почта», «Настройки» and «Добавить». Include the central search field,
protection status and background customization action.
```

### Основные настройки

```text
<ЭКРАН>: Design pulse://settings with left navigation «Основные», «Внешний вид»,
«Поиск», «Приватность», «Загрузки», «Языки», «Система», «О браузере». Main rows:
«При запуске», «Поисковая система», «Карточки быстрого доступа» and
«Браузер по умолчанию».
```

### Аккаунт

```text
<ЭКРАН>: Design pulse://account with profile, synchronization, security,
devices and passwords. Use heading «Ваш аккаунт», summaries «Синхронизация»,
«Устройства», «Пароли» and security rows «Двухэтапная проверка» and
«Последняя активность».
```

### Персонализация

```text
<ЭКРАН>: Design pulse://settings/appearance. Include a live new-tab preview,
background choices, accent colors, card visibility, interface density and a
reorderable card list with «Добавить карточку», «Сбросить» and «Готово».
```

