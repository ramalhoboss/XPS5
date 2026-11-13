'use strict';

// The page of the extension: it finds XPS5 on the PS5 at the address typed,
// reads this browser's x.com session (the auth_token and ct0 cookies) and
// sends it there with the code the PS5 shows. The protocol is in the XPS5
// sources, src/link.h. Nothing here logs the cookies or keeps them.
//
// The page speaks English until the PS5 answers, then the language XPS5
// shows there (the console's own, unless changed in XPS5's Settings), and
// opens in that one the next time. Opened from the page the PS5's QR opens
// (background.js), it comes with the address and the code after '#'.

const PORT = 9070;          // src/link.h, LINK_PORT
const HELLO_MS = 6000;      // for the PS5 to answer that it is there
const SEND_MS = 45000;      // for it to check the session with X
const LANGS = ['en', 'pt', 'es', 'fr', 'de', 'it', 'nl']; // XPS5's, LANG_* of settings.h
const X_ORIGIN = 'https://x.com/*';

// Chrome and Edge have chrome.*, Firefox browser.* (and a chrome.* too)
const api = globalThis.browser ?? globalThis.chrome;

const $ = (id) => document.getElementById(id);
const form = $('link');
const address = $('address');
const code = $('code');
const send = $('send');
const sendLabel = $('send-label');
const status = $('status');
const statusText = $('status-text');
const openX = $('open-x');

let lang = 'en';   // of the words below; the page's own text is English
let words = {};    // lang/<lang>.json
let xGranted = true; // whether x.com's cookies may be read; Firefox lets the user take it away

/** a text of lang/*.json, $1 and $2 filled in */
const t = (key, ...subs) =>
  (words[key] || key).replace(/\$(\d)/g, (_, n) => String(subs[n - 1] ?? ''));

function localize() {
  document.documentElement.lang = lang;
  for (const el of document.querySelectorAll('[data-i18n]')) {
    const text = words[el.dataset.i18n];
    if (text) {
      el.textContent = text;
    }
  }
  sendLabel.textContent = t(send.disabled ? 'sending' : 'send');
}

/** the page in that language (two letters), or in English when it has not it */
async function useLang(code) {
  const want = LANGS.includes(code) ? code : 'en';
  if (want === lang && Object.keys(words).length) {
    return;
  }
  try {
    const response = await fetch(api.runtime.getURL(`lang/${want}.json`));
    words = await response.json();
    lang = want;
  } catch {
    return; // the words it had
  }
  localize();
}

/** what is going on, in the status line: busy, ok or error */
function say(kind, text, { offerX = false } = {}) {
  status.hidden = false;
  status.dataset.kind = kind;
  statusText.textContent = text;
  openX.hidden = !offerX;
}

function setBusy(busy) {
  send.disabled = busy;
  send.setAttribute('aria-busy', String(busy));
  sendLabel.textContent = t(busy ? 'sending' : 'send');
  address.readOnly = busy;
  code.readOnly = busy;
}

function markInvalid(input, invalid) {
  input.setAttribute('aria-invalid', String(invalid));
}

/** the PS5's address as typed: an IPv4 address of a home network, and a port */
function parseAddress(text) {
  const m = /^\s*(?:https?:\/\/)?(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?::(\d{1,5}))?\/?\s*$/.exec(text);
  if (!m) {
    return { error: 'badAddress' };
  }
  const parts = m.slice(1, 5).map(Number);
  const port = m[5] ? Number(m[5]) : PORT;
  if (parts.some((n) => n > 255) || port < 1 || port > 65535) {
    return { error: 'badAddress' };
  }
  const [a, b] = parts;
  const home = a === 10 || a === 127 || (a === 172 && b >= 16 && b <= 31) ||
               (a === 192 && b === 168) || (a === 169 && b === 254);
  if (!home) {
    return { error: 'notLocal' };
  }
  return { host: parts.join('.'), port };
}

/** six digits, with or without the space the PS5 shows */
function parseCode(text) {
  const digits = text.replace(/[\s-]/g, '');
  return /^\d{6}$/.test(digits) ? digits : null;
}

/** a request to the PS5, its answer read as JSON; throws when it does not come */
async function call(url, init, ms) {
  const abort = new AbortController();
  const timer = setTimeout(() => abort.abort(), ms);
  try {
    const response = await fetch(url, {
      ...init, signal: abort.signal, cache: 'no-store', credentials: 'omit', redirect: 'error',
    });
    let body = {};
    try {
      body = await response.json();
    } catch {
      body = {};
    }
    return { ok: response.ok, http: response.status, body: body || {} };
  } finally {
    clearTimeout(timer);
  }
}

