# Where we left off (Code Veronica, branch fmv-av-pacing)

Updated 2026-10-14. All code is committed and pushed; latest commit `267510f`.

## CPU reduction (267510f), heavy 3D cutscene: ~4.15 -> ~3.6 cores, 30 fps, audio clean
- `sys_ppu_thread_yield` naps 100 us after 16 rapid yields (`PPU_YIELD_NAP_US`, 0 = off). The game's 30 fps limiter spun on yield; ppu:main went 0.88 -> 0.45 cores.
- GETLLAR polling sleeps until the line is written (`SPU_POLL_WAIT=0` = off). Line 0x43CB5400 went from ~1M GETLLAR/s to ~30k/s.
- Drain loop per-step checks are inline (`spu_step_check`). `spu_depth_guard` no longer writes a shared ring every call.
- CreateTask claims before setting the ready bits (fixes double-run of a task).
- CPU per thread now:
  - SPU short tasks: ~1.5 cores
  - mixer `spu:svc`: 0.49
  - ppu:main: 0.45
  - snddrv: 0.40, real work
  - frame_clock: 0.39
  - rsx_render: 0.27
- Done in the next commit: SPU jobs run on their LS in place (`spu_context_init_ls`, no copy in or out). The gain was small (~0.06 core).
- **Mixer module lifted (local only; `projects/*/spu/` is gitignored).**
  - The sound driver streams a ~32 KB decompressed module to LS 0x33180, in two GETs (0x4000 + 0x3E80) from a heap buffer. It is in no game file.
  - It was lifted from an `SPU_INTERP_DUMP` of image 23 into `spu/spu_0023m_at_33180`, and registered by hand in `spu/spu_workloads.c` as image 61:
    - signature: the first 16 bytes at 0x33180;
    - span: 0x7E80.
  - Entries: 0x33190 and 0x37458 (found with `SPU_INTERP_LOGALL=1`).
  - Mixer round: ~5.2 -> ~4.3 ms; peaks ~7 ms.
  - To redo it:
    1. Cut 0x33180..0x3B000 from the dump.
    2. Run `tools/wrap_spu_elf.py --base 0x33180 --entry 0x33190`.
    3. Run `tools/spu_lifter.py --auto-functions <elf> --extra-funcs 0x33190,0x37458 --symbol-prefix spu_0023m_`.
  - If `spu_workloads.c` is regenerated, re-add the 0023r and 0023m blocks.
- DMA cuts (ae2965a): no measurable gain.
- **Resolution scale (bd34f6b): `RES_SCALE=1.5` gives 1080p.**
  - The user approved it. It is commented out in the launcher (`rem set "RES_SCALE=1.5"`).
  - How it works: the scale is applied only at the D3D12 boundary (resource sizes, viewport/scissor, copy rects).
  - Targets of <= 16384 px stay native, so write-back is unchanged.
  - WPOS goes through `rsx_alpha_k.w`.
  - Copies between targets of different scales are skipped (logged as `[RES_SCALE]`).
  - Measured at 1080p: 30 fps, GPU ~23%, the same as 720p.
- **Depth clip (58fdd66).**
  - The guest-FP PSO now has `DepthClipEnable=TRUE` (`DEPTH_CLIP=0` reverts).
  - It fixed the prison-cutscene shot where a wall behind the camera covered the screen. The user approved it.
- Finding a bad draw:
  - `RSX_CAP=<frame>:<n>:<stride>` (now with step photos) plus `RSXCAP_STEP_RANGE=lo,hi,stride`. The cap is 128 photos per frame.
  - Easier: the user presses F12 in the game during the shot; captures land in `RSX_CAP_DIR/frame_9N`.
  - The title demo cutscene's timing varies by hundreds of frames between runs.
- Frame capture for visual checks:
  - capture: `CELLMARK_DUMP=24 CELLMARK_DUMP_MINDRAWS=300 CELLMARK_DUMP_EVERY=20 CELLMARK_DUMP_DIR=<dir>` writes BMPs;
  - contact sheet: `%TEMP%\cvaudio\sheet.ps1 <dir> <out.png>`;
  - convert or crop to PNG: `topng.ps1`.
- Next candidates:
  - the remaining SPU task cost is lifted code plus DMA (`mfc_for` slot lookup, `mfc_do_transfer`);
  - the mixer's interpreted runtime-loaded code (image 23, LS 0x33190, ~7% of the mixer).
