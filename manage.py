#!/usr/bin/env python
"""Django's command-line utility for administrative tasks."""
import os
import sys
from pathlib import Path


def main():
    """Run administrative tasks."""
    # Ensure the project root is on sys.path so the settings module can be imported
    # even if the command is invoked from a different working directory.
    project_root = Path(__file__).resolve().parent
    # The Django project package is located at <project_root>/biometria_server/biometria_server
    # Ensure the parent of the package (the inner `biometria_server` folder) is on sys.path so
    # `import biometria_server.settings` resolves to the correct module.
    project_pkg_parent = project_root / 'biometria_server'
    if project_pkg_parent.exists() and str(project_pkg_parent) not in sys.path:
        sys.path.insert(0, str(project_pkg_parent))
    elif str(project_root) not in sys.path:
        # Fallback: ensure project root is on sys.path
        sys.path.insert(0, str(project_root))
    # Also ensure the apps directory is on sys.path so Django can import apps like `biometria`
    apps_dir = project_root / 'apps'
    if apps_dir.exists() and str(apps_dir) not in sys.path:
        sys.path.insert(0, str(apps_dir))

    os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'biometria_server.settings')
    try:
        from django.core.management import execute_from_command_line
    except ImportError as exc:
        raise ImportError(
            "Couldn't import Django. Are you sure it's installed and "
            "available on your PYTHONPATH environment variable? Did you "
            "forget to activate a virtual environment?"
        ) from exc
    execute_from_command_line(sys.argv)


if __name__ == '__main__':
    main()
