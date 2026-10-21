# Desempenho da interpolação 60 FPS — pose sobreposta à GPU (v1)

Data: 21/10/2026. Versão JP, 1080p, DXC, 60 FPS com interpolação na CPU, FSR desligado.
Objetivo: reduzir o custo da interpolação sem mudar a imagem e sem tocar no modo 30 FPS.

## Medição antes de mudar

Ferramentas: `CV_POSE_PROFILE=1` e `CV_FRAME_PROFILE=1` (linhas `[POSE-PERF]`, `[FRAME-PERF]` e
`[FRAME-GPU]` a cada 5 s, só numa cópia de diagnóstico), mais o amostrador externo `tsamp` nas
threads `rsx_render` e `frame_clock`.

Sala pesada do primeiro save JP (cena 80000900, câmera parada, mesma posição):

| por quadro | antes |
|---|---|
| intervalo entre quadros | 23,1–24,3 ms (~42 FPS; 37 FPS no overlay) |
| pose (CPU) | 7,1–7,8 ms |
| — histórico/busca | 3,1–3,3 ms |
| — costuras | 1,6–1,8 ms |
| — coleta dos desenhos | 1,2 ms |
| build (montagem dos comandos) | 6,8–7,2 ms (prepass 2,3) |
| espera final pela GPU | 8,9 ms |
| listas da GPU | 13,2–14,1 ms |

Diagnóstico: `render_frame_impl` era serial. A thread de render esperava a GPU terminar o
quadro anterior, calculava a pose (~7,5 ms com a GPU **ociosa**) e só então montava e enviava o
novo quadro. A pose sozinha era a maior parte do tempo morto da GPU.

## Mudança

A pose de um lote lê e escreve só a sua própria metade (paridade) dos buffers: snapshots de CPU,
`vp_vb`, `vp_cb` e registros `vf`. O lote anterior usa a outra metade. Então:

- A thread de render calcula a pose do lote N+1 **antes** de esperar a GPU, enquanto a GPU ainda
  desenha o lote N. A espera pela GPU vem logo depois da pose e antes do build. O FSR também
  continua depois da espera.
- No fim do lote, a thread de render sinaliza uma fence própria (`s_rt_fence`) em vez de esperar a
  GPU, e fica livre na hora.
- O produtor espera essa fence depois de entregar o lote seguinte e **antes** de gravar na metade
  que a GPU ainda pode estar lendo (`render_frame`). Qualquer outro `render_wait_idle` também
  espera a fence, então o contrato antigo ("thread de render ociosa e GPU livre") continua valendo
  para os demais chamadores.

O cálculo da pose não mudou: mesmos dados, mesma ordem, mesmos resultados. Só muda o momento em
que ele roda. O alpha continua vindo do relógio real no instante da pose, como antes.

Escopo: só 60 FPS com interpolação na CPU (`s_pose_on` e sem `pose_compute_mode`). O modo 30 FPS
e a interpolação na GPU (compute) seguem o caminho antigo. `CV_POSE_OVERLAP=0` desliga e volta ao
comportamento anterior. Marcador no log: `[POSE-OVERLAP] v1 on/off`.

Código: `libs/video/rsx_d3d12_backend.c` (`pose_overlap_on`, `rt_fence_signal`, `rt_fence_wait`,
`render_wait_cpu_idle`, `render_wait_idle`, `render_thread_main`, `render_frame`, início de
`render_frame_impl`).

## Depois (mesma sala, mesma câmera)

| por quadro | antes | depois |
|---|---|---|
| intervalo (com perfis ligados) | 23,1–24,3 ms | 21,2–21,9 ms |
| FPS no overlay (atalho normal, sem perfis) | 37 | 47–48, chegando a 60 em alguns trechos |
| thread de render ocupada | 23–24 ms (gargalo) | 15,7–16,3 ms (sobra tempo) |
| espera final pela GPU na thread de render | 8,9 ms | ~0 ms |
| pose / build | 7,5 / 7,0 ms | 8,0 / 7,0 ms (agora em paralelo com a GPU) |

Lógica do jogo continua em 30 Hz (`[FPS60-LOGIC] game_updates_per_second=30`).
Teste do usuário na JP: câmeras e imagem corretas, sem artefatos novos. Nenhum `STUCK` no log.
Build `build-release.cmd` com `CV_BUILD_JP=1` e `CV_BUILD_US=1`: EU, JP e US compilados.

