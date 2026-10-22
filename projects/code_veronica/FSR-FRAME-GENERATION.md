# Disponibilidade atual — launcher normal EU / US / JP (v48)

FSR 3.1.6 e a correção de overlay do histórico abaixo estão disponíveis nos três
executáveis principais, por opção no launcher normal. Padrão inicial: 30 FPS e
FSR OFF; FSR só é aplicado no modo 60 FPS. Preferências salvas são mantidas.
Runtime AMD oficial e LICENSE.md estão versionados com os headers, permitindo
que CMake copie ambos ao lado do executável sem depender do ambiente de teste JP.

O usuário confirmou melhora visual, mas ainda há bugs. Limitações de vetores
estimados por imagem, dados de câmera aproximados e proteção conservadora
permanecem. A implementação foi promovida para EU/US, sem alegar que receberam
os mesmos testes de gameplay extensos da japonesa. As seções v1–v4 abaixo são
históricas; suas menções a entrega somente JP/overlay antigo descrevem aquela
etapa, não a disponibilidade atual.

---

# v4 — overlay preservado em quadros gerados (v46 JP)

A proteção v2 usava HUDlessColor como referência. Em pixels estáticos isso
apagava o overlay já extraído/combinado pelo SDK no output intermediário,
causando a piscada relatada. Agora uma cópia GPU do quadro real após overlays
é comparada à cópia anterior aos overlays. Pixels alterados exclusivamente
pelo painel/SRT/avisos são preservados exatamente antes de aplicar a proteção.
Inclui bordas/transparências já compostas; a proteção de cena/texto permanece.
Uma cópia GPU adicional por real frame somente com FSR pronto, sem CPU readback.
Teste SDK 1080p confirma painel com cores variando em três quadros gerados,
pixels de cena/textos preservados, contagem 36+35=71 e zero erros D3D12.
Captura F9 de corrida independente do FSR descrita em MOTION-CAPTURE.md.

---

# v3 — overlay FPS real / total (v45 JP)

Usuário confirmou que a correção v2 resolveu os mosaicos percebidos.
Com o swapchain FSR ativo, o painel agora mostra `FPS 60 / 120`:
à esquerda, os Presents reais do jogo; à direita, os Presents reais + gerados
medidos no mesmo intervalo. `ms` continua sendo o tempo do quadro real.
FSR desligado ou falho mantém o formato anterior, inclusive no modo 30 FPS.

O total usa `IDXGISwapChain::GetLastPresentCount`, que a implementação oficial
AMD encaminha ao swapchain DXGI real, incluindo suas apresentações geradas.
Não calcula FPS*2 nem usa apenas jobs solicitados. Nos menus/pausas de geração,
o número total pode se aproximar do real. Sem contador válido, mostra `/ --`.
Deltas unsigned tratam wrap de 32 bits; descontinuidade descarta uma amostra.
É taxa de apresentações, não garantia de scanout físico do monitor (refresh,
VSync e compositor ainda podem limitar os quadros efetivamente vistos).
A consulta ocorre somente na atualização do painel, sem readback ou fence extra.

Validação: teste real SDK em 1080p, 36 quadros reais + 35 gerados = 71 Presents;
verificação por quadro inclusive com geração pausada, troca de cena e resize.
Sem erros D3D12; testes nativos 30/RAW continuam sem carregar AMD DLL.
QA do painel nos layouts coluna/linha: 60/120, 60/90, 60/60, 30 nativo,
contador indisponível, wrap e reset; imagens conferidas sem sobreposição.

Logs do último gameplay antes desta alteração: janelas de 250/250,
300/300 e 301/301 real_frames/generated_jobs confirmam dispatch do FSR.
Esses logs antigos contam jobs, não scanout nem FPS total medido.
Somente o executável japonês foi reconstruído nesta rodada.

---

# v2 — proteção contra mosaicos/quadros incorretos

Relato de gameplay: blocos em grades, textos japoneses e corpo da Claire;
F12 comum sem o defeito. Logs confirmam jobs gerados; frame_93/backbuffer.bmp
mostra o quadro real sem o mosaico. A causa exata de todos os artefatos ainda
não foi provada, mas o caminho v1 tinha vetores inteiros em grade reduzida,
sem coerência espacial, e profundidade reduzida/point sampling.

Mudanças somente no caminho FSR:

