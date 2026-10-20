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
