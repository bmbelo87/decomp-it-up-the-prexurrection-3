# Comparativo PUMPY.EXE (original) × PumpyReconstructed

Documento de cobertura e fidelidade. Gerado a partir da análise estática do `PUMPY.EXE`
via Ghidra cruzada com o código-fonte do `PumpyReconstructed`.

**Data:** 14/09/2026 — atualizado em 25/09/2026 (regressões do port SDL2, ver abaixo)
**Binário:** `PUMPY.EXE` — 756 funções totais, das quais **~290 são código de jogo**
(o restante é libpng, zlib e runtime MSVC estaticamente linkados).
**Fonte:** 24 módulos `.c` ativos em `src/` (arquivos `*_REV01.c` são revisões antigas e
foram excluídos da análise).

## Metodologia

O mapeamento **não é função-a-função**. O reconstructed adota uma arquitetura
diferente do original em vários pontos — o exemplo mais claro é a renderização de
gameplay, que no original está espalhada por `Gameplay_RenderArrowLanes`,
`Gameplay_RenderP1LaneGroup`, `Gameplay_RenderP2LaneGroup`, `Gameplay_RenderCoopLanes`,
`Judge_RenderAnim` e `Judge_RenderEffect`, e no reconstructed está concentrada em um
único `Gameplay_Render`. Por isso a classificação é por **sistema e comportamento**,
não por contagem de símbolos.

Os sistemas marcados como críticos receberam comparação de lógica real (decompilação do
original lida contra o código C). Os demais receberam mapeamento de cobertura.

---

## Resumo executivo

| Sistema | Cobertura | Observação |
|---|---|---|
<!-- ✓ = exercitado em execução, não só compilado -->
| Núcleo / App / Estado | Completo | Arquitetura reorganizada, comportamento equivalente |
| Render / OpenGL | Completo | Shaders do original não são usados (fixed pipeline) |
| Input | **Parcial** | Teclado via SDL; `PUMPPAD.DLL` fora do escopo (sem hardware) — ver Lacunas |
| Fonte / Texto | Completo | Crash do `glBitmap(NULL)` corrigido em 25/09/2026 |
| Textura | Completo | Decodificador PNG próprio + zlib (vcpkg) no lugar da libpng embutida |
| Sprite SPR / SP2 | Completo | Parsers de tile idênticos |
| RES / arquivos | Completo | XOR + ENC1 + bit-reverse conferidos |
| BGA | Completo | BGA0 e BGA2 |
| Áudio | Completo | Mixer SDL2 no lugar do DirectMusic para `.AUD`; MP3 adicionado; relógio da BGM interpolado |
| Step / chart | Completo | Scroll speed embutido em `gameplay.c`; Earthworm corrigido em 25/09/2026 |
| Judgment / timing | **Fiel** | Validado numericamente — ver abaixo |
| Gameplay / holds / explosion | **Fiel** | Validado — ver abaixo |
| HUD / combo | Completo | `Combo_DrawMain` / `DrawSprite` / `DrawDigit` normalizados |
| SongSelect | Completo | — |
| Cheats | Completo | — |
| Menu / Warning / Logo / Staff | Completo | Inclui HowToPlay (`STATE_HOWTOPLAY`) |
| Result / Stage | Completo | Fórmula de grade corrigida em 14/09/2026 |
| GameOption | Completo | — |
| VSL / Model / Lighting (3D) | Completo | — |
| Debug / console | Completo ✓ | `src/debug_console.c` — crase abre, 13 comandos. Verificado em execução |
| ServiceMenu | Completo ✓ | 11 páginas em `src/service_menu.c` — F1 abre/percorre, F2 confirma. Todas as páginas verificadas em execução |
| Arcade / Coin | Completo | `src/coin.c` — F4/F5 moeda, F2 serviço; default FREE PLAY |
| Bink (vídeo) | N/A | Rótulo errado do Ghidra: são wrappers de zlib |
| Ranking | Completo | `src/ranking.c` |
| Demo | Completo | `Demo_ToggleMode` |

---

## Validação profunda — sistemas críticos

### Janelas de timing — MATCH EXATO

O original monta as janelas em `GameInit` (`0x411042`, `0x4110ce`, `0x41114f`), um bloco
por nível de dificuldade, multiplicando constantes float por
`base = [0x00d390b0] × (1/120)` e arredondando com `Math_ROUND`. Os resultados ficam em
`DAT_00da222c`..`DAT_00da2248` e são lidos em `Gameplay_ProcessJudgment`.

