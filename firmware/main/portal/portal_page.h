/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * The portal's single page, embedded as a string literal so it needs no filesystem and
 * survives an assets-partition wipe.
 *
 * It is one file on purpose. A device you are trying to recover is the worst possible
 * time to discover that the stylesheet lives in a partition you just erased, so there
 * are no external requests of any kind — no CDN, no fonts, no icons, no second round
 * trip. Everything below is what the browser gets.
 *
 * The static-IP flow is the one piece of interaction worth explaining. Applying a static
 * address kills the socket you applied it over, so the page cannot watch it succeed:
 * a cross-origin fetch to the new address would be blocked, and adding CORS headers to a
 * recovery tool is a poor trade. Instead the device arms a revert timer, the page hands
 * you a link to the new address, and simply *loading* the page there confirms it — the
 * new page sees a revert pending and cancels it. Never reaching it costs a two-minute
 * wait and an automatic rollback to DHCP.
 */
#pragma once

static const char PORTAL_PAGE_HTML[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>StackyChan</title>
<style>
:root{
  --bg:#0b0d12; --bg2:#0f1219; --panel:#141821; --panel2:#1a1f2b;
  --line:#252b38; --line2:#323a4b;
  --ink:#e8ecf3; --dim:#8d97ab; --faint:#5d6779;
  --accent:#6c8cff; --accent2:#8b6cff;
  --ok:#2fd4a0; --bad:#ff6b6b; --warn:#ffb454;
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
  --r:10px; --shadow:0 1px 2px rgba(0,0,0,.4),0 8px 24px rgba(0,0,0,.25);
}
*{box-sizing:border-box}
html,body{height:100%}
body{margin:0;background:var(--bg);color:var(--ink);
  font:14.5px/1.55 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,system-ui,sans-serif;
  -webkit-font-smoothing:antialiased}

/* ---------------------------------------------------------------- layout -- */
.app{display:grid;grid-template-columns:212px 1fr;min-height:100%}
nav{background:var(--bg2);border-right:1px solid var(--line);padding:16px 12px;
  display:flex;flex-direction:column;gap:2px;position:sticky;top:0;height:100vh;overflow-y:auto}
.brand{display:flex;align-items:center;gap:9px;padding:4px 8px 14px}
.dot{width:9px;height:9px;border-radius:50%;background:var(--faint);flex:none;
  transition:background .3s}
.dot.up{background:var(--ok);box-shadow:0 0 0 3px rgba(47,212,160,.15)}
.dot.down{background:var(--bad);box-shadow:0 0 0 3px rgba(255,107,107,.15)}
.brand b{font-size:15px;letter-spacing:.2px}
nav a{display:flex;align-items:center;gap:10px;padding:8px 10px;border-radius:8px;
  color:var(--dim);text-decoration:none;font-size:13.5px;cursor:pointer;
  border:1px solid transparent;transition:background .12s,color .12s}
nav a:hover{background:var(--panel);color:var(--ink)}
nav a.on{background:var(--panel2);color:var(--ink);border-color:var(--line2)}
nav a i{width:16px;text-align:center;font-style:normal;opacity:.85;font-size:13px}
.navfoot{margin-top:auto;padding:12px 10px 2px;color:var(--faint);font:11.5px/1.5 var(--mono);
  word-break:break-all}
.pagefoot{display:none;margin-top:28px;color:var(--faint);font:11.5px/1.5 var(--mono)}

main{padding:26px 30px 70px;max-width:940px;width:100%}
section{display:none;animation:in .18s ease}
section.on{display:block}
@keyframes in{from{opacity:0;transform:translateY(4px)}to{opacity:1;transform:none}}
h1{font-size:21px;margin:0 0 3px;letter-spacing:-.2px}
.lede{color:var(--dim);font-size:13.5px;margin:0 0 22px;max-width:64ch}

/* ----------------------------------------------------------------- cards -- */
.card{background:var(--panel);border:1px solid var(--line);border-radius:var(--r);
  padding:18px;margin-bottom:16px;box-shadow:var(--shadow)}
.card h2{font-size:11.5px;text-transform:uppercase;letter-spacing:.09em;color:var(--dim);
  margin:0 0 14px;font-weight:600}
.card h2 .hint{text-transform:none;letter-spacing:0;color:var(--faint);font-weight:400;
  margin-left:8px;font-size:11.5px}

/* ----------------------------------------------------------------- forms -- */
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:12px}
.f{display:flex;flex-direction:column;gap:5px}
.f label{color:var(--dim);font-size:12px;font-weight:500}
.f .note{color:var(--faint);font-size:11.5px}
input[type=text],input[type=password],input[type=number],select,textarea{
  background:var(--bg);color:var(--ink);border:1px solid var(--line2);border-radius:7px;
  padding:8px 10px;font:13px/1.4 var(--mono);width:100%;transition:border-color .12s}
textarea{font-family:inherit;font-size:13px;resize:vertical;min-height:84px;line-height:1.5}
input:focus,select:focus,textarea:focus{outline:none;border-color:var(--accent)}
input:disabled,select:disabled{opacity:.45;cursor:not-allowed}
select{cursor:pointer}
input[type=color]{width:100%;height:34px;background:none;border:1px solid var(--line2);
  border-radius:7px;padding:2px;cursor:pointer}
input[type=range]{width:100%;accent-color:var(--accent);cursor:pointer}

.btn{background:var(--panel2);color:var(--ink);border:1px solid var(--line2);border-radius:7px;
  padding:8px 14px;cursor:pointer;font-size:13px;font-weight:500;transition:all .12s;
  font-family:inherit}
