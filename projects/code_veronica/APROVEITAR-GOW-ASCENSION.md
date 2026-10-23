# O que aproveitar do gow-ascension-pc para o desempenho do Code Veronica

Data: 23/10/2026. Só leitura e análise, nenhum código alterado.
Fonte: https://github.com/arnaldolima1/gow-ascension-pc (commit `80fd6b2`, 22/10/2026).

## Resumo

- O projeto deles é o mesmo tipo de port do nosso: PPU e SPU traduzidos para C++ sobre o
  runtime do **ps3recomp** (sp00nznet), que é a mesma base do nosso repositório (`upstream`).
  O repositório público tem só o projeto do jogo, o launcher e **44 patches** (`patches/*.patch`)
  sobre o runtime. O fork completo do runtime deles (`ps3kit`) não é público (o link dá 404).
- Eles estão bem atrás em velocidade: ~16-17 fps no primeiro nível, com as SPUs como gargalo.
  Por isso quase todo o trabalho de desempenho deles está na SPU, e é exatamente aí que dá para
  aproveitar algo.
- O renderizador que desenha o jogo deles é o RSX do **RPCS3** (GPL-2.0). O backend próprio
  (live draw, D3D12) está atrás do nosso `rsx_d3d12_backend.c`, que já tem cache de PSO
  (`ID3D12PipelineLibrary`), cache de shaders em disco, rastreio por `GetWriteWatch` e escolha da
  GPU de alto desempenho. No gráfico não há quase nada a ganhar.
- **A ideia de maior valor:** as formas SSE (`pshufb`) das operações de bytes da SPU
  (`rotqby`, `shlqbyi`, `rotqbyi`, `rotqmby`…). No nosso código elas ainda são laços de bytes, e
  o perfil que já coletamos mostra que isso está no caminho mais quente da imagem SPU 10.

## Licença

- O `LICENSE` deles é **MIT** (Ascension contributors) e vale para o repositório, patches
  incluídos. O ps3recomp, base dos dois projetos, também é MIT (Ned Heller / sp00nznet).
- **Dá para reaproveitar código diretamente** dos patches, mantendo o aviso de copyright e a
  licença MIT, por exemplo num cabeçalho ou num `THIRD-PARTY-NOTICES`.
- **Exceção:** tudo o que vem do RPCS3 (o renderizador RSX e o que for "como o RPCS3 faz",
  copiado de lá) é GPL-2.0. Um build com isso passa a ser GPL-2.0 (o README deles diz isso).
  Desse lado, só aproveitar ideias. Os trechos recomendados abaixo são código próprio deles,
  sob MIT.
- O que não está nos patches (afinidade de threads, `SPU_XFLOAT_APPROX`, `SPU_LS_NO_HOOKS`,
  replay de jobs, a reescrita nativa do módulo 218) fica no `ps3kit`, que não é público.
  Desses, só a ideia.

## O que medimos do nosso lado antes de priorizar

Perfil já coletado pelo `MyGameRecomp_JP_WorkProfile.exe` (sala pesada, 20/10,
`work-profile/cpu-20261020-215822-8132-1.jsonl`), simbolizado com o PDB do mesmo build:

- O escopo "SPU levantada" da **imagem 10** (`spu_0009_at_00940C00`) gastou **~67 s de CPU numa
  janela de ~30 s**. São mais de dois núcleos inteiros, mais da metade da CPU observada do
  processo. A imagem 23 (mixer de áudio) gastou ~12,7 s.
- Das amostras da imagem 10 que estavam **executando** (excluídas as threads dormindo):
  - ~62% no código levantado;
  - ~30% no caminho MFC/canais (`mfc_submit` 6,6%, `spu_wrch` 6,3%, `spu_drain_call` 6,3%,
    `mfc_channel_write` 5,1%, `mfc_for` 4,5%, `mfc_do_transfer` 3,5%).
- As funções levantadas mais quentes (`B7F0`, `B830`, `B848`, `B924`, ~40% do código
  levantado amostrado) são laços curtos de `spu_ls_read128` + **`spu_rotqby`**: o idioma de
  leitura escalar desalinhada da SPU.