- The SPU task pool is still broken.
  - It freezes deterministically: img=10, pc=0x20D54, GET lsa=0x37DD0 ea=0x008F6C68 size 0x12B0 (ea and lsa misaligned by 8). The code there is interpreted, runtime-loaded code at LS 0x20AE0.
  - The task id varies between runs, but the registers are identical.
  - A 3 ms start delay hides it.
  - It also used MORE CPU than thread-per-task.
- Measuring tools in `%TEMP%\cvaudio`:
  - `go.ps1 "ENV=V ..."` launches the game;
  - `measure.ps1`: process CPU and fps over 40 s of the heavy phase;
  - `threads.ps1`: CPU per thread group;
  - `SPU_LL_HIST=1`: lock-line histogram.

## State of the game
- Lighter scene and 3D cutscenes run at 30 fps.
- **3D cutscene audio is fixed** (user confirmed: smooth). Menus and gameplay were already fine.

## How the audio was fixed (604142c)
- The SPU code (lifted + runtime/spu) was built for bare SSE2: `spu_shufb` used its byte loop and every `spu_fma` called ucrt's software `fmaf`.
- `SPU_ARCH` (default `AVX2`, in the project CMakeLists) now builds them with `/arch:AVX2 -ffp-contract=off`.
- Effect on the audio mixer (taskset 0x00CD7980 task 0):
  - each round, which must fit in 10.7 ms: 11.7 ms -> 6.4 ms;
  - rounds/s: 62 -> 93.5 (about 94 are needed);
  - silent blocks per 80 s of cutscene: ~1230 -> ~21.
- Also: `SPU_COH_MAX_CTX` raised from 8 to 64. Tasks past the 8th never got lock-line-lost wakeups.

## Known leftovers
- Peaks of 11-17 ms per mixer round still happen, mostly as the 3D cutscene starts.
- If crackles come back, the next margin is the per-step overhead of the SPU drain loop, about 10-15% of the mixer:
  - `spu_task_launch_check`, `yz_lockstep_tick`, `spu_check_stack_reset`, `spu_pchist_tick`;
  - about 7% of the mixer is interpreted runtime-loaded code in image 23 (LS 0x33190).
- The **SPU task pool (`SPU_TASK_POOL=1`) still freezes** in the cell cutscene: `[mfc] REJECTED malformed transfer img=10 pc=0x20D54`, ea not 16-aligned with lsa, then a `GETLLAR ea=0` spin. It is reproducible on its own. Priority and TLS resets did not fix it. It is off in the launcher.
- AVX2 is only on the SPU code. The PPU lifted code is still SSE2, and rebuilding it takes tens of minutes.

## Autonomous testing (no user needed)
- With no input, the game boots straight into the cell cutscene ("man behind the bars") about 25 s in. The heavy image-10 phase starts around 40-45 s.
- Scripts are in `%TEMP%\cvaudio`:
  - `run.cmd`: a copy of the launcher with `PS3_SPU_STAT`, `AUDIO_RATE`, `AUDIO_WRITER=1`, `SPURS_EF_STATS` and `AUDIO_WAV=mix.f32`;
  - `sum.sh`: mixer rounds/s in the heavy phase, plus gaps in the dump via `blk.py`.
- Start it with PowerShell `Start-Process cmd /c run.cmd`. Wait for 16 `audio-rate` lines, then `taskkill`.
- Key metric: `[ef-stats] flag=0x00CD9300 waits=/s mean=` (rounds/s and duration).
- Screenshot of the game window: `shot.ps1` (PrintWindow). It can return a stale frame with D3D12.
- Sample only the mixer: `PS3_SAMPLE=1` + `PS3_SAMPLE_THREAD=spu:svc 00CD7980`. Symbolize with `llvm-symbolizer --no-inlines --obj=MyGameRecomp.exe <rva+0x140000000>`.

## Things tried that did NOT work
- **SPU_MAX_RUNNING 10 -> 6**: no effect on the mixer.
- Earlier: raising image-10 priority, VBLANK_HZ=600, clock-only audio pacing (kept).

## Working with the user
- Speak Brazilian Portuguese.
- The user tests through the shortcut "Abrir Code Veronica" (`executar-code-veronica-otimizado.cmd`). Keep diagnostics off there.
- Commit and push after the user confirms a test. Never publish game files, dumps or the exe.
- Build with `projects/code_veronica/build-release.cmd`.

