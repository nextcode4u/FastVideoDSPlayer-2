#include <nds.h>
#include "FastVideo/fvWait.h"
#include <nds/fifocommon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../../common/ipc.h"
#include "FastVideo/fvDecoder.h"
#include "FastVideo/fvMcData.h"
#include "FastVideo/fvPlayer.h"
#include "mpu.h"
#include "gui/PlayerController.h"
#include "gui/BrowserController.h"
#include "gui/OptionsMenu.h"
#include "Preferences.h"
#include "LibraryState.h"
#include "MediaText.h"
#include "Storage.h"
#include "LoaderReturn.h"
#include "../../common/twlwram.h"

static DTCM_BSS fv_player_t sPlayer;
static PlayerController* sPlayerController;

extern u8 gDldiStub[];

static char sAdjacentPath[FV_MAX_PATH_LEN] ALIGN(32);
static char sCurPath[FV_MAX_PATH_LEN];

static bool sCanUseWram;
static bool sLoopEnabled = false;

// 0: OFF, 1: ON (Dossier), 2: ON ALL (Toute la SD)
static int sRandomMode = 0; 

static bool sStandalone = false;
static char sBrowserDir[FV_MAX_PATH_LEN];
static char sBrowserPick[FV_MAX_PATH_LEN];

// Récupération de la variable d'inversion des écrans depuis PlayerController.cpp
extern bool gScreenSwapped;

// --- GESTION DE L'HISTORIQUE ---
#define HISTORY_SIZE 20
static char sHistory[HISTORY_SIZE][FV_MAX_PATH_LEN];
static int sHistoryCount = 0;
static int sHistoryIdx = 0;

u32 GetDebounceTicks();
static char *sSubtitleText;
static MediaText::Cue sCues[2048];
static size_t sCueCount;
static char sQueue[256][FV_MAX_PATH_LEN];
static int sQueueCount, sQueuePosition;
static bool sReturnRequested;
static bool sHistoryDirty;
static u32 sOverlayTick;

static void SaveCurrent()
{
    if (!sPlayerController)
        return;
    LibraryState::Remember(sCurPath, sPlayer.curFrame, sPlayer.fvHeader->nrFrames, sPlayer.videoEnded);
    sHistoryDirty = true;
}
static void FlushHistory()
{
    if (sHistoryDirty)
    {
        if (!LibraryState::Save())
            OptionsMenu::Notice(Preferences::Text("Could not save playback history. Check the SD card.",
                                                  "Echec sauvegarde de l'historique. Verifiez la carte SD."));
        else
            sHistoryDirty = false;
    }
}
static void ApplyPreferences()
{
    using namespace Preferences;
    fv_setScaleMode(&sPlayer, (fv_scale_mode_t)values[Scale]);
    fv_audioOptions(values[Volume], values[Mute], values[Mono], values[AudioDelay]);
    gScreenSwapped = values[Screen] != 0;
    if (gScreenSwapped)
        lcdMainOnBottom();
    else
        lcdMainOnTop();
}
static void LoadSubtitles(const char *path)
{
    free(sSubtitleText);
    sSubtitleText = 0;
    sCueCount = 0;
    char sidecar[256];
    snprintf(sidecar, sizeof(sidecar), "%s", path);
    char *extension = strrchr(sidecar, '.');
    if (!extension || extension - sidecar > 250)
        return;
    strcpy(extension, ".srt");
    sSubtitleText = Storage::ReadText(sidecar, FV_FILE_LIMIT);
    if (sSubtitleText)
    {
        size_t skipped;
        sCueCount = MediaText::Subtitles(sSubtitleText, sCues, 2048, skipped);
    }
}
static void UpdateOverlay()
{
    if (!sPlayerController)
        return;
    u32 now = GetDebounceTicks();
    if ((u32)(now - sOverlayTick) < 3272)
        return;
    sOverlayTick = now;
    using namespace Preferences;
    char caption[129] = {}, diagnostic[64] = {};
    int64_t time = (u64)sPlayer.curFrame * sPlayer.fvHeader->fpsDen * 1000 / sPlayer.fvHeader->fpsNum;
    time -= values[SubtitleDelay];
    int cue = values[Subtitles] && time >= 0 ? MediaText::ActiveCue(sCues, sCueCount, (uint32_t)time) : -1;
    if (cue >= 0)
        MediaText::DisplayText(sSubtitleText + sCues[cue].text, sCues[cue].length, caption, sizeof(caption));
    fv_audio_status_t audio = {};
    fv_audioStatus(&audio);
    if (values[Diagnostics])
        snprintf(diagnostic, sizeof(diagnostic), "Late %d AV %+dms Q%lu U%lu", sPlayer.lateCount,
                 sPlayer.syncErrorMs, (unsigned long)audio.queuedBlocks, (unsigned long)audio.underruns);
    else if (audio.flags & FV_AUDIO_BUFFERING)
        snprintf(diagnostic, sizeof(diagnostic), "%s", Text("Buffering...", "Chargement..."));
    const char *name = strrchr(sCurPath, '/');
    sPlayerController->SetDetails(name ? name + 1 : sCurPath, caption, diagnostic);
}
static bool PickRecent(char *out)
{
    const char *items[32];
    for (int i = 0; i < LibraryState::recentCount; ++i)
    {
        const char *name = strrchr(LibraryState::recent[i].path, '/');
        items[i] = name ? name + 1 : LibraryState::recent[i].path;
    }
    int selected = OptionsMenu::Choose(Preferences::Text("Recent videos", "Videos recentes"), items,
                                       LibraryState::recentCount);
    if (selected < 0)
        return false;
    strcpy(out, LibraryState::recent[selected].path);
    return true;
}
static bool OpenPlaylist(const char *path)
{
    char *text = Storage::ReadText(path, 65536);
    if (!text)
        return false;
    size_t skipped;
    sQueueCount = MediaText::Playlist(text, path, sQueue, 256, skipped);
    free(text);
    sQueuePosition = 0;
    if (skipped)
        OptionsMenu::Notice(Preferences::Text(
            "Some playlist entries were invalid, unsupported or beyond the 256-title limit.",
            "Certaines entrees sont invalides ou depassent la limite de 256 videos."));
    return sQueueCount != 0;
}