- A desmontagem do `.exe` mostra o problema: cada `spu_rotqby` virou **16 leituras de byte +
  16 gravações de byte** em `ctx->gpr[]` e, logo depois, uma leitura `vmovdqa` de 16 bytes do
  mesmo lugar. Isso causa um stall de *store forwarding* (gravações pequenas lidas por uma carga
  larga). A amostra mais quente da imagem cai exatamente na instrução depois desse `vmovdqa`.
- No nosso código SPU levantado (todas as imagens) há **8.436** chamadas a `spu_rotqby`,
  **3.745** a `spu_rotqbyi`, **2.995** a `spu_shlqbyi`, 572 a `spu_rotqmbyi` e 140 a
  `spu_rotqmby`. Só `spu_shufb` (11.938) já usa `pshufb`; as outras usam o laço de bytes.

Ressalva: na sala pesada, o quadro também depende da thread de render (pose ~7 ms + montagem
~7 ms) e da GPU (`FPS60-DESEMPENHO-INTERPOLACAO.md`). Cortar CPU da SPU com certeza libera
núcleos. Se isso vira fps depende de quanto o produtor espera a imagem 10, e isso só uma
medição A/B responde.

## Lista priorizada

### 1. Operações de bytes da SPU em SSE (`pshufb`): prioridade ALTA

- **Referência deles:**
  - `patches/ps3recomp-0033-sse-byte-ops-fma-exptex-rsx-copies.patch`, trecho de
    `runtime/spu/spu_helpers.h`: `spu__x_iota3`, `spu__x_perm`, `spu__x_rotby`,
    `spu__x_shlby`, `spu__x_shrby` e as formas de `spu_rotqby`, `spu_shlqby`, `spu_rotqbybi`,
    `spu_shlqbybi`, `spu_rotqmby`, `spu_rotqmbybi`, `spu_rotqmbyi`, mais `spu_cntb` por tabela
    de nibbles;
  - `patches/ps3recomp-0027-sse-ops-interp-ed-job-hist.patch`: `spu_selb`, `spu_rotqbyi`,
    `spu_shlqbyi` em SSE, `fceq/fcgt/fcmeq/fcmgt` e `csflt`;
  - testes bit a bit contra as formas `_ref`: `runtime/spu/tests/test_spu_qby_sse.c` (0033) e
    `test_spu_sse_ops.c` (0027).
- **Onde entra no nosso código:** `runtime/spu/spu_helpers.h`: `spu_cntb` (l. 155),
  `spu_shlqbyi`/`spu_rotqbyi` (l. 260-261), `spu_shlqby`/`spu_rotqby` (l. 355-356) e as
  variantes `bi`/`mby`. A ordem de bytes é a mesma: nosso `SPU_W(P)` é `P ^ 3`, a mesma
  convenção (`iota3`) que eles usam. O código encaixa sem adaptação de layout. Já compilamos
  SPU com `/arch:AVX2`, então `__SSE4_1__` está definido.
- **Ganho esperado:** alto na CPU da imagem 10. Cada `rotqby` sai de ~50 instruções + stall
  para ~5 instruções. Nas funções quentes, que são quase só isso, a estimativa grosseira é de
  15-30% a menos de CPU da imagem 10, e algo no mixer (imagem 23). Eles mediram 110-340
  instruções por operação no x86, e o menu deles foi de 12 para 30 fps com esse patch e outros.
- **Risco:** baixo. Mudança local, com teste diferencial bit a bit contra as formas antigas
  (mantê-las como `_ref`). Recompila o runtime e os TUs da SPU, não o PPU. Validar com o
  `code_veronica_spu_native_tests` e um A/B de gameplay.

### 2. Custo fixo por bloco levantado (trampolim, TLS e /GS): prioridade MÉDIA (achado nosso)

- Não é técnica deles. Apareceu na mesma desmontagem e vale olhar junto com o item 1.
- Cada saída de bloco grava `ctx->pc`, lê `g_spu_trampoline_fn` por TLS
  (`gs:0x58` + índice) e testa um byte de guarda antes de chamar uma rotina. Parece a
  inicialização sob demanda de TLS. Também há verificação de cookie `/GS` no fim das funções
  que têm o array temporário do `rotqby` na pilha. O cookie tende a sumir sozinho com o item 1.
