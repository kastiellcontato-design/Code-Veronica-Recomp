# Pasta de saves e versão americana

## O que mudou

- A aba **Jogo** ganhou **Pasta dos saves**, com **Procurar**, **Abrir** e **Usar pasta padrão**. Traduzido nos 13 idiomas do launcher.
- A seleção é ligada ao sistema real de save/load, não apenas à interface. A pasta escolhida é a raiz que recebe as subpastas de saves criadas pelo jogo.
- Campo não configurado: mantém exatamente o padrão anterior, `./gamedata/dev_hdd0/home/00000001/savedata`, relativo à pasta de trabalho. Pelo iniciador oficial, corresponde a `D:\ps3recomp\projects\code_veronica\gamedata\dev_hdd0\home\00000001\savedata`.
- Mudar a pasta vale na próxima abertura do jogo; não redireciona uma gravação em andamento e não move, apaga ou converte saves existentes. Para continuar um save em outra pasta, copiar as subpastas de saves para ela antes de jogar.
- Caminhos com espaços e caracteres Unicode são suportados no sistema de saves do Windows. O launcher verifica criação e permissão de escrita antes de iniciar; não usa outra pasta silenciosamente se houver erro.
- Versão americana **NPUB30467 / 01.00** adicionada com executável próprio **MyGameRecomp_US.exe**, ao lado dos executáveis EU e JP. O launcher identifica PARAM.SFO e escolhe o executável correto; o SHA-1 do ELF ainda precisa coincidir com o perfil regional.
- PC Settings, pular logos/introdução e pular portas receberam os endereços americanos. FXC e DXC continuam as mesmas opções anteriores.
- A licença da região é procurada junto à licença selecionada, na pasta do jogo e em PS3_RAP_DIR. Isso permite usar o .rap americano presente na pasta indicada pelo usuário, sem confundi-lo com o europeu. Não muda a lógica de criptografia.

## Análise do EBOOT americano fornecido

Origem somente de leitura: `D:\Jogos\rpcs3-v0.0.33-17018-0a57c459_win64\dev_hdd0\game\NPUB30467\USRDIR\EBOOT.elf`.

- SHA-1: `93d060e5019d877df39cd091c28efff920b98e49`.
- SHA-256: `b81ed651b9de5e01acdbfc2e3b00a16e25191c972de63e3569495d46d5fcbcad`.
- Entrada OPD `0x00A5B890`, código `0x0026CC5C`, TOC `0x00A7AE70`.
- Tabela OPD: 17.045 funções, mesmo total e ordem da EU; 17.020 com o mesmo tamanho. Isso auxilia a análise, mas não substitui validar cada hook.
- Imports: 413 em 28 bibliotecas; gerados a partir deste ELF.
- Hooks localizados por correspondência de instruções, mascarando deslocamentos de branches e constantes de endereço; os trechos comparados preservam os opcodes e registradores.
- Os dados usados pelos hooks passaram de `0x00BBE8F8` (EU) para `0x00BAE5F8` (US), deslocamento `-0x10300`. Não confundir com o deslocamento JP `-0x10280`.
- Portas: instrução original `0x4BE8D5B5` em `0x001AE25C`; patch troca por `0x38600000`. Guard explícito no JSON.
- Fontes americanas geradas separadamente em `src/recomp_us/`, ignoradas pelo Git. Nenhum EBOOT foi modificado ou copiado para uma área versionada.

## Arquivos e integração

- `libs/system/cellSaveData.c/.h`: `cellSaveData_set_root`, caminhos UTF-8 e APIs de arquivo Unicode no Windows.
- `projects/code_veronica/main.cpp`: configura a raiz antes de executar código convidado, usando `PS3_SAVE_ROOT`.
- `launcher.cpp`: caminho salvo em `[pastas] saves` no `cv_launcher.ini`, seleção/validação e variável de ambiente. Com valor vazio, limpa o override e mantém o padrão legado.
- `launcher_i18n.h`: nova interface de saves e lista de três versões nos 13 idiomas.
- `cv_version.h`, `tools/cv_native_menu_hooks.py`, `patches.json`: perfil e hooks americanos, sem substituir os perfis EU/JP.
- `CMakeLists.txt` do projeto: `CV_REGION=US`, saída `_US` e teste opcional de saves.
- `build-release.cmd`: `CV_BUILD_US=1` compila a US em `build-release-us`; `CV_BUILD_JP=1` atualiza JP; `CV_SAVE_TESTS=1` executa testes de saves. O ponto de entrada de compilação continua sendo exclusivamente este script.
- Não usar build/build-x64/build-vs, não publicar arquivos do jogo, lifts, dumps, DLLs, executáveis, banner/ícone. Sem commit/push nesta entrega; branch gpt-experimentos.