## Launcher (2026-10-14, não commitado até o teste do usuário)
- launcher.cpp (Win32 + GDI+) abre antes do jogo; CV_NO_LAUNCHER=1 pula. Salva em cv_launcher.ini ao lado do exe (valor vazio = usar padrão).
- No JOGAR exporta PS3_VFS_ROOT, PS3_RAP_DIR, PS3_DEV_FLASH, RES_SCALE, WINDOW_MODE, PAD_BTNMAP, PAD_KEYMAP, PS3_TITLE e os ajustes padrão do atalho.
- WINDOW_MODE: window = janela com moldura de no mínimo 1280x720; borderless = cobre o monitor (16:9 centralizado + fundo preto sem foco); fullscreen = igual + topmost + cursor oculto. Esc fecha o jogo.
- Duplo clique: log vai para build-release\bin\run-log.txt (legível com o jogo aberto).
- Uma vez o launcher morreu ao abrir (CoreMessaging.dll 0xe0464645, logo após recompilar); não repetiu em 10 aberturas.
- Testado: as 3 abas, JOGAR em janela/sem borda/tela cheia, 480p, Enter (Start) chega ao jogo.
- 2026-10-14 (2): launcher fica aberto como menu do jogo (thread própria). Insert ou R3+L2 mostra/esconde; com ele em foco o jogo não recebe entrada (g_pad_block). Modo de janela (rsx_d3d12_set_window_mode, Alt+Enter), resolução (rsx_d3d12_set_res_scale: ResizeBuffers + depth + descarta OffRTs na virada do quadro) e controles (cellpad_set_map) mudam sem reiniciar. Fechar a janela do jogo agora encerra o processo (antes seguia rodando sem tela).
- 2026-10-14 (3): AA (FXAA/SMAA Ultra, AA_MODE, troca ao vivo) e patches trocáveis. RETRADUZIR SEMPRE COM:
  python tools/ppu_lifter.py JOGO/NPEB00553/USRDIR/EBOOT.elf --functions out/EBOOT.functions.json --hle-stubs out/EBOOT.imports.json --patches projects/code_veronica/patches.json -o src/recomp
  (sem --patches a chave "Pular animação das portas" deixa de existir). Arquivo novo no runtime exige reconfigurar o CMake (tocar CMakeLists.txt).

## Sem firmware (2026-10-15, não commitado até o teste do usuário)
- O jogo só pedia /dev_flash/sys/external/flashATRAC.pic: o plug-in ATRAC3 da biblioteca MultiStream (mixer de SPU, image 23). Música/vozes/vídeos já eram FFmpeg/at3_standalone.
- Agora o runtime serve um substituto próprio (runtime/syscalls/sys_fs.c, gerado por tools/make_atrac_stub.py) e o despachante de SPU desvia o decode dele para libs/codec/ms_atrac3.c (decodificador ATRAC3 livre). O launcher liga PS3_ATRAC_STANDIN=1, então o firmware nunca é aberto; a aba Jogo não tem mais o campo de firmware.
- Contrato do decodeATRACPackets documentado no topo de ms_atrac3.c. Pegadinhas: polaridade invertida em relação ao FFmpeg; quadros "adiantados" decodificados só para encher a janela (Sony não salva o estado) -- aqui ficam em fila; janela N=1186/1188 em sons com tom alterado.
- Verificado: offline (tools/ms_atrac3_check.c) e no jogo, chamada a chamada contra o plug-in original: posição sempre igual; amostras iguais a poucos LSB, até ~2% em quadros com controle de ganho. CPU igual (3,5 núcleos, 30 FPS).
- spu/spu_0023m_at_33180 é o plug-in da Sony traduzido (local, gitignored); só roda se PS3_ATRAC_STANDIN=0 e houver firmware.

