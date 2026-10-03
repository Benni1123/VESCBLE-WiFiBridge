// ─────────────────────────────────────────────────────────────────────────────
// Gemeinsames JavaScript aller Seiten
//
// Sprache, Thema, Hinweistexte, die Sichtbarkeit der optionalen Reiter und das
// Easteregg sind auf der Startseite, auf /leds und auf /heat Zeichen fuer
// Zeichen dasselbe. Vorher lag das dreimal im Flash. Jetzt einmal, als eigene
// Datei — der Browser holt sie zu jeder Seite, der ESP haelt sie nur einmal.
//
// Nur was wirklich auf allen drei Seiten gleich ist, steht hier. Alles
// Seitenspezifische bleibt im jeweiligen <script>-Block, damit man beim Lesen
// einer Seite nicht in dieser Datei nachsehen muss.
//
// Eingebunden wird sie blockierend im <head>. Hier stehen nur Definitionen und
// zwei Cookie-Abfragen — kein Zugriff aufs DOM, das zu diesem Zeitpunkt noch
// nicht existiert. Die Aufrufe, die das DOM brauchen (applyTheme, applyHints),
// macht jede Seite selbst am Ende ihres eigenen Blocks.
//
// Quelle: web/app.js. Beim Bauen wird daraus app_page_gz.h.
// ─────────────────────────────────────────────────────────────────────────────

// ── Sprache ─────────────────────────────────────────────────────────────────
var lang=(document.cookie.match(/lang=([a-z]+)/)||[])[1]||(navigator.language.startsWith('de')?'de':'en');
function de(){return lang==='de';}
function L(d,e){return de()?d:e;}
function gid(id){return document.getElementById(id);}
function toggleLang(){lang=lang==='de'?'en':'de';document.cookie='lang='+lang+';path=/;max-age=31536000';location.reload();}

