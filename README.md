# Sunshine ARM64 per Batocera 43 / Raspberry Pi 5

> [!WARNING]
> **Progetto in corso di sviluppo.** Questo repository viene sviluppato e testato
> iterativamente con l'assistenza di **ChatGPT**. Non e' un fork di Sunshine:
> e' un harness riproducibile che compila una revisione upstream fissata e applica
> solo gli adattamenti documentati per il target Batocera ARM64.
>
> La configurazione descritta qui sotto e' stata provata su Raspberry Pi 5 con
> Batocera `43apu.1`. Revisioni diverse di Batocera o Sunshine richiedono una
> nuova validazione runtime.

Questo repository compila i sorgenti ufficiali Sunshine in un checkout separato,
fissato a:

- tag: `v2026.516.143833`
- commit: `14ffa6fdaa53f7b51512be2b3d24f3939695403c`
- destinazione: `/userdata/system/add-ons/sunshine`

Il workflow usa un runner GitHub Actions ARM64 nativo `ubuntu-24.04-arm`.
Non usa Flatpak, AppImage, FUSE, container runtime o binari Sunshine di versioni
precedenti.

## Perche' questa build invece della BUA

La variante ARM64 di Batocera Unofficial Add-ons (BUA) installa
`Sunshine-aarch64.AppImage` e crea una configurazione con:

```ini
encoder = software
sw_preset = ultrafast
```

Riferimento BUA:
https://github.com/batocera-unofficial-addons/batocera-unofficial-addons/blob/main/sunshine/sunshine-arm64.sh

Questa build adotta invece un approccio differente:

- **Supporto KMS/DRM:** la documentazione ufficiale di Sunshine dichiara che
  l'AppImage non supporta la cattura KMS. Questa build viene compilata con
  `SUNSHINE_ENABLE_DRM=ON`; Sunshine definisce questa opzione come supporto alla
  cattura KMS quando disponibile.

  Documentazione Sunshine:
  https://docs.lizardbyte.dev/projects/sunshine/master/md_docs_2getting__started.html

  Opzioni di build upstream:
  https://github.com/LizardByte/Sunshine/blob/master/cmake/prep/options.cmake

- **Sorgente fissato e build ARM64 nativa:** il workflow effettua il checkout
  del commit Sunshine `14ffa6fdaa53f7b51512be2b3d24f3939695403c`, verifica
  che il checkout corrisponda esattamente a quel commit e richiede un runner
  `aarch64`. La build viene eseguita su GitHub Actions con `ubuntu-24.04-arm`.

  Workflow:
  https://github.com/giubbe/sunshine-batocera-arm64/blob/main/.github/workflows/build.yml

- **Installazione senza AppImage:** l'artifact prodotto contiene direttamente
  il binario Sunshine e gli asset destinati a
  `/userdata/system/add-ons/sunshine`; l'avvio non richiede quindi l'AppImage
  utilizzata dall'installer BUA.

  Script di build:
  https://github.com/giubbe/sunshine-batocera-arm64/blob/main/ci/build-package.sh

- **Bundle ICU esplicito:** lo script di packaging individua tramite `DT_NEEDED`
  le librerie ICU 74 richieste dal binario e include nel bundle esclusivamente
  la relativa closure `libicuuc`, `libicudata` e `libicui18n` quando necessaria.
  Lo script non crea symlink per sostituire versioni ABI differenti.

  Script di build:
  https://github.com/giubbe/sunshine-batocera-arm64/blob/main/ci/build-package.sh

- **Controlli CI:** prima della creazione dell'artifact vengono verificati
  l'architettura AArch64, l'identita' del sorgente e l'applicazione della patch
  audio; lo script di packaging esegue inoltre l'audit previsto dal progetto
  prima di creare il tarball.

  Workflow:
  https://github.com/giubbe/sunshine-batocera-arm64/blob/main/.github/workflows/build.yml

- **Avvio differito sul target:** il pacchetto include un servizio Batocera
  progettato per attendere la disponibilita' del socket Wayland e la
  risoluzione delle dipendenze dinamiche prima di avviare Sunshine.

  Script di build:
  https://github.com/giubbe/sunshine-batocera-arm64/blob/main/ci/build-package.sh

