#!/bin/sh
# tests/web_smoke.sh <путь к seabattle> <каталог web>
# Проверка веб-клиента в headless Chrome: стартовый экран, ожидание по сессии из
# localStorage, экран боя после расстановки обоих через API, отсутствие горизонтальной
# прокрутки на ширине 360 и 1280, целая партия двух игроков кликами (tests/e2e/ui_game.js).
# Управление Chrome - через CDP на чистом node
# (tests/e2e/drive.js), без npm-пакетов. Скриншоты - в каталог $SHOTS (если задан).

set -u

BIN=${1:-}
WEB=${2:-}
if [ -z "$BIN" ] || [ -z "$WEB" ]; then
    echo "использование: $0 <путь к seabattle> <каталог web>" >&2
    exit 2
fi

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

CHROME_BIN=${CHROME:-}
if [ -z "$CHROME_BIN" ]; then
    for c in chromium google-chrome google-chrome-stable; do
        if command -v "$c" >/dev/null 2>&1; then
            CHROME_BIN=$(command -v "$c")
            break
        fi
    done
fi
if [ -z "$CHROME_BIN" ]; then
    echo 'не найден браузер: задайте $CHROME или поставьте chromium/google-chrome' >&2
    exit 2
fi
command -v node >/dev/null 2>&1 || { echo 'не найден node' >&2; exit 2; }
command -v curl >/dev/null 2>&1 || { echo 'не найден curl' >&2; exit 2; }

json_field() { # <поле>: вытащить поле из JSON со stdin (строки - без кавычек)
    node -e 'let d="";process.stdin.on("data",c=>d+=c).on("end",()=>{const v=JSON.parse(d)[process.argv[1]];console.log(typeof v==="string"?v:JSON.stringify(v))})' "$1"
}

fail() {
    echo "ПРОВАЛ: $1" >&2
    exit 1
}

PORT=$(node -e 'const s=require("net").createServer();s.listen(0,"127.0.0.1",()=>{console.log(s.address().port);s.close()})')
BASE="http://127.0.0.1:$PORT"
LOG=$(mktemp /tmp/seabattle-web-smoke.XXXXXX.log)

"$BIN" --host 127.0.0.1 --port "$PORT" --web "$WEB" >"$LOG" 2>&1 &
SERVER_PID=$!
cleanup() {
    kill "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null
    rm -f "$LOG"
}
trap cleanup EXIT INT TERM

i=0
while ! curl -fsS "$BASE/api/version" >/dev/null 2>&1; do
    i=$((i + 1))
    if [ "$i" -gt 100 ]; then
        echo 'сервер не ответил на /api/version за 10 с' >&2
        cat "$LOG" >&2
        exit 1
    fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo 'сервер завершился при старте' >&2
        cat "$LOG" >&2
        exit 1
    fi
    sleep 0.1
done

run() { # <фаза> <ширина> <высота> <localStorage json | '-'>
    phase=$1 width=$2 height=$3 storage=${4:-'-'}
    if [ "$storage" = '-' ]; then
        PHASE=$phase WIDTH=$width HEIGHT=$height CHROME=$CHROME_BIN SHOTS=${SHOTS:-} \
            node "$DIR/e2e/drive.js" "$BASE/" "$DIR/e2e/web_check.js"
    else
        PHASE=$phase WIDTH=$width HEIGHT=$height ROOM=$ROOM CHROME=$CHROME_BIN \
            LOCAL_STORAGE_JSON=$storage SHOTS=${SHOTS:-} \
            node "$DIR/e2e/drive.js" "$BASE/" "$DIR/e2e/web_check.js"
    fi
}

echo 'проверка 1/6: стартовый экран'
run start 1280 900 - || fail 'стартовый экран'

echo 'проверка 2/6: экран ожидания по сессии из localStorage'
JOINED=$(curl -fsS -X POST "$BASE/api/rooms" -H 'Content-Type: application/json' -d '{"name":"Смоук"}') \
    || fail 'создание комнаты через API'
ROOM=$(printf '%s' "$JOINED" | json_field room)
TOKEN=$(printf '%s' "$JOINED" | json_field token)
[ -n "$ROOM" ] && [ -n "$TOKEN" ] || fail 'в ответе создания нет room/token'
SESSION=$(printf '{"room":"%s","token":"%s"}' "$ROOM" "$TOKEN")
run wait 1280 900 "$SESSION" || fail 'экран ожидания'

echo 'проверка 3/6: экран боя после входа второго и расстановки через API'
JOIN2=$(curl -fsS -X POST "$BASE/api/rooms/$ROOM/join" -H 'Content-Type: application/json' -d '{"name":"Второй"}') \
    || fail 'вход второго игрока через API'
TOKEN2=$(printf '%s' "$JOIN2" | json_field token)
place() { # <token>
    SHIPS=$(curl -fsS "$BASE/api/random-fleet") || fail 'случайный флот'
    printf '{"token":"%s","ships":%s}' "$1" "$(printf '%s' "$SHIPS" | json_field ships)" \
        | curl -fsS -X POST "$BASE/api/place" -H 'Content-Type: application/json' -d @- >/dev/null \
        || fail 'расстановка через API'
}
place "$TOKEN"
place "$TOKEN2"
run battle 1280 900 "$SESSION" || fail 'экран боя'

echo 'проверка 4/6: нет горизонтальной прокрутки при ширине 360'
run scroll 360 800 "$SESSION" || fail 'прокрутка при ширине 360'

echo 'проверка 5/6: нет горизонтальной прокрутки при ширине 1280'
run scroll 1280 900 "$SESSION" || fail 'прокрутка при ширине 1280'

echo 'проверка 6/6: партия двух игроков через интерфейс (кнопки и клики по клеткам)'
E2E_TIMEOUT=120 WIDTH=1300 HEIGHT=1800 CHROME=$CHROME_BIN SHOTS=${SHOTS:-} \
    node "$DIR/e2e/drive.js" "$BASE/" "$DIR/e2e/ui_game.js" || fail 'партия через интерфейс'

echo 'web_smoke: все проверки прошли'