// ── Thema ───────────────────────────────────────────────────────────────────
var theme=(document.cookie.match(/theme=([a-z]+)/)||[])[1]||(window.matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');
function applyTheme(){document.documentElement.setAttribute('data-theme',theme);var b=gid('themeBtn');if(b)b.textContent=theme==='dark'?'\u2600\uFE0F':'\uD83C\uDF19';document.cookie='theme='+theme+';path=/;max-age=31536000';}
function toggleTheme(){theme=theme==='dark'?'light':'dark';applyTheme();}

// ── Hinweistexte ein-/ausblenden ────────────────────────────────────────────
// Der Zustand steht in einem Cookie und nicht in localStorage: so gilt er
// seitenuebergreifend (Startseite, /leds, /heat) mit demselben Mechanismus,
// den auch Thema und Sprache benutzen. Standard ist AUS — die Oberflaeche
// soll aufgeraeumt aussehen, bis man die Erklaerungen anfordert.
var hintsOn = (document.cookie.match(/hints=(\d)/)||[])[1] === '1';
function applyHints(){
  if (document.body) document.body.classList.toggle('show-info', hintsOn);
  var b = gid('btn-info');
  if (b){
    if (hintsOn) b.classList.add('on'); else b.classList.remove('on');
    b.title = hintsOn ? L('Hinweise ausblenden','Hide notes')
                      : L('Hinweise einblenden','Show notes');
  }
}
function toggleInfo(){
  hintsOn = !hintsOn;
  document.cookie = 'hints=' + (hintsOn?'1':'0') + ';path=/;max-age=31536000';
  applyHints();
}

// ── Sichtbarkeit der optionalen Reiter ──────────────────────────────────────
//
// Welche Reiter es gibt, steht im Attribut data-ui am <html>-Element. Gesetzt
// wird es von /ui-state.js — einem winzigen, vom ESP erzeugten Script, das
// noch vor dieser Datei laeuft. Weil beide den Parser blockieren, steht der
// Zustand fest, BEVOR die Reiterleiste geparst wird: die Leiste wird nie ohne
// ihre Reiter gezeichnet, es ruckt nichts, und der Zustand kommt vom Geraet
// statt aus einem Cookie — der im Captive-Portal-Browser beim ersten Aufruf
// gar nicht da waere.
//
// Die zugehoerigen Regeln ("ohne Token -> display:none") stehen in style.css.
// Hier wird nur noch der Token gesetzt oder entfernt, wenn sich im Betrieb
// etwas aendert.
function uiSet(k,on){
  var h=document.documentElement;
  var t=(h.getAttribute('data-ui')||'').split(/\s+/).filter(function(x){return x&&x!==k;});
  if(on) t.push(k);
  h.setAttribute('data-ui',t.join(' '));
}
function uiHas(k){
  return (' '+(document.documentElement.getAttribute('data-ui')||'')+' ').indexOf(' '+k+' ')>=0;
}
function modsApply(l,h){ uiSet('leds',l); uiSet('heat',h); }

// ── Easteregg ───────────────────────────────────────────────────────────────
//
// Nur fuer das Geraet mit dem BLE-Namen "BLE". Wirft in grossen
// Abstaenden einen Spruch an eine zufaellige Stelle und blendet ihn wieder aus.
//
// Bewusst ohne Interaktion: pointer-events:none, damit nie ein Knopf verdeckt
// wird, den man gerade druecken will. Und mit grossem Abstand nach oben, damit
// die Reiterleiste frei bleibt.
var EGG_LINES = [
  'BLE meldet sich zum Dienst.',
  'Akku voll, Kopf leer, Gas.',
  'Die Griffheizung sagt danke.',
  'Irgendwo zaehlt ein Zaehler hoch.',
  'Der Waechter waecht.',
  'ERPM ist nur eine Zahl.',
  'Dieses Geraet hat schon Schlimmeres ueberlebt.',
  'Blackbox leer. Gutes Zeichen.',
  'Kein Brownout seit Minuten.',
  'Die LEDs sehen dich.',
  'Ja, das Log laeuft noch.',
  'PWM macht genau das, was du gesagt hast.',
  'Wer braucht schon Bremsen.',
  'Noch ein Reiter und es passt nicht mehr drauf.',
  'Der Coredump wartet auf dich.',
  'Alles im gruenen Bereich. Vermutlich.'
];
var eggTimer = null;

// Der Zustand kommt vom Geraet (NVS), nicht aus dem Browser. eggOff ist nur
// die lokale Kopie, damit eggShow() nicht bei jedem Spruch nachfragen muss;
// aufgefrischt wird sie mit jedem /api/info.
var eggOff = false;
function eggIsOff(){ return eggOff; }
function eggSetOff(off){
  eggOff = !!off;
  fetch('/api/egg?en=' + (off?0:1), {method:'POST'}).catch(function(){});
  if (off && eggTimer){ clearTimeout(eggTimer); eggTimer = null; }
  // Laufende Spruchblasen gleich mit wegraeumen, sonst steht die letzte noch
  // sechs Sekunden da, nachdem man es ausgeschaltet hat.
  var old = document.querySelectorAll('.egg');
  for (var i = 0; i < old.length; i++) old[i].parentNode.removeChild(old[i]);
}

function eggShow(){
  if (eggIsOff()) return;
  var d = document.createElement('div');
  d.className = 'egg';
  d.textContent = EGG_LINES[Math.floor(Math.random()*EGG_LINES.length)];
  // SAUER! gehoert an jeden Spruch. Als eigenes Element und nicht per
  // innerHTML: so kann im Spruchtext nie etwas als Markup gedeutet werden,
  // egal was dort kuenftig steht.
  var sa = document.createElement('span');
  sa.textContent = ' SAUER!';
  sa.style.cssText = 'color:var(--accent);font-weight:800;letter-spacing:1px';
  d.appendChild(sa);
  document.body.appendChild(d);
  // Erst anhaengen, dann messen: vorher kennt der Browser die Groesse nicht,
  // und der Spruch landete halb ausserhalb des Fensters.
  var w = d.offsetWidth, h = d.offsetHeight;
  var maxX = Math.max(8, window.innerWidth  - w - 8);
  var maxY = Math.max(60, window.innerHeight - h - 8);
  d.style.left = (8 + Math.random()*(maxX-8)) + 'px';
  d.style.top  = (60 + Math.random()*(maxY-60)) + 'px';
  requestAnimationFrame(function(){ d.classList.add('on'); });
  setTimeout(function(){
    d.classList.remove('on');
    setTimeout(function(){ if(d.parentNode) d.parentNode.removeChild(d); }, 800);
  }, 6000);
}

function eggArm(name){
  if (name !== 'BLE' || eggTimer || eggIsOff()) return;
  // Zufaelliger Abstand zwischen 40 und 160 Sekunden. Ein fester Takt waere
  // schnell nur noch Moebel.
  (function next(){
    eggTimer = setTimeout(function(){ eggShow(); next(); },
                          40000 + Math.random()*120000);
  })();
  setTimeout(eggShow, 4000);   // einer gleich zur Begruessung
}
