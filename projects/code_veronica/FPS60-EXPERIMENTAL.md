# v37 — Recomp Settings e modo normal30/60,JP/EU/US

Escopo autorizado mudou:usuario quer pacote atual e correcao tambemUS/EU.
Compilar MyGameRecomp nas3arvores,sem alterarEBOOT/saves/configpessoal.

UI:submit/done flipcontadores nao coincidem com ritmo de atualizacaodomenu.
Antes UIexigia flip exato e menu exigia delta1. Agora menu aceita gap1..4
somente60+interpolacao;backend usa lote maisrecente naofuturo,idade<=3flips.
8slots;vazio e tombstone prioritario,dados expiram sem vazarUI para gameplay.
30mantem gap1/exactframe. uint32wrap seguro. Nao mudar pulse/input/logicclock.
Pendente confirmacao visual:teste deestado nao substitui playback do jogo.

Launcher:fps0continua default. 60experimental usa interpolacaoCPU e v36na
entrada normal. 30limpaflags60,logic30mantida. Escolhasalva respeitada antes
Jogar/reinicio;semhot-switch de caches/clock numprocesso ativo. Modo normal
limpa perfisCVpose/frame/produtor;logs5sFPS/VFETCH/milestones soemperfil.
Avisos/erros eboot permanecem. DiagnosticosmarcamCV_FPS_DIAGNOSTIC=1 para
preservarRAW/GPUblend/preset. Novoexecutar-code-veronica-jogar.cmd limpa
amostradores/diagnosticos e chamaotimizado;launcher controla30/60. Executavel
direto tambem. LabelExperimental mantido porbugsgraficos conhecidos.

Testes menu real:gap4em60,gap1normal,telas nativas/entrada visivel,
settings/back/input/AA/RCAS/aniso. Headercadence:futuro,expiry,clear,wrap.
FPS testenderecosregionais corretos;OFF/INTERP/RAW devem verificar cadencia,
restore deestado e registroJP/EU/US. Otimizacoesv36preservadas. Semcommit/push.
Uso:jogarCMD ouexecutavelclassico->Inicio->FPS->30original ou60experimental,
selecionarregiao/resolucao->Jogar. Trocar fechandojogo ealterando nolauncher.
Pendente build/testes3regioes e confirmacaovisual.

Concluido:JP/EU/US compilados comsaida0. Logs fps60-v37-{jp,eu,us}-build,
mais jp-final/eu-final aposdefaultDXCnoexecutavel. SHA256de3binarios em
workspacev37-regional-builds.json. Testes menuOFF/interp eFPSOFF/interp/RAW
passaram nas3regioes,registro real0020E9C0JP/00210070EU/0020E0D0US.
DXCdefault no executavel seSHADER_COMPILERausente;overrideexplicito mantido.
Semalterar cv_launcher.ini. Otimizacoesv36agoracompartilhadaspelos3binarios.
Confirmacao visual do menu em60FPS ainda cabe aoteste do usuario;nao dizer
que houve playbackautomatizado dojogo. Semcommit/push nestaetapa.

---

# v36 — indice capped, hash de costuras e lookup de RT (JP/DXC/CPU)

Busca:contar cadeias de bucket enquanto constroi indice,saturando em129.
Incluir todasentradas(colisoes/count0). build_blocks pula somente bounds das
cadeias>128 seprecheckativo:busca ja obrigatoriamente descartaria pelo cap.
Cadeias<=128 mantem blocos/ordem/capiguais. Comprecheckdesativado,construir
blocosoriginalmente. Sem aumentarcap,sem remover candidatos validos.

Costuras:hashXYZ atual calculado uma vez por upload visitado,reusado para
cada camera/desenho. Reset pela zeragem existente vertex_groups emcada weld;
mesmo hashcanonicalzero e mesmos floats comparados,ordem/visitas/nodecap,
unioes/conflitos/receitas idênticos. Nenhum salto de visita,mesmo failclosed.
8bytes/maxv extras(umarray),malloc/free/falha tratados pelo init existente.
CV_POSE_REPEAT_WORK=0 volta bounds/hashao caminho anterior;capv32 permanece.

Preparacao:off_rt_find_sz agora procura tamanhoexato e coleta fallbacks
first-used/first-any nummesmo percurso. Zerooff/defaultsize e prioridades
mantidas.Semcachepersistente oumudancas emrecursos/formatos/barreiras.
GPUinvestigada:listas3.2ms nao demonstram principalgargalo;nenhumshader,
pass,sombra,resolucao ouqualidade reduzidos. Estaotimizacao alivia CPUda
preparacao,nao alegarganhoGPU. Nao prometer60:produtor20.2ms persistena
referencia de cutscene1080p. Native30nao executacaminhodepose;RTlookup geral
somente binarioJP atualizado. Outrasregioes nao recompiladas.

Validacao:2400frames differentialcontraheader v35 sobASan,saidas completas
fundidas/costuradas,tracks/matches/caps efontescoerentes identicas;teste
permanente10milframes passou. Cap127/128/129/130,colisoes,count0,reset e
optout passaram ASan. Um milhao RTlookups random contra funcaooriginal
passou ASan(exact/used/any,defaultsize e tabelasalteradas).
Sintetico searchprepare:6cargas ~empate..5%menostempo,nao benchmarkFPS.
Preservar atribuicao limitada;sem commit/push. Testar cutsceneinicialJP1080
com CMDperfil-produtor30s eF12unico no mesmo trecho;deixarrodar5sa mais.

BuildJPv36saida0:fps60-v36-jp-build.log. HashEU/USidenticos areferencia.
Diffcheckpassou(avisosCRLFexistentes). Differential2400frames repetido com
repeat_work_disabled alternando a cada3frames:saidasiguais. TempoASan nao
usado para benchmark(uma carga mostra overhead pequeno);sinteticoO2 nao
substituijogo. Pendente resultado real da cutscene1080p.

---

# v35 — fetch especifico de atributosCPU da pose (JP/DXC/CPU)

Amostragem do produtor indicou fetch_planned_attribute~9.4%das amostras,
selected~3.6%,plan_build~2.4%. Percentuaisdewall nao sao CPUms nemsoma
inclusiva. Novo rsx_fetch_pose_vertex le somente0/8/15(RSX_POSE_ATTRIBUTE_MASK
0x8101),sem loop generico de16slots. AtributosF32BE semdivisor usam caminho
curto com leitura escalar/bswap e escrita dascomponentesfaltantes do default;
nao escreve default para sobrescrever depois. Inativos copiamfloat4 exato.
Demais tipos/divisores continuam pelo decoder existente. Mesmaregra de
resolucaoLOCAL/MAIN,pagecache,offu32,defaults e formatos;semcache entre draws.
Nao ler16bytes quando n<4;nao mudar halfsubnormal/normalizacao/NaNs.

Backend usaapenas em pose_note_raw_vertex quando plano corresponde aoestado;
fallback e memoizacao pordraw permanecem. CV_FPS_TEST_60=1 ativa e
CV_POSE_FETCH_FAST=0 restaura selecao generica. Native30 permanece caminho
anterior. Outrasregioes nao recompiladas. Perfilv35 identifica build.
Teste de240000comparacoes bitexact sobASan passou:tipos1..7,sizes0..5,
stride0,enabled/defaults bitsarbitrarios,frequencia/modulo,LOCAL/MAIN,
paginas e cache;atributos nao selecionados intactos. Arraysfloat1..4componentes
comredzone ASan verificam ausencia deoverread. Usa codigo real via include,
resolver sintetico compaginas nãocontiguas. Nao e benchmark de ganho nem
validacao visual. Pendente teste real,mesmoCMDperfil-produtor(normal sem
amostrador),JP/1080p,salazumbis30s,F12final. Semcommit/push.

BuildJPv35 concluido saida0(logfps60-v35-jp-build.log). HashesEU/US
preservados. gitdiffcheck passou(avisosCRLFexistentes). Desempenho/visual
emgameplay ainda pendentes;nao afirmar ganho baseado no teste deequivalencia.

---

# Proximo diagnostico v34 — amostragem das funcoes do produtor

executar-code-veronica-60fps-amostrar-comandos.cmd usa amostrador existente:
PS3_SAMPLE=7(ms),PS3_SAMPLE_MAIN=1,PS3_SAMPLE_THREAD=frame_clock,
PS3_SAMPLE_SPU ausente. Apenas thread nomeada frame_clock(vblank+render),
sem survey de todas as threads apos encontra-la. CV_FRAME_DETAIL e
CV_PRODUCER_PROFILE mantem contexto/intervalos anteriores. Semrecompilar.
JP/DXC/CPU/1080p,mesmacamera45s,F12final. FPS nestaexecucao sofreinterferencia
da suspensao/amostragem e nao e benchmark AB. Apenas localizar funcoes caras.

Usar RIP/RVA bruto para simbolizar no PDB exato deMyGameRecomp_JP.exe;
percentuais deamostras incluem esperas e nao sao CPUms diretos. guest
atribuido porstack scan pode conter retornos antigos; priorizarRVA bruto.
RVA vem em buckets16bytes,entao linha exata pode oscilar na fronteira.
Nao somar host/module histograms(alguns caminhos contam duasvezes) nem
caller/leaf. Logs porrelatorio sao janelas separadas,excluir boot/captura.
PDB eSHA256 executavel preservados no workspace performance-v34-command-sampling.
Ferramenta llvm-symbolizer disponivel com --relative-address/--pdb explicito.
Nao alterar executavel antes de resolver as amostras contra simbolos certos.
Semmudancas deotimizacao/clock/grafico/commit/push nestaetapa.

---

# v34 — adiar varredura END do VP ate miss do cache (JP/DXC/CPU)

vp_get_vs fazia rsx_vp_program_size_instrs antes de consultar o cache existente
state/gen/start/vtexmask/indicevalido. A geracao muda quando ucode muda ou
cresce(rsx_commands.c). Na hit, vlen nao era usado antes do return; mover a
varredura para depois dessa hit elimina trabalho cujo resultado era descartado.
Miss usa exatamente o mesmo scanner antes do hash/decompile/preload. Nao muda
chave,LRU,slot,PSO,vertices,normal/iluminacao ou ordem de comandos. Mantem
checagem de start e todos guardas existentes; nao introduz cache novo.

Ativo apenas CV_FPS_TEST_60=1,com CV_VP_SCAN_DEFER=0 para referencia. Native30
mantem ordem antiga. ApenasJPcompilado; outrasregioes nao atualizadas. Perfil
v34 identifica build. Semcommit/push. Nao prometer percentual: esta mudanca
elimina uma varredura redundante especifica,nao todos14.5ms do produtor.
Testar mesmo CMD perfil-produtor,JP/1080p,sala doszumbis,mesmacamera30s,F12.

Validacao:teste diferencial de prefixos reais vp_get_vs,com scanner/fill
simplificados,10000consultas e mudancasgen/start/mask/state:resultados e
vlen iguais,197scans contra10000;optout10000/10000. Nao e benchmark nemteste
deimagem. BuildJPsaida0,fps60-v34-jp-build.log;SHA256EU/USpreservados.
Pendente teste real de desempenho eaparencia pelo usuario.

---

# v33 — diagnostico do produtor, sem alterar trabalho grafico (JP/DXC/CPU)

Objetivo: separar producer_span~20,7ms da CPU efetiva da thread que emite os
batches. O produtor e frame_clock/FIFO,nao necessariamente PPU principal.
CV_PRODUCER_PROFILE=1 ativa GetThreadTimes em release/ready (duas leituras
por entrega),acumula apenas spans com mesmoTID,contador valido e monotono.
Primeira amostra/troca de thread/falha ficam invalidas. Dados transportados
no RenderBatch congelado; relatorio agregado a cada~5s em PRODUCER-PERF.
CPU ewall medidos no mesmo intervalo,antes de wait-render; wall_minus_cpu
inclui esperas e desagendamento,nao representa somente ociosidade.
F12 e janela seguinte devem ser excluidos; CPU tem quantizacao do SO.

CV_PRODUCER_PROFILE tambem ativa modo2 da timeline existente de cellGcmSys:
drain,present,wait-render,fc-sleep,fc-sleep-behind,fc-pump,fc-ticks e esperas
principais. Eventos8/9(upload/draw por chamada) ficam sem QPC; PS3_TIMELINE
permanece ausente,portanto nao instala wrappersGPUCMD nem trace extraSPU.
PS3_TIMELINE explicito mantem modo1completo original. TL perframe possui
faixas aninhadas ethreads concorrentes:nao somar drain/render/esperas.
Histograma usleep por thread mede tempo observado,incluindo desagendamento.
Nao isolaIA/skinning. Se necessario,amostradorPPU sera uma etapa separada.

Novo executar-code-veronica-60fps-perfil-produtor.cmd pedeJP/1080p,30s na
mesma camera da sala dos zumbis eF12 final. MantemDXC/interpolacaoCPU.
Normal semflag nao coleta novos contadores/timeline. Nenhuma alteracao de
pacing,clock,locks,qualidade,busca ou protecoesgraficas. Recompilar apenasJP;
30nativo e executaveisEU/US permanecemfora doexperimento. Semcommit/push.

Validacao: buildJP concluido comsaida0 (fps60-v33-jp-build.log). Teste do
GPUprobe passou emWARP: listas partidas,slots/fences,reuso,amostras
desligadas/descartadas,categorias,alvos,resolucao mista eoverflow. SHA256dos
executaveisEU/US identicos a referencia. gitdiffcheck passou(avisosLF/CRLF
preexistentes). Pendente medicao real deprodutor;nao houve benchmark de
ganho nem mudanca de otimizacao nesta etapa.

---

# Diagnostico seguinte a v32 — preparacao na sala dos zumbis (JP/DXC/CPU)

Sala do portao/escadas: duas janelas limpas proximas da captura28540 mostram
~40,86FPS/24,476ms,build6,826ms (prepass2,439ms),pose7,179ms,history2,511ms,
costuras1,850ms. Atalho v32 ja evita~4134buscas/present. Mais detalhes no
workspace performance-v32-zombies/ANALISE.md. Nao atribuir producer_span a
CPU pura nem usar percentual do overlay para diagnosticar GPU.

Novo executar-code-veronica-60fps-perfil-sala-zumbis.cmd ativa apenas o
CV_FRAME_DETAIL=1 existente, chama o iniciador CPU com DXC e pede JP/1080p,
mesma camera/carga por~30s,F12 uma vez ao final. Nao altera executaveis,
qualidade,clock,protecao de vertices ou busca v32. Sem recompilar,commit/push.

