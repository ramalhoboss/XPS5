'use strict';

// The page XPS5's payload serves on the PS5 while the linking screen is open
// (src/linkserve.c): the phone or the PC that scans the screen's QR opens it.
// It says what to tap: the XPS5 Login extension's button, which reads this
// tab's address and the code after '#' and opens its own page with them
// (tools/xps5-login-extension). Nothing here talks to the PS5 or reads a
// cookie. It speaks the browser's language when XPS5 has it, else English.

const WORDS = {
  en: {
    title: 'Link your X account to XPS5',
    code: 'Code',
    noCode: 'Use the code the PS5 shows on screen.',
    step1: "Tap the XPS5 Login extension's button.",
    step2: 'Check the address and the code, then tap Link.',
    chrome: 'In Chrome: the extensions button (the puzzle piece) beside the address, then XPS5 Login.',
    edge: 'In Edge: the Extensions button beside the address, then XPS5 Login.',
    firefox: 'In Firefox: the extensions button (the puzzle piece), then XPS5 Login.',
    ffAndroid: 'In Firefox: the ⋮ menu, Extensions, then XPS5 Login.',
    other: "Use the XPS5 Login extension's button in the browser's toolbar.",
    noExt: 'This browser does not take extensions. Open this page in Firefox for Android, which does.',
    openFirefox: 'Open in Firefox',
    ios: 'On iPhone and iPad, browsers do not run this extension. Use a PC with Chrome, Edge or Firefox, or an Android phone with Firefox.',
    pc: 'On a PC, open',
    install: 'No extension yet? Install XPS5 Login in Chrome, Edge or Firefox (Firefox for Android too), then open this page again.',
    note: 'The session goes over your home network and stays on the PS5. Do not sign out of X in this browser: that also signs the PS5 out.',
  },
  pt: {
    title: 'Liga a tua conta do X ao XPS5',
    code: 'Código',
    noCode: 'Usa o código que a PS5 mostra no ecrã.',
    step1: 'Toca no botão da extensão XPS5 Login.',
    step2: 'Confirma o endereço e o código e toca em Ligar.',
    chrome: 'No Chrome: o botão das extensões (a peça de puzzle), ao lado do endereço, e depois XPS5 Login.',
    edge: 'No Edge: o botão Extensões, ao lado do endereço, e depois XPS5 Login.',
    firefox: 'No Firefox: o botão das extensões (a peça de puzzle) e depois XPS5 Login.',
    ffAndroid: 'No Firefox: menu ⋮, Extensões e depois XPS5 Login.',
    other: 'Usa o botão da extensão XPS5 Login, na barra do browser.',
    noExt: 'Este browser não aceita extensões. Abre esta página no Firefox para Android, que aceita.',
    openFirefox: 'Abrir no Firefox',
    ios: 'No iPhone e no iPad os browsers não correm esta extensão. Usa um PC com Chrome, Edge ou Firefox, ou um Android com Firefox.',
    pc: 'No PC, abre',
    install: 'Ainda não tens a extensão? Instala a XPS5 Login no Chrome, no Edge ou no Firefox (também no Firefox para Android) e abre esta página outra vez.',
    note: 'A sessão vai pela tua rede de casa e fica só na PS5. Não termines a sessão do X neste browser: isso também a termina na PS5.',
  },
  es: {
    title: 'Vincula tu cuenta de X a XPS5',
    code: 'Código',
    noCode: 'Usa el código que muestra la PS5 en pantalla.',
    step1: 'Toca el botón de la extensión XPS5 Login.',
    step2: 'Comprueba la dirección y el código y toca Vincular.',
    chrome: 'En Chrome: el botón de extensiones (la pieza de puzle), junto a la dirección, y luego XPS5 Login.',
    edge: 'En Edge: el botón Extensiones, junto a la dirección, y luego XPS5 Login.',
    firefox: 'En Firefox: el botón de extensiones (la pieza de puzle) y luego XPS5 Login.',
    ffAndroid: 'En Firefox: menú ⋮, Extensiones y luego XPS5 Login.',
    other: 'Usa el botón de la extensión XPS5 Login, en la barra del navegador.',
    noExt: 'Este navegador no admite extensiones. Abre esta página en Firefox para Android, que sí las admite.',
    openFirefox: 'Abrir en Firefox',
    ios: 'En iPhone y iPad los navegadores no ejecutan esta extensión. Usa un PC con Chrome, Edge o Firefox, o un Android con Firefox.',
    pc: 'En un PC, abre',
    install: '¿Aún no tienes la extensión? Instala XPS5 Login en Chrome, Edge o Firefox (también en Firefox para Android) y vuelve a abrir esta página.',
    note: 'La sesión va por tu red de casa y se queda en la PS5. No cierres la sesión de X en este navegador: eso también la cierra en la PS5.',
  },
  fr: {
    title: 'Associe ton compte X à XPS5',
    code: 'Code',
    noCode: "Utilise le code que la PS5 affiche à l'écran.",
    step1: "Touche le bouton de l'extension XPS5 Login.",
    step2: "Vérifie l'adresse et le code, puis touche Lier.",
    chrome: "Dans Chrome : le bouton des extensions (la pièce de puzzle), à côté de l'adresse, puis XPS5 Login.",
    edge: "Dans Edge : le bouton Extensions, à côté de l'adresse, puis XPS5 Login.",
    firefox: 'Dans Firefox : le bouton des extensions (la pièce de puzzle), puis XPS5 Login.',
    ffAndroid: 'Dans Firefox : le menu ⋮, Extensions, puis XPS5 Login.',
    other: "Utilise le bouton de l'extension XPS5 Login, dans la barre du navigateur.",
    noExt: "Ce navigateur n'accepte pas les extensions. Ouvre cette page dans Firefox pour Android, qui les accepte.",
    openFirefox: 'Ouvrir dans Firefox',
    ios: 'Sur iPhone et iPad, les navigateurs ne font pas tourner cette extension. Utilise un PC avec Chrome, Edge ou Firefox, ou un Android avec Firefox.',
    pc: 'Sur un PC, ouvre',
    install: "Pas encore l'extension ? Installe XPS5 Login dans Chrome, Edge ou Firefox (aussi dans Firefox pour Android), puis rouvre cette page.",
    note: 'La session passe par ton réseau domestique et reste sur la PS5. Ne te déconnecte pas de X dans ce navigateur : cela déconnecte aussi la PS5.',
  },
  de: {
    title: 'Verknüpfe dein X-Konto mit XPS5',
    code: 'Code',
    noCode: 'Nimm den Code, den die PS5 auf dem Bildschirm zeigt.',
    step1: 'Tippe auf die Schaltfläche der Erweiterung XPS5 Login.',
    step2: 'Prüfe Adresse und Code und tippe dann auf Verknüpfen.',
    chrome: 'In Chrome: die Erweiterungen-Schaltfläche (das Puzzleteil) neben der Adresse, dann XPS5 Login.',
    edge: 'In Edge: die Schaltfläche Erweiterungen neben der Adresse, dann XPS5 Login.',
    firefox: 'In Firefox: die Erweiterungen-Schaltfläche (das Puzzleteil), dann XPS5 Login.',
    ffAndroid: 'In Firefox: Menü ⋮, Erweiterungen, dann XPS5 Login.',
    other: 'Nutze die Schaltfläche der Erweiterung XPS5 Login in der Leiste des Browsers.',
    noExt: 'Dieser Browser nimmt keine Erweiterungen. Öffne diese Seite in Firefox für Android, der sie nimmt.',
    openFirefox: 'In Firefox öffnen',
    ios: 'Auf iPhone und iPad führen Browser diese Erweiterung nicht aus. Nutze einen PC mit Chrome, Edge oder Firefox oder ein Android-Handy mit Firefox.',
    pc: 'Am PC öffne',
    install: 'Noch keine Erweiterung? Installiere XPS5 Login in Chrome, Edge oder Firefox (auch in Firefox für Android) und öffne diese Seite noch einmal.',
    note: 'Die Sitzung geht über dein Heimnetz und bleibt auf der PS5. Melde dich in diesem Browser nicht von X ab: Das meldet auch die PS5 ab.',
  },
  it: {
    title: 'Collega il tuo account X a XPS5',
    code: 'Codice',
    noCode: 'Usa il codice che la PS5 mostra sullo schermo.',
    step1: "Tocca il pulsante dell'estensione XPS5 Login.",
    step2: "Controlla l'indirizzo e il codice, poi tocca Collega.",
    chrome: "In Chrome: il pulsante delle estensioni (il pezzo di puzzle), accanto all'indirizzo, poi XPS5 Login.",
    edge: "In Edge: il pulsante Estensioni, accanto all'indirizzo, poi XPS5 Login.",
    firefox: 'In Firefox: il pulsante delle estensioni (il pezzo di puzzle), poi XPS5 Login.',
    ffAndroid: 'In Firefox: menu ⋮, Estensioni, poi XPS5 Login.',
    other: "Usa il pulsante dell'estensione XPS5 Login nella barra del browser.",
    noExt: 'Questo browser non accetta estensioni. Apri questa pagina in Firefox per Android, che le accetta.',
    openFirefox: 'Apri in Firefox',
    ios: 'Su iPhone e iPad i browser non eseguono questa estensione. Usa un PC con Chrome, Edge o Firefox, o un Android con Firefox.',
    pc: 'Su un PC, apri',
    install: "Non hai ancora l'estensione? Installa XPS5 Login in Chrome, Edge o Firefox (anche in Firefox per Android), poi riapri questa pagina.",
    note: 'La sessione passa dalla tua rete di casa e resta sulla PS5. Non uscire da X in questo browser: si scollegherebbe anche la PS5.',
  },
  nl: {
    title: 'Koppel je X-account aan XPS5',
    code: 'Code',
    noCode: 'Gebruik de code die de PS5 op het scherm toont.',
    step1: 'Tik op de knop van de extensie XPS5 Login.',
    step2: 'Controleer het adres en de code en tik dan op Koppelen.',
    chrome: 'In Chrome: de extensieknop (het puzzelstukje) naast het adres, dan XPS5 Login.',
    edge: 'In Edge: de knop Extensies naast het adres, dan XPS5 Login.',
    firefox: 'In Firefox: de extensieknop (het puzzelstukje), dan XPS5 Login.',
    ffAndroid: 'In Firefox: menu ⋮, Extensies, dan XPS5 Login.',
    other: 'Gebruik de knop van de extensie XPS5 Login in de werkbalk van de browser.',
    noExt: 'Deze browser ondersteunt geen extensies. Open deze pagina in Firefox voor Android, die dat wel doet.',
    openFirefox: 'Openen in Firefox',
    ios: 'Op iPhone en iPad draaien browsers deze extensie niet. Gebruik een pc met Chrome, Edge of Firefox, of een Android-telefoon met Firefox.',
    pc: 'Open op een pc',
    install: 'Nog geen extensie? Installeer XPS5 Login in Chrome, Edge of Firefox (ook in Firefox voor Android) en open deze pagina opnieuw.',
    note: 'De sessie gaat via je thuisnetwerk en blijft op de PS5. Log in deze browser niet uit bij X: dan wordt ook de PS5 uitgelogd.',
  },
};

