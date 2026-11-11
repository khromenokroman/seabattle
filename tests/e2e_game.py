#!/usr/bin/env python3
"""Сквозная проверка: партия двух клиентов по HTTP против живого сервера.

Запуск: python3 tests/e2e_game.py <путь к seabattle> <каталог web>

Скрипт сам поднимает сервер на свободном порту, ждёт /api/version, гоняет сценарий
протокола (docs/protocol.md): комната, потоки событий, расстановка, бой до конца,
реванш, сдача и выход. В конце гасит сервер (SIGTERM) и проверяет, что тот завершился
с кодом 0 за 5 секунд. Только стандартная библиотека Python.
"""

import json
import random
import signal
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

STARTUP_TIMEOUT = 10
STEP_TIMEOUT = 10
SHOTS_LIMIT = 200

BASE = ''  # выставляется в main()


class Failure(Exception):
    pass


def check(cond, message):
    if not cond:
        raise Failure(message)


def http(method, path, body=None):
    data = None if body is None else json.dumps(body).encode('utf-8')
    req = urllib.request.Request(BASE + path, data=data, method=method,
                                 headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req, timeout=STEP_TIMEOUT) as res:
            return res.status, json.loads(res.read().decode('utf-8'))
    except urllib.error.HTTPError as e:
        try:
            payload = json.loads(e.read().decode('utf-8'))
        except Exception:
            payload = {}
        return e.code, payload


class Client:
    """Игрок: HTTP-действия и фоновое чтение своего потока событий."""

    def __init__(self, name):
        self.name = name
        self.token = None
        self.room = None
        self.seat = None
        self._cond = threading.Condition()
        self._gen = 0
        self._latest = None

    def _joined(self, data):
        self.room = data['room']
        self.token = data['token']
        self.seat = data['player']

    def create_room(self):
        status, data = http('POST', '/api/rooms', {'name': self.name})
        check(status == 200, f'{self.name}: создание комнаты вернуло {status}: {data}')
        self._joined(data)

    def join_room_raw(self, code):
        return http('POST', f'/api/rooms/{code}/join', {'name': self.name})

    def join_room(self, code):
        status, data = self.join_room_raw(code)
        check(status == 200, f'{self.name}: вход в комнату {code} вернул {status}: {data}')
        self._joined(data)

    def me(self):
        return http('GET', f'/api/me?token={self.token}')

    def place(self, ships):
        return http('POST', '/api/place', {'token': self.token, 'ships': ships})

    def fire(self, x, y):
        return http('POST', '/api/fire', {'token': self.token, 'x': x, 'y': y})

    def resign(self):
        return http('POST', '/api/resign', {'token': self.token})

    def rematch(self):
        return http('POST', '/api/rematch', {'token': self.token})

    def leave(self):
        return http('POST', '/api/leave', {'token': self.token})

    # ---- поток событий ----

    @property
    def latest(self):
        with self._cond:
            return self._latest

    def open_events(self):
        threading.Thread(target=self._events, daemon=True).start()

    def wait_next(self, gen, timeout=STEP_TIMEOUT):
        deadline = time.monotonic() + timeout
        with self._cond:
            while self._gen <= gen:
                left = deadline - time.monotonic()
                check(left > 0, f'{self.name}: нет нового снимка за {timeout} с')
                self._cond.wait(left)
            return self._latest, self._gen

    def _events(self):
        url = BASE + '/api/events?token=' + self.token
        req = urllib.request.Request(url, headers={'Accept': 'text/event-stream'})
        try:
            with urllib.request.urlopen(req, timeout=STEP_TIMEOUT * 3) as res:
                event, data = None, []
                for raw in res:
                    line = raw.decode('utf-8', 'replace').rstrip('\r\n')
                    if not line:
                        if event == 'state' and data:
                            state = json.loads('\n'.join(data))
                            with self._cond:
                                self._gen += 1
                                self._latest = state
                                self._cond.notify_all()
                        event, data = None, []
                    elif line.startswith('event:'):
                        event = line[len('event:'):].strip()
                    elif line.startswith('data:'):
                        data.append(line[len('data:'):].lstrip())
                    # строки-комментарии (: ping) и прочее игнорируем
        except Exception:
            pass  # сервер остановлен или поток закрыт


