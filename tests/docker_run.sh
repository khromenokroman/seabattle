#!/bin/bash
# Проверка Docker-образа: запуск не от root, API и страница, комната, быстрая остановка по docker stop.
#
# Использование: tests/docker_run.sh <образ>
set -u
IMAGE=$1
PORT=${PORT:-18095}
NAME=seabattle-test-$$
fail=0
ok() { echo "ok    $*"; }
bad() { echo "FAIL  $*"; fail=1; }
trap 'docker rm -f "$NAME" >/dev/null 2>&1' EXIT
wait_http() { for _ in $(seq 100); do curl -sf "http://127.0.0.1:$PORT/api/version" >/dev/null && return 0; sleep 0.1; done; return 1; }

docker run -d --name "$NAME" -p "127.0.0.1:$PORT:8090" "$IMAGE" >/dev/null
wait_http && ok "отвечает: $(curl -s http://127.0.0.1:$PORT/api/version)" || { bad "не отвечает"; docker logs "$NAME" | tail -5; exit 1; }
[ "$(docker exec "$NAME" id -u)" = 10001 ] && ok "работает не от root (uid 10001)" || bad "пользователь: $(docker exec "$NAME" id)"
curl -sf "http://127.0.0.1:$PORT/" | grep -q '<title>Морской бой</title>' && ok "веб-страница отдаётся" || bad "нет страницы"
curl -sf -X POST "http://127.0.0.1:$PORT/api/rooms" -H 'Content-Type: application/json' -d '{"name":"Тест"}' \
    | grep -q '"room"' && ok "комната создаётся" || bad "комната не создаётся"

begin=$(date +%s)
docker stop -t 10 "$NAME" >/dev/null
spent=$(($(date +%s) - begin))
code=$(docker inspect -f '{{.State.ExitCode}}' "$NAME")
[ "$spent" -lt 5 ] && [ "$code" = 0 ] && ok "docker stop: остановлен за ${spent} с, код 0" || bad "docker stop: ${spent} с, код $code"
exit $fail
