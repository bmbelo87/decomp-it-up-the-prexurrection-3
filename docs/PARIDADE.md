# Paridade PUMPY.EXE (original) × Pumpy (port Linux)

Atualizado em 2026-09-21. Base: `PUMPY.EXE` descompactado com `upx -d` (uma cópia; o
original não foi tocado). Ferramentas: `tools/analysis/LEIAME.md`.

Legenda: ✅ conferido instrução a instrução e igual · 🔧 divergia, corrigido · ⚠️ divergência
conhecida/não corrigida · ❓ ainda não comparado.

## 1. O original em números

| Item | Valor |
|---|---|
| Formato | PE32 i386, GUI, UPX (2.068.480 bytes descompactado), CRT MSVC (build "Oct 31 2003") |
| `.text` | 0x401000, ~203 KB, 67.358 instruções, ~729 funções (heurística) |
| Imports | KERNEL32, USER32, GDI32, OPENGL32, GLU32, DSOUND, WINMM, ole32 (sem DirectInput) |
| Endereços citados no nosso código | 70; 64 dentro do `.text` (38 exatos), 6 em dados |

## 2. Teclas

| Aspecto | Original | Port |
|---|---|---|
| Como o jogo lê o teclado | WndProc (`0x419ce0`), `WM_KEYDOWN/UP` por **VK**, tabela de saltos `VK-0x0C` | scancode físico SDL |
| Teclas de jogo P1 | Z C S Q E (VK) | ✅ Q E S Z C |
| Teclas de jogo P2 | End PgDn **Num5 (VK_CLEAR)** Home PgUp (VK) | ✅ mesmas + Keypad 7/9/1/3 (extensão) |
| Numpad com NumLock ligado | **ignorado** (VK_NUMPAD0-9 = 0x60-0x69, fora da tabela) | ✅ aceito (extensão pedida) |
| `piukey.cfg` | lido por `KEYInitialize` (0x4018b0): 2 linhas × 9 hex `%02x`, `fopen("rt")`, padrão Z X C/A S D/Q W E e Num1-9 | 🔧 lido no mesmo formato e com o mesmo log (`...OK`/`...FALSE`) |
| Consumo de `piukey.cfg` | **nenhum**: só `KEYInitialize` acessa a tabela (0x8bbf2c); dado vestigial | usado de verdade (índices 0,2,4,6,8 = botões 1,3,5,7,9) |
| `KEYS.INI` | não existe | 🔧 removido |

Os códigos `47/49/4f/51` (Num7/9/1/3) aceitam também Home/PgUp/End/PgDn, espelhando o
`VK_HOME` etc. do original. `configurar_teclas.py` grava o `piukey.cfg`.

## 3. Gameplay: verificado contra o assembly

| Item | Onde no original | Estado |
|---|---|---|
| Life inicial 500 | GameInit 0x41136e | ✅ |
| Velocidade do life: init/min/max Easy 500/200/1000, Normal 300/100/900, Hard 100/0/800 | 0x411381-0x4113fd | ✅ |
| Penalidade de speed −700 (Miss), −350 (Bad) | 0x41138c, 0x410308 | ✅ |
| Perfect: +1000 (+1000 se combo>3), life += speed·12/1000, speed += 20 | 0x41021a | ✅ |
| Great: +500 (+1000 se combo>3), life += speed·10/1000, speed += 16 | 0x41027a | ✅ |
| Good: só zera missCombo | 0x4102d9 | ✅ |
| Bad: combo=0, life −= 50 | 0x4102f3 | ✅ |
| Miss: combo=0, missCombo++, life −= life·500/2000 + 20 | 0x410326 | ✅ |
| Clamp de speed ao min/max do nível | 0x41037f | ✅ |
| Porcentagem: (P + 0,9G + 0,6Go − 0,5B − M [+0,03·maxCombo fora do modo 1]) / total | 0x4103ce | ✅ |
| Escada de nota: ≥1,0 sem miss = S; 0,9 A; 0,8 B; 0,7 C; 0,6 D; senão F | 0x415330 | ✅ |
| Fim de jogo: `life < 1` ou `missCombo > 50` | 0x414a1e / 0x414a5c | ✅ |
| **Janelas de julgamento** (valores e sentido) | 0x41102c-0x4111c8, 0x412944 | 🔧 ver abaixo |
| **Limiar de perigo da barra (life < 180)** | 0x411ed2/0x412102/0x41223c/0x41239f | 🔧 estava 90 |

