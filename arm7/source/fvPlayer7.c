#include <nds.h>
#include <string.h>
#include <strings.h>
#include <nds/fifocommon.h>
#include "isdprint.h"
#include "../../common/ipc.h"
#include "fat.h"
#include "adpcm.h"
#include "irqWait.h"
#include "../../common/twlwram.h"
#include "fvPlayer7.h"
#include "../../common/fvValidation.h"
#include "../../common/fvAudioMath.h"

#define FV_AUDIO_START_OFFSET 12
#define FV_AUDIO_CH_LEFT 1
#define FV_AUDIO_CH_RIGHT 3

static fv_player7_t sPlayer;
extern volatile u32 gFrameCounter; // défini dans main.c, incrémenté à chaque VBlank

static void handleFindFile(u32 value);

void fv_init(void)
{
    memset(&sPlayer, 0, sizeof(sPlayer));
    // FIFO_USER_02 (next/prev/random/list_dir) n'est PAS enregistré via
    // fifoSetValue32Handler : comme FIFO_USER_01 plus bas, il est dépilé en
    // polling depuis fv_main(), pas depuis un callback appelé en contexte
    // IRQ. IPC_CMD_LIST_DIR peut être appelé en rafale (jusqu'à 60 fois de
    // suite par "ALEA ON ALL"), et chaque appel bloque le temps du listing
    // FatFs ; en callback ISR ça geler les IRQ (VBlank/audio/input) pendant
    // toute la boucle, en polling le VBlank/l'IRQ audio peuvent s'intercaler
    // normalement entre deux commandes.
}

static inline int getAudioTimerValue(int rate)
{
    return (16756991 + ((rate + 1) >> 1)) / rate;
}

static void audioFrameIrq(void);

#define PCM_SAMPLES (FV_AUDIO_RING_FRAMES * FV_AUDIO_FRAME_SAMPLES)
#define PCM_TARGET (FV_AUDIO_START_OFFSET * FV_AUDIO_FRAME_SAMPLES)
static volatile fv_audio_status_t *sAudioStatus;
static u32 sAudioBase, sUnderruns;
static volatile u32 sAudioFilled, sAudioBlocks;
static volatile u16 sLastAudioBlocks;
static bool sAudioPaused, sBuffering;
static volatile bool sAudioDrained;
static unsigned sVolume = 127;
static bool sMute, sMono;
static s16 sPcmLeft[FV_AUDIO_FRAME_SAMPLES], sPcmRight[FV_AUDIO_FRAME_SAMPLES];

static u32 audioPosition(void)
{
    if (!sPlayer.audioStarted) return 0;
    int irq = enterCriticalSection();
    u16 blocks, partial;
    do { blocks = TIMER_DATA(3); partial = TIMER_DATA(2) & 255; } while (blocks != TIMER_DATA(3));
    sAudioBlocks += (u16)(blocks - sLastAudioBlocks);
    sLastAudioBlocks = blocks;
    u32 result = sAudioBlocks * FV_AUDIO_FRAME_SAMPLES + partial;
    leaveCriticalSection(irq);
    return result;
}

static void audioFrameIrq(void)
{
    // Silence a drained ring even if foreground SD I/O is still blocked.
    u32 blocks = sAudioBlocks + (u16)(TIMER_DATA(3) - sLastAudioBlocks);
    if (blocks * FV_AUDIO_FRAME_SAMPLES >= sAudioFilled) {
        sAudioDrained = true;
        SCHANNEL_CR(FV_AUDIO_CH_LEFT) &= ~127u;
        SCHANNEL_CR(FV_AUDIO_CH_RIGHT) &= ~127u;
    }
}

static void publishAudio(void)
{
    if (!sAudioStatus) return;
    u32 position = audioPosition();
    if (position > sAudioFilled) position = sAudioFilled;
    sAudioStatus->sequence++;
    sAudioStatus->samples = sAudioBase + position;
    sAudioStatus->bufferedSamples = sAudioFilled - position;
    sAudioStatus->queuedBlocks = sPlayer.queueFrameCount;
    sAudioStatus->underruns = sUnderruns;
    sAudioStatus->flags = (sPlayer.audioStarted ? FV_AUDIO_RUNNING : 0) |
        (sAudioPaused ? FV_AUDIO_PAUSED : 0) | (sBuffering ? FV_AUDIO_BUFFERING : 0);
    sAudioStatus->sequence++;
}

