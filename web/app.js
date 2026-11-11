// Клиент «Морского боя»: экраны рисуются целиком из снимка состояния, который присылает сервер (docs/protocol.md).

const SIZE = 10;
const LETTERS = 'АБВГДЕЖЗИК';
const STORE_SESSION = 'seabattle.session';
const STORE_NAME = 'seabattle.name';

const $ = (id) => document.getElementById(id);

const store = {
    get(key) {
        try { return JSON.parse(localStorage.getItem(key)); } catch { return null; }
    },
    set(key, value) {
        try { localStorage.setItem(key, JSON.stringify(value)); } catch { /* приватный режим */ }
    },
    remove(key) {
        try { localStorage.removeItem(key); } catch { /* приватный режим */ }
    },
};

// ---------- состояние клиента ----------

let session = store.get(STORE_SESSION); // {room, token}
let state = null; // последний снимок
let events = null; // EventSource
let reconnectTimer = 0;
let firing = false;
let pendingShot = null; // 'x,y' выстрела, которого ещё нет в снимке: защита от второго клика по той же клетке
let prevPhase = null;

// расстановка до отправки: корабли, корабль «в руке», клетка под указателем
const placing = { ships: [], hand: null, hover: null, dir: 'h' };

// ---------- сеть ----------

class ApiError extends Error {
    constructor(status, message) {
        super(message);
        this.status = status;
    }
}

async function api(path, body) {
    let res;
    try {
        res = await fetch(path, body === undefined ? {} : {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
        });
    } catch {
        throw new ApiError(0, 'Нет связи с сервером');
    }
    let data = {};
    try { data = await res.json(); } catch { /* пустой ответ */ }
    if (!res.ok) throw new ApiError(res.status, data.error || `Ошибка сервера ${res.status}`);
    return data;
}

/** Жив ли сохранённый токен: 'ok', 'gone' (401) или 'retry' (нет связи). */
async function checkSession() {
    try {
        await api('/api/me?token=' + encodeURIComponent(session.token));
        return 'ok';
    } catch (e) {
        return e.status === 401 ? 'gone' : 'retry';
    }
}

function connect() {
    disconnect();
    events = new EventSource('/api/events?token=' + encodeURIComponent(session.token));
    events.addEventListener('state', (e) => {
        $('conn-banner').hidden = true;
        $('replaced-banner').hidden = true;
        state = JSON.parse(e.data);
        settlePendingShot();
        render();
    });
    // игра открыта в другой вкладке: не переподключаться самим, иначе вкладки будут отбирать поток друг у друга
    events.addEventListener('closed', () => {
        disconnect();
        $('conn-banner').hidden = true;
        $('replaced-banner').hidden = false;
    });
    events.onerror = () => {
        disconnect();
        $('conn-banner').hidden = false;
        scheduleReconnect();
    };
}

function disconnect() {
    clearTimeout(reconnectTimer);
    if (events) {
        events.onerror = null;
        events.close();
        events = null;
    }
}

function scheduleReconnect() {
    clearTimeout(reconnectTimer);
    reconnectTimer = setTimeout(async () => {
        if (!session) return;
        const result = await checkSession();
        if (result === 'ok') {
            connect();
        } else if (result === 'gone') {
            forgetSession();
            toast('Игра завершена на сервере');
        } else {
            scheduleReconnect();
        }
    }, 2000);
}

function startSession(joined) {
    session = { room: joined.room, token: joined.token };
    store.set(STORE_SESSION, session);
    history.replaceState(null, '', location.pathname + '#' + joined.room);
    connect();
}

function forgetSession() {
    disconnect();
    session = null;
    state = null;
    pendingShot = null;
    store.remove(STORE_SESSION);
    $('conn-banner').hidden = true;
    $('replaced-banner').hidden = true;
    history.replaceState(null, '', location.pathname);
    render();
}

// ---------- общие элементы ----------

let toastTimer = 0;
function toast(text) {
    const el = $('toast');
    el.textContent = text;
    el.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.remove('show'), 4000);
}

