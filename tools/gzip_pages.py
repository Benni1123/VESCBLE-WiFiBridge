# ─────────────────────────────────────────────────────────────────────────────
# Webseiten vor dem Uebersetzen packen
#
# Warum: HTML und JavaScript liegen als Klartext im Flash und machen den
# groessten Einzelposten der Firmware aus. Gepackt bleibt rund ein Drittel
# uebrig. Der ESP entpackt dabei NICHTS — er liefert die Bytes unveraendert mit
# dem Kopf "Content-Encoding: gzip" aus, der Browser macht den Rest. Dadurch
# ist es nicht nur kleiner, sondern auch schneller als vorher.
#
# Was passiert:
#
#     web/heat.html   ->   src/heat_page_gz.h
#     web/style.css   ->   src/style_page_gz.h
#
# Die Quelldatei bleibt normales, lesbares HTML. Erzeugt wird daraus ein
# Byte-Array in PROGMEM plus seine Laenge.
#
# Einbinden in platformio.ini (VOR dem Uebersetzen, deshalb "pre:"):
#
#     extra_scripts =
#         pre:tools/gzip_pages.py
#         post:generate_version.py
#         post:merge_firmware.py
#         post:archive_elf.py
#
# ─────────────────────────────────────────────────────────────────────────────

# pyright: reportUndefinedVariable=false

import gzip
import os
import re

Import("env")  # noqa: F821  (von PlatformIO bereitgestellt)

PROJECT = env.subst("$PROJECT_DIR")  # noqa: F821
WEB_DIR = os.path.join(PROJECT, "web")
SRC_DIR = os.path.join(PROJECT, "src")


def c_name(stem):
    """heat -> HEAT_PAGE_GZ"""
    return re.sub(r"[^A-Za-z0-9]", "_", stem).upper() + "_PAGE_GZ"


def build_one(path):
    stem = os.path.splitext(os.path.basename(path))[0]
    with open(path, "rb") as fh:
        raw = fh.read()

    # mtime=0: ohne das steckt der Zeitstempel im gzip-Kopf, und die Datei
    # aendert sich bei jedem Build — das wuerde jedes Mal eine neue
    # Firmware-Kennung erzeugen, obwohl sich nichts geaendert hat.
    packed = gzip.compress(raw, compresslevel=9, mtime=0)

    # Gegenprobe: wieder auspacken und vergleichen. Lieber hier ein harter
    # Abbruch als eine Firmware, die eine halbe Seite ausliefert.
    if gzip.decompress(packed) != raw:
        raise RuntimeError("gzip-Gegenprobe fehlgeschlagen fuer %s" % path)

    name = c_name(stem)
    out  = os.path.join(SRC_DIR, "%s_page_gz.h" % stem)

    lines = ["// Erzeugt von tools/gzip_pages.py aus web/%s - NICHT von Hand aendern."
             % os.path.basename(path),
             "// Quelle: %d Byte, gepackt: %d Byte (%.0f%% kleiner)."
             % (len(raw), len(packed), 100.0 - 100.0 * len(packed) / len(raw)),
             "#pragma once",
             "#include <pgmspace.h>",
             "",
             "static const unsigned char %s[] PROGMEM = {" % name]

    for i in range(0, len(packed), 16):
        chunk = packed[i:i + 16]
        lines.append("  " + ",".join("0x%02x" % b for b in chunk) + ",")

    lines.append("};")
    lines.append("static const unsigned int %s_LEN = %d;" % (name, len(packed)))
    lines.append("")

    text = "\n".join(lines)

    # Nur schreiben, wenn sich etwas geaendert hat: sonst stuft PlatformIO die
    # abhaengigen Dateien bei jedem Build als veraltet ein und uebersetzt alles
    # neu.
    if os.path.isfile(out):
        with open(out, "r", encoding="utf-8") as fh:
            if fh.read() == text:
                print("Seiten: %s unveraendert" % os.path.basename(out))
                return
    with open(out, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("Seiten: %s -> %s (%d -> %d Byte, %.0f%% kleiner)"
          % (os.path.basename(path), os.path.basename(out),
             len(raw), len(packed), 100.0 - 100.0 * len(packed) / len(raw)))


if not os.path.isdir(WEB_DIR):
    print("Seiten: kein web/-Verzeichnis - nichts zu packen")
else:
    for f in sorted(os.listdir(WEB_DIR)):
        if f.endswith(".html") or f.endswith(".css"):
            build_one(os.path.join(WEB_DIR, f))