static void stopChannels(void)
{
    TIMER_CR(0) = 0;
    SCHANNEL_CR(FV_AUDIO_CH_LEFT) = 0;
    SCHANNEL_CR(FV_AUDIO_CH_RIGHT) = 0;
    irqDisable(IRQ_TIMER2);
    TIMER_CR(1) = TIMER_CR(2) = TIMER_CR(3) = 0;
}

static void preserveAudio(void)
{
    if (!sPlayer.audioStarted) return;
    // Stop the sample clock before taking its position. No SD work in here.
    TIMER_CR(0) = 0;
    u32 consumed = audioPosition();
    stopChannels();
    sPlayer.audioStarted = false;
    if (consumed > sAudioFilled) consumed = sAudioFilled;
    fv_rotateSamples(&sPlayer.audioRingL[0][0], PCM_SAMPLES, consumed % PCM_SAMPLES);
    fv_rotateSamples(&sPlayer.audioRingR[0][0], PCM_SAMPLES, consumed % PCM_SAMPLES);
    sAudioFilled -= consumed;
    sAudioBase += consumed;
    memset(&sPlayer.audioRingL[0][0] + sAudioFilled, 0, (PCM_SAMPLES - sAudioFilled) * sizeof(s16));
    memset(&sPlayer.audioRingR[0][0] + sAudioFilled, 0, (PCM_SAMPLES - sAudioFilled) * sizeof(s16));
}

static void decodeAudioFrame(void)
{
    adpcm_decompress(sPlayer.audioQueueL[sPlayer.queueReadPtr], FV_AUDIO_FRAME_SIZE, sPcmLeft);
    adpcm_decompress(sPlayer.audioQueueR[sPlayer.queueReadPtr], FV_AUDIO_FRAME_SIZE, sPcmRight);
    unsigned volume = sMute ? 0 : sVolume;
    for (unsigned i = 0; i < FV_AUDIO_FRAME_SAMPLES; ++i) {
        int left = sPcmLeft[i], right = sPcmRight[i];
        if (sMono) left = right = (left + right) / 2;
        unsigned at = (sAudioFilled + i) % PCM_SAMPLES;
        (&sPlayer.audioRingL[0][0])[at] = left * (int)volume / 127;
        (&sPlayer.audioRingR[0][0])[at] = right * (int)volume / 127;
    }
    sAudioFilled += FV_AUDIO_FRAME_SAMPLES;
    sPlayer.queueReadPtr = (sPlayer.queueReadPtr + 1) % FV_AUDIO_QUEUE_FRAMES;
    --sPlayer.queueFrameCount;
}

