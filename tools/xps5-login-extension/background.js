'use strict';

// The extension's button opens its page in a tab of its own: a popup would
// close while the browser asks for the PS5's address. Pressed on the page the
// PS5 serves while its linking screen is open (the one the screen's QR opens,
// http://<PS5>:9070/#<code>), it takes the address and the code from that tab
// (activeTab lets it read the tab's address then, and only then), so nothing
// needs typing. Chrome and Edge run this as a service worker, Firefox as a
// background script.

const api = globalThis.browser ?? globalThis.chrome;

// the PS5's page: its address, a port, and the code after '#' or in ?c=
const PS5_PAGE = /^http:\/\/((?:\d{1,3}\.){3}\d{1,3})(?::(\d{1,5}))?\/(?:index\.html)?(?:\?c=(\d{6}))?(?:#(\d{6}))?$/;

/** what connect.html gets after '#' from the tab the button was pressed on */
function fromTab(url) {
  const m = PS5_PAGE.exec(url || '');
  if (!m) {
    return '';
  }
  const params = new URLSearchParams({ address: m[2] ? `${m[1]}:${m[2]}` : m[1] });
  const code = m[4] || m[3];
  if (code) {
    params.set('code', code);
  }
  return `#${params}`;
}

api.action.onClicked.addListener((tab) => {
  api.tabs.create({ url: api.runtime.getURL('connect.html') + fromTab(tab && tab.url) });
});