## 2026-10-15: investigacao do cenario externo escuro (historico; resolvido abaixo)
- O usuario reproduziu o mesmo ponto com isqueiro ligado e desligado. Desligado: cenario quase preto; ligado: superficies em cores neon. A causa grafica exata ainda NAO foi confirmada.
- Capturas de 19:43: `rsxcap/frame_90` (4378 ops, ligado) e `frame_91` (2650 ops, desligado). Os arquivos de cada pasta podem incluir sobras de capturas anteriores; conferir o manifesto e os horarios.
- Encontrado e corrigido um erro SOMENTE no diagnostico em `libs/video/rsx_d3d12_backend.c`: a captura usava `s_rb.parity ^ 1` ao ler constantes/vertices, mas a GPU usa `s_rb.parity`. Manifestos anteriores misturavam dados de outro quadro; diferencas entre constantes e o HLSL nesses manifestos nao provam defeito na renderizacao.
- F12 agora fotografa automaticamente, a cada 32 ops, alvos de cena com viewport >= 640x360 e escrita RGB. So executa a coleta nos quadros de captura. Mantem o limite de 128 imagens e a faixa opcional RSXCAP_STEP_RANGE.
- O manifesto lista as imagens da captura atual e indica `step_phase=before_op`: cada imagem mostra o estado ANTES da operacao indicada. Na troca de alvo, uma imagem pode ser do alvo anterior.
- Nao foi alterado o calculo dos shaders, iluminacao, controles ou PC Settings nesta etapa. Build concluida com `projects/code_veronica/build-release.cmd`.
- Proximo teste: na versao recompilada, mesmo ponto/camera, F12 com isqueiro ligado e F12 desligado. Nao precisa definir variaveis de ambiente para as etapas da cena.
## 2026-10-15: correcao do limite dos shaders (validada pelo usuario)
- Capturas corrigidas de 19:54: frame_90 (4414 ops, isqueiro ligado), frame_91 (2653 ops, desligado). Conferidos os primeiros usos de 225 programas: as constantes do manifesto agora coincidem com o bytecode gravado.
- Nas etapas da cena, o personagem aparece antes; as pecas do cenario sao desenhadas no fim. O neon surge entre as imagens anteriores aos ops 4288 e 4320 da captura ligada.
- Leitura SOMENTE de memoria do processo do executavel desta pasta encontrou programas validos com END depois de 4096 bytes, incluindo um de 6592 bytes / 290 instrucoes / 122 constantes. A copia de diagnostico ficou somente no workspace do Codex, fora do Git.
- Defeito encontrado no backend: snapshot, compilacao, hash, constantes e varredura de texturas usavam limite fixo de 4096 bytes. O coletor buffered rejeitava a falta de END, mas o fallback literal aceitava o prefixo e podia exportar zero ou um valor intermediario.
- Alterado libs/video/rsx_d3d12_backend.c: limite comum FP_UCODE_CAP = 16 KiB nesses caminhos; HLSL = 128 KiB para comportar a traducao completa. Mantidos FP_MAX_K=128, tamanho dos buffers de constantes, configuracao de desempenho, controles e PC Settings.
- libs/video/rsx_fp_decompiler.c/.h: caminhos buffered e literal exigem END dentro do limite recebido. Um prefixo incompleto agora retorna erro.
- Captura passa a identificar cada versao pelo par endereco + hash do codigo (fp_ENDERECO_HASH.hlsl), pois o jogo reutiliza o mesmo endereco para shaders diferentes no mesmo quadro. O manifesto aponta o arquivo e tamanho corretos por draw.
- Build aprovada via projects/code_veronica/build-release.cmd (exit 0); log em build-release/graphics-build.log.
- Validacao final em 2026-10-15: usuario confirmou "caraa deu certooooo apertei f12". Conferidas as novas capturas frame_90 (20:17:56, 4382 ops), frame_91 (20:18:22, 2652 ops) e frame_92 (20:18:33, 4347 ops). O mesmo cenario aparece com texturas, grades, paredes e piso visiveis tanto com o isqueiro ligado quanto desligado; o neon deixou de aparecer.
- Manifestos novos registram shaders completos maiores que o limite antigo: ate 5568 bytes nas capturas ligadas e 6592 bytes na desligada. Sem ocorrencias de falha de decompilacao/compilacao de pixel shader no log consultado. Isso confirma o limite de 4 KiB como causa do defeito neste cenario.
- Nao foi feito benchmark de desempenho nesta validacao. O resultado confirmado e visual, no ponto reproduzido pelo usuario. Mudancas locais, sem commit ou push nesta etapa.

