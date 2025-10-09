import os
import sys
from django.urls import URLResolver, URLPattern

# Ensure project root is on sys.path
sys.path.insert(0, os.getcwd())

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'biometria_server.settings')
import django
django.setup()

from django.urls import get_resolver


def list_patterns(patterns, prefix=''):
    for p in patterns:
        if isinstance(p, URLResolver):
            print(f"INCLUDE: {prefix}{p.pattern}")
            try:
                list_patterns(p.url_patterns, prefix + str(p.pattern))
            except Exception as e:
                print('  (error listing nested patterns)', e)
        elif isinstance(p, URLPattern):
            callback = p.callback
            view_name = getattr(callback, '__name__', repr(callback))
            print(f"PATTERN: {prefix}{p.pattern} -> {view_name} (name={p.name})")
        else:
            print('UNKNOWN PATTERN TYPE', p)


resolver = get_resolver()
list_patterns(resolver.url_patterns)
