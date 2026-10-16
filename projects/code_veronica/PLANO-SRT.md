# Plano: SRT (painel de speedrun) dentro do port do Code Veronica

**Estado em 16/10/2026:** implementação nativa inicial compilada para as três
regiões; testes sintéticos aprovados e launcher conferido. Validação de
gameplay abrangente ainda pendente; ícones originais implementados com
fallback por nome (127 IDs mapeados). Veja [SRT-NATIVO.md](SRT-NATIVO.md)
para o que foi feito, limitações e próximos testes. O documento abaixo é o
plano de referência; a implementação incorpora os pedidos posteriores do usuário.

Documento para quem for implementar. Ele descreve **o que fazer, onde mexer e
de onde vêm os dados**. Leia inteiro antes de começar.

## 1. Objetivo

Um painel desenhado **dentro da janela do jogo**, por cima da imagem, com as
mesmas informações do SRT do Kapdap (re-cvx-srt), mas com visual de Resident
Evil. Ele vale para as três versões do port: EU NPEB00553 (`MyGameRecomp.exe`),
JP NPJB00135 (`MyGameRecomp_JP.exe`) e US NPUB30467 (`MyGameRecomp_US.exe`).
O painel é configurado no launcher.

Referência visual (o desenho do usuário):

- **Vida:** uma caixa com borda verde fina e cantos ornamentados. Dentro, o
  texto `100 (100) · Fine` e uma pequena linha de ECG à direita. A cor segue o
  estado: Fine verde, Caution amarelo/laranja, Danger vermelho, Poison roxo.
- **Tempo de jogo:** grande, com fonte serifada clara, como `00:01:05`.
- **Contadores:** logo abaixo, `Saves: 0`, `Retry: 0` e `F.A.S.: 0`.
- **ENEMIES:** um título vermelho com uma linha fina. Depois, uma linha por
  inimigo, com a vida atual em vermelho e uma barra vermelha proporcional
  (vida atual / vida máxima) numa moldura escura. **Sem foto do inimigo.**
- **Inventário:**
  - em cima fica o **item equipado**;
  - embaixo fica a **grade do inventário em 2 colunas**, igual ao menu do
    jogo. Itens que ocupam 2 espaços (rifles, Gold Lugers, Rocket Launcher,
    SMG) ocupam a linha toda;
  - a quantidade aparece embaixo do ícone, só nos itens que têm quantidade.
- **Paleta:** fundo preto translúcido, vermelho escuro `#a3332c`, dourado
  `#c9a24a` para o item equipado e texto creme `#d8cfc0`. Fonte serifada no
  estilo dos menus do jogo.

## 2. Opções no launcher (página nova "SRT")

Cada opção é uma variável de ambiente que o launcher passa ao jogo, como já
fazem as páginas "Desempenho" e "Mods":

| Opção | Variável | Valores |
|---|---|---|
| Ligar o SRT | `SRT` | `0` / `1` |
| Mostrar vida | `SRT_HEALTH` | `0` / `1` |
| Mostrar tempo | `SRT_TIME` | `0` / `1` |
| Mostrar Saves/Retry/F.A.S. | `SRT_STATS` | `0` / `1` |
| Mostrar inimigos | `SRT_ENEMIES` | `0` = não, `1`..`5` = quantos, `99` = todos |
| Mostrar inventário | `SRT_INVENTORY` | `0` / `1` |
| Posição | `SRT_CORNER` | `tl`, `tr`, `bl` ou `br` (cantos superior/inferior, esquerdo/direito) |
| Tamanho | `SRT_SCALE` | `50`..`200` (%), com ajuste fino pela roda do mouse |

Também é preciso:

- **Uma tecla para mostrar e esconder no jogo.** Implementado: F8, além de L3+R1+○ como
  combo no controle, no mesmo esquema do F9 / R3+R2 das texturas: veja
  `toggle_textures`, `WM_TOGGLE_TEXTURES` e `g_rsx_key_hook` em `launcher.cpp`
  e no backend. Ao alternar, mostre um aviso curto com `perf_overlay_toast`.
- **Os textos nas 13 línguas** em `launcher_i18n.h`, seguindo o padrão das
  outras páginas. Os rótulos dentro do painel ("Saves", "Retry", "F.A.S.",
  "ENEMIES" e os estados) também seguem o idioma do launcher. Use o mesmo
  mecanismo do `SHADER_NOTE_TEXT`, que passa o texto já traduzido por
  variável de ambiente.

## 3. De onde vêm os dados

O port guarda a memória do PS3 contígua. A base dela é `vm_base`, que o
LiveSplit também usa (veja `g_cv_livesplit` em `main.cpp`). Os valores ficam
**nos mesmos endereços do PS3 que o RPCS3 usa** e são **big-endian**. No
código do port, leia com `vm_read8/16/32(endereço)`, que já convertem.