- Profundidade e vetores entregues ao SDK em resolução de exibição completa.
  Luma de busca continua reduzida, agora com filtro de área 4x4.
- Refinamento subpixel com condicionamento, erro/confiança e rejeição de
  matches fracos; reconstrução de vetores com pesos de luma, distância e
  profundidade. Não mistura velocidades entre primeiro plano e fundo.
- Após o SDK gerar a imagem, uma proteção conservadora mantém pixels reais
  quando a área está estática, há valor inválido, a cor extrapola as duas
  imagens de referência ou há um traço claro/verde de alto contraste.
  A cópia vem do quadro real completo, não de uma miniatura interpolada.
  É um compromisso explícito: regiões incertas podem repetir o pixel real
  em vez de suavizar a 2x; não afirmar melhoria perfeita em todo movimento.
- F12 mantém a captura real e enfileira três capturas dos próximos jobs SDK.
  `fsr-captures/session_<pid>_<tick>/frame_N` guarda real.bmp,
  generated-raw.bmp (antes da proteção), generated-filtered.bmp,
  motion.bin (RG16_FLOAT), depth.bin (R32_FLOAT) e metadata.json.
  Quadros gerados são capturados antes da composição de SRT/performance.
  Sem pedido de captura não há readback/mapeamento desses recursos.
- A comparação binária em 1080p verifica três saídas reais do SDK: pixels
  estáticos e traços brancos/verdes recém-adicionados preservados exatamente.
  Teste também mantém geração, resize, reset, retomada e validação D3D12.

Mantidos: SDK oficial 3.1.6, seletor default-off, gates native30/RAW, regiões
EU/US, interpolação CPU/GPU v42 e relógio do jogo. O FSR ainda utiliza vetores
estimados por imagem e metadados aproximados de câmera; ver limites abaixo.
Qualidade da personagem em movimento e custo GPU adicional precisam de
novo teste no jogo. Evitar medir FPS durante os quadros de captura.

---

# FSR Frame Generation 3.1.6 — experimental, JP/DXC

## Estado da entrega

Integração real da DLL oficial AMD assinada. Provider analítico 3.1.6
selecionado explicitamente por query/override, com swapchain API 3.1.7.
Não é uma simulação de FPS, simples duplicação de frames ou troca da
interpolação geométrica v42 por interpolação de imagem caseira. O SDK AMD
produz o quadro gerado e faz apresentação/cadência por seu swapchain.

Somente o executável japonês foi atualizado nesta rodada. O launcher JP
normal é aberto por `executar-code-veronica-japones.cmd`. Selecionar a
versão japonesa, 60 FPS, e Geração de quadros > FSR 3.1 (Experimental).
Desligado por padrão; mudar com jogo fechado, iniciar novamente. CPU/GPU
da interpolação de malhas continuam selecionáveis separadamente.

30 FPS: mesmo com preferência FSR salva, o launcher limpa a variável de
ativação. O runtime exige CV_FSR_FRAME_GENERATION=1, CV_FPS_TEST_60=1,
CV_RENDER_INTERPOLATION=1 e RAW desligado. Sem isso não carrega a DLL,
cria contexto, aloca texturas nem despacha geração. Não se alterou o
relógio ou os patches de lógica do jogo.

## Integração no renderizador

- Wrapper C++ isolado `libs/video/rsx_fsr31.cpp`, interface C no backend.
- SDK cria swapchain para o HWND real; CreateContext FG usa 3.1.6 exato.
- Profundidade R24 real do DSV compartilhado é copiada/convertida na GPU
  após o último draw 3D elegível de resolução completa, antes de possíveis
  sobrescritas pelos passes seguintes. Barreiras devolvem DEPTH_WRITE.
  A fronteira participa do limite do batch de sombras; cache de PSO e
  tabelas de descritores são restabelecidos antes de desenhar novamente.
- O jogo não expõe velocity attachment. Nesta implementação os vetores
  current->previous são estimados na GPU a partir das imagens finais:
  luma, busca de patch limitada e rejeição de correspondência fraca.
  Não são vetores geométricos completos nem vetores de câmera nativos.
  São fornecidos como entrada ao SDK, além do optical flow próprio dele.
- Motion/depth/luma são entradas auxiliares de 1/4 da dimensão de saída.
  A cor/saída do jogo conserva a resolução selecionada; não se ativou
  upscaling temporal nem se reduziu a resolução de renderização do jogo.
