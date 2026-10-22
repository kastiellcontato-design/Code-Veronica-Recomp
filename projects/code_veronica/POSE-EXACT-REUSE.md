# Reaproveitamento exato no histórico e nas costuras — v1

Solicitação: verificar dependências e aplicar otimizações, sem testes isolados
nesta rodada. A validação visual e de FPS será feita pelo usuário no jogo.

## 1. Histórico: comparação completa de pequenos pares de geometria

Memo temporário usa os IDs exatos das posições do quadro atual e do anterior,
mais a quantidade de vértices. Só é permitido para desenhos de até 16 vértices:
nesse intervalo a identificação existente compara **todas** as coordenadas por
bits. Em desenhos maiores a identificação é apenas amostral, portanto a
comparação exaustiva continua obrigatória e não usa o memo novo.

Cacheia somente rejeição por salto e mudança geométrica. A diferença de câmera
continua calculada separadamente em cada desenho. A escolha da correspondência,
limites de busca, temporização, tracks, atualização do histórico e fallback não
são alterados. Hash seleciona uma entrada; IDs e quantidade são conferidos antes
de reutilizar. Colisão substitui a entrada, não estabelece equivalência.

Validade é o `memo_epoch` de **uma única chamada de prepare**. Outro quadro,
cena, histórico ou nova preparação ganha outro epoch. O wrap limpa as entradas.
Não há reaproveitamento persistente entre quadros ou sobre dados mutáveis.

## 2. Costuras: consultas repetidas com cadeia inalterada

Só vértices de upload já encontrados no percurso entram no memo. A primeira
repetição consulta normalmente e armazena vértice, grupo de câmera, epoch e a
cabeça da cadeia de busca depois de concluir a operação. Repetições seguintes
podem reaproveitar quando todos esses campos continuam iguais.

Na construção do grafo, nós só são acrescentados na cabeça, nunca removidos ou
substituídos. Cabeça inalterada dentro da chamada implica mesma cadeia, primeiro
resultado e limite de 128 visitas. Os dados de posição do quadro ficam imutáveis
durante essa construção. A união já realizada continua válida: componentes podem
ser unidos, não separados. A resolução final de roots permanece executada.

Uma mudança na cadeia, câmera, vértice ou chamada invalida a consulta anterior.
Uploads únicos não escrevem nesse memo. O grafo completo continua sendo
construído antes de modificar propostas; todas as proteções de conflitos,
geometria estacionária, propagação pela superfície, aliases e receitas permanecem.

## Reversão, custos e escopo

- `CV_POSE_REPEAT_REUSE=0` desativa somente os dois reaproveitamentos novos.
- Alocações opcionais falhando selecionam o cálculo original. Tabelas têm tamanho
  fixo relacionado a `memo_slots`, sem crescimento por quadro, e são liberadas
  junto ao histórico. Não há locks, leituras GPU ou gravações guest novas.
- Contadores `[POSE-EXACT-REUSE]` indicam hits e misses por apresentação. Não
  acrescentam sondas de relógio por vértice. Precisamos dos logs reais para saber
  se o padrão da sala oferece reutilização suficiente para compensar o memo.
- SPU nativa e comparação vetorial anterior continuam ligadas nos dois modos.
- Só o executável experimental `MyGameRecomp_JP_NativeDescriptors.exe` foi
  atualizado. Os binários habituais e os 30 FPS permanecem intactos.
- Câmeras, luz, qualidade, shaders e critérios de correspondência não foram
  flexibilizados. Nenhuma promessa de resultado perfeito ou ganho de FPS antes
  da validação real.

## Entrega e verificação desta rodada

Revisão de dependências, invalidação, falha de alocação e caminhos de retorno;
compilação do alvo `MyGameRecomp` concluída. **Não foram executados testes
isolados nem benchmark**, conforme solicitado. Não é uma alegação de equivalência
demonstrada por testes ou validação gráfica no jogo.

Usar `executar-teste-reaproveitamento-jp.cmd`: entrar na mesma sala/câmera,
esperar pelo menos 10 s e apertar F12. Conferir também corrida, cortes de câmera,
rosto, objetos, costuras e isqueiro. Para desligar só essas mudanças, usar
`executar-teste-reaproveitamento-desligado-jp.cmd`.

Logs separados: `run-log-reaproveitamento-jp.txt` e
`run-log-reaproveitamento-desligado-jp.txt`. Configuração: 1080p, DXC,
interpolação CPU, FSR desligado. Sem commit/push nesta rodada.