## 2026-10-15: armazenamento de shaders conforme o tamanho
- Pedido do usuario apos a correcao visual: permitir programas maiores que 16 KiB sem reservar o tamanho maximo para todos. Esta etapa substitui FP_UCODE_CAP/FP_HLSL_CAP fixos; nao muda as configuracoes de imagem ou desempenho.
- O backend mede ate END, registra fp_bytes por draw e copia somente esses bytes para cada FpJob. Snapshot, hash, constantes, varredura de texturas e captura F12 usam o comprimento completo. Os buffers temporarios de codigo/HLSL sao liberados com o job, tanto no caminho assincrono quanto no sincrono.
- A varredura de memoria convidada verifica as regioes adicionais com VirtualQuery antes de estender a leitura alem da pagina inicial. Limite de seguranca de software: RSX_FP_PROGRAM_LIMIT = 256 KiB por programa. NAO e um maximo de hardware garantido; sem END dentro da memoria legivel/limite, o programa e recusado e o log informa o motivo (mensagens limitadas para nao inundar).
- rsx_fp_program_info tambem conta constantes. END com payload de constante incompleto e invalido. rsx_fp_decompile_alloc comeca com 16 KiB de HLSL e dobra somente quando recebe RSX_FP_OUTPUT_TOO_SMALL (-2), ate RSX_FP_HLSL_LIMIT = 8 MiB. Entrada malformada retorna -1, sem repeticoes de crescimento. O backend reserva adicionalmente espaco para o preambulo e para suas substituicoes de HLSL.
- O buffer GPU de constantes continua com 128 slots. Acima disso, o caminho existente de literais usa agora uma chave de cache que inclui os valores, evitando reutilizar iluminacao de outro draw. Isso pode custar compilacoes adicionais nesses programas; nao foi alegado ganho de FPS.
- Arquivos desta etapa: libs/video/rsx_d3d12_backend.c, rsx_fp_decompiler.c/.h, tests/test_fp_decompiler.c; projects/code_veronica/CMakeLists.txt e build-release.cmd receberam apenas a integracao opcional dos testes. CV_FP_TESTS=1 faz o MESMO build-release.cmd compilar e executar code_veronica_fp_tests; a compilacao normal nao inclui esse alvo EXCLUDE_FROM_ALL.
- Validacao: build-release.cmd com CV_FP_TESTS=1 terminou com exit 0; 51 verificacoes passaram, 0 falhas. Testes incluem shader artificial de 64 KiB / 4096 instrucoes, HLSL maior que 128 KiB, reutilizacao da alocacao, shader pequeno com texto identico ao caminho anterior, falta de END, constante truncada e rejeicao acima do limite. git diff --check aprovado. Log: build-release/shader-growth-build.log.
- A primeira tentativa de regenerar CMake no sandbox falhou ao executar o alias Python instalado; repetir o script com a permissao de execucao resolveu sem alterar a instalacao do Python.
- Executavel atualizado em build-release/bin/MyGameRecomp.exe. Esta etapa passou compilacao e testes sinteticos; ainda nao houve nova confirmacao visual pelo usuario depois desta mudanca. A confirmacao visual do cenario na secao anterior corresponde a versao anterior (limite 16 KiB).
- Mantidas as alteracoes preexistentes do usuario em launcher.cpp, launcher_i18n.h e CMakeLists.txt, alem dos arquivos locais ja existentes. Sem commit/push nesta etapa; branch gpt-experimentos.