## O que limita agora

A interpolação deixou de ser o gargalo. Na mesma sala, a thread `frame_clock`, que recebe a FIFO do
jogo e grava os desenhos, divide o quadro assim:
- ~13 ms gravando desenhos: `vf_note`, `vp_record_cb`, `rsx_fetch_plan_build` e `vf_flush`;
  a parte da pose (`rsx_fetch_pose_vertex` mais o registro) fica em ~1–1,5 ms;
- ~5,5 ms esperando o jogo publicar comandos (`cellGcm_fifo_kick_wait`);
- ~2,7 ms esperando a GPU liberar a metade do buffer (`rt_fence_wait`, a espera que foi movida).

A thread principal do jogo (`ppu:main`) está 80% ocupada. O jogo renderiza ~46 vezes por segundo
para 30 atualizações de lógica, então o código de render do próprio jogo roda quase duas vezes por
tique.

## v2 — terceiro conjunto de buffers de upload

O produtor passou a esperar ~2,7 ms por quadro (`rt_fence_wait`) antes de gravar na metade que
a GPU ainda lia. Agora os fluxos de upload indexados por `vp_parity` têm **três** conjuntos em vez
de dois: `vp_vb`, `vp_cb`, `vp_fpcb`, registros `vf` e os snapshots de pose. `vp_parity` gira
0→1→2. O produtor grava no conjunto usado dois lotes antes, que a thread de render já esperou na
GPU antes de montar o lote seguinte, então a espera praticamente some. Há uma fence por conjunto
(`s_rt_set_fence`), e o produtor espera só a do conjunto em que vai gravar.

Só no modo que usa a sobreposição (60 FPS + interpolação CPU). `CV_POSE_TRIPLE=0` volta a dois
conjuntos. O modo 30 FPS e a interpolação na GPU continuam com dois. Marcador no log:
`[VPVB] ... x3 parity`.

Custo: ~0,7 GB a mais de memória de upload em RAM (vértices 192 MB, constantes 207 MB,
constantes FP 207 MB e vfetch ~70 MB por conjunto). No overlay, a RAM foi de ~2,3 GB para ~3,0 GB.

Mesma sala e câmera:

| por quadro | v1 | v2 |
|---|---|---|
| intervalo (com perfis) | 21,2–21,9 ms | 20,0–21,1 ms |
| FPS no overlay | 47–48 | 49–51 (19,6 ms) |
| produtor esperando a GPU | ~13% do tempo da thread | não aparece mais na amostragem |
| thread de render ocupada | 15,7–16,3 ms | 15,8–16,4 ms |

Agora o produtor passa ~26% do tempo esperando o próprio jogo publicar comandos
(`cellGcm_fifo_kick_wait`). `ppu:main` está ~78% ocupado e o jogo renderiza ~49 vezes por segundo.
O limite passou a ser o trabalho do próprio jogo mais a gravação dos desenhos.

## v3 — replay: o jogo desenha a 30 e o renderer apresenta cada lote duas vezes

Com `CV_POSE_REPLAY` ligado (padrão no 60 FPS + interpolação CPU + 3 conjuntos), o jogo volta ao
laço nativo de 30 Hz (`CV_FN_FRAME_PACE` não troca mais o limite para 60): uma atualização de
lógica e **um** desenho por tique. Antes ele desenhava cada tique duas vezes. A thread de render
apresenta cada lote duas vezes:
1. na chegada, com a pose interpolada (ponto médio), como antes;
2. meio tique nativo depois (16,7 ms após a primeira apresentação, alvo fixo), com o mesmo lote:
   `pose_restore_batch` desfaz exatamente o que a pose escreveu nos registros `vf` e nas constantes
   de câmera, e o lote é montado de novo (`render_replay_batch`).

O replay não regrava render targets na memória do jogo (`off_rt_writeback_record` só no primeiro
desenho). Com FSR Frame Generation ligado, o replay fica desligado, porque o FSR já gera os
próprios quadros. `CV_POSE_REPLAY=0` volta ao comportamento da v2. O atalho de comparação é
`executar-code-veronica-japones-60-sem-replay.cmd`. Marcadores no log: `[POSE-REPLAY] v1 on/off` e
`override=0` na linha `[FPS60 ...]`.

