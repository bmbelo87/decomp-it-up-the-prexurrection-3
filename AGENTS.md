# Instruções do OpenCode

## Linguagem de Fala

- Sempre responda em Português-Brasil

## Compilação e Execução

- Compilação: `cmake --build build` (a partir da raiz do repositório).
- Os recursos (*assets*) são copiados automaticamente para o diretório de compilação (*build*).
- Lembre-se de sempre fazer a cópia do arquivo buildado (PUMPY.EXE), renomea-la para PUMPYTESTE.EXE e enviar para a pasta root do projeto "E:\Pumps\PREX3-Original" para testarmos com os itens originais do jogo.
- Lembre-se de quando usar terminal, estamos em ambiente Windows, terminal aqui é só com códigos powershell, nunca coloque comandos terminal linux pois não vai rodar, o mesmo para comandos cmd.

## Peculiaridades Técnicas

- **Sistema de Coordenadas**: As texturas do OpenGL são invertidas verticalmente pelo carregador (*loader*). Toda renderização de SPR em `renderSPTile` DEVE usar `(256 - v)` para as coordenadas V, a fim de garantir a orientação correta.
- ***Parsing* de BGA**:
  - `BGA2`: Versão 2, identificada pelos bytes mágicos "BGA2".
  - `BGA`: Versão 0, identificada pelos bytes mágicos "BGA".
- **Logs (Registros)**: Use `Log_Print` para depuração; a saída pode ser encontrada em `build/Debug/pumpy.log`.

## Estilo e Convenções

- Definições e estruturas compartilhadas residem em `include/pumpy.h`.
- Mantenha o empacotamento binário estrito para as *structs* que fazem o *parsing* de arquivos BGA (ex.: `#pragma pack(push, 1)`).

# Descobertas de Engenharia Reversa (acumuladas)

## Funções Renomeadas no Ghidra

| Endereço | Nome | Descrição |
|----------|------|-----------|
| 0x00402850 | `Game_MainLoop` | Loop principal: `Render_ClearScreen()` → `switch(g_dwState)` com 26+ cases → `SwapBuffers()` |
| 0x004014d0 | (anônima) | Pipeline de negação Y nas coordenadas de renderização |
| 0x004012e0 | `BGA_LoadFile` | Carrega BGA: lê magic BGA2/BGA, roteia para parser v0 ou v2, carrega SPR por layer |
| 0x004190f0 | `Audio_LoadADPCM` | Carrega AUDIO\\%s.AUD, escreve temp, decodifica ADPCM |
| 0x00403f30 | `Warning_Animate` | `BGA_SetEventFrame(events, g_dwFrameCounter++)`; se >899 → state=3 |
| 0x00404320 | `Menu_UpdateInput` | Input + multi-pass render (7 layers estáticas + background + overlay + modo) |
| 0x004191a0 | `Anim_ConfirmEnter` | Animação de confirmação (enter) |
| 0x004191c0 | `Anim_ConfirmLeave` | Animação de confirmação (leave) |
| 0x00401890 | `Render_SetGlobalColor` | Seta cor global de renderização |
| 0x004269b0 | `Math_ROUND` | Arredondamento matemático |
| 0x0040c0f0 | (anônima) | Desenha retângulo preto/branco (fade overlay?) |
| 0x00403e50 | (anônima) | Desenha texto na tela (usado no menu para copyright) |

## Mapeamento de Estados (g_dwState)

| State | Nome | Função | Descrição |
|-------|------|--------|-----------|
| 0x00 | `STATE_BOOT` | `State_Branch()` (FUN_00409380?) | Boot: decide se vai para Warning, Logo ou Menu (baseado em `g_dwBootCounter`) |
| 0x01 | `STATE_WARNING_INIT` | `Warning_Enter()` (FUN_00409720) | Inicializa Warning (carrega BGA, configura) |
| 0x02 | `STATE_WARNING_ANIM` | `Warning_Animate()` (00403f30) | Anima: `BGA_SetEventFrame(events, frame++)`, max 900 → state 3 |
| 0x03 | `STATE_WARNING_END` | `Warning_End()` (FUN_004054c0) | Finaliza Warning |
| 0x04 | `STATE_LOGO_ENTER` | `Logo_Enter()` (FUN_00403a80?) | Inicializa Logo |
| 0x05 | `STATE_LOGO_UPDATE` | `Logo_Update()` | Atualiza Logo, transiciona para ResetFlow |
| 0x06 | `STATE_RESET_FLOW` | `State_ResetFlow()` | Reset do fluxo (carrega 82W, configura menu) |
| 0x07 | `STATE_MENU_FADE_IN` | `Menu_FadeEnter()` | Fade in do menu |
| 0x08 | `STATE_MENU_IDLE` | `Menu_IdleUpdate()` | Menu idle (60f) |
| 0x09 | `STATE_MENU_INPUT_WAIT` | `Menu_InputReset()` | Aguarda input (30f) |
| 0x0A | `STATE_MENU_ENTER` | `Menu_Enter()` | Menu inicializando (82W resetando) |
| 0x0B | `STATE_MENU_INPUT` | `Menu_UpdateInput()` (00404320) | Menu real: loop eterno, input + multi-pass render |
| 0x0C | (reset) | `State_ResetToWarning()` | Volta ao Warning |
| 0x0D | TryNextStage | `FUN_00412750` | Vídeo de Try Next Stage |
| 0x0F | Gameplay | `FUN_00414820` | Carrega 00.DAT, XXX.DAT, XXX.AUD, XXX.STX |
| 0x19 | DanceGradeResult | `FUN_00415020` | Resultado normal de dança |
| 0x1E | Title | `FUN_004091a0` | Title.PNZ da música selecionada |
| 0x16 | Stage Break | `FUN_00412480` | Tela de Stage Break |
| 0x1F | GameOption transição | `FUN_00415ad0` | Saindo do Menu, entrando no GameOption |
| 0x20 | GameOption sequência | `FUN_00415b30` | Sequência de entrada no GameOption |
| 0x21 | GameOption inside | `FUN_00415ba0` | Dentro do GameOption |
| 0x23 | GameplayEnter | `GameplayEnter()` | Entrada no gameplay |