# Фиксированный правильный флот: 4,3,3,2,2,2,1,1,1,1, без касаний.
FIXED_FLEET = [
    {'x': 0, 'y': 0, 'len': 4, 'dir': 'h'},
    {'x': 0, 'y': 2, 'len': 3, 'dir': 'h'},
    {'x': 5, 'y': 2, 'len': 3, 'dir': 'h'},
    {'x': 0, 'y': 5, 'len': 2, 'dir': 'h'},
    {'x': 3, 'y': 5, 'len': 2, 'dir': 'h'},
    {'x': 6, 'y': 5, 'len': 2, 'dir': 'h'},
    {'x': 0, 'y': 8, 'len': 1, 'dir': 'h'},
    {'x': 2, 'y': 8, 'len': 1, 'dir': 'h'},
    {'x': 4, 'y': 8, 'len': 1, 'dir': 'h'},
    {'x': 6, 'y': 8, 'len': 1, 'dir': 'h'},
]

# Два первых корабля пересекаются: расстановка не по правилам, ждём 400.
INVALID_FLEET = [
    {'x': 0, 'y': 0, 'len': 4, 'dir': 'h'},
    {'x': 1, 'y': 0, 'len': 3, 'dir': 'h'},
] + FIXED_FLEET[2:]


def free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


def wait_server(proc):
    deadline = time.monotonic() + STARTUP_TIMEOUT
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise Failure(f'сервер завершился сразу после запуска с кодом {proc.returncode}')
        try:
            status, _ = http('GET', '/api/version')
            if status == 200:
                return
        except Exception:
            pass
        time.sleep(0.05)
    raise Failure(f'сервер не ответил на /api/version за {STARTUP_TIMEOUT} с')


def check_no_leak(state, player, label):
    if state['phase'] != 'finished':
        check(state['enemy']['ships'] == [],
              f'{label}: утечка кораблей противника у {player.name} в фазе {state["phase"]}')


def choose_target(state, rng):
    """Клетка для выстрела только по тому, что видно в снимке: enemy.shots и enemy.sunk.
    После попадания добиваем соседние клетки, иначе случайная непростреленная."""
    shots = {(s['x'], s['y']): s['hit'] for s in state['enemy']['shots']}
    sunk_cells = set()
    for ship in state['enemy']['sunk']:
        dx, dy = (1, 0) if ship['dir'] == 'h' else (0, 1)
        for i in range(ship['len']):
            sunk_cells.add((ship['x'] + i * dx, ship['y'] + i * dy))
    hits = {cell for cell, hit in shots.items() if hit and cell not in sunk_cells}
    candidates = []
    for x, y in hits:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            cell = (x + dx, y + dy)
            if 0 <= cell[0] < 10 and 0 <= cell[1] < 10 and cell not in shots and cell not in candidates:
                candidates.append(cell)
    if candidates:
        return candidates[0]
    free = [(x, y) for y in range(10) for x in range(10) if (x, y) not in shots]
    check(free, 'нет свободных клеток для выстрела, а партия не закончилась')
    return rng.choice(free)