static void startAudio(void)
{
    if (sPlayer.audioStarted || sAudioPaused) return;
    while (sAudioFilled < PCM_TARGET && sPlayer.queueFrameCount) decodeAudioFrame();
    if (!sAudioFilled) { sBuffering = true; publishAudio(); return; }
    sAudioBlocks = 0;
    sLastAudioBlocks = 0;
    sAudioDrained = false;
    int tmr = getAudioTimerValue(FV_AUDIO_RATE);
    SCHANNEL_SOURCE(FV_AUDIO_CH_LEFT) = (u32)sPlayer.audioRingL;
    SCHANNEL_REPEAT_POINT(FV_AUDIO_CH_LEFT) = 0;
    SCHANNEL_LENGTH(FV_AUDIO_CH_LEFT) = (PCM_SAMPLES * sizeof(s16)) >> 2;
    SCHANNEL_TIMER(FV_AUDIO_CH_LEFT) = -tmr;
    SCHANNEL_SOURCE(FV_AUDIO_CH_RIGHT) = (u32)sPlayer.audioRingR;
    SCHANNEL_REPEAT_POINT(FV_AUDIO_CH_RIGHT) = 0;
    SCHANNEL_LENGTH(FV_AUDIO_CH_RIGHT) = (PCM_SAMPLES * sizeof(s16)) >> 2;
    SCHANNEL_TIMER(FV_AUDIO_CH_RIGHT) = -tmr;
    TIMER_CR(0) = TIMER_CR(1) = TIMER_CR(2) = TIMER_CR(3) = 0;
    TIMER_DATA(0) = -2;
    TIMER_DATA(1) = -tmr;
    TIMER_DATA(2) = -256;
    TIMER_DATA(3) = 0;
    TIMER_CR(3) = TIMER_CASCADE | TIMER_ENABLE;
    TIMER_CR(2) = TIMER_CASCADE | TIMER_ENABLE | TIMER_IRQ_REQ;
    TIMER_CR(1) = TIMER_CASCADE | TIMER_ENABLE;
    irqSet(IRQ_TIMER2, audioFrameIrq);
    irqEnable(IRQ_TIMER2);
    SCHANNEL_CR(FV_AUDIO_CH_LEFT) = SCHANNEL_ENABLE | SOUND_VOL(127) | SOUND_PAN(0) | SOUND_FORMAT_16BIT | SOUND_REPEAT;
    SCHANNEL_CR(FV_AUDIO_CH_RIGHT) = SCHANNEL_ENABLE | SOUND_VOL(127) | SOUND_PAN(127) | SOUND_FORMAT_16BIT | SOUND_REPEAT;
    TIMER_CR(0) = TIMER_ENABLE;
    sPlayer.audioStarted = true;
    sBuffering = false;
    publishAudio();
}

static void stopAudio(void)
{
    stopChannels();
    sPlayer.audioStarted = false;
    sAudioBase = sAudioFilled = sAudioBlocks = 0;
    sAudioPaused = sBuffering = false;
    if (sPlayer.audioRingL) memset(sPlayer.audioRingL, 0, (PCM_SAMPLES * sizeof(s16)));
    if (sPlayer.audioRingR) memset(sPlayer.audioRingR, 0, (PCM_SAMPLES * sizeof(s16)));
    publishAudio();
}

static void updateAudio(void)
{
    if (sPlayer.audioStarted) {
        u32 position = audioPosition();
        if (sAudioDrained || position > sAudioFilled) {
            // Preserve only valid samples; never replay a stale ring on slow SD.
            preserveAudio();
            ++sUnderruns;
            sBuffering = true;
        } else {
            while (sAudioFilled - position < PCM_TARGET && sPlayer.queueFrameCount) decodeAudioFrame();
            // Clear the next block after the valid tail. It is always outside
            // the queued region, so an unexpected stall produces silence.
            unsigned tail = sAudioFilled % PCM_SAMPLES;
            for (unsigned i = 0; i < FV_AUDIO_FRAME_SAMPLES; ++i) {
                (&sPlayer.audioRingL[0][0])[(tail + i) % PCM_SAMPLES] = 0;
                (&sPlayer.audioRingR[0][0])[(tail + i) % PCM_SAMPLES] = 0;
            }
        }
    }
    if (sBuffering && !sAudioPaused && sPlayer.queueFrameCount >= 2) startAudio();
    publishAudio();
}

static void gotoKeyFrameDirect(const fv_keyframe_t *keyFrameData)
{
    if (f_lseek(&sPlayer.file, keyFrameData->offset) != FR_OK) sPlayer.fileError = true;
    sPlayer.nextFrame = keyFrameData->frame;
    stopAudio();
    sPlayer.ringPos = 0;
    sPlayer.queueReadPtr = 0;
    sPlayer.queueWritePtr = 0;
    sPlayer.queueFrameCount = 0;
    sPlayer.audioFramesNeeded = 0;
    sPlayer.audioFramesProvided = 0;
    memset(&sPlayer.audioQueueL[0][0], 0, FV_AUDIO_QUEUE_FRAMES * FV_AUDIO_FRAME_SIZE);
    memset(&sPlayer.audioQueueR[0][0], 0, FV_AUDIO_QUEUE_FRAMES * FV_AUDIO_FRAME_SIZE);
}