Valores extraídos do binário:

| Nível | PERFECT | GREAT | GOOD | BAD |
|---|---|---|---|---|
| 0 (Easy) | −12 / +7 | −17 / +12 | −22 / +17 | −27 / +22 |
| 1 (Normal) | −10 / +5 | −15 / +10 | −20 / +15 | −25 / +20 |
| 2 (Hard) | −8 / +3 | −13 / +8 | −18 / +13 | −23 / +18 |

O reconstructed (`k_judgeEarly` / `k_judgeLate`, `gameplay.c:29-38`) expressa os mesmos
valores em segundos a 8 ms por unidade. Conferindo os 24 números: todos batem.
Exemplo — Easy/PERFECT: `0.096 ÷ 0.008 = 12` e `0.056 ÷ 0.008 = 7`.

### Pontuação — MATCH EXATO

Original (`Gameplay_ProcessJudgment`, switch em `piVar16[0x1a]`):

```
case 1 (PERFECT): combo++; if (combo > 3) score += 1000; score += 1000;
case 2 (GREAT):   combo++; if (combo > 3) score += 1000; score += 500;
case 3 (GOOD):    nada
case 4 (BAD):     combo = 0
case 5 (MISS):    combo = 0
```

O reconstructed reproduz isso em `processRowJudgment`, incluindo o detalhe de o teste
`combo > 3` usar o combo **depois** do incremento — o original faz
`if (3 < iVar20 + 1)` com `iVar20` sendo o valor anterior, o que dá o mesmo resultado.

### Vida (life / lifeSpeed) — MATCH (com uma ressalva de arredondamento)

| Julgamento | Original | Reconstructed | |
|---|---|---|---|
| PERFECT | `life += speed*12/1000; speed += 20` | idem | exato |
| GREAT | `life += speed*10/1000; speed += 16` | idem | exato |
| GOOD | sem efeito | idem | exato |
| BAD | `life -= 50; speed += DAT_00da225c/2` | `life -= 50; speed += −700/2` | exato |
| MISS | `life = life − life*500/2000 − 20` | `life = life*3/4 − 20` | ±1 |

`DAT_00da225c` é inicializado em `GameInit:0x41138c` com `0xFFFFFD44` = **−700**, que é
exatamente o `LIFE_SPEED_PENALTY` do reconstructed.

A fórmula de MISS merece atenção. Em aritmética real as duas são a mesma coisa, mas em
inteiro a ordem da divisão muda o resultado:

- Original: `life − floor(life×500/2000) − 20` = `life − floor(life/4) − 20`, que é
  `ceil(3×life/4) − 20`
- Reconstructed: `floor(life×3/4) − 20`

Quando `life` não é múltiplo de 4, o original fica **1 unidade acima** do nosso. Ex.: com
`life = 501`, o original dá 376 e o nosso dá 375.

> **Resolvido (14/09/2026):** `applyLife` agora usa `life - life/4 - 20`.

Vida inicial 500 (`MOV EDX,0x1f4` em `GameInit`) e faixa de `lifeSpeed` 200–1000
(`[0x00da2260]` / `[0x00da22c0]` no modo padrão) também conferem.

### Tipos de nota — MATCH

`NT_TAP=1`, `NT_HOLD_H=10`, `NT_HOLD_B=11`, `NT_HOLD_T=12` batem com os testes do
original (`bVar2 == 1`, `cVar1 == '\n'`, `'\v'`, `'\f'`). O bit `0x80` que o original usa
para marcar nota já julgada (`*pcVar19 = cVar1 + -0x80`) é resolvido no reconstructed por
limpeza direta do painel (`clearPanel` / `clearHDPanel` / `clearDNPanel`) — abordagem
diferente, efeito equivalente.

### Explosion / ArrowF / borda SP2 — MATCH COMPORTAMENTAL

Validado em sessão anterior e reconfirmado aqui:

- `Judge_RenderEffect` (`0x0040d580`) é a **borda SP2** — timer por painel em
  `DAT_00da2470..9`, incrementa 0→16, escala 0.8→1.1, e é zerado enquanto o botão está
  pressionado. Dispara em qualquer press, sem relação com julgamento. Corresponde ao
  `g_p1FlashTimer`.
- `Gameplay_RenderP1ArrowFX` / `P2ArrowFX` (`0x0040da50` / `da70`) é o **glow ArrowF** —
  chama `SPR_RenderP2Offset` com `DAT_00da2264`; quando esse valor passa de `0x29` (41), a
  camada extra é desenhada em blend aditivo (`GL_SRC_ALPHA, GL_ONE`). Corresponde ao
  `g_glowTimer` + `g_noteState`.