function ask(text) {
    return new Promise((resolve) => {
        $('modal-text').textContent = text;
        $('modal').hidden = false;
        const done = (answer) => {
            $('modal').hidden = true;
            $('modal-yes').onclick = $('modal-no').onclick = null;
            resolve(answer);
        };
        $('modal-yes').onclick = () => done(true);
        $('modal-no').onclick = () => done(false);
        $('modal-no').focus();
    });
}

function decks(len) {
    return `<span class="decks">${'<i></i>'.repeat(len)}</span>`;
}

const key = (x, y) => x + ',' + y;

function shipCells(ship) {
    const cells = [];
    for (let i = 0; i < ship.len; i++) {
        cells.push(ship.dir === 'v' ? { x: ship.x, y: ship.y + i } : { x: ship.x + i, y: ship.y });
    }
    return cells;
}

// ---------- поле ----------

class BoardView {
    constructor(el, onCell) {
        this.el = el;
        this.cells = [];
        el.innerHTML = '';
        el.appendChild(document.createElement('div'));
        for (let x = 0; x < SIZE; x++) el.appendChild(this.label(LETTERS[x]));
        for (let y = 0; y < SIZE; y++) {
            el.appendChild(this.label(String(y + 1)));
            const row = [];
            for (let x = 0; x < SIZE; x++) {
                const cell = document.createElement('div');
                cell.className = 'cell';
                cell.dataset.x = x;
                cell.dataset.y = y;
                el.appendChild(cell);
                row.push(cell);
            }
            this.cells.push(row);
        }
        el.addEventListener('click', (e) => {
            const cell = e.target.closest('.cell');
            if (cell) onCell(+cell.dataset.x, +cell.dataset.y);
        });
    }

    label(text) {
        const el = document.createElement('div');
        el.className = 'label';
        el.textContent = text;
        return el;
    }

    /** classes: Map 'x,y' -> строка классов. */
    paint(classes) {
        for (let y = 0; y < SIZE; y++) {
            for (let x = 0; x < SIZE; x++) {
                this.cells[y][x].className = 'cell ' + (classes.get(key(x, y)) || '');
            }
        }
    }
}

function addClass(map, x, y, cls) {
    const k = key(x, y);
    map.set(k, ((map.get(k) || '') + ' ' + cls).trim());
}

const ownBoard = new BoardView($('own-board'), onOwnCell);
const enemyBoard = new BoardView($('enemy-board'), onEnemyCell);

// ---------- правила расстановки (те же, что на сервере) ----------

function inside(c) {
    return c.x >= 0 && c.x < SIZE && c.y >= 0 && c.y < SIZE;
}

/** Можно ли поставить корабль к уже стоящим: в пределах поля, без пересечений и касаний. */
function canPlace(ship, ships) {
    const cells = shipCells(ship);
    if (!cells.every(inside)) return false;
    const taken = new Set();
    for (const other of ships) for (const c of shipCells(other)) taken.add(key(c.x, c.y));
    for (const c of cells) {
        for (let dy = -1; dy <= 1; dy++) {
            for (let dx = -1; dx <= 1; dx++) {
                if (taken.has(key(c.x + dx, c.y + dy))) return false;
            }
        }
    }
    return true;
}

/** Длины, которые ещё не поставлены, по составу флота. */
function remainingLengths() {
    const left = [...state.fleet];
    for (const s of placing.ships) {
        const i = left.indexOf(s.len);
        if (i >= 0) left.splice(i, 1);
    }
    return left;
}

function resetPlacing() {
    placing.ships = [];
    placing.hover = null;
    placing.hand = state ? Math.max(...state.fleet) : null;
}

function takeNext() {
    const left = remainingLengths();
    placing.hand = left.length ? Math.max(...left) : null;
}

// ---------- действия ----------

function onOwnCell(x, y) {
    if (!state || state.phase !== 'placing' || state.placed.you) return;
    const idx = placing.ships.findIndex((s) => shipCells(s).some((c) => c.x === x && c.y === y));
    if (idx >= 0) {
        const [ship] = placing.ships.splice(idx, 1);
        placing.hand = ship.len;
        placing.dir = ship.dir;
        placing.hover = { x, y };
        render();
        return;
    }
    if (!placing.hand) return;
    const ship = { x, y, len: placing.hand, dir: placing.dir };
    if (!canPlace(ship, placing.ships)) {
        toast('Сюда нельзя: корабли не должны касаться друг друга и выходить за край');
        return;
    }
    placing.ships.push(ship);
    takeNext();
    render();
}

