import logging
import os
import sys

from django.apps import AppConfig

log = logging.getLogger(__name__)


def should_start_link(argv, environ):
    """Decide whether this process runs the UDP link listener.

    HHJ_LINK=0 never; HHJ_LINK=1 always (e.g. under daphne), except in the
    runserver autoreloader parent; unset only in the serving process of
    `manage.py runserver`.
    """
    flag = environ.get('HHJ_LINK')
    if flag == '0':
        return False
    is_runserver = len(argv) > 1 and argv[1] == 'runserver'
    reloader_parent = is_runserver and '--noreload' not in argv and environ.get('RUN_MAIN') != 'true'
    if reloader_parent:
        return False
    if flag == '1':
        return True
    return is_runserver


class TimingsConfig(AppConfig):
    default_auto_field = 'django.db.models.BigAutoField'
    name = 'timings'

    def ready(self):
        if not should_start_link(sys.argv, os.environ):
            return
        from django.conf import settings
        from .udp_link import start_server
        start_server('0.0.0.0', getattr(settings, 'HHJ_LINK_PORT', 3004))
