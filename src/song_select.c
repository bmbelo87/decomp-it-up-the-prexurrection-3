#include "pumpy.h"
#include <string.h>

static int prevSongId = -1;
static int previewState = 0;
static int selectedState = 0;
static int g_cdTexIds[45];
bool g_cdLoaded = false;
static float g_songAnimPos = 0.0f;
static int g_songAnimCounter = 0;

static int g_carrosselFrame = 588;
static int g_carrosselDir = 0;
static int g_carrosselTarget = 588;
static bool g_carrosselIntro = true;
static int g_introFrame = 0;
static int g_pendingMove = 0;
static float g_previewDelay = 0.0f;

static const int g_slotFrameOffset[7] = { -48, -32, -16, 0, +16, +32, +48 };

// Modos 1 jogador: NORMAL(0), HARD(1), CRAZY(2), HALFDOUBLE(3), DOUBLE(4), NIGHTMARE(5)
// TODO: quando P2 estiver ativo, substituir HALFDOUBLE/DOUBLE/NIGHTMARE por BATTLE
static const char* g_modeNames1P[6] = {"NORMAL","HARD","CRAZY","HALFDOUBLE","DOUBLE","NIGHTMARE"};
static const int g_modeLayers1P[6] = {31, 32, 30, 27, 28, 8};
static int g_modeDBIdx[6];   // indices correspondentes no SongDB
static int g_modeTileIdx[6]; // indices dos primeiros tiles SPR de cada modo

static int g_selDispIdx = 0;          // indice de exibicao atual (0-5)
static bool g_modeAnimActive = false;
static int g_modeAnimFrame = 0;
static int g_modeAnimDir = 0;         // +1=UR, -1=UL
#define MODE_ANIM_DURATION 15
#define MODE_LEFT_X   107.0f
#define MODE_CENTER_X 315.0f
#define MODE_RIGHT_X  537.0f
#define MODE_Y         90.0f

typedef struct {
    int frame;
    float x, sx, alpha;
} BoxKeyframe;

static const BoxKeyframe g_boxKF[7] = {
    {540, -235.0f, 0.50f, 0.0f},
    {556, -175.0f, 0.65f, 0.6f},
    {572, -110.0f, 0.80f, 1.0f},
    {588,    0.0f, 1.00f, 1.0f},
    {604,  110.0f, 0.80f, 1.0f},
    {620,  175.0f, 0.65f, 0.6f},
    {636,  235.0f, 0.50f, 0.0f},
};

static void EvalBoxFrame(float frame, float* outX, float* outSc, float* outAlpha) {
    if (frame <= 540.0f) { *outX = -235.0f; *outSc = 0.50f; *outAlpha = 0.0f; return; }
    if (frame >= 636.0f) { *outX =  235.0f; *outSc = 0.50f; *outAlpha = 0.0f; return; }
    for (int i = 0; i < 6; i++) {
        if (frame >= g_boxKF[i].frame && frame <= g_boxKF[i+1].frame) {
            float t = (frame - g_boxKF[i].frame) / (float)(g_boxKF[i+1].frame - g_boxKF[i].frame);
            *outX     = g_boxKF[i].x     + t * (g_boxKF[i+1].x     - g_boxKF[i].x);
            *outSc    = g_boxKF[i].sx    + t * (g_boxKF[i+1].sx    - g_boxKF[i].sx);
            *outAlpha = g_boxKF[i].alpha + t * (g_boxKF[i+1].alpha  - g_boxKF[i].alpha);
            return;
        }
    }
}

static int g_cdSongs[45][2] = {
    {108,109},{112,205},{212,301},{312,318},{320,321},
    {401,402},{403,404},{405,413},{503,505},{508,516},
    {701,703},{704,705},{707,711},{712,714},{719,720},
    {721,722},{730,733},{734,735},{736,801},{802,803},
    {804,805},{806,807},{808,809},{810,811},{812,813},
    {814,815},{816,817},{818,819},{820,911},{916,921},
    {101,104},{202,203},{204,212},{302,303},{305,306},
    {310,311},{312,414},{501,504},{507,517},{902,906},
    {913,915},{922,102},{821,822},{823,824},{825,826}
};

