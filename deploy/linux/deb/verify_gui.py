from pathlib import Path
import json
import os
import re
import signal
import subprocess
import sys
import time

assert os.getuid() != 0, 'GUI verification must run as a normal user'
os.environ['XDG_CONFIG_HOME'] = '/home/debtest/.config'
os.environ['XDG_STATE_HOME'] = '/home/debtest/.local/state'
os.environ['QT_QPA_PLATFORM'] = 'xcb'
phase = sys.argv[1] if len(sys.argv) > 1 else 'initial'
assert phase in ('initial', 'reinstall'), phase
prefix = '' if phase == 'initial' else 'reinstall-'
results = Path('/job/verify-results')
main_title = re.compile(r'^(?:DeskBridge|桌桥(?:\s+DeskBridge)?)(?:\s+-\s+.*)?$')
screen_width, screen_height = map(int, subprocess.check_output(['xdotool', 'getdisplaygeometry'], text=True).split())

with (results / f'{prefix}gui-startup.log').open('w') as log:
    app = subprocess.Popen(['/usr/bin/deskbridge'], stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    observed = []
    try:
        deadline = time.monotonic() + 60
        selected = None
        while time.monotonic() < deadline:
            assert app.poll() is None, f'GUI exited early: {app.returncode}; see {prefix}gui-startup.log'
            search = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(app.pid)], text=True, capture_output=True)
            assert search.returncode in (0, 1), search.stderr
            observed = []
            for window in search.stdout.splitlines():
                try:
                    title = subprocess.check_output(['xdotool', 'getwindowname', window], text=True).strip()
                    raw = subprocess.check_output(['xdotool', 'getwindowgeometry', '--shell', window], text=True)
                except subprocess.CalledProcessError:
                    continue  # A transient splash/dialog can disappear while queried.
                geometry = dict(line.split('=', 1) for line in raw.splitlines() if '=' in line)
                x, y, width, height = (int(geometry[key]) for key in ('X', 'Y', 'WIDTH', 'HEIGHT'))
                visible_width = min(x + width, screen_width) - max(x, 0)
                visible_height = min(y + height, screen_height) - max(y, 0)
                candidate = {'window': window, 'title': title, 'x': x, 'y': y, 'width': width, 'height': height}
                observed.append(candidate)
                if (main_title.fullmatch(title) and 800 <= width <= screen_width and 500 <= height <= screen_height
                        and visible_width >= 640 and visible_height >= 400):
                    selected = candidate
                    break
            if selected:
                break
            time.sleep(0.5)
        assert selected, f'No reasonably sized, visible DeskBridge main window within 60 seconds: {observed}'
        # Keep the matching main window alive long enough to reject immediate startup failure.
        time.sleep(2)
        assert app.poll() is None, f'GUI exited after showing its main window: {app.returncode}'
        with (results / f'{prefix}main-window.txt').open('w') as window_info:
            subprocess.run(['xwininfo', '-id', selected['window']], check=True, stdout=window_info)
        subprocess.run(['xwd', '-silent', '-root', '-out', str(results / f'{prefix}gui.xwd')], check=True)
        (results / f'{prefix}main-window.json').write_text(json.dumps(selected, ensure_ascii=False, indent=2) + '\n')
        (results / f'{prefix}window-titles.txt').write_text('\n'.join(item['title'] for item in observed) + '\n')
        print(f'PASS: {phase} normal-user X11 GUI has a visible DeskBridge main window:', selected)
    finally:
        # Preserve screenshot/window diagnostics on both success and failure.
        if not (results / f'{prefix}gui.xwd').exists():
            subprocess.run(['xwd', '-silent', '-root', '-out', str(results / f'{prefix}gui.xwd')], check=False)
        with (results / f'{prefix}window-tree.txt').open('w') as tree:
            subprocess.run(['xwininfo', '-root', '-tree'], stdout=tree, check=False)
        try:
            os.killpg(app.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            app.wait(timeout=10)
        except subprocess.TimeoutExpired:
            pass
        # Include any core child started by the default profile in cleanup.
        try:
            os.killpg(app.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        app.wait()