static void AddToHistory(const char* path) {
    snprintf(sHistory[sHistoryIdx], FV_MAX_PATH_LEN, "%s", path);
    sHistory[sHistoryIdx][FV_MAX_PATH_LEN - 1] = '\0';
    sHistoryIdx = (sHistoryIdx + 1) % HISTORY_SIZE;
    if (sHistoryCount < HISTORY_SIZE) sHistoryCount++;
}

static bool IsInHistory(const char* path) {
    for (int i = 0; i < sHistoryCount; i++) {
        if (strcasecmp(sHistory[i], path) == 0) return true;
    }
    return false;
}
// --------------------------------

u32 GetDebounceTicks();

static void DestroyCurrentPlayer();

// Seed du LCG utilisé par le reservoir sampling en mode ALL. Persistante
// (au lieu d'être recalculée à chaque appel depuis GetDebounceTicks) pour
// éviter que deux appels rapprochés (vidéo courte, appuis rapides sur
// suivant) ne partent d'un état de timer quasi identique et ne retirent
// des séquences de tirages corrélées.
static u32 sShuffleSeed = 0;
static bool sShuffleSeedInit = false;

static const char* GetFileName(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void ShowVideoMessage()
{
    if (!sPlayerController)
        return;

    using Preferences::Text;
    char modes[80];
    const char *shuffle = sRandomMode == 2 ? Text("library", "bibliotheque")
                         : sRandomMode ? Text("folder", "dossier") : Text("off", "non");
    snprintf(modes, sizeof(modes), "%s: %s / %s: %s", Text("Repeat", "Boucle"),
             sLoopEnabled ? Text("on", "oui") : Text("off", "non"), Text("Shuffle", "Aleat."), shuffle);
    sPlayerController->ShowMessage(modes, "");
}

static char sLoadCandidate[FV_MAX_PATH_LEN]; // buffer static de transit vers fv_initPlayer, voir commentaire ci-dessous

static bool loadAndStartVideo(const char* path)
{
    if (sPlayerController)
    {
        fv_pausePlayer(&sPlayer);
        SaveCurrent();
        FlushHistory();
        delete sPlayerController;
        sPlayerController = NULL;
        fv_destroyPlayer(&sPlayer);
        Preferences::EndVideo();
    }

    // Copie systématique dans un buffer STATIC avant fv_initPlayer : le
    // chemin est lu côté ARM7 (ouverture du fichier via IPC), qui est
    // physiquement incapable d'accéder à la pile ARM9 si elle réside en
    // DTCM (le cas par défaut sur cette toolchain). Si `path` pointe vers
    // une variable locale de l'appelant (ex: un candidat pioché sur la
    // pile en mode ALL), passer ce pointeur brut à fv_initPlayer plante -
    // d'où l'usage de sLoadCandidate plutôt que `path` directement, tout en
    // laissant sCurPath/l'historique intacts tant que le chargement n'a
    // pas réellement réussi (sinon un échec les pollue avec un chemin
    // jamais joué, ce qui fausse les exclusions "vidéo courante"/"déjà vue
    // récemment" ailleurs dans le fichier - particulièrement sensible en
    // mode ALL où switchToRandomVideoAll() peut retenter plusieurs fois).
    strncpy(sLoadCandidate, path, sizeof(sLoadCandidate) - 1);
    sLoadCandidate[sizeof(sLoadCandidate) - 1] = 0;

    if (!fv_initPlayer(&sPlayer, sLoadCandidate, sCanUseWram))
    {
        fv_destroyPlayer(&sPlayer);
        Preferences::EndVideo();
        return false; // Échec du chargement
    }

    strncpy(sCurPath, sLoadCandidate, sizeof(sCurPath) - 1);
    sCurPath[sizeof(sCurPath) - 1] = 0;
    AddToHistory(sCurPath);

    LoadSubtitles(sCurPath);
    LibraryState::Record *record = LibraryState::Find(sCurPath);
    u32 resumeFrame = 0;
    bool matchingRecord = record && record->frames == sPlayer.fvHeader->nrFrames;
    Preferences::BeginVideo(record ? record->scale : 0, record ? record->audioDelay : 0,
                            record ? record->subtitleDelay : 0, matchingRecord);
    if (matchingRecord)
    {
        if (Preferences::values[Preferences::Resume] && record->frame && !sQueueCount)
        {
            const char *items[] = {Preferences::Text("Resume", "Reprendre"),
                                   Preferences::Text("Start over", "Depuis le debut"),
                                   Preferences::Text("Bookmark", "Signet")};
            int choice =
                OptionsMenu::Choose(Preferences::Text("Continue watching?", "Continuer la video?"), items, 3);
            if (choice == 0)
                resumeFrame = record->frame;
            if (choice == 2)
                resumeFrame = record->bookmark;
        }
    }
    ApplyPreferences();
    if (resumeFrame)
        fv_gotoNearestKeyFrame(&sPlayer, resumeFrame);
    fv_startPlayer(&sPlayer);
    sPlayerController = new PlayerController(&sPlayer);
    sPlayerController->Initialize();
    sOverlayTick = GetDebounceTicks() - 3272;
    UpdateOverlay();
    return true;
}

static void switchToAdjacentVideo(bool next)
{
    if (sQueueCount)
    {
        sQueuePosition = (sQueuePosition + (next ? 1 : sQueueCount - 1)) % sQueueCount;
        loadAndStartVideo(sQueue[sQueuePosition]);
        return;
    }
    fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(next ? IPC_CMD_FIND_NEXT_FILE : IPC_CMD_FIND_PREV_FILE,
                                               (u32)sAdjacentPath));
    fv_waitReply(FIFO_USER_02);
    u32 found = fifoGetValue32(FIFO_USER_02) & IPC_CMD_ARG_MASK;
    if (!found) return;

    DC_InvalidateRange(sAdjacentPath, sizeof(sAdjacentPath));
    loadAndStartVideo(sAdjacentPath);
}