/** Снять отметку выстрела, когда он появился в снимке или партия уже не идёт. */
function settlePendingShot() {
    if (!pendingShot) return;
    if (!state || state.phase !== 'playing' || state.enemy.shots.some((s) => key(s.x, s.y) === pendingShot)) {
        pendingShot = null;
    }
}

async function onEnemyCell(x, y) {
    if (!state || state.phase !== 'playing' || state.turn !== 'you' || firing || pendingShot) return;
    if (state.enemy.shots.some((s) => s.x === x && s.y === y)) return;
    firing = true;
    pendingShot = key(x, y);
    enemyBoard.el.classList.remove('active');
    try {
        await api('/api/fire', { token: session.token, x, y });
    } catch (e) {
        pendingShot = null;
        if (e.status === 401) forgetSession();
        toast(e.message);
    } finally {
        firing = false;
        settlePendingShot();
        if (state) render();
    }
}

function rotate() {
    placing.dir = placing.dir === 'h' ? 'v' : 'h';
    render();
}

async function action(path, extra = {}) {
    try {
        await api(path, { token: session.token, ...extra });
        return true;
    } catch (e) {
        if (e.status === 401) forgetSession();
        toast(e.message);
        return false;
    }
}

// ---------- отрисовка ----------

function showScreen(id) {
    for (const s of document.querySelectorAll('.screen')) s.hidden = s.id !== id;
}

function render() {
    if (!session) {
        showScreen('screen-start');
        $('room-badge').textContent = '';
        prevPhase = null;
        return;
    }
    if (!state) {
        showScreen(null);
        return;
    }
    const phase = state.phase;
    if (phase === 'placing' && prevPhase !== 'placing') resetPlacing();
    prevPhase = phase;

    $('room-badge').textContent = state.opponent
        ? `Комната ${state.room} · ${state.you.name} против ${state.opponent.name}`
        : `Комната ${state.room} · ${state.you.name}`;

    if (phase === 'waiting') {
        showScreen('screen-wait');
        $('wait-code').textContent = state.room;
        $('share-link').value = location.origin + location.pathname + '#' + state.room;
        return;
    }
    showScreen('screen-game');
    renderStatus();
    renderOwn();
    renderEnemy();
    renderDock();
    renderFinish();

    $('opp-offline').hidden = !state.opponent || state.opponent.connected;
    $('btn-resign').hidden = phase === 'finished';
    $('btn-leave-game').hidden = phase === 'finished';
}

function renderStatus() {
    const el = $('status');
    el.classList.toggle('your-turn', state.phase === 'playing' && state.turn === 'you');
    if (state.phase === 'placing') {
        if (state.placed.you) {
            el.textContent = 'Ждём, пока противник расставит корабли';
        } else {
            el.textContent = state.placed.opponent ? 'Расставьте корабли · противник готов' : 'Расставьте корабли';
        }
    } else if (state.phase === 'playing') {
        el.textContent = state.turn === 'you' ? 'Ваш ход' : 'Ход противника';
    } else {
        el.textContent = '';
    }
}

function lastShotClass(map, by) {
    if (state.last_shot && state.last_shot.by === by) addClass(map, state.last_shot.x, state.last_shot.y, 'last');
}

function renderOwn() {
    const editing = state.phase === 'placing' && !state.placed.you;
    const map = new Map();
    ownBoard.el.classList.toggle('placing', editing);
    $('own-title').textContent = editing ? 'Расстановка' : 'Мои корабли';

    if (editing) {
        for (const s of placing.ships) for (const c of shipCells(s)) addClass(map, c.x, c.y, 'ship');
        if (placing.hand && placing.hover) {
            const ship = { ...placing.hover, len: placing.hand, dir: placing.dir };
            const cls = canPlace(ship, placing.ships) ? 'preview-ok' : 'preview-bad';
            for (const c of shipCells(ship)) if (inside(c)) addClass(map, c.x, c.y, cls);
        }
    } else {
        for (const s of state.own.ships) {
            for (const c of shipCells(s)) addClass(map, c.x, c.y, s.sunk ? 'ship sunk' : 'ship');
        }
        for (const shot of state.own.shots) addClass(map, shot.x, shot.y, shot.hit ? 'hit' : 'miss');
        lastShotClass(map, 'opponent');
    }
    ownBoard.paint(map);
}

