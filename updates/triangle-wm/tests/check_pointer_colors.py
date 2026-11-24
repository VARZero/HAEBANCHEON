"""Local monitor uses the actual output palette and independent S/B controls."""
from pathlib import Path
from playwright.sync_api import sync_playwright
import json
root=Path(__file__).resolve().parents[1];events=[]
with sync_playwright() as pw:
 browser=pw.chromium.launch(executable_path='C:/Users/ssm01/AppData/Local/ms-playwright/chromium_headless_shell-1248/chrome-headless-shell-win64/chrome-headless-shell.exe')
 page=browser.new_page(viewport=dict(width=1100,height=800));errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
 def route(r):
  url=r.request.url
  if url=='http://monitor.test/':return r.fulfill(content_type='text/html',body=(root/'tools/pointer_monitor.html').read_text(encoding='utf-8'))
  if url.endswith('/status'):return r.fulfill(json=dict(online=True,fast_input=True,async_input=True,capture=dict(capture_sequence=1,capture_age_seconds=0)))
  if url.endswith('/input'):
   q=r.request.post_data_json;events.append(q)
   result=dict(window_colors=[dict(window_id=1,splash_index=228,background_index=84)],display_enabled=True) if q['op']=='input_status' else dict(generation=1)
   return r.fulfill(json=dict(ok=True,result=result,accept_ms=1,present_ms=2))
  return r.fulfill(status=204)
 page.route('**/*',route);page.goto('http://monitor.test/');page.wait_for_function('!document.getElementById("apply-colors").disabled')
 assert page.input_value('#splash-color')=='#ff0024' and page.input_value('#background-color')=='#4800b6'
 # Every picker output index must match the exact palette, including gray rule.
 assert page.evaluate('Array.from({length:256},(_,i)=>hex332(index332(hex332(i)))===hex332(i)).every(Boolean)')
 page.locator('#splash-color').fill('#00ff00');page.locator('#background-color').fill('#ffff00');page.locator('#apply-colors').click();page.wait_for_function('document.getElementById("status").textContent.includes("게시")')
 color=next(q['args'] for q in events if q['op']=='input_colors');assert color==dict(window_id=1,splash_index=3,background_index=227),color
 assert not errors,errors
 browser.close();print(json.dumps(dict(result='PASS_POINTER_COLORS',independent=True,exact_palette=True,actual_input=color)))
