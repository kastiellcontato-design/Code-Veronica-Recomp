<div align="center">

<img src="projects/code_veronica/launcher_art/launcher_logo.png" alt="Resident Evil Code: Veronica X Port" width="360">

# Resident Evil Code: Veronica X — Port para PC

**Port nativo e não oficial da versão de PlayStation 3 para Windows, feito por recompilação estática.**

[English](README.md) | **Português (Brasil)** | [Español](README.es.md)


Rejogue a versão de PS3 no PC. Treine rotas com SRT nativo, seleção de salas e saves em qualquer lugar.

[Preparar o jogo](#preparando-o-jogo) · [Treino de speedrun](#feito-também-para-treinar-speedrun) · [Funções](#funções) · [Galeria](docs/GALLERY.pt-BR.md)

</div>

> [!IMPORTANT]
> **Nenhum arquivo do jogo está incluído.** Este repositório e os seus lançamentos trazem só o port: nenhum dado do jogo, nenhum executável do jogo e nenhum código gerado a partir dele. Você precisa ter a **sua própria cópia original** do jogo de PS3 (versão da PSN). *Resident Evil*, *Code: Veronica* e todos os nomes, personagens e imagens relacionados são marcas e direitos autorais da **Capcom**. Este é um projeto de fã. Ele **não tem ligação com a Capcom nem com a Sony Interactive Entertainment, nem é autorizado ou endossado por elas**.

<p align="center">
  <img src="docs/images/gameplay-claire-red-room.jpg" alt="Resident Evil Code: Veronica X" width="880">
</p>

<details>
<summary><strong>Índice</strong></summary>

- [Treino de speedrun](#feito-também-para-treinar-speedrun)
- [O que é](#o-que-é)
- [Por que a versão de PS3](#por-que-a-versão-de-ps3)
- [Versões compatíveis](#versões-compatíveis)
- [Requisitos](#requisitos)
- [Preparando o jogo](#preparando-o-jogo)
- [Instalando e jogando](#instalando-e-jogando)
- [Funções](#funções)
  - [Patches](#patches)
  - [Mods de textura](#mods-de-textura-experimental)
- [Atalhos](#atalhos)
- [Capturas de tela](#capturas-de-tela)
- [Problemas conhecidos](#problemas-conhecidos)
- [Compilando o código](#compilando-o-código)
- [Rodando os testes](#rodando-os-testes)
- [Organização do repositório](#organização-do-repositório)
- [Créditos](#créditos)
- [Licença](#licença)

</details>

## O que é

Este é um port nativo para Windows x64 do **Resident Evil Code: Veronica X** de PlayStation 3. Não é um emulador. O código PowerPC (PPU) e SPU do jogo é traduzido antecipadamente para C/C++ com o [ps3recomp](https://github.com/sp00nznet/ps3recomp) e depois compilado como um executável normal do Windows. As bibliotecas do sistema do PS3 foram reimplementadas, e os gráficos do RSX rodam em Direct3D 12.

Em cima disso, o port tem launcher próprio, opções gráficas modernas, 60 FPS, geração de quadros, um SRT pronto para speedrun, ferramentas de treino e mais. Está tudo descrito abaixo.

## Por que a versão de PS3

Sou muito fã de Resident Evil e da Capcom, e o Code: Veronica é um dos meus jogos preferidos da série. Escolhi a versão de PS3 porque é a que muitos **speedrunners** usam, e eu queria que eles tivessem uma versão de PC rápida, estável e com ferramentas feitas para treinar: vida, inventário, inimigos e tempo na tela, e a possibilidade de ir para qualquer sala.

O port é para **todos os jogadores**: quem quer jogar de novo com resolução maior, 60 FPS e controles modernos, e também quem corre o jogo de forma competitiva.

## Feito também para treinar speedrun

Além de uma apresentação moderna no PC, o **Veronica Port também é dedicado ao treino de speedrun**. Repita uma sala difícil, prepare uma luta contra chefe ou treine uma rota de inventário com as ferramentas do próprio port.

| Ferramenta de treino | Como ajuda |
|---|---|
| **SRT nativo · F8** | Acompanhe vida do personagem e dos inimigos, inventário, munição e tempo de jogo. |
| **Seleção de salas · F7** | Carregue uma sala e escolha a entrada para repetir um trecho. |
| **Salvar e carregar em qualquer lugar · F7** | Crie saves de treino separados e retome uma situação rapidamente. |
| **Menu de itens · F6** | Prepare armas e recursos para um teste específico. |
| **Integração com LiveSplit** | Use o autosplitter incluído, que também reconhece o port. |

Ative as ferramentas opcionais na aba **Cheats**. Elas são recursos de treino; a validade de uma run depende da categoria e das regras da comunidade.

## Versões compatíveis

Só as **versões da PSN, 01.00**, são compatíveis. Cada região tem o seu executável; todos usam o mesmo launcher, configurações, saves e caches, e qualquer um deles abre o executável certo para a pasta escolhida.

| Região | Código | Executável | Licença (.rap) |
|---|---|---|---|
| Europa | `NPEB00553` | `CodeVeronicaPort.exe` | Necessária |
| Estados Unidos | `NPUB30467` | `CodeVeronicaPort_US.exe` | Necessária |
| Japão (*BIOHAZARD CODE: Veronica Kanzenban*) | `NPJB00135` | `CodeVeronicaPort_JP.exe` | Não precisa |

Atualizações do jogo, versões em disco e outras versões não funcionam. O launcher confere a versão e avisa se a pasta não for compatível.

## Requisitos

- Windows 10 ou 11, 64 bits.
- Placa de vídeo compatível com **Direct3D 12**.
- O **Microsoft Visual C++ Redistributable (x64)**. Sem ele, o Windows reclama de `MSVCP140.dll` ou `VCRUNTIME140.dll`.
- A sua cópia do jogo, em pasta (veja abaixo).

**Não precisa de firmware do PS3 nem do RPCS3 para jogar.**

### PC de teste

O port foi desenvolvido e testado neste PC, onde o jogo foi **zerado do início ao fim**:

| Peça | Modelo |
|---|---|
| Processador | AMD Ryzen 5 5600X |
| Placa de vídeo | AMD Radeon RX 6700 XT 12 GB |
| Memória | 32 GB DDR4 |

Outros PCs devem funcionar, mas esta é a configuração que foi realmente testada.

## Preparando o jogo

O port lê o jogo de uma **pasta**, não de um arquivo `.pkg`. Funciona como os ports de PS4 que usam pastas de jogo no estilo do shadPS4: o PKG precisa ser **instalado ou extraído antes**. Não dá para abrir um `.pkg` direto.

A pasta escolhida precisa ter o `PARAM.SFO` e o `USRDIR\EBOOT.BIN`:

```
NPEB00553\            (ou NPUB30467 / NPJB00135)
├── PARAM.SFO
├── ICON0.PNG
└── USRDIR\
    ├── EBOOT.BIN
    └── BHCV\ ...
```

**Passo a passo**

1. **Tire a pasta do jogo do seu PKG. Escolha uma das opções:**
   - **Veronica PKG Extractor (download separado e opcional):** quando disponível, baixe `Veronica-PKG-Extractor-v1.0.zip` na página de [Releases](https://github.com/kastiellcontato-design/re-cvx-ps3recomp/releases), junto de `CodeVeronica-PC-v1.0.zip`. Extraia a ferramenta, abra `VeronicaPKGExtractor.exe`, arraste ou selecione seu PKG, escolha o destino e clique em **Extrair PKG**. Ela cria a pasta `NPEB00553`, `NPUB30467` ou `NPJB00135` com os arquivos originais do pacote. Adicione essa pasta ao launcher. A ferramenta aceita somente as três versões PSN base compatíveis de Code Veronica, versão 01.00.
   - **RPCS3:** instale o PKG em *File → Install Packages/Raps/Edats*. A pasta fica em `RPCS3\dev_hdd0\game\NPEB00553` (ou `NPUB30467` / `NPJB00135`). Adicione essa pasta ao launcher. O RPCS3 só é necessário para esta preparação.
   Os dois métodos geram a pasta que o port precisa. **Você só precisa usar um deles.** Se já tem o jogo extraído, não precisa baixar o extrator. O launcher não abre PKGs diretamente, e o extrator não cria licenças RAP.
2. **Ache a sua licença (`.rap`)** para as versões europeia e americana. É o arquivo `.rap` da conta que comprou o jogo. No RPCS3 ele fica em `dev_hdd0\home\00000001\exdata`, com o código do jogo no nome (por exemplo `EP0102-NPEB00553_00-…​.rap`). Se o `.rap` estiver dentro da pasta do jogo, o launcher acha sozinho. **A versão japonesa não precisa de `.rap`.**
3. **Deixe os arquivos originais como estão.** Não precisa descriptografar nada: na primeira vez, o port descriptografa o `EBOOT.BIN` com a sua licença para a pasta `eboot_cache` dele (leva menos de um segundo). Os arquivos do jogo nunca são alterados.

## Instalando e jogando

1. Instale o [Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe), se ainda não tiver.
2. Extraia o ZIP inteiro para uma pasta onde você possa gravar, por exemplo `C:\Jogos\CodeVeronica`. Não rode de dentro do ZIP nem de `Arquivos de Programas`, e deixe todos os arquivos juntos.
3. Abra o `CodeVeronicaPort.exe` (qualquer um dos três executáveis abre o mesmo launcher).
4. Na página **Jogos**, adicione a pasta de cada versão que você tem e, na europeia e na americana, o `.rap`, se ele não tiver sido achado sozinho.
5. Escolha a versão, ajuste os gráficos e os controles e clique em **JOGAR**.

Nas primeiras partidas pode haver pequenos trancos enquanto os shaders são compilados. Eles ficam guardados em `shader_cache` e não são compilados de novo.

**Onde ficam as coisas** (tudo dentro da pasta do port): configurações em `cv_launcher.ini`; saves em `gamedata\dev_hdd0\` (dá para escolher outra pasta no launcher); caches em `shader_cache\` e `eboot_cache\` (pode apagar, são refeitos); screenshots em `screenshots\`; o registro da última partida em `run-log.txt`, útil para relatar um problema.

O launcher também pode procurar novas versões nos lançamentos do GitHub e se atualizar (dá para desligar no launcher).

## Funções

### Launcher

- Launcher próprio em **13 idiomas**: Português (Brasil), English, Español, 日本語, Français, Deutsch, Italiano, Polski, Türkçe, العربية, 简体中文, 한국어 e Русский.
- Biblioteca com as três regiões, conferência da versão e da licença, e a bandeira de cada região.
- Fica aberto durante o jogo como **menu de opções** (`Insert`/`F5`), para mudar as configurações sem sair da partida.
- Páginas: Jogos, Gráficos, Saves e capturas, Controles, Atalhos, Patches, Mods, SRT, Cheats, Desempenho e Avançado (idioma, cache de shaders, visualizador do log do jogo).
- Pausa opcional quando a janela do jogo perde o foco.

### Gráficos e desempenho

- **Resolução de renderização**: 480p, 720p nativa, 1080p, 1440p e **4K** (3840×2160). Em janela, janela sem borda ou tela cheia.
- **Anti-serrilhado**: FXAA (leve) ou SMAA (melhor).
- **Filtro anisotrópico**: 2x, 4x, 8x e 16x.
- **Nitidez** com **AMD FSR 1 RCAS**, de 10% a 100%.
- **Limite de quadros**:
  - **30 FPS (original)**: a mesma experiência do PS3, onde o jogo roda a 30 FPS.
  - **60 FPS**: a lógica do jogo continua nos 30 Hz originais e os quadros extras vêm da **interpolação das poses** dos personagens e objetos, então o ritmo do jogo é mantido. A interpolação roda na CPU por padrão, com um modo experimental na GPU. Se o PC não mantiver os 60 FPS, o jogo fica mais lento.
- **AMD FSR 3.1 Frame Generation** (opcional, só no modo 60 FPS): até 120 FPS quando o PC mantém os 60 FPS.
- **VSync** (modo 60 FPS): a imagem não rasga. Não funciona junto com a geração de quadros.
- Overlay de desempenho com FPS, tempo de quadro, CPU, GPU, RAM, VRAM e carga de SPU/RSX, em qualquer canto.
- Cache persistente de shaders e pipelines, carregado em segundo plano, com aviso opcional de progresso.
- Vídeos (FMV) decodificados com FFmpeg e áudio ATRAC3plus decodificado no próprio port: não precisa de firmware do PS3.

### SRT para speedrunners

<p align="center">
  <img src="docs/images/srt-albinoid-chris.jpg" alt="SRT — Albinoid" width="880">
  <br><sub>SRT nativo e barra de vida do chefe durante a partida.</sub>
</p>

<details>
<summary>Ver o painel do SRT em detalhe</summary>

<p align="center">
  <img src="docs/images/presentation/srt-panel-boss.png" alt="SRT" width="268">
</p>

</details>

Um **SRT** (speedrun tool) nativo, desenhado pelo próprio jogo, ligado e desligado com `F8`:

- Nome e retrato do personagem, **vida** (valor, porcentagem e estado: bem, atenção, perigo, envenenado, gás).
- **Tempo de jogo**, saves, tentativas e sprays de primeiros socorros (F.A.S.).
- **Inimigos** com barra de vida e retrato, e **barra de vida dos chefes** (no painel ou no centro, em cima ou embaixo).
- **Inventário** com a arma equipada, a munição e os ícones originais dos itens, lidos dos arquivos do seu jogo.
- Qualquer canto, disposição em linha ou coluna, inventário ao lado ou embaixo, tamanho de 50% a 200%, opacidade do fundo, borda, idioma do painel, ou uma **janela separada** que você coloca onde quiser (por exemplo num segundo monitor ou no OBS).
- Um autosplitter do **LiveSplit** que também reconhece o port vem em `projects/code_veronica/livesplit/`.

### Ferramentas de treino, saves e itens

- Janela **Arquivos** (`F7`, ativada em Cheats): **carregar qualquer sala ou mapa** (com o ponto de entrada da sala) para treinar um trecho, **salvar em qualquer lugar** numa biblioteca de saves independente, com miniaturas, e **carregar** esses saves quando quiser.
- **Menu de itens** (`F6`, ativado em Cheats): coloque qualquer item ou arma no inventário do personagem atual, ou tire, sem sair da partida.
- **Cheats**: vida infinita e munição infinita, que ligam e desligam durante o jogo.
- **Patches**: pular a animação das portas, pular os logos e a introdução, e os seus próprios patches no formato `patch.yml` do RPCS3.
- Escolha da pasta onde os saves ficam.

### RECOMP SETTINGS e troca de personagem dentro do jogo

<p align="center">
  <img src="docs/images/recomp-settings-appearance.jpg" alt="RECOMP SETTINGS com o seletor de aparência" width="880">
</p>

No próprio menu **OPTION** do jogo, a página **RECOMP SETTINGS** muda a resolução, o anti-serrilhado, o modo de exibição, o brilho, o contraste, a saturação, a nitidez (FSR RCAS) e o filtro anisotrópico sem abrir o launcher, com **Reset colors** para voltar ao padrão.

No topo da mesma página, **APPEARANCE** deixa você **jogar com o modelo de outro personagem**: aperte esquerda/direita para escolher e veja numa **prévia girando em 360°**. São 33 opções: Claire (roupa original e alternativa), Chris, Steve, Wesker, 26 variações de zumbi e duas formas da Alexia (primeira forma e humana). A aparência escolhida também vale quando você joga com o Chris, o Steve ou o Wesker. Só muda o visual do personagem; a história, os controles e o seu save continuam iguais.

### Controles

- Controles **Xbox** (XInput), **PlayStation** (DualShock 4 e DualSense) e **Nintendo Switch**, além do teclado, todos ao mesmo tempo.
- **Vibração** com força ajustável (0–200%).
- Qualquer tecla ou botão pode ser trocado, com várias teclas por ação.
- Ícones dos botões na tela em HD para PlayStation, Xbox, Switch ou teclado, escolhidos automaticamente pelo último aparelho usado.

### Patches

A página **Patches** lista mudanças no próprio código do jogo. Elas valem na hora, mesmo com o jogo aberto.

- **Pular animação das portas**: a próxima sala carrega direto, sem a animação da porta abrindo (por Kapdap, da lista de patches do RPCS3).
- **Pular logos e introdução**: o jogo abre direto na tela de título, sem os logos CAPCOM / MT FRAMEWORK e o vídeo de abertura.
- **Seus próprios patches**: escolha uma pasta com arquivos de patch `.yml` do RPCS3 (o mesmo formato do `patch.yml` do RPCS3). Os patches da sua versão do jogo aparecem na lista, cada um com a sua chave. Um patch que mexe em código que não veio compilado nesta versão aparece como indisponível.

### Mods de textura (experimental)

O port consegue **trocar as texturas do jogo por imagens PNG** e também **extrair as texturas originais** para você, já com o nome certo. Tudo fica na página **Mods** do launcher.

**Como funcionam os nomes.** Cada textura tem um código: 16 caracteres hexadecimais no começo do nome do arquivo, por exemplo

```
1A2B3C4D5E6F7081_512x256_DXT5.png
└──── código ──┘ └tam.┘ └formato┘
```

Só o **código** importa. O PNG novo pode ter **qualquer tamanho** (uma textura 512×256 pode virar 2048×1024 em HD) e o resto do nome é livre, mas ele precisa **começar com o mesmo código**. Só arquivos `.png` são lidos.

**Passo 1 — Extraia as texturas** (para saber o código de cada uma)

1. Abra o launcher e vá em **Mods**.
2. Escolha um modo de extração:
   - **Extrair texturas de todos os mapas**: lê os arquivos do jogo sem precisar jogar. Selecione e clique em **Iniciar extração**. Leva alguns minutos, mostra o progresso e pode ser cancelado. Um arquivo CSV diz de qual arquivo do jogo veio cada PNG.
   - **Extrair texturas das salas visitadas**: ligue e jogue; cada textura nova que aparece (salas, personagens, itens, efeitos) é salva. Use para as imagens que o jogo cria enquanto roda, que não ficam guardadas nos arquivos.
3. Os PNGs vão para a **Pasta das texturas extraídas** (por padrão `mods\texture-dumps\<versão>`, ao lado do executável). PNGs que já existem nunca são sobrescritos, então as suas edições ficam seguras.

**Passo 2 — Faça o mod**

1. Copie os PNGs que você quer mudar e edite (ou aumente) em qualquer editor de imagem.
2. **Mantenha o código no começo do nome.** O tamanho pode mudar.
3. Coloque os arquivos na **Pasta das texturas do mod** (por padrão `mods\textures`), dentro da subpasta da sua versão: `NPEB00553`, `NPUB30467` ou `NPJB00135`. Sem subpasta, a própria pasta vale para todas as versões. Pode criar subpastas dentro dela para organizar o pacote como quiser.

**Passo 3 — Use**

1. Ligue **Substituir texturas** na página Mods (reinicie o jogo se você trocou a pasta do mod).
2. No jogo, aperte **F9** (controle: R1+R2+L1+L2+□ / RB+RT+LB+LT+X) para comparar o mod com as texturas originais quando quiser.
3. Pacotes grandes não pesam de cara: cada PNG só é carregado quando a textura dele aparece na tela.

> [!TIP]
> **Adaptando pacotes de texturas HD do PS2.** Packs de PS2 podem usar arquivos **`.dds`**, mas o port atualmente lê apenas **`.png`**. Primeiro **converta as imagens DDS para PNG**, preservando a transparência; mudar apenas a extensão não converte o arquivo. Depois, use **Extrair texturas de todos os mapas** para obter as referências do PS3, identifique a textura correspondente e nomeie o PNG convertido com o **código de 16 caracteres do PS3**. Coloque-o na pasta do mod como no Passo 2. A compatibilidade deve ser conferida por textura: recortes, orientação, transparência e organização da imagem podem diferir entre PS2 e PS3; apenas converter e renomear não garante o resultado correto.

Observações: as faces de cubemaps (reflexos) são extraídas em `reference\cubemaps` só para consulta; por enquanto a troca funciona com texturas 2D. Nunca compartilhe as texturas originais extraídas; compartilhe só as imagens que você editou.

## Atalhos

No controle, segure primeiro os quatro botões de ombro e depois aperte o último botão.

| Teclado | PlayStation | Xbox | Switch | O que faz |
|---|---|---|---|---|
| `Insert` / `F5` | R1+R2+L1+L2+L3 | RB+RT+LB+LT+LS | R+ZR+L+ZL+LS | Menu de opções (tela, áudio, SRT, sair) |
| `F6` | R1+R2+L1+L2+△ | RB+RT+LB+LT+Y | R+ZR+L+ZL+X | Menu de itens (ativar em Cheats) |
| `F7` | R1+R2+L1+L2+○ | RB+RT+LB+LT+B | R+ZR+L+ZL+A | Arquivos: mapas, salvar, carregar (ativar em Cheats) |
| `F8` | R1+R2+L1+L2+✕ | RB+RT+LB+LT+A | R+ZR+L+ZL+B | Mostra ou esconde o SRT |
| `F9` | R1+R2+L1+L2+□ | RB+RT+LB+LT+X | R+ZR+L+ZL+Y | Liga/desliga as texturas do mod |
| `F10` | R1+R2+L1+L2+R3 | RB+RT+LB+LT+RS | R+ZR+L+ZL+RS | Screenshot (salva em `screenshots\`) |

Teclado padrão, sem controle: setas = direcional, `X` = Cruz, `Z` = Círculo, `A` = Quadrado, `S` = Triângulo, `I` = Start, `R` = Select. Tudo pode ser trocado na página Controles.

## Capturas de tela

Uma seleção do launcher e do jogo. As outras telas estão reunidas em uma galeria separada.

<p align="center">
  <img src="docs/images/presentation/launcher-graphics.png" alt="Launcher e opções gráficas" width="880">
</p>

| Sala da caixa de música | Steve Mutante |
|:---:|:---:|
| <img src="docs/images/gameplay-music-box-room.jpg" alt="Sala da caixa de música" width="420"> | <img src="docs/images/gameplay-hunter-armory.jpg" alt="Steve Mutante" width="420"> |

[Ver a galeria completa de capturas](docs/GALLERY.pt-BR.md)

## Problemas conhecidos

O port ainda está em desenvolvimento. Ainda podem aparecer defeitos gráficos e de iluminação, os 60 FPS não são garantidos em todo PC, e a maior parte dos testes de gameplay foi feita na versão japonesa (a europeia e a americana usam o mesmo código). Relate problemas com o seu `run-log.txt` e uma descrição do que aconteceu.

## Compilando o código

Só é preciso compilar para trabalhar no port. Você precisa do seu próprio jogo descriptografado, porque o código recompilado do jogo é gerado na sua máquina e **nunca vai para o repositório**.

**Ferramentas:** Visual Studio 2022 ou mais novo (ou Build Tools) com o pacote de C++, **LLVM/clang-cl**, CMake 3.20+, Ninja, Python 3.12, e as DLLs do FFmpeg 7.1 (`avcodec-61`, `avutil-59`, `swscale-8`, `swresample-5`) em `third_party/ffmpeg/bin/`.

1. **Gere o código do jogo.** Siga o [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) e o [docs/SPU_LIFTER.md](docs/SPU_LIFTER.md) para gerar o código PPU em `src/recomp/` (EU), `src/recomp_us/` (US) e `src/recomp_jp/` (JP), e as imagens SPU em `projects/code_veronica/spu/`. Por exemplo, para a japonesa:
   ```
   python tools/ppu_loader.py <NPJB00135>/USRDIR/EBOOT.elf -o out_jp/
   python tools/ppu_lifter.py <NPJB00135>/USRDIR/EBOOT.elf --functions out_jp/EBOOT.functions.json --hle-stubs out_jp/EBOOT.imports.json --patches projects/code_veronica/patches.json -o src/recomp_jp
   ```
2. **Compile.** Rode `projects/code_veronica/build-release.cmd` (Europa). Defina `CV_BUILD_JP=1` e/ou `CV_BUILD_US=1` para compilar as outras regiões na mesma execução, ou use `build-release-jp.cmd` só para a japonesa. Os scripts têm os caminhos do autor (Visual Studio, LLVM e `D:/ps3recomp`); ajuste para a sua máquina.
3. Os executáveis ficam em `projects/code_veronica/build-release/bin/`. Use sempre a compilação Release: a Debug é lenta demais para o código recompilado.
4. `projects/code_veronica/empacotar-release.cmd` monta o ZIP de lançamento (só os arquivos do port, nunca dados do jogo).

## Rodando os testes

Os testes rodam quando pedidos, pelo `build-release.cmd`, com variáveis de ambiente. Eles usam dados sintéticos e pastas temporárias, nunca os seus saves ou configurações. Exemplo:

```
set CV_SRT_TESTS=1
set CV_UPDATER_TESTS=1
projects\code_veronica\build-release.cmd
```

| Variável | O que confere |
|---|---|
| `CV_SRT_TESTS` | Leitura de memória do SRT, layout e atalhos do controle (EU, e JP/US com `CV_BUILD_JP`/`CV_BUILD_US`) |
| `CV_MAP_TESTS` | Ferramentas de mapas/salas |
| `CV_ITEMS_TESTS` | Regras do menu de itens (F6) e escrita na memória do jogo, por região |
| `CV_CHEAT_TESTS` | Cheats nas três regiões |
| `CV_SAVE_TESTS` | Biblioteca de saves, em pastas isoladas |
| `CV_MENU_TESTS` | Navegação do menu RECOMP SETTINGS |
| `CV_FPS_TESTS`, `CV_POSE_TESTS`, `CV_POSE_GPU_TESTS` | Relógio de 30/60 FPS e interpolação de poses |
| `CV_UPDATER_TESTS` | Atualizador do launcher, sem rede |
| `CV_FOCUS_PAUSE_TESTS` | Pausa ao perder o foco |
| `CV_FP_TESTS`, `CV_DXC_TESTS`, `CV_SHADOW_FILTER_TESTS` | Tradução de shaders e filtros |
| `CV_SPU_FAULT_TESTS`, `CV_SPU_POOL_TESTS`, `CV_SPU_SCHEDULE_TESTS` | Runtime de SPU |

Os outros alvos de teste (por exemplo `code_veronica_launcher_cache_tests`) estão no `projects/code_veronica/CMakeLists.txt` e compilam com `cmake --build build-release --target <nome>`.

## Organização do repositório

| Caminho | Conteúdo |
|---|---|
| `projects/code_veronica/` | O port: launcher, SRT, ferramentas, ganchos do renderizador, scripts de compilação e notas de projeto |
| `projects/code_veronica/livesplit/` | Autosplitter do LiveSplit com suporte ao port |
| `runtime/`, `libs/`, `tools/`, `include/` | O runtime do ps3recomp, as bibliotecas do PS3 reimplementadas e as ferramentas do recompilador |
| `third_party/` | FFmpeg, decodificador ATRAC3, SMAA e AMD FSR 3.1 |
| `docs/` | Documentação do ps3recomp e as imagens deste README |
| [README-ps3recomp.md](README-ps3recomp.md) | O README original do ps3recomp |

## Créditos

**Port:** feito por **Kastiel**.

**Ajuda no desenvolvimento:** o port foi desenvolvido com a ajuda de assistentes de IA: **Claude** (Anthropic) e **Codex** (OpenAI).

**Base**
- [ps3recomp](https://github.com/sp00nznet/ps3recomp), de **sp00nznet (Ned Heller)** e dos seus [colaboradores](CONTRIBUTORS.md): o kit de recompilação estática e o runtime sobre os quais o port foi feito (MIT).
- [RPCS3](https://rpcs3.net): o emulador de PS3, usado como referência do comportamento do hardware e do sistema, nas comparações quadro a quadro e pelo formato `patch.yml`.

**Bibliotecas e recursos**
- [FFmpeg](https://ffmpeg.org) (LGPL 2.1 ou posterior): decodificação dos vídeos MPEG-2.
- Decodificador ATRAC3/ATRAC3plus do FFmpeg, na versão separada pelo [PPSSPP](https://github.com/hrydgard/ppsspp) (`at3_standalone`, LGPL 2.1 ou posterior).
- [AMD FidelityFX FSR 3.1](https://gpuopen.com/fidelityfx-super-resolution-3/), runtime de geração de quadros (licença da AMD), e AMD FSR 1 RCAS/EASU.
- [SMAA](https://www.iryoku.com/smaa/), de Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro e Diego Gutierrez (MIT).
- FXAA 3.11, de Timothy Lottes (NVIDIA).
- [stb_image e stb_image_write](https://github.com/nothings/stb), de Sean Barrett (domínio público / MIT).
- [GitHub Octicons](https://github.com/primer/octicons) (MIT), para o símbolo do GitHub no launcher.

**Comunidade de Resident Evil**
- **Kapdap**: [re-cvx-srt-provider](https://github.com/kapdap/re-cvx-srt-provider) (MIT), cujos endereços de memória e regras de estado do jogo foram estudados para o SRT; o [autosplitter do Code: Veronica](https://github.com/kapdap/re-cvx-autosplitter) (MIT), que o script do LiveSplit incluído amplia; e o patch de pular portas da lista de patches do RPCS3.
- **aikatore e colaboradores**: [recvx-enhanced-port](https://github.com/aikatore/recvx-enhanced-port) (MIT), pelos nomes dos locais das salas e como referência para os quadros intermediários.

A **Capcom** criou o *Resident Evil Code: Veronica*. Apoie os lançamentos oficiais.

Os textos completos das licenças estão em [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Licença

O código do port e o ps3recomp são distribuídos sob a **Licença MIT** — veja [LICENSE](LICENSE). Os componentes de terceiros mantêm as suas próprias licenças, listadas em [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). A licença não cobre o jogo: *Resident Evil Code: Veronica X* e os seus recursos pertencem à Capcom, e você precisa ter o jogo para usar este port.