O original usa o índice do tile da animação como gatilho; nós usamos um timer direto. O
resultado visual é o mesmo: glow aditivo só durante a explosão de Perfect/Great.

---

## Divergências encontradas

### 1. Fórmula de grade — RESOLVIDA (14/09/2026)

> Corrigida em `result.c:calcGrade`. A fórmula e os cinco cortes abaixo são
> agora exatamente os do binário. O texto original do levantamento fica
> preservado para registro do que estava errado.

Esta é a divergência mais significativa do levantamento, e afeta progressão de stage e
tela de resultado.

**Original** (final de `Gameplay_ProcessJudgment`, `0x41042c`; o resultado é gravado em
`[0x00da22d0]` como **float** — a decompilação do Ghidra mostra `(int)` por erro de
tipagem, mas `DanceGradeDisplay` lê o endereço com `FLD float ptr`):

```
ratio = (perfect + great*0.9 + good*0.6 − bad*0.5 − miss + maxCombo*0.03) / total
```

O termo `maxCombo*0.03` só entra quando `g_nGameMode != 1`; no modo 1 a fórmula é
`(perfect + great*0.9 + good*0.6 − bad*0.5 − miss) / total`.

Escada de grade do original (`DanceGradeDisplay`, `0x415330`–`0x4153ac`):

| Condição | Sprite |
|---|---|
| `ratio >= 1.0` **e** `missCount == 0` | `0x2c` |
| `ratio >= 0.9` | `0x2d` |
| `ratio >= 0.8` | `0x2e` |
| `ratio >= 0.7` | `0x2f` |
| `ratio >= 0.6` | `0x30` |
| caso contrário | `0x31` |

**Reconstructed** (`result.c:calcGrade`):

```c
w = (perfect*10 + great*7 + good*5 + bad*2) / (total*10);
if (w >= 0.95f && miss == 0) return 0;  // S
if (w >= 0.85f) return 1;  // A
if (w >= 0.75f) return 2;  // B
if (w >= 0.60f) return 3;  // C
if (w >= 0.40f) return 4;  // D
return 5;                   // F
```

Diferenças ponto a ponto:

| Termo | Original | Reconstructed |
|---|---|---|
| perfect | ×1.0 | ×1.0 |
| great | ×0.9 | ×0.7 |
| good | ×0.6 | ×0.5 |
| bad | **−0.5** | **+0.2** (sinal invertido) |
| miss | **−1.0** | **0** (ignorado) |
| maxCombo | +0.03 cada | ausente |
| Thresholds | 1.0 / 0.9 / 0.8 / 0.7 / 0.6 | 0.95 / 0.85 / 0.75 / 0.60 / 0.40 |

O acerto estrutural é que o grade máximo exige `missCount == 0` nos dois — isso bate.
Mas os pesos e os cortes não. Na prática o reconstructed é mais permissivo: um BAD
*soma* pontuação em vez de subtrair, e MISS não penaliza a razão (só bloqueia o grade
máximo).

### 2. Tipos de nota 2, 3 e 4 — INVESTIGADO, não vale implementar (14/09/2026)

> **Conclusão:** medido e descartado. Ver "Evidência" no fim desta seção.

O original testa `cVar1 == '\x02'` e `cVar1 == '\x03'` em `Gameplay_ProcessJudgment`,
incrementando `piVar16[0xb]` e `piVar16[10]`. Essas contagens alimentam dois julgamentos
extras:

```
if (piVar16[9]  != piVar16[0xb]) julgamento = 6;
if (piVar16[8]  != piVar16[10]) julgamento = 7;
```

Ou seja, além dos cinco julgamentos conhecidos (PERFECT/GREAT/GOOD/BAD/MISS) o original
tem os tipos **6 e 7**, disparados por mudança nesses contadores. O valor `4` também
aparece nos testes de nota.

No reconstructed, `step.c` lê os bytes crus da grade STX direto para `StepRow`, então os
valores 2/3/4 *chegam* às estruturas — mas `step.h` só define `NT_TAP`, `NT_HOLD_H`,
`NT_HOLD_B` e `NT_HOLD_T`, e nenhum caminho de julgamento os consome. São ignorados
silenciosamente.

#### Evidência

**Frequência real nos charts.** Varredura dos 79 `.STX` em `STEP/`, descomprimindo as
seções e histogramando os 10 painéis de cada row:

| Valor | Ocorrências |
|---|---|
| 0 (vazio) | 3.624.630 |
| 1 (TAP) | 125.181 |
| **2** | **11** |
| **3** | **11** |
| **4** | **9** |
| 10/11/12 (hold) | 492 / 3.863 / 493 |

Aparecem em apenas 9 dos 79 arquivos, todos na faixa 7xx, com 1 a 3 ocorrências cada.

**Distribuição.** O 2 e o 3 sempre aparecem *na mesma row*, em DL e DR (painéis 0 e 4,
às vezes trocados). O 4 aparece sozinho, em CN ou DL. Não formam padrões de gameplay —
são marcadores pontuais.

**O que os julgamentos 6 e 7 fazem.** Em `Judge_RenderAnim` (`0x0040dd70`) o `switch` que
escolhe o sprite de julgamento cobre só os casos 1 a 5; 6 e 7 caem no `default` e **não
desenham sprite nenhum**. Recebem duração de 40 frames (`0x28`) em vez de 25 (`0x19`),
usam outra tabela de escala (`DAT_00442910`) e — o ponto decisivo — suprimem o contador
de combo enquanto duram:

```c
if (judgment != 6 && judgment != 7) { ... Combo_RenderValue(...) ... }
```

E no `switch` de pontuação de `Gameplay_ProcessJudgment` não existe `case 6` nem
`case 7`: não somam score, não mexem na vida, não contam para o grade.

#### Decisão

Implementar produziria, em 9 músicas, 1 a 3 momentos de 40 frames com o número do combo
escondido e nada desenhado. Nenhum efeito em pontuação, vida, grade ou combo contabilizado.
O custo de reproduzir o estado extra não se paga. Fica registrado caso apareça evidência
de que esses marcadores sincronizam algo no BGA dessas músicas específicas.

### 3. `lifeSpeed` não varia por nível de dificuldade — RESOLVIDA (14/09/2026)

O original escolhe o perfil de `lifeSpeed` por `[0x00d39041]` (o nível), em `GameInit`
`0x00411381`. Varia o inicial e também a faixa de clamp:

| Nível | Inicial | Mín | Máx |
|---|---|---|---|
| 0 — EASY | 500 | 200 | 1000 |
| 1 — NORMAL | 300 | 100 | 900 |
| 2 — HARD | 100 | 0 | 800 |

O inicial vai para `[0x00da2328]` (P1) e `[0x00da23c0]` (P2); mín e máx para
`[0x00da2260]` e `[0x00da22c0]`.

**Atenção a um detalhe fácil de confundir:** o que varia é o `lifeSpeed`
(`piVar16[0x18]`), não a barra de vida. A barra (`piVar16[0x17]`, gravada em
`[0x00da2324]`/`[0x00da23bc]`) começa sempre em 500, seja qual for o nível. Como o
`lifeSpeed` é o multiplicador de ganho — `life += speed*12/1000` no PERFECT — um inicial
menor faz a vida subir muito mais devagar, sem começar mais baixa.

O reconstructed fixava os três no perfil do EASY. Efeito prático da correção, no primeiro
PERFECT da música: EASY ganha +6 de vida, NORMAL +3 e HARD +1.

> Corrigido em `gameplay.c`: as constantes `LIFE_SPEED_INIT/MIN/MAX` viraram as tabelas
> `k_lifeSpeedInit/Min/Max` indexadas por `optionDifficulty`, e o clamp passou a ser
> único no fim de `applyLife`, como o original faz em `Gameplay_ProcessJudgment`.

---

## Regressões do port SDL2 corrigidas (25/09/2026)

O commit `bb82a22` (port SDL2 cross-platform) foi escrito e testado no Linux. No build
Windows (Win32 Debug, dependências via vcpkg `x86-windows`) ele trouxe as falhas abaixo,
todas corrigidas.