static int findCdForSong(int songId, int* outHalf) {
    for (int i = 0; i < 45; i++) {
        if (g_cdSongs[i][0] == songId) { *outHalf = 0; return i; }
        if (g_cdSongs[i][1] == songId) { *outHalf = 1; return i; }
    }
    return -1;
}

static void cacheModeTileIndices(void) {
    if (g_game.bgaPicCount <= 0) return;
    BGAPicture* pic = &g_game.bgaPics[0];
    for (int m = 0; m < 6; m++) {
        int layer = g_modeLayers1P[m];
        if (layer >= 0 && layer < pic->layerCount) {
            g_modeTileIdx[m] = pic->layers[layer].sprTileStart;
        } else {
            g_modeTileIdx[m] = -1;
        }
    }
}

static void loadCdTextures(void) {
    if (g_cdLoaded) return;
    char datPath[MAX_PATH];
    snprintf(datPath, sizeof(datPath), "%s\\BGA\\90.DAT", g_game.currentDirectory);
    if (!RES_Open(datPath)) { g_cdLoaded = true; return; }
    for (int i = 0; i < 45; i++) {
        char name[32];
        snprintf(name, sizeof(name), "CD%02d.png", i + 1);
        g_cdTexIds[i] = loadTextureFromRES(name);
    }
    RES_Close();
    g_cdLoaded = true;
}

void SongSelect_Reset(void) {
    prevSongId = -1;
    previewState = 0;
    selectedState = 0;
    g_game.selectedSongIndex = 0;
    g_game.songSelectHighlighted = 0;
    g_game.previewSongId = -1;

    // Inicializa indices dos modos no DB
    for (int m = 0; m < 6; m++) {
        g_modeDBIdx[m] = Song_FindMode(&g_game.songDB, g_modeNames1P[m]);
        if (g_modeDBIdx[m] < 0) g_modeDBIdx[m] = 0;
    }
    g_selDispIdx = 0;
    g_game.selectedModeIndex = g_modeDBIdx[0];
    g_modeAnimActive = false;
    g_modeAnimFrame = 0;
    g_modeAnimDir = 0;

    g_songAnimCounter = 0;
    g_songAnimPos = 0.0f;
    g_carrosselFrame = 588;
    g_carrosselDir = 0;
    g_carrosselTarget = 588;
    g_carrosselIntro = true;
    g_introFrame = 0;
    g_previewDelay = 0.0f;
    loadCdTextures();
    cacheModeTileIndices();
}

void SongSelect_ResetIntro(void) {
    prevSongId = -1;
    previewState = 0;
    selectedState = 0;
    g_songAnimCounter = 0;
    g_songAnimPos = 0.0f;
    g_carrosselFrame = 588;
    g_carrosselDir = 0;
    g_carrosselTarget = 588;
    g_carrosselIntro = true;
    g_introFrame = 0;
    g_previewDelay = 0.0f;
    loadCdTextures();
    cacheModeTileIndices();
}

static void stopPreview(void) {
    if (previewState) {
        BGM_Stop();
        previewState = 0;
    }
}

static void playPreview(int songId) {
    stopPreview();
    if (songId < 0) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\AUDIO\\D%d.AUD", g_game.currentDirectory, songId);
    if (BGM_LoadAUDDirect(path)) {
        BGM_Play(false);
        previewState = 1;
    }
    prevSongId = songId;
}

