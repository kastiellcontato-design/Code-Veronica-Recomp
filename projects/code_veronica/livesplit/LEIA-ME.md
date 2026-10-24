# LiveSplit no Code Veronica (port de PC)

`recvx-ps3recomp.asl` é o autosplitter de Code Veronica do Kapdap
([re-cvx-autosplitter](https://github.com/kapdap/re-cvx-autosplitter), licença
MIT, veja `LICENSE-recvx.txt`) com suporte ao nosso port. Ele reconhece as
três versões: `CodeVeronicaPort.exe` (EU, NPEB00553), `CodeVeronicaPort_JP.exe`
(JP, NPJB00135) e `CodeVeronicaPort_US.exe` (US, NPUB30467). Esta adaptação
por Kastiel se conecta somente ao port. Para emuladores, use o script original
do Kapdap.

Ele faz tudo o que o original faz:

- **início automático** quando um jogo novo começa;
- **splits automáticos** por porta, por item pego, por chefe derrotado e por
  evento, com as rotas de Any%, Rocket Launcher, Knife e S-Rank;
- **tempo de jogo**: o timer do próprio jogo, em 1/60 de segundo, e a opção de
  tirar o tempo das portas (LRT).

## Como usar

1. Abra o LiveSplit, clique com o botão direito e escolha **Edit Layout...**
2. Clique em **+** → **Control** → **Scriptable Auto Splitter**.
3. Clique duas vezes no componente, em **Script Path** escolha
   `recvx-ps3recomp.asl` e marque os splits que quiser nas opções.
4. Clique com o botão direito → **Compare Against** → **Game Time**.
5. Abra o jogo pelo atalho de sempre. O LiveSplit acha o jogo sozinho.

Use apenas um autosplitter por vez. Se o autosplitter para emuladores estiver
ativo em **Edit Splits**, clique em **Deactivate** antes de usar o script do
port pelo layout. As opções deste script ficam no **Scriptable Auto Splitter**.
Salve o layout (**Save Layout As...**) para guardar suas escolhas.

## Cadastro na lista do LiveSplit

O nome proposto para a lista é **Resident Evil: Code: Veronica X (PS3 Recomp)**.
O cadastro depende da revisão dos mantenedores do LiveSplit. Até a aprovação,
use a instalação manual acima. Depois da aprovação, será possível digitar esse
nome em **Edit Splits → Game Name** e clicar em **Activate**, sem adicionar o
componente manual ao mesmo tempo. Isso não garante que o nome apareça nas
sugestões de busca nem aprovação em rankings de speedrun.

Com **Load Remover Time** marcado, o tempo de jogo pausa a cada porta. Com o
patch "Pular animação das portas" ligado, a pausa é curta, cerca de 1 segundo,
porque o próprio patch já tira quase todo o load. Sem o patch, é de uns 6 segundos.

## Como o jogo é encontrado

O executável exporta o símbolo `g_cv_livesplit`:

| Deslocamento | Conteúdo |
|---|---|
| 0 | `PS3RECOMP-CVX` (assinatura) |
| 16 | onde a memória do PS3 começa no processo (64 bits) |
| 24 | a sigla da versão (`NPEB00553`, `NPJB00135` ou `NPUB30467`) |
| 40 | versão deste formato (1) |

As variáveis do jogo ficam nos mesmos endereços do PS3 a partir daí, em
big-endian, como no RPCS3. Por isso as tabelas de endereços do PS3 do
script original servem sem mudança.