- O camera metadata do provider analítico usa near=1,far=100000,FOV=60°
  e eixos neutros: o RSX expõe matrizes combinadas arbitrárias, não uma
  câmera world-space padrão. A profundidade é real; esses metadados NÃO
  foram recuperados de todas as câmeras do jogo. Esta aproximação e o
  movimento estimado de imagem precisam ser avaliados visualmente.
- HUDLessColor é a cópia após AA, cores, escala e RCAS e ANTES dos overlays
  SRT/performance do port, permitindo a extração de UI do SDK. Elementos
  já compostos pelo jogo continuam nessa imagem e exigem teste visual.
  Se o menu nativo Recomp estiver visível, a geração pausa por segurança.
- Menus/frames sem profundidade de cena elegível e F12 usam apresentação
  normal. Ao retornar, há reset. Capturas são de quadros reais: não
  deduzir qualidade de FG só de um F12.
- Scene/layout/camera cut, grande salto de tempo, resize e retomada
  invalidam histórico. A cor inicial é real, não histórico não inicializado.
- Antes de reutilizar as entradas, espera as apresentações pendentes do
  SDK, além da cerca já existente do renderizador. Caminho síncrono do
  SDK, sem async compute explícito. Pode haver custo relevante; ainda
  não houve medição de ganho no jogo.
- Erros de DLL/SM6.2/contexto/shaders/preparação/despacho causam retorno
  ao desenho normal, com diagnóstico. Biblioteca permanece residente
  para não invalidar o vtable COM do swapchain no Release final.
- Os CMDs antigos CPU/GPU de diagnóstico deixam FSR desativado mesmo
  se salvo ON. Assim os benchmarks v42 continuam comparáveis. Não usar
  esses atalhos para julgar a ativação deste recurso; usar launcher JP
  normal. CV_FSR_DIAGNOSTIC=1 é a autorização explícita para novo probe FG.

## Distribuição e procedência

Headers e DLL de GPUOpen-LibrariesAndSDKs/FidelityFX-SDK, revisão
60f4ea81909200d8542eca14dccb2628b763a9a3, preservados em
third_party/amd_fsr31. Hash e referência em README.md, licença integral em
LICENSE.md. Assinatura Authenticode AMD verificada válida no download.
CMake copia a DLL e AMD-FSR-LICENSE.md ao lado do EXE; sem DLL o jogo
continua com apresentação normal. DLL não foi alterada ou desmontada.

## Validação

`code_veronica_fsr31_tests`: executa SDK real na GPU física com janela
própria oculta, cena móvel, profundidade, UI, pausa/retomada, troca de
scene, reset, redimensionamento de swapchain/contexto e shutdown COM.
36 frames de teste resultam em 35 jobs de geração; camada de validação
D3D12 sem erros. Testa também que native30/RAW bloqueiam o recurso e que
native30 não carrega o módulo AMD. Modo --1080 exercita 1920x1080 e resize.
Modo --missing, em pasta sem a DLL AMD, verifica fallback para swapchain
DXGI normal.
Não controla a janela do jogo nem usa dados/saves do usuário.

Testes CPU de interpolação existentes passaram (10.000 quadros), sem
alterar o caminho v42. Compilação JP/DXC e teste integrado disponíveis.
Ainda pendentes: avaliação visual e desempenho em gameplay real, demais
GPUs e versões EU/US. Não afirmar 2x efetivo nem qualidade sem artefatos.

`[FSR-FG]` registra versão/ativação e real_frames/generated_jobs/resets.
Generated_jobs mede trabalhos despachados, não refresh efetivo do monitor.
O overlay FPS existente permanece contando quadros reais do renderizador;
não confundir sua leitura com a taxa total apresentada pelo SDK.

## 21/10/2026 — FSR com o replay de 60 FPS (correção de integração)

Relato: 60 FPS com interpolação/replay foi aprovado pelo usuário, mas ligar FSR caiu para cerca de 23 FPS reais / 47 apresentações totais (42,7 ms). Não reduzir qualidade nem desfazer a otimização de 60 FPS.

O log anterior registra `[POSE-REPLAY] v1 off` com FSR ativo. O gate do FSR desabilitava o replay e, por consequência, o produtor da pose e o passo nativo de um desenho por atualização. Isso voltava ao caminho mais caro de renderização do jogo. Agora, por padrão, ligar FSR conserva o replay: jogo a 30 atualizações/desenhos por segundo, duas apresentações reais do renderer com pose interpolada/registrada, e geração AMD sobre essas apresentações. O modo sem FSR conserva a mesma decisão e o mesmo cálculo de pose de antes. Nenhuma mudança nos vértices, costuras, triple buffer, relógio, qualidade ou resolução.

