# Renderização do gameplay — PUMPY.EXE

Levantamento feito em 25/09/2026 lendo o assembly do `PUMPY.EXE` (desempacotado, lido com
capstone). Endereços são VA. "Confirmado" = lido no assembly; "provável" = inferido do fluxo.

## Convenções

- Projeção 2D **Y para cima**, 640×480. A sequence zone fica em `y = 376`.
- Tile de SPR (struct do original): `+0x20` textura, `+0x24/+0x26/+0x28/+0x2A` = `x1,y1,x2,y2`
  (int16), `+0x2C/+0x30/+0x34/+0x38` = `u1,v1,u2,v2` (float).
- Primitivas:
  - `0x40b320(tile)` — quad no retângulo do próprio tile (relativo à matriz atual).
  - `0x40b290(tile, yA, yB)` — usa `x1..x2` do tile e **estica** entre `yA` e `yB` (corpo do hold).
  - `0x40b3b0(tile)` — idem `0x40b320` em coordenada de tela (`480 − y`).
  - `0x40b860(spr, frame)` — desenha um SPR/BGA animado no quadro `frame`.

## Ordem de desenho do frame (`0x414820`)

1. Lógica de velocidade/rampa (`0x4118d0`, `0x411950`), relógio.
2. Lifebar P1 (`0x411c00(0)`), fundo/HUD (`0x40b860`).
3. **Sequence zone** (`0x40da90` → `0x40d960`).
4. **Setas e holds** (`0x414710` / `0x4146b0` / `0x4146e0` → `0x4127a0` por jogador).
5. Lógica de julgamento P1/P2 (`0x40e0d0`, `0x40e330` — não desenham).
6. Combo de comparação 1000/2000/3000 (`0x411b40`, x = 256 / 337 / 408).
7. Lifebar P2, brilho da sequence zone (`0x40da50`/`0x40da70`), setas P1/P2.
8. **Judge + combo** (`0x40dae0` → `0x40dd70`), HUD de modificadores (`0x40c160`).
9. Por jogador, em `glTranslatef(x_jogador, 376, 0)`: **flash de apertar** (`0x40d580`) e
   **efeito de acerto** (`0x40cbe0`).

Posição X do jogador: P1 **28**, P2 **348**; modos de 10 colunas **65** / **312**.

## Setas (`0x4127a0`, uma chamada por jogador)

- `y = 376 − velocidade × frac × 0,001` — `velocidade` = `[0x442718]` (P1) / `[0x44271C]` (P2),
  1000 = x1; `frac` = posição da linha em 1/60 de batida. Em x1: **60 px por batida**.
- Laço: começa na linha atual `[0xda2270]`; por linha `glPushMatrix`, `glTranslatef(0, y, 0)`,
  5 colunas com `glTranslatef(49, 0, 0)` entre elas, `glTranslatef(−196, 0, 0)`, `glPopMatrix`;
  avança `60 / divisão_da_batida` e continua **enquanto `y > −60`**.
- Cada coluna tem seu próprio conjunto de tiles (ex.: coluna 0 = seta `0x445d9c`, corpo
  `0x8bb2fc`, ponta `0x8bb338`; coluna 2 = `0x446ccc`, `0x8bb464`, `0x8bb4a0`).
- **Animação da seta:** quadro = `[0xda24c4]` = `fase_da_batida / 10` → **6 quadros por
  batida**, sincronizado com o BPM (`0x412905`–`0x412930`). Tile = `base + quadro × 0x3C`.
- Tipos de nota: `1` normal; `2`/`3` tiles especiais (`0x8bb194`, `0x8bb02c`); `4` só liga
  `+0x74` e é apagada; `0x0A/0x0B/0x0C` cabeça/corpo/cauda do hold. Bit `0x80` = já julgada.
- Modificadores: bit `0x10` (Vanish) → alfa `(256 − y) / 60` quando `y > 196`;
  bit `0x80` → alfa 0.

## Holds (mesma função)

- Na **cabeça** (`0x0A`) o jogo conta as linhas até a cauda (`0x0C`) e desenha:
  - **corpo** `0x40b290(corpo, yInício, yFim)` deslocado `−4,2` px (coluna 0; outras colunas
    usam `±3,0`), **ponta** `0x40b290(ponta, …)` deslocada `+4,2`, e a **seta da cabeça**.
- **Hold segurado** (botão pressionado, `[0x8baffc]` bit da coluna): a **cabeça fica presa na
  sequence zone** (`glTranslatef` compensando `frac`), o corpo começa em ~30 px e vai até a cauda.