Coletar CPU-RECORD-RANGES e GPU-RANGES por categoria,FRAME-CONTEXT e
CPU-THREAD junto de FRAME-PERF. Categorias CPU sao subconjuntos do build;
prepass tambem faz parte dele. Tempos GPU sobrepoem CPU. renderer_cpu_ms
mede CPU da thread de render;whole_process_cpu_ms inclui threads concorrentes,
nao isola produtor/IA/skinning. Excluir captura e janelas de mudanca de cena.
Esta etapa e diagnostico para escolher uma alteracao, nao ganho de FPS.

---

# v32 — não calcular buscas que o limite obrigatoriamente descartaria (JP/DXC/CPU)

Resultado de partida v32 (sala vermelha,1080p):14janelas/4146presents válidos,
~59,13FPS/16,912ms contra~52,90FPS/18,902ms na v31 estável. Pose total
6,267->4,459ms,history4,126->2,444ms,candidatos215mil->43,7mil,~2532buscas
evitadas/present. Janelas58,10..59,81FPS: próximo60, não travado. Sessões
com duração/movimentos diferentes; não prometer percentual fixo. Dados e
comparação preservados no workspace performance-v32/ANALISE.md.


Referência principal agora é a sala vermelha sem isqueiro,scene0x80091801:
~4953draws elegíveis,~215mil candidatos e~2300..2500 buscas capped por quadro.
A varredura anterior visita toda a cadeia e, se houver uma129ª entrada,
descarta best ao atingir128, mesmo que já tivesse encontrado candidato bom.
Branches de used/key/radius/blocos também consomem visitas. Portanto uma
cadeia de bucket com>128entradas SEMPRE termina no mesmo fallback.

Na construção existente de blocos (já percorre até128entradas), guardar se
sobrou outra entrada. Na consulta desses buckets, manter capped e fallback
originais sem calcular candidatos destinados ao descarte. Contar a cadeia
inteira do bucket, incluindo colisões de chaves e draws anteriores count0:
contar só chaves iguais mudaria a regra e NÃO foi feito. Limite permanece128,
nenhum novo match ou nova interpolação é permitido. Correntes<=128 usam o
mesmo caminho de escores/ordem/empates. Dados de consulta/sampleIDs continuam
preparados como antes, caches/épocas e coerência/alias/costuras preservados.
Um byte adicional por bucket, com alocação/falha/liberação/invalidação tratadas.

Diagnóstico muda deliberadamente: ambiguous agora exclui pesquisas descartadas
por cap, onde a ambiguidade não influencia nenhuma pose; capped e matches
permanecem iguais. Golden verifica ambiguous_new<=ambiguous_old e diferença
limitada por capped; tracks/snapshots/saídas finais bit a bit iguais. Não
apresentar essa diferença de contador como correção gráfica. Fatores de
reuso/pruning/candidatos podem mudar por não executar pesquisas inúteis.

Controle: CV_POSE_CAP_PRECHECK=0 desliga só o atalho; novo iniciador
executar-code-veronica-60fps-busca-referencia.cmd chama o mesmo build sem ele.
CMD CPU normal ativa por padrão; [POSE-CAP]v32 avoided_searches registra o
trabalho evitado. [FRAME-PROFILE]v32 identifica a compilação. Opt32 não afeta
30 sem interpolação, iluminação, resolução,clock ou blendCPU.

Validação:2400frames contra fontev31 com perfilON/OFF sobASan, snapshots,
tracks,identidades/caps,fontes coerentes e saídas completas fundidas/costuradas
idênticas. Casos específicos127/128/129/130/256, chaves idênticas e colisões,
count0 no snapshot por capacidade,reset de cena e disable switch passaram
sobASan. Teste permanente de pose ampliado; estabilidade10milframes passou.

Benchmark sintético: cargas semoverflow próximas do empate,algumas oscilações
pequenas positivas/negativas. Caso misto6000draws/metade cadeias>128 caiu
10,861->3,212ms (~3,38x no prepare),3000buscas evitadas e poses idênticas.
Isso demonstra o alvo, não promete essa proporção em gameplay nem60FPS.
Benchmark ASan não usado para medir desempenho. Testar sala vermelha em1080p
por~30segundos eF12; não solicitar outra sala neste primeiro ensaio.
Build JP concluído, saída0: build-release/fps60-v32-jp-build.log.
Hashes EU/US preservados; git diff --check passou (avisos LF/CRLF existentes).
Pendente medição real na sala vermelha. Sem commit/push.

---

# v31 — referência v29 restaurada e consulta repetida de pipelines (JP/DXC/CPU)

V30 não confirmou ganho em gameplay (~53,46FPS vs54,24v29), então layout da
busca e seu teste permanente voltam exatamente ao snapshot.v29. Sem retirar
SSE2v29, correções gráficas ou pacote nativo30. Foco muda para build/prepass.

Nova otimização só com pose ativa e prep_reuse_on: dentro de um mesmo range
do prepass, duas operações VP consecutivas com opções de PSO e programa
FP inteiro idênticos podem reaproveitar o índice já preparado. Comparação de
VSslot,blend,nrt,formato,exp32,cmask,cull,cube_mask,bytes e TODOS os bytes do
programa (incluindo constantes): conservadora, sem reutilização só por hash.
Apenas snapshots congelados de1..4096bytes. Não mantém ponteiro de PSO vivo
entre operações: fp_prepared_ready revalida índice, geração, entrada/cache,
prontidão,skip,VScompilando,conteúdo/hashVS e fallback antes de retornar.
Evicção ou mudança sempre volta ao vp_get_fp_pso original. Fonte do hint é
atualizada somente com resultado válido; nenhuma correção de shader muda.
Constantes/texturas/descritores por desenho continuam sendo preparados.

[PREPASS-REPEAT] ready_reused mostra acertos por present. CV_PREP_REPEAT=0
retorna à consulta original no mesmo build; iniciador adicional
executar-code-veronica-60fps-prepass-referencia.cmd faz isso com CPU.
O CMD60fps-interpolado-cpu-blend normal ativa a tentativa por padrão.
Não modifica nativo30 (s_pose_on necessário). Perfil identifica v31.

Validação: helper de igualdade exata sobASan, cada byte da chave e programa,
nulos/tamanhos0/4097/UINT32_MAX, último byte e buffers exatos1..4096bytes;
helper de pipeline pronto com evicção/mudançaVS/generation/pending/skip/
índices inválidos/fallback; unidades de pose passaram com fontev29 restaurada.
Sem alegação de ganho antes de medir gameplay e contagem real de acertos.

TesteJP/DXC/CPU/1080p: mesmo CMD normal, mesma sala/câmera, isqueiro ligado e
desligado,F12. Se necessário comparar depois com CMDprepass-referencia.
Build JP concluído, saída0: build-release/fps60-v31-jp-build.log.
Hashes EU/US preservados e git diff --check passou (avisos LF/CRLF existentes).
Pendente medição real de FPS/prepass/acertos. Sem commit/push.

---

# v30 — amostras da busca com localidade de memória (JP / DXC / CPU)

Continuação dos~4,04ms de history medidos na v29. A cache de amostras antes
mantinha16x,16y,16z separados: calcular as primeiras4distâncias acessava
três regiões afastadas. Agora os4pontos consumidos juntos pelo SSE ficam
em um grupo contíguo [x4,y4,z4], seguido pelos próximos grupos. Query e
snapshot anterior usam o mesmo layout,192bytes como antes, sem alocação
adicional. Cache/época/identidade/contagem não mudam.

Distâncias, ordem de soma, limiares e limites, visitas/cap128, chaves,
câmeras/ambiguidade, guardas de salto, alias/costura e cadência mantidos.
Caminho escalar usa o mesmo layout. API SoA de teste mantida para comparação
bit a bit; kernel interno usa os grupos compactos. Sem mudança de GPU,
iluminação ou qualidade. Não há novo reaproveitamento de identidade entre
passes: só melhora do acesso aos dados da busca já existente.

Tentativa anterior descartada: AABBs de dois endpoints por bloco foram
conservadoras e passaram o golden, mas custaram mais em várias cargas.
Snapshot local .v30-endpoint-trial, não instalado no projeto.

Validação final:2400frames contra fonte instalada v29, incluindo perfil
ON/OFF, snapshots/tracks/matches/ambiguidade/caps/fontes coerentes e saída
fundida/costurada bit a bit; também sob ASan. Teste permanente ampliado:
32mil conjuntos com tamanhos1..16,192mil comparações packed/SoA de limites,
raio8/64,zeros com sinal,padding e somas parciais exatas, sob ASan.
As mesmas unidades de pose,20milAABBs e10milframes de estabilidade passaram.

Benchmark com6000draws,40frames/carga,3/6/12/16/24vértices por draw,5tipos:
ganhos pequenos e variáveis,~1..7% em várias cargas; câmeras únicas ficaram
próximas do empate e algumas~1% piores. Com24vértices/passes separados,
2,479->2,320ms em um ensaio. Carga mista pequena não melhorou (0,160->0,169ms).
Não prometer ganho fixo ou converter essas medições em FPS real. ASan não
serve para benchmark. Manter v29 como referência e medir a partida.

Novo perfil [FRAME-PROFILE]v30. Usar CMD60fps-interpolado-cpu-blend normal,
sem perfil-busca,1920x1080,mesma sala/câmera, isqueiro ligado/desligado,F12.
O modo30 sem interpolação não executa a busca, e seu controle está preservado.
Build JP concluído, saída0: build-release/fps60-v30-jp-build.log.
Hashes dos executáveis EU/US conferidos e preservados; git diff --check
passou (avisos LF/CRLF existentes). Caminho escalar também passou sob ASan.
Pendente medição em gameplay. Sem commit/push.

---

# v29 — rejeição espacial dos blocos em SSE2 (JP / DXC / CPU)

Alvo confirmado pela v28: a varredura representa a maior seção do prepare
amostrado, ~3,5ms ligado vs0,9ms desligado. A implementação agora calcula a
distância mínima para UM bloco existente em três lanes SSE2, em vez de três
seleções escalares por eixo. Mantém exatamente (dx²+dy²)+dz², sem FMA,
fast-math, aproximação de divisão ou alteração dos limiares.

Não reordena candidatos/blocos, não altera chaves/visitas/cap128, limites,
empates, identidade de câmeras, validação, alias, costura ou sombras.
Quarta lane é zero, construída com _mm_set_ps; não lê xyz[3] ou bordas além
das três coordenadas. Caminho escalar preservado em hosts sem SSE2.
Nenhuma memória extra. Trial de lista contígua por bloco foi descartado:
mais memória sem benefício claro sobre a versão SSE2 menor.

Probe v28 corrigido: removidos position_est/camera_est e cronômetros por
comparação, pois estimativas excediam search e o piso QPC era0. Permanecem
as cinco seções amplas e contadores opcionais, um prepare em61. Diagnóstico
continua sendo diagnóstico, não usar seus tempos para julgar FPS final.
O CMD CPU normal mantém a instrumentação extra desligada por padrão.

Validação final:1milhão de distâncias bit a bit contra cálculo escalar,
predicados de raio/limite, bordas8/64, zeros com sinal e blocos vazios.
ASan com alocação de apenas3floats prova ausência de leitura de quarta lane.
2400frames vsfonte instalada v28 com perfil ON/OFF: snapshots, tracks,
identidades/ambiguidades/caps, fontes coerentes e saídas completas fundidas e
costuradas idênticas; também sob ASan. Unidades de pose,20mil AABBs,
32mil somas e10mil frames de estabilidade passaram.

Ensaio O2 final (~60frames/carga), v28->v29: grupos3,103->2,912ms;
câmeras únicas11,723->11,463ms; amostras únicas6,601->6,334ms;
esparso3D2,497->2,285ms; passes separados2,522->2,427ms.
Ensaios anteriores tiveram diferenças menores (~2..5%). Não converter os
percentuais sintéticos em promessa de FPS na partida; medir novamente.

Teste: executar-code-veronica-60fps-interpolado-cpu-blend.cmd (normal, SEM
perfil-busca),1920x1080,mesma sala/câmera, isqueiro ligado/desligado e F12.
Perfil identifica [FRAME-PROFILE]v29. Otimização de busca não roda em30 sem
interpolação. Sem nova alteração de iluminação, GPU ou relógio.
Build JP concluído (saída0): build-release/fps60-v29-jp-build.log.
Hashes EU/US preservados; git diff --check passou (avisos LF/CRLF existentes).
Pendente medição em gameplay. Sem commit/push.

---

# v28 — diagnóstico interno amostrado da busca (JP / DXC / CPU)

Novo iniciador: executar-code-veronica-60fps-perfil-busca.cmd.
Ativa CV_POSE_SEARCH_PROFILE=1 apenas na sessão e chama o caminho CPU.
Não é uma otimização nem promete ganho de FPS. Para medições usuais de
FPS, fechar e usar o iniciador CPU normal sem o probe adicional.

[POSE-SEARCH] separa validate (IDs/validação), index (tabelas/blocos),
setup (cópia atual e amostras/consulta), search (visitas, pontuações, empates)
e resolve (salto, histórico anterior e tracks). Tempos em ms por prepare
amostrado, não média de todos os presents. Somente quadros com >=1000 draws
candidatos entram: um em61, sem travar no mesmo lado da cadência30/60.

Dentro de search: position_est e camera_est extrapolam uma chamada em128,
com offset rotativo por prepare, contadores independentes e remoção do piso
mínimo medido de duas leituras de relógio. São ESTIMATIVAS, sobretudo frágeis
para funções muito curtas/cache hits, e se sobrepõem a search. Não somar essas
estimativas aos cinco estágios. Interrupções e custo do probe ainda podem
contaminar amostras. Confirmar tendências em várias linhas e validar o alvo
com outro ensaio antes de concluir causalidade. Se faltarem amostras, olhar
position_probes/camera_probes antes de interpretar os valores.

As seções são tempo wall CPU e incluem instrumentação. Relatório agregado
a cada~5segundos, nenhuma linha por draw/candidato. Relógio/snapshots/chaves,
ordem/limites/rejeições, poses e gráficos permanecem iguais ao v27.
O probe é opcional, caller-owned e NULL fora dos prepares selecionados;
nenhuma chamada nova de relógio interna ocorre no caminho sem probe.
As condições de instrumentação existem no código normal, portanto qualquer
comparação final deve usar o mesmo build e medir, não presumir custo zero.