## 2026-10-16: DXC opcional, preservando FXC como padrao
- Pedido explicito do usuario: disponibilizar DXC experimental sem remover ou prejudicar o FXC. O experimento anterior continua guardado em stash@{0}; nao foi aplicado em bloco nem removido. A integracao foi feita seletivamente sobre o HEAD b9ece43, que ja suporta EU e JP.
- FXC continua padrao. O iniciador normal executar-code-veronica-otimizado.cmd escolhe SHADER_COMPILER=fxc explicitamente. Novo executar-code-veronica-dxc-experimental.cmd passa --dxc-experimental ao mesmo iniciador, escolhendo DXC apenas naquela sessao e gravando run-log-dxc-experimental.txt. O normal mantem run-log-release.txt. Fechar e reabrir pelo iniciador normal volta ao FXC, sem recompilacao ou limpeza de cache.
- Novo adaptador libs/video/rsx_dxc.cpp/.h: DLLs carregadas em tempo de execucao exclusivamente ao lado do exe; cada worker tem seu IDxcCompiler3. HLSL 2016, -Gis e -O3, com bancos de texturas/samplers separados e precise somente na copia de HLSL do DXC. Nenhuma alteracao nos calculos emitidos pelo tradutor para FXC. O modo experimental ainda pode apresentar as listras/manchas de sombras relatadas no experimento anterior: NAO afirmar que estao resolvidas.
- Backend seleciona o compilador uma vez com InitOnce; antes de DXC verifica SM6 e compila pequenos VS/PS. Falha nessa inicializacao mantem a sessao inteira em FXC, com explicacao no log. Falha posterior de shader e registrada; nao mistura um estagio DXBC com outro DXIL no mesmo PSO e nao detecta defeitos visuais automaticamente.
- Preservados os perfis SM5, flags 0, texto HLSL e nomes/chaves do cache FXC. Os shaders de base que podem se juntar aos shaders traduzidos usam DXC so quando selecionado; no FXC mantem sua rota original. Pipeline library e bytecode DXC ficam separados em shader_cache/dxc-exp1-<identidade DLL>/; o cache normal nao e apagado. A identidade usa tamanho/data das DLLs, mais a revisao de politica exp1.
- dxcompiler.dll e dxil.dll copiadas juntas do Windows SDK 10.0.26100.0 x64 instalado, versao 1.8.2502.11; assinaturas Microsoft verificadas como validas. Somente em build-release/bin, ignoradas pelo Git. Nao sao necessarias para FXC.
- Unico ponto de entrada de compilacao usado: build-release.cmd com CV_DXC_TESTS=1 CV_FP_TESTS=1 CV_BUILD_JP=1. A terceira chave chama o script regional existente pelo ponto de entrada oficial e atualiza MyGameRecomp_JP.exe. EU e JP recompilados com sucesso; log build-release/dxc-experimental-build.log. A primeira tentativa pediu includes COM explicitos no adaptador (Unknwn.h/ObjIdl.h); corrigidos, compilacao final exit 0.
- Validacao: 51 testes do tradutor passaram; 9 testes DXC/FXC passaram, incluindo criacao de um PSO DXIL real com root signature de 8 unidades (sem exigir os 16 samplers). Mais 6 shaders completos de capturas fp_ENDERECO_HASH.hlsl compilaram para DXIL. Copia isolada do teste sem DLLs passou 2 verificacoes de indisponibilidade/diagnostico. git diff --check aprovado.
- Limite da validacao: ainda nao houve teste visual desta entrega dentro do jogo nem benchmark novo FXC x DXC. A imagem das sombras deve ser comparada no mesmo ponto; o ganho de 3x e do relato do experimento anterior, nao uma medicao desta entrega.
- Guia para usuario/manutencao: DXC-EXPERIMENTAL.md. Mantida sem alteracao a edicao local preexistente de native_pc_menu.cpp (CV_MENU_TRACE). Nenhuma mudanca em suporte regional, controles ou configuracoes de imagem. Sem commit/push; branch gpt-experimentos.

### Validacao posterior do usuario: JP e EU com DXC
- Em 2026-10-16, usuario testou primeiro NPJB00135 e relatou imagem perfeita. Log experimental confirmou DXC SM6, workers ativos e 252 arquivos .cso no cache experimental; nenhum erro de compilacao, falha de dispositivo ou retorno ao FXC encontrado.
- Em seguida testou NPEB00553, tambem relatou imagem perfeita e grande melhora percebida. Log de 00:46 confirmou novamente DXC EXPERIMENTAL, 6 workers e biblioteca de pipelines aproveitada (30 KB carregados); o cache experimental passou a 263 arquivos .cso. Nenhuma falha de shader/dispositivo nem fallback FXC registrado.
- A melhora de desempenho e relato do usuario, sem benchmark comparativo controlado. A segunda execucao ja aproveitou cache anterior; nao atribuir todo o ganho ao compilador nem afirmar um percentual novo. A validacao visual corresponde aos trechos testados, nao a todos os cenarios/sombras do jogo. FXC permanece padrao e DXC opt-in.

