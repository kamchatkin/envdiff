# envdiff

[English version](README.md)

Небольшая кроссплатформенная утилита на C для добавления в рабочий `.env`
отсутствующих ключей и новых комментариев из `.env.example`. Поддерживаются
Linux, macOS и Windows.

Существующие значения не извлекаются, не сравниваются и не изменяются. Если ключ
уже существует, его значение всегда остаётся прежним.

## Сборка

Требуются CMake 3.20 или новее и компилятор C11: GCC/Clang на Linux, Apple Clang
на macOS или MSVC на Windows.

Linux и macOS:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows, PowerShell из Visual Studio Build Tools:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Windows-бинарник будет находиться в `build/Release/envdiff.exe`, Unix-бинарник —
в `build/envdiff`.

На Linux и macOS также можно использовать Makefile:

```bash
make
make check
```

Makefile выводит размер результата только для информации. Ограничения, при
котором сборка завершается ошибкой из-за размера, нет.

Установка в `~/.local/bin`:

```bash
make install
```

Каталог `~/.local/bin` должен присутствовать в `PATH`.

## Автоматические сборки

GitHub Actions собирает и тестирует программу на Linux, macOS и Windows. Архивы
с бинарниками доступны в артефактах каждого запуска CI.

При отправке тега вида `v0.1.0` workflow собирает архивы для всех трёх платформ
и публикует их в GitHub Releases.

## Использование

```bash
envdiff .env.gateway.example .env.gateway
```

Проверка без изменения файла:

```bash
envdiff --check .env.gateway.example .env.gateway
```

`--check` возвращает код `1`, если найдены новые ключи или комментарии, и `0`,
если рабочий файл уже актуален.

Версия программы:

```bash
envdiff --version
```

Утилита не заменит существующий `NATS_URL=nats://...` на
`NATS_URL=tls://...`: значение существующего ключа нужно менять вручную.

## Лицензия

[MIT](LICENSE)
