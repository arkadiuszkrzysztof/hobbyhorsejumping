import socket
import time

from timings import link

LOCAL = '127.0.0.1'


def wait_for(predicate, timeout=5.0, interval=0.005, msg='condition'):
    """Poll `predicate` until truthy or the deadline passes (no fixed sleeps)."""
    deadline = time.monotonic() + timeout
    while True:
        value = predicate()
        if value:
            return value
        if time.monotonic() > deadline:
            raise AssertionError(f'timed out waiting for {msg}')
        time.sleep(interval)


class Client:
    """A throwaway UDP socket that plays a node."""

    def __init__(self, server_port):
        self.dest = (LOCAL, server_port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((LOCAL, 0))
        self.port = self.sock.getsockname()[1]

    def send(self, payload):
        if not isinstance(payload, bytes):
            payload = link.encode(payload)
        self.sock.sendto(payload, self.dest)

    def recv(self, timeout=5.0):
        """Next datagram as (decoded, raw, addr)."""
        self.sock.settimeout(timeout)
        data, addr = self.sock.recvfrom(4096)
        return link.decode(data), data, addr

    def barrier(self, timeout=5.0):
        """Send a HELLO and return everything received up to its reply.

        The server handles datagrams in order on one thread, so anything it
        would have answered before this HELLO arrives before the reply.
        """
        self.send(link.Hello(role='TIMER', boot=999))
        before = []
        while True:
            msg, raw, addr = self.recv(timeout)
            if isinstance(msg, link.Hello) and msg.role == 'BACKEND':
                return before
            before.append(msg)

    def close(self):
        self.sock.close()


def event(mark='START', boot=1, seq=1, age_ms=0, **kw):
    return link.Event(mark=mark, age_ms=age_ms, boot=boot, seq=seq, **kw)


# The scoring rules a competition gets when none are given (API.md).
USUAL_RULES = {'time_allowed': None, 'knockdown_points': 4.0, 'refusal_points': 4.0, 'overtime_points': 0.4}


def run_row(points=0.0, time_points=0.0, **fields):
    """A run as the state lists it: `fields` plus the defaults of everything else."""
    return {'refusals': {}, 'outcome': 'FINISHED', 'outcome_fence': '',
            'fence_points': points - time_points, 'time_points': time_points, 'points': points, **fields}
