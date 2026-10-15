# Ключи

`test-store.key` / `test-store.pub` — **ТЕСТОВЫЙ** ключ магазина для CI и
`tests/qemu/m8.5`. Его секрет открыт (лежит в репозитории), поэтому пакеты,
подписанные им, в настоящей сборке доверенными быть не должны.

Боевой ключ автор создаёт у себя и хранит вне репозитория:

```bash
build/host/tools/sign/sign keygen ~/charmos-store
build/host/tools/sign/sign pubkey-header ~/charmos-store.pub user/bin/pkgd/store_key.hpp
```

Второй шаг вшивает в ОС открытый ключ (`kStorePublicKey` в pkgd); после
этого тестовые пакеты в сборке не пройдут проверку — для тестов заголовок
возвращается командой с `tools/keys/test-store.pub`.
