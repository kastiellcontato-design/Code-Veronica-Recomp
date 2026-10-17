# Congelamento investigado em 16/10/2026

**Estado: corrida de agendamento reproduzida em teste e corrigida; validação
de gameplay pendente.** A segunda captura repetiu a falha de DMA. A correção
impede uma duplicação possível de tarefas; ainda não se afirma que todos os
congelamentos foram resolvidos. Não houve mudança na aceitação de DMA, no
compilador de shaders ou no SRT.

## Evidência preservada

Log DXC europeu (NPEB00553), modificado às 19:07:56, preservado localmente em
`codex_diag_hang/freeze-20261016-190756.txt`. Nas linhas 49578–49584:

| Imagem / PC | Operação | LS | RAM | Tamanho |
|---|---|---|---|---|
| 10 / 0x20D54 | GET | 0x37DD0 | 0x008F6C68 | 4784 |
| 10 / 0x20D54 | GET | 0x39080 | 0x008F7F18 | 8 |
| 10 / 0x20D54 | GET | 0x39088 | 0x008F7F20 | 4 |

É uma leitura de 4796 bytes, dividida pelo helper DMA em três partes, todas
com alinhamento LS/RAM divergente em 8 bytes. A tarefa é a 20 do taskset
0x47489C80; retorno registrado em r0: 0x1F1F4. O código convertido da imagem
9 (ID de execução 10) chama esse helper para carregar um bloco de contexto.

Depois das recusas, aparecem mais dois registros de desenho, até o contador
37136000, seguidos somente por WAIT_SIGNAL de outro taskset (0x00CD7980).
Essas esperas também existem durante a execução normal: isoladamente, não
identificam um deadlock. Não há erro explícito de dispositivo gráfico no fim
desse log. O jogo não encerrou com uma exceção registrada nesse trecho.

O outro log disponível, FXC de 14:18:17, contém zero recusas desse tipo.
Não existe histórico suficiente para afirmar que todos os congelamentos
têm a mesma origem. O usuário relata travamentos aleatórios ao andar, sem
uma sala ou ação específica.

## Próximo teste

Abrir `executar-code-veronica-diagnostico.cmd`, escolher a mesma versão e
jogar normalmente. Ele usa o iniciador DXC já existente e acrescenta apenas
`SPU_FAULT_DIR`, apontando para `codex_diag_hang`.

Em uma nova recusa, são gravados até dois arquivos `.spufault` por processo,
com todos os registradores, os 256KB de LS, o comando DMA e recortes limitados
da RAM apontada pelos registradores e pelos argumentos da tarefa. Leituras
inválidas são ignoradas através de ReadProcessMemory; a captura não deve
criar páginas de memória nem tentar executar a transferência recusada.
Cada arquivo fica abaixo de 450KB. A RAM compartilhada é uma observação
durante a execução concorrente, não uma parada atômica de todas as tarefas.

Se congelar, avisar enquanto a janela ainda estiver aberta permite examinar
também o estado do processo. Não é necessário fechar imediatamente. A captura
de SPU continua em disco mesmo se o usuário precisar encerrar o jogo depois.
Se não houver recusa, não haverá arquivo: nesse caso será preciso investigar
o outro caminho de espera, sem concluir que se trata do mesmo problema.

O iniciador normal e o DXC passam a manter três cópias dos logs anteriores
(`.previous-1.txt` a `.previous-3.txt`) antes de iniciar uma nova execução.
O diagnóstico adicional continua desligado nesses dois atalhos.

## Validação

As três versões compiladas pelo script oficial com `CV_BUILD_JP=1` e
`CV_BUILD_US=1`. Teste isolado `CV_SPU_FAULT_TESTS=1` aprovado: captura exata
de registradores/LS, desligamento por padrão, ponteiros inacessíveis,
limite de 4GB, deduplicação e limite de dois arquivos. Esse teste não valida
a resolução do congelamento; falta reproduzi-lo com a captura ativada.

Os logs e dumps são locais e ignorados pelo Git. Sem commit ou push.

## Segunda captura e correção (19:36)

O usuário reproduziu o congelamento com o diagnóstico. O log foi preservado
em `codex_diag_hang/freeze-20261016-193648.txt`, junto aos dois arquivos
`spu-408-32772234-{1,2}.spufault`. Novamente img=10, PC=0x20D54, os mesmos
três endereços/tamanhos; agora task=30. Após desenho #53266000, só esperas.
O processo 408 já não estava disponível na consulta das 19:37:28; a análise
usa as capturas, sem uma amostra posterior de todas as threads congeladas.