### Janelas de julgamento (corrigido, 2 rodadas)
O original guarda a janela em **unidades inteiras** (60 por batida):
`win = (int)(k × (bpm × 0.00833333f))`, recalculada a cada mudança de BPM. Em tempo: `win / bpm` s.

**Sentido do delta** (o que a 1ª rodada tinha errado): o original mede
`delta = posLinha − posAtual` (fórmula em 0x412944-0x41294e), ou seja **positivo = cedo**.
Ele testa `Perfect: −12b < delta < +7b` (Easy), então a tolerância é **maior para TARDE** que para
CEDO. Confirmado por um 2º indício independente: a passada de Miss (0x40eaac) só percorre as
**10 linhas mais antigas** e perde a linha com `delta < −Bad`.

| Nível | Cedo (Perfect, Great, Good, Bad) | Tarde (Perfect, Great, Good, Bad) |
|---|---|---|
| Easy | 7, 12, 17, 22 | 12, 17, 22, 27 |
| Normal | 5, 10, 15, 20 | 10, 15, 20, 25 |
| Hard | 3, 8, 13, 18 | 8, 13, 18, 23 |

Diferenças que o port tinha: (1) early/late **invertidos**; (2) 8 ms/unidade em vez de ~8,333 ms;
(3) sem truncar para inteiro (a constante float32 é < 1/120 e o `_ftol` em 0x4269b0 trunca:
BPM 100, Easy Perfect tarde = 0,090 s, cedo = 0,050 s); (4) prazo do Miss fixo em 0,22 s.

### Qual linha o aperto consome e quem gradua a linha (corrigido)
Lido em 0x40e480-0x40ea34:
- As 20 linhas candidatas são varridas em ordem **crescente**; o aperto consome a **primeira** cuja
  nota naquele botão ainda não foi consumida e cujo delta cai em alguma janela. O port escolhia a
  de menor |diff|. Em sequências rápidas isso muda qual nota leva o aperto.
- Numa linha com vários botões, o julgamento só sai quando a **última** nota é consumida e usa o
  delta **dessa batida**. O port usava o maior |diff| entre as batidas.
- Miss: a linha é perdida quando `delta < −Bad`, isto é, passa do limite **tardio** do Bad
  (Easy 27 un, Normal 25, Hard 23), agora calculado por nível e BPM (`judgeBadLate`).

Corrigido em `gameplay.c` (`judgeWindows`, `currentJudgeBpm`, `badLimits`, `processInput`).
**Não validado em partida real**: não há como injetar batidas nesta máquina; a mudança está
confirmada por leitura do assembly e teste numérico das janelas.

### Holds (provado por emulação do código original)
`tools/analysis/emu.py` carrega o PE descompactado no emulador Unicorn e chama `0x40e330` (função pura) com
cenários controlados (`holdtests*.py`). Resultados:

- **Cada linha do hold (cabeça, cada corpo, cauda) é um julgamento independente**, com score, combo, life e
  `speed` como um tap. Hold perfeito de 5 linhas = 5 Perfects (`+1000,+1000,+1000,+2000,+2000`).
- Botão segurado consome a linha: **Normal/Hard/Crazy/Division** (flags `0x10,0x20,0x400,0x100`) quando
  `delta ≤ 0`; **HalfDouble/Double/Nightmare** (`0x80,0x200,0x800`) já na janela cedo do Perfect (≈ 58 ms antes).
  Logo soltar 5 un antes da cauda = **Miss** nos primeiros e **Perfect** nos segundos.
