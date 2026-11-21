// Writing on the phone for XPS5 (src/phone.h): the token from the QR's
// address, the PS5's draft, then each change sent as it is typed, and the
// Send button. Nothing here is kept on the phone.
"use strict";

const STRINGS = {
  en: { connecting: "Connecting to the PS5…", on: "Connected to the PS5", off: "Not connected",
        post: "Write your post", reply: "Write your reply", dm: "Write your message", search: "Search X",
        field: "What you write", send: "Send to the PS5", sent: "Sent. You can close this page.",
        hint: "What you type shows on the TV as you type it.",
        badToken: "This code is no longer valid. Scan the QR on the TV again.",
        lost: "The PS5 does not answer. Is the window still open on the TV?",
        noToken: "Scan the QR that XPS5 shows on the TV." },
  pt: { connecting: "A ligar à PS5…", on: "Ligado à PS5", off: "Sem ligação",
        post: "Escreve o teu post", reply: "Escreve a tua resposta", dm: "Escreve a tua mensagem", search: "Pesquisar no X",
        field: "O que escreves", send: "Enviar para a PS5", sent: "Enviado. Já podes fechar esta página.",
        hint: "O que escreves aparece na TV enquanto escreves.",
        badToken: "Este código já não vale. Lê outra vez o QR da TV.",
        lost: "A PS5 não responde. A janela ainda está aberta na TV?",
        noToken: "Lê o QR que o XPS5 mostra na TV." },
  es: { connecting: "Conectando con la PS5…", on: "Conectado a la PS5", off: "Sin conexión",
        post: "Escribe tu post", reply: "Escribe tu respuesta", dm: "Escribe tu mensaje", search: "Buscar en X",
        field: "Lo que escribes", send: "Enviar a la PS5", sent: "Enviado. Ya puedes cerrar esta página.",
        hint: "Lo que escribes aparece en la TV mientras escribes.",
        badToken: "Este código ya no vale. Vuelve a escanear el QR de la TV.",
        lost: "La PS5 no responde. ¿Sigue abierta la ventana en la TV?",
        noToken: "Escanea el QR que XPS5 muestra en la TV." },
  fr: { connecting: "Connexion à la PS5…", on: "Connecté à la PS5", off: "Non connecté",
        post: "Écris ton post", reply: "Écris ta réponse", dm: "Écris ton message", search: "Rechercher sur X",
        field: "Ce que tu écris", send: "Envoyer à la PS5", sent: "Envoyé. Tu peux fermer cette page.",
        hint: "Ce que tu tapes s'affiche sur la TV pendant que tu tapes.",
        badToken: "Ce code n'est plus valable. Scanne à nouveau le QR de la TV.",
        lost: "La PS5 ne répond pas. La fenêtre est-elle encore ouverte sur la TV ?",
        noToken: "Scanne le QR que XPS5 affiche sur la TV." },
  de: { connecting: "Verbinde mit der PS5…", on: "Mit der PS5 verbunden", off: "Nicht verbunden",
        post: "Schreib deinen Post", reply: "Schreib deine Antwort", dm: "Schreib deine Nachricht", search: "X durchsuchen",
        field: "Was du schreibst", send: "An die PS5 senden", sent: "Gesendet. Du kannst diese Seite schließen.",
        hint: "Was du tippst, erscheint beim Tippen auf dem Fernseher.",
        badToken: "Dieser Code gilt nicht mehr. Scanne den QR-Code am Fernseher erneut.",
        lost: "Die PS5 antwortet nicht. Ist das Fenster am Fernseher noch offen?",
        noToken: "Scanne den QR-Code, den XPS5 am Fernseher zeigt." },
  it: { connecting: "Connessione alla PS5…", on: "Connesso alla PS5", off: "Non connesso",
        post: "Scrivi il tuo post", reply: "Scrivi la tua risposta", dm: "Scrivi il tuo messaggio", search: "Cerca su X",
        field: "Quello che scrivi", send: "Invia alla PS5", sent: "Inviato. Puoi chiudere questa pagina.",
        hint: "Quello che scrivi appare sulla TV mentre scrivi.",
        badToken: "Questo codice non vale più. Scansiona di nuovo il QR della TV.",
        lost: "La PS5 non risponde. La finestra è ancora aperta sulla TV?",
        noToken: "Scansiona il QR che XPS5 mostra sulla TV." },
  nl: { connecting: "Verbinden met de PS5…", on: "Verbonden met de PS5", off: "Niet verbonden",
        post: "Schrijf je post", reply: "Schrijf je antwoord", dm: "Schrijf je bericht", search: "Zoeken op X",
        field: "Wat je schrijft", send: "Naar de PS5 sturen", sent: "Verstuurd. Je kunt deze pagina sluiten.",
        hint: "Wat je typt, verschijnt op de tv terwijl je typt.",
        badToken: "Deze code geldt niet meer. Scan de QR op de tv opnieuw.",
        lost: "De PS5 antwoordt niet. Is het venster op de tv nog open?",
        noToken: "Scan de QR die XPS5 op de tv toont." },
};

