from django.test import SimpleTestCase

from timings.apps import should_start_link

M = 'manage.py'


class AutostartDecisionTests(SimpleTestCase):
    CASES = [
        # argv, env, expected
        ([M, 'runserver'], {}, False),                                   # autoreloader parent
        ([M, 'runserver'], {'RUN_MAIN': 'true'}, True),                  # reloader child serves
        ([M, 'runserver', '--noreload'], {}, True),
        ([M, 'runserver', '0.0.0.0:8000', '--noreload'], {}, True),
        ([M, 'runserver', '--noreload'], {'HHJ_LINK': '0'}, False),
        ([M, 'runserver'], {'RUN_MAIN': 'true', 'HHJ_LINK': '0'}, False),
        ([M, 'runserver'], {'HHJ_LINK': '1'}, False),                    # still not in the parent
        ([M, 'runserver'], {'HHJ_LINK': '1', 'RUN_MAIN': 'true'}, True),
        ([M, 'test'], {}, False),
        ([M, 'test'], {'RUN_MAIN': 'true'}, False),
        ([M, 'migrate'], {}, False),
        ([M, 'makemigrations'], {}, False),
        ([M, 'shell'], {}, False),
        ([M, 'hhj_send', 'START'], {}, False),
        ([M], {}, False),
        (['daphne', 'adminportal.asgi:application'], {}, False),
        (['daphne', 'adminportal.asgi:application'], {'HHJ_LINK': '1'}, True),
        ([M, 'migrate'], {'HHJ_LINK': '1'}, True),                       # explicit request wins
        ([M, 'runserver', '--noreload'], {'HHJ_LINK': 'yes'}, True),     # unknown value = unset
        ([M, 'test'], {'HHJ_LINK': 'yes'}, False),
    ]

    def test_table(self):
        for argv, env, expected in self.CASES:
            with self.subTest(argv=argv, env=env):
                self.assertIs(should_start_link(argv, env), expected)