A captura mostra r83=39, o índice retornado pela operação atômica de entrada
do worker (função 0x21518); o contador no bloco 0x43CB51E0 vale 40. A lista
em 0x43CB5594 contém ponteiros de contexto até o índice 38; na posição 39
já aparece 0x008F6C68, que pertence a uma tabela de funções, incompatível com
o bloco de contexto de 4796 bytes esperado pela tarefa. Isso explica de
onde vem o endereço recusado. A duplicação de uma entrada de tarefa é uma
explicação consistente com esses dados, investigada no agendador.

Foi identificada uma corrida concreta em `spu_taskset_task_exited`: tarefas
prontas eram selecionadas sob lock, mas a reserva só acontecia depois de
soltar o lock. Outro finalizador podia selecionar a mesma tarefa nesse
intervalo. Se a primeira execução acabasse antes da segunda reserva, a
segunda despachava novamente a mesma tarefa com trabalho já consumido.
A reserva antecipada de CreateTask existente não cobria esse segundo
caminho do agendador.

A seleção agora reserva TODAS as tarefas escolhidas antes de soltar o lock;
o despacho recebe a reserva já obtida. O código de propriedade/agendamento
foi separado em `runtime/spu/spu_taskset_schedule.c` para testar a rotina
real sem carregar o jogo. Também foi removido `taskset->taskCount++` em
cellSpursCreateTask: o campo do struct antigo fica em +4, dentro do bitset
CSTS_RUNNING do layout real do convidado, e corrompia quatro bytes dele.
Essa escrita é um erro independente, não uma prova isolada da causa do
congelamento.

O teste determinístico da rotina real reproduziu o problema antes da
correção: 6 despachos para 3 tarefas selecionadas, com 6 reservas que podiam
ser roubadas no intervalo. Depois da correção: PASS para corrida no handoff,
finalizador concorrente, exclusão de tarefas em execução, reutilização de
slot e isolamento entre tasksets. Não foi preciso modificar o teste entre
essa reprodução e a validação da correção.

Build oficial EU/JP/US e os testes de agendamento/captura aprovados em
`build-release/freeze-race-after.log`. Reprodução anterior em
`build-release/freeze-race-before.log`. Continuar o teste de gameplay pelo
iniciador de diagnóstico: a ausência de duplicação no teste não substitui
uma partida longa nem exclui outras causas de congelamento. Sem commit/push.

## Terceira captura e publicação concorrente (19:47)

O usuário relatou novo congelamento e confirmou que fechou o jogo manualmente.
Log preservado em `codex_diag_hang/freeze-20261016-194716.txt`, dumps
`spu-26584-33398640-{1,2}.spufault`. Mesmo PC e endereços, agora task=14,
contador=40, índice=39. O bitset CSTS_RUNNING está zerado: a remoção da escrita
nativa incorreta funcionou, mas não resolveu o congelamento. A primeira
correção do coletor de tarefas também não foi suficiente.

O código PPU do próprio jogo confirma uma lista de 39 contextos de 0x12BC
bytes (EU 0x526DC8..0x526E14, limite 0x27). A rotina que lança o trabalho
zera o contador compartilhado em EU 0x52A6D4 antes de criar as tarefas e
faz joins depois; portanto a entrada 39 observada está fora do limite real,
não se trata de um bloco válido cujo alinhamento deva ser ignorado.

Foi encontrada outra corrida: cellSpursCreateTask procurava um slot olhando
somente o bitset guest, sem considerar reservas ainda não publicadas, e o
builder escrevia os bitsets em palavras BE de 32 bits sem compartilhar o
lock do finalizador, que limpa bytes. A publicação de uma tarefa podia
restaurar um bit encerrado por outra thread. A nova reserva consulta os bits
guest e as reservas host sob o mesmo lock; a publicação do TaskInfo e dos
bits também usa esse lock. CreateTask não sobrescreve mais um slot apenas
reservado por outro criador.

Teste determinístico com duas threads e builder simulado para forçar a
intercalação entre leitura/escrita: antes da correção, publicar task 1
restaurou task 0 encerrada (enabled=C0, esperado 40). Depois da correção,
o teste passou, junto com os testes anteriores e de captura. A rotina de
publicação/reserva testada é a de produção; o teste força a intercalação
simulando o RMW do builder, sem carregar o jogo.

O diagnóstico agora também mantém um anel de 512 eventos recentes das
tarefas da imagem 10: criação, despacho, entrada, saída, término e join,
com horário, thread e observação do contador. Só fica ativo quando
SPU_FAULT_DIR está definido; grava `.spufault.events.txt` junto da captura
quando há erro, sem despejar cada evento continuamente no log. O teste da
captura verifica também a retenção limitada a 512 eventos.

Build EU/JP/US e testes aprovados em `build-release/freeze-publish-after.log`;
reprodução do RMW anterior em `build-release/freeze-publish-before.log`.
Ainda falta confirmar o resultado em gameplay; não afirmar que o
congelamento foi eliminado somente com base nos testes sintéticos.