| # | Sintoma | Causa | Correção |
|---|---|---|---|
| 1 | Build falhava: `VK_A` / `VK_0` não declarados | Só `platform_linux.h` define; o Windows SDK não | Fallback `#ifndef` com `0x41` / `0x30` em `src/input.c` |
| 2 | Jogo abria e fechava (access violation em `nvoglv32`) | `glBitmap(8, 16, …, NULL)` para os caracteres ≥ 128; o driver NVIDIA lê o ponteiro nulo | Buffer zerado de 16 bytes em `src/font.c` (mesmo glifo vazio) |
| 3 | 81 de 92 texturas falhavam (`tex loaded -> -1`) | No Windows o PNG caía no inflate próprio de `zlibinflate.c`, que falha | `zlibinflate.c` usa o zlib real também em `_WIN32` (mesmo caminho do Linux). Afeta `step.c`, que chama a mesma função |
| 4 | Setas atrasadas; `AudioOffset` sem efeito | `BGM_IsDSActive()` fixo em `false` → `g_songTime += dt`, sem ligação com a música | Retorna `BGM_IsPlaying()`; setas seguem a posição da BGM menos o `AudioOffset` |
| 5 | Rolagem com leve travada (~43 fps efetivos) | Consequência de #4: `g_bgmChan.pos` só anda em blocos de 1024 frames (~23 ms) no callback | `BGM_GetPositionMs()` interpola pelo `SDL_GetPerformanceCounter` desde o último callback (máx. 1 bloco, monotônico) |

Dependências novas no Windows: `SDL2d.dll` e `zd.dll` (Debug) ao lado do executável.

### Earthworm — corrigido (25/09/2026)

Verificado no assembly do `PUMPY.EXE` (desempacotado; lido com capstone):

- **Frequência:** o `% 0x30` em `0x4141e3` vale só para o Random Velocity. O `jne 0x41428e`
  em `0x4141e7` leva direto ao bloco do Earthworm, que exige apenas `row != [0xda24bc]`
  (gravado a cada frame em `0x412998`). Earthworm reavalia o alvo **a cada row**; o
  reconstructed fazia isso só a cada compasso, e por isso se comportava como RV.
- **Relógio:** `[0xd35eac]` (lido por `0x4024f0`) **não é ms**. `timeSetEvent(1, 0,
  0x41a1c0, 0, TIME_PERIODIC)`; o callback só incrementa quando `(n*240)/1000` muda →
  tick de 240 Hz. O reconstructed usava `timeGetTime()` direto (onda 4× mais rápida).
- **Velocidades:** BPM ≤ 180 (`double` em `0x434980`): x3 se `tick % 120 <= 60`, senão x2.
  BPM > 180: x2 se `tick % 90 <= 45`, senão x1. Nunca x4.
- **Rampa:** `0x4118d0` (chamada a cada frame em `0x41484d`) copia o alvo para
  `[0xda2294]`; `0x414888` aproxima a velocidade em ±50/1000 por frame. Confere com o
  reconstructed (3x/s a 60 fps).
- **Row (`[0xda24b4]`):** gravada em `0x4129a0` a partir de `esi`, que em `0x4129db` é
  comparado com `[song+0xd39130]` para encerrar a música → é o **índice de row do chart**,
  mesma unidade do `currentRow` do reconstructed. O `% 0x30` do RV, portanto, é aplicado
  sobre rows do chart nos dois (rastreado em 25/09/2026).
- **fps:** o frame roda em `0x402850`, chamado quando `PeekMessage` não tem mensagem
  (`0x41a3cc`). Ele converte `timeGetTime()` em tick de 240 Hz e só executa quando
  `tick − [0xd35f6c] > 3`, gravando `[0xd35f6c] = tick` → **60 Hz**, uma atualização de
  lógica por frame. A rampa de ±50/1000 por frame equivale a 3x/s, igual ao
  reconstructed (passo fixo de 1/60 com acumulador em `Game_MainLoop`).

### Tempos e telas — corrigidos (25/09/2026, tarde)

Todos confirmados no assembly do `PUMPY.EXE`; tempos em ticks de 240 Hz ou frames de 60 Hz.