Fonte dos endereços e das regras: o provider do Kapdap,
https://github.com/kapdap/re-cvx-srt-provider. A licença é MIT e o crédito
"Kapdap" precisa ficar no código e no LEIA-ME. Os arquivos úteis são:

- `SRTPluginProviderRECVX/GameMemoryRECVXScanner.cs`: os endereços, `Refresh()`,
  `RefreshInventory()`, `RefreshEnemy()` e `GetRDX()`;
- `Enumerations/ItemEnumeration.cs` e `Models/InventoryEntry.cs`: nomes,
  tamanho em espaços (`GetSlotSize`) e quais itens têm quantidade
  (`GetHasQuantity`);
- `Enumerations/EnemyEnumeration.cs`: os tipos de inimigo e a vida máxima de
  cada um;
- `Models/PlayerEntry.cs`: os limites de Fine, Caution e Danger, e o bit de
  veneno.

### Endereços (guest, versões PS3)

| Valor | EU NPEB00553 | US NPUB30467 | JP NPJB00135 | Leitura |
|---|---|---|---|---|
| Tempo de jogo | `0xBC40B8` | `0xBB3DB8` | `0xBB3E38` | u32, em 1/60 s |
| Sala | `0xBC40CC` | `0xBB3DCC` | `0xBB3E4C` | u16 big-endian; `vm_read16` já converte |
| Status (veneno etc.) | `0xBEE989` | `0xBDE689` | `0xBDE709` | u8 |
| Vida | `0xBEED1C` | `0xBDEA1C` | `0xBDEA9C` | s32 |
| Personagem | `0xBC389C` | `0xBB359C` | `0xBB361C` | u8 |
| Inventário | `0xBC3A88` | `0xBB3788` | `0xBB3808` | veja abaixo |
| Inimigos | `0xBEEE78` | `0xBDEB78` | `0xBDEBF8` | veja abaixo |
| Nº de inimigos | `0xBEE598` | `0xBDE298` | `0xBDE318` | s32 |
| Dificuldade | `0xBC38A4` | `0xBB36A4` | `0xBB3624` | u8 |
| Saves | `0xBC3898` | `0xBB3598` | `0xBB3618` | s32 |
| Retry | `0xBC40C2` | `0xBB3DC2` | `0xBB3E42` | s16 |
| F.A.S. | `0xBC40C0` | `0xBB3DC0` | `0xBB3E40` | s16 |
| RDX (sala carregada) | `0xBEE3EC` | `0xBDE0EC` | `0xBDE16C` | u32; compare com `GetRDX()` |

Coloque esses endereços em `cv_version.h`, no bloco de cada versão, como
`CV_SRT_*`. O bloco `CV_REGION_JP` é a JP, o `CV_REGION_US` é a US e o
`#else` é a EU.

### Regras de leitura (iguais às do provider)

- **Vida máxima:** na JP é 400 em Very Easy e 200 nas outras dificuldades.
  Na EU e na US é 320 em Very Easy e 160 nas outras.
- **Estado:** com o bit de veneno (`Status & 0x08`) ligado, é Poison. Fora
  isso, depende da porcentagem de vida: Fine a partir de 60%, Fine (laranja)
  a partir de 30%, Caution a partir de 15%, e abaixo disso Danger. Confira os
  limites e nomes exatos em `PlayerEntry.cs`.
- **Inventário:** a base é `Inventário + personagem × 0x40`. A primeira
  palavra (s32) é o índice do item equipado, começando em 1, e 0 quer dizer
  nenhum. Em seguida vêm as entradas de 4 bytes de cada espaço; o número
  delas está em `PlayerEntry` (`Inventory.Length`). O formato de cada
  entrada é `00 ITEM 00 QUANTIDADE` (por exemplo `0x00030007` é o Sniper
  Rifle com 7). Decodifique como `InventoryEntry.Update`.
- **Inimigos:** só leia se a sala estiver carregada (RDX igual a
  `GetRDX()`). A partir da base, cada entrada ocupa `0x578` bytes no PS3.
  - Tipo: s16 em `+0x04`; ignore os tipos desconhecidos.
  - Vida atual: s32 em `+0x41C`.
  - Ação, status e modelo: u8 em `+0x0C`, `+0x0F` e `+0x88`.
  - Use a lógica de `IsAlive` e `MaximumHP` de `RefreshEnemy()` caso a caso
    (Tyrant, Nosferatu, Alexia etc.). Mostre só os vivos, até o limite de
    `SRT_ENEMIES`.