static void readKeyFrame(u32 index, fv_keyframe_t *out)
{
    UINT br = 0;
    memset(out, 0, sizeof(*out));
    if (index >= sPlayer.nrKeyFrames ||
        f_lseek(&sPlayer.file, sizeof(fv_header_t) + sizeof(fv_keyframe_t) * index) != FR_OK ||
        f_read(&sPlayer.file, out, sizeof(*out), &br) != FR_OK || br != sizeof(*out) ||
        out->frame >= sPlayer.header.nrFrames ||
        out->offset < sizeof(fv_header_t) + (uint64_t)sPlayer.nrKeyFrames * sizeof(*out) ||
        (uint64_t)out->offset + 4 > f_size(&sPlayer.file)) sPlayer.fileError = true;
}

static u32 gotoKeyFrame(u32 keyFrame)
{
    fv_keyframe_t keyFrameData;
    if (keyFrame >= sPlayer.nrKeyFrames)
        keyFrame = sPlayer.nrKeyFrames - 1;
    readKeyFrame(keyFrame, &keyFrameData);
    gotoKeyFrameDirect(&keyFrameData);
    return keyFrameData.frame;
}

static u32 gotoNearestKeyFrame(u32 frame, u32 *resultFrame)
{
    if (sPlayer.nrKeyFrames == 0) {
        if (resultFrame) *resultFrame = 0;
        return 0;
    }
    u32 lo = 0;
    u32 hi = sPlayer.nrKeyFrames;
    u32 best = 0;
    fv_keyframe_t kf;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        readKeyFrame(mid, &kf);
        if (kf.frame <= frame) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    readKeyFrame(best, &kf);
    gotoKeyFrameDirect(&kf);
    if (resultFrame)
        *resultFrame = kf.frame;
    return best;
}

static void rememberCurPath(const char *path)
{
    const char *lastSlash = strrchr(path, '/');
    if (lastSlash) {
        int dirLen = lastSlash - path;
        if (dirLen >= (int)sizeof(sPlayer.curDir))
            dirLen = sizeof(sPlayer.curDir) - 1;
        memcpy(sPlayer.curDir, path, dirLen);
        sPlayer.curDir[dirLen] = 0;
        strncpy(sPlayer.curName, lastSlash + 1, sizeof(sPlayer.curName) - 1);
        sPlayer.curName[sizeof(sPlayer.curName) - 1] = 0;
    } else {
        sPlayer.curDir[0] = 0;
        strncpy(sPlayer.curName, path, sizeof(sPlayer.curName) - 1);
        sPlayer.curName[sizeof(sPlayer.curName) - 1] = 0;
    }
}

static bool hasFvExtension(const char *name)
{
    int len = strlen(name);
    return len > 3 && strcasecmp(name + len - 3, ".fv") == 0;
}

// Lists a directory for the standalone browser: fills req->entries with up
// to req->maxEntries entries (directories and .fv files only, dotfiles
// skipped). Returns 1 if the directory could be opened, 0 otherwise.
// Sorting is done on the arm9 side (qsort), the arm7 only fills the buffer.
static u32 listDirInto(fv_listdir_req_t* req)
{
    DIR dir;
    FILINFO info;

    req->count = 0;
    req->total = 0;

    if (f_opendir(&dir, req->path) != FR_OK)
        return 0;

    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0)
    {
        int isDir = (info.fattrib & AM_DIR) != 0;
        if (info.fname[0] == '.')
            continue;
        if (!isDir && !hasFvExtension(info.fname))
            continue;

        req->total++;
        if (req->count < req->maxEntries)
        {
            strncpy(req->entries[req->count].name, info.fname, FV_BROWSER_NAME_LEN - 1);
            req->entries[req->count].name[FV_BROWSER_NAME_LEN - 1] = 0;
            req->entries[req->count].isDir = isDir ? 1 : 0;
            req->count++;
        }
    }
    f_closedir(&dir);
    return 1;
}

static void joinCurDirAndName(const char *name, char *outPath)
{
    if (sPlayer.curDir[0]) {
        size_t dirLen = strlen(sPlayer.curDir);
        if (dirLen > FV_MAX_PATH_LEN - 2)
            dirLen = FV_MAX_PATH_LEN - 2;
        memcpy(outPath, sPlayer.curDir, dirLen);
        outPath[dirLen] = '/';
        size_t avail = FV_MAX_PATH_LEN - dirLen - 2;
        size_t nameLen = strlen(name);
        if (nameLen > avail)
            nameLen = avail;
        memcpy(outPath + dirLen + 1, name, nameLen);
        outPath[dirLen + 1 + nameLen] = 0;
    } else {
        strncpy(outPath, name, FV_MAX_PATH_LEN - 1);
        outPath[FV_MAX_PATH_LEN - 1] = 0;
    }
}