static void switchToRandomVideo()
{
    fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_FIND_RANDOM_FILE, (u32)sAdjacentPath));
    fv_waitReply(FIFO_USER_02);
    u32 found = fifoGetValue32(FIFO_USER_02) & IPC_CMD_ARG_MASK;
    if (!found) return;

    DC_InvalidateRange(sAdjacentPath, sizeof(sAdjacentPath));
    loadAndStartVideo(sAdjacentPath);
}

#define MAX_DIR_STACK 256
static char sDirStack[MAX_DIR_STACK][FV_MAX_PATH_LEN];
static fv_listdir_req_t sListReq ALIGN(32);
static fv_dir_entry_t sListEntries[256] ALIGN(32);

// Index complet des .fv de la carte SD, construit une seule fois par
// lancement de l'app (au premier passage en mode ALL), puis gardé en RAM
// pour toute la session : plus aucun IPC_CMD_LIST_DIR au moment de choisir
// une vidéo, juste une lecture de tableau. Stockage en chemins concaténés
// (plutôt qu'un tableau [N][FV_MAX_PATH_LEN] qui gâcherait ~200 octets par
// entrée en moyenne) pour rester large sans peser sur la RAM ARM9.
//
// Capacité dimensionnée pour ~250 vidéos avec une marge confortable, pas
// pour un maximum théorique : la première version (4096 entrées / 256 Ko)
// réservait ~288 Ko de .bss en permanence pour une bibliothèque qui n'en
// utilise qu'une quinzaine de Ko, ce qui est probablement ce qui a fait
// déborder la RAM disponible au runtime (lancé via TWiLight Menu++, la RAM
// réellement utilisable est souvent bien inférieure aux 4 Mo nominaux) -
// le crash juste après l'indexation, au moment où fv_initPlayer() alloue
// ses propres buffers de décodage, colle avec ce diagnostic.
#define VIDEO_INDEX_MAX_ENTRIES 600
#define VIDEO_INDEX_BUF_SIZE    (48 * 1024)
static char sIndexBuf[VIDEO_INDEX_BUF_SIZE];
static u32  sIndexOffset[VIDEO_INDEX_MAX_ENTRIES];
static u32  sIndexCount = 0;
static u32  sIndexBufUsed = 0;
static bool sIndexBuilt = false;

// Bag mélangé (Fisher-Yates) sur les indices [0..sIndexCount) : tirage sans
// remise jusqu'à épuisement, puis remélange - même principe que le shuffle
// bag déjà utilisé côté ARM7 pour le mode aléatoire par dossier.
static u32 sShuffleBag[VIDEO_INDEX_MAX_ENTRIES];
static u32 sShuffleBagRemaining = 0;

static void RebuildShuffleBag()
{
    sShuffleBagRemaining = sIndexCount;
    for (u32 i = 0; i < sIndexCount; i++) sShuffleBag[i] = i;
    for (u32 i = sIndexCount; i > 1; i--) {
        sShuffleSeed = (1103515245 * sShuffleSeed + 12345);
        u32 j = (sShuffleSeed >> 16) % i;
        u32 tmp = sShuffleBag[i - 1];
        sShuffleBag[i - 1] = sShuffleBag[j];
        sShuffleBag[j] = tmp;
    }
}

