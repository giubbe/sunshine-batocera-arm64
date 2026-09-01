# Sunshine ARM64 per Batocera 43 / Raspberry Pi 5

> [!WARNING]
> **Experimental/test build.** Non e' una release stabile: l'artifact esatto
> prodotto dalla CI deve ancora essere provato sul Raspberry Pi 5 reale.

Questo harness costruisce Sunshine per Raspberry Pi 5 / BCM2712 / Cortex-A76,
Batocera `43apu.1` e AArch64, con installazione in
`/userdata/system/add-ons/sunshine`.

Identita' fissate e verificate dalla CI:

- Sunshine tag `v2026.516.143833`, commit `14ffa6fdaa53f7b51512be2b3d24f3939695403c`;
- libpisp 1.7.0, commit `f8a5eb2af4c5dea76442785ef42b2fb1aa9e62f9`.

Il tratto distintivo e' il converter sperimentale
`BGR0 -> RGB888 (repack NEON) -> PiSP -> YUV420P -> libx264`. Matrice JPEG
(full range), scaler e formati restano quelli gia' validati; se inizializzazione
o conversione PiSP falliscono, Sunshine passa automaticamente a libswscale.
Non vengono usati i formati RGB32 PiSP non supportati dal backend provato.

La compilazione Sunshine usa Release, `-O3 -mcpu=cortex-a76
-mtune=cortex-a76` e CMake IPO/LTO. libpisp usa gli stessi flag CPU e di
ottimizzazione, ma non LTO: evitare LTO attraverso Meson/CMake mantiene semplice
e riproducibile il linking statico. Non sono usati `-ffast-math`, `-Ofast` o
assunzioni di undefined behaviour. Queste scelte non costituiscono un benchmark
ne' dimostrano un guadagno prestazionale.

## Differenze rispetto alla variante BUA ARM64

> **Questa build non vuole sostituire BUA e non e' dichiarata migliore di BUA.
> E' un esperimento separato finalizzato a verificare l'uso del PiSP del
> Raspberry Pi 5 nella pipeline software di Sunshine.**

| Aspetto | BUA ARM64 corrente | Questa build sperimentale |
|---|---|---|
| Sunshine | `v2025.924.154138-BUA-1-g0f5f4e27`, verificato estraendo l'AppImage ARM64 | `v2026.516.143833`, commit fissato `14ffa6fdaa53f7b51512be2b3d24f3939695403c` |
| Distribuzione | `Sunshine-aarch64.AppImage`, tramite installer BUA | binario/package Batocera dedicato |
| Configurazione pubblicamente verificabile | `encoder = software`, `sw_preset = ultrafast` | profilo esplicito e auditato in questo repository |
| Cattura/build | opzioni interne non attribuite: lo script di build dell'AppImage non e' pubblicato nel materiale verificato | KMS/DRM abilitato nella compilazione |
| Conversione | nessuna caratteristica interna ulteriore attribuita | libpisp 1.7.0 statica, converter PiSP sperimentale e fallback libswscale |
| CPU | non attribuita dal materiale pubblico | Release/O3, Cortex-A76, IPO/LTO per Sunshine |

Installer BUA pubblico: <https://github.com/batocera-unofficial-addons/batocera-unofficial-addons/blob/main/sunshine/sunshine-arm64.sh>.
Una versione Sunshine piu' nuova non implica automaticamente prestazioni o
stabilita' migliori.

## Audio upstream e gate passivi

L'audio coincide con il sorgente Sunshine fissato: `pa_simple_new()` e
`pa_simple_read()`. Non viene applicata alcuna cattura sperimentale `pa_stream`
callback/deque, non viene inserito un decoder Opus diagnostico e non esiste
alcuna scrittura `audio-preopus.f32le`/`audio-postopus.f32le`.

La CI conserva soltanto gate passivi che rifiutano un binario contenente
`AUDIO_PROBE` o `PIPELINE_TELEMETRY`; questi controlli non aggiungono telemetria
runtime. Gli injector e le patch diagnostiche non utilizzati sono stati rimossi.

## Configurazione osservata nel test Dreamcast/Flycast

La chiave Batocera 43.1 e' `flycast_render_resolution`; il prompt UI corretto e'
**RENDER RESOLUTION**. Il valore `480` corrisponde a `1x (640x480)`:

```text
Batocera -> Dreamcast/Flycast -> Advanced Game Options
RENDER RESOLUTION = 1x (640x480)

Run-Ahead Frames                    = None
Use Second Instance for Run-Ahead   = On
Automatic Frame Delay               = Off
Variable Refresh Rate               = Off
```

Questa e' la configurazione empiricamente risultata funzionante nel test sul
target specifico, non una raccomandazione universale Batocera. Con
`Run-Ahead Frames = None`, la seconda istanza non dovrebbe avere un effetto
operativo rilevante.

**VIDEO MODE** controlla la modalita'/risoluzione di uscita del display e non
garantisce la risoluzione di rendering interna di Flycast. Il precedente test
interpretato come rendering a 720p aveva modificato l'opzione sbagliata: il
risultato utile e' stato osservato con la vera RENDER RESOLUTION a 640x480.

## Attribuzione

L'harness di build e l'integrazione sperimentale PiSP sono stati sviluppati con
ChatGPT (OpenAI), con verifica e test runtime eseguiti dal maintainer sul
Raspberry Pi 5 reale. Sunshine e libpisp restano opere dei rispettivi progetti
upstream; questa attribuzione non riguarda i loro sorgenti.

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

## Validazione runtime ancora necessaria

La CI valida sorgenti, build, ELF, dipendenze e artifact, ma non equivale a un
test Batocera. Prima di promuovere qualsiasi release devono essere riprovati sul
Raspberry Pi 5 reale: accesso al device PiSP, conversione e fallback, qualita' e
range video, stabilita', latenza/prestazioni, audio PulseAudio upstream,
streaming Flycast e avvio persistente dopo reboot. Fino ad allora il risultato
resta **experimental/test build**.

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