function renderEnemy() {
    const show = state.phase === 'playing' || state.phase === 'finished';
    $('enemy-wrap').hidden = !show;
    if (!show) return;
    const map = new Map();
    for (const s of state.enemy.ships) for (const c of shipCells(s)) addClass(map, c.x, c.y, 'reveal');
    for (const s of state.enemy.sunk) for (const c of shipCells(s)) addClass(map, c.x, c.y, 'sunk');
    const shot = new Set();
    for (const s of state.enemy.shots) {
        addClass(map, s.x, s.y, s.hit ? 'hit' : 'miss');
        shot.add(key(s.x, s.y));
    }
    const active = state.phase === 'playing' && state.turn === 'you' && !firing && !pendingShot;
    if (active) {
        for (let y = 0; y < SIZE; y++) for (let x = 0; x < SIZE; x++) if (!shot.has(key(x, y))) addClass(map, x, y, 'free');
    }
    lastShotClass(map, 'you');
    enemyBoard.el.classList.toggle('active', active);
    enemyBoard.paint(map);

    // сколько кораблей противника осталось по длинам
    const lengths = [...new Set(state.fleet)].sort((a, b) => b - a);
    $('enemy-fleet').innerHTML = lengths.map((len) => {
        const total = state.fleet.filter((l) => l === len).length;
        const sunk = state.enemy.sunk.filter((s) => s.len === len).length;
        const left = total - sunk;
        return `<span class="${left ? '' : 'gone'}">${decks(len)} × ${left}</span>`;
    }).join('');
}

function renderDock() {
    const editing = state.phase === 'placing' && !state.placed.you;
    $('dock-wrap').hidden = !editing;
    if (!editing) return;
    const left = remainingLengths();
    const lengths = [...new Set(state.fleet)].sort((a, b) => b - a);
    $('dock').innerHTML = lengths.map((len) => {
        const count = left.filter((l) => l === len).length;
        const selected = placing.hand === len ? ' selected' : '';
        return `<button class="dock-item${selected}" data-len="${len}" ${count ? '' : 'disabled'}>${decks(len)} × ${count}</button>`;
    }).join('');
    $('btn-ready').disabled = left.length > 0;
    $('btn-rotate').textContent = placing.dir === 'h' ? 'Повернуть ↓' : 'Повернуть →';
}

function renderFinish() {
    const finished = state.phase === 'finished';
    $('finish').hidden = !finished;
    if (!finished) return;
    const win = state.winner === 'you';
    const title = $('finish-title');
    title.textContent = win ? 'Победа!' : 'Поражение';
    title.className = 'finish-title ' + (win ? 'win' : 'lose');
    const notes = [];
    if (state.resigned) notes.push(win ? 'Противник сдался' : 'Вы сдались');
    if (state.rematch.you) notes.push('Ждём ответа противника');
    else if (state.rematch.opponent) notes.push('Противник хочет ещё партию');
    $('finish-note').textContent = notes.join(' · ');
    $('btn-rematch').disabled = state.rematch.you;
}

// ---------- обработчики ----------

$('name').value = store.get(STORE_NAME) || '';

function readName() {
    const name = $('name').value.trim();
    if (!name) {
        $('start-error').textContent = 'Введите имя';
        $('name').focus();
        return null;
    }
    store.set(STORE_NAME, name);
    return name;
}

$('btn-create').addEventListener('click', async () => {
    $('start-error').textContent = '';
    const name = readName();
    if (!name) return;
    try {
        startSession(await api('/api/rooms', { name }));
    } catch (e) {
        $('start-error').textContent = e.message;
    }
});

