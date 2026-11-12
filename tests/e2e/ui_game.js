// Партия двух игроков через интерфейс (web_smoke.sh, проверка 6), выполняется в странице drive.js.
// На странице два iframe с клиентом - два игрока. Только нажатия на кнопки и клетки, как у человека:
// создание комнаты, вход по коду, ручная расстановка кликами и «Случайно», бой до победы, ещё партия,
// выход с подтверждением. Ошибки JavaScript в любом из клиентов - провал.
// Помощники: sleep, btn, expect, log, shot.

const FLEET = [[0, 0, 4], [5, 0, 3], [0, 2, 3], [4, 2, 2], [7, 2, 2], [0, 4, 2], [3, 4, 1], [5, 4, 1], [7, 4, 1], [9, 4, 1]];
const errors = [];

async function waitFor(cond, text, ms) {
    const deadline = Date.now() + (ms || 8000);
    while (Date.now() < deadline) {
        if (cond()) return;
        await sleep(50);
    }
    throw new Error(text);
}

async function player(name, src) {
    localStorage.removeItem('seabattle.session'); // iframe делят localStorage: новый игрок без чужой сессии
    const frame = document.createElement('iframe');
    frame.style.cssText = 'width:1100px;height:800px;border:0';
    document.body.appendChild(frame);
    await new Promise((resolve) => { frame.onload = resolve; frame.src = src || '/'; });
    const win = frame.contentWindow;
    win.addEventListener('error', (e) => errors.push(`${name}: ${e.message}`));
    win.addEventListener('unhandledrejection', (e) => errors.push(`${name}: ${e.reason}`));
    const $ = (id) => win.document.getElementById(id);
    return {
        name, $, win,
        shown: (id) => !$(id).hidden,
        status: () => $('status').textContent.trim(),
        cell: (board, x, y) => win.document.querySelector(`#${board} .cell[data-x="${x}"][data-y="${y}"]`),
        button: (text) => [...win.document.querySelectorAll('button')].find((b) => b.textContent.trim() === text && b.offsetParent),
    };
}

document.body.innerHTML = '';
document.body.style.background = '#fff';
const a = await player('Алиса');
await waitFor(() => a.shown('screen-start'), 'стартовый экран не появился');
expect(a.shown('start-choice') && !a.shown('join-form'), 'на главной не выбор «Создать игру» / «Войти по коду»');

// создание комнаты, копирование ссылки
a.$('name').value = 'Алиса';
a.$('btn-create').click();
await waitFor(() => a.shown('screen-wait'), 'Алиса: нет экрана ожидания после «Создать игру»');
const code = a.$('wait-code').textContent.trim();
expect(/^[A-Z0-9]{4}$/.test(code), `код комнаты "${code}"`);
const link = a.$('share-link').textContent.trim();
expect(link.endsWith('#' + code), `ссылка ${link} без кода`);
a.$('btn-copy').click();
await waitFor(() => /скопирована/.test(a.$('btn-copy').textContent) || /скопируйте/.test(a.$('toast').textContent),
    '«Скопировать ссылку» ничего не сделала');

// Борис приходит по ссылке: только вход в эту комнату, без «Создать игру»
const b = await player('Борис', '/#' + code.toLowerCase());
await waitFor(() => b.shown('screen-start'), 'Борис: стартовый экран не появился');
expect(!b.shown('start-choice') && b.shown('invite') && !b.shown('code-field'), 'по ссылке не экран приглашения');
expect(b.$('invite-code').textContent === code, `по ссылке код «${b.$('invite-code').textContent}» вместо ${code}`);
b.$('name').value = 'Борис';
b.$('btn-join').click();
await waitFor(() => a.shown('screen-game') && b.shown('screen-game'), 'после входа Бориса нет экрана расстановки');

// третий: «Войти по коду» с главной, комната занята - ошибка и «Назад» к выбору
const c = await player('Вера');
c.$('name').value = 'Вера';
c.$('btn-to-join').click();
expect(c.shown('join-form') && c.shown('code-field') && !c.shown('start-choice'), '«Войти по коду» не открыла поле кода');
c.$('code').value = code.toLowerCase();
c.$('btn-join').click();
await waitFor(() => c.$('start-error').textContent !== '', 'третий игрок вошёл в занятую комнату');
const busy = c.$('start-error').textContent;
c.$('btn-back').click();
expect(c.shown('start-choice') && !c.shown('join-form'), '«Назад» не вернула к выбору');
log.push(`комната ${code}: создана, ссылка скопирована; Борис по ссылке (только «Войти»), третий по коду получил «${busy}»`);