- Sem botão, cada linha é perdida **individualmente**, só após o limite tardio do Bad (Easy 27 un), com penalidade
  de life e quebra de combo. Soltar não encerra o hold: reapertar a tempo acerta a linha com a nota real
  (ex.: `Great` 2 un atrasado).
- `0x40cbe0` = efeito visual da explosão; `0x40e0d0` = autoplay; `0x4127a0` = desenho. Bits do pad: P1 DL=0x1 DR=0x2
  C=0x4 UL=0x8 UR=0x10, P2 ×0x100.

Corrigido no port (`gameplay.c`: `holdLeadSec`, `applyRowJudgment`): (1) o botão segurado só consome a linha a
partir do `lead` acima (antes capturava até ~0,18 s antes); (2) grade pelo tempo real em vez de sempre Perfect;
(3) **linhas de hold agora ganham/perdem life** (antes nunca mexiam no life); (4) carência antes do Miss ao soltar,
com penalidade de life. **Não testado em partida real.** Não verificado: se Good/Bad também fazem a seta sumir
(o port a deixa passar).

### Animações de acerto (bug corrigido: explosão sem seta)
- **Flash "p1" no aperto** (borda do receptor): **fiel ao original**. `0x414f7f` zera o contador de cada
  botão apertado (`0xda2470+i`) sem checar se há nota. Não é bug.
- **Explosão/glow**: no original só reinicia quando uma nota é consumida (`0x40e5e5`).
- **Bug do port**: a explosão reinicia sozinha enquanto `g_holdRows ≥ 0`, e um hold podia nunca encerrar
  se a cauda fosse consumida por outro caminho (auto-capture/pending); resultado: explosão repetindo sem
  seta. Corrigido em `gameplay.c` (`holdHasRowsAhead`): sem corpo/cauda à frente, o hold termina.
  **Causa provável, não reproduzida em jogo**; o log ganhou a linha `HOLD: … preso …` para confirmar.

### Tipos de nota 2, 3 e 4
Os seus `.STX` têm notas de tipo **2 (140), 3 (206) e 4 (142)**, todas no **chart 7 = DIVISION**
(e 4+4 no chart 0 de uma música, Practice). O original as desenha com sprites próprios; o tipo 4 é
zerado ao chegar ao receptor (seta `[player+0x74]=1`) e nunca exige aperto. O port não conhece esses
tipos e os trataria como setas comuns, mas só aparecem no modo Division (já listado como
"planejado"), então **não afeta os modos normais**.

## 4. Funcionalidades presentes/ausentes (busca por strings)

Presentes: `-demo2`, `/testmode2`, `STATISTICS MENU`, EEPROM, modos de step, `LogFile.txt`,
`UPTIME/BPLTIME`. Ausentes: aviso `EEPROM chksum err` (0x4067a0), captura de tela
`.\cap\pr_%03d.TGA`, `stage.cap` (0x419a30, a investigar), textos `Now Loading %s...`,
capas `CD01..CD45.TGA` (a confirmar), `PUMPPAD.DLL` (N/A em Linux).

## 5. Demais áreas

