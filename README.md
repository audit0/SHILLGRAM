# SHILLGRAM

Неофициальный клиент Telegram для macOS со встроенным SHILLVPN.
Основан на [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) и [Telegram Desktop](https://github.com/telegramdesktop/tdesktop), работает через официальный Telegram API. Это не официальное приложение Telegram.

Канал: [@SHILGRAM](https://t.me/SHILGRAM) · поддержка: [@shillsupport](https://t.me/shillsupport)

## Скачать

**macOS 12 и новее, Apple Silicon (M1 и новее):** [последний релиз](../../releases/latest), файл `SHILLGRAM-…-macOS-arm64.zip`.

1. Распакуйте архив и перенесите `SHILLGRAM.app` в «Программы».
2. Сборка не нотаризована Apple, поэтому при первом запуске macOS спросит подтверждение. Откройте приложение, затем «Системные настройки» → «Конфиденциальность и безопасность» → «Всё равно открыть». На macOS 14 и старше можно просто нажать на приложение правой кнопкой → «Открыть».
3. Контрольная сумма SHA-256 лежит рядом с архивом в релизе.

Windows, Android и iOS в работе. Первыми о них узнают подписчики канала.

## Что внутри

- **SHILLVPN встроен в приложение.** Telegram идёт через защищённое соединение ещё до входа в аккаунт. При первом запуске 3 дня бесплатно (одна проба на устройство), дальше подписка SHILLVPN. Одна подписка работает и в SHILLGRAM, и на телефоне, и на компьютере.
- **Закрепы без лимита.** Первые закрепы хранит Telegram, всё сверх его лимита — это устройство.
- **Панель ⌘K**: чаты, действия, настройки и шаблоны ответов в одном поиске. Открывается сочетанием ⌘K или жестом «потянуть вниз» двумя пальцами.
- **Шаблоны ответов** и **отправка «через час» / «завтра в 9:00»** в один клик.
- **Агенты ИИ**: Claude Code и Codex подключаются к вашему Telegram в один клик через локальный MCP-сервер. Писать они могут только в «Избранное» и в чаты, которые вы сами разрешили.
- **Тёмная и светлая темы** в стиле SHILLVPN, стекло macOS.
- Возможности AyuGram: история сообщений, переводчик, фильтры, режим стримера и другие.

## Как зарабатывает проект

SHILLGRAM бесплатный, все функции клиента бесплатные. Проект зарабатывает на подписке SHILLVPN: после трёх бесплатных дней встроенный VPN работает по подписке, её можно продлить прямо в приложении. Ещё есть партнёрская программа SHILLVPN. Рекламы в чатах нет, данные пользователей не продаются и не собираются.

## Приватность

- Переписка идёт напрямую между приложением и серверами Telegram.
- Ссылка подписки SHILLVPN и ключи хранятся в Связке ключей macOS.
- Для пробного периода приложение отправляет на сайт не идентификатор устройства, а его хэш.
- Мост агентов ИИ слушает только `127.0.0.1` и пускает только с токеном из Связки ключей.

## Сборка из исходников

Сборка такая же, как у Telegram Desktop: [docs/building-mac.md](docs/building-mac.md). Отличия:

- нужны свои `api_id` и `api_hash` с [my.telegram.org](https://my.telegram.org), они передаются при сборке: `-DTDESKTOP_API_ID=… -DTDESKTOP_API_HASH=…`. В исходниках ключей нет;
- ядро VPN — [Xray-core](https://github.com/XTLS/Xray-core) 26.9.9 или новее. Положите бинарник в `../Libraries/xray/xray` рядом с папкой исходников или укажите путь: `-DSHILLGRAMM_VPN_CORE=/path/to/xray`.

## Лицензия

Код распространяется под [GPLv3](LICENSE) с исключением для OpenSSL ([LEGAL](LEGAL)), как Telegram Desktop и AyuGram. Xray-core распространяется под [MPL-2.0](https://github.com/XTLS/Xray-core/blob/main/LICENSE).

Название SHILLGRAM и значок под лицензию не входят. Сборки из этих исходников с другими ключами API не должны называться SHILLGRAM.

Исходный README AyuGram: [docs/upstream/README-AyuGram.md](docs/upstream/README-AyuGram.md).

---

## English

**SHILLGRAM** is an unofficial Telegram client for macOS with SHILLVPN built in, based on AyuGram Desktop and Telegram Desktop. It uses the official Telegram API and is not affiliated with Telegram.

- Download: [latest release](../../releases/latest), macOS 12+ on Apple Silicon. The build is not notarized: on first launch allow it in System Settings → Privacy & Security → Open Anyway.
- Inside: built-in SHILLVPN (3 free days per device, then a SHILLVPN subscription), pins beyond Telegram's limit, the ⌘K command palette, reply templates, one-click scheduled sending, a local MCP bridge for Claude Code and Codex, dark and light themes.
- Business model: the client is free; the project earns from SHILLVPN subscriptions and the SHILLVPN partner program. No ads in chats, no data sold.
- Build: as Telegram Desktop ([docs/building-mac.md](docs/building-mac.md)) with your own `api_id`/`api_hash` and an Xray-core binary.
- License: GPLv3 with the OpenSSL exception; Xray-core is MPL-2.0. The SHILLGRAM name and icon are not licensed for other builds.