`CV_FSR_GUEST=replay` explicita o novo padrão; `legacy` seleciona o caminho antigo para comparação; `30` conserva a alternativa de apenas uma apresentação real por atualização. `CV_POSE_REPLAY=0` continua disponível. A decisão ainda exige os três conjuntos de upload, como antes.

Também foi preservada e compilada a alteração já presente em `rsx_fsr31.cpp`: sem a espera CPU por todas as apresentações do SDK a cada `begin`. `allowAsyncWorkloads=false` mantém a geração na fila do jogo; captura e recriação de recursos continuam esperando. `CV_FSR_WAIT_PRESENTS=1` restaura a espera antiga. Não ativar esse override para medir a correção.

Marcadores: `[POSE-REPLAY] v2 on ... FSR=1` e `[FSR-FG] per-frame wait ... off`. Contagens de jobs não equivalem a FPS físico; o overlay continua usando o contador de Presents.

Validação: compilação dos três executáveis normais e revisão da alteração limitada ao gate FSR. Sem testes isolados nem controle do jogo, conforme preferência do usuário. Desempenho e qualidade em gameplay ainda dependem da comparação do usuário na mesma sala. Não prometer 120 FPS: GPU, SDK e monitor podem limitar a apresentação. Logs em `work/fsr-replay-fix/build-{jp,us,eu}.log`. Backup em `appearance-backups/pre-fsr-replay-20261021/`. Sem commit/push.


### Medição na sala pesada, mesma sessão (21/10/2026, 23:30)

JP, 1080p, sala do primeiro save (mesa da oficina), câmera parada. Build com a correção acima
(`[POSE-REPLAY] v2 on`, sem espera por apresentações). Cópia de diagnóstico com `CV_FRAME_PROFILE=1`,
`CV_POSE_PROFILE=1` e `CV_FRAME_DETAIL=1` (tempo de GPU por categoria e por alvo). Valores por
apresentação real, janelas de 5 s.

| | FG ligado | FG desligado |
|---|---|---|
| intervalo do quadro real | 16,7–18,0 ms (~56–60) | 16,7 ms (60) |
| lógica do jogo (tiques/s) | 27,1–29,8 | 29,9–30,0 |
| GPU nas nossas listas (`[FRAME-GPU] lists`) | **12,7–13,2 ms** | 6,2–6,5 ms |
| — `other_offscreen` | 9,6–10,6 ms | 5,2–5,4 ms |
| — `post_display` (FSR: luma, busca, vetores, cópias) | 1,6–2,0 ms | 0,37–0,43 ms |
| — sombras + alvo principal | ~0,35 ms | ~0,28 ms |
| alvo 1920x1080 em 0x01C30000 | 3,0 ms | 1,2 ms |
| alvo 1920x1080 em 0x01528000 | 3,0 ms | 1,6 ms |
| thread de render: espera da GPU no início | 4,2–5,2 ms | 4,1–4,4 ms |
| thread de render: build (prepass) | 6,9–7,6 ms (2,4–2,6) | 7,7–7,9 ms (2,7) |
| pose no produtor (por lote) | 10,7 ms | 11,0–11,2 ms |

Leitura:
- Sem FG a sala tem folga: GPU ~6,3 ms em 16,7 ms; thread de render ~12,6 ms. Limite = os 60 FPS.
- Com FG os **mesmos** desenhos levam cerca do dobro do tempo de GPU. O trabalho do SDK (geração,
  cópias e apresentação do swapchain) roda ao mesmo tempo e não aparece nas nossas listas; os
  timestamps medem tempo de parede na GPU, então a disputa infla todos os alvos. Os nossos passes
  de FSR somam ~1,2–1,6 ms a mais. Com FG a GPU vira o gargalo (~13 ms + SDK por quadro real), e o
  jogo às vezes cai abaixo de 30 tiques/s. No modo replay, o tempo do jogo anda mais devagar.
- Com o replay, o FSR recebe 60 quadros reais e gera outros 60 (alvo de 120). Num monitor de 60 Hz,
  metade disso não chega à tela. `CV_FSR_GUEST=30` (uma apresentação real por tique, o FSR dobra para
  60) cortaria cerca de metade da GPU por segundo; ainda não foi medido nem avaliado visualmente.

