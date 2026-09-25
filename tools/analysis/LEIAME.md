# Análise do PUMPY.EXE original

O original é compactado com UPX. Descompacte **uma cópia** (nunca o original):

    upx -d -o pumpy_unpacked.exe PUMPY.EXE
    python3 -m venv venv && venv/bin/pip install pefile capstone

Rode os scripts nesta ordem, na pasta onde está o `pumpy_unpacked.exe`
(os caminhos do nosso código estão fixos no topo de `xref.py` / `strs2.py`):

| Script | Faz |
|---|---|
| `funcs.py` | descobre funções (prólogo + alvos de call), grava `starts.pkl` |
| `findstr.py REGEX...` | acha strings do original e quem as referencia (grava `strs.pkl`, `xr.pkl`) |
| `xref.py` | cruza os endereços `0x00xxxxxx` citados no nosso fonte com as funções do original |
| `strs2.py` | lista strings do jogo ausentes no nosso fonte |
| `pdis.py ENDERECO TAMANHO` | desmonta um trecho, anotando strings e imports |
| `refs.py LO HI` | funcoes que referenciam enderecos de dados no intervalo |
| `keymap.py` | decodifica a tabela de teclas da WndProc (VK -> variavel) |
| `compact.py IN OUT` | colapsa as verificacoes desenroladas de linha (util em 0x40e330) |