## 2026-10-16: aviso de .rap no launcher
- Pedido do usuario: avisar na aba Jogo que a versao japonesa BIOHAZARD CODE: Veronica Kanzenban (NPJB00135) nao precisa de .rap, enquanto as outras precisam da licenca correspondente.
- launcher.cpp desenha o aviso abaixo do campo de licenca com quebra de linha e suporte a texto arabe da direita para a esquerda. check_rap mostra o status traduzido "Esta versao japonesa nao precisa de arquivo .rap." quando a pasta selecionada e NPJB00135; a checagem das demais permanece igual. Apenas apresentacao: nenhuma alteracao na descriptografia/licencas ou shaders.
- launcher_i18n.h recebeu aviso e status nos 13 idiomas: pt, en, es, ja, fr, de, it, pl, tr, ar, zh, ko, ru. static_assert confere a quantidade de mensagens em cada idioma durante a compilacao.
- EU e JP recompilados via build-release.cmd com CV_BUILD_JP=1, exit 0. Log build-release/launcher-rap-notice-build.log. Medicao de layout GDI+ verificou que o aviso cabe na area de 655x72 pixels nos 13 idiomas; git diff --check aprovado. Sem commit/push.

## 2026-10-16: credito de Kastiel no launcher
- A pedido do usuario, adicionado "Feito por Kastiel" discretamente no rodape da barra lateral, visivel em todas as abas. Grafia confirmada pelo usuario: K-A-S-T-I-E-L.
- Credito traduzido para os 13 idiomas do launcher, com o nome preservado. Usa a fonte pequena existente (12 px) e a cor secundaria; nao adiciona botoes ou muda a navegacao.
- launcher.cpp e launcher_i18n.h atualizados; EU e JP recompilados pelo build-release.cmd com CV_BUILD_JP=1, exit 0. Log build-release/launcher-credit-build.log. static_assert conferiu as tabelas e medicao GDI+ confirmou que todas as traducoes cabem no rodape de 186x22 px. Sem commit/push.


## 2026-10-16: pasta configurável de saves e suporte US NPUB30467
- Pedido do usuário: selecionar onde o jogo realmente salva/carrega e adicionar a edição americana fornecida em D:/Jogos/rpcs3-v0.0.33-17018-0a57c459_win64/dev_hdd0/game/NPUB30467.
- Entrega detalhada, hashes, endereços, instruções e limites de validação: **SAVES-E-VERSAO-AMERICANA.md**.
- Launcher: aba Jogo com Pasta dos saves/Procurar/Abrir/Usar pasta padrão, nos 13 idiomas. Valor vazio mantém exatamente ./gamedata/dev_hdd0/home/00000001/savedata; não move saves existentes. Mudanças só na próxima sessão. INI novo criado com BOM UTF-16 para preservar caminhos Unicode.
- Runtime cellSaveData recebe raiz UTF-8 uma vez antes de executar o convidado; Windows usa APIs de arquivo Unicode. Sem alterar arquivos do jogo nem direcionar a VFS inteira para a pasta dos saves.
- US: ELF SHA-1 93d060e5019d877df39cd091c28efff920b98e49, APP_VER 01.00. 17045 funções OPD, 17020 com mesmo tamanho EU. Lift próprio em src/recomp_us/ (gitignored), MyGameRecomp_US.exe no mesmo bin de EU/JP. CV_REGION_US/cv_version.h/hooks e guard do patch de portas adaptados e conferidos. Lista de regiões do launcher ampliada; resolução do .rap por região também procura na pasta do jogo.
- Compilar SOMENTE por build-release.cmd, agora com opção CV_BUILD_US=1. CV_BUILD_JP=1 atualiza JP na mesma chamada e CV_SAVE_TESTS=1 testa gravação/leitura real em pastas temporárias. Três executáveis compilados com sucesso, exit 0, log build-release/saves-us-final-build.log.
- Testes save/load: padrão, personalizado, espaços/Unicode; metadados/conteúdo, caminhos inválidos. Passaram. Layout dos novos textos medido nos 13 idiomas.
- US iniciou de verdade com FXC e o usuário assumiu o controle do teste. Log reconheceu NPUB30467 e registrou gravação real concluída às 01:38:18 (DATA0.DAT 31876 bytes + metadados/imagens).
- ATENÇÃO: a janela de teste foi iniciada sem launcher, com saves isolados em projects/code_veronica/codex_diag_us/boot/isolated-saves. Usuário foi avisado. Não apagar essa pasta, pode conter progresso real dele. Para continuar esse save pela abertura normal, selecionar essa raiz na aba Jogo ou copiar a subpasta NPUB304670 para o destino escolhido com o jogo fechado.
- Não foi modificada a pasta de jogo selecionada no INI durante o teste. Para usar US pelo iniciador normal/DXC, selecionar NPUB30467 na aba Jogo; ele troca automaticamente para o exe correto. Ajustes de PC Settings feitos pelo próprio usuário nessa janela podem estar salvos no INI.
- Não encerrar o jogo do usuário automaticamente. Não afirmar validação de todos os cenários/slots/carregamento manual da US. EU/JP recompilados, perfis mantidos. FXC/DXC, controles e mudanças preexistentes CV_MENU_TRACE preservados. Branch gpt-experimentos; sem commit/push.


