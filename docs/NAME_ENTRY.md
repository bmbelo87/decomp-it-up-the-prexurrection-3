# Enter Your Name (085.DAT) e High Scores (084.DAT)

O `PumpyOriginal.exe` (PREX3) **não** carrega `085.DAT`/`084.DAT`: o fim de crédito vai direto
`0x16 (083.DAT)` → `0x17 (84.DAT, grava .ini)` → attract. A referência usada aqui é o
`PREX3-MK5\PIU\PIU32.EXE` (mesma base de código, imagem EEPROM idêntica). Endereços abaixo são do PIU32.

## Estados (dispatcher 0x404d60, variável 0x43a59c)

| Estado | Função | Papel |
|---|---|---|
| — | 0x413100 | fim de jogo: se `Rank_Find(scoreP1)` ou `Rank_Find(scoreP2)` ≠ -1 → `0x1b`, senão `0x17` |
| 0x1b | 0x406ce0 | Enter: `085.DAT`, FONT1~4.TGA, `AL_FRAME.SPR`, `AUDIO\085.AUD`; nomes = `"    "`; flags P1/P2 pendentes (só se entram no Top 20 **e** o jogador está ativo, máscara em 0x43ff38) → `0x1c` |
| 0x1c | 0x406f90 | Intro de 60 frames (contador 0xed30dc) → `0x1d` |
| 0x1d | 0x407d90 | Digitação |
| 0x1e | 0x408550 | para som → `0x17` (Game Over, 84.DAT, grava .ini) |
| 0x1f | 0x408580 | High Scores (`084.DAT`) → `0x20` (0x408660) |

Um jogador por vez: P1 primeiro; ao terminar volta para `0x1c` (intro) com o P2; depois `0x1e`.
Botão Service (`0xecac54 & 0x10000`) em `0x1c`/`0x1d` → estado `0x11`.

## Ranking e EEPROM

- `Rank_Find` 0x406cc0: primeira posição `i` (0..19) com `score > scores[i]` (comparação unsigned); -1 se nenhuma.
- `Rank_Insert` 0x405650(score, name4): `\0` no nome vira espaço; empurra as entradas de baixo; `strncpy(name, 4)`.
- A tabela vive **dentro da imagem EEPROM de 2048 bytes** (base 0xedac78 no MK5, 0xd38858 no PREX3):

| Offset | Conteúdo | PREX3 | MK5 |
|---|---|---|---|
| +0x748 | 20 × u32 score | 0xd38fa0 | 0xedb3c0 |
| +0x798 | 20 × char[4] nome (sem terminador) | 0xd38ff0 | 0xedb410 |

- O ranking não entra no checksum: o Adler-32 (+0x690) cobre só os 8 bytes de +0x7E8.
- O Enter Your Name só altera a imagem em memória. A gravação em disco ocorre no Game Over (`0x4124b0` MK5 /
  `0x412520` PREX3 → `0x405190`).
- Defaults do ranking (`0x404fe0` PREX3) são aplicados só no reset da EEPROM (`0x405150` termina com `jmp 0x404fe0`),
  não a cada boot.

## Digitação (0x407d90)

- Tabela 0x43fd74 (58, circular): `ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789~!@#$%^&*()_-+=\:;/?` + `0x01` (BACK, idx 56) + `0x00` (END, idx 57).
- Cursor `0xed4024`; posição da letra `0xed30e4` (0..4); direção da animação `0xedabc4` (1 = →, 2 = ←); frames desde o último passo `0xed30e8`.
- Esq/dir: P1 bits `0x1`/`0x2`, P2 `0x100`/`0x200` (borda). Segurado (`0xed07d4`): repete se `t > 30`, ou se já repetindo (`0xed30d8`) e `t > 20`.
  Cada passo toca o som `0xf3ee8c` (segurado: `0xf3eec0`).
- Centro (`0x4` / `0x400`): letra → grava e avança; BACK → apaga a anterior (`pos--`, grava espaço); END → encerra.
  Sempre que `pos` é 4 ou mais, o cursor vai para END (idx 0x39), e `0x43fdb0` vira 0 (a roda fica parada).
- A letra sob o cursor é escrita em `name[pos]` a cada frame (pré-visualização).
- Timer: `40 − ticks/240` (ticks = 0xed07e4, zerado por 0x405640). Fim com END ou timer ≤ 0.
- Ao encerrar: `name[pos..3]` = espaço; se tudo espaço → `"PUMP"`; `Rank_Insert`; som `0xf3ee88`.
- Animação: `t += 1` por frame (`+2` quando repetindo); a roda interpola por 20 frames (`t*0.05`).

## Layout (coordenadas Y-UP, 640×480)

**Camadas do 085.BGA** (0x403d00(bga, frame, layer)):
- `frame = contador % 227`: layers 2, 7, 10, 0x22, 0x23, 0x25, 0x26, 0x29, 0x2a
- frame 60 fixo: layers 0x2b, 0x2d
- frame 0: layer 0xc (P1) ou 0xd (P2)
- layer 0x25 frame 360 (P1) / 0x26 frame 526 (P2)
- setas 0x2f (→) / 0x30 (←): frame `11 + 1.5·t` enquanto animando, senão 11

