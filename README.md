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
verificano architettura, `DT_NEEDED`, blacklist e bundle; la compatibilita'
finale deve essere provata separatamente sul target reale.

## Validazione runtime

La build e' stata validata su **Raspberry Pi 5** con **Batocera 43apu.1**.
L'artefatto installato sul target corrisponde alla build GitHub Actions di
Sunshine `v2026.516.143833` / commit
`14ffa6fdaa53f7b51512be2b3d24f3939695403c`.

Verifiche eseguite sul target:

- `sunshine --version` riporta versione e commit attesi;
- SHA256 del binario installato:
  `b7cd913b356c0a34b3c440d53a8cc922c7d32b4b4583e3e3fbb79379fbe35335`;
- lo SHA256 coincide byte-per-byte con il binario estratto dall'artefatto CI;
- il file installato risulta creato sul target il 17 agosto 2026 alle
  19:07:17 +0200;
- una successiva sessione di streaming della stessa serata e' rimasta attiva
  per circa tre ore senza crash osservati.

La correlazione dell'endurance test con questo specifico binario e' supportata
dal timestamp di installazione precedente al test e dallo SHA256 tuttora
identico all'artefatto CI. Il log di processo originale della sessione non e'
stato conservato, quindi questo punto e' documentato come ricostruzione
storica forte, non come tracciamento forense completo del processo.

### Warning noto sul target

Sul sistema validato il loader emette:

```text
/usr/lib/libcurl.so.4: no version information available
```

Il warning non ha impedito l'avvio di Sunshine ne' lo streaming nel test sopra
descritto. Resta comunque una differenza ABI/symbol-versioning del target da
considerare se il pacchetto viene usato su revisioni Batocera differenti.

## Rollback

La build non installa nulla sul sistema host. Sul target, il rollback consiste
nel rinominare o rimuovere esclusivamente la directory add-on dopo averne
conservato una copia; questo repository non automatizza operazioni distruttive.