| Item | Original | Reconstructed antes | Correção |
|---|---|---|---|
| Preview no Song Select | toca com `[0xd5fd34] > 0x28` **e** `[0xd5e394] > 0x50` (`0x40a6d5`), contadores +1/frame | 1,0 s após navegar; imediato na entrada e na troca de modo | entrada/troca de modo 81 frames (1,35 s), navegação 41 frames (0,68 s); contagem corre durante animações (`src/song_select.c`) |
| Título → música | Na confirmação o Song Select carrega o título e executa o comando de console `run %d -<modo>` (`0x4091a0`–`0x4092a2`) → `0x410cf0` carrega tudo, espera o contador `[0xd35eb4]` ≥ `0x3C0` (`0x4116b5`), toca a música e só então zera o contador. **O contador conta desde o início do TIME do Song Select** (zerado em `0x4096bd`/`0x40aadc`; TIME = 60 − ticks/240, `0x409768`), então com mais de 4 s de escolha **não há espera**: o título fica congelado só durante a carga | ~3,6 s + carga; steps depois do `BGM_Play` | piso de 4,0 s desde o título (aproxima o tempo de carga percebido do original — hipótese; validado em jogo pelo usuário); steps antes da espera (`src/loading.c`) |
| Staff | `0x4041b0` carrega `STAFF.DAT`/`84.AUD`, zera o contador e espera `0x1E0` ticks = 2,0 s (`0x404220`) | música imediata | espera de 2,0 s antes do `BGM_Play` (`src/staff.c`) |
| Console (crase) | `VK_OEM_3` = posição física à esquerda do 1 (scancode `0x29`) | mapeado por keycode `SDLK_BACKQUOTE`; não abria no ABNT2 | mapeado por `SDL_SCANCODE_GRAVE` (`src/input.c`) |
| Lifebar HalfDouble (preenchimento) | — | V invertido no estilo TGA (`th − 1 − v`) lia as linhas 97–111 / 81–95 do `ST02.PNG` | linhas 144–158 / 160–174 direto (`src/gameplay.c`); coordenadas ainda não conferidas no assembly |

### VSL (músicas 900) — corrigido (25/09/2026, noite)

Confirmado no assembly e por capturas lado a lado da 902 (original × port).

| Item | Original | Reconstructed antes | Correção (`src/vsl.c`) |
|---|---|---|---|
| Modo de repetição da textura | por material: `WRAP_S/T` = `GL_CLAMP` se flags `0x400`/`0x800`, senão `GL_REPEAT` (`0x4166f5`–`0x416736`) | `CLAMP_TO_EDGE` sempre; UVs do `.tc` vão de −1 a 0 → borda esticada, fundo quase todo preto | `GL_REPEAT` ao ligar cada textura |
| Orientação V | — | `1 - v` compensava o WIC antigo, que invertia linhas | V direto |
| Matriz do quadro-chave | 12 floats na ordem do arquivo: `a[0..2]=f0..f2`, `a[4..6]=f3..f5`, `a[8..10]=f6..f8`, `a[12..14]=f9..f11` (`0x416ab0`–`0x416b2a`); `glMultMatrixf` direto (`0x416d01`) | rotação transposta + inversão de `m[1]`/`m[4]` no render (só acertava rotação em Z) | ordem do original, sem inversão |
| Faces de trás | `glEnable(GL_CULL_FACE)` + `glCullFace(GL_BACK)` (`0x418333`–`0x41834e`) | cull desligado | cull ligado |

Câmera já idêntica: `gluPerspective(73.74, 4/3, 0.01, 15)` e `gluLookAt((0,0,3.2)→(0,0,−1), up (0,1,0))`.

**Pendente:** as flags do material no original também escolhem o blend (`0x10` aditivo,
`0x20` alpha, nenhum → sem blend) e o clamp (`0x400`/`0x800`). O parser do `.tc` do port não
lê esse campo (o `d0` lido é outro); todos os materiais usam `REPEAT` e o blend + "color key".

> **Tentativa descartada (25/09/2026):** ler as flags do 1º `u32` após o shininess (o campo
> que o parser chama de `texWidth`, com valores 0x10/0x20/0x200/0x280/0) e aplicar as regras
> acima. Em teste na 902 as **cores estouraram** — visivelmente pior que o original. O
> comportamento atual (REPEAT + blend normal + "color key") foi confirmado pelo usuário como
> correto. Não reaplicar sem antes confirmar no assembly qual campo do arquivo vai para
> `+0x20` (a simulação da pilha de `0x416140` ficou inconsistente).

**Debug:** F11 mostra o cronômetro da música e o frame do VSL (`Gameplay_Render`); debug
começa desligado. `Resource_ClearBGA` → `Font_Shutdown` desligava a fonte de debug no gameplay;
o bloco chama `Font_Init()` antes de desenhar.

### Gameplay — judge, setas e fim da música (25/09/2026, noite)

Levantamento completo do desenho do gameplay em `docs/GAMEPLAY_RENDER.md`.