// Index only the configured library, on demand. Directory pages are bounded;
// the existing 600-title / 48 KiB index limits still apply. B cancels between
// pages so whole-card traversal is never required just to browse a folder.
static int BuildVideoIndex()
{
    sIndexCount = 0;
    sIndexBufUsed = 0;
    int failedDirs = 0;

    int queueHead = 0;
    int queueTail = 0;
    snprintf(sDirStack[queueTail], FV_MAX_PATH_LEN, "%s", Preferences::videoRoot);
    queueTail++;

    while (queueHead < queueTail) {
        scanKeys();
        if (keysDown() & KEY_B)
        {
            sIndexCount = 0;
            sIndexBufUsed = 0;
            return -1;
        }
        char currentDir[FV_MAX_PATH_LEN];
        strncpy(currentDir, sDirStack[queueHead], FV_MAX_PATH_LEN - 1);
        currentDir[FV_MAX_PATH_LEN - 1] = '\0';
        queueHead++;

        strncpy(sListReq.path, currentDir, FV_MAX_PATH_LEN - 1);
        sListReq.path[FV_MAX_PATH_LEN - 1] = '\0';
        sListReq.entries = sListEntries;
        sListReq.maxEntries = 256;
        sListReq.direction = 0;
        do
        {

            DC_FlushRange(&sListReq, sizeof(sListReq));
            DC_FlushRange(sListEntries, sizeof(sListEntries));

            fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_LIST_DIR, (u32)&sListReq));

            // Attente bloquante : le protocole IPC_CMD_LIST_DIR n'autorise
            // qu'une seule requête en vol à la fois (sListReq/sListEntries sont
            // des buffers partagés, la réponse ne porte aucun identifiant de
            // requête). Un timeout qui abandonnerait puis enverrait la requête
            // suivante désynchroniserait durablement le protocole : la réponse
            // tardive de l'ARM7 à la requête abandonnée serait alors lue comme
            // la réponse d'une requête ultérieure, avec des données de mauvais
            // dossier dans sListEntries -> crash quasi garanti en aval.
            fv_waitReply(FIFO_USER_02);

            u32 ok = fifoGetValue32(FIFO_USER_02) & IPC_CMD_ARG_MASK;

            DC_InvalidateRange(&sListReq, sizeof(sListReq));
            DC_InvalidateRange(sListEntries, sizeof(sListEntries));

            if (!ok)
            {
                failedDirs++;
                break;
            }

            for (u32 i = 0; i < sListReq.count; i++)
            {
                const char *dName = sListEntries[i].name;

                if (dName[0] == '.')
                    continue;
                if (strcasecmp(dName, "System Volume Information") == 0)
                    continue;
                if (strcasecmp(dName, "_nds") == 0)
                    continue;
                if (strcasecmp(dName, "Nintendo 3DS") == 0)
                    continue;
                if (strcasecmp(dName, "Nintendo DSi") == 0)
                    continue;
                if (strcasecmp(dName, "TWiLightMenu") == 0)
                    continue;
                if (strcasecmp(dName, "luma") == 0)
                    continue;
                if (strcasecmp(dName, "DCIM") == 0)
                    continue;

                char fullPath[FV_MAX_PATH_LEN];
                int dirLen = strlen(currentDir);
                int nameLen = strlen(dName);

                if (dirLen + nameLen + 2 >= FV_MAX_PATH_LEN)
                    continue;

                strcpy(fullPath, currentDir);
                if (dirLen > 0 && fullPath[dirLen - 1] != '/')
                    strcat(fullPath, "/");
                strcat(fullPath, dName);

                if (sListEntries[i].isDir)
                {
                    if (queueTail < MAX_DIR_STACK)
                    {
                        strncpy(sDirStack[queueTail], fullPath, FV_MAX_PATH_LEN - 1);
                        queueTail++;
                    }
                }
                else if (nameLen > 3 && strcasecmp(dName + nameLen - 3, ".fv") == 0)
                {
                    size_t pathLen = strlen(fullPath);
                    if (sIndexCount < VIDEO_INDEX_MAX_ENTRIES &&
                        sIndexBufUsed + pathLen + 1 <= VIDEO_INDEX_BUF_SIZE)
                    {
                        memcpy(sIndexBuf + sIndexBufUsed, fullPath, pathLen + 1);
                        sIndexOffset[sIndexCount] = sIndexBufUsed;
                        sIndexBufUsed += (u32)(pathLen + 1);
                        sIndexCount++;
                    }
                }
            }
            if (!sListReq.more || !sListReq.count)
                break;
            sListReq.boundary = sListEntries[sListReq.count - 1];
            sListReq.direction = 1;
            scanKeys();
            if (keysDown() & KEY_B)
            {
                sIndexCount = 0;
                sIndexBufUsed = 0;
                return -1;
            }
        } while (true);
    }

    return failedDirs;
}