## Arquitetura de Renderização Multi-Pass (ORIGINAL)

O original NÃO renderiza em um único passe. `Menu_UpdateInput` (00404320) faz múltiplas chamadas sequenciais a `BGA_SetEventFrame`/`BGA_SetEventLayer` no MESMO frame do jogo. Cada chamada desenha imediatamente via OpenGL, sobrescrevendo o que foi desenhado antes para as camadas afetadas. Como cada faixa de frame tem keyframes diferentes ativos, layers diferentes ficam visíveis em cada passe.

### Sequência exata (state 0x0B):

1. **Passe 1 - Layers estáticas**: `BGA_SetEventLayer(events, frameCounter % 415, layerIdx)` para layers 1, 2, 13, 19, 5, 29, 48
2. **Passe 2 - Background**: `BGA_SetEventFrame(events, (frameCounter % 120) + 1020)` — fundo animado (1020-1139)
3. **Passe 3 - Overlay de seleção**: `BGA_SetEventFrame(events, (frameCounter % 120) + 900)` — quando `g_dwMenuMode != 0` (900-1019)
4. **Passe 4 - Específico do modo**:
   - UL (modo 1): `BGA_SetEventFrame(events, (frameCounter % 120) + 420)` (420-539)
   - UR (modo 3): `BGA_SetEventFrame(events, (frameCounter % 120) + 660)` (660-779)
   - Center (modo 7): `BGA_SetEventLayer(events, (frameCounter % 120) + 300, 21)` e `BGA_SetEventLayer(events, (frameCounter % 120) + 300, 32)` (300-419)
   - DL (modo 9): `BGA_SetEventFrame(events, (frameCounter % 120) + 540)` (540-659)
   - DR (default): `BGA_SetEventFrame(events, (frameCounter % 120) + 780)` (780-899)

### Faixas de frame do 82W.DAT:
- 0-299: ?
- 300-419: Center (int_c/int_d? layers 21,32)
- 420-539: UL (Start)
- 540-659: UR (Options)
- 660-779: DL (Credits)
- 780-899: DR (Exit)
- 900-1019: Center selection overlay (16.spr)
- 1020+: Background continuation

### Warning_Animate (00403f30):
- Apenas `BGA_SetEventFrame(events, g_dwFrameCounter++)` — playback linear de TODOS os eventos (frame 0 → 899)
- Quando > 899 → state 3 (Warning_End)

## Globais Importantes

| Endereço | Nome | Descrição |
|----------|------|-----------|
| 0x00d387f0 | `g_dwFrameCounter` | Contador de frames (incrementado em Warning_Animate, Menu_UpdateInput) |
| 0x008bbf40 | `g_dwBootCounter` | Contador de boot (decide Warning/Logo/Menu) |
| 0x00d387e0 | `g_dwMenuMode` | Modo do menu (1=UL, 3=UR, 7=Center, 9=DL, outros=DR) |
| 0x00d387ec | `g_dwConfirmTimer` | Timer de confirmação (60 frames após segundo toque) |
| (flag) | `g_bConfirmActive` | Flag de confirmação ativa |
| (HWND) | `g_dwHWnd` | Handle da janela |

## Estruturas BGA

- **BGA v2**: Magic "BGA2", eventos fixos de 50 layers, keyframes de 64 bytes com timestamp no offset 44
- **BGA v0**: Magic "BGA", sem keyframes por evento (apenas frame a frame)
- `BGA_SetEventFrame(bgaIndex, frame)`: renderiza TODAS as 50 layers no frame dado
- `BGA_SetEventLayer(bgaIndex, layerIdx, frame)`: renderiza UMA layer no frame dado
- `interpolate_layer`: retorna NULL se `frame < keyframes[0].frame` (layer invisível)

## Outras Descobertas

- `BGA_LoadFile` (004012e0): Limpa 0x46ab9*4 bytes, verifica magic BGA2, roteia para parser, carrega SPR por layer
- `g_bootCounter%3==0` → Logo, `%3==1` → Menu, `%3==2` → ???
- Projeção OpenGL: Y-down (`gluOrtho2D(0, 640, 480, 0)`)
- 81.DAT maxFrame=465; R_WARN maxFrame=900; 82W.DAT maxFrame=1140+