| Área | Estado | Detalhe |
|---|---|---|
| Velocidade de scroll | ✅ fórmula | `60/beatSplit` × velocidade `0x442720`×0,001; x1…x8 = 1000…8000. O deslocamento vertical absoluto (56 no original) depende da convenção de coordenadas e **não foi verificado** |
| Janelas ao trocar de BPM | ✅ | recalculadas em `judgeWindows` no BPM do segmento atual |
| **Mirror** | ✅ idêntico | swaps do original (`0x410a30`): Double `0↔8,1↔9,2↔7,3↔5,4↔6`; Single `0↔3,1↔4`; HD = subconjunto `2↔7,3↔5,4↔6` (posições extras vazias no HD). Iguais às nossas permutações |
| **Random** | ⚠️ variante | original (`0x4106e0`): por linha, **5 trocas aleatórias** dentro de `[lo..hi]` (P1 0-4, P2 5-9, Double 0-9, HD 2-7) só nas linhas **sem hold**; hold movido como bloco de colunas. O port faz Fisher-Yates por linha e embaralha também as linhas com hold. Diferença estatística/cosmética, não reescrita (falta decodificar `0x410670/0x410620`). O comentário do `step.h` que falava em "4 swaps globais" estava desatualizado |
| **Ranking** | ✅ idêntico | 20 entradas (nome, pontuação, posição) iguais às de `Ranking_RegisterDefaults` (0x404fe0), conferido por script |
| Console de debug | ✅ | 11 comandos nos dois; o port tem aliases extras (`/coin`, `/h`); `/mode` e `/play` são de linha de comando no original |
| Linha de comando | ⚠️ ausente | o original lê `/play`, `/autoplay`, `/mode`, `/testmode2`, `-n -h -d -c -hd -dv -nm`; o port ignora `argv` |
| Timer | ✅ coerente | `timeSetEvent` periódico de 1 ms (`0x41a302`); o movimento é por `dt` em ms |
| **Efeitos sonoros** | 🔧 corrigido | ver 5.2 |
| Notas 2/3/4 | ⚠️ só Division | ver seção de holds |
| Lógica de holds | ✅/🔧 | provada por emulação; port ajustado (ver seção de holds) |
| Estados | ⚠️ parcial | original grava 0x0,2,3,4,5,**8**,A,B,C,D,F,11,12,16-1F,20,21,23,24; `0x8` = HowToPlay (o port usa 0x86) |
| Arquivos referenciados | ✅ | os 13 arquivos fixos que o original cita (`BGA\\00/03/083/086/099/81/82W/83/84/LT03/R_WARN/STAFF.DAT`, `AUDIO\\83/84.AUD`) existem na sua pasta (2 com outra caixa: `82w.daT`, `staff.dat`, que o port já resolve) e todos são citados no nosso código |
| Battle | ✅ n/a | não existe no original (ver 5.3) |
| Menu principal | ✅ | ver 5.6 |
| Seleção de música / resultado (animações) / BGA/VSL / `Stage.cfg` | ❓ | não comparados por dentro |

### 5.2 Efeitos sonoros (corrigido)
O original embute **35 sons WAVE** no `.exe` (ids PE 148…182; PCM 8-bit 22 kHz mono). O port os lê de `WAVE/*.wav`,
pasta que **não existia** na instalação: os 15 efeitos que ele usa (`3-2, 2-1, 4-2, 8-1, 5-1, RANK_A…F, 10-2, 7-1,
01-1, COIN2, 10-1`) ficavam **mudos** (15 × `failed to open` no log). Agora `tools/extract_wave.py` extrai os 35
sons do `PUMPY.EXE` (descompactado) para `WAVE/`, e o log mostra 0 falhas.
- Nomes: lista alfabética de `src/resources.rc` com deslocamento fixo de 48 (ids 100…134 → 148…182). Indício forte
  (o carregador `0x4184f0` percorre `171, 173, 2-1, 3-2 … 9-5` em ids consecutivos), **mas não conferido de ouvido**.
- Os outros 20 (`GOODJOB1-3`, `NOTGOOD1-4`, `9-x`, `12-1`, `13-1`, `14`, `171`, `173`, `01-11`, `6-2`) têm slots que
  nenhuma função do original toca fora do carregador (só a tela de resultado toca os `RANK_*`), então parecem sobras.

### 5.1 EEPROM / persistência (corrigido)
O original persiste uma imagem binária de **2048 bytes** (`c:\pumpprex3.ini`, `fread(…,1,0x800,…)`).
Agora o port grava `pumpprex3.ini` na pasta do jogo, no mesmo formato (`src/eeprom.c`):