**Valide com o jogo rodando** antes de desenhar. Compare os números com o SRT
do Kapdap no RPCS3: o usuário tem o SRT instalado e as três versões no RPCS3.

## 4. Onde implementar

1. **Leitura** (arquivo novo `projects/code_veronica/cv_srt.cpp`): uma função
   `cv_srt_sample()` que preenche uma struct simples (vida, máximo, estado,
   tempo, contadores, lista de inimigos, item equipado e espaços). Chame-a uma
   vez por quadro, na thread do jogo. Um bom lugar é perto de onde o menu
   nativo já roda; ou use um timer de cerca de 30 Hz na thread principal
   (`main.cpp`). Só leitura: **não escreva na memória do jogo**.
2. **Desenho:** use a camada de overlay que já existe no backend,
   `libs/video/rsx_perf_overlay.c/.h`. É a que desenha a nota de shaders e o
   aviso do F9 (`draw_pill`, `perf_overlay_toast`), em pixels do back buffer,
   depois do anti-aliasing. Ela não é afetada pelos ajustes de cor. Acrescente
   uma API do tipo `perf_overlay_set_srt(const srt_view*)`, com o estado já
   pronto, e desenhe no `perf_overlay_draw`. O texto do PC SETTINGS usa
   glifos rasterizados pelo GDI (`TextBatch` em `native_pc_menu.cpp`); dá para
   reaproveitar essa ideia para a fonte serifada.
   - Escale tudo pela altura da janela, para ficar igual em 720p, 1080p e 4K.
   - Respeite `SRT_CORNER` com uma margem de uns 16 px.
   - Atualize só quando os valores mudarem, para não custar desempenho.
3. **Launcher** (`launcher.cpp` e `launcher_i18n.h`): a página "SRT" com os
   controles da seção 2, gravados em `cv_launcher.ini`, mais a tecla de
   alternar.
4. **Ícones dos itens, tirados do próprio jogo:** os ícones são as mesmas
   imagens que o jogo mostra na caixa de itens e no inventário. **Não
   redesenhe nem recrie nada**: carregue os ícones dos arquivos do jogo do
   usuário (a pasta escolhida no launcher), em tempo de execução. Assim nada
   da Capcom vai para o repositório nem para a distribuição. Os passos:
   1. Ache a textura dos ícones. Abra o inventário com `TEXTURE_DUMP=1` (o
      dump de texturas que já existe no port) e veja qual PNG tem os ícones.
      Depois descubra de qual arquivo do jogo ela vem; o upload da textura no
      backend mostra o endereço de origem.
   2. Descubra como o jogo recorta cada ícone: a posição na folha por ID de
      item. Normalmente há uma tabela no executável ou no arquivo de dados
      do menu.
   3. Carregue essa folha uma vez e desenhe o recorte de cada item no painel.
   Enquanto o ícone de um item não estiver disponível, mostre o **nome do
   item** no lugar, para o painel nunca ficar vazio.

## 5. Regras do projeto (obrigatórias)

- Compile **só** com `projects/code_veronica/build-release.cmd`. Para as três
  versões use `CV_BUILD_JP=1 CV_BUILD_US=1`.
- **Nunca** coloque no git arquivos do jogo, dumps, o exe, DLLs, código
  convertido do jogo ou imagens tiradas do jogo. A pasta `textures/` e a
  `projects/code_veronica/gamedata/` ficam fora do git.
- Antes de compilar, confira se o jogo do usuário está aberto; o exe fica
  travado. Nos testes, feche **só o processo que você abriu**, pelo PID.
- Só faça commit e push depois de o usuário testar e confirmar.
- Os diagnósticos ficam desligados no atalho do usuário.
- Crédito: "endereços e regras de memória: re-cvx-srt-provider, de Kapdap
  (MIT)".

## 6. Links

- Código do SRT do Kapdap (endereços e regras; licença MIT):
  https://github.com/kapdap/re-cvx-srt-provider
- Interface WPF do SRT dele (referência de layout):
  https://github.com/kapdap/re-cvx-srt-ui-wpf
- Página do SRT (como instalar e usar no RPCS3, para comparar valores):
  https://kapdap.github.io/re-cvx-srt/
- SRTPluginManager (instala o SRT do Kapdap):
  https://github.com/SpeedrunTooling/SRTPluginManager/releases
- Autosplitter do Kapdap, que usa os mesmos endereços PS3:
  https://github.com/kapdap/re-cvx-autosplitter
  (no projeto: `projects/code_veronica/livesplit/recvx-ps3recomp.asl`)

## 7. Ordem sugerida e como validar