void Gamestate_UpdateSongSelect(float dt) {
    if (g_game.state == STATE_SONG_SELECT_B) {
        float a = g_game.globalColorA - dt;
        if (a <= 0.0f) {
            Render_SetGlobalColor(0, 0, 0, 0);
            if (g_game.stateFrame > 60) {
                g_game.state = STATE_SONG_SELECT;
                g_game.stateFrame = 0;
                previewState = 0;
                prevSongId = -1;
                selectedState = 0;
            }
        } else {
            Render_SetGlobalColor(0, 0, 0, a);
        }
        return;
    }

    SongDB* db = &g_game.songDB;

    if (Input_IsPadHit(0, PAD_UR) && !g_modeAnimActive) {
        loadCdTextures();
        cacheModeTileIndices();
        Audio_Play(g_waveSoundIds[SND_3_2], false);
        selectedState = 0;
        stopPreview();
        g_modeAnimActive = true;
        g_modeAnimFrame = 0;
        g_modeAnimDir = 1;
        g_game.selectedSongIndex = 0;
        prevSongId = -1;
        g_songAnimCounter = 0;
        g_carrosselIntro = true;
        g_introFrame = 0;
        g_carrosselFrame = 588;
        g_carrosselDir = 0;
        g_carrosselTarget = 588;
    }

    if (Input_IsPadHit(0, PAD_UL) && !g_modeAnimActive) {
        loadCdTextures();
        cacheModeTileIndices();
        Audio_Play(g_waveSoundIds[SND_3_2], false);
        selectedState = 0;
        stopPreview();
        g_modeAnimActive = true;
        g_modeAnimFrame = 0;
        g_modeAnimDir = -1;
        g_game.selectedSongIndex = 0;
        prevSongId = -1;
        g_songAnimCounter = 0;
        g_carrosselIntro = true;
        g_introFrame = 0;
        g_carrosselFrame = 588;
        g_carrosselDir = 0;
        g_carrosselTarget = 588;
    }

    // Atualiza animacao dos modos
    if (g_modeAnimActive) {
        g_modeAnimFrame++;
        if (g_modeAnimFrame >= MODE_ANIM_DURATION) {
            g_modeAnimActive = false;
            g_selDispIdx = (g_selDispIdx + g_modeAnimDir + 6) % 6;
            g_game.selectedModeIndex = g_modeDBIdx[g_selDispIdx];
        }
    }

    SongMode* mode = &db->modes[g_game.selectedModeIndex];
    int songCount = mode->songCount;
    if (songCount == 0) return;

    if (g_carrosselIntro) {
        g_introFrame++;

        if (Input_IsPadHit(0, PAD_DR) || Input_IsPadHit(0, PAD_DL)) {
            Audio_Play(g_waveSoundIds[SND_3_2], false);
            g_carrosselIntro = false;
        }

        if (g_introFrame >= 50) {
            g_carrosselIntro = false;
        }
        return;
    }

    if (Input_IsPadHit(0, PAD_DR)) {
        Audio_Play(g_waveSoundIds[SND_3_2], false);
        selectedState = 0;
        stopPreview();
        prevSongId = -1;
        // Se já animando, ignore input para evitar acúmulo
        if (g_carrosselDir == 0) {
            g_pendingMove = +1;
            g_carrosselTarget = g_carrosselFrame - 16;
            g_carrosselDir = -1;
            g_previewDelay = 1.0f;
        }
    }

    if (Input_IsPadHit(0, PAD_DL)) {
        Audio_Play(g_waveSoundIds[SND_3_2], false);
        selectedState = 0;
        stopPreview();
        prevSongId = -1;
        if (g_carrosselDir == 0) {
            g_pendingMove = -1;
            g_carrosselTarget = g_carrosselFrame + 16;
            g_carrosselDir = +1;
            g_previewDelay = 1.0f;
        }
    }

    // Avanca o frame do carrossel — velocidade 1 = mais lento
    if (g_carrosselDir != 0) {
        g_carrosselFrame += g_carrosselDir * 1;

        if ((g_carrosselDir == +1 && g_carrosselFrame >= g_carrosselTarget) ||
            (g_carrosselDir == -1 && g_carrosselFrame <= g_carrosselTarget)) {
            // Aplicar movimento pendente ao índice só quando a animação terminar
            if (g_pendingMove != 0) {
                g_game.selectedSongIndex += g_pendingMove;
                if (g_game.selectedSongIndex >= songCount) g_game.selectedSongIndex = 0;
                if (g_game.selectedSongIndex < 0) g_game.selectedSongIndex = songCount - 1;
                g_pendingMove = 0;
            }
            // Reset base frame para centro
            g_carrosselFrame = 588;
            g_carrosselTarget = 588;
            g_carrosselDir = 0;
        }
    }

    int songId = mode->songIds[g_game.selectedSongIndex];
    if (g_previewDelay > 0.0f) {
        g_previewDelay -= dt;
        if (g_previewDelay <= 0.0f) {
            if (songId != prevSongId) {
                playPreview(songId);
            }
        }
    } else {
        if (songId != prevSongId)
            playPreview(songId);
    }

    if (Input_IsPadHit(0, PAD_C)) {
        if (!selectedState)
            Audio_Play(g_waveSoundIds[SND_3_2], false);
        if (selectedState) {
            Audio_Play(g_waveSoundIds[SND_4_2], false);
            stopPreview();
            g_game.selectedDifficulty = mode->difficulties[g_game.selectedSongIndex];
            selectedState = 0;
            Loading_Enter(songId);
        } else {
            selectedState = 1;
        }
    }
}