.btn:hover:not(:disabled){border-color:var(--accent);background:#1f2534}
.btn:disabled{opacity:.45;cursor:not-allowed}
.btn.pri{background:var(--accent);border-color:var(--accent);color:#0a0d16;font-weight:600}
.btn.pri:hover:not(:disabled){background:#7d99ff;border-color:#7d99ff}
.btn.danger:hover:not(:disabled){border-color:var(--bad);color:var(--bad)}
.btn.sm{padding:5px 10px;font-size:12px}
.actions{display:flex;gap:9px;align-items:center;flex-wrap:wrap;margin-top:16px;
  padding-top:15px;border-top:1px solid var(--line)}
.actions .spacer{flex:1}

.toggle{display:flex;gap:0;border:1px solid var(--line2);border-radius:7px;overflow:hidden;
  width:fit-content}
.toggle button{background:transparent;border:none;color:var(--dim);padding:7px 16px;
  cursor:pointer;font-size:13px;font-family:inherit;transition:all .12s}
.toggle button.on{background:var(--accent);color:#0a0d16;font-weight:600}
.toggle button:not(.on):hover{background:var(--panel2);color:var(--ink)}

/* --------------------------------------------------------------- readouts -- */
table.kv{width:100%;border-collapse:collapse;font-size:13px}
table.kv td{padding:7px 0;border-bottom:1px solid var(--line)}
table.kv tr:last-child td{border-bottom:none}
table.kv td:first-child{color:var(--dim);width:40%;font-size:12.5px}
table.kv td:last-child{font-family:var(--mono);font-size:12.5px;word-break:break-all}

.pill{display:inline-block;padding:2px 8px;border-radius:20px;font-size:11px;font-weight:600;
  letter-spacing:.03em;vertical-align:middle}
.pill.ok{background:rgba(47,212,160,.14);color:var(--ok)}
.pill.bad{background:rgba(255,107,107,.14);color:var(--bad)}
.pill.warn{background:rgba(255,180,84,.14);color:var(--warn)}
.pill.mut{background:var(--panel2);color:var(--dim)}

.choices{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:9px}
.choice{border:1px solid var(--line2);border-radius:8px;padding:11px 13px;cursor:pointer;
  background:var(--bg);transition:all .12s;text-align:left;display:flex;align-items:center;gap:10px}
.choice:hover{border-color:var(--accent)}
.choice.on{border-color:var(--accent);background:rgba(108,140,255,.09)}
.choice .icon{flex:none;width:28px;height:28px;border-radius:7px;display:flex;align-items:center;
  justify-content:center;font-size:15px;background:var(--panel2);line-height:1}
.choice.on .icon{background:rgba(108,140,255,.2)}
.choice .body{display:flex;flex-direction:column;min-width:0}
.choice .t{font-size:13.5px;font-weight:600;display:block}
.choice .s{font-size:11.5px;color:var(--dim);display:block;margin-top:2px;font-family:var(--mono);
  overflow:hidden;text-overflow:ellipsis;white-space:nowrap}

.emos{display:grid;grid-template-columns:repeat(auto-fit,minmax(88px,1fr));gap:8px}
.slider{display:grid;grid-template-columns:70px 1fr 48px;gap:11px;align-items:center;margin:9px 0}
.slider label{color:var(--dim);font-size:12.5px}
.slider .v{font-family:var(--mono);font-size:12px;color:var(--accent);text-align:right}

.banner{border-radius:8px;padding:12px 14px;margin-bottom:16px;font-size:13px;
  border:1px solid;display:flex;gap:11px;align-items:flex-start}
.banner.warn{background:rgba(255,180,84,.08);border-color:rgba(255,180,84,.3);color:#ffd49b}
.banner.bad{background:rgba(255,107,107,.08);border-color:rgba(255,107,107,.3);color:#ffb3b3}
.banner.ok{background:rgba(47,212,160,.08);border-color:rgba(47,212,160,.3);color:#a5efd7}
.banner b{display:block;margin-bottom:3px}
.banner a{color:inherit;font-weight:700}

/* ------------------------------------------------------------------- ota -- */
#drop{border:2px dashed var(--line2);border-radius:10px;padding:30px;text-align:center;
  color:var(--dim);cursor:pointer;transition:all .15s;background:var(--bg)}
#drop:hover,#drop.hot{border-color:var(--accent);color:var(--ink);background:rgba(108,140,255,.05)}
#drop b{color:var(--ink)}
progress{width:100%;height:6px;margin-top:13px;border-radius:3px;overflow:hidden;
  border:none;background:var(--bg)}
progress::-webkit-progress-bar{background:var(--bg);border-radius:3px}
progress::-webkit-progress-value{background:var(--accent);border-radius:3px}
progress::-moz-progress-bar{background:var(--accent)}
.msg{font-family:var(--mono);font-size:12px;margin-top:12px;white-space:pre-wrap;
  color:var(--dim);line-height:1.6}
.msg.ok{color:var(--ok)} .msg.bad{color:var(--bad)} .msg.warn{color:var(--warn)}

/* ----------------------------------------------------------------- toast -- */
#toast{position:fixed;right:20px;bottom:20px;display:flex;flex-direction:column;gap:8px;
  z-index:50;pointer-events:none}
.t{background:var(--panel2);border:1px solid var(--line2);border-radius:8px;
  padding:10px 15px;font-size:13px;box-shadow:var(--shadow);animation:tin .2s ease;
  max-width:340px}
.t.ok{border-color:rgba(47,212,160,.45)} .t.bad{border-color:rgba(255,107,107,.45)}
@keyframes tin{from{opacity:0;transform:translateX(16px)}to{opacity:1;transform:none}}

.rows{display:flex;flex-direction:column;gap:10px}
.prow{display:flex;gap:11px;align-items:center;padding:11px 13px;border:1px solid var(--line2);
  border-radius:8px;background:var(--bg);cursor:pointer;transition:all .12s}
.prow:hover{border-color:var(--accent)}
.prow.on{border-color:var(--accent);background:rgba(108,140,255,.09)}
.prow .nm{font-weight:600;font-size:13.5px}
.prow .pr{color:var(--dim);font-size:12px;margin-top:1px;overflow:hidden;
  text-overflow:ellipsis;white-space:nowrap;max-width:100%}
.prow .grow{flex:1;min-width:0}

@media(max-width:760px){
  .app{grid-template-columns:1fr}
  nav{position:static;height:auto;flex-direction:row;flex-wrap:wrap;
    border-right:none;border-bottom:1px solid var(--line);padding:10px}
  .brand{width:100%;padding:2px 6px 8px}
  .navfoot{display:none}
  .pagefoot{display:block}
  main{padding:20px 16px 60px}
  .slider{grid-template-columns:60px 1fr 40px}
}
</style></head><body>

<div class="app">
<nav>
  <div class="brand"><span class="dot" id="dot"></span><b>StackyChan</b></div>
  <a data-go="overview" class="on"><i>&#9673;</i>Overview</a>
  <a data-go="network"><i>&#9783;</i>Network</a>
  <a data-go="time"><i>&#9201;</i>Time</a>
  <a data-go="ai"><i>&#9678;</i>AI backend</a>
  <a data-go="persona"><i>&#9787;</i>Personas</a>
  <a data-go="memory"><i>&#9998;</i>Memory</a>
  <a data-go="face"><i>&#9635;</i>Face &amp; body</a>
  <a data-go="music"><i>&#9835;</i>Music</a>
  <a data-go="wake"><i>&#9834;</i>Wake word</a>
  <a data-go="update"><i>&#8679;</i>Update</a>
  <div class="navfoot" id="navfoot"></div>
</nav>

<main>

<!-- =============================================================== overview -->
<section id="s-overview" class="on">
  <h1>Overview</h1>
  <p class="lede">Everything this device is doing right now. Nothing on this page changes anything.</p>
  <div id="confirmbanner"></div>
  <div class="grid">
    <div class="card"><h2>Firmware</h2><table class="kv" id="k-fw"></table></div>
    <div class="card"><h2>Network</h2><table class="kv" id="k-net"></table></div>
  </div>
  <div class="card"><h2>Character</h2><table class="kv" id="k-char"></table></div>
  <div class="card"><h2>Device<span class="hint">a reboot keeps every setting</span></h2>
    <div class="actions" style="margin-top:0;padding-top:0;border:none">
      <button class="btn danger" id="reboot">Reboot device</button>
    </div>
  </div>
</section>

<!-- ================================================================ network -->
<section id="s-network">
  <h1>Network</h1>
  <p class="lede">Addressing for the Wi-Fi interface. DHCP is the safe default; a static
    address is applied with a rollback so a typo cannot strand the device.</p>

  <div class="card"><h2>Live interface</h2><table class="kv" id="k-netlive"></table></div>

  <div class="card"><h2>Addressing</h2>
    <div class="toggle" id="netmode">
      <button data-m="0" class="on">DHCP</button><button data-m="1">Static</button>
    </div>
    <div class="grid" style="margin-top:15px">
      <div class="f"><label>IP address</label><input type="text" id="n-ip" placeholder="192.168.1.50"></div>
      <div class="f"><label>Subnet mask</label><input type="text" id="n-mask" placeholder="255.255.255.0"></div>
      <div class="f"><label>Gateway</label><input type="text" id="n-gw" placeholder="192.168.1.1"></div>
      <div class="f"><label>DNS 1</label><input type="text" id="n-dns1" placeholder="1.1.1.1"></div>
      <div class="f"><label>DNS 2</label><input type="text" id="n-dns2" placeholder="8.8.8.8"></div>
      <div class="f"><label>Hostname</label><input type="text" id="n-host" placeholder="stackchan">
        <span class="note">reachable as &lt;hostname&gt;.local</span></div>
    </div>
    <div class="banner warn" id="staticwarn" style="display:none;margin-top:16px">
      <div><b>The address changes the moment you save.</b>
      This page will lose the device and hand you a link to the new address. Open it and the
      change sticks. Do nothing and the device returns to DHCP on its own after two minutes.</div>
    </div>
    <div class="actions">
      <button class="btn pri" id="n-save">Save &amp; apply</button>
      <span class="spacer"></span><span id="n-msg" class="msg" style="margin:0"></span>
    </div>
  </div>
</section>

<!-- =================================================================== time -->
<section id="s-time">
  <h1>Time</h1>
  <p class="lede">The device keeps time in a battery-backed RTC and corrects it from NTP.
    The timezone is a POSIX rule string, so daylight saving is handled on-device.</p>

  <div class="card"><h2>Clock</h2><table class="kv" id="k-clock"></table>
    <div class="actions"><button class="btn" id="t-sync">Sync now</button></div>
  </div>

  <div class="card"><h2>NTP servers</h2>
    <div class="toggle" id="ntpmode">
      <button data-m="1" class="on">Enabled</button><button data-m="0">Disabled</button>
    </div>
    <div class="grid" style="margin-top:15px">
      <div class="f"><label>Primary</label><input type="text" id="t-1" placeholder="pool.ntp.org"></div>
      <div class="f"><label>Secondary</label><input type="text" id="t-2" placeholder="time.google.com"></div>
      <div class="f"><label>Tertiary</label><input type="text" id="t-3" placeholder="time.cloudflare.com"></div>
    </div>
    <div class="grid" style="margin-top:12px">
      <div class="f"><label>Timezone</label><select id="t-tzpick"></select>
        <span class="note">picking a region fills in the field to the right for you</span></div>
      <div class="f"><label>POSIX TZ string</label>
        <input type="text" id="t-tz" placeholder="EST5EDT,M3.2.0,M11.1.0">
        <span class="note">advanced — only edit directly if the region list doesn't have yours</span></div>
    </div>
    <div class="actions"><button class="btn pri" id="t-save">Save &amp; apply</button></div>
  </div>
</section>

<!-- ===================================================================== ai -->
<section id="s-ai">
  <h1>AI backend</h1>
  <p class="lede">Where the device sends what it hears. This is the setting that decides
    which language model answers you.</p>

  <div class="banner warn"><div>
    <b>The device is a microphone and a speaker, not the model.</b>
    It streams Opus audio over a WebSocket and plays back whatever audio comes home. Speech
    recognition, the LLM and the voice all live on the server at the address below. API keys
    never go on the device: sign in to Claude or ChatGPT on the bridge's own page instead.
    <a href="#" data-go="ai-help">How to run one &#8594;</a>
  </div></div>

  <div class="card"><h2>Claude &amp; ChatGPT accounts<span class="hint">kept on the bridge, never on the device</span></h2>
    <table class="kv" id="k-acct"></table>
    <p class="note" id="a-acct-note" style="margin:10px 0 0"></p>
    <div class="actions">
      <a class="btn pri" id="a-acct-open" href="#" target="_blank" rel="noopener"
         style="text-decoration:none">Sign in or switch provider &#8599;</a>
      <button class="btn" id="a-acct-refresh">Refresh</button>
    </div>
  </div>

  <div class="card"><h2>Endpoint</h2>
    <div class="grid">
      <div class="f" style="grid-column:1/-1"><label>WebSocket URL</label>
        <input type="text" id="a-url" placeholder="ws://192.168.1.20:8000/xiaozhi/v1/">
        <span class="note">ws:// or wss:// &mdash; must be reachable from the DEVICE's network</span></div>
      <div class="f"><label>Access token</label>
        <input type="password" id="a-token" placeholder="leave blank to keep current">
        <span class="note" id="a-tokstate"></span></div>
      <div class="f"><label>Protocol version</label>
        <select id="a-ver"><option value="1">1</option><option value="2">2</option><option value="3">3</option></select></div>
      <div class="f" style="grid-column:1/-1"><label>Config / OTA URL</label>
        <input type="text" id="a-ota" placeholder="https://api.tenclass.net/xiaozhi/ota/">
        <span class="note">polled at boot for server settings; automatic firmware replacement is disabled in this build</span></div>
    </div>
    <div class="actions"><button class="btn pri" id="a-save">Save endpoint</button>
      <span class="spacer"></span>
      <span class="note">takes effect on the next conversation</span></div>
  </div>

  <div class="card"><h2>Model preference<span class="hint">sent to your server in the opening handshake</span></h2>
    <div class="grid">
      <div class="f"><label>Provider</label>
        <select id="a-prov">
          <option value="">(server decides)</option>
          <option value="anthropic">Anthropic</option>
          <option value="openai">OpenAI</option>
          <option value="ollama">Ollama (local)</option>
          <option value="groq">Groq</option>
          <option value="openai-compatible">Other OpenAI-compatible</option>
          <!-- No "Google" option: server/bridge has no real Gemini integration --
               it used to silently route Google-selected requests through OpenAI's own
               endpoint instead, which is worse than not offering it. -->
        </select></div>
      <div class="f"><label>Tier</label>
        <select id="a-tier">
          <option value="budget">Budget (cheapest / free-tier friendly)</option>
          <option value="balanced">Balanced</option>
          <option value="best">Best (needs a paid/full account)</option>
          <option value="">(custom model, ignore tier)</option>
        </select>
        <span class="note">Picks a model for you. There's no way for this device to know
          what your API key can actually do, so this defaults to the cheapest option &mdash;
          only move it up if you know your account supports it.</span></div>
      <div class="f" style="grid-column:1/-1"><label>Model</label>
        <input type="text" id="a-model" placeholder="claude-sonnet-5">
        <span class="note">Filled in automatically from Provider + Tier above. Edit
          directly (and set Tier to "custom") to pin an exact model name instead.</span></div>
    </div>
    <p class="note" style="margin:12px 0 0">Provider and model are advisory. The device
      announces them in its <span style="font-family:var(--mono)">hello</span> message; a
      server that understands them switches model, and one that does not ignores them
      harmlessly. On the StackyChan bridge, a provider picked on its sign-in page overrides
      this.</p>
    <div class="actions">
      <button class="btn pri" id="a-save2">Save preference</button>
    </div>
  </div>

  <div class="card" id="ai-help"><h2>Running your own backend</h2>
    <p class="note" style="margin:0 0 10px">The protocol is small and documented in this repo at
      <span style="font-family:var(--mono)">server/bridge/README.md</span>. In outline, your server:</p>
    <table class="kv">
      <tr><td>accepts</td><td>a WebSocket, with Device-Id and Client-Id headers</td></tr>
      <tr><td>receives</td><td>a JSON hello, then binary Opus frames at 16 kHz mono</td></tr>
      <tr><td>does</td><td>speech to text &#8594; your LLM &#8594; text to speech</td></tr>
      <tr><td>returns</td><td>Opus frames, plus JSON for the on-screen text and emotion</td></tr>
    </table>
    <p class="note" style="margin:12px 0 0">The repo ships a reference bridge that already speaks
      this and calls Anthropic, OpenAI or a local Ollama. Point the URL above at it and the
      persona below becomes the system prompt.</p>
  </div>

  <div class="card"><h2>Media (KIE.ai)<span class="hint">connection only, for now</span></h2>
    <p class="note" style="margin:0 0 10px">Just a model preference — nothing calls this yet.
      The API key lives on the bridge server, same as your Anthropic/OpenAI keys, never here.</p>
    <div class="f"><label>Model</label>
      <select id="k-model">
        <option value="kling-2.6/text-to-video">Kling 2.6 (text to video)</option>
        <option value="flux/dev">Flux (text to image)</option>
      </select>
      <span class="note">The default here matches the bridge's own default — change both if you
        switch, so a fresh device and a fresh bridge still agree.</span></div>
    <div class="actions"><button class="btn pri" id="k-save">Save preference</button></div>
  </div>

  <div class="card"><h2>Weather</h2>
    <div class="f"><label>Location</label>
      <input type="text" id="wx-location" placeholder="Chicago, IL">
      <span class="note">a city/state name, or "lat,lon" (e.g. 41.88,-87.63) — used both when you
        ask out loud and by the on-device Weather screen. Free lookup (Open-Meteo), no API key
        needed.</span></div>
    <div class="actions"><button class="btn pri" id="wx-save">Save</button></div>
  </div>

  <div class="card"><h2>Stock ticker</h2>
    <div class="f"><label>Symbol</label>
      <input type="text" id="stk-symbol" placeholder="AAPL" style="text-transform:uppercase">
      <span class="note">one ticker symbol for the on-device Stocks screen. Uses a free,
        no-key public quote feed — good for a rough number on the desk, not for trading
        decisions.</span></div>
    <div class="actions"><button class="btn pri" id="stk-save">Save</button></div>
  </div>
</section>

<!-- ================================================================ persona -->
<section id="s-persona">
  <h1>Personas</h1>
  <p class="lede">A persona is a name, a system prompt and optionally a face. The selected one
    travels with every conversation, so the character is a device setting rather than something
    you re-explain to the model each time.</p>

  <div class="card"><h2>Saved personas</h2>
    <div class="rows" id="plist"></div>
    <div class="actions"><button class="btn" id="p-new">New persona</button>
      <span class="spacer"></span><span class="note" id="p-count"></span></div>
  </div>

  <div class="card" id="peditcard" style="display:none"><h2>Edit</h2>
    <div class="grid">
      <div class="f"><label>Name</label><input type="text" id="p-name" placeholder="MAX"></div>
      <div class="f"><label>Voice</label>
        <select id="p-voice">
          <option value="">(server decides)</option>
          <option value="alloy">Alloy</option>
          <option value="echo">Echo</option>
          <option value="fable">Fable</option>
          <option value="onyx">Onyx</option>
          <option value="nova">Nova</option>
          <option value="shimmer">Shimmer</option>
        </select></div>
      <div class="slider"><label>speed</label>
        <input type="range" id="p-speed" min="0.25" max="2" step="0.05" value="1">
        <span class="v" id="p-speedv">1.0&times;</span></div>
      <div class="f"><label>Face</label><select id="p-skin"></select></div>
    </div>
    <div class="f" style="margin-top:12px"><label>System prompt</label>
      <textarea id="p-prompt" maxlength="700" placeholder="You are MAX, a fast-talking television host..."></textarea>
      <span class="note"><span id="p-len">0</span> / 700 characters</span></div>
    <div class="actions"><button class="btn pri" id="p-save">Save persona</button>
      <button class="btn danger" id="p-del">Delete</button>
      <button class="btn" id="p-cancel">Cancel</button></div>
  </div>
</section>

<!-- ================================================================= memory -->
<section id="s-memory">
  <h1>Memory</h1>
  <p class="lede">Two different things live here. Notes are facts you write once, sent
    with every conversation. Conversation memory is how much of the current back-and-forth
    the model can actually see &mdash; true short-term memory, but it forgets everything
    the moment a conversation ends.</p>

  <div class="card"><h2>Things to remember</h2>
    <div class="f">
      <textarea id="mem-notes" rows="8" maxlength="2000"
        placeholder="e.g. Lives in Chicago. Has a cat named Pixel. Prefers short answers."></textarea>
      <span class="note">Plain text, up to 2000 characters. Sent with every conversation
        regardless of which persona is selected. You edit these directly; the model does
        not write to them itself.</span></div>
    <div class="actions"><button class="btn pri" id="mem-save">Save</button></div>
  </div>

  <div class="card"><h2>Conversation memory<span class="hint">short-term, resets when a conversation ends</span></h2>
    <div class="f"><label>Turns to remember</label>
      <input type="number" id="mem-turns" min="1" max="50" placeholder="bridge default">
      <span class="note">How many back-and-forth exchanges the backend keeps in context
        before it starts forgetting the oldest ones. Leave blank to use whatever the
        backend server itself is set to. Only applies when talking to this fork's own
        <code>server/bridge</code> &mdash; the stock xiaozhi.me cloud service has no
        equivalent setting.</span></div>
    <div class="actions"><button class="btn pri" id="mem-turns-save">Save</button></div>
  </div>
</section>

<!-- =================================================================== face -->
<section id="s-face">
  <h1>Face &amp; body</h1>
  <p class="lede">The character on screen, and live control of the head, eyes and lights.
    Movement here goes straight to the same JSON control plane the dance app uses.</p>

  <div class="card"><h2>Character<span class="hint">applies on the next boot</span></h2>
    <div class="choices" id="skins"></div>
    <div class="actions" id="skinact" style="display:none">
      <span class="note">Saved. The face is built once when an app starts, so it changes on reboot.</span>
      <span class="spacer"></span><button class="btn" id="skinreboot">Reboot now</button></div>
  </div>

  <div class="card"><h2>Expression</h2>
    <div class="emos" id="emos"></div>
    <div class="slider"><label>mouth</label><input type="range" id="mouth" min="0" max="100" value="20"><span class="v" id="mouthv">20</span></div>
    <div class="slider"><label>eyes</label><input type="range" id="eyes" min="0" max="100" value="100"><span class="v" id="eyesv">100</span></div>
    <div class="slider"><label>look x</label><input type="range" id="gx" min="-100" max="100" value="0"><span class="v" id="gxv">0</span></div>
    <div class="slider"><label>look y</label><input type="range" id="gy" min="-100" max="100" value="0"><span class="v" id="gyv">0</span></div>
  </div>

  <div class="card"><h2>Head<span class="hint">tenths of a degree &middot; firmware clamps pitch to 30&ndash;870</span></h2>
    <div class="slider"><label>yaw</label><input type="range" id="yaw" min="-1280" max="1280" step="10" value="0"><span class="v" id="yawv">0</span></div>
    <div class="slider"><label>pitch</label><input type="range" id="pitch" min="50" max="850" step="10" value="450"><span class="v" id="pitchv">450</span></div>
    <div class="actions"><button class="btn" id="home">Centre head</button></div>
  </div>

  <div class="card"><h2>Lights</h2>
    <div class="grid">
      <div class="f"><label>Left strip</label><input type="color" id="ledl" value="#6c8cff"></div>
      <div class="f"><label>Right strip</label><input type="color" id="ledr" value="#2fd4a0"></div>
    </div>
    <div class="actions"><button class="btn" id="ledoff">All off</button></div>
  </div>

  <div class="card"><h2>Screensaver<span class="hint">launcher menu only</span></h2>
    <p class="note" style="margin:0 0 12px">Shows when the device sits idle on its app menu
      &mdash; not while a conversation is running. Applies within about a second, no reboot.</p>
    <div class="toggle" id="scr-on"><button data-m="1" class="on">On</button><button data-m="0">Off</button></div>
    <div class="grid" style="margin-top:15px">
      <div class="f"><label>Idle time before it shows</label>
        <input type="text" id="scr-secs" placeholder="30">
        <span class="note">seconds</span></div>
      <div class="f"><label>Graphics</label>
        <select id="scr-style">
          <option value="0">Bouncing logo</option>
          <option value="1">Blank (screen off look)</option>
        </select></div>
    </div>
    <div class="actions"><button class="btn pri" id="scr-save">Save</button></div>
  </div>
</section>

<!-- ================================================================== music -->
<section id="s-music">
  <h1>Music</h1>
  <p class="lede">Upload MP3s to the microSD card, then say "dance to &lt;song&gt;" and
    StackChan plays it through its own speaker while dancing. Requires a formatted
    microSD card in the slot &mdash; there is no fallback storage.</p>

  <div class="card"><h2>SD card status</h2>
    <div class="note" id="music-sdstate">checking&hellip;</div>
  </div>

  <div class="card" id="music-upload-card" style="display:none">
    <h2>Upload a song<span class="hint">.mp3 files only, up to 30 MB</span></h2>
    <div id="music-drop">Drop an <b>.mp3</b> here, or click to choose<br>
      <span class="note">sent in 32 KB chunks &middot; each one retried on its own</span></div>
    <input type="file" id="music-file" accept=".mp3" hidden>
    <progress id="music-prog" value="0" max="100" style="display:none"></progress>
    <div class="msg" id="music-msg"></div>
  </div>

  <div class="card" id="music-list-card" style="display:none">
    <h2>Uploaded songs</h2>
    <div class="rows" id="music-songs"></div>
  </div>
</section>

<!-- =================================================================== wake -->
<section id="s-wake">
  <h1>Wake word</h1>
  <p class="lede">Which phrase starts a conversation. Pick one of the trained phrases below
    &mdash; each is a dedicated neural network built into this firmware, so it's the most
    accurate option available. Takes effect on a reboot.</p>

  <div class="card"><h2>Trained phrases</h2>
    <div class="choices" id="wakes"></div>
    <div class="note" id="wakenote" style="margin-top:12px"></div>
    <div class="actions" style="margin-top:12px">
      <button class="btn" id="wakereboot">Reboot now</button></div>
  </div>
</section>

<!-- ================================================================= update -->
<section id="s-update">
  <h1>Update</h1>
  <p class="lede">Firmware is written to the spare slot, never the running one. A bad image
    costs a reboot: the bootloader rolls back on its own if the new build fails to stay up.</p>

  <div class="card"><h2>Upload firmware</h2>
    <div id="drop">Drop <b>stack-chan.bin</b> here, or click to choose<br>
      <span class="note">sent in 32 KB chunks &middot; each one retried on its own</span></div>
    <input type="file" id="file" accept=".bin" hidden>
    <progress id="prog" value="0" max="100" style="display:none"></progress>
    <div class="msg" id="otamsg"></div>
  </div>

  <div class="card"><h2>Pull from a URL<span class="hint">the device downloads it itself</span></h2>
    <div class="f"><input type="text" id="url" placeholder="http://192.168.1.5:8000/stack-chan.bin">
      <span class="note">must be reachable from the device's network, which may not be yours</span></div>
    <div class="actions"><button class="btn" id="pull">Fetch &amp; flash</button></div>
  </div>

  <div class="card" style="border-color:rgba(255,107,107,.35)">
    <h2>Restore stock firmware<span class="hint">one-way — this fork and this portal go away</span></h2>
    <p class="note" style="margin:0 0 12px">Reverts the running code to real M5Stack factory
      firmware — extracted from M5Stack's own release, written to the spare slot exactly like
      any other update, never touching the bootloader or partition table. Wi-Fi and servo
      calibration are <b>kept</b>; the USB recovery kit's full factory reset clears those,
      this does not. On-screen graphics may look wrong until stock's own updater re-syncs its
      assets, and once stock boots it resumes auto-updating itself — this fork's
      anti-auto-overwrite guard no longer applies to it. <b>This portal will not exist after
      it reboots.</b> Getting back needs the USB recovery kit, or a fresh URL pull of this
      fork's own firmware if stock still reaches this network. <b>Not set up in a default
      build:</b> it needs the address of an app image you host yourself, and until one is
      set the button only explains how.</p>
    <div class="actions" style="margin-top:0;padding-top:0;border:none">
      <button class="btn" id="stock" style="border-color:rgba(255,107,107,.45);color:var(--bad)">Restore stock firmware</button></div>
    <div class="msg" id="stockmsg"></div>
  </div>

  <div class="card"><h2>Network test<span class="hint">tells a network fault from a firmware fault</span></h2>
    <p class="note" style="margin:0 0 12px">Sends 1 MB to an endpoint that counts the bytes and
      throws them away. If this succeeds but an upload does not, the problem is in the OTA path.
      If this fails too, it is the network between you and the device.</p>
    <div class="actions" style="margin-top:0;padding-top:0;border:none">
      <button class="btn" id="echo">Run test</button></div>
  </div>
</section>

<div class="pagefoot" id="pagefoot"></div>
</main>
</div>
<div id="toast"></div>

<script>
"use strict";
function $(id){ return document.getElementById(id); }
function el(t,c,x){ var e=document.createElement(t); if(c)e.className=c; if(x!=null)e.textContent=x; return e; }
function esc(s){ return String(s==null?"":s); }

function jget(p){ return fetch(p).then(function(r){
  if(!r.ok) throw new Error("HTTP "+r.status); return r.json(); }); }
function jpost(p,o){ return fetch(p,{method:"POST",body:JSON.stringify(o||{})}).then(function(r){
  if(!r.ok) return r.text().then(function(t){ throw new Error(t||("HTTP "+r.status)); });
  return r.json(); }); }

function toast(text,kind){
  var t=el("div","t"+(kind?" "+kind:""),text);
  $("toast").appendChild(t);
  setTimeout(function(){ t.style.opacity="0"; t.style.transition="opacity .3s";
    setTimeout(function(){ t.remove(); },300); }, kind==="bad"?6000:2600);
}

/* ------------------------------------------------------------ navigation -- */
var SECTIONS=["overview","network","time","ai","persona","memory","face","music","wake","update"];
function go(name){
  if(SECTIONS.indexOf(name)<0) return;
  SECTIONS.forEach(function(s){ $("s-"+s).classList.toggle("on", s===name); });
  var links=document.querySelectorAll("nav a");
  for(var i=0;i<links.length;i++){ links[i].classList.toggle("on", links[i].dataset.go===name); }
  if(location.hash!=="#"+name) history.replaceState(null,"","#"+name);
  if(LOADERS[name]) LOADERS[name]();
}
document.addEventListener("click",function(e){
  var a=e.target.closest("[data-go]"); if(!a) return;
  e.preventDefault();
  var target=a.dataset.go;
  if(target==="ai-help"){ go("ai"); $("ai-help").scrollIntoView({behavior:"smooth"}); return; }
  go(target);
});

/* ---------------------------------------------------------------- overview -- */
function loadOverview(){
  jget("/api/info").then(function(d){
    $("dot").className="dot up";
    $("navfoot").textContent=d.version+"\n"+d.ip;
    $("pagefoot").textContent="StackyChan "+d.version;
    kv("k-fw",[["version",d.version],["compiled",d.compiled],["running slot",d.partition],
               ["uptime",dur(d.uptime_ms)],["free heap",(d.free_heap/1024).toFixed(0)+" KB"]]);
    return d;
  }).then(function(info){
    // Character, clock and persona come from three endpoints but read as one fact
    // about the device, so they are rendered together rather than as they arrive.
    return Promise.all([
      jget("/api/time").catch(function(){ return null; }),
      jget("/api/personas").catch(function(){ return null; }),
      jget("/api/wake").catch(function(){ return null; })
    ]).then(function(r){
      var t=r[0], p=r[1], w=r[2];
      var persona="none selected";
      if(p && p.active){
        (p.personas||[]).forEach(function(x){ if(x.id===p.active) persona=x.name||x.id; });
      }
      var phrase="—";
      if(w){ (w.models||[]).forEach(function(m){ if(m.active) phrase="“"+m.phrase+"”"; }); }
      kv("k-char",[["face",info.skin],["persona",persona],["wake word",phrase],
                   ["clock", t ? (t.now + (t.synced?"":"  (never synced)")) : "—"]]);
    });
  }).catch(function(){ $("dot").className="dot down"; });

  jget("/api/net").then(function(d){
    var s=d.status;
    kv("k-net",[["ip",s.ip||"—"],["mode",s.dhcp?"DHCP":"static"],
                ["gateway",s.gateway||"—"],["dns",[s.dns1,s.dns2].filter(Boolean).join(", ")||"—"],
                ["ssid",s.ssid||"—"],["signal",s.rssi?s.rssi+" dBm":"—"],
                ["mac",s.mac||"—"]]);
    maybeConfirm(d);
  }).catch(function(){});
}
function dur(ms){
  var s=Math.floor(ms/1000), d=Math.floor(s/86400), h=Math.floor(s%86400/3600),
      m=Math.floor(s%3600/60);
  if(d) return d+"d "+h+"h"; if(h) return h+"h "+m+"m";
  if(m) return m+"m "+(s%60)+"s"; return s+"s";
}
function kv(id,rows){
  var t=$(id); if(!t) return;
  t.innerHTML="";
  rows.forEach(function(r){
    var tr=el("tr"); tr.appendChild(el("td",null,r[0])); tr.appendChild(el("td",null,esc(r[1])));
    t.appendChild(tr);
  });
}

/* The other half of the static-IP handshake: simply arriving here confirms the address. */
function maybeConfirm(d){
  if(!d.revert_in || d.revert_in<=0){ $("confirmbanner").innerHTML=""; return; }
  jpost("/api/net/confirm",{}).then(function(){
    var b=el("div","banner ok");
    b.innerHTML="<div><b>New address confirmed.</b>This page reached the device at its "+
                "static address, so the pending rollback to DHCP has been cancelled.</div>";
    $("confirmbanner").innerHTML=""; $("confirmbanner").appendChild(b);
    toast("Static address confirmed","ok");
  }).catch(function(){});
}

$("reboot").onclick=function(){
  jpost("/api/reboot",{}).then(function(){
    toast("Rebooting…"); $("dot").className="dot down";
    setTimeout(loadOverview,9000);
  }).catch(function(e){ toast(e.message,"bad"); });
};

/* ----------------------------------------------------------------- network -- */
var netStatic=false;
function loadNetwork(){
  jget("/api/net").then(function(d){
    var c=d.config, s=d.status;
    netStatic=c.static_ip;
    setToggle("netmode", c.static_ip?1:0);
    $("n-ip").value=c.ip||""; $("n-mask").value=c.netmask||"";
    $("n-gw").value=c.gateway||""; $("n-dns1").value=c.dns1||"";
    $("n-dns2").value=c.dns2||""; $("n-host").value=c.hostname||"";
    netFields();
    kv("k-netlive",[["ip",s.ip||"—"],["netmask",s.netmask||"—"],
      ["gateway",s.gateway||"—"],["dns 1",s.dns1||"—"],["dns 2",s.dns2||"—"],
      ["mac",s.mac||"—"],["ssid",s.ssid||"—"],
      ["signal",s.rssi?s.rssi+" dBm":"—"],
      ["assigned by",s.dhcp?"DHCP":"static configuration"]]);
  }).catch(function(e){ toast("Could not read network: "+e.message,"bad"); });
}
function netFields(){
  ["n-ip","n-mask","n-gw","n-dns1","n-dns2"].forEach(function(id){ $(id).disabled=!netStatic; });
  $("staticwarn").style.display = netStatic?"flex":"none";
}
function setToggle(id,val){
  var bs=$(id).querySelectorAll("button");
  for(var i=0;i<bs.length;i++){ bs[i].classList.toggle("on", +bs[i].dataset.m===val); }
}
$("netmode").onclick=function(e){
  if(e.target.tagName!=="BUTTON") return;
  netStatic = e.target.dataset.m==="1";
  setToggle("netmode", netStatic?1:0); netFields();
};

$("n-save").onclick=function(){
  var body={ static_ip:netStatic, ip:$("n-ip").value.trim(), netmask:$("n-mask").value.trim(),
             gateway:$("n-gw").value.trim(), dns1:$("n-dns1").value.trim(),
             dns2:$("n-dns2").value.trim(), hostname:$("n-host").value.trim()||"stackchan",
             revert_seconds:120 };
  if(netStatic && (!body.ip||!body.netmask)){ toast("Static mode needs an IP and a netmask","bad"); return; }
  $("n-save").disabled=true;
  jpost("/api/net",body).then(function(d){
    $("n-save").disabled=false;
    if(!d.staged){ toast("Saved — back on DHCP","ok"); setTimeout(loadNetwork,2500); return; }
    var url="http://"+d.next_ip+"/";
    var b=el("div","banner warn");
    b.innerHTML="<div><b>The device is moving to "+d.next_ip+".</b>"+
      "This page is now talking to an address that no longer answers. Open "+
      "<a href='"+url+"'>"+url+"</a> to confirm the change — loading it there is the "+
      "confirmation. If you do not, the device reverts to DHCP and reboots in "+
      d.revert_in+" seconds.</div>";
    var host=$("s-network"); host.insertBefore(b, host.children[2]);
    $("dot").className="dot down";
  }).catch(function(e){ $("n-save").disabled=false; toast(e.message,"bad"); });
};

/* -------------------------------------------------------------------- time -- */
var TZS=[
  ["America/New_York","EST5EDT,M3.2.0,M11.1.0"],
  ["America/Chicago","CST6CDT,M3.2.0,M11.1.0"],
  ["America/Denver","MST7MDT,M3.2.0,M11.1.0"],
  ["America/Phoenix","MST7"],
  ["America/Los_Angeles","PST8PDT,M3.2.0,M11.1.0"],
  ["America/Anchorage","AKST9AKDT,M3.2.0,M11.1.0"],
  ["Pacific/Honolulu","HST10"],
  ["UTC","UTC0"],
  ["Europe/London","GMT0BST,M3.5.0/1,M10.5.0"],
  ["Europe/Berlin","CET-1CEST,M3.5.0,M10.5.0/3"],
  ["Europe/Moscow","MSK-3"],
  ["Asia/Kolkata","IST-5:30"],
  ["Asia/Shanghai","CST-8"],
  ["Asia/Tokyo","JST-9"],
  ["Australia/Sydney","AEST-10AEDT,M10.1.0,M4.1.0/3"]
];
(function(){
  var sel=$("t-tzpick");
  TZS.forEach(function(t){ var o=el("option",null,t[0]); o.value=t[1]; sel.appendChild(o); });
  var custom=el("option",null,"Custom…"); custom.value="__custom"; sel.appendChild(custom);
  sel.onchange=function(){ if(sel.value!=="__custom") $("t-tz").value=sel.value; };
})();

var ntpOn=true;
function loadTime(){
  jget("/api/time").then(function(d){
    ntpOn=d.enabled;
    setToggle("ntpmode", d.enabled?1:0);
    $("t-1").value=d.ntp1||""; $("t-2").value=d.ntp2||""; $("t-3").value=d.ntp3||"";
    $("t-tz").value=d.tz||"";
    var match=""; TZS.forEach(function(t){ if(t[1]===d.tz) match=t[1]; });
    $("t-tzpick").value = match || "__custom";
    kv("k-clock",[["device time",d.now],
      ["status", d.synced?"synced":"never synced"],
      ["timezone",d.tz],["ntp",d.enabled?"enabled":"disabled"]]);
  }).catch(function(e){ toast("Could not read time: "+e.message,"bad"); });
}
$("ntpmode").onclick=function(e){
  if(e.target.tagName!=="BUTTON") return;
  ntpOn = e.target.dataset.m==="1"; setToggle("ntpmode", ntpOn?1:0);
};
$("t-save").onclick=function(){
  jpost("/api/time",{ ntp1:$("t-1").value.trim(), ntp2:$("t-2").value.trim(),
    ntp3:$("t-3").value.trim(), tz:$("t-tz").value.trim(), enabled:ntpOn })
  .then(function(){ toast("Time settings applied","ok"); loadTime(); })
  .catch(function(e){ toast(e.message,"bad"); });
};
$("t-sync").onclick=function(){
  $("t-sync").disabled=true;
  jpost("/api/time/sync",{}).then(function(d){
    toast(d.ok?"Resync requested":"NTP is disabled", d.ok?"ok":"bad");
    setTimeout(function(){ $("t-sync").disabled=false; loadTime(); },2500);
  }).catch(function(e){ $("t-sync").disabled=false; toast(e.message,"bad"); });
};

/* ---------------------------------------------------------------------- ai -- */
// Mirrors server/bridge/config.py's resolved_llm_model() defaults for "balanced", plus a
// cheaper "budget" and a stronger "best" per provider. No entry for openai-compatible: it
// points at an arbitrary server (LM Studio, vLLM, OpenRouter, ...) with its own model
// names, so there's nothing sane to guess -- that provider always falls to "custom".
var AI_TIER_MODELS = {
  anthropic: {budget:"claude-haiku-4-5",       balanced:"claude-sonnet-5",          best:"claude-opus-5"},
  openai:    {budget:"gpt-4o-mini",            balanced:"gpt-4o-mini",              best:"gpt-4o"},
  groq:      {budget:"llama-3.1-8b-instant",   balanced:"llama-3.3-70b-versatile",  best:"llama-3.3-70b-versatile"},
  ollama:    {budget:"llama3.2",               balanced:"llama3.2",                 best:"llama3.2"}
};
function applyTierModel(){
  var models = AI_TIER_MODELS[$("a-prov").value], tier = $("a-tier").value;
  if(models && tier && models[tier]) $("a-model").value = models[tier];
}
$("a-prov").onchange=applyTierModel;
$("a-tier").onchange=applyTierModel;

function loadAi(){
  jget("/api/ai").then(function(d){
    $("a-url").value=d.url||"";
    $("a-ota").value=d.ota_url||"";
    $("a-ver").value=String(d.version||1);
    $("a-prov").value=d.provider||"";
    $("a-model").value=d.model||"";
    // Best-effort: if the saved model matches one of this provider's tiers, reflect that
    // in the dropdown instead of leaving it on whatever it defaulted to. Doesn't touch
    // the model field itself either way.
    var models = AI_TIER_MODELS[d.provider], matched = "";
    if(models){
      for(var t in models){ if(models[t]===d.model){ matched=t; break; } }
    }
    $("a-tier").value = matched;
    $("a-tokstate").textContent = d.token_set
      ? "a token is stored — typing here replaces it"
      : "no token stored";
    loadAccounts();
  }).catch(function(e){ toast("Could not read AI settings: "+e.message,"bad"); });
  loadKie();
  loadWeatherLocation();
  loadStockSymbol();
}
function saveAi(extra){
  var body={ url:$("a-url").value.trim(), ota_url:$("a-ota").value.trim(),
             version:+$("a-ver").value, provider:$("a-prov").value,
             model:$("a-model").value.trim() };
  var tok=$("a-token").value;
  if(tok) body.token=tok;
  return jpost("/api/ai",body).then(function(){
    $("a-token").value="";
    toast(extra||"Saved — applies to the next conversation","ok");
    loadAi();
  }).catch(function(e){ toast(e.message,"bad"); });
}
$("a-save").onclick=function(){ saveAi(); };
$("a-save2").onclick=function(){ saveAi("Model preference saved"); };

/* Keys live on the bridge, so this card only reads the bridge's public status (it sends
   CORS for exactly this) and links to the bridge's own sign-in page. */
function bridgeHttp(ws){
  var m=/^(wss?):\/\/([^\/?#]+)/i.exec(ws||"");
  return m ? (m[1].toLowerCase()==="wss"?"https://":"http://")+m[2]+"/" : "";
}
var acctSeq=0;
function loadAccounts(){
  var base=bridgeHttp($("a-url").value.trim()), open=$("a-acct-open"), seq=++acctSeq;
  open.style.display = base ? "" : "none";
  open.href = base || "#";
  kv("k-acct",[]);
  if(!base){
    $("a-acct-note").textContent="Set the WebSocket URL below to your StackyChan bridge first.";
    return;
  }
  $("a-acct-note").textContent="Checking "+base+" …";
  var ctl = window.AbortController ? new AbortController() : null;
  var timer = setTimeout(function(){ if(ctl) ctl.abort(); }, 5000);
  fetch(base+"api/accounts", ctl ? {signal:ctl.signal} : {}).then(function(r){
    if(!r.ok) throw new Error("HTTP "+r.status);
    return r.json();
  }).then(function(d){
    clearTimeout(timer);
    if(seq!==acctSeq) return;
    var p=d.providers||{}, rows=[];
    ["anthropic","openai"].forEach(function(k){
      if(p[k]) rows.push([p[k].label, p[k].signed_in ? "signed in" : "not signed in"]);
    });
    var who = d.active && p[d.active.provider];
    rows.push(["answering", who ? who.label+" · "+d.active.model : "nobody yet"]);
    rows.push(["hearing & voice", d.speech_ready ? "ready" : "needs ChatGPT signed in"]);
    kv("k-acct",rows);
    $("a-acct-note").textContent = who
      ? "Switch between Claude and ChatGPT on the bridge page. It applies from the next thing you say."
      : "Open the bridge page to sign in. Keys are checked with Claude or ChatGPT before they're saved.";
  }).catch(function(){
    clearTimeout(timer);
    if(seq!==acctSeq) return;
    $("a-acct-note").textContent="No sign-in page answered at "+base+". Only the StackyChan bridge "+
      "(server/bridge) has one; other servers manage their own accounts.";
  });
}
$("a-acct-refresh").onclick=loadAccounts;
$("a-url").onchange=loadAccounts;

/* ------------------------------------------------------------------- kie.ai -- */
function loadKie(){
  jget("/api/kie").then(function(d){ if(d.model) $("k-model").value=d.model; })
    .catch(function(e){ toast(e.message,"bad"); });
}
$("k-save").onclick=function(){
  jpost("/api/kie",{model:$("k-model").value}).then(function(){
    toast("Media preference saved","ok");
  }).catch(function(e){ toast(e.message,"bad"); });
};

/* ------------------------------------------------------------------ weather -- */
function loadWeatherLocation(){
  jget("/api/weather").then(function(d){ $("wx-location").value=d.location||""; })
    .catch(function(e){ toast(e.message,"bad"); });
}
$("wx-save").onclick=function(){
  jpost("/api/weather",{location:$("wx-location").value.trim()}).then(function(){
    toast("Weather location saved","ok");
  }).catch(function(e){ toast(e.message,"bad"); });
};

/* -------------------------------------------------------------------- stock -- */
function loadStockSymbol(){
  jget("/api/stock").then(function(d){ $("stk-symbol").value=d.symbol||""; })
    .catch(function(e){ toast(e.message,"bad"); });
}
$("stk-save").onclick=function(){
  jpost("/api/stock",{symbol:$("stk-symbol").value.trim()}).then(function(){
    toast("Stock ticker saved","ok");
    loadStockSymbol();
  }).catch(function(e){ toast(e.message,"bad"); });
};

/* ------------------------------------------------------------------- memory -- */
function loadMemory(){
  jget("/api/memory").then(function(d){
    $("mem-notes").value=d.notes||"";
    $("mem-turns").value = d.history_turns ? String(d.history_turns) : "";
  }).catch(function(e){ toast(e.message,"bad"); });
}
$("mem-save").onclick=function(){
  jpost("/api/memory",{notes:$("mem-notes").value}).then(function(){
    toast("Memory saved","ok");
  }).catch(function(e){ toast(e.message,"bad"); });
};
$("mem-turns-save").onclick=function(){
  var raw = $("mem-turns").value.trim();
  jpost("/api/memory",{history_turns: raw==="" ? 0 : (+raw)}).then(function(){
    toast("Conversation memory saved","ok"); loadMemory();
  }).catch(function(e){ toast(e.message,"bad"); });
};

/* ------------------------------------------------------------------- music -- */
var MUSIC_CHUNK=32768, MUSIC_RETRIES=4;
var mdrop=$("music-drop"), mfile=$("music-file"), mprog=$("music-prog"),
    mmsg=$("music-msg"), musicBusy=false;

function loadMusic(){
  jget("/api/music/list").then(function(d){
    var has = !!d.sd_card_available;
    $("music-sdstate").textContent = has
      ? "Card present and mounted."
      : "No formatted microSD card detected. Insert one and reboot to enable this feature.";
    $("music-sdstate").className = "msg " + (has ? "ok" : "bad");
    $("music-upload-card").style.display = has ? "" : "none";
    $("music-list-card").style.display = has ? "" : "none";
    if(!has) return;

    var host=$("music-songs"); host.innerHTML="";
    (d.songs||[]).forEach(function(s){
      var row=el("div","prow");
      row.style.cssText="display:flex;align-items:center;justify-content:space-between";
      row.appendChild(el("span",null,s.filename+" ("+(s.bytes/1048576).toFixed(2)+" MB)"));
      var del=el("button","btn sm","Delete");
      del.onclick=function(){
        jpost("/api/music/delete",{filename:s.filename}).then(function(){
          toast("Deleted","ok"); loadMusic();
        }).catch(function(e){ toast(e.message,"bad"); });
      };
      row.appendChild(del);
      host.appendChild(row);
    });
    if(!(d.songs||[]).length){
      host.appendChild(el("p","note","No songs uploaded yet."));
    }
  }).catch(function(e){ toast("Could not read the music list: "+e.message,"bad"); });
}

mdrop.onclick=function(){ if(!musicBusy) mfile.click(); };
mdrop.ondragover=function(e){ e.preventDefault(); mdrop.classList.add("hot"); };
mdrop.ondragleave=function(){ mdrop.classList.remove("hot"); };
mdrop.ondrop=function(e){ e.preventDefault(); mdrop.classList.remove("hot");
  if(!musicBusy && e.dataTransfer.files.length) musicUpload(e.dataTransfer.files[0]); };
mfile.onchange=function(){ if(!musicBusy && mfile.files.length) musicUpload(mfile.files[0]); };

function saym(cls,t){ mmsg.className="msg "+cls; mmsg.textContent=t; }
function musicSendChunk(blob,index,total,attempt){
  return fetch("/api/music/chunk",{method:"POST",body:blob,
    headers:{"Content-Type":"application/octet-stream"}}).then(function(r){
    if(r.ok) return r.json();
    return r.text().then(function(t){ throw new Error(t||("HTTP "+r.status)); });
  }).catch(function(err){
    if(attempt<MUSIC_RETRIES){
      saym("warn","chunk "+(index+1)+"/"+total+" failed ("+err.message+")\nretry "+(attempt+1)+" of "+MUSIC_RETRIES+"…");
      return new Promise(function(res){ setTimeout(res,400*(attempt+1)); })
             .then(function(){ return musicSendChunk(blob,index,total,attempt+1); });
    }
    throw err;
  });
}
function musicUpload(f){
  if(!/\.mp3$/i.test(f.name)){ saym("bad","Not an .mp3"); return; }
  if(f.size>30*1048576){ saym("bad","Too large — 30 MB max"); return; }
  musicBusy=true; mprog.style.display="block"; mprog.value=0;
  var total=Math.ceil(f.size/MUSIC_CHUNK), t0=Date.now();
  saym("","preparing "+f.name+" ("+(f.size/1048576).toFixed(2)+" MB, "+total+" chunks)…");

  jpost("/api/music/begin",{filename:f.name,size:f.size}).then(function(){
    var i=0;
    function next(){
      if(i>=total){
        saym("","all chunks sent, finishing…");
        return jpost("/api/music/end",{}).then(function(){
          var secs=((Date.now()-t0)/1000).toFixed(1);
          saym("ok","Uploaded in "+secs+"s.");
          musicBusy=false; toast("Song uploaded","ok"); loadMusic();
        });
      }
      var blob=f.slice(i*MUSIC_CHUNK, Math.min((i+1)*MUSIC_CHUNK,f.size));
      return musicSendChunk(blob,i,total,0).then(function(res){
        i++;
        mprog.value=res.written/res.expected*100;
        return next();
      });
    }
    return next();
  }).catch(function(err){
    musicBusy=false;
    saym("bad","Upload failed: "+err.message);
    fetch("/api/music/abort",{method:"POST",body:"{}"}).catch(function(){});
  });
}

/* ----------------------------------------------------------------- persona -- */
var personas=[], activeId="", editIndex=-1, skinList=[];
function loadPersonas(){
  return jget("/api/skins").then(function(sk){
    skinList=sk.skins;
    var sel=$("p-skin"); sel.innerHTML="";
    var none=el("option",null,"leave unchanged"); none.value="-1"; sel.appendChild(none);
    skinList.forEach(function(s){ var o=el("option",null,s.name); o.value=String(s.id); sel.appendChild(o); });
  }).then(function(){ return jget("/api/personas"); }).then(function(d){
    personas=d.personas||[]; activeId=d.active||"";
    renderPersonas();
  }).catch(function(e){ toast("Could not read personas: "+e.message,"bad"); });
}
function renderPersonas(){
  var host=$("plist"); host.innerHTML="";
  $("p-count").textContent=personas.length+" of 8 used";
  if(!personas.length){
    host.appendChild(el("p","note","No personas yet. The device uses whatever default your backend has."));
  }
  personas.forEach(function(p,i){
    var r=el("div","prow"+(p.id===activeId?" on":""));
    var g=el("div","grow");
    g.appendChild(el("div","nm",p.name||p.id));
    g.appendChild(el("div","pr",p.prompt||"(no prompt)"));
    r.appendChild(g);
    if(p.id===activeId) r.appendChild(el("span","pill ok","ACTIVE"));
    var ed=el("button","btn sm","Edit");
    ed.onclick=function(ev){ ev.stopPropagation(); openEditor(i); };
    r.appendChild(ed);
    r.onclick=function(){ selectPersona(p.id); };
    host.appendChild(r);
  });
}
function selectPersona(id){
  jpost("/api/persona",{id:id}).then(function(d){
    activeId=id; renderPersonas();
    toast(d.reboot_required ? "Persona selected — reboot to see its face" : "Persona selected","ok");
    loadOverview();
  }).catch(function(e){ toast(e.message,"bad"); });
}
function openEditor(i){
  editIndex=i;
  var p = i>=0 ? personas[i] : {name:"",prompt:"",voice:"",speed:1,skin:-1};
  $("p-name").value=p.name||""; $("p-prompt").value=p.prompt||"";
  $("p-voice").value=p.voice||""; $("p-skin").value=String(p.skin==null?-1:p.skin);
  $("p-speed").value=String(p.speed||1); $("p-speedv").textContent=(p.speed||1).toFixed(2)+"×";
  $("p-len").textContent=String(($("p-prompt").value||"").length);
  $("p-del").style.display = i>=0 ? "" : "none";
  $("peditcard").style.display="";
  $("peditcard").scrollIntoView({behavior:"smooth"});
}
$("p-prompt").oninput=function(){ $("p-len").textContent=String(this.value.length); };
$("p-speed").oninput=function(e){ $("p-speedv").textContent=(+e.target.value).toFixed(2)+"×"; };
$("p-new").onclick=function(){
  if(personas.length>=8){ toast("Eight is the limit — delete one first","bad"); return; }
  openEditor(-1);
};
$("p-cancel").onclick=function(){ $("peditcard").style.display="none"; editIndex=-1; };
$("p-save").onclick=function(){
  var name=$("p-name").value.trim();
  if(!name){ toast("Give it a name","bad"); return; }
  var p={ id: editIndex>=0 ? personas[editIndex].id : slug(name),
          name:name, prompt:$("p-prompt").value, voice:$("p-voice").value.trim(),
          speed:+$("p-speed").value, skin:+$("p-skin").value };
  if(editIndex>=0) personas[editIndex]=p; else personas.push(p);
  jpost("/api/personas",{personas:personas}).then(function(d){
    $("peditcard").style.display="none"; editIndex=-1;
    toast(d.reboot_required ? "Persona saved — reboot to see its face" : "Persona saved","ok");
    loadPersonas();
  }).catch(function(e){ toast(e.message,"bad"); });
};
$("p-del").onclick=function(){
  if(editIndex<0) return;
  personas.splice(editIndex,1);
  jpost("/api/personas",{personas:personas}).then(function(){
    $("peditcard").style.display="none"; editIndex=-1;
    toast("Deleted","ok"); loadPersonas();
  }).catch(function(e){ toast(e.message,"bad"); });
};
function slug(s){
  var out=s.toLowerCase().replace(/[^a-z0-9]+/g,"-").replace(/^-+|-+$/g,"");
  return out ? out.slice(0,20) : ("p"+personas.length);
}

/* -------------------------------------------------------------------- face -- */
var SKIN_ICONS={"default":"🙂","max":"📺","volt":"⚡"};
function skinIcon(name){ return SKIN_ICONS[(name||"").toLowerCase()] || "👤"; }
function loadFace(){
  jget("/api/skins").then(function(d){
    var host=$("skins"); host.innerHTML="";
    d.skins.forEach(function(s){
      var b=el("button","choice"+(s.id===d.active?" on":""));
      b.appendChild(el("span","icon",skinIcon(s.name)));
      var body=el("span","body");
      body.appendChild(el("span","t",s.name));
      body.appendChild(el("span","s","skin "+s.id));
      b.appendChild(body);
      b.onclick=function(){
        jpost("/api/skin",{id:s.id}).then(function(d){
          if(d.reboot_required){
            toast("Face set to "+s.name+" — reboot to see it","ok");
            $("skinact").style.display="flex";
          } else {
            toast("Face set to "+s.name,"ok");
            $("skinact").style.display="none";
          }
          loadFace();
        }).catch(function(e){ toast(e.message,"bad"); });
      };
      host.appendChild(b);
    });
  }).catch(function(e){ toast("Could not read skins: "+e.message,"bad"); });

  loadScreensaver();
}
$("skinreboot").onclick=function(){ jpost("/api/reboot",{}).then(function(){ toast("Rebooting…"); }); };

/* -------------------------------------------------------------- screensaver ---- */
var scrOn=true;
function loadScreensaver(){
  jget("/api/screensaver").then(function(d){
    scrOn=!!d.enabled;
    setToggle("scr-on", scrOn?1:0);
    $("scr-secs").value=String(Math.round((d.timeout_ms||30000)/1000));
    $("scr-style").value=String(d.style||0);
  }).catch(function(e){ toast("Could not read screensaver settings: "+e.message,"bad"); });
}
$("scr-on").onclick=function(e){
  if(e.target.tagName!=="BUTTON") return;
  scrOn = e.target.dataset.m==="1"; setToggle("scr-on", scrOn?1:0);
};
$("scr-save").onclick=function(){
  var secs=parseInt($("scr-secs").value,10);
  if(!secs || secs<1){ toast("Enter a number of seconds","bad"); return; }
  jpost("/api/screensaver",{enabled:scrOn, timeout_ms:secs*1000, style:+$("scr-style").value})
    .then(function(){ toast("Screensaver settings saved","ok"); loadScreensaver(); })
    .catch(function(e){ toast(e.message,"bad"); });
};

["Neutral","Happy","Angry","Sad","Doubt","Sleepy"].forEach(function(n,i){
  var b=el("button","btn",n);
  b.onclick=function(){ jpost("/api/emotion",{emotion:i}).catch(function(){}); };
  $("emos").appendChild(b);
});
function eyesNow(){
  jpost("/api/avatar",{leftEye:{weight:+$("eyes").value,x:+$("gx").value,y:+$("gy").value},
                       rightEye:{weight:+$("eyes").value,x:+$("gx").value,y:+$("gy").value}})
    .catch(function(){});
}
function bindSlider(id,vid,fn){
  $(id).oninput=function(e){ $(vid).textContent=e.target.value; fn(+e.target.value); };
}
bindSlider("mouth","mouthv",function(v){ jpost("/api/avatar",{mouth:{weight:v}}).catch(function(){}); });
bindSlider("eyes","eyesv",eyesNow);
bindSlider("gx","gxv",eyesNow);
bindSlider("gy","gyv",eyesNow);
bindSlider("yaw","yawv",function(v){ jpost("/api/motion",{yawServo:{angle:v,speed:500}}).catch(function(){}); });
bindSlider("pitch","pitchv",function(v){ jpost("/api/motion",{pitchServo:{angle:v,speed:500}}).catch(function(){}); });
$("home").onclick=function(){
  $("yaw").value=0; $("yawv").textContent="0";
  $("pitch").value=450; $("pitchv").textContent="450";
  jpost("/api/motion",{yawServo:{angle:0,speed:400},pitchServo:{angle:450,speed:400}}).catch(function(){});
};
function leds(){
  jpost("/api/rgb",{leftRgbColor:$("ledl").value.toUpperCase(),
                    rightRgbColor:$("ledr").value.toUpperCase()}).catch(function(){});
}
$("ledl").oninput=leds; $("ledr").oninput=leds;
$("ledoff").onclick=function(){
  jpost("/api/rgb",{leftRgbColor:"#000000",rightRgbColor:"#000000"}).catch(function(){});
};

/* -------------------------------------------------------------------- wake -- */
function loadWake(){
  jget("/api/wake").then(function(d){
    $("wakenote").textContent = "";

    var host=$("wakes"); host.innerHTML="";
    d.models.forEach(function(m){
      var b=el("button","choice"+(m.active?" on":""));
      b.appendChild(el("span","icon","🎙️"));
      var body=el("span","body");
      body.appendChild(el("span","t","“"+m.phrase+"”"));
      body.appendChild(el("span","s",m.model));
      b.appendChild(body);
      b.onclick=function(){
        jpost("/api/wake",{model:m.model}).then(function(){
          toast("Wake word set to “"+m.phrase+"” — reboot to apply","ok");
        }).then(function(){ loadWake(); })
          .catch(function(e){ toast(e.message,"bad"); });
      };
      host.appendChild(b);
    });
    if(!d.models.length){
      host.appendChild(el("p","note","No trained wake word models are packed into this build."));
    }
  }).catch(function(e){ toast("Could not read wake words: "+e.message,"bad"); });
}
$("wakereboot").onclick=function(){ jpost("/api/reboot",{}).then(function(){ toast("Rebooting…"); }); };

/* --------------------------------------------------------------------- ota -- */
/* Chunked on purpose. A single 3.85MB POST across a router dies silently; small
   independent requests do not, and a failed chunk can be retried on its own. */
var CHUNK=32768, RETRIES=4;
var drop=$("drop"), file=$("file"), prog=$("prog"), msg=$("otamsg"), busy=false;

drop.onclick=function(){ if(!busy) file.click(); };
drop.ondragover=function(e){ e.preventDefault(); drop.classList.add("hot"); };
drop.ondragleave=function(){ drop.classList.remove("hot"); };
drop.ondrop=function(e){ e.preventDefault(); drop.classList.remove("hot");
  if(!busy && e.dataTransfer.files.length) upload(e.dataTransfer.files[0]); };
file.onchange=function(){ if(!busy && file.files.length) upload(file.files[0]); };

function say(cls,t){ msg.className="msg "+cls; msg.textContent=t; }
function postRaw(path,blob){
  return fetch(path,{method:"POST",body:blob,
    headers:{"Content-Type":"application/octet-stream"}});
}
function sendChunk(blob,index,total,attempt){
  return postRaw("/api/ota/chunk",blob).then(function(r){
    if(r.ok) return r.json();
    return r.text().then(function(t){ throw new Error(t||("HTTP "+r.status)); });
  }).catch(function(err){
    if(attempt<RETRIES){
      say("warn","chunk "+(index+1)+"/"+total+" failed ("+err.message+")\nretry "+(attempt+1)+" of "+RETRIES+"…");
      return new Promise(function(res){ setTimeout(res,400*(attempt+1)); })
             .then(function(){ return sendChunk(blob,index,total,attempt+1); });
    }
    throw err;
  });
}
function upload(f){
  if(!/\.bin$/i.test(f.name)){ say("bad","Not a .bin — use firmware/build/stack-chan.bin"); return; }
  busy=true; prog.style.display="block"; prog.value=0;
  var total=Math.ceil(f.size/CHUNK), t0=Date.now();
  say("","preparing "+f.name+" ("+(f.size/1048576).toFixed(2)+" MB, "+total+" chunks)\nerasing slot…");

  jpost("/api/ota/begin",{size:f.size}).then(function(b){
    say("","slot "+b.slot+" erased in "+b.erase_ms+" ms\nsending "+total+" chunks…");
    var i=0;
    function next(){
      if(i>=total){
        say("","all chunks sent, finalising…");
        return jpost("/api/ota/end",{}).then(function(){
          var secs=((Date.now()-t0)/1000).toFixed(1);
          say("ok","Flashed in "+secs+"s. Rebooting into the new image.\nIf it fails to stay up, the bootloader falls back on its own.");
          busy=false; toast("Firmware written","ok"); setTimeout(loadOverview,12000);
        });
      }
      var blob=f.slice(i*CHUNK, Math.min((i+1)*CHUNK,f.size));
      return sendChunk(blob,i,total,0).then(function(res){
        i++;
        prog.value=res.written/res.expected*100;
        if(i%8===0||i===total){
          var kbps=Math.round(res.written/1024/((Date.now()-t0)/1000));
          say("","chunk "+i+"/"+total+"  ·  "+(res.written/1048576).toFixed(2)+" MB  ·  "+kbps+" KB/s");
        }
        return next();
      });
    }
    return next();
  }).catch(function(err){
    busy=false;
    say("bad","Upload failed: "+err.message+"\n\nThe old firmware is untouched — this wrote to the spare slot.\nTry the URL pull, or run the network test.");
    fetch("/api/ota/abort",{method:"POST",body:"{}"}).catch(function(){});
  });
}
$("pull").onclick=function(){
  var u=$("url").value.trim();
  if(!u){ say("bad","enter a URL first"); return; }
  say("","asking the device to fetch "+u+" …");
  jpost("/api/ota_url",{url:u}).then(function(){
    say("ok","Download started ON THE DEVICE.\nWatch its screen; it reboots when done.\nIf nothing happens, that URL is not reachable from the device's network.");
    setTimeout(loadOverview,15000);
  }).catch(function(e){ say("bad","rejected: "+e.message); });
};
/* Restore-stock needs a BARE APP image of M5Stack's firmware on a web server this device
   can reach. M5Stack publish only a full merged flash image, which must never be written
   into an app slot, so there is no public URL to put here. Empty means "not configured":
   the button explains what to do instead of flashing. See workshop/kit/README.md. */
var STOCK_URL="";
var stockmsg=$("stockmsg");
function sayStock(cls,t){ stockmsg.className="msg "+cls; stockmsg.textContent=t; }
$("stock").onclick=function(){
  if(!STOCK_URL){
    sayStock("warn","Not configured in this build.\n\n"+
      "This needs a bare app image of M5Stack's firmware on a web server the device can "+
      "reach. Make one with workshop/kit/extract_stock_app.py and host it. Then either "+
      "paste its address into \"Pull from a URL\" above, or set STOCK_URL in "+
      "portal_page.h and rebuild.\n\n"+
      "Do NOT point either at M5Stack's own download. That file is a full flash image, "+
      "not an app image.\n\n"+
      "The USB recovery kit (5-RESTORE-STOCK.bat) or M5Burner restores stock without any "+
      "of this.");
    return;
  }
  if(!confirm("Restore real M5Stack stock firmware?\n\nThis fork and this portal will not "+
    "exist after it reboots. Wi-Fi and servo calibration are kept. Graphics may look wrong "+
    "until stock's own updater catches up.\n\nThis cannot be undone from this page."))return;
  sayStock("","asking the device to fetch stock firmware …");
  jpost("/api/ota_url",{url:STOCK_URL}).then(function(){
    sayStock("warn","Download started ON THE DEVICE.\nWatch its screen; it reboots into stock "+
      "when done.\nThis page will stop responding once it does — that's expected.");
  }).catch(function(e){ sayStock("bad","rejected: "+e.message); });
};
$("echo").onclick=function(){
  var size=1048576, blob=new Blob([new Uint8Array(size)]);
  say("","sending 1 MB to /api/echo …");
  var t0=Date.now();
  postRaw("/api/echo",blob).then(function(r){ return r.json(); }).then(function(d){
    var secs=(Date.now()-t0)/1000;
    if(d.ok){
      say("ok","Network OK — 1 MB in "+secs.toFixed(1)+"s ("+d.kbps+" KB/s device-side).\nIf this works but firmware upload does not, the problem is in the OTA path, not the network.");
    } else {
      say("bad","Network truncated the transfer: device got "+d.received+" of "+d.expected+" bytes (recv="+d.recv+").\nThat points at the router between you and the device, not the firmware.");
    }
  }).catch(function(e){
    say("bad","echo failed: "+e+"\nThe connection dropped — strong sign the network is the problem.");
  });
};

/* -------------------------------------------------------------------- boot -- */
var LOADERS={ overview:loadOverview, network:loadNetwork, time:loadTime, ai:loadAi,
              persona:loadPersonas, memory:loadMemory, face:loadFace, music:loadMusic,
              wake:loadWake, update:function(){} };

go((location.hash||"#overview").slice(1));
loadOverview();
setInterval(function(){ if($("s-overview").classList.contains("on")) loadOverview(); }, 6000);
</script></body></html>)HTML";
