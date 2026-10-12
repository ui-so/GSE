import copy
import http.client
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest
from server import Server, Store, validate


def sample():
    return dict(schema_version=1, type='world_snapshot', project_id='test', session_id='one', sequence=1,
                active_world_id='village', paused=False, worlds=[dict(world_id='village', village_id='village',
                kind='village', name='한글 마을', phase='daily', simulated=True,
                counts=dict(npc_total=1, npc_active=1, monster_alive=2, monster_total=3, wildlife=4),
                npcs=[dict(entity_id='1', name='<script>한글</script>', state='idle', behavior='생업',
                           reaction='stay', service='cleric', active=True, recorded=False,
                           position=dict(x=1.5, y=-2))])])


class Tests(unittest.TestCase):
    def test_duplicate_listener(self):
        server = Server(0)
        try:
            with self.assertRaises(OSError):
                duplicate = Server(server.server_port)
                duplicate.server_close()
        finally:
            server.server_close()

    def test_schema(self):
        validate(sample())
        for change in [lambda s:s.update(schema_version=2),
                       lambda s:s['worlds'][0]['counts'].update(npc_total=5),
                       lambda s:s['worlds'][0]['counts'].update(monster_alive=4),
                       lambda s:s['worlds'][0]['npcs'][0]['position'].update(x=float('nan')),
                       lambda s:s['worlds'].append(copy.deepcopy(s['worlds'][0]))]:
            value=sample();change(value)
            with self.assertRaises(ValueError):validate(value)

    def test_sessions_order_and_stale(self):
        store=Store();data=sample()
        self.assertEqual(store.receive(data),202)
        self.assertEqual(store.receive(data),409)
        other=copy.deepcopy(data);other['project_id']='other-project'
        self.assertEqual(store.receive(other),202)
        self.assertEqual(len(store.snapshot()['sessions']),2)
        key=('test','one');tick,stamp,body=store.sessions[key]
        store.sessions[key]=(tick-6,stamp,body)
        self.assertTrue(store.snapshot()['sessions'][0]['stale'])
        newer=copy.deepcopy(data);newer['sequence']=2;newer['worlds'][0]['phase']='warning'
        self.assertEqual(store.receive(newer),202)
        self.assertFalse(store.snapshot()['sessions'][0]['stale'])
        self.assertEqual(store.snapshot()['sessions'][0]['snapshot']['worlds'][0]['phase'],'warning')

    def test_http(self):
        server=Server(0);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            def request(method,path,body=None,headers=None):
                c=http.client.HTTPConnection('127.0.0.1',server.server_port,timeout=2)
                c.request(method,path,body,headers or {});r=c.getresponse();result=r.status,r.read();c.close();return result
            status,body=request('POST','/api/snapshots',json.dumps(sample()).encode(),{'Content-Type':'application/json'})
            self.assertEqual(status,202)
            self.assertEqual(request('POST','/api/snapshots','{}',{'Content-Type':'text/plain'})[0],415)
            self.assertEqual(request('POST','/api/snapshots','{',{'Content-Type':'application/json'})[0],400)
            self.assertEqual(request('GET','/api/state',headers={'Origin':'https://external.invalid'})[0],403)
            self.assertEqual(request('GET','/../server.py')[0],404)
            self.assertEqual(request('POST','/api/snapshots','{}',{'Content-Type':'application/json','Content-Length':'3000000'})[0],413)
            self.assertEqual(request('GET','/api/state',headers={'Host':'external.invalid'})[0],403)
            self.assertIn('한글 마을',request('GET','/api/state')[1].decode())
            self.assertIn('월드 모니터',request('GET','/')[1].decode())
        finally:server.shutdown();server.server_close();thread.join()


def integration(exe, output):
    output.mkdir(parents=True,exist_ok=True)
    server=Server(8765)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory() as cwd:
            process=subprocess.Popen([str(exe),'--monitor-test'],cwd=cwd)
            deadline=time.monotonic()+45;messages={}
            try:
                while time.monotonic()<deadline:
                    state=server.store.snapshot()
                    for session in state['sessions']:
                        snapshot=session['snapshot'];messages[snapshot['sequence']]=snapshot
                    if process.poll() is not None:break
                    time.sleep(.1)
                if process.poll() is None:raise AssertionError('producer timeout')
                assert process.returncode==0,process.returncode
            finally:
                if process.poll() is None:process.kill();process.wait()
        assert len(messages)>=8, len(messages)
        phases={m['worlds'][0]['phase'] for m in messages.values()}
        assert phases=={'daily','warning','evacuation','stillness'},phases
        assert any(m['active_world_id']=='first_level' for m in messages.values())
        assert all(not m['worlds'][0]['simulated'] for m in messages.values() if m['active_world_id']=='first_level')
        for m in messages.values():
            validate(m);v=m['worlds'][0]
            assert v['counts']['npc_total']==36 and v['counts']['monster_alive']==18
            assert len(v['npcs'])==36 and v['counts']['wildlife']==15
        assert any(n['state']=='frozen' for m in messages.values() for n in m['worlds'][0]['npcs'])
        (output/'received-snapshots.json').write_text(json.dumps(list(messages.values()),ensure_ascii=False,indent=2),encoding='utf-8')
        print(f'PASS: {len(messages)} real C++ snapshots, 36 NPCs, 18 mobs, 15 wildlife, four phases and world switch')
    finally:server.shutdown();server.server_close();thread.join()


def reconnect(exe, output):
    output.mkdir(parents=True,exist_ok=True)
    server=None;thread=None
    with tempfile.TemporaryDirectory() as cwd:
        process=subprocess.Popen([str(exe),'--monitor-test'],cwd=cwd)
        try:
            time.sleep(3)
            server=Server(8765)
            thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
            deadline=time.monotonic()+15;first=None
            while time.monotonic()<deadline:
                sessions=server.store.snapshot()['sessions']
                if sessions:first=sessions[0]['snapshot'];break
                time.sleep(.1)
            assert first and first['sequence']>1, 'late server recovery failed'
            server.shutdown();server.server_close();thread.join();server=None
            time.sleep(.7)
            server=Server(8765)
            thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
            process.wait(timeout=20)
            snapshots=server.store.snapshot()['sessions']
            assert process.returncode==0 and snapshots, 'server restart recovery failed'
            last=snapshots[0]['snapshot']
            assert last['sequence']>first['sequence']
            (output/'reconnect-report.txt').write_text(
                f"PASS: late server received sequence {first['sequence']}; restarted server received {last['sequence']}.\n",encoding='utf-8')
            print((output/'reconnect-report.txt').read_text())
        finally:
            if process.poll() is None:process.kill();process.wait()
            if server:server.shutdown();server.server_close();thread.join()


if __name__=='__main__':
    import sys
    if len(sys.argv)>1 and sys.argv[1]=='--reconnect':
        reconnect(Path(sys.argv[2]).resolve(),Path(sys.argv[3]).resolve())
    elif len(sys.argv)>1 and sys.argv[1]=='--integration':
        integration(Path(sys.argv[2]).resolve(),Path(sys.argv[3]).resolve())
    else:unittest.main()
