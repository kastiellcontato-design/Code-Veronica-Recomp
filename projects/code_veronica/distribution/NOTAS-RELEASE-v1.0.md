# Code Veronica Recomp Release v1.0

## Português

Primeira versão pública jogável do port para Windows x64 de **Resident Evil Code: Veronica X HD**, baseada na versão de PS3.

### Downloads

| Arquivo | Conteúdo |
|---|---|
| **CodeVeronica-PC-v1.0.zip** | Jogo/launcher do port, executáveis das três regiões e instruções em português e inglês. Não inclui os arquivos do jogo original. |
| **Veronica-PKG-Extractor-v1.0.zip** | Ferramenta opcional para extrair seu PKG de Code Veronica em uma pasta compatível com o launcher. |
| **CodeVeronica-LiveSplit-Speedrun-v1.0.zip** | Splits prontos de Any% e S-Rank com ícones e instruções para ativar o autosplitter pelo catálogo oficial do LiveSplit. Não é necessário selecionar um ASL manualmente. |

Extraia o ZIP completo do port e abra **Code Veronica.exe**. Há apenas um executável principal na pasta inicial; os executáveis regionais e as DLLs ficam em `bin`. A abertura não exige CMD. A correção do log compartilhado foi mantida e verificada para permitir a passagem do launcher ao executável regional.

### Recursos e correções

- Opção de 60 FPS com interpolação visual e geração de quadros FSR 3.1 opcional.
- Controles configuráveis, ajustes gráficos e volumes separados de música/vozes e efeitos.
- Troca de aparência com preparação dos modelos a partir dos arquivos do próprio usuário. Corrigidas as texturas ausentes ao trocar a skin com Chris, voltar ao título e começar um jogo novo com Claire.
- Pacotes de texturas PNG/DDS e exportação de texturas em PNG ou DDS.
- SRT integrado e ferramentas para treino; integração com LiveSplit, splits configuráveis e Load Remover Time em **Game Time**.
- Interface, guias e licenças incluídos. Capturas técnicas de desenvolvimento ficam desativadas nesta distribuição.

### Preparação do jogo

Use **Veronica PKG Extractor ou RPCS3** para extrair seu próprio jogo. No launcher, selecione a pasta que contém `PARAM.SFO` e `USRDIR/EBOOT.BIN`; não selecione o PKG diretamente.

Compatibilidade: versões PSN base **01.00**, **NPEB00553 (Europa)**, **NPUB30467 (USA)** e **NPJB00135 (Japão)**. EU/US precisam da sua licença RAP; JP não precisa. Nenhum jogo original, PKG, licença RAP, firmware ou save pessoal está incluído.

O autosplitter original é de **Kapdap**, adaptado para o port por **Kastiel**, com licença e créditos preservados. O autosplitter já está no catálogo oficial do LiveSplit: em **Edit Splits → Game Name**, digite exatamente **Resident Evil: Code: Veronica X (PS3 Recomp)** e clique em **Activate → Settings**. Use **Compare Against → Game Time** para descontar os carregamentos. O ZIP opcional mantém os splits prontos com ícones e os presets manuais. A aceitação do port em rankings de speedrun depende das regras e dos moderadores de cada categoria.

O port continua em desenvolvimento: podem existir diferenças gráficas e de iluminação, e 60 FPS dependem do computador. Se encontrar um problema, envie uma descrição, a região do jogo e o `bin/run-log.txt` em uma issue. Os arquivos `.sha256` são opcionais e permitem conferir a integridade dos downloads.

## English

First public playable Windows x64 release of the **Resident Evil Code: Veronica X HD** PC port, based on the PS3 version.

### Downloads

| File | Contents |
|---|---|
| **CodeVeronica-PC-v1.0.zip** | PC port/launcher, all three regional executables, and English/Portuguese instructions. Original game files are not included. |
| **Veronica-PKG-Extractor-v1.0.zip** | Optional standalone tool to extract your Code Veronica PKG into a launcher-compatible folder. |
| **CodeVeronica-LiveSplit-Speedrun-v1.0.zip** | Ready-made Any% and S-Rank split files with icons and official LiveSplit catalog setup guides. No manual ASL selection is required. |

Extract the entire port ZIP and open **Code Veronica.exe**. There is one main executable at the top level; regional executables and DLLs are under `bin`. No terminal is required. Shared logging has been retained and checked so launching another regional executable does not fail because the log is already open.

### Features and fixes

- Optional 60 FPS mode with visual interpolation and optional FSR 3.1 frame generation.
- Configurable controls, graphics settings, and separate music/voice and sound-effect volumes.
- Appearance switching with models prepared from the user's own game files. Fixed missing skin textures after changing Chris's appearance, returning to the title, and starting a new Claire game.
- PNG/DDS texture packs and selectable PNG or DDS texture export.
- Built-in SRT and practice tools; LiveSplit integration, configurable splits, and Load Remover Time using **Game Time**.
- Guides and licenses included. Developer diagnostic captures are disabled in this distribution.

### Game setup

Use **Veronica PKG Extractor or RPCS3** to extract your own game. Add the folder containing `PARAM.SFO` and `USRDIR/EBOOT.BIN` to the launcher, rather than the PKG itself.

Supported: PSN base version **01.00**, **NPEB00553 (Europe)**, **NPUB30467 (USA)**, and **NPJB00135 (Japan)**. EU/US require your RAP license; JP does not. No original game files, PKGs, RAP licenses, firmware, or personal saves are included.

Original autosplitter by **Kapdap**, adapted to the PC port by **Kastiel**; original license and credits retained. The autosplitter is now in the official LiveSplit catalog: in **Edit Splits → Game Name**, enter exactly **Resident Evil: Code: Veronica X (PS3 Recomp)**, then click **Activate → Settings**. Select **Compare Against → Game Time** for load removal. The optional ZIP retains ready-made split lists with icons and catalog setup instructions. Speedrun leaderboard eligibility is determined by each category's rules and moderators.

The port is still in development: graphical/lighting differences may remain, and 60 FPS depends on the PC. Please report issues with a description, game region, and `bin/run-log.txt`. Optional `.sha256` files let you verify download integrity.
