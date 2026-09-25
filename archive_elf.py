# ─────────────────────────────────────────────────────────────────────────────
# Firmware-Symbole zu jedem Build archivieren
#
# Warum das noetig ist:
#
# Ein Absturzabbild (Core Dump) enthaelt nur Adressen. Erst die firmware.elf
# des betroffenen Builds macht daraus Dateinamen und Zeilennummern. Genau diese
# Datei liegt aber unter .pio/build/esp32-s3/firmware.elf und wird bei JEDEM
# Build ueberschrieben — auch bei jedem Testlauf zwischendurch.
#
# Stuerzt ein Geraet drei Wochen spaeter ab, ist die passende ELF laengst weg,
# und der Dump taugt nur noch fuer den Tasknamen. Deshalb legt dieses Skript
# nach jedem Build eine Kopie ab, benannt nach Version und Firmware-Kennung:
#
#     releases/firmware-1.3.0-3032623536643235.elf.gz
#
# Die Kennung ist derselbe Wert, den das Geraet im Log meldet
# ("[COREDUMP] Firmware-Kennung: Abbild=02b56d25"). Damit findet man die
# richtige Datei, ohne raten zu muessen.
#
# Einbinden in platformio.ini:
#
#     extra_scripts = post:generate_version.py, post:merge_firmware.py, post:archive_elf.py
# ─────────────────────────────────────────────────────────────────────────────

#
# Hinweis zu den Namen "Import" und "env":
# Beide spritzt PlatformIO beim Ausfuehren des Skripts ein, sie stehen nirgends
# im Quelltext. Eine statische Pruefung im Editor (Pylance/Pyright) kann das
# nicht wissen und meldet sie als unbekannt. Die Zeile darunter schaltet genau
# diese eine Pruefung fuer diese Datei ab — alle anderen bleiben aktiv.
#
# pyright: reportUndefinedVariable=false

import gzip
import os
import re
import shutil
import struct
import time

Import("env")  # noqa: F821  (von PlatformIO bereitgestellt)

# Wie viele Staende aufbewahrt werden. Eine ELF mit Debug-Symbolen ist je nach
# Projektgroesse etliche Megabyte gross, gepackt noch rund ein Drittel davon.
# 15 deckt mehrere Wochen Entwicklung ab, ohne die Platte vollaufen zu lassen.
MAX_KEEP = 15

RELEASE_DIR = "releases"

# Die Beschreibungsstruktur esp_app_desc_t wird im Abbild GESUCHT, nicht an
# einer angenommenen Stelle erwartet.
#
# Der Grund: die Struktur liegt zwar ueblicherweise bei Offset 32 (24 Byte
# Abbildkopf + 8 Byte Abschnittskopf), aber das haengt am Aufbau des Abbilds
# und daran, was weitere Nachlauf-Skripte damit anstellen. Wird dort geraten
# und die Annahme stimmt nicht, kommt keine brauchbare Kennung heraus — genau
# das ist passiert. Ihr Magic-Wort macht die Struktur dagegen eindeutig
# auffindbar.
APP_DESC_MAGIC = 0xABCD5432

# Aufbau innerhalb der Struktur:
#   magic(4) secure(4) reserv(8) version(32) project(32) time(16) date(16)
#   idf_ver(32) app_elf_sha256(32)
DESC_VERSION_OFF = 16
DESC_VERSION_LEN = 32
DESC_SHA_OFF     = 144
DESC_SHA_LEN     = 32


def read_app_desc(bin_path):
    """Version UND Kennung aus dem gebauten Abbild lesen.

    Beide Angaben kommen aus derselben Quelle: dem Abbild, das gerade gebaut
    und geflasht wurde. Die Version aus version.h zu lesen ging schief — ein
    anderes Nachlauf-Skript zaehlt sie im selben Durchgang hoch, sodass im
    Dateinamen der naechste statt des gebauten Stands landete.

    Liefert (version, kennung) oder (None, None).
    """
    try:
        with open(bin_path, "rb") as fh:
            head = fh.read(64 * 1024)
    except OSError:
        return None, None

    pos = head.find(struct.pack("<I", APP_DESC_MAGIC))
    if pos < 0:
        return None, None

    desc = head[pos:pos + 256]
    if len(desc) < DESC_SHA_OFF + DESC_SHA_LEN:
        return None, None

    raw_ver = desc[DESC_VERSION_OFF:DESC_VERSION_OFF + DESC_VERSION_LEN]
    version = raw_ver.split(b"\0")[0].decode("utf-8", errors="replace").strip()

    raw_sha = desc[DESC_SHA_OFF:DESC_SHA_OFF + DESC_SHA_LEN]
    # Als rohe Bytes behandeln und die ersten acht in Hex wandeln — dieselbe
    # Darstellung, die die Firmware im Log ausgibt ("Abbild=...").
    sha = "".join("%02x" % b for b in raw_sha[:8])

    return (version or None), sha


def read_version_header(project_dir):
    """Ersatzweise die Version aus version.h — nur wenn das Abbild schweigt."""
    path = os.path.join(project_dir, "src", "version.h")
    if not os.path.isfile(path):
        path = os.path.join(project_dir, "version.h")
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', fh.read())
            if m:
                return m.group(1)
    except OSError:
        pass
    return "unknown"