| Offset | Campo | Conferido em |
|---|---|---|
| +0x68C | ident `0x312E3358` ("X3.1") | 0x404f4b, 0x4067f4 |
| +0x690 | checksum **Adler-32** dos 8 bytes de +0x7E8 (`0x419570`, módulo 0xFFF1) | validado contra `zlib.adler32` |
| +0x7E8…7ED | GAME MODE, LEVEL, STAGE BREAK, LANGUAGE, DEMO SOUND, SHOW HELP (defaults 0,1,**2**,1,0,0) | 0x404f40, menu 0x405910 |
| +0x7EE/7EF | COIN 1 (default **5**), COIN 2 (1) | 0x404fa0 |
| +0x7F0/7F4/7FC | bookkeeping COIN 1, COIN 2, SERVICE | tela 0x406360 |
| +0x7F8 | total de moedas (mapeamento por eliminação) | ⚠️ não confirmado por rótulo |

Comportamento igual ao original: arquivo ausente → defaults e grava; ident errado → `Warning - ident mismatch`,
reset e regrava; checksum errado → `Warning - EEPROM chksum err` e reset **sem** regravar.
**Diferenças deliberadas:** default de COIN 1 = 0 (FREE PLAY) em vez de 5, para o jogo não ficar sem crédito no PC;
o `PUMPY.INI` antigo é **importado uma vez** e agora guarda só o `AudioOffset` (extensão do port; o original não tem).
O `eeprom.dat` de 30 bytes é o padrão do menu EEPROM TEST (`0x4422e4`), fiel ao original, não a persistência.

### 5.3 Máquina de estados do original (despachante `0x4029a3`, tabela `0x402b44`, 38 estados `0x00–0x25`)

| Estado | Handler | Vai para | Arquivos / função |
|---|---|---|---|
| 0x00 BOOT | 0x4020a0 | 4, A | |
| 0x01→0x02→0x03 AVISO | 0x403ee0, 0x403f30, 0x403f60 | 2, 3, 0 | |
| 0x04 LOGO (entra) | 0x403f90 | 5 | `BGA\81.DAT` |
| 0x05 LOGO (atualiza) | 0x404270 | 0x11 (serviço) | |
| 0x07 HOW TO PLAY | 0x4040e0 | 8 | `BGA\03.DAT` (o port usa 0x86) |
| 0x0A MENU (entra) / 0x0B MENU (entrada) | 0x404010 / 0x404320 | B / C, 0x11, **0x1F**, **0x23** | `BGA\82W.DAT` |
| 0x0D ENTRA NA SELEÇÃO | 0x412750 | 0x1B | |
| 0x0E GAME INIT / 0x0F GAMEPLAY | 0x410cf0 / 0x414820 | F / 0x11, 0x16, 0x18 | `Now Loading`, `main.vsl` |
| 0x11 ENTRA NO SERVIÇO / 0x12 SERVIÇO | 0x404ee0 / 0x4066e0 | 0x12 | |
| 0x14–0x17 (ranking/game over) | 0x412400, 0x412440, 0x412480, 0x412520 | 0x18, 0x17, 0x16/0x17, 0x17 | `BGA\84.DAT` |
| 0x18 NOTA (entra) / 0x19 RESULTADO | 0x415970 / 0x415020 | 0x19 / 0x11, 0x1A | `BGA\83.DAT`, `AUDIO\83.AUD` |
| 0x1A TRANSIÇÃO | 0x4125e0 | 0, 0x0D, 0x17 | `BGA\LT03.DAT` |
| 0x1B–0x1E SELEÇÃO DE MÚSICA | 0x40abe0, 0x4092e0, 0x409720, 0x4091a0 | 1C, 0x11/1D, 0x11/1E | `BGA\099.DAT`, sprites de dificuldade |
| 0x1F–0x21 GAME OPTION | 0x415ad0, 0x415b30, 0x415ba0 | 20, 21, A/0x11 | `BGA\086.DAT` |
| 0x22 SAÍDA | 0x415b70 | | encerra (`PostMessage WM_CLOSE`) |
| 0x23–0x25 STAFF | 0x4041b0, 0x404c20, 0x404240 | 24, 0x11, A | `BGA\STAFF.DAT`, `AUDIO\84.AUD` |