- **Referência deles (a ideia, não esse detalhe):** `--merge-chunks` (0040/0044) junta blocos de
  um laço numa função só. Eles não mediram ganho no nível, mas os nossos laços quentes
  (`B7F0 → B830 → B848 → B7F0`) passam pelo `spu_drain_call` a cada volta, e esse custo é 6% das
  amostras da imagem 10.
- **Onde:** flags dos TUs SPU em `projects/code_veronica/CMakeLists.txt` (l. 325-333: avaliar
  `/GS-` e a guarda de TLS) e, no lifter, juntar os blocos de um laço.
- **Ganho:** pequeno a médio. **Risco:** baixo para as flags; médio para mudar o lifter.

### 3. `mfc_for` e o caminho de MFC: prioridade MÉDIA

- **Referência deles:** 0027 (`mfc_for` com "last hit" por thread e uma geração incrementada
  quando um engine é liberado) e 0043 (cache de faixa dos slots residentes).
- **Nosso estado:** `runtime/spu/spu_channels.c:450` já tem a dica por contexto
  (`ctx->mfc_slot1`), sem lock. Mesmo assim `mfc_for` aparece com 4,5% e o conjunto MFC com
  ~30% das amostras da imagem 10. Vale descobrir se a dica está errando (contexto trocando de
  slot) ou se o custo está em `mfc_submit`/`mfc_do_transfer` (`spu_dma.h:1225` e `:593`).
- **Ganho:** médio na imagem 10. **Risco:** baixo a médio (DMA é sensível; usar o teste
  diferencial que já existe para a SPU nativa).

### 4. Modo de ponto flutuante da SPU (FTZ/DAZ e o caminho rápido): prioridade MÉDIA-BAIXA

- **Referência deles:** 0024 (float SSE4.1), 0032 (`spu_run_with_halt` põe a thread em
  arredondamento para zero + FTZ + DAZ enquanto roda código SPU; `-frounding-math`) e 0033
  (`vfmadd` sob `__FMA__`, `SPU_ASSUME_SPU_FP`).
- **Nosso estado:** `spu_fa/fm/fma` (l. 277-295) já são floats IEEE do host com `fmaf`. Com
  AVX2, isso já é o caminho "aproximado". O que falta é **FTZ/DAZ**: a SPU zera denormais, e o
  x86 sem FTZ/DAZ paga uma penalidade grande de microcódigo quando aparece um denormal.
- **Onde:** onde a thread SPU entra no código levantado (`spu_run_lifted_job_abi` e
  `spu_lifted_thread.c`), ligando FTZ/DAZ no MXCSR.
- **Ganho:** incerto. Só aparece se a geometria ou o áudio gerarem denormais. **Risco:** médio.
  Muda resultados numéricos em casos-limite (mais fiel à SPU, mas diferente do que temos hoje),
  então precisa de A/B visual e de áudio.

### 5. Afinidade de threads (SPU por núcleo, PPU principal no núcleo 0): prioridade MÉDIA-BAIXA

- **Referência deles:** só a ideia, em `host/ascension_defaults.cpp` (`SPURS_PIN=1`,
  `PS3_PIN_PPU=1`). O código está no `ps3kit` privado. Eles mediram 14,8 → 15,6 fps (SPUs
  fixas) e 15,8 → 16,7 fps (PPU principal no núcleo 0).
- **Onde:** criação das threads SPU (`runtime/spu/spu_workload.c`,
  `spu_lifted_thread.c`) e as threads `frame_clock`/`rsx_render` em `main.cpp` (~l. 650).
- **Ganho:** ~5% para eles. Para nós, que já estamos perto de 60, o ganho seria só na sala
  pesada. **Risco:** médio. Em CPUs híbridas (Intel com núcleos P/E) fixar no núcleo errado
  piora, e o jogo roda na máquina de outras pessoas. Fazer opcional, detectar núcleos físicos
  e P-cores (`GetLogicalProcessorInformationEx`), e preferir `SetThreadIdealProcessor` a uma
  máscara rígida.

### 6. `sys_timer_usleep` com timer de alta resolução: prioridade BAIXA (economia de CPU)

- **Referência deles:** `patches/ps3recomp-0034-usleep-hr-timer.patch`. Esperas de 200 µs ou
  mais usam um timer `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` **por thread**, em vez de girar
  até o prazo do QPC. Eles caíram de 54% para 48% de CPU com o mesmo fps.
