"""UDP side of the HHJ link protocol (hhj-common/PROTOCOL.md).

Two daemon threads:
  - the receiver only does recvfrom() and stamps the arrival time. Nothing it
    does can take long, so a datagram is never stamped late because the
    previous one was still being written to the database — and the arrival
    time IS the measurement (edge = arrival - age_ms);
  - the worker takes the stamped datagrams in order, answers HELLOs with the
    backend's own HELLO (unicast to the source address and port), stores
    EVENTs and acknowledges them once they are in the database.
"""
import logging
import queue
import secrets
import socket
import sys
import threading
import time

from django.db import close_old_connections, connection

from . import link, live
from .devices import registry as default_registry

log = logging.getLogger(__name__)

BACKEND_FW = '2.0.0'
PROCESS_BOOT = secrets.randbits(32)  # per-process boot nonce
_PROCESS_T0 = time.monotonic()

STAT_KEYS = ('datagrams', 'events', 'duplicates', 'hellos', 'acks_sent',
             'invalid', 'stale', 'ignored', 'errors', 'dropped', 'shows_sent')
QUEUE_MAX = 2000  # stamped datagrams waiting for the worker; beyond this they are dropped
SHOW_PERIOD_S = 1.0     # how often every TIMER is told what to show (PROTOCOL.md, SHOW)
DEVICES_PERIOD_S = 2.0  # how often the consoles get gate / timer status
KICK = object()         # queue item: "what the display should show has changed"


def backend_hello() -> bytes:
    up_ms = int((time.monotonic() - _PROCESS_T0) * 1000)
    return link.encode_hello(link.Hello(role='BACKEND', boot=PROCESS_BOOT, up_ms=up_ms, fw=BACKEND_FW))