Os nomes do port coincidem no fluxo (logo, menu, opções, staff, game over, nota, transição). Correções de nome no
`pumpy.h` (sem efeito no código): `0x1B–0x1E` eram chamados de "BATTLE" mas são **subestados da seleção de música**;
`0x0D` é a entrada da seleção; `0x11` é a entrada do serviço (o resultado é `0x19`). **Não existe "battle" no original.**

### 5.4 Fim de jogo (corrigido, `0x4149ee–0x414a77`)
STAGE BREAK é um número (0=OFF, 1…4), não booleano: a checagem de life só vale se `opção ≠ 0`, `(opção−1) ≤ estágio
atual (0-based)` e não é o 1º estágio (o 1º nunca falha por life). Com 2 jogadores só termina quando **ambos** têm
life < 1. `missCombo > 50` vale **sempre**. O port tratava a opção como liga/desliga e falhava no 1º estágio.
Não testado em partida real.

### 5.5 Barra de life (corrigido, `0x411c6b`)
`displayF = life × 0,001 − (1 − pulso) × k`, limitado a `[0,1]`, `k = 0,1` nos modos simples e `0,05` em
HD/Double/Nightmare; a largura usa `_ftol` (trunca). **A barra cheia é life = 1000** (life inicial 500 = meia barra); o
port usava `life/500` (barra cheia no início). O `× 0,59` da largura do HalfDouble não foi aplicado (geometria própria
do port). Não confundir: o passo de 50/frame em `0x414888` suaviza a **velocidade de scroll** (`[0x442718]→[0xda2294]`),
não o life; **não há suavização do life exibido no original**.

### 5.6 Menu e seleção
- **Menu principal**: UL=iniciar, UR=GAME OPTION (`0x1F`), DL=STAFF (`0x23`), DR=EXIT (`WM_CLOSE`). O port já é fiel.
- **Seleção de música**: no original, botões de navegação aceitam aperto OU **segurado quando o contador de animação
  `[0xd5fd34] > 0x28`** (auto-repetição). O port só reage a aperto novo. **Ainda não implementado.**
- **Formato `.STX`**: a estrutura por chart bate com a do original (`+0x00 bpm`, `+0x08 beatSplit` inteiro,
  `+0x0C delay` em centésimos de segundo, `rowCount` em `+0x80`).

## 6. Limitações desta análise

- Detecção de funções é heurística (varredura linear + `call`); não substitui Ghidra/IDA.
- A regra das janelas está confirmada por leitura do assembly (incluindo `_ftol` e a constante
  float32), mas **não foi validada rodando o original** (sem Wine/máquina Windows aqui).
- O padrão de teclas com Home/PgUp/End/PgDn junto ao numpad é decisão de compatibilidade
  nossa; o original só reconhece o numpad com NumLock desligado.

## 7. Build multiplataforma (Windows / Linux)

| Item | Estado |
|---|---|
| Backend único: SDL2 (janela, entrada, áudio) + OpenGL 1.1/GLU | ✅ igual nas duas plataformas; todas as funções GL usadas são do GL 1.1 |
| `CMakeLists.txt` portável (`find_package` SDL2/OpenGL/zlib; fallback vendorizado só no Linux) | ✅ testado no Linux (clang via CMake e gcc direto) |
| Camada de compatibilidade Win32 (`platform_linux.h`, `platform_posix.c`) | ✅ só compilada fora do Windows; no Windows valem `windows.h` + `winmm` |
| DirectSound | removido dos headers; áudio já era SDL2 |
| `GL_CLAMP_TO_EDGE` (GL 1.2) | ✅ fallback definido para o `gl.h` da Microsoft |
| Ponto de entrada no Windows | `main()` + `SDL_MAIN_HANDLED` + `/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup` (ou `-mwindows`) |
| CI (`.github/workflows/build.yml`) compila Linux e Windows (vcpkg) | ⚠️ **escrito, ainda não executado**: sem MinGW/Wine nesta máquina |
| Compilação real no Windows | ⚠️ **não verificada** (código passou em `-std=c11 -pedantic -Wvla` no gcc, sem extensões GNU) |