- **Nosso estado:** `runtime/syscalls/sys_timer.c:295-323`. A partir de 1 ms cria e destrói
  um timer comum a cada chamada; abaixo de 1 ms gira com `SwitchToThread`. O `[TL]` mostra a
  MAIN com ~4 usleeps por quadro.
- **Ganho:** menos CPU, menos calor e mais folga para o FSR FG em notebooks; pouco ou nenhum
  fps. **Risco:** baixo a médio (pacing de threads que dependem de microssegundos; manter o giro
  abaixo de 200 µs, como eles).

### 7. Código SPU carregado em tempo de execução (fragmentos PIC): prioridade BAIXA (áudio)

- **Referência deles:** 0023 (`spu_lifter --pic`, `tools/lift_spu_pic.py`,
  `spu_overlay_register_pic`), 0031 (fragmentos achados por conteúdo e dentro de GETs maiores,
  32 slots) e 0043.
- **Nosso estado:** `PROXIMO-CHAT.md` registra ~7% do mixer (imagem 23, LS 0x33190) em código
  interpretado carregado em tempo de execução.
- **Ganho:** ~7% do mixer, margem contra estalos de áudio. **Risco:** médio (mexe no lifter e
  no despacho).

### 8. Itens menores

- **Hash em quatro pistas** (0033, `rsx_live_draw.c`): o hash FNV de uma cadeia só, em
  `rsx_d3d12_backend.c:4241` (textura de vértices, até 1 MB por quadro na matriz do CV), fica
  ~4x mais rápido com quatro cadeias independentes. Ganho de décimos de ms; risco baixo.
  **Não** mexer em `texpack_hash`: os mods de textura dependem dos valores atuais.
- **Ganchos de LS compilados fora** (`SPU_LS_NO_HOOKS`, ideia deles): `spu_ls_read128`
  (`spu_context.h:555`) ainda testa `g_spu_ls_probe` e `image_id == 2 && policy_mode` a cada
  leitura. Num build de release dá para tirar isso em tempo de compilação. Ganho pequeno.
- **Métodos de validação** que valem copiar como ideia para a reescrita nativa da imagem 10:
  - `SPU_SHADOW` (0028): roda a mesma função levantada e interpretada em duas cópias do
    contexto e compara;
  - o *replay* de jobs gravados fora do jogo, com comparação byte a byte e tempo
    (`scripts/windows/replay.ps1`);
  - o painel de desempenho com SPU/RSX/espera (0042).

  O nosso `CV_SPU_NATIVE_VERIFY` já faz parte disso.

## O que não serve (e por quê)

- **Renderizador RSX do RPCS3:** GPL-2.0, e o nosso backend D3D12 já está à frente.
- **Vblank no QPC a 59,94 Hz** (0025): `main.cpp:248-261` já usa QPC.
- **Stores inline no PPU** (0038): `runtime/ppu/ppu_memory.h:190-203` já é inline.
- **Ganchos por passo da SPU** (0041): lockstep, `spu_step_check` e `spu_depth_guard` já
  estão protegidos por flags no nosso `spu_drain.c`/`spu_context.h`.
- **Esperas de FIFO/relatório do SPURS** (`GCM_PARK_*`, `GCM_REPORT_SPU_HOLD_MS`,
  `GCM_ROUND_GATE`, `SPURS_READY_LEVEL`, `SPURS_MAXCONT`): são correções de sincronização do
  God of War (Wws/RsxSync), não ganho geral. `SPURS_IDLE_BACKOFF_MAX=0` deu +8% para eles, mas
  o nosso `cellSpurs.c:1798` tem o mesmo backoff e o CV usa pouco o caminho de policy module.
  Só vale testar se um perfil mostrar o kernel SPURS na espera.
- **Logs por quadro desligados** (`PS3_VERBOSE=0`): já fazemos (diagnósticos desligados no
  atalho).

## Próximo passo sugerido

Fazer o item 1 sozinho, com o teste bit a bit, e medir A/B na sala pesada com a cópia de
diagnóstico (`WORK-PROFILE`): CPU do escopo da imagem 10, `PRODUCER-PERF` e fps. Se o
produtor ficar visivelmente mais rápido, seguir para os itens 2 e 3 na mesma imagem.
