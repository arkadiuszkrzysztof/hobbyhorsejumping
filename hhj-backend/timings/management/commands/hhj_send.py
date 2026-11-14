"""Play a gate without hardware: send one conformant EVENT burst over UDP."""
import secrets
import socket
import time

from django.core.management.base import BaseCommand, CommandError

from timings import link

BURST_MS = (0, 10, 20, 40, 80, 160, 320)  # PROTOCOL.md fast burst


class Command(BaseCommand):
    help = 'Send one HHJ link EVENT (with a HELLO first) and print HELLO/ACK replies.'

    def add_arguments(self, parser):
        parser.add_argument('mark', choices=link.GATE_ROLES)
        parser.add_argument('--to', default='255.255.255.255')
        parser.add_argument('--port', type=int, default=3004)
        parser.add_argument('--boot', type=int, default=None)
        parser.add_argument('--seq', type=int, default=1)
        parser.add_argument('--age', type=int, default=0, help='age_ms of the cut at the first copy')
        parser.add_argument('--wait', type=float, default=1.0, help='seconds to listen for replies')

    def handle(self, *args, mark, to, port, boot, seq, age, wait, **opts):
        boot = secrets.randbits(32) if boot is None else boot
        if not 0 <= boot <= link.U32 or not 1 <= seq <= link.U32 or not 0 <= age <= link.U32:
            raise CommandError('--boot 0..2^32-1, --seq 1..2^32-1, --age 0..2^32-1')
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.bind(('0.0.0.0', 0))
        dest = (to, port)
        t0 = time.monotonic()

        def elapsed_ms():
            return int((time.monotonic() - t0) * 1000)

        def drain(until):
            while True:
                left = until - time.monotonic()
                if left <= 0:
                    return
                sock.settimeout(left)
                try:
                    data, addr = sock.recvfrom(2048)
                except socket.timeout:
                    return
                msg = link.decode(data)
                if isinstance(msg, (link.Hello, link.Ack)):
                    self.stdout.write(f'{addr[0]}:{addr[1]} {type(msg).__name__.upper()} {data.decode()}')

        try:
            sock.sendto(link.encode_hello(link.Hello(role=mark, boot=boot, up_ms=elapsed_ms(), fw='hhj-send',
                                                     st=24, seq=seq - 1)), dest)
            for n, at in enumerate(BURST_MS):
                drain(t0 + at / 1000)
                ev = link.Event(mark=mark, age_ms=age + elapsed_ms(), boot=boot, seq=seq, n=n)
                sock.sendto(link.encode_event(ev), dest)
            self.stdout.write(f'sent {mark} boot={boot} seq={seq} x{len(BURST_MS)} to {to}:{port}')
            drain(time.monotonic() + wait)
        finally:
            sock.close()