## 2026-10-16: versões compatíveis na tela inicial
- A pedido do usuário, incluído um aviso discreto abaixo dos ajustes/status da página Início: "Versões compatíveis (PSN 01.00)" e "Europa: NPEB00553 · Japão: NPJB00135 · América: NPUB30467".
- Traduzido para os 13 idiomas; códigos preservados. Usa fonte pequena/cor secundária e não interfere no botão Jogar ou nos avisos do rodapé. Continua indicando a região selecionada no banner.
- Apenas launcher.cpp/launcher_i18n.h nesta mudança, além desta anotação. Nenhuma alteração em saves, controles, patches, shaders ou arquivos do jogo.
- Três executáveis EU/JP/US recompilados pelo build-release.cmd com CV_BUILD_JP=1 CV_BUILD_US=1, exit 0. Log build-release/launcher-supported-versions-build.log. static_assert das 13 tabelas aprovado; medição GDI+ confirmou que a linha cabe em todos os idiomas. git diff --check aprovado. Sem commit/push, branch gpt-experimentos.


## 2026-10-16: RESET COLORS no PC Settings nativo
- Pedido do usuário: restaurar os ajustes de cor dentro do PC Settings do jogo, preservando resolução. Implementada a linha **RESET COLORS**, entre SATURATION e BACK, seguindo o idioma inglês já usado neste menu nativo.
- Confirmar restaura brilho 0%, contraste 100% e saturação 100%. São os valores neutros do pós-processamento PC: removem esses ajustes adicionais, sem alterar o Monitor Tuning original do jogo. Usa cv_pc_settings_change nas linhas 3, 4 e 5, que aplica na hora e persiste pelos caminhos existentes do launcher ou fallback sem launcher.
- Mantém resolução, anti-serrilhado, modo de janela, controles e patches. Não escreve preferências reais até o usuário confirmar a ação. Esquerda/direita na linha de reset não executa; cancelar tem prioridade sobre confirmar.
- O menu passa a oito linhas; espaçamento ajustado para manter BACK dentro do painel. Dica contextual mostra CONFIRM TO RESET COLORS ou CONFIRM TO GO BACK nas duas ações. Fonte, suavização e caminho de lote único mantidos. Navegação para cima/baixo e retorno atualizados para os novos índices.
- Arquivo funcional: native_pc_menu.cpp, preservando as alterações CV_MENU_TRACE preexistentes. Teste novo tests/test_pc_menu.cpp exercita o próprio hook com memória/input sintéticos e callbacks de configuração monitorados, sem abrir jogo/GPU ou tocar no INI/saves reais. Testa confirmação, cores neutras, três valores de exibição preservados, esquerda/direita sem reset, cancelar+confirmar sem reset, BACK, navegação circular, reabertura e ajuste existente de um ponto. Também verifica limites da geometria; maior lote observado 52390 quads, abaixo de 131072.
- CMakeLists.txt e build-release.cmd: teste opcional code_veronica_menu_tests / CV_MENU_TESTS=1. Não entra na build normal. CV_BUILD_JP=1 CV_BUILD_US=1 CV_MENU_TESTS=1 via build-release.cmd concluiu exit 0; as duas linhas [pc-menu-test] PASS estão em build-release/pc-reset-colors-build.log. Três executáveis atualizados em build-release/bin. git diff --check aprovado.
- Esta entrega passou compilação e testes de input/geometria; ainda não houve nova validação visual do reset pelo usuário no jogo, nem benchmark. Não afirmar que o teste sintético verificou imagem final ou persistência real do INI. Branch gpt-experimentos, sem commit/push.