static bool findAdjacentFvFile(int direction, char *outPath)
{
    DIR dir;
    FILINFO info;
    static char best[FV_MAX_PATH_LEN];
    static char edge[FV_MAX_PATH_LEN];
    bool haveBest = false;
    bool haveEdge = false;
    const char *dirPath = sPlayer.curDir[0] ? sPlayer.curDir : ".";
    if (f_opendir(&dir, dirPath) != FR_OK)
        return false;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0) {
        if (info.fattrib & AM_DIR)
            continue;
        if (!hasFvExtension(info.fname))
            continue;
        int cmpToCur = strcasecmp(info.fname, sPlayer.curName);
        if (direction > 0) {
            if (cmpToCur > 0 && (!haveBest || strcasecmp(info.fname, best) < 0)) {
                strncpy(best, info.fname, sizeof(best) - 1);
                best[sizeof(best) - 1] = 0;
                haveBest = true;
            }
            if (!haveEdge || strcasecmp(info.fname, edge) < 0) {
                strncpy(edge, info.fname, sizeof(edge) - 1);
                edge[sizeof(edge) - 1] = 0;
                haveEdge = true;
            }
        } else {
            if (cmpToCur < 0 && (!haveBest || strcasecmp(info.fname, best) > 0)) {
                strncpy(best, info.fname, sizeof(best) - 1);
                best[sizeof(best) - 1] = 0;
                haveBest = true;
            }
            if (!haveEdge || strcasecmp(info.fname, edge) > 0) {
                strncpy(edge, info.fname, sizeof(edge) - 1);
                edge[sizeof(edge) - 1] = 0;
                haveEdge = true;
            }
        }
    }
    f_closedir(&dir);
    const char *chosen = haveBest ? best : (haveEdge ? edge : NULL);
    if (!chosen || strcasecmp(chosen, sPlayer.curName) == 0)
        return false;
    joinCurDirAndName(chosen, outPath);
    return true;
}

static u32 hash32(u32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// NOUVEAU : Construit le "bag" d'indices mélangés
static void buildShuffleBag(void)
{
    DIR dir;
    FILINFO info;
    const char *dirPath = sPlayer.curDir[0] ? sPlayer.curDir : ".";
    
    // Passage 1 : compter les fichiers éligibles (hors vidéo en cours)
    if (f_opendir(&dir, dirPath) != FR_OK) {
        sPlayer.shuffleRemaining = 0;
        return;
    }
    
    u32 count = 0;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0) {
        if (info.fattrib & AM_DIR) continue;
        if (!hasFvExtension(info.fname)) continue;
        // EXCLUSION CRITIQUE : empêche la dernière vidéo lue d'être dans le nouveau bag
        if (strcasecmp(info.fname, sPlayer.curName) == 0) continue;
        count++;
    }
    f_closedir(&dir);
    
    if (count == 0) {
        sPlayer.shuffleCount = 0;
        sPlayer.shuffleRemaining = 0;
        return;
    }
    
    sPlayer.shuffleCount = (count > MAX_SHUFFLE_FILES) ? MAX_SHUFFLE_FILES : (u16)count;
    
    // Passage 2 : initialiser les indices et mélanger (Fisher-Yates)
    for (u16 i = 0; i < sPlayer.shuffleCount; i++) {
        sPlayer.shuffleIndices[i] = i;
    }
    
    for (u16 i = sPlayer.shuffleCount - 1; i > 0; i--) {
        u32 seed = hash32(gFrameCounter + i + sPlayer.shuffleCount);
        u16 j = seed % (i + 1);
        u16 temp = sPlayer.shuffleIndices[i];
        sPlayer.shuffleIndices[i] = sPlayer.shuffleIndices[j];
        sPlayer.shuffleIndices[j] = temp;
    }
    
    sPlayer.shuffleRemaining = sPlayer.shuffleCount;
    strncpy(sPlayer.shuffleDir, sPlayer.curDir, FV_MAX_PATH_LEN - 1);
    sPlayer.shuffleDir[FV_MAX_PATH_LEN - 1] = 0;
    // Fige le nom exclu utilisé pour compter/indexer ci-dessus : curName va
    // changer à chaque tirage suivant, mais l'énumération du dossier doit
    // rester identique pendant toute la durée de vie de ce bag
    strncpy(sPlayer.shuffleExcludedName, sPlayer.curName, FV_MAX_PATH_LEN - 1);
    sPlayer.shuffleExcludedName[FV_MAX_PATH_LEN - 1] = 0;
}

