// Плавающая точка с переключениями: два экземпляра на одном CPU. Если ядро
// не сохраняет состояние SSE/x87, суммы перемешаются.
#include <charm/sys.hpp>

extern "C" int64_t charm_main() {
  // Разные экземпляры считают разное, чтобы порча была заметна.
  double scale = static_cast<double>(charm::now_ns() % 7 + 1) * 0.25;
  double acc = 0.0;
  constexpr int kN = 4000;
  for (int i = 1; i <= kN; ++i) {
    acc += static_cast<double>(i) * scale;
    if (i % 25 == 0) charm::yield();
  }
  double want = scale * (static_cast<double>(kN) * (kN + 1) / 2.0);
  if (acc != want) {
    charm::print("fpu: MISMATCH\n");
    return 1;
  }
  charm::print("fpu: sum ok\n");
  return 0;
}