def play_battle(a, b, gen_a, gen_b, label):
    """Партия до победы. Возвращает (победитель, проигравший, ген_a, ген_b)."""
    gens = {id(a): gen_a, id(b): gen_b}
    rng = random.Random(20261111)
    shots = 0
    while True:
        for p in (a, b):
            check(p.latest is not None, f'{label}: у {p.name} нет ни одного снимка')
        if a.latest['phase'] == 'finished' or b.latest['phase'] == 'finished':
            for p in (a, b):
                st = p.latest
                check(st['phase'] == 'finished',
                      f'{label}: у {p.name} фаза {st["phase"]}, а у другого finished')
                check(len(st['enemy']['ships']) == 10,
                      f'{label}: у {p.name} в финале {len(st["enemy"]["ships"])} кораблей противника вместо 10')
                check(st['last_shot'] is not None and st['last_shot']['result'] == 'sunk',
                      f'{label}: последний выстрел у {p.name} не потопление: {st["last_shot"]}')
            winner = a if a.latest['winner'] == 'you' else b
            check((a.latest['winner'] == 'you') != (b.latest['winner'] == 'you'),
                  f'{label}: победитель не сходится у игроков: {a.latest["winner"]} / {b.latest["winner"]}')
            return winner, b if winner is a else a, gens[id(a)], gens[id(b)]

        turn = a.latest['turn']
        check(turn in ('you', 'opponent'), f'{label}: у {a.name} некорректный turn: {turn}')
        shooter, victim = (a, b) if turn == 'you' else (b, a)
        check(shooter.latest['turn'] == 'you', f'{label}: ход не за {shooter.name}')

        x, y = choose_target(shooter.latest, rng)
        status, data = shooter.fire(x, y)
        check(status == 200, f'{label}: выстрел {shooter.name} ({x},{y}) вернул {status}: {data}')
        result = data.get('result')
        check(result in ('miss', 'hit', 'sunk'), f'{label}: неожиданный результат выстрела: {data}')
        shots += 1
        check(shots <= SHOTS_LIMIT, f'{label}: партия не закончилась за {SHOTS_LIMIT} выстрелов')

        for p in (a, b):
            state, gens[id(p)] = p.wait_next(gens[id(p)])
            check_no_leak(state, p, label)
            shot = state['last_shot']
            who = 'you' if p is shooter else 'opponent'
            check(shot is not None and shot['by'] == who and shot['x'] == x and shot['y'] == y
                  and shot['result'] == result,
                  f'{label}: last_shot у {p.name} = {shot}, ждали by={who} ({x},{y}) result={result}')
            expected = 'you' if (p is shooter) == (result != 'miss') else 'opponent'
            check(state['turn'] == expected or state['phase'] == 'finished',
                  f'{label}: turn у {p.name} = {state["turn"]}, ждали {expected} после {result}')


def place_both(a, ships_a, b, ships_b, label):
    status, data = a.place(ships_a)
    check(status == 200, f'{label}: расстановка {a.name} вернула {status}: {data}')
    status, data = b.place(ships_b)
    check(status == 200, f'{label}: расстановка {b.name} вернула {status}: {data}')


def wait_playing(player, gen, label):
    state, gen = player.wait_next(gen)
    while not (state['phase'] == 'playing' and state['placed']['you'] and state['placed']['opponent']):
        state, gen = player.wait_next(gen)
    return gen


def wait_until(player, gen, pred, message, tries=20):
    """Ждать снимок, удовлетворяющий предикату, пропуская промежуточные."""
    for _ in range(tries):
        state, gen = player.wait_next(gen)
        if pred(state):
            return state, gen
    raise Failure(message)


