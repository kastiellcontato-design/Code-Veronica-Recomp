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

## Ativar pelo catálogo oficial do LiveSplit (recomendado)

O autosplitter do port já está no catálogo oficial:
[PR #2964 mesclada](https://github.com/LiveSplit/LiveSplit.AutoSplitters/pull/2964).

1. Abra o LiveSplit. Se quiser usar um split pronto, primeiro abra o `.lss`
   de Any% ou S-Rank do ZIP em **Open Splits → From File**.
2. Entre em **Edit Splits** e cole em **Game Name** exatamente este nome,
   incluindo os dois-pontos e o texto entre parênteses:

   **Resident Evil: Code: Veronica X (PS3 Recomp)**

3. Clique em **Activate** e depois em **Settings**. Se o autosplitter antigo
   de emuladores estiver ativo, desative-o antes de trocar. Se a lista ainda
   não tiver atualizado, feche e abra o LiveSplit e digite o nome novamente;
   não é necessário depender das sugestões de preenchimento.
4. Marque **Start** e **Split** e confira os eventos de acordo com sua rota.
   Ative **Load Remover Time** se quiser descontar o tempo das portas.
5. No menu principal do LiveSplit, use **Compare Against → Game Time**.
   A remoção dos carregamentos afeta Game Time; Real Time continua contando.
6. Inicie o port e salve os splits e o layout do LiveSplit.

Nesse método não precisa selecionar um arquivo ASL manualmente. Use apenas
um autosplitter: remova o componente **Scriptable Auto Splitter** adicionado
manualmente em **Edit Layout** antes de ativar o autosplitter do catálogo.

## Por que manter o ZIP?

Ele contém dois splits prontos de Any% e S-Rank com ícones, marcações de
rota, licença/créditos e instruções em português e inglês. O nome do jogo já
corresponde ao catálogo. Não há arquivos ASL no ZIP: o LiveSplit baixa e
carrega o script automaticamente ao clicar em **Activate**. O ASL fonte
continua neste repositório para o catálogo funcionar.

Os tempos e o histórico são exemplos; limpe-os no LiveSplit se quiser
iniciar seu próprio histórico. O ZIP é opcional para quem prefere montar
sua própria lista de splits.

Não ative uma rota de todas as portas se estiver usando splits somente por
itens. Com **Load Remover Time** marcado, Game Time pausa enquanto a flag de
porta do jogo estiver ativa; Door Skip pode encurtar esse intervalo. Sem LRT,
Game Time usa o timer interno do jogo. A inclusão no catálogo não representa
aprovação em rankings: consulte os moderadores da categoria.

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
