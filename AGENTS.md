# OpenCode Instructions

## Project Objective

This project is a reverse engineering and reconstruction effort.
**The objective is NOT to redesign, modernize or improve the original game.**
The objective is to reproduce the original executable as faithfully as possible.

### Priority order:

1. Original behavior
2. Binary compatibility whenever possible
3. Equivalent logic
4. Equivalent rendering
5. Equivalent timing
6. Code readability (only when it does not alter behavior)

> **Note:** If there is a conflict between cleaner code and compatibility with the original executable, compatibility always wins.

---

## Language

* Always respond in Português (Brasil).
* Use technical terminology whenever appropriate.

---

## Thinking Process

Before answering:

1. Observe
2. Analyze
3. Build hypotheses
4. Search for evidence
5. Reach a conclusion

**Never** skip the verification step.

---

## Confidence Levels

Every conclusion should implicitly fit one of these categories:

* Confirmed
* Highly Probable
* Probable
* Hypothesis

**Never** present assumptions as facts. Whenever evidence is insufficient, clearly state the uncertainty.

---

## Development Philosophy

* **Never** refactor simply because the code looks old.
* **Never** modernize APIs.
* **Never** replace legacy algorithms without evidence.
* **Never** optimize code unless explicitly requested.
* Prefer preserving original behavior over writing cleaner code.

---

## Programming Languages

**Preferred languages:**

* C
* Python
* PowerShell

Lua may be used when appropriate. Avoid introducing unnecessary C++ abstractions.

---

## Coding Style

* Prefer procedural code.
* Avoid unnecessary abstractions.
* Avoid unnecessary classes.
* Avoid templates.
* Avoid macros unless already used.
* Prefer small isolated patches.
* **Never** rewrite an entire subsystem to solve a localized issue.

---

## Reverse Engineering

Always prioritize evidence. Decompiler output is **NOT** authoritative. Assembly always has higher priority than decompiled C.

When analyzing unknown behavior, follow this order:

1. Strings
2. Imports
3. Cross References
4. Global Variables
5. Structures
6. Assembly
7. Decompiled C

Avoid jumping directly into decompiled code.

---

## Decompiled Code

Decompiler generated symbols such as:

* `FUN_xxxxxxxx`
* `DAT_xxxxxxxx`
* `PTR_xxxxxxxx`
* `LAB_xxxxxxxx`

...should be preserved unless explicitly requested.

* **Never** rename functions automatically.
* **Never** infer semantics solely from variable names.

---

## Binary Analysis

When investigating binary formats always identify:

* Magic
* Version
* Header Size
* Flags
* Endianness
* Alignment
* Padding
* Offset Tables
* Pointer Tables
* Checksums
* Compression
* Encryption

**Never** stop after identifying only the header.

---

## Compression

**Never** assume encryption first. Always verify:

* RLE
* LZ77
* LZSS
* Deflate
* Zlib
* LZO
* Huffman
* XOR

Only after excluding common compression methods should encryption be considered.

---

## Structures

**Never** modify:

* Structure packing
* Offsets
* Field ordering
* Integer sizes
* Alignment

...unless there is concrete evidence that they are incorrect. Always preserve binary compatibility.

---

## OpenGL

* Preserve original rendering order.
* Avoid changing draw order.
* Avoid introducing state changes unnecessarily.
* **Never** assume OpenGL state.
* Preserve original rendering pipeline whenever possible.

---

## Legacy Windows

Assume the original executable targets:

* Windows 95
* Win32 API
* OpenGL
* DirectSound
* Visual C

Timing differences between Windows 95 and modern Windows should always be considered.

---

## Ghidra

When using Ghidra:

1. Prefer Strings.
2. Then Imports.
3. Then Xrefs.
4. Only inspect functions relevant to the current task.
5. Request only one new function at a time.
* Avoid recursive exploration.
* Avoid mass decompilation.
* **Never** request the entire executable.

---

## Context Usage

* Minimize token consumption.
* Do not request information already analyzed.
* Do not request files already provided.
* Prefer summaries over duplicated code.
* Reference functions by address whenever possible.

