// Сценарий проверок веб-клиента (web_smoke.sh), выполняется в странице drive.js.
// PHASE: 'start' | 'wait' | 'battle' | 'scroll'; ROOM - код комнаты создателя.
// Помощники: sleep, btn, expect, log, shot.
const $ = (id) => document.getElementById(id);
const shown = (id) => !$(id).hidden;

async function waitFor(cond, text, ms) {
    const deadline = Date.now() + (ms || 8000);
    while (Date.now() < deadline) {
        if (cond()) return;
        await sleep(100);
    }
    throw new Error(text);
}

if (PHASE === 'start') {
    await waitFor(() => shown('screen-start'), 'стартовый экран не появился');
    expect(!!btn('Создать игру'), 'на стартовом экране нет кнопки «Создать игру»');
    log.push('стартовый экран: кнопка «Создать игру» есть');
}

if (PHASE === 'wait') {
    await waitFor(() => shown('screen-wait'), 'экран ожидания не появился по сохранённой сессии');
    const code = $('wait-code').textContent.trim();
    expect(code === ROOM, `код комнаты на экране "${code}" вместо "${ROOM}"`);
    log.push(`экран ожидания: код ${ROOM} на месте`);
    await shot('wait');
}

if (PHASE === 'battle') {
    await sleep(700); // поток от предыдущего запуска мог ещё держаться на сервере
    if (! $('replaced-banner').hidden) {
        btn('Играть здесь').click();
        log.push('поток был отобран предыдущим запуском, переподключились');
    }
    await waitFor(() => shown('screen-game'), 'экран боя не появился');
    await waitFor(() => $('status').textContent.trim().length > 0, 'строка статуса пуста');
    const own = document.querySelectorAll('#own-board .cell').length;
    const enemy = document.querySelectorAll('#enemy-board .cell').length;
    expect(own === 100, `в своём поле ${own} клеток вместо 100`);
    expect(enemy === 100, `в поле противника ${enemy} клеток вместо 100`);
    log.push('экран боя: два поля по 100 клеток, строка статуса есть');
    await shot('battle');
}

if (PHASE === 'scroll') {
    await waitFor(() => shown('screen-game'), 'экран боя не появился');
    await sleep(300);
    const sw = document.documentElement.scrollWidth;
    const iw = window.innerWidth;
    expect(sw <= iw, `ширина ${iw}: горизонтальная прокрутка (scrollWidth ${sw})`);
    log.push(`ширина ${iw}: прокрутки нет (scrollWidth ${sw})`);
    await shot('scroll-' + iw);
}
