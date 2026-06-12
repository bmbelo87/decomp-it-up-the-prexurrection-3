# Instruções do OpenCode

## Linguagem de Fala

- Sempre responda em Português-Brasil

## Compilação e Execução

- Compilação: `cmake --build build` (a partir da raiz do repositório).
- Os recursos (*assets*) são copiados automaticamente para o diretório de compilação (*build*).
- Lembre-se de sempre fazer a cópia do arquivo buildado (PUMPY.EXE), renomea-la para PUMPYTESTE.EXE e enviar para a pasta root do projeto "E:\Pumps\PREX3-Original" para testarmos com os itens originais do jogo.
- Lembre-se de quando usar terminal, estamos em ambiente Windows, terminal aqui é só com códigos powershell, nunca coloque comandos terminal linux pois não vai rodar.

## Peculiaridades Técnicas

- **Sistema de Coordenadas**: As texturas do OpenGL são invertidas verticalmente pelo carregador (*loader*). Toda renderização de SPR em `renderSPTile` DEVE usar `(256 - v)` para as coordenadas V, a fim de garantir a orientação correta.
- ***Parsing* de BGA**:
  - `BGA2`: Versão 2, identificada pelos bytes mágicos "BGA2".
  - `BGA`: Versão 0, identificada pelos bytes mágicos "BGA".
- **Logs (Registros)**: Use `Log_Print` para depuração; a saída pode ser encontrada em `build/Debug/pumpy.log`.

## Estilo e Convenções

- Definições e estruturas compartilhadas residem em `include/pumpy.h`.
- Mantenha o empacotamento binário estrito para as *structs* que fazem o *parsing* de arquivos BGA (ex.: `#pragma pack(push, 1)`).

# Engenharia Reversa

## Objetivo

Minimizar consumo de tokens durante análises.

## Regras Gerais

- Nunca solicitar o executável inteiro.
- Nunca solicitar todas as funções de uma vez.
- Analisar apenas funções relacionadas ao objetivo atual.
- Não repetir funções já analisadas anteriormente.
- Utilizar resumos em vez de reenviar código completo.
- Antes de solicitar contexto adicional, justificar sua necessidade.

## Uso do GhidraMCP

Caso o GhidraMCP tenha que ser utilizado:

- Priorizar Strings.

- Priorizar Imports.

- Priorizar Xrefs.

- Solicitar no máximo uma função por vez.

- Não expandir chamadas recursivamente.

- Não descompilar em massa.

- Caso necessário verificar mais de 1 função por vez, peça verificação manual pelo usuario indicando aonde encontrar a função, somente se ele permitir faça a verificação asincrona normalmente.

## Criptografia

- Identificar primeiro o algoritmo provável.
- Procurar constantes e tabelas.
- Testar hipóteses antes de solicitar mais código.
- Evitar solicitar funções auxiliares sem evidências de relevância.

## Contexto

- Manter um resumo acumulado das descobertas.
- Referenciar descobertas anteriores pelo endereço da função.
- Não reenviar assembly já conhecido.

# Outros

- Analisar a solicitação detalhadamente

- Não solicitar verificação de códigos que já foram verificados no mesmo projeto.
