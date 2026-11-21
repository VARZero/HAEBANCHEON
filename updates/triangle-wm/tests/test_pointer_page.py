"""Explicit live-board browser acceptance test; stops the bridge on success.

Requires the capture server, one visible app, pointer_bridge.py and Playwright
with Edge on Windows. This moves a real window, cancels a drag, then releases
COM4 through the page's stop button. Run only with exclusive input ownership.
"""
import argparse
import json
from pathlib import Path
from urllib.request import Request, urlopen
from playwright.sync_api import sync_playwright


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--url', required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    url = args.url.rstrip('/')

    def status():
        with urlopen(url + '/status', timeout=30) as r:
            return json.load(r)

    def controls():
        q = Request(url + '/input', data=b'{"op":"input_status","args":{}}',
                    headers={'Content-Type': 'application/json', 'Origin': url})
        with urlopen(q, timeout=30) as r:
            result = json.load(r)
        assert result['ok'], result
        return result['result']

    errors = []
    report = {}
    with sync_playwright() as p:
        browser = p.chromium.launch(channel='msedge', headless=True)
        try:
            page = browser.new_page(viewport={'width': 1400, 'height': 1050})
            page.on('pageerror', lambda error: errors.append(str(error)))
            page.goto(url, wait_until='domcontentloaded')
            page.wait_for_function("document.getElementById('screen').naturalWidth === 1280")
            page.locator('#arm').click()

            def idle():
                page.wait_for_function('!busy && queue.length === 0', timeout=30000)

            def locate(handle):
                box = page.locator('#screen').bounding_box()
                return (box['x'] + handle['x'] * box['width'] / 1280,
                        box['y'] + handle['y'] * box['height'] / 720,
                        box['width'] / 1280)

            before = controls()
            handle = next(c for c in before['controls'] if c['action'] == 'move')
            x, y, scale = locate(handle)
            page.mouse.move(x, y)
            idle()
            page.mouse.down()
            idle()
            captured = controls()
            assert captured['pressed'] and captured['captured_window'] == handle['window_id']
            page.mouse.move(x + 24 * scale, y, steps=8)
            page.mouse.up()
            idle()
            moved = status()['last']
            assert moved['ok'] and moved['result']['applied'], moved
            assert not controls()['pressed']

            handle = next(c for c in controls()['controls'] if c['action'] == 'move')
            x, y, _ = locate(handle)
            page.mouse.move(x, y)
            idle()
            page.mouse.down()
            idle()
            page.keyboard.press('Escape')
            idle()
            assert not controls()['pressed']
            page.mouse.up()
            idle()
            page.locator('#stop').click()
            page.wait_for_function("document.getElementById('status').textContent.includes('UART 콘솔 복원')", timeout=30000)
            assert not status()['online']
            assert not errors, errors
            report = {'passed': True, 'move_present_ms': moved['present_ms'],
                      'checks': ['actual HDMI loaded', 'pointer capture', 'drag release applied',
                                 'Escape cancels', 'stop releases UART'], 'page_errors': errors}
        finally:
            browser.close()
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
