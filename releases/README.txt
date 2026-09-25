Firmware-Symbole zum Auswerten von Absturzabbildern
==================================================

Jede Datei gehoert zu genau einem Build:

    firmware-<version>-<kennung>.elf.gz

Die <kennung> meldet das Geraet selbst im Log:

    [COREDUMP] Firmware-Kennung: Abbild=02b56d25 laufend=...

Passende Datei heraussuchen, entpacken und auswerten:

    gzip -d firmware-1.3.0-02b56d25.elf.gz
    python -m esp_coredump info_corefile \
        -c coredump.bin -t raw firmware-1.3.0-02b56d25.elf

Das coredump.bin holt man vom Geraet:

    http://<geraete-ip>/api/coredump

Stimmen Kennung und Datei nicht ueberein, zeigen die
Zeilennummern auf falsche Stellen - dann ist es der
falsche Stand.