static void switchToRandomVideoAll()
{
    // Sauvegarde AVANT loadAndStartVideo(), qui écrase sCurPath dès l'appel
    // (avant même de savoir si le chargement réussit). Sans ça, le filet de
    // sécurité plus bas rejoue le fichier en échec au lieu de l'ancienne vidéo.
    char prevPath[FV_MAX_PATH_LEN];
    strncpy(prevPath, sCurPath, FV_MAX_PATH_LEN - 1);
    prevPath[FV_MAX_PATH_LEN - 1] = '\0';

    DestroyCurrentPlayer();

    // SÉCURITÉ IPC : On vide les anciens messages bloqués pour éviter une désynchronisation
    while (fifoCheckValue32(FIFO_USER_02)) {
        fifoGetValue32(FIFO_USER_02);
    }

    // Petite pause pour laisser la SD fermer proprement le fichier vidéo
    for (int i = 0; i < 10; i++) swiWaitForVBlank();

    if (!sShuffleSeedInit) {
        sShuffleSeed = GetDebounceTicks() ^ 0x13579BDF;
        sShuffleSeedInit = true;
    }

    if (!sIndexBuilt) {
        // Réinitialise la console texte sur l'écran du bas : PlayerView::Initialize()
        // a repris BG0/BG1/BG2/sprites pour son propre affichage (fond, compteur de
        // temps, légende...) pendant la lecture. Sans ce nettoyage, l'écran
        // "Indexation..." s'afficherait par-dessus/mélangé à ces résidus.
        oamClear(&oamSub, 0, 128);
        REG_DISPCNT_SUB = MODE_0_2D;
        consoleInit(NULL, 2, BgType_Text4bpp, BgSize_T_256x256, 2, 1, false, true);
        REG_DISPCNT_SUB = MODE_0_2D | DISPLAY_BG2_ACTIVE;

        consoleClear();
        iprintf("%s", Preferences::Text("Indexing video folder...\nB: cancel",
                                        "Indexation du dossier...\nB: annuler"));
        swiWaitForVBlank();

        int failedDirs = BuildVideoIndex();
        // Index vide ET au moins un dossier en échec IPC (pas juste "carte
        // sans .fv") -> probable glitch I/O transitoire, on retente une fois.
        if (sIndexCount == 0 && failedDirs > 0) {
            for (int i = 0; i < 10; i++) swiWaitForVBlank();
            failedDirs = BuildVideoIndex();
        }

        if (failedDirs < 0)
        {
            loadAndStartVideo(prevPath);
            return;
        }
        RebuildShuffleBag();
        // Construit une seule fois par lancement de l'app : un échec I/O
        // persistant ou une bibliothèque vide ne redéclenchera pas de
        // nouvelle tentative avant un redémarrage du logiciel.
        sIndexBuilt = true;

        consoleClear();
        iprintf(Preferences::Text("%lu videos indexed.", "%lu videos indexees."), (unsigned long)sIndexCount);
        swiWaitForVBlank();
        for (int i = 0; i < 30; i++) swiWaitForVBlank();
        consoleClear();
    }

    bool loadSuccess = false;

    if (sIndexCount > 0) {
        // Pioche dans le bag en sautant la vidéo courante et l'historique
        // récent, et RETENTE avec un autre candidat si le chargement échoue
        // (fichier illisible, glitch I/O ponctuel) - sans ce retry, un seul
        // échec de chargement faisait retomber sur prevPath juste en dessous,
        // ce qui donnait l'impression que "next" relançait la même vidéo.
        // Jamais plus d'un tour complet du bag pour rester borné si la
        // bibliothèque est petite et très couverte par l'historique.
        u32 attemptsLeft = sIndexCount;
        while (attemptsLeft-- > 0 && !loadSuccess) {
            if (sShuffleBagRemaining == 0)
                RebuildShuffleBag();
            u32 idx = sShuffleBag[--sShuffleBagRemaining];
            const char* candidate = sIndexBuf + sIndexOffset[idx];
            if (strcasecmp(candidate, sCurPath) == 0) continue;
            if (IsInHistory(candidate)) continue;

            char selected[FV_MAX_PATH_LEN];
            strncpy(selected, candidate, FV_MAX_PATH_LEN - 1);
            selected[FV_MAX_PATH_LEN - 1] = '\0';
            loadSuccess = loadAndStartVideo(selected);
        }
    }

    // SÉCURITÉ DE SECOURS : rien trouvé ou échec du chargement -> relance l'ancienne vidéo
    if (!loadSuccess) {
        if (sIndexCount == 0) sHistoryCount = 0; // vide l'historique s'il est plein
        if (prevPath[0]) loadAndStartVideo(prevPath);
    }
}

static void DestroyCurrentPlayer()
{
    if (sPlayerController)
    {
        fv_pausePlayer(&sPlayer);
        SaveCurrent();
        FlushHistory();
        delete sPlayerController;
        sPlayerController = NULL;
        fv_destroyPlayer(&sPlayer);
        Preferences::EndVideo();
        free(sSubtitleText);
        sSubtitleText = 0;
        sCueCount = 0;
        if (!Preferences::Save())
            OptionsMenu::Notice(
                Preferences::Text("Could not save settings.", "Echec sauvegarde des options."));
    }
}