// MODIFIÉ : Utilise le bag au lieu d'un tirage avec remise
static bool findRandomFvFile(char *outPath)
{
    // Reconstruire le bag si le dossier a changé ou s'il est épuisé
    if (sPlayer.shuffleRemaining == 0 || strcmp(sPlayer.shuffleDir, sPlayer.curDir) != 0) {
        buildShuffleBag();
    }
    
    if (sPlayer.shuffleRemaining == 0) {
        return false;
    }
    
    // Piocher l'indice cible dans le bag mélangé (de la fin vers le début)
    u16 targetIdx = sPlayer.shuffleIndices[sPlayer.shuffleRemaining - 1];
    sPlayer.shuffleRemaining--;
    
    // Passage 3 : retrouver le fichier correspondant à l'indice cible
    DIR dir;
    FILINFO info;
    const char *dirPath = sPlayer.curDir[0] ? sPlayer.curDir : ".";
    
    if (f_opendir(&dir, dirPath) != FR_OK) return false;
    
    bool found = false;
    u16 currentIdx = 0;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0) {
        if (info.fattrib & AM_DIR) continue;
        if (!hasFvExtension(info.fname)) continue;
        if (strcasecmp(info.fname, sPlayer.shuffleExcludedName) == 0) continue;
        
        if (currentIdx == targetIdx) {
            joinCurDirAndName(info.fname, outPath);
            found = true;
            break;
        }
        currentIdx++;
    }
    f_closedir(&dir);
    
    return found;
}

static void handleFindFile(u32 value)
{
    switch (value >> IPC_CMD_CMD_SHIFT) {
        case IPC_CMD_FIND_NEXT_FILE: {
            char *outPath = (char *)(value & IPC_CMD_ARG_MASK);
            bool found = findAdjacentFvFile(1, outPath);
            fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_FIND_NEXT_FILE, found ? 1 : 0));
            break;
        }
        case IPC_CMD_FIND_PREV_FILE: {
            char *outPath = (char *)(value & IPC_CMD_ARG_MASK);
            bool found = findAdjacentFvFile(-1, outPath);
            fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_FIND_PREV_FILE, found ? 1 : 0));
            break;
        }
        case IPC_CMD_FIND_RANDOM_FILE: {
            char *outPath = (char *)(value & IPC_CMD_ARG_MASK);
            bool found = findRandomFvFile(outPath);
            fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_FIND_RANDOM_FILE, found ? 1 : 0));
            break;
        }
        case IPC_CMD_LIST_DIR:
        {
            fv_listdir_req_t* req = (fv_listdir_req_t*)(value & IPC_CMD_ARG_MASK);
            u32 ok = listDirInto(req);
            fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_LIST_DIR, ok));
            break;
        }
    }
}