const $ = (id) => document.getElementById(id);

/** the first of the browser's languages XPS5 speaks, else English */
function language() {
  const wanted = navigator.languages && navigator.languages.length ? navigator.languages : [navigator.language || 'en'];
  for (const tag of wanted) {
    const two = String(tag).slice(0, 2).toLowerCase();
    if (WORDS[two]) {
      return two;
    }
  }
  return 'en';
}

/** the six digits after '#' (the QR's) or in ?c= (the Firefox link's), else '' */
function code() {
  const hash = /^#(\d{6})$/.exec(location.hash);
  if (hash) {
    return hash[1];
  }
  const query = /(?:^\?|&)c=(\d{6})(?:&|$)/.exec(location.search);
  return query ? query[1] : '';
}

/** which browser this is, for where its extension button sits */
function browser() {
  const ua = navigator.userAgent;
  if (/iPhone|iPad|iPod/.test(ua) || (/Macintosh/.test(ua) && navigator.maxTouchPoints > 1)) {
    return 'ios';
  }
  if (/Android/.test(ua)) {
    return /Firefox\//.test(ua) ? 'ffAndroid' : 'noExt';
  }
  if (/Firefox\//.test(ua)) {
    return 'firefox';
  }
  if (/Edg\//.test(ua)) {
    return 'edge';
  }
  if (/Chrome\//.test(ua)) {
    return 'chrome';
  }
  return 'other';
}

