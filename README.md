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

La variante ARM64 di Batocera Unofficial Add-ons (BUA) installa un
`Sunshine-aarch64.AppImage`, abilita `encoder = software` e avvia l'AppImage
tramite FUSE. Questa implementazione segue un approccio diverso e piu' adatto al
target Raspberry Pi 5/Batocera testato:

- **KMS disponibile:** Sunshine upstream avverte che l'AppImage non supporta la
  cattura KMS. Questa build e' invece compilata con `SUNSHINE_ENABLE_DRM=ON` e
  puo' usare `capture = kms`, che e' la modalita' consigliata su questo target.
- **Build riproducibile e sorgente fissato:** tag, commit upstream, opzioni CMake
  e runner ARM64 sono espliciti e verificati dalla CI. Non viene semplicemente
  scaricato un AppImage precompilato dal repository.
- **Niente FUSE/AppImage a runtime:** il pacchetto contiene il binario e gli
  asset installabili direttamente sotto `/userdata/system/add-ons/sunshine`.
- **Dipendenze controllate:** vengono incluse solo le librerie ICU 74 realmente
  necessarie al binario prodotto; non vengono inventati symlink ABI.
- **Audit CI:** architettura AArch64, `DT_NEEDED`, bundle, identita' della build,
  wrapper e servizio Batocera vengono controllati prima di produrre l'artifact.
- **Servizio Batocera robusto:** il launcher aspetta che Wayland e le dipendenze
  runtime siano realmente disponibili, evitando la race osservata durante il
  boot di Batocera.
- **Cattura audio asincrona:** la build applica una piccola patch Linux che
  sostituisce il percorso sincrono `libpulse-simple/pa_simple_read()` con un
  `pa_stream` asincrono e una coda PCM. Nei test sul target questo ha eliminato
  la corruzione del monitor PipeWire che il percorso sincrono riusciva a
  provocare durante la cattura.

Queste differenze non significano che BUA sia in generale "sbagliata": il suo
AppImage privilegia semplicita' di distribuzione e portabilita'. Questo progetto
privilegia invece **controllo, KMS, riproducibilita' e validazione specifica sul
Pi 5**.

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
irregolare **non e' necessario abbassare la risoluzione dell'intera interfaccia
Batocera**. Il test che ha risolto il caso Dreamcast consiste nel lasciare
EmulationStation/Batocera a `1920x1080` e impostare **solo quel gioco o
emulatore** a una risoluzione inferiore, ad esempio `1280x720`.

Sul Raspberry Pi 5 testato la combinazione seguente funziona correttamente:

```text
Batocera / EmulationStation: 1920x1080
Dreamcast / Flycast:         1280x720 @ 60 Hz
Sunshine capture:            KMS
Moonlight:                   1280x720 @ 60 fps
audio:                       attivo
```

Il punto importante e' la **risoluzione reale del framebuffer del gioco**. Nei
test, chiedere semplicemente a Moonlight una risoluzione piu' bassa mentre il
gioco continuava a renderizzare a 1080p non bastava: Sunshine continuava a
catturare il framebuffer sorgente 1920x1080. Portando invece il solo gioco a
720p, audio e video sono tornati regolari senza rinunciare al desktop Batocera
1080p.

Questa e' quindi la prima regolazione da provare per i titoli che mettono in
crisi lo streaming: **ridurre la risoluzione del solo gioco/emulatore che ne ha
bisogno**, mantenendo il resto del sistema alla risoluzione preferita.

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
- prova operativa finale con Batocera 1080p e gioco Dreamcast 720p60, con audio
  e video regolari durante lo streaming.

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