**Linha do jogador** (glScalef 1.5, então as coordenadas na tela são ×1.5), cor (1,1,1,0.5):
- rank `%02d` em (44,44), nome `%c%c%c%c` em (103,44), score `%07d` em (218,44)
- avanço: 24 px/letra (0x406c00), 20 px/dígito (0x406c60); glifo 34×34 a partir do canto inferior esquerdo
- cursor: `_` sob a posição atual em (103,42), piscando (`contador % 60 > 30`)

**Roda de letras** (0x407720 → 0x406e60 por letra):
- 9 letras, índice `cursor + k`, k = −4..4
- X = 245 ± {0, 115, 210, 260, 300} (tabela 0x43fdb4; 330 é o slot de entrada da animação)
- Y = −104 (0x43fdcc)
- escala {1.0, 0.85, 0.75, 0.65, 0.55} (0x43fde4)
- alpha 1, exceto no slot externo (k = ±4), que tem alpha 0
- 0x406e60(x, y, ch, rot, s, a):
  - desenha AL_FRAME escalado por `s` em torno do pivô (76,400) com alpha `a`
  - depois o glifo em (x+74, y+400), escala `3.5·s`, alpha `a·0.5`, quad centrado ±17

**Glifo** (0x406910/0x406930/0x406940):
- `i` = índice na tabela de 58 caracteres
- textura FONT(1 + i/16)
- célula de 64 px: coluna = `0x43fd38[i]`, linha = `0x43fcfc[i]`
- `i == 58` (caractere fora da tabela, ex. espaço) → não desenha

**Timer** (0x407610): FONT4, 2 dígitos 34×34, unidade em x = 600 e dezena em x = 566; y = 424.
UV: u = dígito·25/256 (largura 25/256), v de 0.75 a 0.8477. Blend aditivo (`GL_SRC_ALPHA, GL_ONE`).
Na intro, y = 424 + 250 − 25·frame até o frame 10.

## Attract (ciclo de demonstração)

**PREX3** (`PumpyOriginal.exe`, estado 0 = 0x4020a0, contador 0x8bbf40 % 3):
Logo `81.DAT` (4→5) → Menu `82W.DAT` (0xa→0xb) → Demo Play (0x407cb0) → volta ao Logo.

**MK5** (`PIU32.EXE`, estado 0 = 0x4044b0, contador 0x5e05e8 % 4):
Logo (4) → Menu (0xa) → Demo (0x416c80) → **High Scores `084.DAT`** (0x1f) → volta ao Logo.

- **Menu 82W:** a música `082.AUD` fica em loop. Sai com o contador de frames ≥ 1980 (`0x7bc`; PREX3 0x404687, MK5 0x40923e),
  sem crédito e sem jogador entrando. Isso dá cerca de 33 s a 60 fps (taxa de frames não conferida).
- **Demo Play:**
  - Música: lista 0xd5f622, índice `demoCount % total`.
  - Modo: alterna Normal/Hard, montando `"%d -n -demo"` / `"%d -h -demo"`.
  - Flag `-demo`: grava a máscara `0x100533`, com o bit `0x100000` de demo. Liga o autoplay nos dois jogadores e faz o jogo ignorar o pad, exceto as moedas.
  - Sai quando entra crédito ou quando o relógio da música ≥ 8400 ticks (PREX3 0x4149d5, MK5 0x411497). O tick é de 240 Hz, então **35 s**.
    O relógio 0xd35eac é zerado ao carregar a música.
  - Depois volta ao estado 0. Não passa pelo Resultado.
- **High Scores MK5:**
  - `0x1f` (0x408580): carrega `084.DAT` e FONT1~4. Se houver crédito, vai para o menu (`0xa`).
  - `0x20` (0x408660), página 1:
    - 133 frames (`0x85`);
    - 8 linhas que entram com fade (linha i: alpha = `(f − 78 − 5i)/15`);
    - y = 480 − 132 − 48i;
    - colunas: rank `" %2d"` em x=25, nome em x=135, score `"%08d"` em x=385.
  - `0x21` (0x4087e0): lista das 20 entradas rolando por 900 frames (`0x384`), rolagem a partir do frame 120,
    12 linhas visíveis, altura 48; depois fade-out de 30 frames.
  - `0x22` (0x408b90): para o som → estado 0.
  - Service → estado `0x11`.

## Em aberto
- Unidade de `0xed07e4`: o callback 0x41e367 chama 0x405500 quando `ms*80/1000` muda de valor, o que sugere 80 Hz.
  Com isso o timer duraria 120 s, o que parece longo. Hipótese não confirmada.
- 0x406f90 (intro): trajetória exata das letras entrando (constantes 800/1045/245 e limiares por coluna) não detalhada.
- Tela 084 (0x408660) não analisada.