---

## Code Modification Workflow

Before modifying any code:

1. Explain what the code currently does.
2. Explain why it should change.
3. Explain possible side effects.
4. Only then propose modifications.

---

## Data Analysis

When comparing binary files:

* Identify common regions.
* Identify changed regions.
* Identify pointer differences.
* Identify checksum changes.
* Identify alignment changes.

**Never** classify changed bytes as random without evidence.

---

## Python

Python scripts should:

* Accept command-line arguments.
* Support recursive processing when appropriate.
* Avoid unnecessary dependencies.
* Print meaningful diagnostics.
* Prefer standard library.

---

## PowerShell

* Always assume a Windows environment.
* **Never** generate Linux shell commands.
* **Never** generate Bash scripts unless explicitly requested.
* Prefer native PowerShell commands.

---

## Documentation

Separate information into:

* Facts
* Evidence
* Hypotheses
* Open Questions

Avoid mixing speculation with confirmed information.

---

## Response Style

* Prefer technical explanations.
* Avoid generic advice.
* Avoid motivational language.
* Avoid repeating previous explanations.
* If multiple interpretations exist, explain each.

---

## Build Process

**Compilation:**

```sh
cmake --build build
```

Assets are copied automatically.

**After building:**

1. Copy: `PUMPY.EXE`
2. Rename to: `PUMPYTESTE.EXE`
3. Copy it to: `E:\Pumps\PREX3-Original` (for validation using original game assets).

**After build and copy process, always ASK if I want to continue another thing.**

---

## Logging

Use:

```c
Log_Print()
```

Log location: `root/pumpy.log`

---

## Original Executable

Original executable characteristics:

* **System:** Windows 95 (32-bit)
* **Compiler:** Microsoft Visual C 13.00
* **Language:** C
* **Graphics:** OpenGL
* **Audio:** DirectSound
* **Protection:** PELock 2.x

> **Note:** Decompiler output may contain artifacts caused by PELock.

---

## Rendering

* **Projection:** Y-UP
* **External API:** Y-DOWN

Sprites rendered by `renderSPTile` must always invert the V coordinate using `256 - v` unless explicitly proven otherwise.

### Global Color Overlay (Render_EndScene)

- `Render_SetGlobalColor(r, g, b, a)` controla o overlay fullscreen desenhado por `Render_EndScene`
- O overlay só aparece quando `globalColorA > 0.0f`
- **NUNCA** chame `Render_SetGlobalColor(1,1,1,1)` em telas novas — isso produz overlay branco opaco que tampa tudo (`alpha=1` = opaco)
- Ao criar uma nova tela, omita `Render_SetGlobalColor` ou passe `alpha=0` para não sobrescrever o overlay
- **IMPORTANTE:** Se você setar `alpha=1`, o overlay preto tapa TUDO. A cada tela, verifique se o `alpha` está sendo atualizado corretamente ao longo dos frames (fade-in/fade-out). Caso contrário a tela fica preta e parece que o conteúdo não carregou.

---

## BGA Parsing

**Supported formats:** `BGA`, `BGA2`
Detect version through magic bytes.

Shared parsing structures must use:

```c
#pragma pack(push,1)
```

---

## Project Conventions

* Shared structures belong in: `include/pumpy.h`
* Every STATE screen should default to: `RGBA(0,0,0,1)` unless the original executable behaves differently.
* Whenever `.tga` resources are referenced, project assets should use `.png`.
* All DAT resources are already extracted inside: `BGA_extracted`
* **Never** decrypt any DAT file — all are already extracted in `BGA_extracted`
* **Never** propose extracting DAT files again unless explicitly requested.
* `81.DAT` → Somente animação do LOGO, não mexer
* `82W.DAT` → Animações da tela de menu, não mexer
* SPR files contain sprite coordinate definitions, not textures.
* Use `C:\Users\bruno\AppData\Local\Temp\parse_bga3.py` to analyze entry names and structure of BGA files. It parses the fixed-record format correctly (64-byte name fields, count, keyframes).

---

## Critical Rules

