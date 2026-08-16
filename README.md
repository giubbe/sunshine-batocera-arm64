# Sunshine ARM64 per Batocera 43 / Raspberry Pi 5

Questo repository e' un harness di build: non modifica Batocera e compila i
sorgenti ufficiali Sunshine in un checkout separato, fissato a:

- tag: `v2026.516.143833`
- commit: `14ffa6fdaa53f7b51512be2b3d24f3939695403c`
- destinazione: `/userdata/system/add-ons/sunshine`

Il workflow usa un runner GitHub Actions ARM64 nativo `ubuntu-24.04-arm`.
Non usa Flatpak, AppImage, FUSE, container runtime o binari Sunshine di versioni
precedenti.

## Esecuzione

Avviare manualmente il workflow **Build Sunshine for Batocera ARM64** oppure
eseguirlo con un push sul branch `main`. L'artifact GitHub Actions contiene:

- `sunshine-batocera-arm64-v2026.516.143833.tar.gz`;
- checksum SHA256 del tarball;
- log completo della build;
- rapporto CI e inventario ELF.

Il tarball contiene una singola directory `sunshine-batocera-arm64/`. Per il
test sul target deve essere estratta in
`/userdata/system/add-ons/sunshine`; il comando di avvio e':

```sh
/userdata/system/add-ons/sunshine/bin/sunshine-start
```

Il wrapper imposta `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, `LD_LIBRARY_PATH`,
`SUNSHINE_ASSETS`, `SUNSHINE_WEBROOT` e `SUNSHINE_APP_ICONS`. Non viene creato
alcun servizio Batocera.

## Dipendenze locali

La CI ammette nel bundle soltanto le librerie ICU 74 effettivamente richieste
dall'ELF Ubuntu 24.04 (`libicuuc`, `libicudata` e, solo se necessaria,
`libicui18n`). Le altre dipendenze restano intenzionalmente dinamiche e devono
essere soddisfatte dal target verificato. Non vengono creati symlink ABI.

## Limite della CI

`ldd` sul runner Ubuntu descrive il runner Ubuntu, non Batocera. Gli audit CI
verificano architettura, `DT_NEEDED`, blacklist e bundle, ma la compatibilita'
finale deve essere provata eseguendo l'artefatto sul Raspberry Pi 5 con
Batocera 43apu.1. Fino a tale prova non va dichiarato il successo runtime.

## Rollback

La build non installa nulla sul sistema host. Sul target, il rollback consiste
nel rinominare o rimuovere esclusivamente la directory add-on dopo averne
conservato una copia; questo repository non automatizza operazioni distruttive.