## Como regenerar a versão americana

A partir da raiz do projeto, com o Python instalado:

```text
python tools/ppu_loader.py "<pasta NPUB30467>/USRDIR/EBOOT.elf" --output projects/code_veronica/codex_diag_us
python tools/ppu_lifter.py "<pasta NPUB30467>/USRDIR/EBOOT.elf" --functions projects/code_veronica/codex_diag_us/EBOOT.functions.json --hle-stubs projects/code_veronica/codex_diag_us/EBOOT.imports.json --patches projects/code_veronica/patches.json --title-id NPUB30467 -j 6 -o src/recomp_us
```

Depois definir `CV_BUILD_US=1`, opcionalmente `CV_BUILD_JP=1` e `CV_SAVE_TESTS=1`, e executar `projects/code_veronica/build-release.cmd`.

## Validação

- Teste com gravação/leitura real via callbacks: padrão, pasta personalizada e pasta com espaços, acento e caracteres japoneses. Também testa metadados, conteúdo e rejeição de caminho inválido sem trocar a raiz anterior. Tudo ocorre em diretórios temporários isolados, sem tocar saves reais.
- Compilação e teste de inicialização: resultados finais registrados no fim deste documento após a conclusão.
- Não afirmar que todos os cenários e saves das três regiões foram testados apenas porque a inicialização funciona.

## Resultado final — 2026-10-16

- `build-release.cmd` com `CV_BUILD_JP=1 CV_BUILD_US=1 CV_SAVE_TESTS=1` concluiu as três compilações, exit 0. Log final: `build-release/saves-us-final-build.log`. A primeira compilação americana completa está em `build-release/saves-us-build.log`.
- Executáveis finais em `build-release/bin`: EU 01:35:01, JP 01:35:08, US 01:35:13 (horário local).
- Testes de save/load passaram no padrão legado, pasta personalizada e pasta com espaços, acento e caracteres japoneses; rejeições de caminho longo/UTF-8 inválido mantêm a raiz anterior. Uma dependência do harness de teste foi corrigida com um stub de registro HLE; o runtime não foi alterado para contornar o teste.
- Medição GDI+ conferiu os textos de saves nos 13 idiomas: rótulo/botão cabem nas áreas existentes e os avisos cabem nas caixas de 655x52 px.
- Inicialização US real confirmada pelo log: título NPUB30467, entry OPD 0x00A5B890, TOC 0x00A7AE70, assets lidos da pasta americana fornecida. Teste iniciado com FXC e CV_NO_LAUNCHER=1, sem alterar a pasta de jogo salva no launcher.
- O usuário confirmou que estava controlando/testando essa janela. O log registrou mudanças de PC Settings feitas durante a sessão. Não encerrar a janela dele automaticamente.
- **Save real americano confirmado às 01:38:18:** `SAVE complete for NPUB304670`; DATA0.DAT de 31.876 bytes, ICON0.PNG, PIC1.PNG, PARAM.PFD e PARAM.SFO gravados em `codex_diag_us/boot/isolated-saves/NPUB304670`. É uma pasta separada de teste e foi informada ao usuário. Não apagar: pode conter o progresso que ele fez nessa sessão.
- Cópia do log observado: `codex_diag_us/boot/boot-observed.log`; a sessão ativa continua registrando em `build-release/bin/run-log.txt`, pelo redirecionamento de console que já existia no runner.
- Ao abrir pelo launcher normal/experimental, selecionar a pasta **NPUB30467** na aba Jogo. O launcher escolhe MyGameRecomp_US.exe; a pasta de saves configurada nessa aba é aplicada nessa nova sessão. Para continuar especificamente o save desta sessão de teste, selecionar `codex_diag_us/boot/isolated-saves` ou copiar sua subpasta de save para o destino desejado, com o jogo fechado.
- Hash SHA-256 do EBOOT original conferido novamente após a implementação: continua b81ed651b9de5e01acdbfc2e3b00a16e25191c972de63e3569495d46d5fcbcad. Arquivos originais do jogo não foram editados.
- Não houve teste de todos os cenários, slots de save ou carregamento manual de uma partida americana, nem benchmark novo. EU/JP foram recompilados, não tiveram nova validação visual completa nesta entrega. Alterações preexistentes em native_pc_menu.cpp e a integração FXC/DXC foram preservadas. Nenhum commit/push.