- **Always verify in GhidraMCP** before implementing any format fix — never base corrections on format hypotheses alone. The decompiled original always has priority.
- When analyzing the original, follow the data flow: what does it READ from the file/buffer? The read pattern reveals the actual format (e.g., fixed 64-byte records vs variable-length strings).

## Reverse Engineering Rules

**Never** request:

* The entire executable
* Every function
* All globals
* Large unrelated code blocks

Only request information strictly necessary for the current objective. Always justify why additional information is needed.

---

## Resource Mapping

**NUNCA** assuma qual arquivo DAT/BGA/AUD/WAV corresponde a qual tela ou recurso. As numerações nem sempre são óbvias (ex: Song Select é `099.DAT`, não `SELECT.DAT`). Sempre pergunte ao usuário antes de tirar conclusões sobre nomes de arquivos ou mapeamentos de recursos.

## Investigation Checklist

Before concluding any investigation verify:

- [ ] Original behavior identified
- [ ] Related globals identified
- [ ] Related structures identified
- [ ] Related strings identified
- [ ] Related xrefs identified
- [ ] Related rendering identified
- [ ] Compression checked
- [ ] Encryption checked
- [ ] Side effects evaluated

Only after completing this checklist should a final conclusion be presented.

---

## Session Summary — Staff Screen (DL 2x)

### Goal

Implementar tela de Staff (créditos) ativada via DL 2x no menu.

### Done

- `src/staff.c` criado com `Staff_Enter()` e `Staff_Update(float dt)`
- `Staff_Enter()`: `Resource_LoadBGADirect("BGA\\STAFF.DAT")` carrega RES com Staff.bga + S.SPR, S00.spr + staff01~08.png; `BGM_LoadAUDDirect` toca 84.AUD; `bgaLoop=false`, `bgaFrame=0`, `frameCounter=0`
- `Staff_Update()`: incrementa `frameCounter` até 1920, sincroniza `bgaFrame`. ESC/CENTER ou fim → `BGM_Stop()` + `Menu_ResetState()` + `Game_ChangeState(STATE_MENU_ENTER)`
- `LoadBGAForState`: `STATE_STAFF_ENTER/STAFF/STAFF_END` → `bgaName = ""` (limpa BGA)
- `Game_Update`: cases `STATE_STAFF_ENTER` (Staff_Enter), `STATE_STAFF` (Staff_Update), `STATE_STAFF_END` (volta menu). `manualBGA` inclui `STATE_STAFF`. ESC handler trata STAFF.
- `Game_Render`: `STATE_STAFF` usa `BGA_Render` padrão (não excluído)
- `menu.c`: DL 2x confirma → fade → `STATE_STAFF_ENTER`
- `CMakeLists.txt`: adicionado `src/staff.c`
- `resource.c`: parser BGA2 aceita `.png`/`.PNG` além de `.spr`/`.sp2`/`.tga`/`.TGA`
- Removido `Render_SetGlobalColor(1,1,1,1)` de Staff_Enter (causava overlay branco)

### Key Fixes

1. **Tela branca**: causada por `Render_SetGlobalColor(1,1,1,1)` em Staff_Enter — overlay branco full-alpha tampava o BGA
2. **File path**: `Resource_LoadBGADirect("BGA\\STAFF.DAT")` funciona do diretório raiz do jogo (`E:\Pumps\PREX3-Original\`) sem precisar de fallback `assets/`

### Known

- RES entries usam XOR contínuo (key 0xEF, step 0x4F) sem reinício entre entries — decodificação funciona corretamente
- `loadBGAFromRES` (usado por `Resource_LoadBGAByName`) NÃO funciona pra Staff.bga (offset 16 não tem filenames); `Resource_LoadBGADirect` (scan de extensões) funciona
- Staff.BGA2 tem 2 layers: S00.spr (frames 0-325) e S.spr (frames 115-1891)
- S.SPR: 10 tiles, S00.spr: 2 tiles, todos referenciando `staffXX.tga` resolvido via `loadTextureFromRES` para `.png`
- Texturas staff01~08.png: 256x256 cada
