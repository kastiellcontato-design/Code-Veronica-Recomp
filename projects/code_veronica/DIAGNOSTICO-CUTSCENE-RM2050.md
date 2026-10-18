# Diagnostico: disparos do Steve na rm_2050

Pedido do usuario em 2026-10-18: a cutscene da area 2, sala 05 engasga enquanto
Steve dispara a metralhadora, recuperando quando os disparos param. Causa ainda
NAO determinada. Nao atribuir automaticamente a shaders ou audio.

Atalho: executar-code-veronica-diagnostico-cutscene.cmd. Usa o executavel atual
pelo iniciador otimizado, mantem as escolhas do launcher e o cache. Nao requer
build nem muda codigo do renderer, logica ou arquivos originais do jogo.

Liga HITCHDBG=1 e PS3_TIMELINE=1; PERF=0 evita timers por draw nesta primeira
medicao. Esses contadores ja existem no executavel:
- [HITCH]: intervalos de apresentacao acima de 45 ms, numero/tempo de compilacao
  de VS, construcao inline de fragment shaders, espera por shader e quantidade
  de compilacoes em segundo plano. Sao tempos de parede, nao tempo GPU medido.
- [TL-HITCH]: intervalos acima de 60 ms, com diferencas dos tempos acumulados
  por etapa no intervalo. CPU/RSX/render/SPU podem se sobrepor: nao somar tudo
  como se fossem parcelas exclusivas. Ha limite de 400 registros por processo.
- [TL] e [GPUCMD]: resumo a cada 5 segundos de apresentacoes, logica, SPU joins,
  esperas, render e comandos; GPU busy span quando disponivel. Medias de janela
  podem esconder picos, por isso analisar tambem os registros por hitch.

Procedimento: usuario fecha a execucao normal, abre o atalho acima, carrega o
save, espera aproximadamente 10 s, reproduz a cutscene e espera mais 10 s apos
os tiros. Deixa o jogo aberto e avisa. Nao enviar inputs sinteticos ao jogo.
O diagnostico e lido em run-log-release.txt; o runner preserva tres execucoes
anteriores. Nao apagar shader/PSO cache nem trocar compilador/opcoes no primeiro
ensaio: medir o problema com as mesmas condicoes de uso.

Triagem depois da reproducao:
1. Picos junto a VS/FP inline/esperas indicam compilacao ou espera por recurso;
   distinguir custo de compilar do custo de criar PSO, que nao e integralmente
   atribuido pelo contador atual. Uma repeticao sem reiniciar testa cache quente.
2. Picos de draw-calls/render/GPU sugerem custo dos efeitos; checar GPU span e
   esperas antes de atribuir gargalo ao processador ou placa de video.
3. MAIN-spu-join/evflag/lwmutex/drain altos sugerem dependencia na logica/RSX/SPU;
   investigar a thread correspondente, e audio se houver evidencia de decode.
4. Caso o intervalo nao seja explicado pelos contadores existentes, adicionar
   instrumentacao focada apenas na etapa suspeita, evitando log por draw/tiro.

Estado: atalho preparado, reproduzir com o usuario. Nenhuma correcao anunciada,
nenhum teste da cena executado pelo agente, nenhum novo commit ou push feito.

## Primeira reproducao
Usuario confirmou as mesmas travadas. Na rm_2050: frames de 49-54 ms sem compilacao VS/FP inline nem espera por shader; criacao de PSO ainda precisa ser medida. Excluir carga de sala e pausas por foco.
CV_CUTSCENE_DIAG=1 adiciona tempos de pipeline/textura acima de 2 ms e fases de render acima de 20 ms; limitado por processo, PERF=0. Nova compilacao/reproducao pendentes.

Compilacao oficial concluida em 2026-10-18: EU, JP e US linkados, CV_BUILD_JP=1 e CV_BUILD_US=1. diff --check sem erros. Aguardando reproducao pelo usuario; ainda nao e uma correcao do stutter.

## Segunda reproducao: capacidade de comandos
Snapshot local: build-release/diagnostics/rm2050-focused-pass.txt (nao publicar).
Na rm_2050, linhas 2830-2838: oito avisos MAX_DRAWS=12288 (limite do log).
O renderer descarta a cauda do lote; isso pode omitir a copia final para a
imagem exibida apesar de Present/TL continuarem a aproximadamente 30 FPS.
Nao ha CV-DIAG de pipeline/textura nesta cena; render nos picos ~21-23 ms,
Present ~0.13 ms e fences de entrada/fim abaixo de 0.02 ms. Nao houve pausa
de 1-2 segundos medida na apresentacao durante a cena. O sintoma pode ser
imagem repetida por lotes incompletos; confirmar visualmente apos ajuste.
MAX_DRAWS aumentado para 24576, mantendo ordem, snapshots e dimensionamento
derivado dos buffers/descriptors. Custo extra de buffers VP+FP: ~396 MiB,
alem de registros/descriptors/historico. CV-CAPACITY registra lotes >=12288
para confirmar uso da nova capacidade. Sem commit/push. Compilacao pendente.

Compilacao da capacidade ampliada concluida nas tres regioes (EU/JP/US),
sem falhas de compilacao/link. VP_FPCB_STRIDE atual tambem e 8448 bytes: os
dois buffers de constantes somados passam de ~396 para ~792 MiB.
Aguardando validacao visual e log de CV-CAPACITY pelo usuario.

## Validacao final do aumento de capacidade
Usuario confirmou melhora substancial e autorizou commit da correcao.
Ultima reproducao: 45 lotes >=12288, pico 15812/24576, zero avisos de
MAX_DRAWS. A capacidade atual tem ~55% de folga sobre o pico observado.
Persistiu lentidao: gravacao/submissao do render ~31-37 ms em parte da
rajada; janela TL de 28.1 FPS. Nao aumentar novamente sem evidencia de
novo estouro. Otimizacao de custo de processamento e tarefa separada.
A correcao e compilada no backend compartilhado EU/JP/US e vale pelo
atalho principal executar-code-veronica-otimizado.cmd. O atalho diagnostico
apenas habilita contadores adicionais. A instrumentacao optativa permanece
local, fora deste commit; commit inclui somente capacidade e este MD.
Validacao: build-release.cmd com CV_BUILD_JP=1 e CV_BUILD_US=1, tres
executaveis linkados; reproducao manual JP pelo usuario. Sem push nesta etapa.