static void GetParentDir(const char* path, char* out, size_t outMax)
{
    const char* slash = strrchr(path, '/');
    if (!slash)
    {
        snprintf(out, outMax, "%s", path);
        out[outMax - 1] = 0;
        return;
    }
    size_t len = (size_t)(slash - path);
    if (len == 0)
    {
        strncpy(out, "/", outMax - 1);
        out[outMax - 1] = 0;
        return;
    }
    if (len >= outMax)
        len = outMax - 1;
    memcpy(out, path, len);
    out[len] = 0;
    if (strchr(out, ':') && !strchr(out, '/'))
    {
        if (len + 2 < outMax)
        {
            out[len] = '/';
            out[len + 1] = 0;
        }
    }
}

static int RunBrowser(char* outPath, size_t outPathMax, char* outDir, size_t outDirMax, const char* selectName)
{
    // --- NOUVEAUTÉ : On force les écrans dans le bon sens (vidéo en haut, menu en bas) ---
    gScreenSwapped = false;
    lcdMainOnTop();
    // -----------------------------------------------------------------------------------

    PlayerController::RestoreSubScreen();
    BrowserController browser;
    if (!browser.OpenDir(sBrowserDir))
    {
        snprintf(sBrowserDir, sizeof(sBrowserDir), "%s/", Storage::Drive());
        if (!browser.OpenDir(sBrowserDir))
        {
            OptionsMenu::Notice(Preferences::Text("Video folder unavailable. Check the SD card.",
                                                  "Dossier indisponible. Verifiez la carte SD."));
            return 0;
        }
    }

    browser.SetModes(sLoopEnabled, sRandomMode > 0);
    if (selectName)
        browser.SelectEntryByName(selectName);

    char tmp[FV_MAX_PATH_LEN];
    for (;;)
    {
        BrowserController::Action action = browser.Update();
        switch (action)
        {
            case BrowserController::ACT_EXIT:
                return 0;

            case BrowserController::ACT_PLAY:
                browser.GetSelectedPath(outPath, outPathMax);
                if (!*outPath)
                    break;
                {
                    const char *ext = strrchr(outPath, '.');
                    if (ext && (!strcasecmp(ext, ".m3u") || !strcasecmp(ext, ".m3u8")))
                    {
                        if (!OpenPlaylist(outPath))
                        {
                            OptionsMenu::Notice(Preferences::Text("No playable entries in playlist.",
                                                                  "Liste sans video valide."));
                            browser.OpenDir(sBrowserDir);
                            break;
                        }
                        snprintf(outPath, outPathMax, "%s", sQueue[0]);
                    }
                    else
                        sQueueCount = 0;
                }
                snprintf(outDir, outDirMax, "%s", browser.GetCurDir());
                outDir[outDirMax - 1] = 0;
                consoleClear();
                return 1;

            case BrowserController::ACT_OPEN_DIR:
                browser.GetSelectedPath(tmp, sizeof(tmp));
                browser.OpenDir(tmp);
                break;

            case BrowserController::ACT_SETTINGS:
            {
                char current[256];
                snprintf(current, sizeof(current), "%s", browser.GetCurDir());
                OptionsMenu::Action menu = OptionsMenu::Show(false, current);
                if (menu == OptionsMenu::Return)
                {
                    sReturnRequested = true;
                    return 0;
                }
                if (menu == OptionsMenu::Recent && PickRecent(outPath))
                {
                    sQueueCount = 0;
                    return 1;
                }
                sLoopEnabled = Preferences::values[Preferences::Repeat];
                sRandomMode = Preferences::values[Preferences::Shuffle];
                if (menu == OptionsMenu::SetRoot)
                    sIndexBuilt = false;
                if (menu == OptionsMenu::BrowseRoot)
                    snprintf(current, sizeof(current), "%s/", Storage::Drive());
                browser.OpenDir(current);
                break;
            }
            case BrowserController::ACT_RECENTS:
            {
                char current[256];
                snprintf(current, sizeof(current), "%s", browser.GetCurDir());
                if (PickRecent(outPath))
                {
                    sQueueCount = 0;
                    return 1;
                }
                browser.OpenDir(current);
                break;
            }
            case BrowserController::ACT_PARENT:
                GetParentDir(browser.GetCurDir(), tmp, sizeof(tmp));
                browser.OpenDir(tmp);
                break;

            default:
                break;
        }
    }
}