$('join-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    $('start-error').textContent = '';
    const name = readName();
    if (!name) return;
    const code = $('code').value.trim().toUpperCase();
    if (!/^[A-Z0-9]{4}$/.test(code)) {
        $('start-error').textContent = 'Код комнаты: 4 буквы или цифры';
        return;
    }
    try {
        startSession(await api(`/api/rooms/${code}/join`, { name }));
    } catch (err) {
        $('start-error').textContent = err.message;
    }
});

$('btn-copy').addEventListener('click', async () => {
    const input = $('share-link');
    try {
        await navigator.clipboard.writeText(input.value);
        toast('Ссылка скопирована');
    } catch {
        input.focus();
        input.select();
        toast('Скопируйте ссылку из поля');
    }
});

for (const btn of document.querySelectorAll('.js-leave')) {
    btn.addEventListener('click', async () => {
        if (state && state.phase !== 'waiting' && state.phase !== 'finished' && !(await ask('Выйти из комнаты? Партия будет засчитана как поражение.'))) return;
        const token = session && session.token;
        forgetSession();
        history.replaceState(null, '', location.pathname);
        if (token) api('/api/leave', { token }).catch(() => {});
    });
}

$('btn-resign').addEventListener('click', async () => {
    if (await ask('Сдаться? Победа достанется противнику.')) action('/api/resign');
});

$('btn-rematch').addEventListener('click', () => action('/api/rematch'));
$('btn-here').addEventListener('click', () => {
    $('replaced-banner').hidden = true;
    if (session) connect();
});

$('dock').addEventListener('click', (e) => {
    const item = e.target.closest('.dock-item');
    if (!item || item.disabled) return;
    placing.hand = +item.dataset.len;
    render();
});

$('btn-rotate').addEventListener('click', rotate);
$('btn-clear').addEventListener('click', () => {
    resetPlacing();
    render();
});
$('btn-random').addEventListener('click', async () => {
    try {
        const data = await api('/api/random-fleet');
        placing.ships = data.ships;
        placing.hand = null;
        render();
    } catch (e) {
        toast(e.message);
    }
});
$('btn-ready').addEventListener('click', async () => {
    $('btn-ready').disabled = true;
    if (!(await action('/api/place', { ships: placing.ships }))) render();
});

document.addEventListener('keydown', (e) => {
    if ((e.key === 'r' || e.key === 'R' || e.key === 'к' || e.key === 'К') && e.target.tagName !== 'INPUT'
        && state && state.phase === 'placing' && !state.placed.you) rotate();
    if (e.key === 'Escape' && !$('modal').hidden) $('modal-no').click();
});

ownBoard.el.addEventListener('pointerover', (e) => {
    const cell = e.target.closest('.cell');
    if (!cell || !state || state.phase !== 'placing' || state.placed.you) return;
    const hover = { x: +cell.dataset.x, y: +cell.dataset.y };
    if (placing.hover && placing.hover.x === hover.x && placing.hover.y === hover.y) return;
    placing.hover = hover;
    renderOwn();
});
ownBoard.el.addEventListener('pointerleave', () => {
    if (!placing.hover) return;
    placing.hover = null;
    if (state && state.phase === 'placing') renderOwn();
});

// ---------- запуск ----------

async function start() {
    const hashCode = location.hash.replace('#', '').toUpperCase();
    if (session && session.token) {
        const result = await checkSession();
        if (result === 'gone') {
            session = null;
            store.remove(STORE_SESSION);
        } else {
            // ссылка на другую комнату: остаёмся в текущей игре, код будет подставлен после выхода
            if (/^[A-Z0-9]{4}$/.test(hashCode) && hashCode !== session.room) $('code').value = hashCode;
            render();
            if (result === 'retry') {
                $('conn-banner').hidden = false;
                scheduleReconnect();
            } else {
                connect();
            }
            return;
        }
    }
    if (/^[A-Z0-9]{4}$/.test(hashCode)) $('code').value = hashCode;
    render();
    ($('name').value ? $('code').value ? $('join-form').querySelector('button') : $('btn-create') : $('name')).focus();
}

start();