def prune(directory, keep):
    """Aelteste Staende entfernen, sobald zu viele da sind."""
    try:
        files = [
            os.path.join(directory, f)
            for f in os.listdir(directory)
            if f.startswith("firmware-") and f.endswith(".elf.gz")
        ]
    except OSError:
        return
    if len(files) <= keep:
        return
    files.sort(key=lambda p: os.path.getmtime(p))
    for path in files[: len(files) - keep]:
        try:
            os.remove(path)
            print("Symbole: alten Stand entfernt -> %s" % os.path.basename(path))
        except OSError:
            pass


def after_build(source, target, env):
    build_dir   = env.subst("$BUILD_DIR")
    project_dir = env.subst("$PROJECT_DIR")
    prog        = env.subst("$PROGNAME")

    elf_path = os.path.join(build_dir, prog + ".elf")
    bin_path = os.path.join(build_dir, prog + ".bin")
    if not os.path.isfile(elf_path):
        print("Symbole: firmware.elf nicht gefunden - uebersprungen")
        return

    version, sha = read_app_desc(bin_path)

    if not version:
        version = read_version_header(project_dir)
        print("Symbole: Version nicht im Abbild gefunden, nehme version.h (%s)" % version)

    if not sha:
        # Ohne Kennung waere die Datei spaeter nicht zuzuordnen. Dann lieber
        # den Zeitstempel nehmen als gar nichts abzulegen — und sagen, wo
        # gesucht wurde, damit sich der Grund finden laesst.
        sha = time.strftime("%Y%m%d-%H%M%S")
        print("Symbole: Kennung nicht im Abbild gefunden (%s) - benenne nach Zeitstempel"
              % bin_path)

    out_dir = os.path.join(project_dir, RELEASE_DIR)
    os.makedirs(out_dir, exist_ok=True)

    name    = "firmware-%s-%s.elf.gz" % (version, sha)
    out_path = os.path.join(out_dir, name)

    if os.path.isfile(out_path):
        print("Symbole: Stand bereits vorhanden -> %s" % name)
        return

    # Gepackt abgelegt: eine ELF mit Debug-Symbolen ist mehrere Megabyte gross
    # und laesst sich gut auf etwa ein Drittel verkleinern. esp_coredump
    # braucht sie entpackt — siehe Hinweis in releases/README.txt.
    try:
        with open(elf_path, "rb") as src, gzip.open(out_path, "wb", compresslevel=6) as dst:
            shutil.copyfileobj(src, dst, 1024 * 1024)
    except OSError as exc:
        print("Symbole: Ablegen fehlgeschlagen (%s)" % exc)
        return

    size_mb = os.path.getsize(out_path) / (1024.0 * 1024.0)
    print("Symbole: %s (%.1f MB) abgelegt" % (name, size_mb))

    # ── Uebersicht fortschreiben ─────────────────────────────────────────────
    #
    # Im Dateinamen steht die Kennung, weil nur sie eindeutig zum Abbild
    # gehoert — sie ist der Wert, den das Geraet im Log meldet. Zum Nachsehen,
    # WELCHER Stand das war, taugt sie aber wenig. Deshalb hier eine Zeile je
    # Build mit allem, was zur Einordnung hilft.
    #
    # Die Spalte version.h ist mit Vorsicht zu lesen: laeuft ein Skript, das
    # die Nummer hochzaehlt, im selben Durchgang, steht dort schon der
    # naechste Stand. Verlaesslich sind Kennung und Zeitpunkt.
    index_path = os.path.join(out_dir, "index.txt")
    try:
        new_file = not os.path.isfile(index_path)
        with open(index_path, "a", encoding="utf-8") as fh:
            if new_file:
                fh.write("# Zeitpunkt         version.h  Abbild-Version  Kennung"
                         "           Datei\n")
            fh.write("%-19s %-10s %-15s %-18s %s\n" % (
                time.strftime("%Y-%m-%d %H:%M"),
                read_version_header(project_dir),
                version,
                sha,
                name,
            ))
    except OSError:
        pass

    # Kurze Anleitung daneben, damit spaeter niemand suchen muss, wie das
    # Auswerten ging.
    readme = os.path.join(out_dir, "README.txt")
    if not os.path.isfile(readme):
        try:
            with open(readme, "w", encoding="utf-8") as fh:
                fh.write(
                    "Firmware-Symbole zum Auswerten von Absturzabbildern\n"
                    "==================================================\n\n"
                    "Jede Datei gehoert zu genau einem Build:\n\n"
                    "    firmware-<version>-<kennung>.elf.gz\n\n"
                    "Welcher Stand welche Datei ist, steht in index.txt.\n\n"
                    "Die <kennung> meldet das Geraet selbst im Log:\n\n"
                    "    [COREDUMP] Firmware-Kennung: Abbild=3032623536643235 laufend=...\n\n"
                    "Passende Datei heraussuchen, entpacken und auswerten:\n\n"
                    "    gzip -d firmware-1.3.0-3032623536643235.elf.gz\n"
                    "    python -m esp_coredump info_corefile \\\n"
                    "        -c coredump.bin -t raw firmware-1.3.0-3032623536643235.elf\n\n"
                    "Das coredump.bin holt man vom Geraet:\n\n"
                    "    http://<geraete-ip>/api/coredump\n\n"
                    "Stimmen Kennung und Datei nicht ueberein, zeigen die\n"
                    "Zeilennummern auf falsche Stellen - dann ist es der\n"
                    "falsche Stand.\n"
                )
        except OSError:
            pass

    prune(out_dir, MAX_KEEP)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)  # noqa: F821