async function cookie(name) {
  const found = await api.cookies.get({ url: 'https://x.com/', name });
  return found ? found.value : '';
}

/** the address typed and the PS5's language, for the next time */
async function remember(items) {
  try {
    await api.storage.local.set(items);
  } catch {
    // only a convenience
  }
}

/** what the PS5 answered the session */
function explain({ ok, http, body }) {
  if (ok && body.ok) {
    say('ok', t('done', body.user || '?', body.account || 1));
    return;
  }
  switch (body.error) {
    case 'code':
      markInvalid(code, true);
      say('error', body.renewed ? t('renewedCode') : t('wrongCode', body.left ?? 0));
      code.focus();
      code.select();
      return;
    case 'x':
      say('error', t('xRefused', body.status ?? http));
      return;
    case 'network':
      say('error', t('psOffline'));
      return;
    case 'write':
      say('error', t('psWrite'));
      return;
    case 'cookies':
      say('error', t('badCookies'));
      return;
    default:
      say('error', t('unknown', http));
  }
}

async function link(where, digits, origin, asked) {
  const base = `http://${where.host}:${where.port}`;
  let granted = false;
  let answer = null;

  setBusy(true);
  try {
    try {
      granted = await asked;
    } catch {
      granted = false;
    }
    if (!granted) {
      say('error', t('noPermission'));
      return;
    }

    // the PS5 first, so no cookie is read for nothing
    say('busy', t('looking'));
    try {
      answer = await call(`${base}/hello`, { method: 'GET' }, HELLO_MS);
    } catch {
      answer = null;
    }
    if (!answer || !answer.ok || answer.body.app !== 'XPS5') {
      markInvalid(address, true);
      say('error', t('notFound', where.host));
      address.focus();
      return;
    }
    const told = LANGS.includes(answer.body.lang) ? answer.body.lang : 'en';
    remember({ address: address.value.trim(), lang: told });
    await useLang(told);

    const [authToken, ct0] = await Promise.all([cookie('auth_token'), cookie('ct0')]);
    if (!authToken || !ct0) {
      say('error', t('signIn'), { offerX: true });
      return;
    }

    say('busy', t('checking'));
    try {
      answer = await call(`${base}/session`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ code: digits, auth_token: authToken, ct0 }),
      }, SEND_MS);
    } catch {
      answer = null;
    }
    if (!answer) {
      say('error', t('notFound', where.host));
      return;
    }
    explain(answer);
    if (answer.ok && answer.body.ok) {
      code.value = '';
      // the PS5's address is not needed again until the next time
      api.permissions.remove({ origins: [origin] }).catch(() => {});
    }
  } finally {
    setBusy(false);
  }
}

form.addEventListener('submit', (event) => {
  event.preventDefault();
  if (send.disabled) {
    return;
  }
  const where = parseAddress(address.value);
  const digits = parseCode(code.value);
  markInvalid(address, Boolean(where.error));
  markInvalid(code, !digits);
  if (where.error) {
    say('error', t(where.error));
    address.focus();
    return;
  }
  if (!digits) {
    say('error', t('badCode'));
    code.focus();
    return;
  }
  const origin = `http://${where.host}/*`;
  let asked;
  try {
    // asked at once, while the click still counts as the user's
    asked = api.permissions.request({ origins: xGranted ? [origin] : [origin, X_ORIGIN] });
  } catch {
    asked = Promise.resolve(false);
  }
  link(where, digits, origin, asked);
});

for (const input of [address, code]) {
  input.addEventListener('input', () => markInvalid(input, false));
}

openX.addEventListener('click', () => {
  api.tabs.create({ url: 'https://x.com/' });
});

(async () => {
  // what the PS5's page gave, kept out of the tab's history
  const given = new URLSearchParams(location.hash.slice(1));
  const givenAddress = parseAddress(given.get('address') || '').error ? '' : given.get('address');
  const givenCode = parseCode(given.get('code') || '');
  if (location.hash) {
    history.replaceState(null, '', location.pathname);
  }

  let known = 'en';
  try {
    const kept = await api.storage.local.get(['address', 'lang']);
    if (kept.address) {
      address.value = kept.address;
    }
    known = kept.lang || 'en';
  } catch {
    // nothing kept
  }
  try {
    xGranted = await api.permissions.contains({ origins: [X_ORIGIN] });
  } catch {
    xGranted = false;
  }
  if (givenAddress) {
    address.value = givenAddress;
  }
  if (givenCode) {
    code.value = `${givenCode.slice(0, 3)} ${givenCode.slice(3)}`;
  }
  await useLang(known);
  if (givenAddress && givenCode) {
    say('info', t('prefilled'));
    send.focus();
  } else {
    (address.value ? code : address).focus();
  }
})();
