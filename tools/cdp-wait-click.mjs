// Czeka, az element pasujacy do selektora pojawi sie i bedzie klikalny, po czym klika.
// uzycie: node cdp-wait-click.mjs '<selektor>' [timeout_s]
const PORT = process.env.CDP_PORT || 9222;
const sel = process.argv[2];
const timeoutS = parseInt(process.argv[3] || '120', 10);

async function pageWs() {
  for (let i = 0; i < 60; i++) {
    try {
      const r = await fetch(`http://127.0.0.1:${PORT}/json`);
      const list = await r.json();
      const p = list.find(t => t.type === 'page');
      if (p) return p.webSocketDebuggerUrl;
    } catch {}
    await new Promise(r => setTimeout(r, 1000));
  }
  throw new Error('DevTools niedostepny');
}
let _id = 1;
function send(ws, method, params) {
  const id = _id++;
  return new Promise((resolve, reject) => {
    const onMsg = (ev) => { const m = JSON.parse(ev.data);
      if (m.id === id) { ws.removeEventListener('message', onMsg); resolve(m); } };
    ws.addEventListener('message', onMsg);
    ws.send(JSON.stringify({ id, method, params }));
    setTimeout(() => reject(new Error('timeout ' + method)), 20000);
  });
}
async function evalJS(ws, expr) {
  const m = await send(ws, 'Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
  return m.result && m.result.result ? m.result.result.value : undefined;
}
const ws = new WebSocket(await pageWs());
await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
await send(ws, 'Runtime.enable', {});
const deadline = Date.now() + timeoutS * 1000;
let clicked = false;
while (Date.now() < deadline) {
  const r = await evalJS(ws, `(() => {
    const el = document.querySelector(${JSON.stringify(sel)});
    if (el && !el.disabled) { el.click(); return { ok:true, text:(el.innerText||'').trim().slice(0,30) }; }
    return { ok:false, url: location.href };
  })()`);
  if (r && r.ok) { console.log(JSON.stringify(r)); clicked = true; break; }
  await new Promise(r => setTimeout(r, 1500));
}
ws.close();
process.exit(clicked ? 0 : 1);
