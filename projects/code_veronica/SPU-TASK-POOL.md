# Reutilizacao de threads de tarefas SPURS

Implementacao Windows em runtime/spu/spu_task_pool.h, integrada pelo caminho
spu_workload_dispatch_task_async em runtime/spu/spu_workload.c.

## Escopo e invariantes

- Fila FIFO de despacho. O SO continua decidindo quando cada thread executa.
  Workers ociosos sao escolhidos em LIFO, favorecendo cache quente. Uma CV
  por worker acorda exatamente uma thread por tarefa. Isso nao troca a ordem
  de retirada das tarefas da fila FIFO e nao altera afinidade/prioridade.
- A reserva/publicacao e a coleta de tarefas de f59ffb6 nao foram alteradas:
  a tarefa chega ao pool ja reclamada, nunca e reclamada novamente pelo pool.
- spurs_task2_finished continua antes de spu_workload_task_exited. O join,
  seus sinais e os codigos de conclusao nao foram alterados.
- SPU_MAX_RUNNING e seu filtro por imagem continuam no runner, incluindo a
  liberacao/reobtencao do semaforo em WAIT_SIGNAL. O pool nao e esse semaforo.
- Inicializa uma vez, com 2 a 16 workers conforme CPUs logicas. Cresce somente
  se as threads disponiveis nao cobrem o trabalho despachado. Nao ha limite
  fixo de workers: servicos persistentes nao podem bloquear seus produtores.
- Mantem reserva de pilha de 256 MiB, garantia de stack-overflow, nome
  spu:task <id> e prioridades do runner. Threads ociosas ficam em espera,
  com prioridade normal; estado TLS de canais/mailboxes/poll/trampoline e
  limpo antes de outro trabalho. Contextos/LS continuam nos pools existentes.
- Conta tambem workers ainda iniciando para nao criar threads excedentes em
  rajadas. Falha na criacao deixa a tarefa com o chamador para o caminho antigo;
  nao deixa trabalho aceito preso na fila sem nenhuma thread disponivel.
- Em vez de alocar outro objeto por submissao, o no FIFO faz parte da tarefa.
- Preparacao de pilha ocorre uma vez por thread. Nome so e atualizado quando
  o id muda (ou um servico alterou o nome); prioridade so quando foi elevada.

## Uso e reversao

SPU_TASK_POOL=0 usa uma thread nova por tarefa, como antes.
SPU_TASK_POOL=1 usa o pool. O modo antigo permanece o padrao: a comparacao
na sala pesada nao demonstrou ganho consistente. A implementacao e opt-in.

Atalhos de comparacao, ambos com PS3_TIMELINE:
- executar-code-veronica-spu-pool.cmd
- executar-code-veronica-spu-threads-antigas.cmd

Os atalhos preservam as opcoes do launcher; escolher JP, 60 FPS experimental
 e vertices na GPU para reproduzir a medicao. Carregar o mesmo save, manter
 a mesma camera e ficar parado pelo menos 20 segundos. Comparar [TL] e
 [GPUCMD] em run-log-release.txt; [SPU-POOL] mostra criacoes/reuso/fallback.
Nao confundir numero acumulado de tarefas com threads criadas.

## Compilacao e testes

Unico ponto de entrada: projects/code_veronica/build-release.cmd.
Variaveis: CV_BUILD_JP=1, CV_BUILD_US=1, CV_FPS_TESTS=1,
CV_SPU_POOL_TESTS=1, CV_SPU_SCHEDULE_TESTS=1.
Verificar que MyGameRecomp*.exe esta fechado antes de compilar.

- EU, JP e US compiladas nas tres iteracoes e na versao final opt-in.
- Stress: 4000 tarefas exatamente uma vez, workers reutilizados (ver log).
- Reuso da thread ociosa mais recente em trabalhos sequenciais: passou.
- Progresso com servico bloqueado e fallback em falha de criacao: passaram.
- Regressao f59ffb6: handoff/sibling race, exclusao, reuso e isolamento passaram.
- Testes FPS OFF/ON/RAW passaram.

## Medicao em 2026-10-17 (JP)

Logs locais fora do commit em build-release/:
- spu-pool-before.txt: comportamento antigo. Ultimas 6 janelas pesadas:
  media 48,53 FPS; MAIN-spu-join 3,70 ms; drain 12,10 ms;
  render 13,42 ms; gpu-wait 3,65 ms.
- spu-pool-after-v1.txt: primeira iteracao funcional. Sem ganho claro de FPS;
  aproximadamente 48 FPS e 3,7 ms de join. Mais de 90 mil tarefas usando
  somente 39 workers, sem fallback. Usuario confirmou audio normal.
- spu-pool-after-v2.txt: remove chamadas repetidas de preparacao/nome/prioridade.
  Ultimas 6 janelas: 46,38 FPS; join 3,67 ms; render 14,30 ms.
- spu-pool-after-v3.txt: acorda workers ociosos em LIFO, mantendo tarefas FIFO.
  Ultimas 6 janelas: 47,02 FPS; join 3,83 ms; drain 13,05 ms;
  render 13,90 ms; gpu-wait 3,40 ms. 37 workers, sem fallback.
- spu-pool-old-same-build.txt: mesmo executavel v3, SPU_TASK_POOL=0.
  Ultimas 6 janelas: 47,35 FPS; join 3,58 ms; drain 12,95 ms;
  render 13,78 ms; gpu-wait 3,68 ms.

Metodo: janelas [TL] de aproximadamente 5 s; filtrar gameplay da sala pesada
(mais de 8000 draw-calls/quadro), usar as ultimas 6 janelas completas de cada
captura. Medias aritmeticas dos valores por janela, ja arredondados no log;
nao sao tempos obtidos de um trace bruto por quadro. Opcoes JP/60 FPS/GPU
mantidas pelo usuario. Comparacao entre execucoes separadas, com variacao do
sistema; nao e um ensaio que prove equivalencia exata.

Conclusao: nenhuma melhoria consistente demonstrada. FPS v3 versus modo
antigo no mesmo build: -0,7%; join aproximadamente +0,25 ms. Sao resultados
pequenos e variaveis, nao evidencia de otimizacao. Nao houve a reducao
esperada de 1-3 ms. O pool fica opcional e o comportamento antigo e o padrao.
Usuario informou audio normal e concluiu os testes das execucoes do pool;
nenhum travamento foi relatado nessas sessoes, mas isso nao substitui teste
prolongado. A meta PRONTO QUANDO (ganho de FPS/join) NAO foi atingida.

O amostrador tsamp.cpp informado nao foi encontrado no caminho de
AppData/Roaming/Claude/side-session-notes deste ambiente; nao foi executado.
Proximo diagnostico sugerido: medir latencia de inicio e tempo de execucao
por imagem com PS3_SPU_STAT e obter amostras da ppu:main/SPU em A/B, antes de
outra alteracao. Evitar alterar ordem, prioridades ou WAIT_SIGNAL apenas para
forcar um resultado de FPS.

Estimativa inicial de 5-15% nao e resultado medido. Nao considerar a meta
atingida antes de obter reducao do join, aumento de FPS e validacao do usuario.
Nenhum commit autorizado/feito nesta tarefa. CMakeLists.txt e build-release.cmd
contem alteracoes anteriores do usuario: somente os blocos do teste SPU pool
foram adicionados aqui; preservar todos os outros trechos.