Primeira versão: o alvo do segundo quadro seguia o intervalo medido do jogo. Como o produtor espera
a thread de render, isso realimentava a lentidão e o jogo travava em ~25 atualizações/s. Corrigido
com o alvo fixo de 30 Hz.

Mesma sala e câmera, depois da correção:

| | v2 | v3 (replay) |
|---|---|---|
| intervalo | ~20 ms (49–51 FPS) | **16,7 ms (60 FPS)** |
| lógica do jogo | 30/s | 30/s |
| desenhos do jogo por segundo | ~49 | 30 |

Teste do usuário na JP: 60 FPS andando e girando a câmera, imagem boa, sem artefatos relatados.

Comportamento diferente: o jogo usa passo fixo nativo. Se não conseguir 30 quadros de jogo por
segundo, o tempo do jogo anda mais devagar, como no modo 30 original. A v2 compensava rodando
mais de uma atualização por desenho. Isso pode ser trazido para o replay depois.

### O que aperta agora (por lote de 33 ms, thread de render)

- pose: ~9 ms
- montagem do primeiro quadro: ~7 ms (prepass 2,3)
- montagem do replay: ~7 ms (os mesmos comandos de novo)
- esperas pela GPU antes de cada montagem: ~10 ms no total
- GPU: ~11 ms por apresentação, ~22 ms por lote

A thread de render fica ocupada quase o lote inteiro. O produtor chega a esperar ~10 ms por ela.
A espera antes do replay existe porque a restauração e o reenvio de texturas alteradas escrevem em
recursos que a GPU do primeiro quadro ainda pode estar lendo.

## v4 — pose calculada pelo produtor, fora da thread de render

No replay, o produtor (thread `frame_clock`, que grava os comandos) termina o lote seguinte e
esperava ~10 ms pela thread de render. Agora ele calcula a pose desse lote nesse tempo, em
`render_frame`, antes de esperar a thread de render. A pose lê e escreve só o conjunto de upload
do próprio lote, que nenhuma GPU usa ainda. A thread de render pula a pose de um lote que já vem
pronto (`RenderBatch.pose_done`).

Para isso, `pose_interpolate_batch_for(const RenderBatch*)` recebe o lote como parâmetro, em vez
de ler o `s_rb` global, e os arrays de mesclagem e saída da pose (`seen`/`out`) passaram a ser
por conjunto. Assim o replay de um lote desfaz as suas posições enquanto o lote seguinte já está
sendo calculado. O cálculo da pose é o mesmo.
Só no modo replay. Sem replay, a pose continua na thread de render (v1/v2).

Limitação: as capturas de diagnóstico F9/F12 leem o estado da pose na thread de render. No modo
replay esse estado pode já ser do lote seguinte. Isso não afeta o jogo normal.

Mesma sala e câmera:

| por apresentação | v3 | v4 |
|---|---|---|
| intervalo | 16,7 ms | 16,7 ms (60 FPS estáveis) |
| thread de render ocupada | 16,6 ms (sem folga) | 10,6–11,9 ms |
| pose na thread de render | 4,5 ms | 0,16 ms |
| lógica do jogo | 30/s | 30/s |

Amostragem: `rsx_render` fica ~32% do tempo esperando trabalho mais ~6% dormindo até o replay.
`frame_clock` fica ~32% esperando o jogo publicar comandos. Agora ele também faz a pose, ~10 ms
por lote. `ppu:main` tem ~28% em yield/usleep (limitador do jogo) e ~8% esperando SPURS. Há folga
em todas as threads nesta sala.

Próximos passos possíveis, nenhum implementado:
1. ~~Triplicar as metades dos buffers de upload~~ (feito na v2).
1b. Pose mais barata (~10 ms por lote no produtor). Agora só daria mais folga ao produtor.
2. Reduzir o custo da gravação por vértice no produtor.
3. Mudança grande: o jogo renderiza só a 30 e o quadro intermediário reaproveita o lote gravado, com
   as poses interpoladas. Isso cortaria quase metade do trabalho do jogo e da gravação, mas pode
   mudar efeitos que avançam por render. Precisa de validação visual cuidadosa.