static bool RunPlayerLoop(bool canReturnToBrowser)
{
    bool shouldExit = false;
    bool backToBrowser = false;
    bool readErrorShown = false;

    while (sPlayerController && !shouldExit)
    {
        bool wasPlaying = sPlayerController->IsPlaying();
        int previousFrame = sPlayer.curFrame;
        PlayerController::NavAction action = sPlayerController->Update();
        if (!sPlayerController->IsPlaying() && (wasPlaying || previousFrame != sPlayer.curFrame))
            SaveCurrent();
        if (sHistoryDirty && !sPlayerController->IsPlaying() && !(keysCurrent() & KEY_LID))
            FlushHistory();
        UpdateOverlay();
        if (sPlayer.readError) {
            if (!readErrorShown)
                sPlayerController->ShowMessage(
                    Preferences::Text("Video read failed", "Lecture impossible"),
                    Preferences::Text("B: back to browser", "B: liste des videos"));
            readErrorShown = true;
            if (action != PlayerController::NAV_ACTION_EXIT) continue;
        }
        switch (action)
        {
            case PlayerController::NAV_ACTION_NEXT:
                if (sRandomMode == 2) switchToRandomVideoAll();
                else if (sRandomMode == 1) switchToRandomVideo();
                else switchToAdjacentVideo(true);
                break;

            case PlayerController::NAV_ACTION_PREV:
                if (sRandomMode == 2) switchToRandomVideoAll();
                else if (sRandomMode == 1) switchToRandomVideo();
                else switchToAdjacentVideo(false);
                break;

            case PlayerController::NAV_ACTION_VIDEO_ENDED:
                if (Preferences::values[Preferences::EndAction] == 3)
                {
                    sReturnRequested = true;
                    shouldExit = true;
                    break;
                }
                if (Preferences::values[Preferences::EndAction] == 0 ||
                    (sQueueCount && sQueuePosition == sQueueCount - 1 && !sLoopEnabled &&
                     Preferences::values[Preferences::EndAction] != 2))
                {
                    shouldExit = true;
                    backToBrowser = true;
                    break;
                }
                if (sLoopEnabled || Preferences::values[Preferences::EndAction] == 2)
                    loadAndStartVideo(sCurPath);
                else if (sRandomMode == 2) switchToRandomVideoAll();
                else if (sRandomMode == 1) switchToRandomVideo();
                else switchToAdjacentVideo(true);
                break;

            case PlayerController::NAV_ACTION_TOGGLE_LOOP:
                sLoopEnabled = !sLoopEnabled;
                Preferences::values[Preferences::Repeat] = sLoopEnabled;
                ShowVideoMessage();
                break;

            case PlayerController::NAV_ACTION_TOGGLE_RANDOM:
                // MODIFICATION : Désormais accessible aussi en version autonome sur DSi/3DS
                if (!isDSiMode()) {
                    // Les très vieilles DS "Fat/Lite" restent limitées au mode aléatoire simple
                    // car le scan de la carte entière demande trop de ressources.
                    sRandomMode = (sRandomMode == 0) ? 1 : 0;
                } else {
                    sRandomMode++;
                    if (sRandomMode > 2) sRandomMode = 0;
                }
                Preferences::values[Preferences::Shuffle] = sRandomMode;
                ShowVideoMessage();
                break;

            case PlayerController::NAV_ACTION_SHOW_INFO:
                ShowVideoMessage();
                break;

            case PlayerController::NAV_ACTION_OPTIONS:
            {
                bool wasPlaying = sPlayerController->IsPlaying();
                sPlayerController->SetPaused(true);
                SaveCurrent();
                FlushHistory();
                OptionsMenu::Action menu = OptionsMenu::Show(true);
                char selection[256] = {};
                if (menu == OptionsMenu::Bookmark)
                {
                    LibraryState::Record *record = LibraryState::Find(sCurPath);
                    if (record)
                    {
                        record->bookmark = sPlayer.curFrame;
                        sHistoryDirty = true;
                        FlushHistory();
                    }
                }
                if (menu == OptionsMenu::Recent)
                    PickRecent(selection);
                if (menu == OptionsMenu::Queue)
                {
                    const char *items[256];
                    for (int i = 0; i < sQueueCount; ++i)
                    {
                        const char *name = strrchr(sQueue[i], '/');
                        items[i] = name ? name + 1 : sQueue[i];
                    }
                    int chosen = OptionsMenu::Choose(Preferences::Text("Playlist queue", "Liste de lecture"),
                                                     items, sQueueCount, sQueuePosition);
                    if (chosen >= 0)
                    {
                        sQueuePosition = chosen;
                        strcpy(selection, sQueue[chosen]);
                    }
                }
                if (menu == OptionsMenu::Return)
                {
                    sReturnRequested = true;
                    shouldExit = true;
                    break;
                }
                if (menu == OptionsMenu::Back)
                {
                    shouldExit = true;
                    backToBrowser = true;
                    break;
                }
                if (*selection)
                {
                    if (menu == OptionsMenu::Recent)
                        sQueueCount = 0;
                    loadAndStartVideo(selection);
                    break;
                }
                delete sPlayerController;
                sPlayerController = new PlayerController(&sPlayer);
                ApplyPreferences();
                sPlayerController->Initialize();
                sLoopEnabled = Preferences::values[Preferences::Repeat];
                sRandomMode = Preferences::values[Preferences::Shuffle];
                if (menu == OptionsMenu::Bookmark)
                    sPlayerController->ShowMessage(Preferences::Text("Bookmark saved", "Signet enregistre"),
                                                   "");
                if (menu == OptionsMenu::GoBookmark)
                {
                    LibraryState::Record *record = LibraryState::Find(sCurPath);
                    if (record)
                        sPlayerController->SeekFrame(record->bookmark);
                }
                if (wasPlaying)
                    sPlayerController->SetPaused(false);
                sOverlayTick = GetDebounceTicks() - 3272;
                UpdateOverlay();
                break;
            }
            case PlayerController::NAV_ACTION_EXIT:
                shouldExit = true;
                backToBrowser = canReturnToBrowser;
                break;

            default:
                break;
        }
    }

    DestroyCurrentPlayer();
    return sReturnRequested || (shouldExit && !backToBrowser);
}