Validação: 2400 frames contra v27, alternando probe ligado/desligado e offsets,
com relógio sintético, saídas completas e decisões idênticas, também sob ASan.
Unidades de pose passaram, incluindo10mil frames de estabilidade. Build JP
concluído, saída0: build-release/fps60-v28-jp-build.log. Hashes EU/US
conferidos e preservados; git diff --check passou (avisos LF/CRLF existentes).
Sem commit/push. Novo perfil identifica v28.

Teste solicitado: JP, DXC, CPU,1920x1080; mesma sala/câmera com isqueiro ligado
por~30segundos, F12 depois; repetir desligado para separar as cargas.
O mesmo arquivo run-log-fps60-interpolado.txt recebe [POSE-SEARCH].

---

# v27 — limites da busca por consulta e histórico sem cópia duplicada (JP / DXC / CPU)

Retomada de 60 FPS após o pacote reversível nativo30 v26. Dois custos removidos
sem mudar critérios de identidade, ordem dos candidatos, limite128, epsilon,
cortes de câmera, teleporte, alias, costuras ou cadência do jogo:

1. O limite conservador de soma v25 é calculado uma vez por consulta e
   atualizado somente quando muda o melhor escore. Candidatos e blocos usam
   esse limite para rejeição antecipada. A pontuação final continua com a
   divisão e epsilon originais; o limite pode apenas adiar uma rejeição.
   A sondagem do memo usa limite completo, nunca armazena soma truncada.
2. A inicialização do histórico antigo com a pose atual só acontece quando
   não há correspondência, há ambiguidade ou salto. Correspondências válidas
   já escrevem todo o histórico anterior: a cópia inicial era sobrescrita.

Validação: 2400 frames comparados contra fonte instalada v26; snapshots,
tracks, identidade das correspondências, ambiguidades, caps, aliases, fontes
coerentes e saídas completas fundidas/costuradas idênticas, também sob ASan.
Unidades de pose e limites conservadores passaram: 7 milhões de limites,
200 mil somas, 20 mil AABBs, 32 mil somas vetoriais e 10 mil frames de estabilidade.

Ensaio O2 inicial da versão final (60 frames/carga): grupos exatos praticamente
empatados2,853->2,851ms; câmeras únicas11,415->11,611ms (~2% mais lento);
amostras únicas6,674->6,222ms; esparso3D2,396->2,282ms; passes separados
2,681->2,471ms. Cargas sintéticas não representam FPS real. Variação observada
em outro ensaio; alvo principal são cargas com baixo reaproveitamento de
amostras como nos logs v25. Sem promessa de percentual ou de60 FPS.

Novo perfil se identifica como [FRAME-PROFILE] v27. Continuar pelo iniciador
60fps-interpolado-cpu-blend; primeiro teste1920x1080, mesma sala/câmera e
isqueiro ligado, depois desligado, F12 fora de menus/cortes/carregamentos.
Modo30 sem interpolação não executa esse trabalho; controle v26 preservado.
Shader, blend na CPU, iluminação, relógio e resolução não foram alterados.
Compilação JP concluída: build-release/fps60-v27-jp-build.log, saída0.
Hashes dos executáveis EU/US conferidos e preservados; git diff --check
passou. Fonte instalada com proteção contra alterações independentes.
Pendente: medição no jogo pelo usuário. Sem commit/push.

---

# v26 — comparação reversível das otimizações no modo nativo de 30 FPS (JP / DXC)

Controle novo: `executar-code-veronica-30fps-comparacao.cmd`, menu 1=ON,
2=OFF, ou argumentos `--on` / `--off`. Fechar jogo/launcher antes de trocar:
a política e a ABI das constantes dos shaders são fixas por processo.
O iniciador mantém DXC e vertex fetch na GPU iguais nos dois caminhos.
`CV30_OPTIMIZATIONS=1|0` força o limite nativo de 30 FPS no launcher sem
sobrescrever a escolha de FPS salva no INI. Dropdown de FPS bloqueado só nesta
sessão; troca automática para executáveis EU/US bloqueada porque só JP recebe
a compilação. Atalhos existentes continuam com o comportamento anterior.

ON reaproveita PSOs preparados e janelas de descritores imutáveis dentro do
quadro, agrupa desenhos consecutivos equivalentes do gerador de sombras e
antecipa o cálculo da meia borda da textura. OFF desativa este pacote (não
reverte correções gráficas ou todas as otimizações históricas). Shader OFF
recebe dimensões e calcula a borda; ON recebe a borda pronta. Fonte e hash do
shader diferem para evitar reutilizar código com ABI incompatível.

Sem interpolação ou aumento da frequência da lógica no teste de 30 FPS.
Busca de malhas, AABBs, seletor de amostras e blends continuam específicos do
modo interpolado de 60 FPS. Nem toda otimização de 60 FPS tem trabalho útil
a executar nos 30. Caminhos de diagnóstico que exigem desenhos separados
continuam impedindo o agrupamento. Reuso de PSOs mantém guardas de geração,
prontidão, conteúdo do VS e invalidação/evicção; descritores não atravessam
quadros. Nenhum ajuste novo de qualidade, resolução ou iluminação.

Validação de v26: política nativa ON/OFF/default, valores inválidos, compatibilidade
com 60 e overrides; 15 variantes de shaders reais compiladas com DXC/FXC;
2.949.376 resultados de endereçamento bit a bit em D3D12 WARP; 512 desenhos
do gerador de sombra vs um grupo, cor e profundidade idênticas; testes dos
helpers reais de preparação/invalidação; 20.000 comparações de agrupamento,
variação de cada byte do estado e 10.000 casos de endereçamento de vértices.
Build JP e checagem de outras regiões registrados em `VALIDACAO-v26.md`.
Ganho em hardware e imagem em gameplay ainda precisam de comparação ON/OFF.
Não prometer FPS adicionais: com limite de 30, ganho pode aparecer como folga.

Logs separados ao lado do CMD: `run-log-30fps-otimizacoes-on.txt` e
`run-log-30fps-otimizacoes-off.txt`; `[OPT30]` confirma modo e flags.
Primeiro teste sugerido: 1920x1080, mesma sala/câmera, isqueiro ligado,
aguardar carregamento dos shaders, depois comparar movimento e cortes de câmera.
Não houve commit ou push neste trabalho.

---

# Experimento de 60 FPS — 17/10/2026

## Resultado real v25 — 19/10/2026

JP/DXC1080p interno e saída:6 janelas válidas/1525 presents,intervalo19,720ms
(~50,71 FPS),contra21,197ms (~47,18 FPS) na v24. Antes deF12:~51,14 versus
48,30 FPS. History inicial4,40..4,44ms;pose média7,260ms (v24=8,030ms).
Melhora observada nesta sessão; outros estágios também aceleraram, portanto
não atribuir todo~7,5% de FPS à alteração nem prometer ganho fixo. Capturas
com readback7,038/8,296ms excluídas. Última janela~48,46FPS,indicando variação.
Sombras continuam~0,607ms e~3033 chamadas poupadas por present. Cache de
amostras continua sem hits na sondagem; não é origem do ganho. Resultados,
log e duas capturas em work/fps60-v2/performance-v25/ANALISE.md.
Nenhuma nova mudança de renderização neste turno;sem commit/push.

## Limite conservador na soma de distâncias CPU v25 — somente JP

Continuação da busca de~5 ms. Pré-rejeição vetorial por bloco foi testada,
mas descartada por ganho inconsistente. Versão final reduz divisões repetidas
nas saídas antecipadas de rsx_pose_sample_sum: calcula limite conservador uma
vez por candidato. Pontuação final e todos os critérios originais mantidos.
Distâncias/somas conservam ordem e precisão; margem ULP impede poda falsa
nas fronteiras. Nada de fast-math, aproximação de câmera ou remoção de vetos.

2400 frames exatos contra v24, incluindo pipeline completo, e ASan passaram.
7 milhões de casos de limite de float e200 mil somas adicionais passaram,
assim como unidades gerais de pose. Benchmarks locais indicam redução pequena
em algumas cargas; ganho real ainda NÃO medido. Ver
work/fps60-v2/performance-v25/INVESTIGACAO.md e logs de teste preservados.
Somente JP/DXC,teste1080p; EU/US preservados. Não muda shaders/relógio/blend.
Sem commit/push. Não prometer60 FPS a partir de tempo sintético.

## Resultado da v24 na partida — 19/10/2026

JP/DXC,1080p interno E saída. 9 janelas válidas/2127 presents:
intervalo médio21.197 ms (~47.18 FPS), pose8.030 ms e preparação
4.508 ms. Antes de F12:~48.30 FPS; busca~4,74..4,81 ms. Final da
sessão:busca5,19 ms e~44 FPS. Melhora pequena/variável comparada à v23,
sem A/B controlado nem evidência de salto geral. Captura/pausa excluídas.
POSE-REUSE:0 hits na sondagem,512 misses e~142,6 mil bypass; a heurística
interrompe cedo, portanto NÃO comprova ausência de repetições no quadro todo.
Não forçar cache maior sem medir. Câmeras seguem com alto reaproveitamento.
Dados em work/fps60-v2/performance-v24/ANALISE.md. Próxima investigação sugerida:
custo por candidato e poda da busca. Nenhuma nova mudança de renderização.

## Identificação exata de amostras CPU v24 — somente JP — 19/10/2026

Investigação da busca de~5 ms e pose total~8 ms. Ampliar o cache para o
quadro inteiro não compensou de forma consistente em ensaios locais; essas
tentativas NÃO foram instaladas. Mantidas as heurísticas de bypass da v23.

A versão final reduz o hash seletor a três amostras (primeira/meio/última),
mas continua verificando TODAS as amostras antes de compartilhar ID. Para
desenhos de até16 vértices, compara os mesmos bytes contíguos de uma só vez;
para maiores, mantém índices esparsos e verificação original. Nenhuma mudança
na busca, ordem/limite128, escores, empates, câmeras, vetos, aliases e costuras.

2400 quadros comparados à v23: estado e saídas finais idênticos, com ASan.
30 casos adicionais validam colisões do seletor, vértices interiores, índices
esparsos e zero com sinal. Cinco cargas sintéticas: redução~2..8% no prepare;
isto NÃO comprova ganho de FPS em gameplay. Ver
work/fps60-v2/performance-v24/INVESTIGACAO.md e logs preservados.
POSE-REUSE passa a informar grupos e hits/misses/bypass das distâncias para
medir o reaproveitamento real. Pendente novo teste1080p JP/DXC. Sem commit/push.

## Validação na partida da v23 — 19/10/2026

JP/DXC,1080p interno e saída. Quatro janelas estáveis/941 presents;
intervalo médio21.293 ms (~46.96 FPS), preparação CPU4.676 ms,
pose CPU8.081 ms. Última janela: sombras GPU0,567 ms e3040,5 chamadas
poupadas por present. A preparação retornou a4,637..4,753 ms, contra6,7..7,0
na v22. Portanto a regressão CPU foi corrigida e o benefício GPU preservado.
Sobre a v21 (~47,65 FPS), não há grande ganho de FPS: agora há mais folga nas
sombras, mas outros custos ainda limitam. Não anunciar salto rumo a60 só por
esses dados. Captura/pausa excluídas da média. Dados em
work/fps60-v2/performance-v23/ANALISE.md. Nenhuma nova mudança de renderização
neste turno. Próximo alvo GPU planejado: RT01C30000 (~1,44 ms na última janela).

## Correção do custo CPU do agrupamento v23 — somente JP / DXC60

Teste real v22: cerca de3 mil chamadas poupadas por present e custo GPU do
gerador0,60 ms contra1,49 ms (~60% menor). Entretanto preparação CPU subiu
para6,7..7,0 ms e intervalos22,3..22,9 ms, logo v22 NÃO melhorou o FPS total.
Log/captura/análise preservados em work/fps60-v2/performance-v22.

Identificado readback CPU redundante de96 bytes dos CBs de upload por par.
Agora compara diretamente5 float4 do snapshot extraído na CPU, exatamente a
fonte copiada para fp_k em vp_record_cb; programa e estado continuam exatos.
alpha_ctl já faz parte do estado completo; alpha.w só escala WPOS, ausente
nesse gerador. Metadados de textura são inutilizados pela máscara zero.
FP_ZERO_K desativa agrupamento para não ignorar a alteração de diagnóstico.
Contagem nk=5 obrigatória nos dois snapshots. Nenhuma leitura do upload heap
é necessária à checagem; sem alteração de shader ou precisão.

Ensaio CPU isolado no heap de upload D3D12 do adaptador padrão:7,529 ms por
3000 pares na v22 e0,161 ms pela fonte de CPU. NÃO equivale a tempo de frame.
Em WARP os números são0,260/0,162 ms, mostrando por que o teste anterior de
cor/profundidade não revelou o custo de ler o heap no hardware. ASan e20 mil
decisões v22/v23 verificadas usando o fluxo real de alpha/constantes.10 mil
ensaios de endereçamento e variação de todos os bytes relevantes passaram.
Pendente medição na partida. Opção CV_SHADOW_BATCH=0 e escopo DXC60 mantidos.

## Agrupamento exato do gerador de sombras v22 — somente JP / DXC60

O custo classificado como shadow_shader (~1,49 ms no teste v21) inclui as
chamadas de desenho, seus shaders VS/PS e barreiras, não apenas aritmética
fragment. O gerador 7EA4C4E7 não amostra texturas e contém poucos cálculos;
não se substitui rsqrt/div por sqrt nem se reduz precisão dos momentos.
A etapa v22 tenta reduzir chamadas de desenho mantendo os mesmos triângulos.

No replay, agrupa somente listas de triângulos consecutivas e contíguas no
buffer de vértices. Exige o mesmo PSO pronto, mesmo banco b0 (mesmo vcb_slot),
bytes exatos do programa completo de240 bytes incluindo suas5 constantes,
linha alpha/constantes b1 iguais e todos os campos de estado do draw iguais.
Só endereços/índices de snapshot, BEGIN/END já expandido e offsets/contagens
são normalizados. Hash identifica o candidato, mas não substitui as comparações
exatas. A máscara de textura congelada deve ser zero. O vertex fetch usa
vf_first+SV_VertexID: mantém o mesmo índice físico, descritor, entrada original
e override de pose de cada vértice. Nenhuma reordenação ou redução de geometria.

