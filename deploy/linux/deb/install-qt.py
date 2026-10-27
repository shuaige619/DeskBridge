#!/usr/bin/env python3
"""Run aqt with its own tool identity, retaining official archive checksums.

Some public mirrors reject the generic python-requests User-Agent with an HTML
403 response. Identify this downloader as aqtinstall instead of the HTTP library.
The patch is at module scope so aqt's spawned download workers inherit it.
"""
import runpy
import requests.utils

requests.utils.default_user_agent = lambda name='python-requests': 'aqtinstall/3.3.0 (DeskBridge release build)'

if __name__ == '__main__':
    runpy.run_module('aqt', run_name='__main__')
