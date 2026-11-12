// Звуки боя синтезируются Web Audio: выстрел, затем всплеск (мимо), взрыв (попал) или большой взрыв
// (потопил); сигнал «ваш ход»; короткие мелодии победы и поражения. Без файлов: шум и генераторы с огибающей.
// Браузер разрешает звук только после действия пользователя, поэтому контекст создаётся по первому
// нажатию на странице.

const STORE_MUTED = 'seabattle.muted';
const RESULT_DELAY = 0.35; // с: пауза между выстрелом и результатом

let ctx = null;
let master = null;
let noiseBuffer = null;
let muted = false;

try {
    muted = localStorage.getItem(STORE_MUTED) === '1';
} catch {
    // приватный режим
}

function unlock() {
    if (!ctx) {
        const Ctx = window.AudioContext || window.webkitAudioContext;
        if (!Ctx) return;
        ctx = new Ctx();
        master = ctx.createGain();
        master.gain.value = 0.6;
        master.connect(ctx.destination);
        noiseBuffer = ctx.createBuffer(1, ctx.sampleRate * 2, ctx.sampleRate);
        const data = noiseBuffer.getChannelData(0);
        for (let i = 0; i < data.length; i++) data[i] = Math.random() * 2 - 1;
    }
    if (ctx.state === 'suspended') ctx.resume();
}

document.addEventListener('pointerdown', unlock, { capture: true });
document.addEventListener('keydown', unlock, { capture: true });

function envelope(at, peak, attack, decay) {
    const g = ctx.createGain();
    g.gain.setValueAtTime(0.0001, at);
    g.gain.exponentialRampToValueAtTime(peak, at + attack);
    g.gain.exponentialRampToValueAtTime(0.0001, at + attack + decay);
    g.connect(master);
    return g;
}

// шум через фильтр, частота среза едет от f0 к f1
function noise(at, { type = 'lowpass', f0, f1, q = 1, peak, attack = 0.005, decay }) {
    const src = ctx.createBufferSource();
    src.buffer = noiseBuffer;
    const filter = ctx.createBiquadFilter();
    filter.type = type;
    filter.Q.value = q;
    filter.frequency.setValueAtTime(f0, at);
    filter.frequency.exponentialRampToValueAtTime(f1, at + attack + decay);
    src.connect(filter).connect(envelope(at, peak, attack, decay));
    src.start(at);
    src.stop(at + attack + decay + 0.05);
}

// тон с падающей частотой: «удар» в низах
function thump(at, { f0, f1, peak, decay, type = 'sine' }) {
    const osc = ctx.createOscillator();
    osc.type = type;
    osc.frequency.setValueAtTime(f0, at);
    osc.frequency.exponentialRampToValueAtTime(f1, at + decay);
    osc.connect(envelope(at, peak, 0.005, decay));
    osc.start(at);
    osc.stop(at + decay + 0.05);
}

function note(at, freq, len) {
    const osc = ctx.createOscillator();
    osc.type = 'triangle';
    osc.frequency.value = freq;
    osc.connect(envelope(at, 0.35, 0.02, len));
    osc.start(at);
    osc.stop(at + len + 0.05);
}

function shotAt(at) {
    noise(at, { type: 'bandpass', f0: 1800, f1: 400, q: 0.8, peak: 0.9, decay: 0.18 });
    thump(at, { f0: 140, f1: 45, peak: 0.8, decay: 0.2 });
}

function missAt(at) {
    noise(at, { type: 'bandpass', f0: 3000, f1: 500, q: 0.7, peak: 0.45, attack: 0.03, decay: 0.6 });
    noise(at + 0.08, { f0: 1500, f1: 300, peak: 0.25, attack: 0.05, decay: 0.5 });
}

function hitAt(at) {
    noise(at, { f0: 2500, f1: 120, peak: 1, decay: 0.7 });
    thump(at, { f0: 90, f1: 30, peak: 1, decay: 0.6 });
}

function sunkAt(at) {
    hitAt(at);
    noise(at + 0.25, { f0: 1200, f1: 60, peak: 0.9, attack: 0.02, decay: 1.3 });
    thump(at + 0.25, { f0: 70, f1: 25, peak: 0.9, decay: 1.1 });
}

function ready() {
    if (muted || !ctx || ctx.state !== 'running') return false;
    return true;
}

/**
 * Выстрел и его результат: "miss", "hit" или "sunk".
 */
export function play_shot(result) {
    if (!ready()) return;
    const at = ctx.currentTime + 0.01;
    shotAt(at);
    if (result === 'miss') missAt(at + RESULT_DELAY);
    else if (result === 'hit') hitAt(at + RESULT_DELAY);
    else if (result === 'sunk') sunkAt(at + RESULT_DELAY);
}

/**
 * Конец партии: мелодия победы или поражения.
 */
export function play_finish(won) {
    if (!ready()) return;
    const at = ctx.currentTime + 1.2; // после звука последнего взрыва
    const notes = won ? [523, 659, 784, 1047] : [440, 392, 349, 262];
    notes.forEach((f, i) => note(at + i * 0.16, f, i === notes.length - 1 ? 0.6 : 0.18));
}

/**
 * Наступил ваш ход: два коротких тона; delay - пауза, чтобы не наложиться на звук выстрела.
 */
export function play_turn(delay) {
    if (!ready()) return;
    const at = ctx.currentTime + (delay || 0);
    note(at, 880, 0.12);
    note(at + 0.14, 1319, 0.3);
}

export function is_muted() {
    return muted;
}

export function set_muted(value) {
    muted = value;
    try {
        localStorage.setItem(STORE_MUTED, value ? '1' : '0');
    } catch {
        // приватный режим
    }
}
