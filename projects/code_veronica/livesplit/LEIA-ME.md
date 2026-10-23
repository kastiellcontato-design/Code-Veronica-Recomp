# LiveSplit no Code Veronica (port de PC)

`recvx-ps3recomp.asl` é o autosplitter de Code Veronica do Kapdap
([re-cvx-autosplitter](https://github.com/kapdap/re-cvx-autosplitter), licença
MIT, veja `LICENSE-recvx.txt`) com suporte ao nosso port. Ele reconhece as
três versões: `CodeVeronicaPort.exe` (EU, NPEB00553), `CodeVeronicaPort_JP.exe`
(JP, NPJB00135) e `CodeVeronicaPort_US.exe` (US, NPUB30467). Também continua
funcionando no RPCS3, no PCSX2 e no Dolphin, como o original.

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

Use só este script. Se o autosplitter da lista do LiveSplit estiver ativado
(**Edit Splits** → **Activate**), clique em **Deactivate**: ele não se conecta
ao port, e as opções marcadas nele não fazem efeito. As opções que valem são as
do **Scriptable Auto Splitter** do layout. Salve o layout (**Save Layout As...**)
para não perdê-las.

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