static void InitDebounceTimer()
{
    TIMER0_DATA = 0;
    TIMER0_CR = TIMER_DIV_1024 | TIMER_ENABLE;
    TIMER1_DATA = 0;
    TIMER1_CR = TIMER_CASCADE | TIMER_ENABLE;
}

u32 GetDebounceTicks()
{
    return fv_ticks();
}

extern "C" int applicationMain(int argc, char** argv)
{
    DC_FlushAll();

    mpu_enableVramCache();
    irqEnable(IRQ_VBLANK);
    InitDebounceTimer();

    bool canUseWram = false;
    if (isDSiMode() && twr_isUnlocked())
    {
        twr_setBlockMapping(TWR_WRAM_BLOCK_A, 0x03000000, 0x40000, TWR_WRAM_BLOCK_IMAGE_SIZE_256K);
        twr_setBlockMapping(TWR_WRAM_BLOCK_B, 0x03100000, 0x40000, TWR_WRAM_BLOCK_IMAGE_SIZE_256K);
        twr_setBlockMapping(TWR_WRAM_BLOCK_C, 0x03140000, 0x40000, TWR_WRAM_BLOCK_IMAGE_SIZE_256K);
        mpu_enableTwlWramCache();
        canUseWram = true;
    }

    fifoSetValue32Handler(FIFO_USER_01, NULL, NULL);

    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_HANDSHAKE, 0));
    fv_waitReply(FIFO_USER_01);
    u32 handShake = fifoGetValue32(FIFO_USER_01);

    if (canUseWram && (handShake & IPC_CMD_ARG_MASK) == 0)
        canUseWram = false;

    sCanUseWram = canUseWram;

    if (!isDSiMode())
    {
        DC_FlushRange(gDldiStub, 16 * 1024);
        fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_SETUP_DLDI, (u32)gDldiStub));
        fv_waitReply(FIFO_USER_01);
        fifoGetValue32(FIFO_USER_01);
    }

    videoSetModeSub(MODE_0_2D);
    vramSetBankH(VRAM_H_SUB_BG);
    vramSetBankI(VRAM_I_SUB_SPRITE);

    consoleInit(NULL, 2, BgType_Text4bpp, BgSize_T_256x256, 2, 1, false, true);

    vramSetBankA(VRAM_A_LCD);
    vramSetBankB(VRAM_B_LCD);
    vramSetBankC(VRAM_C_LCD);
    vramSetBankD(VRAM_D_LCD);
    vramSetBankE(VRAM_E_LCD);

    for (int i = 0; i < 3 * 128 * 1024; i += 4)
        *(vu32*)((u32)VRAM_A + i) = 0x80008000;

    const char* filePath = NULL;
    if (argc >= 2)
        filePath = argv[1];
    sStandalone = true; // B always opens the browser; Return to launcher is explicit.
    Preferences::Load();
    LibraryState::Load();
    sLoopEnabled = Preferences::values[Preferences::Repeat];
    sRandomMode = Preferences::values[Preferences::Shuffle];

    strncpy(sBrowserDir, Preferences::videoRoot, sizeof(sBrowserDir) - 1);
    sBrowserDir[sizeof(sBrowserDir) - 1] = 0;

    if (filePath)
    {
        const char *ext = strrchr(filePath, '.');
        if (ext && (!strcasecmp(ext, ".m3u") || !strcasecmp(ext, ".m3u8")))
        {
            filePath = OpenPlaylist(filePath) ? sQueue[0] : NULL;
        }
    }
    bool quit = false;
    while (!quit)
    {
        if (filePath)
        {
            if (loadAndStartVideo(filePath))
                quit = RunPlayerLoop(sStandalone);
            else
                OptionsMenu::Notice(
                    Preferences::Text("Video could not be opened. Check the file and SD card.",
                                      "Impossible de lire cette video. Verifiez fichier et SD."));
            filePath = NULL;
        }
        else
        {
            if (!sStandalone)
            {
                quit = true;
                break;
            }
            if (RunBrowser(sBrowserPick, sizeof(sBrowserPick), sBrowserDir, sizeof(sBrowserDir),
                           sCurPath[0] ? GetFileName(sCurPath) : NULL))
                filePath = sBrowserPick;
            else
            {
                if (LoaderReturn::Run())
                    quit = true;
                else
                {
                    sReturnRequested = false;
                    quit = false;
                }
            }
        }
        if (sReturnRequested)
        {
            if (!LoaderReturn::Run())
            {
                sReturnRequested = false;
                quit = false;
            }
        }
    }

    DestroyCurrentPlayer();
    PlayerController::RestoreSubScreen();

    Preferences::Save();
    free(sSubtitleText);
    exit(0);
}
