/* ranking.c — Tabela de recordes
 *
 * Reconstrução do sistema de variáveis/ranking do PUMPY.EXE.
 *
 * Mapa de origem:
 *   Var_RegisterName          0x00402ca0
 *   Var_SetSystemVariable     0x00402be0
 *   Ranking_RegisterDefaults  0x00404fe0  (chamada por Game_InitState 0x0040517c)
 *
 * Armazenamento do original:
 *   scores -> int  [0x00d38fa0], 20 entradas
 *   nomes  -> char [0x00d38ff0], 20 entradas de 4 bytes (sem terminador)
 *
 * Os nomes têm exatamente 4 caracteres, preenchidos com espaço quando menores —
 * é o próprio Var_SetSystemVariable que faz esse preenchimento. Aqui guardamos
 * 5 bytes por nome só para ter o terminador e poder imprimir com %s.
 *
 * Observação sobre visibilidade: no original não existe tela de ranking. Os
 * estados STATE_RANKING/RANKING_IN/RANKING_OUT do nosso enum são valores
 * herdados da numeração original sem código correspondente. A única forma de
 * ver a tabela é pelo comando /highscore do console de debug.
 */

#include "pumpy.h"
#include <string.h>

#define RANK_ENTRIES 20
#define RANK_NAME_LEN 4

static int  g_rankScore[RANK_ENTRIES];
static char g_rankName[RANK_ENTRIES][RANK_NAME_LEN + 1];

/* ---------------------------------------------- Var_RegisterName 0x00402ca0
 * Escrita direta numa posição, sem ordenação. Usada só para semear a tabela.
 */
void Var_RegisterName(int index, int score, const char* name)
{
    if (index < 0 || index >= RANK_ENTRIES) return;
    g_rankScore[index] = score;
    strncpy(g_rankName[index], name, RANK_NAME_LEN);
    g_rankName[index][RANK_NAME_LEN] = '\0';
}

/* ----------------------------------------- Var_SetSystemVariable 0x00402be0
 * Inserção ordenada: procura a primeira entrada com score menor que o novo,
 * desloca o restante uma posição para baixo e insere ali. A última entrada
 * cai fora da tabela.
 */
void Var_SetSystemVariable(int score, const char* name)
{
    char padded[RANK_NAME_LEN + 1];
    int i, j;

    /* O original preenche com espaço os bytes nulos dentro dos 4 primeiros */
    for (i = 0; i < RANK_NAME_LEN; i++)
        padded[i] = (name && name[i]) ? name[i] : ' ';
    padded[RANK_NAME_LEN] = '\0';

    for (i = 0; i < RANK_ENTRIES; i++) {
        if (g_rankScore[i] < score) {
            for (j = RANK_ENTRIES - 1; j > i; j--) {
                g_rankScore[j] = g_rankScore[j - 1];
                memcpy(g_rankName[j], g_rankName[j - 1], RANK_NAME_LEN + 1);
            }
            g_rankScore[i] = score;
            memcpy(g_rankName[i], padded, RANK_NAME_LEN + 1);
            return;
        }
    }
}

/* ------------------------------------- Ranking_RegisterDefaults 0x00404fe0
 * Os 20 valores e nomes são exatamente os do binário.
 */
void Ranking_RegisterDefaults(void)
{
    Var_RegisterName(0,  1000000, "ANDA");
    Var_RegisterName(1,   900000, "MIRO");
    Var_RegisterName(2,   800000, "BOSS");
    Var_RegisterName(3,   700000, "NEXT");
    Var_RegisterName(4,   600000, "SUN ");
    Var_RegisterName(5,   500000, "HOON");
    Var_RegisterName(6,   400000, "BAE ");
    Var_RegisterName(7,   300000, "GUN ");
    Var_RegisterName(8,   200000, "HALO");
    Var_RegisterName(9,   100000, "HWAO");
    Var_RegisterName(10,   90000, "RANG");
    Var_RegisterName(11,   80000, "SS  ");
    Var_RegisterName(12,   70000, "PIAH");
    Var_RegisterName(13,   60000, "CATY");
    Var_RegisterName(14,   50000, "KANN");
    Var_RegisterName(15,   40000, "YAPP");
    Var_RegisterName(16,   30000, "MOON");
    Var_RegisterName(17,   20000, "SMEP");
    Var_RegisterName(18,   10000, "KURA");
    Var_RegisterName(19,    9000, "^_^/");
}

/* Acesso para o comando /highscore do console */
int Ranking_GetCount(void)            { return RANK_ENTRIES; }
int Ranking_GetScore(int i)           { return (i >= 0 && i < RANK_ENTRIES) ? g_rankScore[i] : 0; }
const char* Ranking_GetName(int i)    { return (i >= 0 && i < RANK_ENTRIES) ? g_rankName[i] : ""; }