const MAX_BYTES = 3800; // what the PS5 takes in one request, with room to spare

const $ = (id) => document.getElementById(id);
const token = (location.hash || "").slice(1).toLowerCase();
const encoder = new TextEncoder();
let strings = STRINGS.en, max = 280, what = "post", timer = 0, sending = false;

function t(key) {
  return strings[key] || STRINGS.en[key] || key;
}

function speak(lang) {
  strings = STRINGS[lang] || STRINGS.en;
  document.documentElement.lang = STRINGS[lang] ? lang : "en";
  for (const el of document.querySelectorAll("[data-t]")) {
    el.textContent = t(el.dataset.t);
  }
  $("title").textContent = t(what);
}

function status(key, on) {
  const el = $("status");
  el.textContent = t(key);
  el.classList.toggle("on", !!on);
}

function error(key) {
  const el = $("error");
  el.textContent = key ? t(key) : "";
  el.hidden = !key;
}

function count() {
  const n = Array.from($("text").value).length, el = $("count");
  el.textContent = `${n} / ${max}`;
  el.classList.toggle("over", n > max);
}

// the text cut to what the PS5 takes, on a whole character
function fitted(text) {
  if (encoder.encode(text).length <= MAX_BYTES) {
    return text;
  }
  let out = "";
  for (const ch of text) {
    if (encoder.encode(out + ch).length > MAX_BYTES) {
      break;
    }
    out += ch;
  }
  return out;
}

async function post(path, body) {
  const res = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(Object.assign({ token }, body)),
    cache: "no-store",
  });
  if (res.status === 403) {
    throw new Error("badToken");
  }
  if (!res.ok) {
    throw new Error("lost");
  }
  return res.json();
}

async function push(done) {
  const area = $("text"), text = fitted(area.value);
  if (text !== area.value) {
    area.value = text;
  }
  count();
  try {
    await post("/text", { text, done: !!done });
    status("on", true);
    error(null);
    return true;
  } catch (e) {
    status("off", false);
    error(e.message === "badToken" ? "badToken" : "lost");
    return false;
  }
}

function changed() {
  count();
  $("send").disabled = false;
  clearTimeout(timer);
  timer = setTimeout(() => push(false), 250);
}

async function send() {
  if (sending) {
    return;
  }
  sending = true;
  clearTimeout(timer);
  $("send").disabled = true;
  if (await push(true)) {
    status("sent", true);
    $("text").blur();
  } else {
    $("send").disabled = false;
  }
  sending = false;
}

async function start() {
  speak((navigator.language || "en").slice(0, 2));
  if (!/^[0-9a-f]{16}$/.test(token)) {
    status("off", false);
    error("noToken");
    return;
  }
  try {
    const draft = await post("/draft", {});
    max = Number(draft.max) > 0 ? Number(draft.max) : 280;
    what = STRINGS.en[draft.what] ? draft.what : "post";
    speak(draft.lang);
    const area = $("text");
    area.value = typeof draft.text === "string" ? draft.text : "";
    area.disabled = false;
    area.enterKeyHint = what === "post" || what === "reply" ? "enter" : "send";
    $("send").disabled = false;
    status("on", true);
    count();
    area.focus();
    area.setSelectionRange(area.value.length, area.value.length);
  } catch (e) {
    status("off", false);
    error(e.message === "badToken" ? "badToken" : "lost");
  }
}

$("text").addEventListener("input", changed);
$("text").addEventListener("keydown", (e) => {
  // Enter sends a message or a search; a post keeps its lines (Ctrl or ⌘ + Enter sends it)
  if (e.key === "Enter" && (what === "dm" || what === "search" || e.ctrlKey || e.metaKey)) {
    e.preventDefault();
    send();
  }
});
$("send").addEventListener("click", send);
start();