// Алиса расставляет вручную: выбор корабля в доке и клик по клетке; сначала неудачная попытка у края
a.$('dock').querySelector('[data-len="4"]').click();
a.cell('own-board', 8, 9).click();
expect(a.win.document.querySelectorAll('#own-board .cell.ship').length === 0, 'корабль за краем поля встал');
for (const [x, y, len] of FLEET) {
    const item = a.$('dock').querySelector(`[data-len="${len}"]`);
    expect(item && !item.disabled, `в доке нет корабля длины ${len}`);
    item.click();
    a.cell('own-board', x, y).click();
}
expect(a.win.document.querySelectorAll('#own-board .cell.ship').length === 20, 'Алиса: на поле не 20 палуб');
expect(!a.$('btn-ready').disabled, 'Алиса: «Готов» неактивна после расстановки всех кораблей');
a.$('btn-ready').click();

// Борис - «Случайно» и «Готов»
b.$('btn-random').click();
await waitFor(() => !b.$('btn-ready').disabled, 'Борис: «Готов» неактивна после «Случайно»');
expect(b.win.document.querySelectorAll('#own-board .cell.ship').length === 20, 'Борис: случайный флот не 20 палуб');
b.$('btn-ready').click();
await waitFor(() => a.shown('enemy-wrap') && b.shown('enemy-wrap'), 'бой не начался');
await waitFor(() => /ход/i.test(a.status()) && /ход/i.test(b.status()), 'нет строки хода');
expect((a.status() === 'Ваш ход') !== (b.status() === 'Ваш ход'), `ход у обоих или ни у кого: ${a.status()} / ${b.status()}`);
log.push('расстановка: Алиса кликами (корабль за краем не встал), Борис «Случайно»; бой начался');

// бой: тот, у кого «Ваш ход», кликает по свободной клетке противника
let shots = 0;
while (!(a.shown('finish') && b.shown('finish'))) {
    const p = a.status() === 'Ваш ход' ? a : b.status() === 'Ваш ход' ? b : null;
    if (p && p.$('enemy-board').classList.contains('active')) {
        const free = [...p.win.document.querySelectorAll('#enemy-board .cell.free')];
        expect(free.length > 0, `${p.name}: нет свободных клеток, а партия не кончилась`);
        free[Math.floor(Math.random() * free.length)].click();
        free[0].click(); // второй клик до снимка не должен уйти на сервер
        shots++;
        expect(shots <= 220, 'партия не закончилась за 220 выстрелов');
    }
    await sleep(30);
    if (a.shown('finish') !== b.shown('finish')) await sleep(300);
}
const win = a.$('finish-title').textContent === 'Победа!' ? a : b;
const lose = win === a ? b : a;
expect(lose.$('finish-title').textContent === 'Поражение', `у проигравшего «${lose.$('finish-title').textContent}»`);
expect(win.win.document.querySelectorAll('#enemy-board .cell.sunk').length === 20, 'у победителя не 20 потопленных палуб');
const revealed = lose.win.document.querySelectorAll('#enemy-board .cell.reveal').length;
expect(revealed === 20, `у проигравшего открыто ${revealed} палуб противника вместо 20`);
const toasts = [a, b].map((p) => p.$('toast').textContent).filter((t) => /уже стреляли/.test(t));
expect(toasts.length === 0, 'двойной клик ушёл на сервер: ' + toasts.join(', '));
log.push(`бой: ${shots} кликов, победил ${win.name}, у проигравшего открыты корабли противника`);
await shot('ui-finish');

// ещё партия и выход с подтверждением
win.$('btn-rematch').click();
await waitFor(() => /противник хочет/i.test(lose.$('finish-note').textContent), 'проигравший не видит предложения');
lose.$('btn-rematch').click();
await waitFor(() => a.shown('dock-wrap') && b.shown('dock-wrap'), 'после «Ещё партию» нет расстановки');
expect(a.win.document.querySelectorAll('#own-board .cell.ship').length === 0, 'старая расстановка осталась');
b.$('btn-leave-game').click();
await waitFor(() => !b.$('modal').hidden, 'нет подтверждения выхода');
b.$('modal-yes').click();
await waitFor(() => b.shown('screen-start'), 'Борис не вернулся на старт');
await waitFor(() => a.shown('screen-wait'), 'Алиса не вернулась к ожиданию соперника');
log.push('ещё партия: оба согласились, расстановка заново; выход Бориса с подтверждением, Алиса ждёт');

expect(errors.length === 0, 'ошибки JavaScript: ' + errors.join('; '));
