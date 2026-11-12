// Драйвер seabattle в headless Chrome по CDP (--remote-debugging-pipe), без npm-пакетов.
//   node drive.js <url> <scenario.js>
// Сценарий - тело async-функции в странице с помощниками:
//   sleep(ms), btn(текст), expect(условие, текст), log (массив строк), shot(имя)
// и строковыми константами PHASE и ROOM из окружения.
// Код возврата 1, если сценарий бросил исключение или не завершился вовремя.
// Окружение:
//   CHROME - браузер (по умолчанию google-chrome)
//   LOCAL_STORAGE_JSON - значение localStorage['seabattle.session'] до загрузки страницы
//   WIDTH, HEIGHT - размеры окна (по умолчанию 1280x900)
//   SHOTS - каталог для скриншотов из shot(); без него скриншоты не снимаются
//   E2E_TIMEOUT - предел сценария в секундах (по умолчанию 30)
const {spawn} = require('child_process');
const fs = require('fs');
const path = require('path');

const [url, scenarioFile] = process.argv.slice(2);
if (!url || !scenarioFile) {
    console.error('использование: node drive.js <url> <scenario.js>');
    process.exit(2);
}
const scenario = fs.readFileSync(scenarioFile, 'utf8');
const width = Number(process.env.WIDTH || 1280);
const height = Number(process.env.HEIGHT || 900);
const shotsDir = process.env.SHOTS || '';
const profile = fs.mkdtempSync('/tmp/seabattle-chrome-');

const chrome = spawn(process.env.CHROME || 'google-chrome',
    ['--headless=new', '--disable-gpu', '--no-sandbox', '--remote-debugging-pipe',
        '--user-data-dir=' + profile, 'about:blank'],
    {stdio: ['ignore', 'ignore', 'ignore', 'pipe', 'pipe']});
const out = chrome.stdio[3], inp = chrome.stdio[4];
let buf = '', nextId = 1;
const waiting = new Map();
const events = new Map(); // имя события CDP -> обработчик
inp.on('data', d => {
    buf += d.toString();
    let i;
    while ((i = buf.indexOf('\0')) >= 0) {
        const msg = JSON.parse(buf.slice(0, i));
        buf = buf.slice(i + 1);
        if (msg.id && waiting.has(msg.id)) {
            waiting.get(msg.id)(msg);
            waiting.delete(msg.id);
        } else if (msg.method && events.has(msg.method)) {
            events.get(msg.method)(msg);
        }
    }
});
const send = (method, params = {}, sessionId) => new Promise((resolve, reject) => {
    const id = nextId++;
    waiting.set(id, msg => (msg.error ? reject(new Error(method + ': ' + JSON.stringify(msg.error))) : resolve(msg)));
    out.write(JSON.stringify({id, method, params, sessionId}) + '\0');
});

function cleanup(code) {
    clearInterval(shotPoll);
    clearTimeout(timer);
    try {
        chrome.kill();
    } catch (e) { /* уже завершился */ }
    try {
        fs.rmSync(profile, {recursive: true, force: true});
    } catch (e) { /* каталог занят */ }
    process.exit(code);
}

let shotPoll = 0, timer = 0;

(async () => {
    const {result: {targetId}} = await send('Target.createTarget', {url: 'about:blank'});
    const {result: {sessionId}} = await send('Target.attachToTarget', {targetId, flatten: true});
    await send('Runtime.enable', {}, sessionId);
    await send('Page.enable', {}, sessionId);
    if (process.env.LOCAL_STORAGE_JSON) {
        const value = JSON.stringify(process.env.LOCAL_STORAGE_JSON);
        await send('Page.addScriptToEvaluateOnNewDocument',
            {source: `try { localStorage.setItem('seabattle.session', ${value}); } catch (e) {}`}, sessionId);
    }
    await send('Emulation.setDeviceMetricsOverride',
        {width, height, deviceScaleFactor: 1, mobile: width < 500}, sessionId);
    // Page.navigate отвечает до загрузки: без ожидания load сценарий может выполниться в about:blank
    const loaded = new Promise((resolve) => events.set('Page.loadEventFired', resolve));
    await send('Page.navigate', {url}, sessionId);
    await loaded;
    events.delete('Page.loadEventFired');

    const limit = Number(process.env.E2E_TIMEOUT || 30);
    timer = setTimeout(() => {
        console.log(`ОШИБКА: сценарий не завершился за ${limit} с`);
        cleanup(1);
    }, limit * 1000);

    // скриншоты: страница просит через window.__shotRequest, драйвер снимает и сохраняет
    let lastShot = '';
    if (shotsDir) {
        shotPoll = setInterval(async () => {
            try {
                const r = await send('Runtime.evaluate',
                    {expression: 'window.__shotRequest || ""', returnByValue: true}, sessionId);
                const name = r.result && r.result.result ? r.result.result.value : '';
                if (!name || name === lastShot) return;
                const shot = await send('Page.captureScreenshot', {format: 'png'}, sessionId);
                if (shot.result && shot.result.data) {
                    const safe = String(name).replace(/[^A-Za-z0-9_-]/g, '_') + '.png';
                    fs.writeFileSync(path.join(shotsDir, safe), Buffer.from(shot.result.data, 'base64'));
                    lastShot = name;
                }
                await send('Runtime.evaluate', {expression: 'window.__shotRequest = ""'}, sessionId);
            } catch (e) {
                // страница занята или уничтожена - пропускаем тик
            }
        }, 100);
    }

    const r = await send('Runtime.evaluate', {
        expression: `(async () => {
            const sleep = ms => new Promise(r => setTimeout(r, ms));
            const btn = t => [...document.querySelectorAll('button')].find(b => b.textContent.trim() === t);
            const expect = (cond, text) => { if (!cond) throw new Error(text); };
            const log = [];
            const shot = name => new Promise(res => {
                window.__shotRequest = name;
                const t = setInterval(() => { if (!window.__shotRequest) { clearInterval(t); res(); } }, 50);
                setTimeout(() => { clearInterval(t); res(); }, 3000);
            });
            const PHASE = ${JSON.stringify(process.env.PHASE || '')};
            const ROOM = ${JSON.stringify(process.env.ROOM || '')};
            try { ${scenario} } catch (e) { return {ok: false, error: e.message, log}; }
            return {ok: true, log}; })()`,
        awaitPromise: true, returnByValue: true}, sessionId);

    const value = r.result && r.result.result ? r.result.result.value : null;
    if (!value) {
        console.log('сценарий не выполнился: ' + JSON.stringify(r));
        cleanup(1);
    }
    (value.log || []).forEach(line => console.log('    ' + line));
    if (!value.ok) console.log('    ОШИБКА: ' + value.error);
    cleanup(value.ok ? 0 : 1);
})().catch(e => {
    console.log('ОШИБКА драйвера: ' + e.message);
    cleanup(1);
});
