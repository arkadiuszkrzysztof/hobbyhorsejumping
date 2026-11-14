"""In-memory registry of nodes seen via HELLO (keyed by role + IP)."""
import threading
import time

from .link import GATE_ROLES, Hello, gate_flags

ONLINE_S = 7.0  # PROTOCOL.md: a node not heard from for 7 s is gone


class DeviceRegistry:
    def __init__(self, clock=time.monotonic):
        self._clock = clock
        self._lock = threading.Lock()
        self._nodes = {}

    def seen(self, hello: Hello, ip: str, now=None):
        now = self._clock() if now is None else now
        key = (hello.role, ip)
        with self._lock:
            node = self._nodes.get(key)
            if node is None:
                node = self._nodes[key] = {'first_seen': now, 'reboots': 0, 'hellos': 0}
            elif node['hello'].boot != hello.boot:
                node['reboots'] += 1
            node['hello'] = hello
            node['last_seen'] = now
            node['hellos'] += 1

    def clear(self):
        with self._lock:
            self._nodes.clear()

    def snapshot(self, now=None):
        now = self._clock() if now is None else now
        with self._lock:
            items = sorted(self._nodes.items())
        out = []
        for (role, ip), node in items:
            h = node['hello']
            age = max(0.0, now - node['last_seen'])
            entry = {
                'role': role, 'ip': ip, 'boot': h.boot, 'fw': h.fw, 'st': h.st,
                'seq': h.seq, 'pending': h.pending, 'ms': h.ms, 'rssi': h.rssi,
                'net': h.net, 'up_ms': h.up_ms,
                'age_s': round(age, 3), 'online': age < ONLINE_S,
                'reboots': node['reboots'], 'hellos': node['hellos'],
            }
            if role in GATE_ROLES:
                entry.update(gate_flags(h.st))
            out.append(entry)
        return out


registry = DeviceRegistry()