function show() {
  const lang = language();
  const words = WORDS[lang];
  const digits = code();
  const host = /^\d{1,3}(?:\.\d{1,3}){3}(?::\d{1,5})?$/.test(location.host) ? location.host : '';
  const kind = browser();

  document.documentElement.lang = lang;
  for (const el of document.querySelectorAll('[data-t]')) {
    el.textContent = words[el.dataset.t];
  }

  $('code').textContent = digits ? `${digits.slice(0, 3)} ${digits.slice(3)}` : '—';
  $('no-code').hidden = Boolean(digits);

  if (kind === 'ios' || kind === 'noExt') {
    // nothing to tap here: what to do instead, and the address for a PC
    $('steps').hidden = true;
    $('warn').hidden = false;
    $('warn-text').textContent = words[kind];
    if (kind === 'noExt' && host && digits) {
      const link = $('firefox');
      link.href = `intent://${host}/?c=${digits}#Intent;scheme=http;package=org.mozilla.firefox;end`;
      link.hidden = false;
    }
    if (host) {
      const strong = document.createElement('strong');
      strong.textContent = host;
      $('pc-text').replaceChildren(`${words.pc} `, strong);
      $('pc').hidden = false;
    }
  } else {
    $('where').textContent = words[kind];
  }
}

show();
window.addEventListener('hashchange', show);