Logs: scratchpad da sessão (`runlog-B-fg.txt`, `runlog-C-sem-fg.txt`). Save JP conferido por hash.

### O que são os alvos 1080p fora da tela (captura `rsxcap/frame_90` antiga da mesma sala, depois sobrescrita por um F12 em 21/10 23:39)

São a renderização do próprio jogo em três passadas sobre a mesma geometria, com a profundidade
compartilhada em 0x026E0000 (10.514 desenhos no quadro):

| alvo | desenhos | o que faz | GPU sem FG | GPU com FG |
|---|---|---|---|---|
| 0x011A4000 | 2.331 | pré-passada só de profundidade (cmask=0) | 0,36–0,44 ms | 0,69 ms |
| 0x01C30000 | 4.598 (≈2×) | máscara de um canal com blend; lê os mapas de sombra 1536x1536 | 1,16–1,19 ms | 3,0 ms |
| 0x01528000 | 2.400 | cena final em cor completa, lê a máscara | 1,55–1,71 ms | 3,0 ms |

Nenhum dos shaders dominantes usa `discard`, então o early-Z funciona sobre a pré-passada. O backend
já agrupa estado: ~116 trocas de PSO e 126 barreiras por quadro (`[GPUCMD]`). Custo por desenho sem FG:
~0,16 µs (profundidade), ~0,25 µs (máscara), ~0,67 µs (cor). Não há passada redundante do nosso lado
para cortar sem mudar a imagem. Reduzir a resolução dos mapas de sombra ou da cena mudaria a imagem.

Com o FG, **todos** os alvos ficam ~2x mais lentos, inclusive os pequenos (384x384: 0,17 → 0,36 ms).
Uma desaceleração uniforme indica trabalho do FSR rodando ao mesmo tempo na GPU (geração, cópias e
apresentação do swapchain AMD), e não um passe específico do jogo. Pela diferença, o FG custa algo
como ~6 ms de GPU por quadro real. Para ganhar FPS real com o FG, o alvo é esse custo (o SDK e os
nossos passes de vetores e proteção), não os três alvos do jogo. Falta medir diretamente o tempo
de GPU da lista de geração.

### Geração em async compute (22/10/2026)

Medição na GPU por passe, com `CV_FRAME_PROFILE=1` (`[FSR-GPU]`, timestamps lidos 3 quadros depois).
JP, cena 0x80030C00, mesmo ponto, FG ligado. Valores por quadro real:

| | fila do jogo (antes) | async compute |
|---|---|---|
| conversão de profundidade | 0,07 ms | 0,03 ms |
| nossos vetores + prepare do SDK | 1,35–1,38 ms | 0,70–0,72 ms |
| geração do SDK | 1,8–2,2 ms | 1,35–1,37 ms |
| proteção final | 0,09–0,11 ms | 0,08 ms |
| intervalo do quadro real | 16,8–17,8 ms | 16,65–16,99 ms |
| lógica do jogo | 28–29 tiques/s | 30,0 tiques/s |
| thread de render esperando a GPU | 4–7 ms | ~0 ms |

Ressalva: a carga das listas do jogo variou entre as janelas da referência (2,6–13,4 ms) e ficou
estável na de async (~3,4 ms). O sinal mais confiável é a espera pela GPU, que zerou.

`allowAsyncWorkloads` passou a ser `true` por padrão (`CV_FSR_ASYNC=0` volta à fila do jogo).
Chaves de teste que ficaram, desligadas por padrão: `CV_FSR_GUARD=0` (sem a proteção final, 0,08 ms)
e `CV_FSR_VECTORS=0` (sem os nossos vetores, <0,7 ms). Não compensam o risco para a imagem.
Teste do usuário no atalho normal JP: FPS real bom e nenhum defeito nos quadros gerados.

Ponto em aberto: sem a espera por apresentações, a geração assíncrona do quadro N pode, em teoria,
coincidir com a escrita das entradas do quadro N+1 (hudless, profundidade, vetores e cópia final).
A geração leva ~1,4 ms, e essas entradas só são reescritas no fim do quadro seguinte. Se surgir um
defeito que só acontece com FG, comparar com `CV_FSR_ASYNC=0`; a correção seria alternar dois
conjuntos de entradas.
