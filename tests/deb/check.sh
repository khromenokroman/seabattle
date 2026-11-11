#!/bin/bash
# Сценарий внутри контейнера с systemd (запускает tests/deb_install.sh): установка пакета из /deb,
# служба, обновление с сохранением настроек, выключенная служба, remove, purge, повторная установка.
set -u
fail=0
ok() { echo "ok    $*"; }
bad() { echo "FAIL  $*"; fail=1; }
wait_http() { for _ in $(seq 50); do curl -sf "http://127.0.0.1:$1/api/version" >/dev/null && return 0; sleep 0.2; done; return 1; }
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq /deb/*.deb >/dev/null 2>&1 || bad "apt install"
systemctl is-active -q seabattle && ok "служба запущена" || { bad "служба не запущена"; journalctl -u seabattle --no-pager | tail -5; }
systemctl is-enabled -q seabattle && ok "служба включена" || bad "служба не включена"
wait_http 8090 && ok "отвечает на 8090: $(curl -s http://127.0.0.1:8090/api/version)" || bad "нет ответа на 8090"
ss -ltn | grep -q '0.0.0.0:8090' && ok "слушает 0.0.0.0:8090" || bad "не 0.0.0.0: $(ss -ltn | grep 8090)"
[ "$(systemctl show -p User --value seabattle)" = seabattle ] && ok "под пользователем seabattle" || bad "пользователь службы"
curl -sf http://127.0.0.1:8090/ | grep -q '<title>Морской бой</title>' && ok "веб-страница отдаётся" || bad "нет страницы"

sed -i 's/--port 8090/--port 8091/' /etc/default/seabattle
apt-get install -y -qq --reinstall /deb/*.deb >/dev/null 2>&1 || bad "переустановка"
grep -q 8091 /etc/default/seabattle && ok "настройки сохранены при обновлении" || bad "настройки затёрты"
wait_http 8091 && ok "после обновления перезапущена на 8091" || bad "нет ответа на 8091"

systemctl disable -q --now seabattle
apt-get install -y -qq --reinstall /deb/*.deb >/dev/null 2>&1
systemctl is-enabled -q seabattle && bad "выключенная служба включилась при обновлении" || ok "выключенная администратором остаётся выключенной"
systemctl enable -q --now seabattle

apt-get remove -y -qq seabattle >/dev/null 2>&1 || bad "remove"
systemctl is-active -q seabattle && bad "служба работает после удаления" || ok "remove: служба остановлена"
apt-get purge -y -qq seabattle >/dev/null 2>&1 || bad "purge"
[ ! -e /etc/default/seabattle ] && ok "purge: настройки удалены" || bad "purge: настройки остались"

apt-get install -y -qq /deb/*.deb >/dev/null 2>&1 || bad "повторная установка"
wait_http 8090 && ok "повторная установка: служба отвечает на 8090" || bad "повторная установка"
exit $fail