1. `cv_srt.cpp` e os endereços em `cv_version.h`, com um log temporário
   (por exemplo `SRT_LOG=1`) que imprime os valores a cada segundo. Compare
   com o SRT do Kapdap no RPCS3 nas três versões: vida, tempo, inimigos e
   inventário. Os dois devem mostrar os mesmos números na mesma situação.
2. Desenho do painel com valores reais, sem opções, no canto superior
   esquerdo. Tire prints em 720p e 1080p e compare com o desenho de
   referência.
3. Opções do launcher e tecla de alternar; teste cada chave e cada canto.
4. Ícones dos itens.
5. Teste final nas três versões: New Game, levar dano, envenenar, uma sala com
   inimigos, um chefe (o Tyrant do avião) e equipar e trocar itens.

## 8. Pedidos posteriores incorporados

- Nome do personagem, incluindo Claire, Chris e Steve; retratos opcionais
  ficaram de fora para manter o painel compacto.
- Barra e porcentagem da vida, cores dos estados, incluindo veneno.
- Escala 50–200%, quatro cantos e controle individual de cada bloco.
- Inglês / português brasileiro selecionáveis para o painel, além de seguir
  os rótulos do launcher nos demais idiomas.
- Implementação própria: não copiar o scanner ou a interface da referência.
- F8 e L3+R1+○ mostram/escondem sem abrir o launcher.
- Ver a divergência do endereço de dificuldade US documentada no SRT-NATIVO.md.

## 9. Refinamento após teste do usuário

Funcionamento confirmado na EU. A pedido do usuário, revisão com inventário
na coluna direita, fontes Palatino/Bahnschrift rasterizadas na resolução
final, ECG animado em geometria separada e linha reta em morte. Testes de
animação, posição, escala, 64 inimigos e builds das três regiões aprovados.
Imagens de conceito em `codex_diag_srt/design/`, sem confundir com screenshots
do jogo. Ícones originais adiados explicitamente pelo usuário até localizar
as imagens do próprio jogo.

## 10. Ajustes após os prints do usuário

Legibilidade: Unicode correto para maiúsculas acentuadas, fontes Georgia e
Segoe UI, rodapé maior e remoção do detalhe sobre VIDA. Layout mais compacto,
barra de HP de 11 unidades, contadores ativados, ECG antialiasado com ciclo
de ~2,2 s e aviso de espera reduzido. Opacidade do fundo configurável de
0–100% no launcher, persistida em `[srt] opacidade` e enviada em `SRT_OPACITY`.
Rótulo nas 13 línguas; texto/barras preservam sua visibilidade ao mudar o fundo.
Builds/testes de EU/JP/US aprovados. Veja a seção final de SRT-NATIVO.md.

## 11. Refinamento de sombras e fontes

Barras de inimigos de 11 unidades, sem prefixo ~ nos números, crédito de
Kastiel no rodapé. Fontes dos nomes/títulos/estado em Segoe UI Semibold,
textos com supersampling 2x e cache limitado, sombras em textos/barras/ECG
independentes da opacidade do fundo. Testes também sobre fundo claro sem
painel e em 4K/200%, aprovados EU/JP/US. Ver SRT-NATIVO.md para limites e
medidas. Preferências atuais 90%/10% preservadas.

## 12. Sliders e segurança dos atalhos

Tamanho/fundo com barras de arrastar, porcentagem visível e passos de 1%.
Crédito completo localizado nas 13 línguas, Feito por Kastiel em PT-BR.
SRT: F8 ou L3+R1+○. Launcher: Insert ou L2+R3+R1. Dicas traduzidas atualizadas.
Arrasto capturado e gravação no fim do gesto; máscaras e mapeamento de slider
testados em cv_launcher_controls.h. Ver última seção de SRT-NATIVO.md.


## 13. Ícones fornecidos pelo usuário e cantos direitos

As folhas do dump correspondem a subtx tex0004/tex0003 e foram encontradas
nos arquivos originais das três regiões. Carregamento somente leitura da cópia
selecionada, cache em memória, transparência pelo azul de fundo e 127 IDs
mapeados. Sem imagens versionadas ou dependência dos dumps. Inventário pequeno
sem alterar o tamanho das células. A coluna de vida fica junto à borda nos
quatro cantos; nos cantos direitos o inventário fica à esquerda. Ver última
seção de SRT-NATIVO.md para fallback dos IDs não identificados e validação.


## 14. Nomes completos e armas de dois slots

Grade e equipado passam a medir/quebrar nomes completos, sem diminuir a fonte.
A altura cresce apenas nas fileiras necessárias; quantidades ficam abaixo.
Armas largas têm ícones maiores e ocupam uma célula de largura dupla. Prévias
PT/EN do renderer, com inventário sintético, e validação EU/JP/US estão na
última seção de SRT-NATIVO.md. Sem mudança do inventário real ou dos saves.
