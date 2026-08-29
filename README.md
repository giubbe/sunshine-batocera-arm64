# Sunshine ARM64 per Batocera 43 / Raspberry Pi 5

Questo repository e' un harness di build: non modifica Batocera e compila i
sorgenti ufficiali Sunshine in un checkout separato, fissato a:

- tag: `v2026.516.143833`
- commit: `14ffa6fdaa53f7b51512be2b3d24f3939695403c`
- destinazione: `/userdata/system/add-ons/sunshine`

Il workflow usa un runner GitHub Actions ARM64 nativo `ubuntu-24.04-arm`.
Non usa Flatpak, AppImage, FUSE, container runtime o binari Sunshine di versioni
precedenti.

## Build e artifact

Avviare manualmente il workflow **Build Sunshine for Batocera ARM64** oppure
eseguirlo con un push sul branch `main`. L'artifact GitHub Actions contiene:

- `sunshine-batocera-arm64-v2026.516.143833.tar.gz`;
- checksum SHA256 del tarball;
- log completo della build;
- rapporto CI e inventario ELF.

Il tarball contiene una singola directory `sunshine-batocera-arm64/`. Sul target
va estratta in `/userdata/system/add-ons/sunshine`.

Avvio manuale:

```sh
/userdata/system/add-ons/sunshine/bin/sunshine-start
```

Il wrapper imposta `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, `LD_LIBRARY_PATH`,
`SUNSHINE_ASSETS`, `SUNSHINE_WEBROOT` e `SUNSHINE_APP_ICONS`.

## Servizio Batocera

Il pacchetto include un user service Batocera e il relativo installer. Dopo
l'estrazione dell'add-on, installarlo con:

```sh
/userdata/system/add-ons/sunshine/bin/install-batocera-service
```

L'installer:

- copia il servizio in `/userdata/system/services/sunshine`;
- conserva un eventuale servizio precedente sotto
  `/userdata/system/service-backups/`;
- crea, se assente, `/userdata/system/configs/sunshine-service.conf` come file
  opzionale di override;
- abilita `sunshine` tramite `batocera-services enable sunshine`.

L'abilitazione e' persistente al boot. Per avviare immediatamente il servizio
senza riavviare:

```sh
batocera-services start sunshine
```

Il servizio usa per default:

```text
/userdata/system/.config/sunshine/sunshine.conf
```

Per usare un file diverso, impostare ad esempio in
`/userdata/system/configs/sunshine-service.conf`:

```sh
SUNSHINE_CONFIG=/userdata/system/sunshine-prod/sunshine.conf
```

Il servizio attende fino a 60 secondi che il socket Wayland `wayland-0` sia
disponibile e che `ldd` non riporti dipendenze dinamiche mancanti. Il gate evita
la race di boot osservata su Batocera 43, dove `S99userservices` avvia i servizi
utente in parallelo e le librerie rese disponibili da altri servizi possono non
essere ancora pronte. Il timeout e' modificabile tramite
`SUNSHINE_RUNTIME_TIMEOUT` nel file di override.

Il servizio supporta `start`, `stop`, `restart` e `status`; lo stop invia
`SIGTERM` e non forza `SIGKILL` se Sunshine non termina entro 15 secondi.

## Dipendenze locali

La CI ammette nel bundle soltanto le librerie ICU 74 effettivamente richieste
dall'ELF Ubuntu 24.04 (`libicuuc`, `libicudata` e, solo se necessaria,
`libicui18n`). Le altre dipendenze restano intenzionalmente dinamiche e devono
essere soddisfatte dal target verificato. Non vengono creati symlink ABI.

Sul target validato, `libva.so.2` e altre librerie di sistema possono essere
rese disponibili dall'ambiente Batocera/add-on locale. Il servizio non copia ne'
inventa librerie: aspetta che il runtime reale sia risolvibile e fallisce in
modo diagnostico se non lo diventa entro il timeout.

## Limite della CI

`ldd` sul runner Ubuntu descrive il runner Ubuntu, non Batocera. Gli audit CI
verificano architettura, `DT_NEEDED`, blacklist, bundle, presenza e sintassi del
servizio Batocera; la compatibilita' finale deve essere provata separatamente
sul target reale.

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
  per circa tre ore senza crash osservati;
- il servizio Batocera e' stato provato manualmente con avvio, streaming e stop;
- dopo l'abilitazione persistente del servizio, Sunshine e' risultato in
  esecuzione dopo reboot con il binario e la configurazione attesi.

La correlazione dell'endurance test con questo specifico binario e' supportata
dal timestamp di installazione precedente al test e dallo SHA256 tuttora
identico all'artefatto CI. Il log di processo originale della sessione non e'
stato conservato, quindi questo punto e' documentato come ricostruzione
storica forte, non come tracciamento forense completo del processo.

La validazione del servizio riguarda il comportamento dello script testato sul
target. La copia distribuita nel tarball viene inoltre controllata staticamente
dalla CI; una nuova build del tarball deve comunque essere verificata prima di
estendere la validazione runtime a quell'artefatto specifico.

### Warning noto sul target

Sul sistema validato il loader emette:

```text
/usr/lib/libcurl.so.4: no version information available
```

Il warning non ha impedito l'avvio di Sunshine ne' lo streaming nel test sopra
descritto. Resta comunque una differenza ABI/symbol-versioning del target da
considerare se il pacchetto viene usato su revisioni Batocera differenti.

## Rollback

La build non installa nulla sul sistema host. Sul target, il rollback dell'add-on
consiste nel rinominare o rimuovere esclusivamente la directory add-on dopo
averne conservato una copia.

Il servizio puo' essere disabilitato con:

```sh
batocera-services disable sunshine
batocera-services stop sunshine
```

L'installer conserva un servizio preesistente in
`/userdata/system/service-backups/`; non crea backup dentro la directory dei
servizi, evitando che Batocera li interpreti come script di servizio.