Não atravessa prepass pendente, split de command list, cópia/clear, troca de
estado nem conversão de profundidade agendada. F12 por operação e queries por
draw desativam agrupamento na captura para manter o diagnóstico original.
Desligar com CV_SHADOW_BATCH=0 (caminho anterior). Ativo apenas com DXC, GPU
vertex fetch, CV_FPS_TEST_60=1 e CV_RENDER_INTERPOLATION=1. Não ativa no modo30.
Ainda não houve medição de ganho nem contagem de grupos na partida real.
SHADOW-BATCH relata groups/draws_saved por present; zero pode indicar ausência
de pares exatos nessa carga. Os6755 ops do manifest continuam sendo as operações
originais, e não a quantidade final de DrawInstanced após agrupamento.

Validação: helper com registro real de672 bytes; alteração de cada byte de
estado não normalizado/constantes e testes de rejeição; ASan;10 mil ensaios de
endereçamento de vértices/poses. Render WARP com o PS capturado7EA4C4E7:
512 desenhos vs1 grupo, cor RGBA32F E profundidade D32 idênticas em128x128,
triângulos sobrepostos. PS com prelude compilou por DXC de produção e FXC;
FXC é apenas referência auxiliar de teste, não novo alvo da otimização.
Artefatos no workspace work/fps60-v2/*v22*. Pendente teste na partida com DXC.
Somente executável japonês será atualizado; EU/US preservados. Sem commit/push.

## Resultado do teste v21 — 19/10/2026

DXC confirmado, JP, resolução interna/saída1920x1080. Cinco janelas estáveis
(1194 presents): intervalo médio20,987 ms, aproximadamente47,65 FPS;
pose CPU7,699 ms, preparação4,865 ms. GPU11,374..11,562 ms;
alvo015280003,278..3,463 ms. Captura F12 e pausa excluídas da média.
A v20 usou saída2267x1274; portanto a diferença NÃO isola o ganho v21.
Detalhes, log, manifest e backbuffer preservados no workspace:
work/fps60-v2/performance-v21/ANALISE.md. Não atribuir toda a melhora de FPS
à alteração das bordas. Imagem estática não exclui defeitos de iluminação
em movimento. Usuário definiu DXC como foco das futuras otimizações;
FXC não removido. Próximo alvo planejado: shader de sombras (~1,49 ms),
depois RT01C30000(~1,27 ms). Neste turno nenhuma nova mudança de renderização.

## Amostragem de texturas GPU v21, somente JP — 19/10/2026

Primeiro alvo: RT 0x01528000, aproximadamente 3,44 ms GPU no teste v20.
Nos programas 412A8EF5 e 2165F997, a coordenada de amostragem calculava
0,5/tamanho por pixel. b1.texaddr.xy agora contém essa distância já calculada
uma vez por desenho pela CPU, usando as mesmas dimensões do guest. O shader
continua aplicando os mesmos modos, limites, coordenadas e derivadas; não muda
resolução, amostras, filtragem packed16, sombras, precisão ou relógio.
A ABI interna de b1 muda em conjunto com o HLSL; o cache de bytecode usa
hash da fonte inteira e a biblioteca de pipelines usa bytecode/estado.

Também explicita o desvio uniforme da conversão gamma: FXC antes calculava
log/exp mesmo com gamma desligado. DXC já pulava esse bloco, portanto NÃO
se atribui ganho de DXC a essa alteração. A retirada das divisões de borda
foi conferida no DXIL dos programas capturados. Não confundir inspeção de
instruções com medição real de tempo da GPU.

Validação: 2.949.376 saídas bit a bit iguais em D3D12 WARP, com o adaptador
DXC de produção e FXC (dimensões 0..16384, modos 0..9 e UVs dentro/fora das
bordas); mais 1.048.576 saídas float4 do helper gamma idênticas em WARP.
Cinco programas capturados, antes/depois, compilados por FXC e DXC (10 variantes).
Artefatos de teste preservados no workspace em work/fps60-v2/*v21*.

Ganho em gameplay ainda NÃO medido. Comparar a mesma cena, isqueiro ligado,
resolução interna 1920x1080 e janela de saída 1920x1080, excluindo pausas F12.
O último teste v20 usou saída 2267x1274, logo não é comparação controlada de FPS.
Interpolação final permanece na CPU. Executáveis EU/US não são atualizados.

## Busca CPU e identificação de alvos GPU v20, somente JP — 19/10/2026

A v19 confirmou custo misto. Em carga próxima de 6,7 mil draws, GPU em
8,4–8,5 ms a 720p interno, 12,2–12,6 ms em 1080p, 14,4–15,3 ms em 1440p
e 15,7–20,3 ms em 4K. A busca CPU fica perto de 5 ms. A maior categoria GPU
é other_offscreen, que inclui partes necessárias da cena. Log e análise
preservados em performance-v19. Não há justificativa para eliminar passes
ou reduzir qualidade só com essa classificação.

CPU v20: a AABB do primeiro ponto de cada bloco existente fornece um limite
inferior de distância. Além do raio original, esse limite agora usa o melhor
escore já encontrado e o mesmo denominador/epsilon do teste original. Se o
bloco inteiro não pode vencer NEM empatar, contabiliza as visitas originais
e segue para o próximo. Não altera ordem, limite de 128, ambiguidade, câmera,
verificação completa de saltos, alias ou costura. Não há novo índice espacial
ou alocação. POSE-BLOCKS registra os blocos descartados.

Validação: 2.400 frames comparados à v19 incluindo estado completo e saída
final de vetos/costuras, ASan, 20 mil AABBs conservativas e fronteiras de
raio/epsilon. Ensaios sintéticos do histórico aproximadamente 4–7% mais leves
nas cargas medidas; carga pequena/mista pode não ganhar. Isso não comprova
o mesmo ganho de FPS na partida.

GPU v20: perfil amplo agora também separa tempos pelo alvo primário de cada
range. GPU-TARGET mostra os oito mais caros por offset guest, dimensões reais
no host e categoria. Inclui trabalho/barreiras do range e, quando presentes,
os anexos MRT/profundidade: não é o custo isolado de uma textura. Os rótulos
são metadados; não mudam PSO, desenho, shaders ou a ordem de comandos.
Somar os alvos ao GPU-RANGES duplicaria os tempos. Alvos sem dimensões e
excesso de 64 chaves vão para GPU-TARGET-OTHER, sem perder o total. Capacidade
de 512 ranges continua descartando amostras excedentes com segurança.
Testes WARP cobrem associação entre timestamp e alvo após fence, chaves com
mesmo offset e tamanhos diferentes, acumulação e excesso de chaves.

Teste: executar-code-veronica-60fps-perfil-amplo.cmd, mesma cena com isqueiro
ligado durante 20 segundos em 1080p interno; repetir em 4K se desejado. F12
uma vez ao final permite relacionar o alvo mais caro ao manifest/shader.
O CMD normal permanece sem detalhamento; cálculo final CPU permanece padrão.
Somente o executável JP foi recompilado. Não houve mudança de qualidade.

## Perfil amplo v19, somente JP — 19/10/2026

O teste v18 foi preservado em performance-v18. No trecho pesado, pose em
torno de 8,1 ms, build 4,5 ms e GPU 12,1 ms, em cerca de 21,2 ms/quadro.
CPU e GPU se sobrepõem: não somar esses tempos. Outras amostras mostram
GPU perto de 8 ms; o log anterior não marca resolução/escala por janela,
então não é possível atribuir essa diferença só à resolução. O contador
repeated_checks_avoided ficou zerado nessa sessão: a validação v18 não
encontrou uploads compartilhados e não demonstrou benefício nesse cenário.

Novo atalho: executar-code-veronica-60fps-perfil-amplo.cmd. Mantém blend CPU
e qualidade original. CV_FRAME_DETAIL=1 ativa ranges amostrados em 1/30
presents, sem PERF nem relógio por desenho no modo normal. Há pequeno custo
nos quadros amostrados; medir FPS de referência também com o CMD normal.

GPU-RANGES separa setup/uploads, shader de sombra reconhecido, outros RTs
fora da tela, alvo principal, cópias/clears explícitos, pós-processamento/
composição/UI, conversão de profundidade e desenho legado. Cada range
inclui os comandos/barreiras nele contidos; não representa só custo do
shader. A categoria de sombra identifica o FP conhecido, não toda luz do
jogo. A soma dos ranges corresponde aos intervalos medidos, sem somar CPU.

CPU-RECORD-RANGES mede preparação/gravação nos mesmos quadros amostrados.
É parte de build e inclui prepass; não somar novamente ao FRAME-PERF.
CPU-THREAD usa tempos efetivos Windows de usuário+kernel, agregados por
janela, separando custo da thread de render e do processo inteiro. O processo
inclui outras threads simultâneas, portanto tampouco se soma ao tempo do frame.
GetThreadTimes tem granularidade limitada: avaliar agregados, não um frame.

FRAME-CONTEXT informa base, display, escala e a resolução capturada junto
aos timestamps. mixed_resolution=1 invalida a comparação daquele agregado:
esperar a próxima janela estável. O primeiro agregado de CPU do processo
não tem referência anterior (process_window_ready=0). Pausas, minimização,
menus e F12 também podem distorcer intervalos; desconsiderar esses trechos.

O probe apenas insere timestamps/resolve na lista existente e lê depois da
fence original. Não cria submits, signals ou waits. Limite de 512 ranges;
se excedido, descarta a amostra, registra dropped_samples e mantém o desenho.
A medição fora do modo amplo permanece por listas, como antes. Testes WARP
cobrem duas listas e bancos, tags, resolução mista, soma dos tempos, leitura
somente após fence, amostras descartadas e overflow sem queries fora de faixa.

Teste manual: na mesma sala/câmera, manter o isqueiro desligado e ligado
por 20 segundos cada; repetir ligado em resolução menor e maior, mantendo
as outras opções iguais. Depois visitar outra sala por 20 segundos. Evitar
F12 repetido; uma captura ao final de cada condição é suficiente. Não houve
alteração gráfica nem mudança nas regras de interpolação nesta rodada.

## Continuação CPU v18, somente JP — 19/10/2026

O teste v17 não demonstrou ganho com blend final GPU. No trecho de cerca de
6.694 draws, pose CPU 7,53 ms contra 7,98 ms no caminho GPU; upload 0,24
contra 0,48 ms. Logs preservados em performance-v17/cpu-blend.txt e
gpu-blend.txt. São amostras aproximadas, com F12/movimento em parte da sessão.

O CMD normal volta a usar blend CPU e registros de 16 bytes. O protótipo
GPU fica opcional com --gpu-blend / CV_POSE_CPU_BLEND=0. O modo original
de 30 FPS e os executáveis EU/US continuam preservados.

A v18 evita validar várias vezes o MESMO vértice físico dentro do frame
imutável: intervalos reutilizados por passes compartilham a validação de
stamp, finitude e magnitude. Usa scratch existente, zerado em cada chamada,
sem memória extra persistente. Vértices distintos continuam independentes,
mesmo que tenham posições iguais. Uma falha mantém a recusa do frame inteiro.
POSE-VALIDATE registra verificações feitas e repetições evitadas.
CV_POSE_NOVERTEX_VALIDATION_CACHE=1 permite comparar a validação anterior.
Na mesclagem, propostas já vetadas não voltam a calcular posição/matriz;
os vetos coletivos e o fechamento de costuras continuam idênticos.

Caches completos de posições interpoladas e consultas de costuras foram
experimentados e DESCARTADOS antes da instalação: o custo de memória e
consulta não compensou. A v18 não contém esses dois caches.
Validação: 2.400 frames contra v17, incluindo snapshots, fontes, saídas,
receitas, vetos e costuras; casos de ranges sobrepostos, dados que se tornam
inválidos no frame seguinte, stamp, limites e alternativa sem reaproveitamento.
Ensaio sintético do histórico mostrou diferenças pequenas (aproximadamente
1–4% conforme a carga), sem comprovar ganho correspondente de FPS no jogo.

## Cálculo final de posições na GPU v17, somente JP — 19/10/2026

Protótipo comparável ao caminho CPU v16. O shader calcula old + (current-old)
* alpha, usando operações precise para preservar a ordem sem fusão FMA.
Os registros por vértice têm 32 bytes (endpoints, modo e alpha), em bancos
que continuam protegidos pelas fences existentes. Não há dispatch, fence
nem retorno GPU->CPU adicionais na renderização da partida.

A busca, a coerência, a seleção de propostas, os vetos e a união de costuras
continuam na CPU. A receita acompanha exatamente a proposta vencedora das
costuras, inclusive quando vem de outro upload/câmera. Conflitos e falhas
conservam o comportamento anterior. A CPU AINDA calcula propostas para essas
comparações; portanto esta etapa não elimina o custo CPU total da interpolação
e pode não aumentar FPS. O objetivo imediato é validar esse caminho GPU.
Câmeras também continuam calculadas na CPU.

Valores minúsculos, parâmetros fora da faixa ou ausência de receita usam
a posição final CPU, sem aproximação. F12 reconstrói a receita na CPU para
manter as capturas úteis. POSE-GPU relata vértices que realmente usaram o
blend GPU e os fallbacks. Não interpretar fetch=GPU como prova do blend:
fetch já era GPU na v16.

CMD normal: blend GPU; atalho executar-code-veronica-60fps-interpolado-cpu-blend.cmd
ou parâmetro --cpu-blend: blend CPU e registros originais
de 16 bytes, preservando também o tamanho do buffer para comparação.
--cpu continua sendo a comparação completa com conversão de atributos CPU.
O caminho GPU adiciona 24 MiB de upload nos dois bancos e 6 MiB de receitas
CPU; a ausência de memória para receitas conserva o cálculo final CPU.
A lógica de 30 Hz e o modo original de 30 FPS continuam preservados.

Validação: 2.400 frames comparados à v16 incluindo saída final e procedência
das receitas após costuras/conflitos; testes CPU e AddressSanitizer; 3.072
resultados em D3D11 WARP e 32.768 em D3D12 WARP usando FXC e o adaptador DXC
de produção, bit a bit iguais à CPU. Ambos os bancos, reset, w original,
zeros com sinal e recusa de entradas minúsculas/inválidas cobertos. Os 32
programas de vértices capturados também compilam com os dois compiladores.
Ainda é necessária a validação visual e de desempenho na GPU do usuário.

## Validação e coerência v16, somente JP — 19/10/2026

A partida v15 ficou em cerca de 21,7 ms / 46 FPS com isqueiro ligado:
pose 8,3 ms, histórico 4,9 ms, coerência 0,75 ms e costuras 1,05 ms.
O log está preservado em performance-v15. Ainda faltam aproximadamente
5 ms para o orçamento de 16,7 ms necessário aos 60 FPS nessa cena.

A v16 reaproveita os IDs exatos de câmera durante a própria validação:
normas e verificações numéricas são executadas uma vez por matriz
bit a bit idêntica no frame. Cada chamada reconstrói as tabelas; colisões
continuam exigindo comparação completa. A preparação reutiliza esses IDs.
A coerência calcula o hash canônico de cada banco uma vez por chamada,
reutilizando-o nos dois passes. Comparações numéricas longas usam SSE2
sem ler fora do array e conservam a igualdade de zeros com sinal e a
rejeição de NaNs. A alternativa escalar continua disponível.

São removidas repetições na CPU; esta etapa ainda não move a interpolação
para a GPU. Não altera amostragem, limites, políticas de luz, proteção de
aliases, costuras nem o relógio original de 30 Hz. Scratch adicional:
9 bytes por banco máximo, alocado somente junto ao histórico opt-in.

Validação: comparação com a v15 em 2.400 frames incluindo snapshots,
fontes de coerência, propostas mescladas, vetos e resultado completo das
costuras; casos de NaN/infinito após reutilização, bancos fora de faixa,
zeros com sinal e caudas das comparações vetoriais. No ensaio sintético,
histórico aproximadamente 2–6% mais leve conforme a carga; esses números
não comprovam ganho igual de FPS em gameplay. Executável atualizado
somente para JP; medir novamente pelo CMD normal com isqueiro ligado.

## Busca de correspondências v15, somente JP — 19/10/2026

O teste real v14 confirmou o ganho de preparação: prepass cerca de 1,7 ms
e build 5,7 ms, com aproximadamente 6.649 pipelines e 6.648 janelas de
texturas de vértices reaproveitados por present. O trecho estável ficou em
24,7 ms (40–41 FPS), com lógica em aproximadamente 30 updates/s. A pose
continuou em 9,9 ms, dos quais cerca de 6 ms no histórico. O memo de posições
quase não teve acertos na partida. Log preservado em performance-v14.

A v15 mantém a preparação da v14 e modifica a busca:

- Empacota amostras do candidato anterior uma vez por prepare, sob demanda,
  em memória CPU comum. SSE2 compara quatro distâncias simultâneas, mantendo
  a ordem de operações de cada distância e a soma original entre amostras.
  Há alternativa escalar para plataformas sem SSE2.
- Uma soma parcial pode descartar apenas candidatos comprovadamente acima
  do melhor escore mais o epsilon original. O termo da câmera e as demais
  distâncias são não negativos. A ordem de visitas, o limite de 128, os
  empates, a verificação completa de saltos e as políticas gráficas continuam
  iguais. Uma rejeição pelo escore NÃO é gravada como distância inválida no
  memo: ela depende do melhor candidato naquele momento.
- Amostragem de draws pequenos usa índices consecutivos; draws maiores
  usam denominador constante 15. Após sondar 512 draws sem grupos repetidos,
  evita hashear as amostras restantes e usa IDs únicos, sempre seguros.
  A sondagem recomeça pelo input atual no próximo prepare, permitindo recuperar
  reaproveitamento sem depender dos IDs únicos do frame anterior.

Validação: 2.400 frames comparados à v14, com decisões, poses, limites e
fontes coerentes idênticos; 32.000 somas comparadas bit a bit, fronteiras de
raio/epsilon, rejeições sem contaminar o memo e soak de 10.000 frames. Testes
otimizados, alternativa escalar e AddressSanitizer passaram. Ensaio denso
com amostras distintas: histórico 8,04 -> 6,64 ms (cerca de 17% menos tempo).
Casos com forte reaproveitamento ficaram próximos da v14; carga pequena
mista pode pagar alguns microssegundos extras. Isso não comprova ganho
equivalente de FPS na partida. Memória adicional de amostras/epochs: cerca
de 4,6 MiB, alocada apenas junto ao histórico no modo interpolado.
POSE-PERF registra search_candidates e search_pruned. Usar o CMD normal,
sem PERF detalhado. A atualização de executável continua restrita ao JP.

## Reutilização de preparação e interpolação v14, somente JP — 19/10/2026

O cache de busca de superfícies da v13 não demonstrou ganho no teste do
usuário (isqueiro ligado: prepass cerca de 6,5 ms, 6.755 operações). Ele foi
removido desta rodada. A v14 modifica duas partes do caminho interpolado:

- Preparação: reutiliza o índice e a chave do PSO pronto encontrado no
  prepass. Na hora do draw confere a entrada viva, geração, conteúdo do VS e
  estado de compilação. Evicção ou mudança usa a consulta original; não se
  guarda ponteiro de PSO sem referência. Programas sem snapshot ficam fora.
  Janelas de quatro descritores de texturas de vértices também são
  compartilhadas dentro do mesmo frame quando recursos e formatos são
  exatamente iguais. Colisões usam comparação completa. O cache dessas
  texturas já impede sobrescrever um recurso usado no frame corrente.
- Interpolação: agrupa amostras de posições somente depois de comparar
  exatamente seus bits e a contagem total de vértices. Memoriza a soma
  original de distâncias por par de grupos, invalidada a cada prepare.
  Preserva visitas, ordem de candidatos, limite de busca, ambiguidade,
  cortes, verificação de salto de TODOS os vértices e política de iluminação.
  Se houver pouco reaproveitamento, abandona o memo naquele prepare e tenta
  novamente no seguinte.

As otimizações de preparação exigem o modo interpolado inicializado.
CV_PREP_NOREUSE=1 permite desativá-las para comparação. O modo original
continua usando as consultas e escritas de descritores originais. O CMD
normal mantém PERF desativado; --prepass-detail continua opcional.
FRAME-PREP registra contagens de PSOs e janelas reaproveitados, sem timers
por draw. POSE-PERF inclui position_hits/misses/bypass.

Validação local: 2.400 frames com resultados idênticos à v13 (snapshots,
tracks, decisões, ambiguidades, limites e fontes coerentes); testes de
10.000 frames e dos novos caches com AddressSanitizer. Helpers extraídos do
backend foram verificados com colisões, mudança de frame, evicção, shader
pendente e mudança de geração/VS. Teste sintético de 6.000 draws repetidos
entre passes: history 10,35 -> 6,33 ms com seis grupos de câmera, checksum
idêntico. Carga pequena mista pode pagar cerca de 0,03 ms de indexação extra.
Esses números não comprovam ganho de FPS no jogo; falta a rodada do usuário.
Compilar somente MyGameRecomp no build-release-jp; conferir hashes EU/US.

## Perfil do quadro v12, somente JP — 19/10/2026

Resultado real JP: isqueiro ligado, janelas estáveis de cerca de 37–38 FPS
(26,4–27,0 ms entre presents). FRAME-PERF: pose 9,1–9,4 ms, build 9,9–10,2
ms, sendo prepass 6,2–6,4 ms, e tail_wait 7,1 ms. GPU em amostras válidas:
lists 12,0–12,2 ms, span 15,0–15,7 ms e gaps 2,9–3,6 ms, invalid=0. Sete
listas/quadros. CPU e GPU se sobrepõem e não se somam: o trecho exclusivo
da CPU que precede os desenhos e as lacunas de alimentação da GPU são
relevantes. A espera final é trabalho ainda em andamento, não tempo ocioso
que possa ser simplesmente removido. A lista na GPU pode custar 12 ms
sem a utilização média da placa chegar a 100% no quadro de 26 ms.
Sem isqueiro, trechos estáveis em aproximadamente 60 FPS (16,67 ms), pose
3,3 ms, build 3,2–3,6 ms (prepass 1,9–2,1), GPU lists 7,4–7,8 ms. Lógica
continua em cerca de 30 updates/s. Log preservado em performance-v12 no
workspace. O próximo alvo é detalhar/reduzir o custo de preparar recursos,
SRVs e pipelines em prepass, mantendo também o objetivo de reduzir pose.
Otimizar apenas build não garante 60 FPS: a pose precede os comandos GPU.

O campo ops inicialmente saiu zero porque s_rb.count é zerado antes do
retorno do render. Corrigido para o snapshot s_dbg_last_draws, capturado
antes do reset. Os timers, contadores de listas e timestamps não dependem
daquele campo e continuam válidos. Correção só no diagnóstico, sem mudança
de renderização. Executável atualizado novamente apenas no JP.

Por orientação do usuário, nesta fase compilar e atualizar SOMENTE a versão
japonesa. EU/US ficam para depois da validação. Build desta rodada usa
build-release-jp.cmd diretamente, sem passar pelo build-release.cmd que
sempre constrói EU. SHA-256 dos executáveis EU e US foi registrado antes e
conferido depois: ambos preservados. MyGameRecomp_JP.exe foi atualizado.

A interpolação continua v11. Esta rodada acrescenta diagnóstico opt-in:
CV_FRAME_PROFILE=1, somente quando CV_FPS_TEST_60=1 e
CV_RENDER_INTERPOLATION=1. O mesmo CMD de 60 FPS interpolado ativa o perfil;
--no-frame-profile permite comparar sem ele. O launcher e o modo de 30 FPS
não são modificados. Não há mudança de shader, qualidade, iluminação,
resolução, lógica ou ordem de desenhos. Nenhuma espera, submissão,
reinicialização de lista ou sinalização de fence é adicionada pelo medidor.

FRAME-PERF: uma média a cada 5 s, em milissegundos por present:
- interval_ms: intervalo entre inícios de quadros apresentados, incluindo
  a produção/pacing do jogo, lotes parciais e espera pelo próximo trabalho;
- render_wall: tempo da thread de render, da entrada à espera final;
- entry_wait, pose, build, submit, readback_diag, present, fence,
  bookkeeping e tail_wait: partes exclusivas de render_wall;
- prepass: SUBCONJUNTO de build, preparação de recursos/SRVs/pipelines;
- producer_span: intervalo na thread da FIFO entre a liberação do lote
  anterior e a prontidão do próximo; inclui lógica/pacing/esperas, não é
  uma medição isolada de uso de CPU;
- backpressure: espera da FIFO pela liberação da thread de render;
- ops e parts: médias das operações registradas e das listas enviadas.
Producer/backpressure podem se sobrepor ao render e não são somados a ele.
F12 fica fora da média CPU e também invalida o próximo intervalo entre
inícios. Os relógios são lidos só em limites de etapas/listas; não há
contadores/timers por desenho, PERF ou PS3_TIMELINE pesados.

FRAME-GPU: timestamps D3D12 em aproximadamente um de cada 30 presents.
Cada lista enviada ganha início/fim, inclusive as partes de submit_split.
Readback mapeado é lido depois da espera já existente e SOMENTE quando o
fence normal do quadro alcança o valor gravado. Um slot pendente nunca é
reutilizado. lists_ms é a soma dos intervalos das listas na direct queue;
span_ms vai do primeiro timestamp ao último; gaps_ms separa as lacunas
entre listas (por exemplo, GPU aguardando os próximos comandos da CPU).
Isso evita chamar o span inteiro de trabalho ocupado da GPU. São amostras,
com janela ligeiramente defasada da CPU e com sobreposição a ela: NÃO somar
FRAME-GPU ao FRAME-PERF nem interpretar como percentual de uso da placa.
Não engloba trabalho de outras filas/processos ou o compositor do Windows.
Falha na criação/frequência/map mantém diagnóstico CPU e desenho normais.
F12 não gera amostras válidas. Recursos são liberados no shutdown normal.

Teste usa o helper de produção em D3D12 WARP com múltiplas submissões,
ambos os bancos, leitura bloqueada antes do fence, reuso de slots, perfil
desligado, descarte de captura, limite de partes/slots e ausência de fence.
Redução valida timestamps invertidos/sobrepostos, frequência inválida,
intervalos zero e valores perto de UINT64_MAX. PASS. Build JP e testes
OFF/ON/RAW/INTERP, pose (10.000 quadros), fetch seletivo, override GPU/WARP
e frame probe passaram. git diff --check passou. Log completo:
build-release/fps60-v12-jp-build.log. Falta a execução real na sala do
isqueiro, mantendo alguns segundos sem e com a luz. Sem commit/push.

## Reaproveitamento de comparações v11 — 18/10/2026

O novo teste real da v10 confirmou CPU history como principal custo da
interpolação. Exemplos na sala 80000000: 10,566–10,744 ms/present de
history, total de pose 15,091–15,435 ms/present e cerca de 33–34 renders/s,
com a lógica em 30 updates/s. Trechos sem os passes da luz ficam perto de
60 renders/s e 3–5 ms/present de pose. O log foi preservado no workspace
em performance-v10. Câmera, animação e quantidades de malhas variaram;
não se deduz um ganho exato v9/v10 dessas janelas diferentes.

A v11 reaproveita o resultado da comparação de PARES DE MATRIZES, não
uma escolha anterior de objeto. Matrizes c0..c7 com os 128 bytes idênticos
recebem um ID canônico dentro de cada snapshot. O hash serve apenas para
localizar candidatos: memcmp integral confirma a equivalência. Cada par
(matriz anterior, matriz atual) guarda o veto de corte ou a distância da
transformação calculada originalmente. As verificações de chave, contagem,
distância dos vértices e ambiguidades continuam para cada candidato na
mesma ordem e com o mesmo limite. A decisão final da malha não é armazenada
para ser aceita sem busca; IDs de banco/ponteiros não viram identidades de
objeto. A tabela de pares é invalidada a cada prepare e confere ambos os
IDs e a geração, inclusive em colisões e wrap. Matrizes são imutáveis no
snapshot congelado. Grupos podem deixar de ser unidos em um bucket cheio,
o que apenas perde oportunidade de reaproveitamento.

Também são preparados uma única vez por consulta os até 16 índices/XYZ
amostrados. A mesma aritmética inteira e os mesmos bytes são usados em
todos os candidatos, evitando repetir divisões e leituras do quadro atual.
O teste completo de todos os vértices para vetar saltos permanece depois da
busca, incluindo vértices fora da amostra. Coerência, aliases, costuras,
política da projeção da luz, shaders, resolução e lógica não mudam.

Um teste com matrizes todas diferentes mostrou custo desnecessário do
cache. Por isso, se um prepare acumula pelo menos 512 misses
e menos de um hit para cada 16 misses, o restante do prepare usa o cálculo
direto. O próximo prepare tenta novamente. IDs exatos continuam sendo
construídos mesmo durante esse desvio, para permitir recuperação quando
as matrizes voltam a se repetir. Os novos contadores camera_hits,
camera_misses e camera_bypass no POSE-PERF são médias de comparações por
present, junto aos mesmos tempos (não são milissegundos). Um resumo/5s.

Microteste com 6.000 draws, 144.000 vértices, seis passes, 60 quadros:
matrizes em seis grupos exatos, v10=15,572 -> v11=7,321 ms/prepare (2,13x).
11.072.880 hits e 2.160 misses ao longo do teste. Com matrizes distintas:
15,843 -> 15,174 ms/prepare, usando o desvio automático. Poses e checksums
idênticos em ambos os casos (D324D4A2C8CD6BF2). Microteste misto de 2.400
quadros: 0,247 -> 0,159 ms/prepare. São cargas sintéticas, não ganho de FPS
medido na partida. Os ganhos dependem da repetição real das matrizes.

Comparação com o header v10 preservado passou: snapshots, poses atuais e
anteriores, câmeras, tracks, correspondências, ambiguidades, caps, aliases
e fontes coerentes idênticos. Passou também com AddressSanitizer. Suíte
permanente testa colisões forçadas de hash e cache, troca de conteúdo no
mesmo banco, wrap de geração, amostragem de 1 a 32 vértices e 600 matrizes
distintas seguidas de matrizes repetidas (desvio e recuperação). Estabilidade
de 10.000 quadros e proteções anteriores também passaram com ASan.
Memória adicional v11 no backend: cerca de 1,75 MiB. Somente o modo
experimental aloca/usa essas estruturas; o caminho original de 30 FPS é
preservado. Build EU/JP/US concluído; executáveis atualizados. OFF/ON/RAW,
INTERP, pose, fetch seletivo e GPU/WARP passaram. git diff --check passou.
Log completo em build-release/fps60-v11-build.log. Ainda falta medir o
efeito na partida real. Sem commit/push nesta etapa.

## Filtro espacial conservador v10 — 18/10/2026

F12 v9 frame_92 e frame_93 mostram a mesma sala/câmera, com o isqueiro
desligado e ligado. Contando DRAW (sem CLEAR/COPY): 2.655 -> 7.330 por
quadro. A segunda captura contém 3.276 draws do gerador de sombra
7EA4C4E7 em seis viewports do atlas. POSE shadow_draws=3.143 conta apenas
os elegíveis para interpolação, não todos os geradores. No log, trechos
estáveis passam de cerca de 60 para 27–31 renders/s; lógica continua em
cerca de 30 updates/s. CPU history sobe de 2,743–3,621 para cerca de
12,2–12,4 ms/present, e o total de pose de 4–5 para 15–19 ms/present.
As pausas do próprio F12 foram excluídas. Isso confirma custo relevante
na CPU; esses tempos não medem execução na GPU. Registros preservados
no workspace em work/fps60-v2/performance-v9-lighter.

A v10 agrupa cada oito candidatos consecutivos da cadeia de busca já
existente e calcula uma caixa que contém seu primeiro vértice. Se a
distância mínima da consulta à caixa excede o limite original de 8 unidades,
todos os candidatos do bloco seriam rejeitados pelo teste original e
podem ser pulados. Caixas são construídas só para os primeiros 128
candidatos de cada bucket. A contagem de visitas inclui os descartados,
mantendo exatamente o teto original, a ordem de busca, a escolha do
melhor candidato e as regras de ambiguidade. Nenhuma pose nova é aceita.
Continuam as verificações completas de saltos, coerência de passes,
vértices compartilhados, alias e projeção da luz. Não há mudança de shader,
resolução, intensidade da luz, qualidade/frequência das sombras ou lógica.
Somente a interpolação experimental usa o filtro. Memória adicional:
32 bytes por draw de capacidade, 768 KiB no backend (24.576 slots).

Comparação automática com o header v9 preservado: 2.400 quadros de
1.025 draws em oito cenários, incluindo câmera em movimento, reordenação,
culling, chaves com colisões, ambiguidades, capacidade esgotada, aliases e
tetos de busca. Poses e câmeras atuais/anteriores, tracks, fontes coerentes,
correspondências, decisões e contadores idênticos bit a bit. Passou também
com AddressSanitizer. Microteste misto: 0,370 -> 0,257 ms/quadro.
No microteste de 6.000 draws/36.000 vértices/120 quadros, câmera fixa:
2,028 -> 1,643 ms/quadro; câmera em movimento: 2,060 -> 1,737 ms/quadro.
Checksums e 720.000 correspondências por caso idênticos. São medições
isoladas, não ganho medido de FPS da partida. A próxima execução no mesmo
CMD e na mesma sala deve confirmar a diferença em POSE-PERF history/total.

Regressões permanentes incluem blocos incompletos, 127/128/129 visitas,
armazenamento esgotado com first==maxv e distância exatamente no limite,
com arredondamento de float em coordenadas até +/-1.000.000.
Build EU/JP/US concluído, executáveis atualizados. Testes OFF/ON/RAW/INTERP,
pose (incluindo estabilidade de 10.000 quadros), fetch seletivo e GPU/WARP
passaram. Suíte de pose também passou com AddressSanitizer. Log completo:
build-release/fps60-v10-build.log. Ainda falta medir o FPS da v10 na partida.
Sem commit/push nesta etapa.

## Busca de histórico mais barata v9 — 18/10/2026

Usuário confirmou lentidão na v8. A GPU/VFETCH híbrida foi efetivamente
ativada: log registra buffer de 148 MiB e "GPU + CPU position/UV". No log
anterior da mesma revisão, POSE-PERF registra 22,753 ms/present para a
interpolação, dos quais 18,111 ms em history; collect=0,864, coherence=1,138,
merge=0,632, aliases=0,189, seams=1,524, upload=0,295. A execução estava
em cerca de 25–27 desenhos/s e 30 updates/s. Isso localiza concretamente
um gargalo na busca de correspondências. O log mais recente acabou durante
o carregamento, sem novo relatório de gameplay; o processo não estava em
execução na consulta. Logs foram preservados em performance-v8 no workspace.

Na v8, cada candidato passava primeiro pela comparação de câmera (matrizes,
normas e raízes), antes de ser descartado por distância. Malhas da sombra
repetem muitas chaves de UV/topologia pela sala, então havia milhares de
comparações caras para candidatos que não poderiam corresponder.

A v9 testa a distância dos vértices amostrados antes da câmera. Câmeras
bit a bit idênticas também retornam imediatamente sem repetir a álgebra.
Não há mudança de candidatos, ordem de visita, pontuação, limite de busca,
regra de ambiguidade, alpha, verificação exaustiva de saltos ou fallback.
Não foram removidas proteções nem modificadas a iluminação ou a renderização
original de 30 FPS. Esta é uma reordenação de rejeições equivalentes sobre
dados já validados, mantendo o resultado.

Benchmark comparativo: 6.000 desenhos com chaves repetidas, 36.000 vértices,
120 quadros em cada caso. Câmera fixa: 6,276 -> 1,896 ms/quadro; câmera em
movimento: 6,407 -> 1,965 ms/quadro. Ambas as versões somaram 720.000
correspondências por caso, capped=0 e checksum de todos os vértices igual
C4E986E7FB27AD9B. É cerca de 3,3x NESSE microteste de histórico; não é uma
medição nova do FPS da partida. Saídas before/after preservadas no workspace.

Regressão adicional verifica que um vértice com salto grande, fora dos 16
amostrados pelo filtro, continua vetando a pose na verificação completa.
Build final EU/JP/US concluído e executáveis atualizados. OFF, ON, RAW,
INTERP, POSE, fetch seletivo e GPU/WARP passaram; a suíte de estabilidade
passou também com AddressSanitizer. `git diff --check` passou. Log em
`build-release/fps60-v9-build.log`. O próximo POSE-PERF no mesmo CMD medirá
o custo real desta revisão durante a partida. Sem commit/push.

## Primeira otimização de desempenho v8 — 18/10/2026

Comparação dos logs reais mais recentes: tanto o modo 30 quanto o modo 60
usavam RES_SCALE=1.50 (1920x1080), mas somente o de 30 registrava VFETCH na
GPU. O experimento desligava esse caminho para conseguir acompanhar a pose
no CPU. No log v7, a sala 0x80000700 chega a cerca de 20–23 desenhos/s,
mantendo aproximadamente 30 updates lógicos/s. Isso confirma custo de
renderização; ainda não quantifica a divisão entre conversão, pareamento,
GPU e esperas. Não atribuir toda a queda a uma única causa sem medição.

A v8 permite VFETCH na GPU junto com interpolação quando a preferência
RSX_GPU_VFETCH=1 está ativa. A CPU converte só a0/a8/a15 para obter posição
e identidade de UV, usando o MESMO decoder e memo por draw. Os 13 atributos
restantes continuam no caminho GPU existente. O envio de 256 bytes de
atributos por vértice dá lugar à cópia de intervalos brutos do VFETCH mais
um float3/flag opcional por vértice de saída. Não remove verificações,
sombras, resolução, efeitos ou as decisões visuais das revisões anteriores.

Overrides ficam em uma região separada do buffer de vértices brutos, com
16 bytes por saída em cada banco cercado por fence (24 MiB adicionais).
O shader aplica só XYZ, preservando a0.w e todos os demais atributos. A
flag é zerada a cada gravação do slot, inclusive quando não há interpolação,
evitando reaproveitar uma pose antiga. Descritores e índices ficam intactos.
O cache separado `vfetch-pose1` evita reutilizar shaders/PSOs incompatíveis;
o cache e o caminho normal de 30 FPS continuam separados. F12 decodifica
o snapshot bruto congelado e inclui o override efetivamente usado.

CV_POSE_CPU=1 conserva a referência antiga de CPU para comparar. O mesmo
CMD de 60 aceita `--cpu`. Não se grava essa escolha no INI. CV_POSE_PROFILE=1
registra um resumo por 5 s dos estágios CPU da interpolação, sem probes por
draw; esse tempo exclui gravação de vértices e execução na GPU. Os probes
pesados PERF/TIMELINE/FRAME_BUDGET continuam desligados no CMD.

Regressão: 2.352 casos do decoder seletivo comparados bit a bit com a versão
completa e com rsx_fetch_attrib; formatos 1/2/3/4/5/6/7, constantes, divisores
e páginas IO não contíguas. Teste WARP executa o helper HLSL de produção e
verifica duas paridades, índices de saída, reset, a0.w e metadados intactos.
Um microteste de conversão de 100.000 vértices mediu 5,234 ms no caminho
completo mais upload e 2,416 ms no seletivo (2,17x nesse trecho). Isso NÃO
mede o FPS da partida nem o custo total da interpolação.

Classificação dos shaders de vértices é calculada uma vez e armazenada no
cache, evitando vasculhar o preâmbulo HLSL a cada draw. A elegibilidade
considera apenas o corpo do shader do jogo; os Loads do próprio VFETCH não
desativam a interpolação. Texturas de vértices do jogo seguem excluídas.

Build final EU/JP/US concluído e executáveis atualizados. Testes OFF, ON,
RAW, INTERP, POSE (10.000 quadros), fetch seletivo e GPU/WARP aprovados.
Fetch/overrides passaram também com AddressSanitizer. As 32 variantes dos
16 shaders reais do F12 (com/sem override) compilaram em FXC SM5 e DXC SM6
com o wrapper e helper de produção. `git diff --check` passou. Registro em
`build-release/fps60-v8-build.log`; última medição de conversão 5,091 vs
2,381 ms (2,14x no trecho sintético). Logs anteriores de 30/60 foram
preservados no workspace em `work/fps60-v2/performance-v7`.

Ganho de FPS e validação visual precisam de teste no mesmo trecho do jogo,
depois da compilação inicial dos shaders no cache novo. O jogo não foi
aberto automaticamente nesta etapa. Não há promessa de 60 constantes em
todas as salas. Sem commit/push.

## Auditoria contra explosão de vértices v7 — 18/10/2026

Pedido: revisar a estabilidade das malhas nos 60 FPS. O usuário pediu para
deixar a iluminação como está; a política de luz da v6 foi preservada.
Não há relato/captura nova de uma explosão de vértices ocorrida na partida.
Esta é uma verificação preventiva, não confirmação de defeito observado.

Proteções revisadas: entrada finita e limitada, ranges/índices e stamps de
vértices, mudanças de cena, cortes de câmera e pausas, salto de todos os
vértices (não só os 16 usados no pareamento inicial), correspondências
ambíguas, capacidade dos snapshots, coerência entre passes e veto de
aliases. A interpolação limita alpha a [.5,1], sem extrapolação da pose.
Não há interpolação genérica das paletas ósseas ou shaders desconhecidos.

Encontrada brecha na detecção de cortes de projeção: a v6 comparava a
energia total de c4..c6. Uma reversão de um vetor de projeção pode conservar
exatamente essa energia e se anular no ponto médio. Redistribuir energia
entre eixos também podia ocultar o colapso de um deles. O teste de reversão
falha na v6; não foram usadas novas imagens para alegar ocorrência real.

A v7 também compara cada vetor de c4..c6: surgimento/desaparecimento de um
vetor, inversão de direção e mudanças bruscas de comprimento iniciam um novo
histórico, conservando a pose atual. Pequenas mudanças legítimas continuam
interpolando. O próximo quadro normal recupera o histórico. A mudança afeta
apenas o caminho experimental; o modo original e a regra da luz v6 seguem
iguais. Não muda shaders, normais, filtros, profundidade ou intensidade.

Novas regressões cobrem a reversão e o colapso de projeção, recuperação,
movimento comum, first/count/cb/nv/nd iguais a UINT32_MAX, NaN, infinitos,
coordenadas fora do limite e interpolação finita próxima do limite válido.
Teste determinístico de 10.000 quadros inclui saltos, trocas de cena,
identidade e projeção. Passou com 4.890 poses intermediárias: todos os
vértices emitidos ficaram finitos e entre as duas poses, e os dados de
entrada permaneceram intactos. É um teste CPU, não uma partida completa.

Limitação: o pareamento ainda usa topologia/UV/passe e proximidade, sem IDs
semânticos fornecidos pelo motor. Esta revisão não garante ausência de todo
defeito em qualquer sala/animação. Os casos testados retornam à pose atual
quando não há uma correspondência segura.

Build final EU/JP/US concluído e os três executáveis atualizados. Testes OFF,
ON, RAW, INTERP e POSE v7 aprovados, incluindo os 10.000 quadros. A suíte
também passou com AddressSanitizer e `git diff --check` passou. Log final:
`build-release/fps60-v7-build.log`. Mesmo CMD de 60 FPS. A validação CPU não
substitui uma partida completa. Sem alterações de performance, commit ou push.

## Coerência da projeção do isqueiro v6 — 18/10/2026

O usuário confirmou melhora da v5, mas encontrou um risco escuro no queixo
ao correr com o isqueiro. Novos F12: frame_90=7390/3530 ops e
frame_91=7791/3578 ops, ambos pose_version=5. Backbuffer da segunda captura
mostra o defeito. Arquivos referenciados no manifesto foram preservados em
`work/fps60-v2/captures-v5` no workspace da investigação. As pastas F12
reutilizam nomes e contêm arquivos antigos: considerar apenas referências
do manifesto atual.

Encontrada uma inconsistência numérica diretamente no F12 frame_91:

- VS26, op001, gerador dos momentos de sombra (FP 7EA4C4E7), usa
  c7=(39.2381,3.71998,3.39271,4.44995), já interpolado pela v5.
- VS33, op379, gera o MESMO atlas com skinning por paleta e usa
  c262=(38.8953,3.71517,3.42015,4.47582), a projeção atual. Esse caminho
  não é elegível à interpolação de c0..c7, que contém a paleta óssea.
- O próprio gerador de momentos e o leitor VS28/FP 0C121A16 usam
  fp_k0=(-4.47582,-3.71517,38.8954,0.0454545), coerente com a luz atual.

Portanto, o atlas mistura projeções para a mesma luz, e sua leitura conserva
outra origem. A diferença da primeira componente é cerca de 0.3428, não um
simples erro de arredondamento. O shader de momentos calcula distância à
luz atual, mas a posição de rasterização havia sido movida separadamente.
Essa inconsistência foi comprovada; o desaparecimento de todo risco visual
ainda depende de teste no jogo.

A v6 identifica o gerador por hash de código do snapshot do lote congelado
(não por endereço de programa/RT, que varia). Marca a projeção dessa luz
para conservar coletivamente suas constantes atuais. Outros desenhos com
a mesma câmera original, inclusive sem histórico, seguem a mesma decisão.
Os vértices continuam interpolando. A câmera de exibição permanece livre
para interpolar em seu grupo separado. Não há remoção de sombras, mudança
de bias, intensidade, filtro de 16 bits, normais, água ou modo de 30 FPS.
Interpolar a fonte de luz integralmente exigiria sincronizar também os
uniformes FP e projeções das paletas; interpolar só sua matriz VP era errado.

Regressão com as constantes reais falha na v5 e passa na v6. Verifica
projeção atual em todas as cópias, inclusive quando a cópia marcada não
possui histórico, movimento intermediário dos vértices e interpolação
independente da câmera principal. F12 registra `pose_shadow` com c7 original
e efetivamente enviado para a GPU; o log acrescenta `shadow_draws`.

Build final EU/JP/US concluído; os três executáveis foram atualizados.
Testes OFF, ON, RAW, INTERP e POSE v6 aprovados. A suíte de malhas também
passou com AddressSanitizer e `git diff --check` passou. Registro final em
`build-release/fps60-v6-build.log`. Mesmo CMD de teste. A confirmação visual
do queixo durante a corrida ainda depende da próxima execução no jogo.
Sem otimizações, commit ou push nesta revisão.

## Revisão de vértices compartilhados v5 — 18/10/2026

Revisão solicitada após o usuário relatar melhora aproximada de 90% dos
defeitos gráficos. O log da execução v4 foi conferido; não há capturas F12
novas dessa execução, portanto a revisão não atribui todos os defeitos
restantes a uma causa visual já comprovada.

Foi encontrado e reproduzido em teste um caso não coberto pela v4: um mesmo
intervalo de upload pode ser usado por duas câmeras, enquanto polígonos
vizinhos possuem uploads separados para cada vista. A v4 marcava aquele
ponto como pertencente a várias câmeras e o excluía da união das bordas.
O controle de aliases anterior só vetava propostas conflitantes; propostas
iguais nas duas vistas podiam continuar animadas, separadas das bordas
vizinhas sem histórico. O novo teste falha na v4 nessa comparação exata.

A v5 constrói um grafo dos pontos antes de modificar qualquer proposta.
Posições exatamente iguais na mesma câmera ligam seus slots de upload;
um slot efetivamente compartilhado liga as cópias das duas vistas de forma
transitiva. Posições iguais em câmeras diferentes, sem esse slot comum,
continuam independentes. Um veto ou conflito chega a todas as cópias do
ponto. Não se misturam normais, UVs nem posições por proximidade.

O teste passa na v5 nas duas ordens de desenho, preservando o ponto médio
da animação e a propagação do veto pelas duas vistas. Novas regressões
também verificam que esgotar a capacidade do grafo ou o limite de busca
não altera parcialmente as propostas. O chamador conserva o quadro
original nesses casos. Todas as regressões anteriores seguem passando.

O armazenamento opcional soma mais 3 MiB no modo interpolado. Não foram
alterados o modo original de 30 FPS, o relógio lógico ou os logs existentes,
nem realizada otimização de desempenho. O custo total da v5 ainda precisa
ser medido na etapa de performance; o microteste de histórico não mede
o quadro completo. A captura F12 e o log agora identificam a versão 5.

Build EU/JP/US concluído e os três executáveis atualizados. Testes OFF, ON,
RAW, INTERP e POSE v5 aprovados, assim como a execução de toda a suíte POSE
com AddressSanitizer e `git diff --check`. Log da compilação e dos testes:
`build-release/fps60-v5-build.log`. O resultado visual desta revisão ainda
precisa ser conferido no jogo, pelo mesmo CMD de 60 FPS. Não foram feitos
commit ou push.

## Continuidade das malhas v4 — 18/10/2026

O usuário confirmou que a v3 eliminou as manchas grandes, mas relatou
pequenas falhas pretas no personagem em movimento; parado ele fica correto.
Foram inspecionadas as cinco novas capturas F12 frame_90 a frame_94,
incluindo os closes do rosto e a movimentação na cela. Elas mostram linhas
e fissuras curtas nas superfícies e nos contornos durante a animação.

Os atributos capturados revelaram também um erro concreto na identidade:
no VS10 da malha animada, a14 alimenta os cálculos de iluminação e a15
alimenta as coordenadas de textura. O protótipo usava a14 no hash de UV,
então a animação das normais podia invalidar a identidade de uma parte do
personagem. A captura mostra, por exemplo, a14=(0.0941176,0.701961,0.701961)
e a15=(0.998535,0.614746). A v4 registra os UVs em a15/a8 e exclui a14
dessa identidade. Uma regressão com os valores reais confirma que mudar
a normal conserva o histórico e o ponto médio da animação; mudar o UV
continua distinguindo a geometria.

Foi reproduzida uma falha ainda presente na lógica da v3: dois polígonos
vizinhos, separados por material/UV e com intervalos de upload diferentes,
podem ter históricos diferentes. Seus vértices duplicados de borda ficam
em posições distintas quando apenas um dos desenhos interpola. A coerência
de cópias inteiras do mesmo desenho não cobre essa união entre polígonos
de formas diferentes. Essa reprodução é compatível com as novas fissuras;
o resultado visual da correção ainda precisa ser confirmado na partida.

A v4 resolve os pontos duplicados após os vetos de aliases. Exige posição
original exatamente igual e a mesma câmera original; não aproxima nem
arredonda pontos próximos, evitando unir pálpebras, camadas e superfícies
distintas por uma tolerância espacial. Um candidato reconhecido fornece
a posição intermediária para todas as cópias do ponto. Históricos em
conflito fazem aquele ponto usar a posição original em todas as cópias.
Um veto de proteção também alcança o ponto duplicado. Nenhuma etapa muda
o relógio lógico, o áudio ou a configuração original de 30 FPS.

Novo teste reproduz a abertura da borda na v3 e verifica seu fechamento
bit a bit na v4, conservando a posição intermediária reconhecida. Cobre
também conflitos de fase, pontos protegidos, pontos próximos distintos e
câmeras diferentes. O agrupamento é limitado; uma busca incompleta
conserva o quadro original. O armazenamento temporário soma cerca de
6 MiB apenas no modo interpolado. Não foi feita otimização de desempenho.

O log `[POSE] v4` acrescenta `seams` (cópias de pontos ajustadas) e
`seam_conflicts`. As próximas capturas F12 também registram a versão e
essas contagens no manifest, para que o diagnóstico seja do quadro exato.
O atalho de teste continua `executar-code-veronica-60fps-interpolado.cmd`.

Build final EU/JP/US concluído; executáveis atualizados. Testes OFF, ON,
RAW, INTERP e POSE v4 aprovados, incluindo identidade com normais animadas
e fechamento de bordas. `git diff --check` passou. Log:
`build-release/fps60-v4-build.log`. A validação visual desta revisão segue
pendente; o sucesso técnico não confirma a ausência de todos os defeitos
em movimento. Não foram feitos commit, push ou ajustes de performance.

## Correção visual v3 — 18/10/2026

O usuário confirmou fluidez de 60 FPS na revisão v2, mas encontrou manchas
verde-azuladas no personagem e em objetos, além de padrões de linhas.
As capturas F12 frame_90/91/93 mostram o defeito; frame_92 mostra o mesmo
ambiente sem a falha. O log confirma cerca de 60 desenhos/s e 30 updates/s.

Na captura frame_91, o passe de profundidade tem 561 desenhos em
`rt=0x011A4000 cmask=0`; o passe de iluminação tem 443 em `rt=0x01C30000
blend=1 cmask=4`; o material principal tem 556 em `rt=0x01528000 cmask=F`.
Os três usam zeta `0x026E0000`. A v2 excluía todos os desenhos com blend,
deixando a iluminação na pose original enquanto outros passes interpolavam.
Mesmo entre passes aceitos, uma história ausente ou diferente podia produzir
fases distintas. O log também registra quadros com `vertices=0 cameras=6`:
é necessário sincronizar as constantes mesmo sem movimento dos vértices.

A v3 inclui geometria 3D com blend que passe pelas mesmas verificações de
shader/câmera/profundidade. Após o pareamento temporal, agrupa as cópias da
mesma geometria do quadro atual por posições ordenadas e câmera exatas,
independentemente de shader, UVs, alvo ou material. Um histórico reconhecido
fornece uma única pose para todas as cópias. Históricos conflitantes fazem
o grupo inteiro usar a pose original; não apenas um dos passes.

Também agrupa câmeras separadamente. Todos os desenhos com as mesmas
constantes originais recebem a mesma câmera intermediária, incluindo
objetos recém-aparecidos. Câmeras diferentes de sombras/reflexos continuam
separadas. A comparação completa confirma o hash, sem depender apenas de
uma possível colisão. Agrupamento incompleto por limite de busca mantém
o quadro original. Shaders 2D que não leem c0..c3 deixam de bloquear
indevidamente a interpolação de uma câmera só porque reutilizam um slot.

O veto final de um slot compartilhado também é coletivo. Componentes
conexos unem cópias exatas da superfície e faixas de vértices sobrepostas;
um conflito faz todas essas cópias manterem a posição original. O mesmo
vale para cópias de constantes de câmera. Assim, a proteção de aliases
não desfaz a coerência obtida antes. Esse controle soma cerca de 3 MiB
ao armazenamento opcional da v2 e não se aloca no modo original.

Regressões CPU reproduzem três cópias de superfície com identidades de
passe diferentes e histórico incompleto. Verificam que profundidade,
iluminação e material recebem posições/matrizes bit a bit iguais após a
resolução, inclusive em movimentos apenas de câmera. Há verificações de
separação das câmeras de sombras, descarte coletivo e recuperação após
conflitos e veto transitivo de aliases. A carga de 4.000 desenhos/48.000
vértices, agora com agrupamento e controle de aliases, levou cerca de
1,1 ms por apresentação nesta máquina.

Build final EU/JP/US concluído; testes OFF, ON, RAW, INTERP e POSE v3
aprovados, assim como `git diff --check`. Log em
`build-release/fps60-v3-build.log`. Cópias das quatro capturas com defeito
foram preservadas no workspace da investigação para futura comparação.

O log passa a identificar `[POSE] v3`, com contagens `coherent` e
`coherence_conflicts`. A seleção e o atalho permanecem os mesmos. O modo
original de 30 FPS continua fora desse caminho. A correção visual desta
revisão ainda precisa ser conferida na partida.

## Revisão de 18/10/2026: estratégia e interpolação por desenho

Objetivo: 60 imagens diferentes por segundo, mantendo o gameplay original
a 30 atualizações por segundo. A mudança é experimental e selecionável;
o perfil padrão continua em 30 FPS.

### Referência estudada

Foi lido o código de `aikatore/recvx-enhanced-port` na revisão
`8138b468f38a67ea5a40556a030cf20e4d6462e4`:

- [Arquitetura](https://github.com/aikatore/recvx-enhanced-port/blob/8138b468f38a67ea5a40556a030cf20e4d6462e4/docs/ARCHITECTURE.md#enhanced-mode-in-between-frames).
- [Apresentação](https://github.com/aikatore/recvx-enhanced-port/blob/8138b468f38a67ea5a40556a030cf20e4d6462e4/pc/src/host/render.c).
- [Correspondência e replay](https://github.com/aikatore/recvx-enhanced-port/blob/8138b468f38a67ea5a40556a030cf20e4d6462e4/pc/src/gs/stream.c).

O port PS2 conserva a simulação a 30 Hz e reapresenta a geometria com posições
intermediárias. Identifica modelos/faixas de vértices com tags do motor,
detecta cortes de câmera e usa alvos separados para replay. Usa ponto médio
e pose final para o caso de saída a 60 Hz. Código de referência estudado
como orientação; esta implementação é própria para o caminho RSX/D3D12.

No nosso projeto PS3 já existe um caminho que desenha a 60 Hz e limita o
callback Biohazard a 30 Hz. Não é necessário executar a lógica duas vezes
mais rápido. Também não é possível transplantar diretamente o replay GS
do outro port: temos comandos RSX, shaders e alvos persistentes diferentes,
sem as tags de modelos do port PS2.

### Falha observada no protótipo anterior

O log JP `run-log-fps60-interpolado.txt` registrou `eligible=4942`, `1810` e
`4307`, sempre com `apply=0 reason=4`. A exigência de listas idênticas
descartava a interpolação inteira quando mudava qualquer desenho. Endereços
de texturas/alvos e relações de slots também participavam da identidade,
embora esses endereços possam alternar entre bancos.

### Implementado nesta revisão

1. Históricos individuais com a pose atual e a anterior de cada desenho.
   Inserção, remoção, culling ou reordenação não reiniciam os demais.
2. Identidade por shader, forma do passe, material e UVs ordenadas. Não usa
   endereços rotativos de texturas/alvos. Um pareamento espacial próximo,
   exclusivo e sem empate precisa confirmar a identidade; sem segurança,
   mantém a pose original. A busca tem um limite fixo por desenho.
3. Ponto médio na chegada de uma nova pose e pose atual após cerca de
   16,7 ms. Cada objeto tem seu próprio relógio: alterações em outros passes
   não reiniciam a fase da animação. Há cerca de meio tick de atraso visual
   no regime estável de 30 para 60 Hz.
4. Descartes por mudança de cena, corte de orientação/projeção/translação,
   teleporte, relógio inválido ou pausa longa. Limiar de translação considera
   também c7, onde a captura real contém a translação da projeção.
5. Resultados são reunidos primeiro na CPU. Se dois passes exigirem valores
   diferentes no mesmo slot compartilhado, aquele slot conserva o original.
   Um passe não reconhecido também protege os slots que compartilha.
6. A opção **60 FPS (Experimental)** do launcher usa lógica a 30 Hz e
   interpolação. O benchmark acelerado RAW fica somente no atalho próprio.
7. `executar-code-veronica-60fps-interpolado.cmd` chama o iniciador otimizado
   e desliga PERF, TIMELINE e FRAME_BUDGET para não medir fluidez com probes
   que produzem trancos.

Os dados são cópias CPU dos vértices/constantes antes do upload. Nenhum
histórico é obtido lendo a memória de upload da GPU. O modo requer cerca
de 105 MiB adicionais de RAM nos limites atuais; aloca-os somente quando
ativado. Falha de alocação conserva o desenho original.

### Validação e limites

Testes CPU cobrem cadência de 60 amostras com movimento a 30 Hz, históricos
independentes, listas parciais/reordenadas, bancos relocados, instâncias
repetidas, empates, aliases, orçamento de memória/busca, cortes, teleportes,
pausas e dados inválidos. Uma matriz real da captura F12 frame_92 serve de
fixture de layout. O teste de pacing também tem o modo `interp`, validando
a publicação da cena sem mudar a frequência lógica.

O log `[POSE] v2` informa `matched`, `smooth`, vértices/câmeras efetivamente
alterados, conflitos, pares ambíguos e `subframes`. Esses números distinguem
interpolação aplicada de apenas um contador em 60 FPS.

Build final EU/JP/US concluído pelo iniciador oficial, com os executáveis
atualizados. Regressões OFF, ON, RAW, INTERP e POSE v2 passaram. Carga CPU
sintética: 4.000 desenhos / 48.000 vértices, 0,57 ms por apresentação nesta
máquina; essa medida cobre o histórico, não o custo total do desenho D3D12.
`git diff --check` passou. Log: `build-release/fps60-v2-build.log`.
Reprodução: `CV_FPS_TESTS=1 CV_POSE_TESTS=1` no ambiente do build-release.cmd;
o caso adicional usa `build-release/bin/code_veronica_fps_tests.exe interp`.

Validação visual em gameplay ainda pendente. Esta revisão não garante 60
FPS em salas que excedam o orçamento de CPU/GPU. HUD, filmes, transparências
e shaders não reconhecidos conservam o comportamento original; normais e
iluminação não são interpoladas. O caminho de vértices na GPU é suspenso
nesse modo porque o protótipo precisa das posições convertidas na CPU.

Próxima etapa após avaliação em partida: capturar os pares que forem
descartados, identificar modelos no motor e só então ampliar a cobertura
para efeitos, passes dependentes e vértices na GPU. Um replay independente
do fluxo RSX deve tratar texturas/alvos persistentes sem aplicar efeitos
duas vezes; fica fora desta primeira revisão.

Para experimentar, fechar o jogo e usar o atalho interpolado, ou selecionar
60 FPS (Experimental) no launcher. Comparar caminhada, giro, inimigos e
cortes de câmera com 30 FPS; comparar também 30 segundos reais com o tempo
do SRT, descontando pausas. Para voltar, escolher 30 FPS ou abrir o atalho
habitual. O atalho de interpolação não grava a configuração do launcher.

O restante deste documento registra as tentativas anteriores.

## Resultado inicial e nova tentativa

VBLANK_HZ=120 não desbloqueou o jogo. O log confirmou a ativação e o usuário
confirmou aproximadamente 30 FPS. Essa tentativa foi substituída.

O novo teste mantém VBLANK_HZ=60. Intercepta a rotina interna de pacing:
EU 0x0050E538, JP 0x0050CE68, US 0x0050C578. A sequência dos 49 acessos
diretos aos campos do objeto é idêntica nas três versões.

A inicialização japonesa em 0x0002676C grava 30.0 no campo +0x60. O wrapper
mantém essa base lógica, muda temporariamente +0x64 (limite variável) para 60
e +0x68 (modo) para zero. O caminho original de passo variável espera pelo
relógio real e calcula o delta: alvo de 30/60 = 0.5 ticks por quadro. O modo
fixo espera um tick inteiro e, com base 30, continua limitado a 30 quadros.
O wrapper não força o delta de saída (+0x90) nem altera o relógio do áudio.

Aplicação restrita a base 30, limite entre 30 e 3000 e modos 0/1. Limite e modo
são restaurados após a chamada, preservando valores diferentes que a própria
rotina original escrever. Desativado, encaminha diretamente ao corpo original.

Isso ainda NÃO comprova que todas as rotinas de gameplay usam o delta.
60 FPS e velocidade correta na partida continuam pendentes de validação.

## Testar e voltar

Abra executar-code-veronica-60fps-experimental.cmd após fechar a execução
anterior. Carregue um save e confira FPS, caminhada, animações, inimigos e áudio.
Compare 30 segundos reais com o SRT sem pausar. O launcher ainda mostra o
limite original; use o contador de FPS dentro do jogo para avaliar o resultado.

Log separado: run-log-fps60-experimental.txt, com três execuções anteriores.
[FPS60] mostra base, limite, modo original e override. A cada cinco segundos,
registra chamadas por segundo real e ticks por segundo real. Cerca de
60 chamadas/s com 30 ticks/s seria compatível com o objetivo. Se override=0,
verificar campos. Se override=1 e continuar em 30, procurar outro bloqueio.
Se acelerar, não compensar apenas o cronômetro do SRT: separar atualização e
desenho ou corrigir as rotinas que dependem da contagem de quadros.

Para voltar: fechar e abrir o atalho habitual. A opção CV_FPS_TEST_60=1 só
existe no processo do atalho experimental; não é gravada no INI. Caminhos
normais mantêm a rotina original e restauram vblank=60. FXC preservado;
o experimento usa o atalho DXC de desempenho habitual do usuário.

## Validação técnica

Build oficial EU/JP/US concluído. Passagem final atualizou os três executáveis
com o último ajuste. Testes OFF e ON passaram: encaminhamento original,
restauração de modo/limite, preservação do delta e dos registradores de retorno,
configurações fora do conjunto e alterações feitas pela função original.
Simulação dos atalhos passou para FXC normal, DXC normal e experimental.

Logs em build-release/fps60-initial-build.log e fps60-final-build.log.
Testes reproduzíveis: CV_FPS_TESTS=1 pelo build-release.cmd.
Nenhuma partida foi controlada automaticamente. Sem commit/push. Sem alteração
no EBOOT ou fontes levantados. Água continua pendente, separada deste teste.

## Resultado em partida e terceira tentativa: lógica separada

Usuário confirmou que a segunda tentativa chega a 60 FPS, com quedas nas
salas pesadas, mas acelera o jogo. Log JP: cerca de 38–59 chamadas/s, mantendo
30 ticks/s no delta do motor. Portanto o pacing já está correto; a lógica
original do Biohazard ainda avança a cada chamada e não usa esse delta todo.

Terceira tentativa: interceptar a chamada virtual do update Biohazard inteiro,
incluindo preparação de listas/objetos, em vez de pular só tarefas depois de
limpar os dados. Método EU 0x00210070 / JP 0x0020E9C0 / US 0x0020E0D0:
as três funções têm os mesmos três passos: preparação 0x155D0, setter,
dispatcher já usado pelas ferramentas de teste. Só são referidas pela tabela
de funções, sem chamadas diretas nos fontes levantados. Registrar o wrapper
após ppu_recomp_register permite usar o corpo original sem relift/recompilar
as unidades gigantes. Registro apenas no Windows e somente com opção ativa.

cv_fps_clock.h acumula os ticks do motor e executa a lógica quando soma 1 tick
(30 Hz), em vez de alternar quadros cegamente. Simulações a 30,35,40,45,60 FPS
produzem cerca de 30 updates/s. Recuperação limitada a 4 updates após atraso;
argumentos originais restaurados entre chamadas múltiplas. Quando não há
update, contexto de entrada preservado. Timing publicado atomicamente para
permitir update em worker diferente do thread do pacing. Log [FPS60-LOGIC]
registra frequência das chamadas virtuais e das atualizações executadas.

Isto é uma separação EXPERIMENTAL, ainda não validada em gameplay: o desenho
continua pelo motor externo, mas é necessário conferir se as listas persistem
corretamente e não causam cintilação ou objetos ausentes. Não há interpolação:
mesmo com 60 desenhos/s, poses e movimento podem continuar mudando a 30 Hz.
Não apresentar como 60 FPS de animação nativos/suaves sem verificar.

Atalho de teste com lógica separada: executar-code-veronica-60fps-experimental.cmd.
Benchmark acelerado preservado: executar-code-veronica-benchmark60-acelerado.cmd
(CV_FPS_BENCHMARK_RAW=1), log run-log-benchmark60-acelerado.txt.
Modo 30 original: atalhos normais, opção de FPS ausente.

Build EU/JP/US concluído. Testes OFF/ON/RAW aprovados, incluindo callback real
registrado com meio tick, contexto de quadros sem update, múltiplos updates,
worker separado, framerate variável, delta inválido e restauração de opções.
Correção no teste: helper de escrita sintético grava exatamente 4 bytes.
Log: build-release/fps60-logic-final-build.log. Sem controle automático de partida.

### Referência fornecida por Kastiel: XML do God of War III PS4

Arquivo lido apenas como referência, sem modificações:
F:/God.of.War.III.Remastered.PS4-CUSA01623/gow3-windows att/gow3-windows/patches/God_of_War_III_Remastered.xml

Possui patches 120 e 240 FPS. Ambos alteram a constante de taxa em 0x75dbe4
para 120.0 (LE 0000f042) ou 240.0 (00007043) e redirecionam uma leitura do
cálculo de energia dos balanços (LEA em 0x48ff1a) para a constante original
60.0 em 0x6fc0d8. O XML descreve adequação do limite mínimo do delta e
preservação da referência original desse cálculo de gameplay. Isso ilustra
que o limite visual e os multiplicadores da lógica podem precisar de ajustes
separados. Não transportar endereços/instruções x86 desse jogo para o PPU de
Code Veronica. Esta referência não comprova o comportamento do nosso jogo.

## Quarta tentativa: interpolação de geometria, 17/10/2026

A terceira tentativa foi testada pelo usuário: velocidade normal, contador a
60 FPS, mas movimento ainda com aparência de 30 FPS. Isso é esperado quando
a lógica e as poses continuam a 30 Hz; não era ainda animação fluida a 60.

Novo atalho: executar-code-veronica-60fps-interpolado.cmd.
Log: run-log-fps60-interpolado.txt. Ativa CV_RENDER_INTERPOLATION=1 junto de
CV_FPS_TEST_60=1. Os atalhos anteriores limpam a interpolação explicitamente;
benchmark acelerado e modo original permanecem disponíveis, sem gravar INI.
Para reverter, fechar esta execução e abrir o atalho normal de 30 FPS.

### Evidência e implementação

Leitura da partida japonesa e captura F12 frame_90 (frame_count=8033,
4806 desenhos) mostraram que os shaders de geometria mais comuns recebem
posições já transformadas na entrada de vértice. Declarar textura de vértice
não significa utilizá-la: os shaders analisados não faziam Load/Sample dela.
As primeiras oito constantes VP contêm orientação e projeção da câmera.

O protótipo guarda cópias CPU das posições finais xyz e dessas constantes
antes de escrever nos buffers de upload D3D12. Não lê o heap de upload para
obter histórico e não altera memória do jogo. Após sincronizar o banco de
renderização, escreve somente posições/câmera intermediárias no desenho.
Lógica, colisões e saves continuam com os valores originais a 30 Hz.

A correspondência exige ordem, geometria, material/texturas, UV e relações de
compartilhamento compatíveis. Relocação uniforme dos bancos é permitida.
Mudança de sala/personagem, layout, dados inválidos, pausas longas ou cortes
estimados de câmera descartam o histórico. A identificação é conservadora:
efeitos ou mudanças de listas podem impedir interpolação naquele quadro.
Os cortes são heurísticos; ainda é necessário conferir o resultado no jogo.

Quando chega uma pose nova, o desenho começa na pose anterior. Nos desenhos
repetidos, avança até a nova posição usando tempo real. Assim há um atraso
visual aproximado de um tick nativo (33 ms). A fase depende das poses que
realmente chegaram ao renderizador, evitando misturar publicação de lógica
nova com geometria antiga em outro thread.

Caminhos transparentes, HUD, shaders que leem texturas de vértices e formatos
não reconhecidos ficam fora deste primeiro teste. Normais e constantes de
material/iluminação não são interpoladas; observar possíveis diferenças de
iluminação, sombras e efeitos ao movimentar a câmera. Não é frame generation
por imagem e não representa lógica nativa a 60 Hz. Memória CPU adicional de
aproximadamente 30 MB é alocada somente no modo opt-in.

### Validação e próximo teste

Build oficial EU/JP/US concluído. Testes OFF/ON/RAW passaram. Novo teste CPU
passou para ponto médio, extremos, preservação dos dados originais, bancos
relocados, mudança de cena/layout/alias, cortes, teleporte, dados inválidos,
pausas e relógio retrocedendo. git diff --check sem erros.
Reproduzir com CV_FPS_TESTS=1 e CV_POSE_TESTS=1 no build-release.cmd.
Log de compilação: build-release/fps60-pose-build.log.

PENDENTE: validação visual pelo usuário andando, girando e trocando de câmera.
O log [POSE] registra eligible, apply, vertices, alpha e reason. Conferir se
há poses correspondentes e quadros intermediários de fato; compilar e passar
testes sintéticos não comprova fluidez em gameplay. Comparar com o atalho
experimental anterior. Não houve controle automático da partida, commit ou
push. A investigação da água permanece separada e interrompida a pedido.
