/* coin.c — Sistema de crédito e moeda (arcade)
 *
 * Reconstrução das funções Arcade_ e Coin_ do PUMPY.EXE.
 *
 * Mapa de origem:
 *   Arcade_ProcessCoin   0x00402340
 *   Coin_GetCredits      0x00402430
 *   Coin_GetMaxCredits   0x00402460
 *   Coin_ConsumeCredit   0x00402480
 *
 * Modelo do original — o ponto central é que o acumulador conta MOEDAS, não
 * créditos:
 *
 *   g_nCoinTotal    -> g_game.svcCoinTotal   (acumulador, em moedas)
 *   g_nCoin1Setting -> g_game.svcCoin1       (quantas moedas valem 1 crédito)
 *   g_nCoin2Setting -> g_game.svcCoin2       (quantos créditos vale 1 moeda da 2)
 *   g_nCoinTotal / g_nCoin1Setting = créditos disponíveis
 *
 * Isso casa com o texto que o COIN OPTION desenha:
 *   COIN1 -> "1 CREDITS / %d COIN"  (svcCoin1 moedas = 1 crédito)
 *   COIN2 -> "%d CREDITS / 1 COIN"  (1 moeda = svcCoin2 créditos)
 *
 * Dois casos dispensam moeda por completo, devolvendo 99 créditos e não
 * consumindo nada:
 *   - FREE PLAY: svcCoin1 == 0
 *   - modo EVENT: svcGameMode == 1
 *
 * Contadores de bookkeeping (zerados pelo CLEAR BOOKKEEPING do SETUP MENU):
 *   svcCoin1Total, svcCoin2Total, svcServiceTotal
 */

#include "pumpy.h"

#define COIN_MAX_CREDITS 9   /* o original satura em 9 créditos */

/* ------------------------------------------- Arcade_ProcessCoin 0x00402340
 * type: 1 = COIN1, 2 = COIN2, 3 = SERVICE
 */
void Arcade_ProcessCoin(int type)
{
    int after;

    /* Modo EVENT ignora moeda por completo */
    if (g_game.svcGameMode == 1)
        return;

    /* FREE PLAY: não contabiliza nada, mas o original ainda toca o som de
     * crédito (desvia para o mesmo bloco de som, LAB_00402401). */
    if (g_game.svcCoin1 == 0) {
        Audio_Play(g_waveSoundIds[SND_COIN_CREDIT], false);
        return;
    }

    if (type == 1) {             /* COIN1 — uma moeda */
        g_game.svcCoin1Total++;
        g_game.svcCoinTotal++;
    } else if (type == 2) {      /* COIN2 — vale svcCoin2 créditos */
        g_game.svcCoin2Total++;
        g_game.svcCoinTotal += g_game.svcCoin2 * g_game.svcCoin1;
    } else if (type == 3) {      /* SERVICE — crédito de cortesia */
        g_game.svcServiceTotal++;
        g_game.svcCoinTotal++;
    } else {
        return;
    }

    /* Satura em 9 créditos. O original testa "8 < creditos", ou seja, dispara
     * ao ATINGIR 9 — e ao fixar em svcCoin1*9 descarta a moeda parcial que
     * estivesse sobrando. Escrito literalmente para preservar esse efeito. */
    if (g_game.svcCoinTotal / g_game.svcCoin1 > COIN_MAX_CREDITS - 1)
        g_game.svcCoinTotal = g_game.svcCoin1 * COIN_MAX_CREDITS;

    after = g_game.svcCoinTotal / g_game.svcCoin1;

    /* O original distingue os dois casos por som: resto != 0 (moeda inserida
     * mas sem fechar crédito) toca um efeito, crédito fechado toca outro. */
    if (g_game.svcCoinTotal % g_game.svcCoin1 != 0) {
        Audio_Play(g_waveSoundIds[SND_COIN_PARTIAL], false);   /* 01-1.WAV */
        Log_Print("Coin: moeda parcial (tipo=%d) %d/%d\n", type,
                  g_game.svcCoinTotal % g_game.svcCoin1, g_game.svcCoin1);
    } else {
        Audio_Play(g_waveSoundIds[SND_COIN_CREDIT], false);    /* COIN2.WAV */
        Log_Print("Coin: +credito (tipo=%d) -> %d credito(s)\n", type, after);
    }
}

/* ---------------------------------------------- Coin_GetCredits 0x00402430 */
int Coin_GetCredits(void)
{
    if (g_game.svcCoin1 != 0 && g_game.svcGameMode != 1)
        return g_game.svcCoinTotal / g_game.svcCoin1;
    return 99;   /* FREE PLAY ou EVENT */
}

/* ------------------------------------------- Coin_GetMaxCredits 0x00402460
 * O nome no original engana: devolve o total bruto de MOEDAS, não de créditos.
 * Mantido com a mesma semântica para não divergir.
 */
int Coin_GetMaxCredits(void)
{
    if (g_game.svcCoin1 == 0 || g_game.svcGameMode == 1)
        return 99;
    return g_game.svcCoinTotal;
}

/* ------------------------------------------- Coin_ConsumeCredit 0x00402480 */
void Coin_ConsumeCredit(void)
{
    if (Coin_GetCredits() != 0 && g_game.svcGameMode != 1) {
        g_game.svcCoinTotal -= g_game.svcCoin1;
        if (g_game.svcCoinTotal < 0) g_game.svcCoinTotal = 0;
        Log_Print("Coin: credito consumido -> %d restante(s)\n", Coin_GetCredits());
    }
}

/* ------------------------------------------------------------------ extras -
 * Conveniências que o original não tem como função separada, mas que o
 * reconstructed precisa para decidir se pode iniciar partida.
 */

bool Coin_IsFreePlay(void)
{
    return (g_game.svcCoin1 == 0) || (g_game.svcGameMode == 1);
}

bool Coin_HasCredit(void)
{
    return Coin_IsFreePlay() || (Coin_GetCredits() > 0);
}
