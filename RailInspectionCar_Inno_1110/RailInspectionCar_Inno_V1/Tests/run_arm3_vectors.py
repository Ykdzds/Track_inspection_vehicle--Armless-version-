"""Compare external Python golden vectors against actual C frame encoding."""
import json
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
vectors = json.loads((root.parent / 'RK3588_ARM3_20261106/golden_vectors_arm3_r2.json').read_text())
compiler = r'D:/Dev-C++软件--蓝桥杯比赛环境/Dev-Cpp/MinGW64/bin/gcc.exe'
with tempfile.TemporaryDirectory(prefix='arm3_vectors_') as temp:
    exe = str(Path(temp) / 'vectors.exe')
    subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-IProtocol/CAN',
                    'Tests/test_arm3_vectors.c', 'Protocol/CAN/can_protocol.c', 'Protocol/CAN/can_arm3.c', '-o', exe], cwd=root, check=True)
    for seq, vector in enumerate(vectors, 1):
        result = subprocess.run([exe, str(vector['kind']), str(seq), '66', vector['payload']], capture_output=True, text=True, check=True)
        actual = [dict(id=int(line.split()[0]), data=line.split()[1]) for line in result.stdout.splitlines()]
        base = vector['frames'][0]['id'] - 0x300
        expected = [dict(id=f['id']-base, data=f['data']) for f in vector['frames']]
        assert actual == expected, vector['name']
        print('PASS C/Python actual frame/CRC/manifest vector:', vector['name'])
print('PASS', len(vectors), 'bidirectional golden vectors; host only')