def scenario():
    # комната: создание, поток первого игрока, вход второго по коду в нижнем регистре
    a = Client('Алиса')
    a.create_room()
    check(len(a.room) == 4, f'код комнаты "{a.room}" не из 4 символов')
    b = Client('Борис')

    # потоки событий: первый снимок сразу, opponent.connected у обоих после подключения
    a.open_events()
    st, gen_a = a.wait_next(0)
    check(st['phase'] == 'waiting' and st['opponent'] is None,
          f'первый снимок {a.name}: фаза {st["phase"]}, opponent {st["opponent"]}')
    b.join_room(a.room.lower())
    check(b.room == a.room, f'код после входа в нижнем регистре: {b.room} вместо {a.room}')
    b.open_events()
    st, gen_b = wait_until(b, 0, lambda s: bool(s['opponent'] and s['opponent']['connected']),
                           f'{b.name}: opponent.connected != true после подключения {a.name}')
    st, gen_a = wait_until(a, gen_a, lambda s: bool(s['opponent'] and s['opponent']['connected']),
                           f'{a.name}: opponent.connected != true после подключения {b.name}')
    check(st['phase'] == 'placing', f'фаза после входа второго: {st["phase"]} вместо placing')
    c = Client('Вера')
    status, data = c.join_room_raw(a.room)
    check(status == 409, f'третий игрок: ожидали 409, получили {status}: {data}')

    # расстановка: один случайный флот, другой фиксированный, неправильный - 400
    status, data = http('GET', '/api/random-fleet')
    check(status == 200 and len(data.get('ships', [])) == 10,
          f'/api/random-fleet вернул {status}: {data}')
    random_fleet = data['ships']
    status, data = a.place(INVALID_FLEET)
    check(status == 400, f'неправильный флот: ожидали 400, получили {status}: {data}')
    place_both(a, random_fleet, b, FIXED_FLEET, 'расстановка')
    gen_a = wait_playing(a, gen_a, 'расстановка')
    gen_b = wait_playing(b, gen_b, 'расстановка')
    check(a.latest['turn'] in ('you', 'opponent'), f'нет хода в начале партии: {a.latest["turn"]}')

    # бой до конца
    winner, loser, gen_a, gen_b = play_battle(a, b, gen_a, gen_b, 'первая партия')
    print(f'  первая партия: победил {winner.name} за '
          f'{len(winner.latest["enemy"]["shots"])} отмеченных клеток')

    # реванш: оба согласны -> placing, первым ходит проигравший
    gens = {id(a): gen_a, id(b): gen_b}
    status, data = winner.rematch()
    check(status == 200, f'rematch {winner.name} вернул {status}: {data}')
    for p in (a, b):
        st, gens[id(p)] = p.wait_next(gens[id(p)])
        if p is loser:
            check(st['phase'] == 'finished' and st['rematch']['opponent'] is True,
                  f'{loser.name} не видит предложения реванша: {st["rematch"]}')
        else:
            check(st['rematch']['you'] is True, f'{winner.name} не видит своего реванша: {st["rematch"]}')
    status, data = loser.rematch()
    check(status == 200, f'rematch {loser.name} вернул {status}: {data}')
    for p in (a, b):
        st, gens[id(p)] = p.wait_next(gens[id(p)])
        check(st['phase'] == 'placing' and st['winner'] is None and st['placed']['you'] is False,
              f'после реванша у {p.name} фаза {st["phase"]}')
    gen_a, gen_b = gens[id(a)], gens[id(b)]

    place_both(loser, FIXED_FLEET, winner, FIXED_FLEET, 'вторая партия')
    gen_loser = wait_playing(loser, gen_b if loser is b else gen_a, 'вторая партия')
    gen_winner = wait_playing(winner, gen_a if winner is a else gen_b, 'вторая партия')
    gen_a, gen_b = (gen_loser, gen_winner) if loser is a else (gen_winner, gen_loser)
    check(loser.latest['turn'] == 'you', 'первым ходит не проигравший прошлой партии')
    check(winner.latest['turn'] == 'opponent', 'у победителя прошлой партии не ход противника')

    # сдача во второй партии
    status, data = winner.resign()
    check(status == 200, f'сдача {winner.name} вернула {status}: {data}')
    st, gen_loser = loser.wait_next(gen_b if loser is b else gen_a)
    check(st['phase'] == 'finished' and st['winner'] == 'you' and st['resigned'] is True,
          f'{loser.name} не видит победы по сдаче: winner={st["winner"]} resigned={st["resigned"]}')
    st, gen_winner = winner.wait_next(gen_a if winner is a else gen_b)
    check(st['phase'] == 'finished' and st['winner'] == 'opponent' and st['resigned'] is True,
          f'{winner.name} не видит своего поражения по сдаче: winner={st["winner"]}')

    # выход одного: у второго waiting и opponent == null, токен вышедшего - 401
    status, data = winner.leave()
    check(status == 200, f'выход {winner.name} вернул {status}: {data}')
    st, _ = loser.wait_next(gen_loser)
    check(st['phase'] == 'waiting' and st['opponent'] is None,
          f'после выхода у {loser.name}: фаза {st["phase"]}, opponent {st["opponent"]}')
    status, _ = winner.me()
    check(status == 401, f'токен вышедшего: ожидали 401, получили {status}')


def main():
    global BASE
    if len(sys.argv) != 3:
        print(f'использование: {sys.argv[0]} <путь к seabattle> <каталог web>', file=sys.stderr)
        return 2
    binary, web = sys.argv[1], sys.argv[2]
    BASE = f'http://127.0.0.1:{free_port()}'
    proc = subprocess.Popen([binary, '--host', '127.0.0.1', '--port', BASE.rsplit(':', 1)[1],
                             '--web', web],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    code = 1
    try:
        wait_server(proc)
        scenario()
        print('e2e_game: все проверки прошли')
        code = 0
    except Failure as e:
        print(f'ОШИБКА: {e}', file=sys.stderr)
    except Exception as e:
        print(f'ОШИБКА: неожиданное исключение: {e!r}', file=sys.stderr)
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            rc = proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            rc = proc.wait()
            print('ОШИБКА: сервер не завершился за 5 с после SIGTERM', file=sys.stderr)
            code = 1
        if rc != 0:
            print(f'ОШИБКА: сервер завершился с кодом {rc} вместо 0', file=sys.stderr)
            code = 1
    return code


if __name__ == '__main__':
    sys.exit(main())