- **Patch della cattura audio Linux:** prima della compilazione il workflow
  applica `0001-linux-audio-pa-stream-callback.patch` e verifica che nel
  sorgente risultante siano presenti `pa_stream_connect_record()` e
  `pa_stream_set_read_callback()` e che il precedente percorso di cattura
  basato su `pa_simple_read()` non sia piu' presente.

  Workflow:
  https://github.com/giubbe/sunshine-batocera-arm64/blob/main/.github/workflows/build.yml

Queste sono differenze tecniche verificabili tra i due metodi di distribuzione.
Non implicano, da sole, che questa build sia generalmente piu' veloce, piu'
stabile o migliore della variante BUA. Le prestazioni e il comportamento
runtime devono essere verificati sul target reale.

## Configurazione consigliata sul Raspberry Pi 5

Per il target validato si consiglia di aggiungere al file `sunshine.conf`:

```ini
capture = kms
encoder = software
sw_preset = ultrafast
```

KMS evita il percorso `zwlr-screencopy` usato dalla cattura Wayland e lascia
Sunshine sul percorso DRM/KMS disponibile in questa build.

### Giochi o emulatori troppo pesanti

Se un gioco o un emulatore presenta audio che gracchia, rallentamenti o video
irregolare, ridurre la **risoluzione reale del solo gioco/emulatore** resta una
regolazione utile da provare prima di abbassare l'intera interfaccia Batocera.
Nei test Dreamcast/Flycast, portare il gioco da 1080p a 720p ha inizialmente
eliminato il disturbo mantenendo EmulationStation/Batocera a 1080p.

Il comportamento pero' **non e' risultato stabile dopo reboot**: con configurazione
ancora verificata come `capture = kms`, `encoder = software` e gioco realmente a
`1280x720`, il gracchiare e' ricomparso. Per questo motivo la riduzione per-gioco
va considerata al momento un **workaround sperimentale**, non una soluzione
validata o garantita.

Il test resta comunque diagnostico e potenzialmente utile: chiedere soltanto a
Moonlight una risoluzione piu' bassa non riduce necessariamente la risoluzione
del framebuffer sorgente catturato da Sunshine; impostare invece il gioco o
l'emulatore a una risoluzione inferiore riduce realmente il framebuffer prodotto
dal target.

Configurazione attualmente consigliata per continuare i test:

```text
Batocera / EmulationStation: risoluzione preferita (es. 1920x1080)
Gioco / emulatore critico:   provare 1280x720 o inferiore
Sunshine capture:            KMS
Sunshine encoder:            software
Audio:                       attivo
```

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

La famiglia di build e' stata validata su **Raspberry Pi 5** con
**Batocera 43apu.1** e Sunshine `v2026.516.143833` / commit
`14ffa6fdaa53f7b51512be2b3d24f3939695403c`.

Verifiche eseguite sul target durante lo sviluppo:

- avvio, stop, reboot e servizio persistente Batocera;
- sessione storica di streaming di circa tre ore senza crash osservati;
- confronto tra cattura audio `pa_simple`, `pa_stream`, `parec` e `pw-record`;
- verifica che il percorso asincrono `pa_stream` non corrompa il monitor
  PipeWire come osservato con il percorso sincrono durante i test iniziali;
- confronto Wayland/KMS;
- test con Dreamcast/Flycast a 1080p e 720p;
- verifica di assenza di throttling (`throttled=0x0`) durante il caso critico;
- miglioramento iniziale osservato impostando il gioco Dreamcast a 720p, seguito
  pero' dalla ricomparsa del gracchiare dopo reboot a parita' di risoluzione e
  backend KMS; la causa residua e' quindi ancora in indagine.

La build finale prodotta dopo modifiche al repository deve comunque superare la
CI ed essere nuovamente verificata sul target prima di considerare estesa a
quell'esatto artifact tutta la validazione runtime sopra descritta.

### Warning noto sul target

Sul sistema validato il loader puo' emettere:

```text
/usr/lib/libcurl.so.4: no version information available
```

Il warning non ha impedito l'avvio di Sunshine ne' lo streaming nei test. Resta
una differenza ABI/symbol-versioning del target da considerare se il pacchetto
viene usato su revisioni Batocera differenti.

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