| Item | Original | Reconstructed antes | Correção (`src/gameplay.c`) |
|---|---|---|---|
| Duração do judge | 25 frames para PERFECT..MISS; 40 só nos tipos 6/7 (`0x40dd8a`–`0x40dda6`), que não desenham sprite | 40 frames + escala fixa 0,99 para PERFECT/GREAT (lógica dos tipos 6/7) | 25 para todos; ramo "P/G" desligado |
| Quadro da animação da seta | `fase_da_batida / 10` (`0x412905`–`0x412930`) → 6 quadros por batida | `(frameCounter / 3) % 6` (20 fps fixos) | `arrowAnimFrame()` |
| Fim do gameplay | sai no que vier primeiro: chart do jogador acabou (`[0xda24b4] >= [chart+0xd39130]`) **ou** BGM parou (`0x4192a0() == 1`) (`0x414902`–`0x414968`) | só por tempo parado/timeout; `g_hasAudio` nunca atribuída → esperava o chart inteiro (815: 148 s de chart, 95 s de música) | as duas regras do original; `g_hasAudio` definida no `Gameplay_Start` |

**Esperas antes de música — todas conferidas (25/09/2026):** `0x4191a0` (toca a BGM) é
chamada em 10 pontos. Só há espera no gameplay (`0x4116dd`, 4,0 s) e no Staff (`0x40422c`,
2,0 s), já corrigidos. Os demais tocam logo após carregar, como o reconstructed:
`0x40414d` abertura (`03.DAT`/`003.AUD`), `0x4044fb`/`0x4046ba` menu (o segundo reinicia a
música quando ela acaba), `0x405fd2` teste de áudio do Service Menu, `0x415a17` resultado
(`83.DAT`/`83.AUD`), `0x415b0b` (`086.DAT`/`086.AUD`), além do preview (`0x40a715`/`0x40a797`).

**Build Release:** o Debug (sem otimização) deixava as trocas de tela mais lentas que o
original — decodificação de MP3/PNG e XOR dos RES. `PUMPYTESTE_RELEASE.EXE` (+ `SDL2.dll`,
`z.dll`) removeu parte da diferença.

**Ferramenta:** `tools/key_mirror.py` (+ `KEY_MIRROR.bat` e cópia na pasta do jogo) espelha o
teclado para a janela do original via `PostMessageA(WM_KEYDOWN/UP, VK, 0)`, para comparar os
dois lado a lado. Base: o `PUMPY.EXE` só lê teclado pela WndProc (`0x419ce0`, sem
`GetAsyncKeyState`/DirectInput) e ignora o `lParam`; fecha sozinho se minimizado
(`WM_ACTIVATE` → `WM_CLOSE`).

---

## Lacunas (sistemas sem implementação)

### PUMPPAD.DLL — analisado, fora do escopo (25/09/2026)

O port SDL2 reduziu `Input_LoadPumpPad()` (`src/input.c`) a um stub que retorna `false`;
só teclado funciona. Fora do escopo: o pad Andamiro USB é difícil de conseguir para PC.
A análise abaixo fica como referência caso isso mude.

**Fatos (assembly):**
- `PUMPY.EXE` `0x41a308`–`0x41a37e`: `LoadLibraryA("PUMPPAD.DLL")` (duas tentativas; falha →
  MessageBox "This file is missing or corrupt."), `GetProcAddress("PumpPadInit")` e
  `PumpPadInit(hWnd)` com o retorno ignorado. `PumpPadFini` nunca é resolvido.
- `PUMPPAD.DLL` é Delphi (VCL). `PumpPadInit` (`0x452464`) procura um HID USB de fabricante
  **"Andamiro"**; sem ele retorna 0. Com ele, cria a thread `TPumpPad` (`+0x30` hWnd,
  `+0x34` device, `+0x38`/`+0x39` estado anterior = `0x3F`).
- `TPumpPad.Execute` (`0x452290`) lê relatórios de 3 bytes `[ID][byte1][byte2]`, loga com
  `OutputDebugString("%x %x %x")` e, por bit que mudou, chama
  `SendMessageA(hWnd, WM_KEYDOWN/WM_KEYUP, VK, 0)`. Bit em 0 = pressionado.

| byte1 | P1 | VK | | byte2 | P2 | VK |
|---|---|---|---|---|---|---|
| 0x02 | UL | `Q` | | 0x02 | UL | `VK_HOME` |
| 0x10 | UR | `E` | | 0x10 | UR | `VK_PRIOR` |
| 0x20 | Centro | `S` | | 0x20 | Centro | `VK_CLEAR` |
| 0x08 | DL | `Z` | | 0x08 | DL | `VK_END` |
| 0x04 | DR | `C` | | 0x04 | DR | `VK_NEXT` |
| — | | | | 0x01 | ESC (reset) | `VK_ESCAPE` |