static void handleFifo(u32 value)
{
    UINT br;
    switch (value >> IPC_CMD_CMD_SHIFT) {
        case IPC_CMD_READ_FRAME: {
            u32 len = 0, reply = IPC_FRAME_ERROR;
            FSIZE_t position = f_tell(&sPlayer.file);
            if (sPlayer.fileError) goto frameReply;
            if (sPlayer.nextFrame == sPlayer.header.nrFrames) { reply = 0; goto frameReply; }
            if (f_read(&sPlayer.file, &len, 4, &br) != FR_OK || br != 4 ||
                !fv_validRecord(len, f_size(&sPlayer.file) - f_tell(&sPlayer.file))) goto frameError;
            unsigned audioFrames = len >> 17;
            if (audioFrames > FV_AUDIO_QUEUE_FRAMES - sPlayer.queueFrameCount) {
                if (f_lseek(&sPlayer.file, position) != FR_OK) goto frameError;
                reply = IPC_FRAME_RETRY;
                goto frameReply;
            }
            u8 *video = (u8 *)(value & IPC_CMD_ARG_MASK);
            unsigned bytes = len & 0x1ffff;
            if (f_read(&sPlayer.file, video, bytes, &br) != FR_OK || br != bytes) goto frameError;
            memset(video + bytes, 0, 4); // decoder word lookahead
            for (unsigned i = 0; i < audioFrames; ++i) {
                u8 *left = sPlayer.audioQueueL[sPlayer.queueWritePtr];
                u8 *right = sPlayer.audioQueueR[sPlayer.queueWritePtr];
                if (f_read(&sPlayer.file, left, FV_AUDIO_FRAME_SIZE, &br) != FR_OK || br != FV_AUDIO_FRAME_SIZE ||
                    f_read(&sPlayer.file, right, FV_AUDIO_FRAME_SIZE, &br) != FR_OK || br != FV_AUDIO_FRAME_SIZE ||
                    (left[2] | left[3] << 8) > 88 || (right[2] | right[3] << 8) > 88) goto frameError;
                sPlayer.queueWritePtr = (sPlayer.queueWritePtr + 1) % FV_AUDIO_QUEUE_FRAMES;
                sPlayer.queueFrameCount++;
                updateAudio();
            }
            sPlayer.nextFrame++;
            reply = bytes;
            goto frameReply;
frameError:
            sPlayer.fileError = true;
            stopAudio();
frameReply:
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_READ_FRAME, reply));
            break;
        }
        case IPC_CMD_OPEN_FILE: {
            const char *path = (const char *)(value & IPC_CMD_ARG_MASK);
            stopAudio();
            sPlayer.fileError = false;
            f_close(&sPlayer.file);
            FRESULT result = f_open(&sPlayer.file, path, FA_OPEN_EXISTING | FA_READ);
            if (result != FR_OK)
                fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_OPEN_FILE, 0));
            else {
                rememberCurPath(path);
                fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_OPEN_FILE, 1));
            }
            break;
        }
        case IPC_CMD_READ_HEADER: {
            fv_header_t *header = (fv_header_t *)(value & IPC_CMD_ARG_MASK);
            bool valid = f_read(&sPlayer.file, header, sizeof(*header), &br) == FR_OK &&
                br == sizeof(*header) && fv_validHeader(header, f_size(&sPlayer.file));
            fv_keyframe_t previous = {0}, key;
            for (u32 i = 0; valid && i < header->nrKeyFrames; ++i) {
                valid = f_read(&sPlayer.file, &key, sizeof(key), &br) == FR_OK && br == sizeof(key) &&
                    fv_validKey(header, &key, i ? &previous : NULL, f_size(&sPlayer.file));
                previous = key;
            }
            if (!valid) {
                sPlayer.fileError = true;
                memset(header, 0, sizeof(*header));
                fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_READ_HEADER, 0));
                break;
            }
            sPlayer.header = *header;
            sPlayer.nrKeyFrames = header->nrKeyFrames;
            gotoKeyFrame(0);
            u32 num = header->fpsNum;
            u32 den = header->fpsDen;
            int vblankCount = 1;
            while (num * (vblankCount + 1) / den < 62)
                vblankCount++;
            fpsa_stop(&sPlayer.fpsa);
            if (num * vblankCount / den < 62) {
                fpsa_init(&sPlayer.fpsa);
                fpsa_setTargetFpsFraction(&sPlayer.fpsa, num * vblankCount, den);
                sPlayer.fpsa.targetCycles = (double)sPlayer.fpsa.targetCycles * getAudioTimerValue(FV_AUDIO_RATE) * FV_AUDIO_RATE / 16756991.0;
                fpsa_start(&sPlayer.fpsa);
            }
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_READ_HEADER, vblankCount));
            break;
        }
        case IPC_CMD_GOTO_KEYFRAME: {
            u32 frame = gotoKeyFrame(value & IPC_CMD_ARG_MASK);
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_GOTO_KEYFRAME, sPlayer.fileError ? IPC_FRAME_ERROR : frame));
            break;
        }
        case IPC_CMD_GOTO_NEAREST_KEYFRAME: {
            u32 frame = value & IPC_CMD_ARG_MASK;
            u32 resultFrame;
            u32 keyFrame = gotoNearestKeyFrame(frame, &resultFrame);
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_GOTO_NEAREST_KEYFRAME, keyFrame));
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_GOTO_KEYFRAME, sPlayer.fileError ? IPC_FRAME_ERROR : resultFrame));
            break;
        }
        case IPC_CMD_AUDIO_BUFFERS: {
            fv_audio_buffers_t *buffers = (fv_audio_buffers_t *)(value & IPC_CMD_ARG_MASK);
            sPlayer.audioQueueL = buffers->queueLeft;
            sPlayer.audioQueueR = buffers->queueRight;
            sPlayer.audioRingL = buffers->ringLeft;
            sPlayer.audioRingR = buffers->ringRight;
            break;
        }
        case IPC_CMD_AUDIO_STATUS:
            sAudioStatus = (fv_audio_status_t *)(value & IPC_CMD_ARG_MASK);
            publishAudio();
            break;
        case IPC_CMD_AUDIO_OPTIONS: {
            u32 options = value & IPC_CMD_ARG_MASK;
            sVolume = options & 127;
            sMute = (options & 128) != 0;
            sMono = (options & 256) != 0;
            break;
        }
        case IPC_CMD_CONTROL_AUDIO: {
            if ((value & IPC_CMD_ARG_MASK) == IPC_ARG_CONTROL_AUDIO_START)
                startAudio();
            else if ((value & IPC_CMD_ARG_MASK) == IPC_ARG_CONTROL_AUDIO_STOP)
                stopAudio();
            else if ((value & IPC_CMD_ARG_MASK) == IPC_ARG_CONTROL_AUDIO_PAUSE) {
                preserveAudio();
                sAudioPaused = true;
                publishAudio();
            } else if ((value & IPC_CMD_ARG_MASK) == IPC_ARG_CONTROL_AUDIO_RESUME) {
                sAudioPaused = false;
                startAudio();
            } else if ((value & IPC_CMD_ARG_MASK) == IPC_ARG_CONTROL_AUDIO_ARM) {
                sAudioPaused = false;
            } else if ((value & IPC_CMD_ARG_MASK) == IPC_ARG_CONTROL_AUDIO_STOP_CLEAR) {
                stopAudio();
                sPlayer.ringPos = 0;
                sPlayer.queueReadPtr = 0;
                sPlayer.queueWritePtr = 0;
                sPlayer.queueFrameCount = 0;
                sPlayer.audioFramesNeeded = 0;
                sPlayer.audioFramesProvided = 0;
                memset(&sPlayer.audioQueueL[0][0], 0, FV_AUDIO_QUEUE_FRAMES * FV_AUDIO_FRAME_SIZE);
                memset(&sPlayer.audioQueueR[0][0], 0, FV_AUDIO_QUEUE_FRAMES * FV_AUDIO_FRAME_SIZE);
            }
            break;
        }
        case IPC_CMD_SETUP_DLDI: {
            if (!isDSiMode()) {
                memcpy((void *)0x037F8000, (void *)(value & IPC_CMD_ARG_MASK), 16 * 1024);
                fat_mountDldi();
            }
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_SETUP_DLDI, 0));
            break;
        }
        case IPC_CMD_HANDSHAKE: {
            bool canUseWram = false;
            if (isDSiMode())
                canUseWram = twr_isUnlocked();
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_HANDSHAKE, canUseWram ? 1 : 0));
            break;
        }
    }
}

void fv_main(void)
{
    if (!fifoCheckValue32(FIFO_USER_01) && !fifoCheckValue32(FIFO_USER_02))
        irq_wait(false, IRQ_TIMER2 | IRQ_FIFO_NOT_EMPTY);
    updateAudio();
    if (fifoCheckValue32(FIFO_USER_01))
        handleFifo(fifoGetValue32(FIFO_USER_01));
    if (fifoCheckValue32(FIFO_USER_02))
        handleFindFile(fifoGetValue32(FIFO_USER_02));
}