// Frames dos keyframes do BGA para cada posicao (extraidos do 099.DAT)
#define FRAME_LEFT    14   // Event 1: x=0,   y=0,   hx=0,   sx=1.0
#define FRAME_CENTER  74   // Event 3: x=210, y=35,  hx=110, sx=1.3
#define FRAME_RIGHT   134  // Event 5: x=430, y=0,   hx=110, sx=1.0
#define FRAME_OFF     194  // Event 7: x=210, y=-130, hx=110, sx=1.0

void Gamestate_RenderSongSelect(void) {
    if (g_game.state != STATE_SONG_SELECT && g_game.state != STATE_SONG_SELECT_B)
        return;
    if (g_game.state != STATE_SONG_SELECT) return;

    if (g_game.bgaPicCount > 0) {
        int f = g_game.bgaFrame;
        BGA_SetEventLayer(0, f % 60, 0);

        BGA_SetEventLayer(0, 0, 0x1a);
        BGA_SetEventLayer(0, 0, 0x24);

        // Mantém camadas de fundo/overlay; os boxes serão desenhados
        // intercalados com os CDs abaixo para garantir a ordem desejada.
    }

    SongDB* db = &g_game.songDB;
    SongMode* mode = &db->modes[g_game.selectedModeIndex];
    int songCount = mode->songCount;
    if (songCount == 0) return;

    // Renderiza os 7 slots do carrossel em ordem de distância do centro,
    // garantindo que os discos mais distantes fiquem atrás e os mais próximos fiquem por cima.
    typedef struct {
        int slotIndex;
        float screenX;
        float bsc;
        float balpha;
        int texId;
        int cdHalf;
        float tw;
        float th;
        int bgaSlotFrame;
        float introXOff;
    } SlotRender;

    SlotRender slots[7];
    int slotCount = 0;

    for (int si = 0; si < 7; si++) {
        float slotFrame;
        float introXOff = 0.0f;

        if (g_carrosselIntro) {
            int slotPhase = si - 1;
            int phaseStart = slotPhase * 5;
            int local = g_introFrame - phaseStart;
            if (local <= 0) {
                introXOff = 500.0f;
            } else if (local < 5) {
                float t = local / 5.0f;
                introXOff = 500.0f * (1.0f - t);
            } else {
                introXOff = 0.0f;
            }
            slotFrame = (float)(588 + g_slotFrameOffset[si]);
        } else {
            slotFrame = (float)(g_carrosselFrame + g_slotFrameOffset[si]);
        }
        float bx, bsc, balpha;
        EvalBoxFrame(slotFrame, &bx, &bsc, &balpha);
        if (balpha < 0.01f) continue;

        float screenX = 320.0f + bx;
        int slotOffset = si - 3;
        int idx = (g_game.selectedSongIndex + slotOffset + songCount) % songCount;
        int sid = mode->songIds[idx];
        int si2 = Song_FindByID(db, sid);

        int cdHalf = 0;
        int cdIdx = (si2 >= 0) ? findCdForSong(sid, &cdHalf) : -1;
        int texId = (cdIdx >= 0) ? g_cdTexIds[cdIdx] : -1;

        if (slotCount < 7) {
            int bgaSlotFrame = (int)slotFrame;
            if (bgaSlotFrame < 540) bgaSlotFrame = 540;
            if (bgaSlotFrame > 636) bgaSlotFrame = 636;
            slots[slotCount].slotIndex = si;
            slots[slotCount].screenX = screenX + introXOff;
            slots[slotCount].bsc = bsc;
            slots[slotCount].balpha = balpha;
            slots[slotCount].texId = texId;
            slots[slotCount].cdHalf = cdHalf;
            slots[slotCount].tw = 256.0f * bsc;
            slots[slotCount].th = 128.0f * bsc;
            slots[slotCount].bgaSlotFrame = bgaSlotFrame;
            slots[slotCount].introXOff = introXOff;
            slotCount++;
        }
    }

    // Ordena do mais distante ao mais próximo do centro para manter a sobreposição correta.
    for (int i = 0; i < slotCount - 1; i++) {
        for (int j = i + 1; j < slotCount; j++) {
            float distI = fabsf(slots[i].screenX - 320.0f);
            float distJ = fabsf(slots[j].screenX - 320.0f);
            if (distI < distJ) {
                SlotRender tmp = slots[i];
                slots[i] = slots[j];
                slots[j] = tmp;
            }
        }
    }

    for (int i = 0; i < slotCount; i++) {
        SlotRender* slot = &slots[i];
        float selScale = (selectedState && slot->slotIndex == 3) ? 1.1f : 1.0f;
        if (slot->texId >= 0) {
            float cdScaleX, cdScaleY, cdOffX, cdOffY;
            if (selectedState && slot->slotIndex == 3) {
                cdScaleX = 1.1f; cdScaleY = 1.1f; cdOffX = 2.0f; cdOffY = -10.0f;
            } else {
                cdScaleX = 1.00f; cdScaleY = 0.95f; cdOffX = 0.0f; cdOffY = -1.0f;
            }
            float sw = slot->tw * cdScaleX;
            float sh = slot->th * cdScaleY;
            float u1 = 0.0f;
            float v1 = (1.0f - (float)slot->cdHalf) * 128.0f;
            float u2 = 256.0f;
            float v2 = v1 + 128.0f;
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            Texture_DrawUV(slot->texId,
                slot->screenX - sw * 0.5f + cdOffX,
                240.0f - sh * 0.5f - 10.0f + cdOffY,
                sw,
                sh * 1.6f,
                u1, v1, u2, v2,
                1.0f, 1.0f, 1.0f, slot->balpha);
        }
        if (selScale != 1.0f || (g_carrosselIntro && slot->introXOff != 0.0f)) {
            glPushMatrix();
            if (selScale != 1.0f) {
                glTranslatef(290.0f, 216.5f, 0.0f);
                glScalef(selScale, selScale, 1.0f);
                glTranslatef(-290.0f, -216.5f, 0.0f);
            }
            if (g_carrosselIntro && slot->introXOff != 0.0f)
                glTranslatef(slot->introXOff, 0.0f, 0.0f);
            BGA_SetEventLayer(0, slot->bgaSlotFrame, 0x0b);
            glPopMatrix();
        } else {
            BGA_SetEventLayer(0, slot->bgaSlotFrame, 0x0b);
        }
    }

    // Desenha os sprites dos modos (esquerda, centro, direita)
    if (g_game.bgaPicCount > 0) {
        int leftIdx  = (g_selDispIdx - 1 + 6) % 6;
        int centIdx  = g_selDispIdx;
        int rightIdx = (g_selDispIdx + 1) % 6;

        if (g_modeAnimActive) {
            float t = g_modeAnimFrame / (float)MODE_ANIM_DURATION;
            int oldLeft   = (g_selDispIdx - 1 + 6) % 6;
            int oldCenter = g_selDispIdx;
            int oldRight  = (g_selDispIdx + 1) % 6;
            if (g_modeAnimDir == 1) {
                // UR: oldCenter→LEFT, oldRight→CENTER, newRight enters from OFF
                int newRight = (g_selDispIdx + 2) % 6;
                // oldLeft:  LEFT→OFF  (Event 14→15, frame 420→434, direto s/ passar por C/R)
                BGA_SetEventLayer(0, (int)Math_Lerp(420.0f, 434.0f, t), g_modeLayers1P[oldLeft]);
                // oldCenter: CENTER→LEFT (Event 3→2, frame 74→60, reverso de L→C)
                BGA_SetEventLayer(0, (int)Math_Lerp(74.0f, 60.0f, t), g_modeLayers1P[oldCenter]);
                // oldRight:  RIGHT→CENTER (Event 5→4, frame 134→120, reverso de C→R)
                BGA_SetEventLayer(0, (int)Math_Lerp(134.0f, 120.0f, t), g_modeLayers1P[oldRight]);
                // newRight:  OFF→RIGHT  (Event 7→6, frame 194→180, reverso de R→O)
                BGA_SetEventLayer(0, (int)Math_Lerp(194.0f, 180.0f, t), g_modeLayers1P[newRight]);
            } else {
                // UL: oldCenter→RIGHT, oldLeft→CENTER, newLeft enters from OFF
                int newLeft = (g_selDispIdx - 2 + 6) % 6;
                // oldRight: RIGHT→OFF  (Event 6→7, frame 180→194)
                BGA_SetEventLayer(0, (int)Math_Lerp(180.0f, 194.0f, t), g_modeLayers1P[oldRight]);
                // oldCenter: CENTER→RIGHT (Event 4→5, frame 120→134)
                BGA_SetEventLayer(0, (int)Math_Lerp(120.0f, 134.0f, t), g_modeLayers1P[oldCenter]);
                // oldLeft:  LEFT→CENTER (Event 2→3, frame 60→74)
                BGA_SetEventLayer(0, (int)Math_Lerp(60.0f, 74.0f, t), g_modeLayers1P[oldLeft]);
                // newLeft:  OFF→LEFT   (Event 15→14, frame 434→420, reverso de L→O)
                BGA_SetEventLayer(0, (int)Math_Lerp(434.0f, 420.0f, t), g_modeLayers1P[newLeft]);
            }
        } else {
            // Estatico: BGA_SetEventLayer com frame fixo de cada posicao
            BGA_SetEventLayer(0, FRAME_LEFT,   g_modeLayers1P[leftIdx]);
            BGA_SetEventLayer(0, FRAME_CENTER, g_modeLayers1P[centIdx]);
            BGA_SetEventLayer(0, FRAME_RIGHT,  g_modeLayers1P[rightIdx]);
        }
    }

    // box2.spr glow pulsante, começa junto com o preview
    if (g_game.bgaPicCount > 0 && previewState) {
        if (selectedState) {
            glPushMatrix();
            glTranslatef(320.0f, 240.0f, 0.0f);
            glScalef(1.1f, 1.1f, 1.0f);
            glTranslatef(-320.0f, -240.0f, 0.0f);
            BGA_SetEventLayer(0, g_game.bgaFrame % 55, 0x18);
            glPopMatrix();
        } else {
            BGA_SetEventLayer(0, g_game.bgaFrame % 55, 0x18);
        }
    }

    if (previewState) {
        char buf[64];
        snprintf(buf, sizeof(buf), "D%d.AUD", prevSongId);
        Font_DrawStringCentered(320, 410, buf, 0.5f, 1.0f, 0.5f, 1.0f);
    }

    if (selectedState) {
        Font_DrawStringCentered(320, 380, "SELECTED", 0.0f, 1.0f, 0.0f, 1.0f);
    }
}