- **Cauda** (`0x0C`) com a linha anterior vazia/julgada: corpo de 46 a 55 + ponta de 4 a 56.
- Corpo só é desenhado para linhas com `y ≤ 520`.

## Sequence zone (`0x40d960(coluna_base, fase, flag)`)

- SPR dos receptores (`0x8bcec8` no single; variantes `0x9dc598`/`0x8bfc58` nos modos
  0x200/0x800) no quadro = **fase da batida** `[0xda2264]` (0–59).
- Se `fase > 40`: camada de brilho (`0x8bddf4`) em **blend aditivo** (`SRC_ALPHA, ONE`).
- P2 (coluna_base 5): `glTranslatef(320, 0, 0)`.

## Judge (`0x40dd70`)

- Estado por jogador (stride `0x98`): `+0x68` judge novo, `+0x6C` exibido, `+0x70` timer.
- Judge novo → timer **25** (tipos 6 e 7: **40**), −1 por frame.
- Posição: `glTranslatef(x, 240, 0)`, x = P1 **160**, P2 **480**, 10 colunas **320**.
- Escala (timer 25→11): 1,52 1,43 1,35 1,28 1,21 1,15 1,10 1,06 1,03 1,01 0,99 0,98 0,97 0,98 0,99;
  timer 10–9: 1,0; timer ≤ 8: X = tabela `0x442850` (1,0 → 1,35), alfa = `t × 0,125`,
  **blend aditivo**.
- Sprite em escala 0,8. Tipos 1–5 = PERFECT `0x445f40`, GREAT `0x445f7c`, GOOD `0x445fb8`,
  BAD `0x445ff4`, MISS `0x446030`. Com combo ≥ 4: sprite "COMBO" `0x44606c` em escala 1,0.

## Combo (`0x40c840` → `0x40c780`), 70 px abaixo do judge

- Combo (`+0x50`) ≥ 4 → branco; **misses seguidos** (`+0x54`) ≥ 4 → vermelho `(1; 0,2; 0,2)`.
- `glTranslatef(24, 0)`, escala 1,1, **sempre 3 dígitos** (zeros à esquerda), da unidade para a
  centena, −40 px por dígito.
- Dígito: quad 44×45 px; grade **5 colunas × 2 linhas**: `u = (d % 5) × 0,171875`,
  `v = 0,65625 + (d / 5) × 0,17578125` (textura `[0x9e20b0]`).

## Efeito de acerto (`0x40cbe0`)

- Dispara **só em PERFECT e GREAT** (judge 1 ou 2): timer da coluna `[0xda2494+c] = 0`
  (`0x40f455`–`0x40f4aa`).
- Timer sobe 1/frame até 24. Alfa `1 − t/24`, escala `1 + t × 0,01`, **blend aditivo**:
  clarão `0xd34fac` (64×64, centro +32) + a própria seta da coluna crescendo (centro +27).
- **Hold:** flag `[0xda2298+c] = 1`; com ela, ao chegar em 12 o timer volta a 0 (efeito em
  loop) e a seta é desenhada parada no receptor em blend normal.

## Flash de apertar (`0x40d580`)

- Timer da coluna `[0xda2470+c]` de 0 a 16, zerado enquanto o botão está pressionado.
- Escala `0,8 + t × 0,01875` (0,8 → 1,1); alfa por `t/2`: 0; 1,0; 0,9; 0,8; 0,7; 0,6; 0,4; 0,2; 0.
- Sprite: borda da seta da coluna (`0x445f04`, `0xc196fc`, …).

## Divergências no reconstructed

> Corrigidas em 25/09/2026 (aguardando validação em jogo): timer 25 para todos os judges e
> ramo "P/G" desligado; animação da seta via `arrowAnimFrame()` (fase da batida / 10).
> O código anterior ficou comentado em `src/gameplay.c`.

| Item | Original | Reconstructed |
|---|---|---|
| Duração do judge PERFECT/GREAT | 25 frames (40 só nos tipos 6/7) | 40 frames para P/G (`src/gameplay.c`, comentário "0..39 P/G") |
| Quadro da animação da seta | `fase_da_batida / 10` (6 por batida, segue o BPM) | `(frameCounter / 3) % 6` (20 fps fixos) |

## Pendências

- Tipos de nota 2/3/4 e judges 6/7 (ver `COMPARATIVO.md`, seção "Tipos de nota 2, 3 e 4").
- Detalhe exato dos limites do corpo do hold por coluna (±3,0 vs ±4,2) nas colunas 1, 3 e 4.