class LinkServer:
    def __init__(self, host='0.0.0.0', port=3004, registry=None, drive_displays=True):
        self.host = host
        self.drive_displays = drive_displays
        self.requested_port = port
        self.port = None
        self.registry = registry if registry is not None else default_registry
        self.stats = dict.fromkeys(STAT_KEYS, 0)
        self._sock = None
        self._thread = None        # worker
        self._receiver = None
        self._queue = queue.Queue(maxsize=QUEUE_MAX)
        self._stop = threading.Event()

    @property
    def running(self):
        return (self._thread is not None and self._thread.is_alive()
                and self._receiver is not None and self._receiver.is_alive())

    def start(self):
        """Bind and start the listener thread. Raises OSError if the bind fails."""
        if self._thread is not None:
            raise RuntimeError('LinkServer already started')
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            # SO_REUSEADDR on BSD/macOS still refuses a second identical UDP
            # bind; on Linux it would let a second backend share the port, so
            # it is not set there. SO_REUSEPORT is deliberately not set: it
            # lets two backends silently split the gates' datagrams.
            if not sys.platform.startswith('linux'):
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind((self.host, self.requested_port))
            sock.settimeout(0.2)
        except OSError:
            sock.close()
            raise
        self._sock = sock
        self.port = sock.getsockname()[1]
        self._stop.clear()
        self._thread = threading.Thread(target=self._work, name=f'hhj-link-{self.port}', daemon=True)
        self._receiver = threading.Thread(target=self._receive, name=f'hhj-link-rx-{self.port}', daemon=True)
        self._thread.start()
        self._receiver.start()
        log.info('HHJ link listening on udp %s:%d', self.host, self.port)
        return self

    def stop(self, timeout=5.0):
        self._stop.set()
        if self._receiver is not None:
            self._receiver.join(timeout)
        if self._thread is not None:
            try:
                self._queue.put_nowait(None)   # wake the worker
            except queue.Full:
                pass
            self._thread.join(timeout)
        if self._sock is not None:
            self._sock.close()

    # --- threads --------------------------------------------------------------

    def _receive(self):
        sock = self._sock
        while not self._stop.is_set():
            try:
                data, addr = sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                if self._stop.is_set():
                    break
                self.stats['errors'] += 1
                log.exception('HHJ link: recvfrom failed')
                time.sleep(0.05)  # avoid a hot loop on a persistent error
                continue
            recv_ns = time.time_ns()
            self.stats['datagrams'] += 1
            try:
                self._queue.put_nowait((data, addr, recv_ns))
            except queue.Full:
                self.stats['dropped'] += 1

    def kick(self):
        """What the display should show has changed: send it now, not at the next tick."""
        try:
            self._queue.put_nowait(KICK)
        except queue.Full:
            pass

    def _work(self):
        next_show = next_devices = 0.0
        try:
            while True:
                try:
                    item = self._queue.get(timeout=0.2)
                except queue.Empty:
                    if self._stop.is_set():
                        break
                    item = False                      # nothing arrived: just the periodic work
                if item is None:
                    break
                if item is KICK:
                    next_show = 0.0
                elif item:
                    data, addr, recv_ns = item
                    try:
                        self._handle(data, addr, recv_ns)
                    except Exception:
                        self.stats['errors'] += 1
                        log.exception('HHJ link: failed to handle datagram from %s', addr)
                if not self.drive_displays:
                    continue
                now = time.monotonic()
                try:
                    if now >= next_show:
                        next_show = now + SHOW_PERIOD_S
                        self._show()
                    if now >= next_devices:
                        next_devices = now + DEVICES_PERIOD_S
                        live.publish_devices()
                except Exception:
                    self.stats['errors'] += 1
                    log.exception('HHJ link: periodic work failed')
        finally:
            connection.close()

    def _show(self):
        """Tell every timer what to show (arena.display)."""
        targets = self.registry.addresses('TIMER')
        if not targets:
            return
        from . import arena
        close_old_connections()
        try:
            payload = link.encode_show(arena.display())
        finally:
            close_old_connections()
        for addr in targets:
            if self._send(payload, addr):
                self.stats['shows_sent'] += 1

    def _send(self, payload, addr):
        try:
            self._sock.sendto(payload, addr)
            return True
        except OSError:
            self.stats['errors'] += 1
            log.warning('HHJ link: sendto %s failed', addr, exc_info=True)
            return False

    def _handle(self, data, addr, recv_ns):
        msg = link.decode(data)
        if msg is None:
            self.stats['invalid'] += 1
            return
        if isinstance(msg, link.Hello):
            if msg.role == 'BACKEND':
                self.stats['ignored'] += 1
                return
            self.stats['hellos'] += 1
            new_timer = msg.role == 'TIMER' and addr not in self.registry.addresses('TIMER')
            self.registry.seen(msg, addr[0], addr[1])
            self._send(backend_hello(), addr)
            if new_timer:
                self.kick()                           # a display appeared: tell it what to show at once
            return
        if isinstance(msg, (link.Ack, link.Show)):
            self.stats['ignored'] += 1
            return

        from .ingest import ingest_event
        close_old_connections()
        try:
            reading, created = ingest_event(msg, recv_ns // 1_000_000, addr[0])
        finally:
            close_old_connections()
        if reading is None:
            self.stats['stale'] += 1
            return
        self.stats['events' if created else 'duplicates'] += 1
        if self._send(link.encode_ack(link.Ack(mark=msg.mark, boot=msg.boot, seq=msg.seq)), addr):
            self.stats['acks_sent'] += 1


# --- process-wide instance (autostarted from TimingsConfig.ready) ---------------

_server = None
_server_lock = threading.Lock()


def get_server():
    return _server


def kick_display():
    """Called after every change of state: the running server (if any) updates the displays."""
    server = get_server()
    if server is not None:
        server.kick()


def start_server(host='0.0.0.0', port=3004):
    """Start the process-wide LinkServer once. Returns it, or None if the bind failed."""
    global _server
    with _server_lock:
        if _server is not None:
            return _server
        server = LinkServer(host, port)
        try:
            server.start()
        except OSError as exc:
            log.error('HHJ link: cannot bind UDP %s:%d (%s) - gates will NOT be heard; '
                      'HTTP keeps running. Is another backend already running?', host, port, exc)
            return None
        _server = server
        return server