A DLL só simula teclado, com os mesmos VKs que o port já mapeia. Para implementar:
carregar a DLL, chamar `PumpPadInit` com o HWND do SDL e capturar os `WM_KEYDOWN/UP`
injetados via `SDL_SetWindowsMessageHook` (`SDL_GetKeyboardState` não os enxerga).

### Avaliados e descartados

Após a varredura de 14/09/2026 restaram dois itens, e **os dois foram avaliados e
descartados com evidência** — nenhum é ausência real:

### Bink — não existe neste binário

As cinco funções `Bink_*` são um erro de rótulo do Ghidra. Seguindo as chamadas:

- `Bink_InitDecoder` (`0x0041a71c`) ← chamada por **`Step_ParseFile`** (duas vezes)
- `Bink_DecoderOpenDefault` (`0x0041a8fd`) ← chamada por **`PNG_CreateReadStruct`**

São os wrappers de `inflateInit` do zlib, usados para descomprimir as seções dos `.STX`
e os dados da libpng. Ficam imediatamente antes do bloco `ZLib_*` no espaço de endereços.
Nada de vídeo. A funcionalidade já está coberta por `src/zlibinflate.c`.

### DirectMusic — coberto por outro backend

`DMLoader_Init` é chamado por **`Audio_LoadADPCM`**, ou seja, DirectMusic é o caminho de
reprodução dos `.AUD` no original. Não é um sistema ocioso.

Mas o reconstructed já reproduz `.AUD` por DirectSound (`BGM_LoadAUD` / `BGM_LoadAUDDirect`),
além de suportar MP3, o que o original não faz. Trocar um caminho de áudio funcionando por
outro equivalente não agrega comportamento e só adiciona risco.

---

#### Tabela histórica (antes da avaliação)

| Sistema | Funções no original | Impacto |
|---|---|---|
| DirectMusic (`DMLoader_*`, `DMPlayer_*`) | 8 | BGM usa DirectSound; MP3/AUD cobrem o uso real |
| Debug console | ~13 | **Implementado** — `src/debug_console.c`, abre com a crase |
| Ranking | 1 | **Implementado** — `src/ranking.c`, visível pelo `/highscore` |
| Demo | 1 | **Implementado** — `Demo_ToggleMode` no console (`/testmode`) |

Nenhuma dessas afeta o loop principal de jogo.

---

## Notas de arquitetura

O reconstructed diverge do original por escolha em alguns pontos, sem prejuízo de
comportamento:

- **Plataforma (desde `bb82a22`):** janela, input e áudio via SDL2. Build Windows usa
  vcpkg (`x86-windows`: `sdl2`, `zlib`) com o toolchain em `C:/vcpkg`.
- **PNG:** o original linka libpng inteira (~120 funções `PNG_*`); o reconstructed usa um
  decodificador próprio em `texture.c` (o WIC foi removido no port SDL2).
- **zlib:** o original linka zlib; o reconstructed usa o zlib real em Windows e Linux via
  `zlibinflate.c`. O inflate próprio desse arquivo só entra em outras plataformas e
  falhava nos PNGs do jogo.
- **Áudio:** mixer SDL2 a 44,1 kHz (buffer de 1024 frames) no lugar do DirectSound /
  DirectMusic; MP3 via `dr_mp3.h`, o que o original não faz.
- **Músicas de teste:** a música 100 (sem `100.AUD` / `D100.AUD`) é adição local para
  testes, não existe no original; o aviso "preview silencioso" no log é esperado.
- **Nomes pendentes:** `gameplay.c` ainda tem `FUN_00411b40`, `FUN_00411a90` e
  `FUN_004119d0` com os nomes do Ghidra. Correspondem a `Combo_DrawMain`,
  `Combo_DrawSprite` e `Combo_DrawDigit`. Renomear é cosmético mas ajuda a leitura.

---

## Prioridades sugeridas

Os quatro itens da lista de 14/09/2026 (grade, tipos de nota 2/3/4, `lifeSpeed` por modo,
arredondamento do MISS) estão resolvidos ou descartados com evidência. Atualizado em
25/09/2026:

1. **PUMPPAD.DLL** — fora do escopo (sem hardware); protocolo documentado.
2. **Earthworm** — confirmado em jogo em 25/09/2026 (animação igual ao original).
3. **Esperas antes de música** — conferidas; nenhuma diferença restante.
4. **Relógio das setas** — o original usa o acumulador de `0x412880`; o reconstructed usa a
   posição do áudio.
