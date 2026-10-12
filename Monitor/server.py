"""Loopback-only JSON snapshot receiver. Python 3.10+, standard library only."""
import argparse
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
from pathlib import Path
import threading
import time

ROOT = Path(__file__).resolve().parent
MAX_BODY = 2 * 1024 * 1024
STALE_SECONDS = 5


def require(condition, message):
    if not condition:
        raise ValueError(message)


def text(value, limit=160):
    return isinstance(value, str) and len(value) <= limit


def integer(value):
    return type(value) is int and 0 <= value <= 2**53 - 1


def validate(data):
    require(isinstance(data, dict), 'object required')
    require(type(data.get('schema_version')) is int and data['schema_version'] == 1, 'unsupported schema')
    require(data.get('type') == 'world_snapshot', 'unsupported message type')
    for key in ('project_id', 'session_id', 'active_world_id'):
        require(text(data.get(key)) and bool(data[key]), f'invalid {key}')
    require(integer(data.get('sequence')), 'invalid sequence')
    require(type(data.get('paused')) is bool, 'invalid paused')
    worlds = data.get('worlds')
    require(isinstance(worlds, list) and 1 <= len(worlds) <= 64, 'invalid worlds')
    ids = set()
    for world in worlds:
        require(isinstance(world, dict), 'invalid world')
        for key in ('world_id', 'village_id', 'kind', 'name', 'phase'):
            require(text(world.get(key)), f'invalid world {key}')
        require(world['world_id'] and world['world_id'] not in ids, 'duplicate world_id')
        ids.add(world['world_id'])
        require(type(world.get('simulated')) is bool, 'invalid simulated')
        require(world['kind'] in ('village', 'hunting_ground', 'world'), 'invalid world kind')
        require(world['kind'] != 'village' or bool(world['village_id']), 'village_id required')
        counts = world.get('counts')
        require(isinstance(counts, dict), 'counts required')
        for key in ('npc_total', 'npc_active', 'monster_alive', 'monster_total', 'wildlife'):
            require(integer(counts.get(key)), f'invalid count {key}')
        require(counts['monster_alive'] <= counts['monster_total'], 'inconsistent monsters')
        npcs = world.get('npcs')
        require(isinstance(npcs, list) and len(npcs) <= 5000, 'invalid npcs')
        require(counts['npc_total'] == len(npcs), 'npc_total mismatch')
        npc_ids = set()
        for npc in npcs:
            require(isinstance(npc, dict), 'invalid npc')
            for key in ('entity_id', 'name', 'state', 'behavior', 'reaction', 'service'):
                require(text(npc.get(key), 256), f'invalid npc {key}')
            require(npc['entity_id'] and npc['entity_id'] not in npc_ids, 'duplicate entity_id')
            npc_ids.add(npc['entity_id'])
            require(npc['state'] in ('inactive', 'frozen', 'talking', 'walking', 'idle'), 'invalid npc state')
            for key in ('active', 'recorded'):
                require(type(npc.get(key)) is bool, f'invalid npc {key}')
            position = npc.get('position')
            require(isinstance(position, dict), 'position required')
            for key in ('x', 'y'):
                v = position.get(key)
                require(type(v) in (int, float) and math.isfinite(v) and abs(v) <= 1e9, 'invalid coordinate')
        require(counts['npc_active'] == sum(n['active'] for n in npcs), 'npc_active mismatch')
    require(data['active_world_id'] in ids, 'active world missing')
    return data


class Store:
    def __init__(self):
        self.lock = threading.Lock()
        self.sessions = {}

    def receive(self, data):
        validate(data)
        key = (data['project_id'], data['session_id'])
        now = time.monotonic()
        with self.lock:
            self.sessions = {k: v for k, v in self.sessions.items() if now - v[0] < 600}
            prior = self.sessions.get(key)
            if prior and data['sequence'] <= prior[2]['sequence']:
                return 409
            if not prior and len(self.sessions) >= 32:
                return 503
            self.sessions[key] = (now, datetime.now(timezone.utc).isoformat(), data)
        return 202

    def snapshot(self):
        now = time.monotonic()
        with self.lock:
            return {'application': 'gse-monitor', 'schema_version': 1, 'stale_after_ms': 5000,
                    'sessions': [dict(received_at=stamp, age_ms=round((now - tick) * 1000),
                                      stale=now - tick > STALE_SECONDS, snapshot=data)
                                 for tick, stamp, data in self.sessions.values() if now - tick < 600]}


class Server(ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self, port=8765):
        self.store = Store()
        super().__init__(('127.0.0.1', port), Handler)


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(2)

    def log_message(self, *args):
        pass

    def reply(self, status, body, content_type='application/json; charset=utf-8'):
        if not isinstance(body, bytes):
            body = json.dumps(body, ensure_ascii=False, allow_nan=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; frame-ancestors 'none'")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def local_request(self):
        allowed = {f'127.0.0.1:{self.server.server_port}', f'localhost:{self.server.server_port}'}
        return self.headers.get('Host') in allowed and (
            not self.headers.get('Origin') or self.headers['Origin'] in {'http://' + h for h in allowed})

    def do_GET(self):
        if not self.local_request():
            return self.reply(403, {'error': 'local origin required'})
        if self.path == '/api/state':
            return self.reply(200, self.server.store.snapshot())
        assets = {'/': ('index.html', 'text/html'), '/app.js': ('app.js', 'text/javascript'),
                  '/style.css': ('style.css', 'text/css')}
        if self.path not in assets:
            return self.reply(404, {'error': 'not found'})
        filename, mime = assets[self.path]
        self.reply(200, (ROOT / filename).read_bytes(), mime + '; charset=utf-8')

    def do_POST(self):
        if not self.local_request():
            return self.reply(403, {'error': 'local origin required'})
        if self.path != '/api/snapshots':
            return self.reply(404, {'error': 'not found'})
        if self.headers.get_content_type() != 'application/json':
            return self.reply(415, {'error': 'application/json required'})
        try:
            length = int(self.headers.get('Content-Length', '-1'))
            if not 0 < length <= MAX_BODY:
                return self.reply(413, {'error': 'body must be 1..2097152 bytes'})
            body = self.rfile.read(length)
            require(len(body) == length, 'incomplete body')
            data = json.loads(body.decode('utf-8'))
            status = self.server.store.receive(data)
            self.reply(status, {'accepted': status == 202})
        except (ValueError, TypeError, OverflowError, RecursionError, TimeoutError) as error:
            self.reply(400, {'error': str(error)[:200]})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    with Server(args.port) as server:
        print(f'GSE monitor: http://127.0.0.1:{server.server_port}', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()
