// Klient DevTools (CDP) dla launchera pod Wine.
// uzycie: node cdp.mjs eval '<wyrazenie JS>'
//         node cdp.mjs click '<selektor CSS>'
//         node cdp.mjs dom
const PORT = process.env.CDP_PORT || 9222;
const [,, cmd, arg] = process.argv;

async function pageWs() {
  const r = await fetch(`http://127.0.0.1:${PORT}/json`);
  const list = await r.json();
  const page = list.find(t => t.type === 'page');
  if (!page) throw new Error('brak strony w DevTools');
  return page.webSocketDebuggerUrl;
}

function send(ws, id, method, params) {
  return new Promise((resolve, reject) => {
    const onMsg = (ev) => {
      const m = JSON.parse(ev.data);
      if (m.id === id) { ws.removeEventListener('message', onMsg); resolve(m); }
    };
    ws.addEventListener('message', onMsg);
    ws.send(JSON.stringify({ id, method, params }));
    setTimeout(() => reject(new Error('timeout '+method)), 15000);
  });
}

async function evalJS(ws, expr) {
  const m = await send(ws, Math.floor(Math.random()*1e6), 'Runtime.evaluate',
    { expression: expr, returnByValue: true, awaitPromise: true });
  if (m.result && m.result.exceptionDetails)
    return { error: m.result.exceptionDetails.text || JSON.stringify(m.result.exceptionDetails) };
  return m.result && m.result.result ? m.result.result.value : undefined;
}

const wsUrl = await pageWs();
const ws = new WebSocket(wsUrl);
await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
await send(ws, 1, 'Runtime.enable', {});

if (cmd === 'eval') {
  console.log(JSON.stringify(await evalJS(ws, arg), null, 2));
} else if (cmd === 'dom') {
  const expr = `(() => {
    const out = [];
    const clickables = document.querySelectorAll('button, a, [role=button], [onclick], input, .btn, [class*=button], [class*=sign], [class*=login]');
    clickables.forEach(el => {
      const r = el.getBoundingClientRect();
      out.push({ tag: el.tagName, cls: el.className, id: el.id,
                 text: (el.innerText||el.value||'').slice(0,40).trim(),
                 x: Math.round(r.x+r.width/2), y: Math.round(r.y+r.height/2),
                 vis: r.width>0 && r.height>0 });
    });
    return { url: location.href, title: document.title,
             bodyText: document.body.innerText.slice(0,500), clickables: out };
  })()`;
  console.log(JSON.stringify(await evalJS(ws, expr), null, 2));
} else if (cmd === 'click') {
  const expr = `(() => {
    const el = document.querySelector(${JSON.stringify(arg)});
    if (!el) return { ok:false, reason:'brak elementu' };
    el.click();
    return { ok:true, text:(el.innerText||el.value||'').slice(0,40) };
  })()`;
  console.log(JSON.stringify(await evalJS(ws, expr), null, 2));
}
ws.close();
