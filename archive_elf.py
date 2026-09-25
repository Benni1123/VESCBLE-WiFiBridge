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
#     releases/firmware-1.3.0-02b56d25.elf.gz
#
# Die Kennung ist derselbe Wert, den das Geraet im Log meldet
# ("[COREDUMP] Firmware-Kennung: Abbild=02b56d25"). Damit findet man die
# richtige Datei, ohne raten zu muessen.
#
# Einbinden in platformio.ini:
#
#     extra_scripts = post:generate_version.py, post:merge_firmware.py, post:archive_elf.py
# ─────────────────────────────────────────────────────────────────────────────

import gzip
import os
import re
import shutil
import time

Import("env")  # noqa: F821  (von PlatformIO bereitgestellt)

# Wie viele Staende aufbewahrt werden. Eine ELF mit Debug-Symbolen ist je nach
# Projektgroesse etliche Megabyte gross, gepackt noch rund ein Drittel davon.
# 15 deckt mehrere Wochen Entwicklung ab, ohne die Platte vollaufen zu lassen.
MAX_KEEP = 15

RELEASE_DIR = "releases"

# Lage der Beschreibungsstruktur im fertigen Abbild:
# Der Abbildkopf ist 24 Byte, danach folgt der Kopf des ersten Abschnitts mit
# 8 Byte — die esp_app_desc_t beginnt also bei 32. Innerhalb dieser Struktur
# liegt app_elf_sha256 nach magic(4) + secure(4) + reserv(8) + version(32) +
# project(32) + time(16) + date(16) + idf_ver(32) = 144 Byte.
APP_DESC_OFFSET = 32
SHA_OFFSET_IN_DESC = 144
SHA_LEN = 32


def read_version(project_dir):
    """Firmware-Version aus version.h lesen."""
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


def read_elf_sha(bin_path):
    """Die Firmware-Kennung aus dem gebauten Abbild lesen.

    Bewusst aus der .bin und nicht per sha256sum ueber die .elf: Das Geraet
    meldet genau diesen Wert, und er wird vom Build-System gesetzt. Selbst zu
    rechnen hiesse, eine Annahme darueber zu treffen, wie er zustande kommt —
    und bei einer Abweichung suchte man spaeter die falsche Datei heraus.
    """
    try:
        with open(bin_path, "rb") as fh:
            fh.seek(APP_DESC_OFFSET + SHA_OFFSET_IN_DESC)
            raw = fh.read(SHA_LEN)
    except OSError:
        return ""

    out = []
    for b in raw:
        if b == 0 or b < 32 or b > 126:
            break
        out.append(chr(b))
    return "".join(out)


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

    version = read_version(project_dir)
    sha     = read_elf_sha(bin_path) if os.path.isfile(bin_path) else ""
    if not sha:
        # Ohne Kennung waere die Datei spaeter nicht zuzuordnen. Dann lieber
        # den Zeitstempel nehmen als gar nichts abzulegen.
        sha = time.strftime("%Y%m%d-%H%M%S")
        print("Symbole: Kennung nicht lesbar, benenne nach Zeitstempel")

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
                    "Die <kennung> meldet das Geraet selbst im Log:\n\n"
                    "    [COREDUMP] Firmware-Kennung: Abbild=02b56d25 laufend=...\n\n"
                    "Passende Datei heraussuchen, entpacken und auswerten:\n\n"
                    "    gzip -d firmware-1.3.0-02b56d25.elf.gz\n"
                    "    python -m esp_coredump info_corefile \\\n"
                    "        -c coredump.bin -t raw firmware-1.3.0-02b56d25.elf\n\n"
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