# Estado entregue — base estável v25, regiões JP/US/EU (20/10)

Usuário rejeitou a tentativa de sincronização da sombra: não resolveu o piscar.
Restaurada a geometria aprovada v25, com câmera suave e proteções de olhos,
rosto, partes desconectadas, itens 3D e objetos estáticos. Fonte da restauração:
snapshot local work/anchors-v25, anterior aos experimentos v26/normais/câmera
de referência/sincronização de sombra. Não voltar ao commit1f14a37 inteiro:
isso perderia correções posteriores aprovadas pelo usuário.

Retirados os experimentos recentes de normais, iluminação v3, câmera fixa F6
e transferência de pose para o passe de sombra, incluindo seus CMDs de teste.
Os testes antigos/diagnósticos que já existiam no commit base permanecem como
antes. Teclas F6/F7/F8 voltam ao comportamento anterior a esses experimentos.
O piscar de luz/sombra continua como problema conhecido; não declarar resolvido.

Mantidos v22 proteção de detalhes faciais, v23 identidade pelo fragment program,
v24 identidade pelo índice real do vértice, v25 restrições de geometria estática
e equivalência CPU/GPU. Nenhuma união ampla de projeções v26 permanece.
A câmera usa o caminho suave aprovado; não aplicar camera_sources=-2 global.
F9 mantém schema2 com estado congelado dos programas e analisador compatível.

Estas correções pertencem à interpolação opcional60FPS, compartilhada por JP,
US e EU.30FPS nativo continua padrão com o mesmo caminho de renderização,
configurações e temporização; nenhum experimento novo é ativado nesse modo.
Launcher/seleção30/60 já existentes permanecem como antes. Dados/saves não são
alterados. Executáveis regionais serão reconstruídos dos mesmos fontes estáveis.

As seções abaixo são HISTÓRICO de investigação: resultados pendentes ou caminhos
experimentais nelas descritos não representam o estado atual entregue.
Validação concluída: builds JP/US/EU com target MyGameRecomp bem-sucedidos,
executáveis principais atualizados nos caminhos habituais. Suíte CPU v25 passou
10000 quadros; GPU compute passou512 quadros WARP+512 hardware bit a bit com CPU;
consumer DXC e blend WARP32768 resultados passaram; schema F9 v1/v2 passou.
Os quatro fontes centrais restaurados são idênticos ao snapshot aprovado v25
(normalizando somente finais de linha). Nenhum código de launcher/seleção FPS
ou temporização nativa mudou. Padrão fps=0 e desativação da interpolação em30
confirmados no launcher existente. Sem teste manual de jogo nas três regiões
nesta entrega. Piscar de iluminação/sombra continua conhecido.
Commit/push da base estável autorizados explicitamente pelo usuário.

---

# Sombra segue a pose final visível — experimento restrito v1 (20/10)

CV_POSE_SHADOW_SYNC=1, somente interpolação60 no caminho CPU. Alocação ocorre
após os gates existentes de interpolação;30 nativo não aloca nem executa este
passo. Desativado no CMD normal e nas regiões não reconstruídas nesta rodada.
CMD executar-teste-sombra-pose-60fps-jp.cmd:JP/DXC1080p60CPU, câmera suave,
FSR/normais/probes de luz/referência de câmera desligados. Não precisa F6/F8/F10.

Depois da resolução final v25, lê os doadores imutáveis do material77b2c9f8
(FPbff5fa66/7835a34b) e transfere posições somente para o casterf4a0730e
(FP7ea4c4e7), ambos com decodificação de posição verificada (x,-y,-z), c467.x1,y0.
Não altera programa com paleta d81bdfb6, programas desconhecidos, luz, normais,
câmeras, histórico, cadência, matching, grupos ou proteção de olhos/objetos.
Não repetir união geral de projeções v26. Ainda não constitui implementação
geral de interpolação de todos os casters/luzes em todas as salas.

Correspondência EXATA de xyz do mesmo lote congelado. Todos os doadores finais
do ponto devem concordar, incluindo fallbacks atuais v25. Nenhuma pesquisa de
vizinhança. Componentes de destino ligam draws/arestas por pontos exatos. Todos
os pontos do componente devem ter doador seguro; um ponto ausente, ambíguo,
não finito, estacionário incompatível ou com alias desconhecido rejeita TODO
o componente. Upload compartilhado com material nunca é sobrescrito. Falha de
bounds/hash é atômica. A cópia ocorre num único sentido; receita GPU de destino
é invalidada para não substituir o resultado por uma interpolação diferente.

F9 JSON acrescenta shadow_sync,shadow_sync_vertices,shadow_sync_components,
shadow_sync_rejected. Log [POSE-SHADOW] v1 a cada5s registra o presente atual,
não média de FPS. Capturas continuam com as posições finais realmente usadas.

Validação inicial do helper de produção: testes de cobertura completa, ponto
ausente, alias, conflito de doadores, veto estacionário, NaN/Inf, bounds, signed
zero, receita e componentes independentes. Replay525 capturas completas das
16 sessões mais recentes;7 .pose incompletos ignorados.47 quadros alteram
somente posições do passe de sombra. Todos os demais uploads, incluindo modelo
visível/rosto/objetos, ficam byte a byte iguais aos arquivos capturados.
Exemplo frame000 novo F9:11062 slots de sombra alinhados,23 componentes;frame017
10638 slots;frame032 parado zero mudanças. Mediana inicial do helper0,887ms
no replay local (inclui scratch, não é ganho nem benchmark de FPS do jogo).

Compilação JP/DXC concluída: MyGameRecomp_JP.exe atualizado20/10 às15:41:44,
233746944bytes. Targets MyGameRecomp,code_veronica_shadow_sync_tests e
code_veronica_pose_tests concluídos com sucesso. Novo teste de produção rodado
novamente nas525 capturas:47 com mudanças somente de sombra,422932 slots
somados; mediana do helper0,946ms nesta execução, sem medição de FPS no jogo.
Suíte v25 passou, incluindo10000 quadros de estabilidade, proteção dos olhos,
objetos, aliases, componentes e correspondências. git diff --check sem erros.
Sucesso da compilação/replay não prova melhora VISUAL: falta teste no jogo.
Nada foi promovido ao CMD normal. Sem commit/push.

---

# Vídeo da parede esquerda — câmera isolada não resolveu (20/10)

Usuário confirmou ausência de melhora no teste F6. Manter a câmera suave
como objetivo; referência CURRENT é diagnóstico, não solução permanente.
Não promover a referência nem o teste de normais para o lançamento normal.

Vídeo fornecido 2026-10-20 15-20-48.mp4: decodificado localmente com PyAV19,
1920x1080,60fps,247 quadros,4,1167s. Inspeção dos quadros130/131 (2,1667 e
2,1833s) confirma grande alternância escuro/claro na parede esquerda com o
personagem correndo perto dela. Não é apenas falta de suavidade da câmera.
Vídeo e F9 são gravações distintas; não atribuir diretamente um quadro do
vídeo a um .pose sem sincronização por relógio.

F9 session_27484_90921031:36 JSON/BMP/.pose completos; FSRoff, normais teste
off, probe de luz off. F6 CURRENT nas capturas0..26, SMOOTH27..35. Comparação
cam/sent: TODAS as câmeras capturadas em0..26 iguais às originais; apenas
frame032 em SMOOTH tem4504 draws com matrizes alteradas. Logo, a divergência
de câmera isolada não explica o defeito capturado em CURRENT.

Nova evidência para investigar: comparar os pontos de entrada xyz EXATAMENTE
iguais entre material VP77b2c9f8 e caster VPf4a0730e, no MESMO .pose. Existem
1052 pontos únicos compartilhados nas amostras verificadas. Frame000:
984 desses pontos têm posições finais diferentes entre os passes, maior
distância1,611126 unidades do jogo; shadow permanece atual enquanto material
interpola. Frame017:945 divergentes, máximo1,958482. Frame032 (personagem
parado):zero divergências. São diferenças de geometria/tempo, independentes
do teste de câmera. Não usar magnitude de deslocamento como prova de explosão
de vértices; aqui a métrica é diferença entre cópias exatas de um mesmo ponto
de entrada em passes distintos.

Os dois VPs capturados decodificam entrada como (x,-y,-z) antes de c0..c2;
isso permite investigar a coerência das poses, mas igualdade de xyz sozinha
ainda não comprova identidade de objeto nem a causa de cada pixel piscando.
O alvo seguinte é a relação entre pose visível e geometria que produz sombra,
incluindo os casters com paleta ainda não aceitos. NÃO repetir a união geral
de projeções v26 que causou regressões de olhos/objetos. Eventual correção deve
preservar as posições visíveis já aprovadas e exigir correspondência segura
e cobertura de componentes completos antes de alterar um caster.

Nesta rodada somente investigação/documentação: nenhum código de renderização,
executável ou configuração padrão alterado; nenhuma correção visual anunciada.
Scripts/resultados locais: work/wall-shadow/video_check.py, f9_check.py,
pass_check.py,cross_pass_check.py; work/wall-video/capture.json,passes.json,
cross-pass.json,sheet.jpg e frames decodificados. Sem commit/push.

---

# Parede em outra sala — referência de câmera, normais sem benefício

Usuário confirmou: teste de normais não adiantou. Não promover ao CMD normal.
Novo F9 session_10516_90227390:28 JSON/imagens e25 .pose completos; normal_active1
mas zero overrides em todos, FSR off. Programas VP diferentes nesta sala não
estão no whitelist de normais; o experimento não atuou aqui. Não concluir que
normais gerais foram totalmente excluídas; benefício no teste anterior negado
pelo usuário. Manter experimento fora do caminho normal.

Divergência nova medida: em6/25 quadros completos, receptor77b2c9f8 usa c0..c2
interpolados, mas casterf4a0730e e bancos de projeção17..20 conservam o estado
atual. Maior diferença por coeficiente da orientação:0,00597736. Frame017:
2.386 receptores usam câmera intermediária, caster original. VP77b2c9f8 usa
posição em vista tanto para iluminação quanto para projetar sombra por17..20.
Isso é incompatibilidade temporal reproduzível, ainda não prova da causa de
todo o piscar: em19/25 quadros as câmeras enviadas já eram atuais.

Teste optativo CV_POSE_CAMERA_REFERENCE=1 no CPU: após a coerência original,
apenas camera_sources recebe -2, mantendo constantes gravadas em TODOS os
passes de câmera. Preserva grupos de câmera, grafo de superfície, fontes de
vértices, matching e todas as proteções v25. Não unir pontos/projeções como
na regressão v26. Não modifica sombras, luz, matrizes17..20, normais ou geometria.
F6 alterna câmera ATUAL/SUAVE. Modo ATUAL pode parecer30Hz na câmera, enquanto
personagem/posições continuam com interpolação60; é REFERÊNCIA DIAGNÓSTICA,
não solução final nem promoção para o caminho normal. F9 JSON registra estado;
cam/sent do .pose devem ser iguais sob referência. Compute excluído neste teste.

CMD executar-teste-camera-sombra-60fps-jp.cmd:JP/DXC1080p60CPU, normais off,
probe F8/F10off, FSRoff. IniciaATUAL; correr na parede/F9, esperar a sequência,
F6SUAVE e repetir. Não prometer correção completa: resultado visual pendente.
JP/DXC compilado, diff sem erros de whitespace. Somente backend optativo,
CMD e documentação alterados nesta rodada: headers de matching, grafo e compute
não foram modificados. F6 é consumido antes do hook de menu de itens enquanto
este diagnóstico estiver ativo; lançamento normal mantém a tecla original.
Nenhum teste gráfico foi considerado prova de resolução; comparação visual
ATUAL/SUAVE pendente. Sem commit/push.

---

# Resultado inicial do teste de normais — ainda sem conclusão visual

F9 session_19812_90018687:36 metadados/imagens, normal_test ativo e normal_active1
em todos,29 quadros com overrides, máximo23.689 vértices. Sessão90033343:
17 quadros/metadados, normal_active0 em todos e zero overrides. Log confirma
F7OFF/ON/OFF e atuação real. FSR off nos dois. A segunda sequência terminou
antes das36 amostras; não tratar ausência das restantes como defeito gráfico.

Inspeção de imagens completas, montagens e recortes do tronco: trajetórias,
poses e mudanças de câmera diferentes impedem comparação pixel a pixel.
O contador é global, não prova cobertura de um vértice específico da garganta.
Atuação do teste confirmada, eficácia contra o piscar NÃO confirmada. Ainda
necessária percepção ON/OFF do usuário ou reprodução visual mais completa.
Não promover para CMD normal, não retirar salvaguardas para forçar cobertura.
Nenhuma alteração adicional de renderização nesta análise.

---

# Teste optativo de normais — japonês, v25 mantida

Autorizado testar a hipótese de iluminação com normais sincronizadas. Ativar
somente por CV_POSE_NORMALS=1, em 60 CPU com vertex pulling; GPU compute e
conversão CPU antiga não ativam o teste. Novo CMD
executar-teste-normais-60fps-jp.cmd, 1080p DXC, FSR e probe de luz desligados.
F7 alterna normal sincronizada ON/OFF no mesmo processo. Título/log e F9 JSON
informam estado e normal_overrides. CMD normal permanece sem essa opção.
Nativo30 sem novas alocações, fetches ou alteração de atributos.

Histórico opcional armazena a14.xyz atual/anterior em cada snapshot, usando a
MESMA correspondência, ordem de vértices, flag changed e alpha da posição.
Nunca inclui normais na chave de malha nem altera matching/cadência/câmeras.
Sem histórico, corte, mudança de cena, salto ou ambiguidade conserva normal
atual. Endpoints são interpolados na codificação original; os shaders já
normalizam 2*a14-1 depois da transformação. Valores não finitos, fora de[0,1],
normais quase nulas ou pares opostos conservam original. Nunca altera a14.w.

Aplicação: apenas VPs caa490ea/f830c5da/e985b049, inspecionados e com constantes
c467.z=2,c467.w=-1 confirmadas. Requer que a posição da correspondência do
PRÓPRIO desenho seja exatamente igual à posição final pós-vetos/costuras.
Um doador geométrico não pode transferir normais entre materiais/arestas.
Vetos geométricos conservam normais atuais. Uploads compartilhados por passes
normais desconhecidos ou propostas incompatíveis vetam o override desse upload.
Não modifica grafo de superfícies v25, sombras, olhos ou projeções mundiais.

No vertex pulling, registro separado de16bytes depois do registro de posição
(16/32bytes): xyz e flag, reset em cada upload/paridade. HLSL lê somente com
feature habilitada e flag1; conserva W original. Captura VF reconstrói a14
final, mas .pose v2 ainda não serializa históricos de normais. CPU coleta
atributo14 adicional somente no teste; sem alocação por quadro. Há memória e
custo de fetch/cópia adicionais, medir no jogo; não prometer ganho de FPS.
Se falhar alocação, mantém normais originais e registra. Hipótese sem confirmação
visual: verificar corrida, rosto/olhos/peito/objetos, F7ON versus OFF, F9 nos dois.

Validação inicial CPU passou: correspondência/cadência/xyz idênticos ao baseline,
normal acompanha alpha da pose; fim de interpolação, cena, posições finais
diferentes, NaN, normal nula/oposta preservam original. Regressões v25 continuam
passando. Build JP, compute baseline e consumidor WARP serão registrados abaixo.
Validação final: JP compilado; testes CPU e regressões v25 passaram, assim
como fetch/VF/blend. Compute WARP512 e hardware512 permanecem iguais à CPU.
Consumidor de normal testado em WARP com DXC/FXC para registros de posição
CPU16/GPU32, paridades, flags inativas e a14.w original. Três VPs capturados
com override a14 compilaram em DXC. Codificação c467=1,0,2,-1 confirmada nas
novas capturas para caa490ea e f830c5da.

Replay sequencial adicional de144 amostras antigas: novo código com teste
inativo produziu xyz e estados bit a bit iguais à v25 de referência; deslocamentos
indevidos de pontos estáveis continuam zero. Isso não testa iluminação ou
normais reais das capturas antigas, que não estão serializadas. Cache optativo
separado vfetch-pose-normals1-cpu/blend evita PSOs com stride/layout anterior.
F7OFF mantém coleta/histórico opcional para permitir reativação imediata; não
usar F7OFF como benchmark contra lançamento normal. A eficácia visual no jogo
continua pendente; não afirmar que o piscar foi resolvido.
Sem commit/push; não portar ainda às outras regiões.

---

# Investigação das capturas 5552 — estado aprovado v25 mantido

108 F9 válidos, probe versão3, FSR off. Modos congelados: 25 NORMAL,
48 LUZ TESTADA DESLIGADA, 33 SEM SOMBRA PROJETADA, 2 SEM RESPOSTA ANGULAR.
Sessões: 88597859, 88612578, 88681359. Houve troca de modo dentro das
sequências, mas o JSON registra o modo real dos receptores de cada quadro.
Imagens mostram bloqueio efetivo da contribuição testada com F8, preservando
outros materiais/luzes. Usuário confirmou: o piscar no peito/garganta CONTINUOU
quando apareceu LUZ TESTADA DESLIGADA. Isso exclui essa contribuição como causa
única; não exclui todos os outros caminhos de iluminação nem todos os passes.
Não usar esse resultado para declarar genericamente que nenhuma sombra falha.

Os receptores interpolados nas capturas usam VP caa490ea. Código desse VP
consome a0 para posição e a14 para normal; o prelude interpola somente a0.
RsxPoseVertex salva xyz, identidade UV+índice e stamp, sem histórico de a14.
Fetch seletivo de pose inclui somente atributos0/8/15. Materiais f830c5da
também consomem a14 para normais. Assim há uma lacuna comprovada de cobertura:
posição intermediária pode coexistir com normal atual. Isso é hipótese forte
para instabilidade angular em outras luzes, NÃO prova causal do piscar observado.
F9 v2 não contém o histórico dos atributos14 nem todas as texturas/paletas para
reproduzir iluminação completa. Nenhuma correção especulativa aplicada.

Próximo foco técnico: coerência temporal de normais e poses por superfície,
checando também passes profundidade/material no mesmo domínio de câmera.
Preservar regras v25 de olhos/objetos e isolamento das projeções. Nunca repetir
a fusão mundial v26 para tentar corrigir iluminação. Não desligar luz como
solução. Sem alteração do executável/render nesta análise; sem commit/push.

---

# Revisão do diagnóstico — máscara de oclusão, não intensidade luminosa

Novos F9 session_26832_87980484 (modos0/1 misturados), 88046453 (1/2),
88068328 (2/0), 88080859 (36modo1), 88113656 (36modo2), 88130234 (20modo1).
Os logs confirmam patched=1 para os três shaders; FSR desligado. O usuário
percebe o piscar mesmo no suposto modo sem sombra. Porém a análise do programa
mostra que o teste v2 estava incorreto: o fator final é uma máscara de
ESCURECIMENTO, incluindo resposta angular. Trocar esse fator por1 não remove
somente sombra; pode bloquear a luz. Logo as capturas não excluem sombras.
Revoga-se a interpretação do diagnóstico v2 e sua validação semântica anterior.
Compilar o shader e testar seu produto não validou o significado dos fatores.

Novo probe v3: neutralizar a sombra projetada com ZERO ANTES do máximo com
oclusão angular; preservar a oclusão angular e atenuação no modo1. Modo2 retira
a resposta angular, incluindo o veto de face, conservando sombra projetada e
atenuação. Código59540949 usa veto angular binário, removido apenas no modo2.
Modo0 mantém o programa original em todos os ramos. Somente três programas
inspecionados, padrões únicos/exatos, construção temporária e aplicação atômica:
layout inesperado/capacidade insuficiente não altera fonte. Cache novo
lighter-occlusion3 separa os shaders dos probes anteriores.

F8 alterna modo3: máscara branca no primeiro MRT dos receptores conhecidos
para bloquear a contribuição dinâmica TESTADA; segundo F8 volta ao normal.
Não é desligamento de toda iluminação do jogo. F11 já é screenshot e fica
preservado. F10 continua alternando modos0/1/2. Tudo somente no CMD diagnóstico,
não salvo no INI e sem alterar pose v25 ou modo nativo30. F9 informa versão3
e modo lido do banco FP congelado do quadro: -1 sem receptor, -2 modos mistos.
Isso evita atribuir uma captura ao estado da tecla após sua gravação.

Causa do defeito original ainda não comprovada. Comparação visual v3 pendente.
Não enviar outra correção geométrica entre projeções. Usuário deve fazer uma
comparação simples NORMAL versus F8/luz testada bloqueada, com F9 em cada.
Validação: três shaders capturados compilados por DXC com helper real v3;
4.000 casos de máscara verificaram zero neutro, iluminação angular mantida
sem sombra e máscara total para bloqueio. Compilação JP DXC concluída; diff sem erros de whitespace. Headers de pose,
plano compute e shader compute permanecem byte a byte iguais à v25 aprovada.
Apenas o diagnóstico optativo foi alterado. Resultado visual ainda pendente.

---

# Isolamento luz/sombra após retorno confirmado à v25 (JP)

Usuário confirmou novamente que o retorno v25 ficou bom; descreve piscar no
peito/garganta de Claire em movimento. Novos F9 session_8480_87330109,
87338484, 87356921. Preservar v25; nenhuma regra de interpolação modificada.

O probe antigo F10 substituía r0 inteiro por branco, retirando junto sombra,
atenuação e resposta angular. Portanto não isolava somente a sombra. Novo
probe separa o produto final nos três shaders FP inspecionados: 0c121a16
usa r3.z, 4d26314d usa r3.w e 59540949 usa r2.x como fator de visibilidade.
F10 cicla 0 NORMAL (produto original), 1 SEM SOMBRA (visibilidade=1,
resposta luminosa/atenuação original), 2 APENAS SOMBRA (resposta luminosa=1,
visibilidade original). Isso isola o fator do receptor, não oculta geometria
nem altera atlas/paletas/câmeras/históricos/normais/INI. Não é correção visual.
O modo APENAS SOMBRA pode ficar artificialmente claro: é diagnóstico.

Só funciona com CV_LIGHTER_PROBE=1, whitelisting dos três códigos e expressão
final única verificada. Se mudar a tradução, não modifica e registra patched=0.
Cache separado lighter-factors2 invalida o probe antigo. Logs registram código
e aplicação, modo F10, e JSON F9 informa lighter_probe/lighter_mode. Fechar o
processo e usar o CMD habitual retorna ao comportamento normal v25.
Novo executar-teste-sombra-isqueiro-60fps-jp.cmd seleciona japonês DXC,
1080p/60 CPU, sem FSR; F9 nos três modos, repetir corrida com isqueiro no
mesmo enquadramento e aguardar terminar a sequência antes do próximo F10.

Validação: as três traduções capturadas foram modificadas pelo mesmo helper
usado no backend e compilaram via DXC. 3.000 casos verificaram a separação dos
fatores, além de capacidade insuficiente, shader desconhecido e patch repetido
sem mutação. Compilação JP DXC concluída, sem erros de build; diff sem erros de whitespace.
Headers de pose/compute/shader comparados byte a byte com v25 aprovada: iguais.
Nos frame001 das três sessões, os três códigos FP estão presentes, todos no
alvo 01c30000 esperado pelo transporte do modo: respectivamente 1.264, 1.253
e 1.253 draws receptores abrangidos. Isso verifica cobertura do teste, não a
causa do defeito. As imagens capturadas confirmam Claire em movimento com o
isqueiro; as amostras JSON válidas são 36, 36 e 35, FSR off e sem não finitos.
Não declarar corrigido: teste visual dos modos permanece pendente.
A causa do piscar ainda não está isolada: comparação visual dos modos pendente.
F9 .pose atual não preserva todos os atributos animados nem a textura da paleta,
e o replay de posições não prova sincronização de normais/iluminação/sombras.
Sem commit/push, nativo30 e regras v25 intactos.

---

# Regressão v26 — retirada, retorno à v25 (JP)

Usuário informou piora visual: olho voltou a deformar, rosto com isqueiro,
itens 3D e iluminação piscando. Cinco sessões F9 novas session_14848_86791687,
86798406, 86825343, 86841421, 86891906. A validação geométrica v26 não foi
suficiente para provar a imagem final. Não considerar zerar divergências de
pontos entre projeções como prova de correção da iluminação.

Retirar integralmente o código v26: agrupamento de superfícies entre projeções,
prioridade de passe visível, domínio mundial adicional nos grafos e ajuste de
contenção. Restaurar exatamente código e testes de antes da rodada v26, que
o usuário confirmou corrigir olhos e objetos (v25). Manter capturas e este
histórico; sem apagar alterações anteriores, sem commit/push. Nativo 30 mantido.
Confirmação nas imagens: frame001 da sessão 86891906 mostra olhos
exageradamente abertos em comparação com frames000/002; outras imagens mostram
Claire com isqueiro em movimento. Todas as cinco sessões FSR off e compute0.

Rollback aplicado com comparação byte a byte dos arquivos atuais contra os
.applied v26 e cópia exata dos .before v25 (somente normalização CRLF original).
Código e testes anteriores restaurados; japonês recompilado. CPU/10.000 frames,
fetch seletivo, VF, blend e compute WARP512/hardware512 passaram.
Replay dos 180 novos arquivos completos: arestas acima de 2x o maior
comprimento nos endpoints, ocorrências antes -> após rollback:
86791687: 57.922 -> 0; 86798406: 43.258 -> 0; 86825343: 55.302 -> 0;
86841421: 35.594 -> 0; 86891906: 2.150 -> 0. Nenhuma superfície
parcialmente vetada depois. São ocorrências, não vértices únicos nem prova
de renderização final. Replay não recompõe todos os aliases não aceitos nem
paletas/atributos finais. O snapshot da face frame001 sozinho não reproduziu
seu deslocamento (moving_after=0); não declarar a causa exata do olho isolada.
A imagem do usuário é a confirmação da regressão, e a validação anterior de
igualdade entre projeções era um critério insuficiente para aprovar v26.
Confirmar visualmente o retorno ao comportamento aprovado v25.

A iluminação original ainda precisa de investigação; não reaplicar uma mistura
entre projeções sem validar paletas, normais, constantes FP e resultado visual.

---

# Sombras e isqueiro — pose v26 (JP)

Usuário confirmou a correção v25 dos objetos. Novos F9: Chris na parede
(session_26336_84679921, session_26336_84691359, session_26336_84698203)
e Claire com isqueiro (session_16500_84792109, session_16500_84798390).
A terceira sessão de Chris terminou com arquivos incompletos: validar somente
arquivos completos, sem interpretar cauda truncada como erro de renderização.
1080p, interpolação CPU, FSR off nas amostras verificadas.

Causa reproduzida: pontos de mundo exatamente iguais podiam receber poses
diferentes nos passes visível e de sombra porque a coerência exigia a mesma
projeção completa. Exemplos capturados: Chris frame018, 24.310 ocorrências
compartilhadas divergentes, até 1,85649 unidades; Claire frame001 da última
sessão, 40.344 ocorrências divergentes, até 2,26442 unidades. São unidades
do espaço de entrada, não pixels nem milissegundos.

v26 preserva a coerência original por câmera para todos os shaders e acrescenta
um domínio de pontos de mundo apenas para os cinco VPs já inspecionados.
Cópias de superfície inteira, com mesmo número/ordem/xyz exatos, recebem uma
proposta comum do passe visível. Históricos de sombra não são uma segunda
fonte de animação. Propostas visíveis incompatíveis conservam a pose atual.
Divisões diferentes de draws compartilham os pontos exatos e conservam o
fallback da superfície inteira se existir conflito. Não usar prioridade por
ponto isolado: ensaio descartado produziu arestas esticadas nas divisões.

A regra anterior de contenção de olhos/boca continua usando a câmera visível:
cópias da sombra não tornam esse container falsamente ambíguo. Projeções
locais/desconhecidas continuam isoladas, salvo compartilhamento já comprovado
pela câmera original/upload. Sem aproximação espacial e sem alocação por quadro.
CPU e compute constroem os mesmos grupos; GPU prioriza apenas as propostas
visíveis dentro de uma cópia de superfície inteira comprovada.

Esta alteração sincroniza geometria entre passes. Não interpola arbitrariamente
constantes FP, paletas ou matrizes de luz: permanece a proteção que mantém a
projeção da luz atual junto de seus leitores. Assim, o replay geométrico não
prova sozinho toda a fluidez da iluminação do rosto; confirmar visualmente
no jogo, correndo e movimentando a câmera, FSR off. Não declarar a iluminação
inteira resolvida antes desse teste. Nativo 30 FPS fora da alteração.

Validação final: executável japonês DXC compilado. CPU/cadência/10.000 quadros,
fetch seletivo, VF e blend passaram. Compute WARP 512 e hardware 512 quadros
iguais à CPU bit a bit; incluem cópias entre projeções, divisão de draws,
conflitos de material originais, indisponibilidade de histórico e vetos.

Replay das 160 novas amostras completas: divergências de pontos mundo entre
visível/sombra, acima de 1e-5 por componente, passaram a zero em todas:
- Chris sessão 84679921: 240.866 ocorrências -> zero (36 amostras).
- Chris sessão 84691359: 175.645 -> zero (36).
- Chris sessão 84698203: 79.257 -> zero (16 completas, 1 .pose truncado omitido).
- Claire sessão 84792109: 693.780 -> zero (36).
- Claire sessão 84798390: 522.508 -> zero (36).
São ocorrências entre amostras, não número de vértices únicos. Nenhuma nova
aresta acima de 2x ou triângulo parcialmente vetado nos replays. Outras 108
amostras antigas (objetos/rosto) também passaram. As antigas sem flag de espaço
de mundo são cobertas adicionalmente pelos testes sintéticos com sombras.
O replay de snapshot não reconstrói todos os aliases de draws não aceitos;
a comparação geométrica não substitui a imagem final do jogo. Nenhuma medição
de ganho de FPS foi feita nesta correção. Confirmar fluidez e desempenho no jogo.
Regressões dos olhos/peças desprendidas incluem cópia de sombra em outra
projeção, nos dois sentidos de ordem, e continuam passando.
Sem commit/push; executável inicial somente japonês.

---

# Cenário — pose v25, restrições de superfícies estáveis (JP)

As duas novas sessões session_21004_81784640 e session_21004_81801390
confirmaram defeitos remanescentes na v24, com identidade UV+source_index.
Uma proposta temporal incorreta podia mover pontos de uma superfície estável:
a ausência de movimento era ignorada pela resolução das propostas. Cópias
usadas nas sombras também podiam divergir por projeção ou divisão dos draws.

v25 registra superfícies comprovadamente estáveis por correspondência temporal.
A prova pode ser conservada entre os dois snapshots recentes somente com
geometria inteira e câmera completas exatamente iguais, sem atravessar cortes.
Diferenças de até 1e-5 por coordenada qualificam estabilidade numérica; isso
não altera o casamento espacial, que continua exato. Geometria nova ou sem
histórico não é automaticamente estável. Um ponto estável só bloqueia uma
proposta conflitante; a proteção existente conserva toda a superfície conectada
para evitar triângulos parcialmente interpolados. Partes móveis independentes
continuam interpolando.

Somente cinco programas VP inspecionados, com entrada em espaço de mundo,
podem fornecer restrições exatas para as sombras conhecidas. Não se compartilham
câmeras ou posições interpoladas entre projeções. A regra cobre cópias com
outra divisão de draws; shaders locais/desconhecidos ficam fora dessa regra.
CPU e compute usam a mesma restrição, com armazenamento persistente, sem
alocação por quadro. Caminho nativo 30 FPS permanece fora da interpolação.

Replay sequencial de 144 amostras (quatro sessões): pontos presentes em pelo
menos 24/36 amostras foram rastreados. Deslocamentos indevidos acima de 0,005:
- session_21004_81784640: 938 ocorrências, máximo 3,89093 -> zero.
- session_21004_81801390: 534 ocorrências, máximo 3,59088 -> zero.
- session_23224_80970906: 707 ocorrências, máximo 3,52909 -> zero.
- session_24676_77808890 (rosto): zero -> zero.
Sem novos triângulos parcialmente vetados ou arestas esticadas acima de 2x.
Isso é reprodução de entradas capturadas, não medição de FPS ou garantia de
que todos os defeitos visuais foram resolvidos. F9 pode omitir quadros; confirmar
no jogo, correndo e mudando a câmera, inicialmente JP 1080p/60 CPU, FSR off.

Diagnóstico .pose v2 preservado: flag 4 nos draws serializados registra prova
de estabilidade; JSON informa stationary_draws. Replay lê esse indício, e
capturas antigas continuam aceitas. Log [POSE] v25. Testes incluem superfície
estável contra vizinho incorreto, movimento independente, reaparecimento,
corte de cena/câmera, ruído numérico, sombras entre projeções e shaders excluídos.
Validação concluída: executável JP compilado; regressões CPU, fetch seletivo,
VF e blend passaram. Compute WARP 512 quadros e hardware 512 quadros iguais
à CPU bit a bit, incluindo as novas restrições estáveis. Replay sequencial
final repetiu os resultados acima; replay do arquivo F9 sem divergências.
Teste sintético de preparação CPU: 4.000 draws / 48.000 vértices,
0,893 ms/present nesta máquina; não é FPS do jogo. Diff sem erros
de whitespace. Usuário confirmou a correção v25 visualmente no jogo.
Sem commit/push; instalação inicial somente japonesa.

---

# Cenário — pose v24, identidade de vértices de origem (JP)

Usuário: v23 amenizou, mas não resolveu os objetos do cenário.
Nova sessão session_23224_80970906: 36 quadros completos 1080p/60 CPU,
FSR off. Pontos da parte frontal do timão antes rastreada não deslocaram,
mas outras peças ainda saltam. Diagnóstico por pontos presentes em pelo
menos 24/36 amostras encontrou 111 draws suspeitos; exemplo frame 007:
draws 1840/1841, FP 7835a34b, pontos na região do timão, deslocamento até
3,52909 unidades. A identidade por FP não distingue todas as peças que
compartilham o mesmo programa, UV e formato. Não declarar resolvido pela v23.

v24 inclui o índice guest real de cada vértice de origem no fingerprint UV.
Esse índice é o que leu atributo 0, nunca o slot de upload, o offset guest
rotativo nem a ordem global dos desenhos. Sequências distintas de índices
não podem virar a mesma correspondência apenas por repetir UV/formato.
A coerência de cópias exatas entre passes continua independente dessa chave.
Não se inclui normal animada, iluminação ou posição atual na identidade.

Encaminhamento completo: fetch CPU (hit/miss), fetch seletivo/VF, triângulos,
strips/fans e expansão de quads. Quads não indexados usam first+q*4+corner;
quads indexados leem os quatro índices guest uma vez e reutilizam a mesma
origem na emissão dos seis vértices. Nenhuma leitura adicional de índice por
triângulo e nenhuma nova alocação. Registro só acontece com pose 60 ativa.
O layout .pose v2 permanece: campo uv agora é hash UV+source_index; JSON
informa vertex_identity=uv_source_index; log [POSE] v24.

Limite importante: isso é uma identidade adicional de topologia, não ID
semântico global do modelo. Modelos distintos ainda podem reutilizar a
mesma numeração. Se um mesh realmente muda seus índices entre frames,
perde aquele match e conserva a pose atual, em vez de inventar deslocamento.
É necessário verificar também suavidade/cadência no novo teste.

Os índices guest não foram gravados separadamente nas capturas anteriores;
portanto não é possível reconstruir retroativamente a chave v24 com aqueles
arquivos. Testes sintéticos validam separação por índice, índice completo de
32 bits, normal/posição animadas fora da chave e aliases da expansão quad.
A eficácia na imagem real desta sala depende de novo F9 após instalar v24;
não se declara que o replay anterior já comprovou a correção atual.
Validação concluída: build JP; regressões de identidade/cadência/10.000
frames CPU; fetch seletivo, GPU/VF e blend passaram. Compute WARP 512 frames
e hardware 512 frames continuam iguais à CPU bit a bit. Esses testes validam
compatibilidade e a regra de identidade, não provam a imagem do cenário.
Modo nativo 30 inalterado. Instalação inicialmente só japonês, sem commit/push.

---

# Cenário/timão — pose v23, JP, 20/10/2026

Usuário confirmou que a correção facial v22 funcionou. Novas sequências:
session_8152_79439437 (36 amostras) e session_8152_79464171 (25 amostras),
1080p/60 CPU, FSR desligado; sala scene=0x80010d01. Timão claramente desmonta
nos quadros intermediários. Verificação geométrica simples: nenhuma aresta
>2x em 61 snapshots. O defeito pode mover/rotacionar triângulos inteiros,
logo o teste de arestas não basta para identificar toda deformação.

Evidência: frame 004 da segunda sequência, draw elegível 1441 (op 1521),
key antiga 0x1d07b11d6462a11b, VP e985b049, FP atual bff5fa66, possui endpoint
antigo deslocado 0,428 unidades. Esse endpoint coincide com geometria de
outra passagem FP 7835a34b encontrada na amostra anterior, enquanto a própria
geometria atual do timão também está presente naquela amostra. Câmeras iguais.
A identidade temporal antiga incluía VP/UV/layout, mas ignorava o FP:
era permitido corresponder objetos/passagens de material distintos.

v23 acrescenta à chave temporal o hash do PROGRAMA fragment congelado do
mesmo batch (fp_snap_code), já disponível em cache. Não usa endereço guest,
constantes animadas nem lê o estado do FIFO em outra thread. Sem programa
congelado reconhecido, não cria correspondência de pose; a proteção existente
de uploads não aceitos preserva o desenho original. A coerência geométrica
continua independente da chave: cópias exatas entre materiais/passes ainda
recebem a mesma pose. Nenhuma classificação de sala, timão ou objeto especial.
Regra vale para CPU, blend VF e compute; modo nativo 30 permanece inalterado.

Replay sequencial comparando as duas identidades, com os inputs capturados:
na primeira sequência, maior deslocamento dos pontos rastreados do timão cai
de 2,6644776 para 0,0023249 unidades. Grandes saltos foram removidos nesse
reprodutor; não se promete zero resíduo nem correção universal de cenários.
Na segunda sequência, o defeito visto no BMP não se reproduziu reconstruindo
apenas a série de amostras F9 (há frames intermediários não capturados).
Isso é uma limitação explícita do replay, não evidência de ausência do bug.
Quatro arquivos .pose no fim dessa sequência têm extensão VP truncada;
as seções completas de geometria/câmeras/ops permanecem legíveis para o
replay específico. O analisador estrito continua rejeitando arquivo truncado.

Regressão nova reproduz o pareamento falso com as duas identidades FP reais,
verifica fallback na reaparição sem histórico próprio, mantém a animação
válida a 60 e testa coerência entre passes com chaves de material distintas.
Validação concluída: replay sequencial de 133 amostras em quatro sessões v2,
sem triângulos de veto parcial nem arestas >2x no caminho novo; regressões
CPU/10.000 frames, fetch e blend VF passaram; 512 frames WARP + 512 hardware
compute iguais à CPU bit a bit. Build JP concluído (MyGameRecomp_JP.exe).
Comparação sequencial é reconstruída com amostras F9, não medição de FPS
nem reprodução de todos os frames reais entre elas. As imagens finais
continuam sendo a validação necessária do usuário.
A confirmação da imagem final do timão e de outros objetos exige teste no jogo.
Build inicialmente só japonês. Sem commit/push nesta rodada.

---

# Correção facial pose v22 — JP, 20/10/2026

Nova sequência: session_24676_77808890, 36 amostras 1080p/60 CPU, FSR off.
A captura v2 funcionou: frame 002 tem 10.063 operações, 58 bancos VP e
19 programas. Sem NaN e sem arestas esticadas como na falha antiga.

Causa reproduzida no replay: a superfície principal da personagem retém a
pose atual por conflito (componente 6417 no frame 002), mas os globos oculares,
inserções da boca e cílios são superfícies desconectadas e continuam no
midpoint. Componentes dos olhos 6932/6945 e cílios 9785/9793 estavam deslocados
em relação à pele. Não era animação GPU por paleta; os shaders capturados
permitem localizar as posições e as respectivas passagens no rosto.

v22 estende SOMENTE o grafo de fallback. Um detalhe inteiramente contido em
uma superfície animada substancialmente maior acompanha o seu fallback:
mesma câmera exata (32 floats), contagem de vértices pelo menos 4x menor,
volume pelo menos 8x menor, contêiner com movimento posicional real (>1e-5).
Não cria solda por proximidade e não muda a interpolação numérica. Contêineres
sobrepostos que não se contêm mutuamente são ambíguos e não são vinculados.
A hierarquia usa o menor contêiner elegível. No máximo 128 possíveis
contêineres; acima disso omite a regra opcional, preservando proteção v21.
Reaproveita scratch de busca morto após prepare; sem alocação por frame.
Vale para CPU, blend VF e plano compute. Nenhum caminho novo em 30 FPS.

Limitação: containment é uma proteção geométrica conservadora, não um ID
semântico do personagem. Pode reter também um detalhe/objeto contido no
mesmo volume quando houver conflito; partes retidas apresentam pose nativa
nesse quadro. É preferível à separação visual, mas exige teste de suavidade.
Não se declara iluminação nem todos os defeitos visuais corrigidos.

No replay do frame 002 os olhos, inserções da boca e cílios ficam exatamente
na posição atual junto com a pele; os 22.806 vértices do outro personagem
que estavam interpolando continuam interpolando. Zero triângulos com veto
parcial e zero arestas >2x. Testes cobrem objetos fora do volume, outra câmera,
contêiner estático, sobreposição ambígua, ordem dos desenhos e recipes VF.
Validação concluída: 180 snapshots de cinco sequências (corrida + close)
sem triângulos de veto parcial nem arestas >2x, maior razão 1,00013;
regressões CPU/10.000 frames, fetch seletivo, recipes GPU e blend passaram;
512 frames WARP + 512 hardware compute iguais à CPU bit a bit, incluindo
novo caso com detalhes desconectados contidos. Build JP concluído.
Microbenchmark isolado de componentes no frame 002: cerca de 0,61 ms antes
e 1,01 ms depois (200 execuções em cada). Custo adicional aproximado 0,4 ms
nesse caminho de proteção, não medição de FPS do jogo. Não se promete custo
zero nem ganho de desempenho por esta correção visual.
A confirmação da imagem final no jogo ainda depende de novo teste do usuário.
Executável atualizado inicialmente só no Japão. Sem commit/push nesta rodada.

---

# Investigação do rosto — captura v2 (JP)

A sequência session_8264_76282140 mostra olho/sclera fora da pálpebra e uma
fenda ao lado da boca nos frames 001–003, com FSR desligado. O replay dos
36 frames não encontra as explosões de triângulos corrigidas na v47.
Isso não prova que olho e pele estejam sincronizados após o vertex shader.

Encontrado e corrigido no diagnóstico: o contador s_rb.count já era zero
quando F9 copiava os registros de desenho. Agora usa s_dbg_last_draws,
congelado pelo mesmo frame antes do reset. O JSON informa captured_ops,
captured_vp_banks e captured_vp_programs para conferir a presença dos dados.

A captura v2 acrescenta first/count/topology/is_vp por operação, os bancos
completos de 512 constantes VP usados no frame e os programas HLSL usados.
Os primeiros 32 floats não bastam para investigar transformações faciais
que dependem de c8+. Não se afirma que esse seja o diagnóstico final.
Apenas o diagnóstico mudou nesta rodada; a renderização continua pose v21.
Uma hipótese de união de fallback entre projeções foi descartada porque
reproduziu o frame defeituoso sem nenhuma mudança nos vértices finais.

Validação: build JP concluído; teste de schema v1/v2 passou (inclui truncamento
e bancos duplicados); teste D3D12 salvou 36 amostras com pixels/metadados
corretos, resize e zero erros D3D12; replay do frame facial 001 continua
bit a bit igual ao capturado. Nenhum ajuste especulativo de pose foi instalado.

Próximo teste: JP, 1080p, 60 FPS CPU, FSR desligado, F9 na mesma cena de
close do rosto. Opcionalmente outro F9 em 30 FPS para referência visual.
Não há automação de controles. Esta captura não é benchmark de desempenho.

Extensão binária v2: header mantém 16 u32, versão=2, stride op=336;
os 320 primeiros bytes de cada op mantêm o significado v1; os últimos
16 bytes são u32 first/count/topology/is_vp. Após os ops, quatro u32:
magic=0x3256434d, número de bancos, número de programas, constantes=512.
Cada banco: u32 slot, 512 float4 efetivamente enviados ao VP.
Cada programa: u32 hash, u32 tamanho UTF-8, bytes HLSL sem terminador/padding.
As matrizes originais e enviadas permanecem nos arrays anteriores; o banco
completo representa o estado enviado. Limite de 64 MiB e fila de quatro slots
continuam. Ferramenta de análise e replay aceitam v1 e v2.

---

# Atualização v47 — resultado da investigação

As três primeiras sequências capturaram uma falha real de interpolação: pontos
vizinhos misturavam midpoint e posição atual após um conflito de costura.
O fallback agora é coletivo por superfície conectada. Log e JSON incluem
seam_components e seam_veto_vertices, para medir onde a interpolação foi retida.
O formato binário .pose v1 continua o mesmo. Execute outro F9 durante a corrida
para verificar a forma da personagem e a suavidade no jogo atualizado.
Iluminação não foi declarada corrigida: exige comparação visual após a geometria.

---

# Captura de corrida — F9 (v46 JP)

1. Abra executar-code-veronica-japones.cmd, escolha 1080p e 60 FPS.
2. Primeiro teste com Frame Generation desligado, para isolar a interpolação.
3. Na sala onde aparece o defeito, comece a correr e aperte F9 uma vez enquanto
   continua correndo. A sequência registra 36 amostras. Repita com o isqueiro
   ligado/desligado e outras câmeras, se necessário. Cada F9 cria outra pasta.
4. O log mostra [MOTION-CAPTURE] start com o caminho completo, arquivos salvos,
   amostras e quantos frames foram pulados por fila cheia. Avise após o teste.

`motion-captures/session_<pid>_<tick>/frame_NNN` contém:
- .bmp: quadro real completo antes do overlay, depois de AA/escalonamento;
  inclui a pose de apresentação dos 60 FPS, não é imagem gerada pelo FSR.
- .json: QPC/frequência, frame/scene/stamp, razão/fase, matches, conflitos,
  costuras, sombras, deslocamento máximo, não finitos e modo de interpolação.
- .pose: posições originais e finais CPU/VF, correspondências, histórico,
  matrizes atuais/anteriores e efetivamente enviadas, e constantes FP congeladas.

Captura opt-in: sem alocar readback quando nunca acionada. Quatro slots limitam
memória/concorrência; cópias usam a lista normal, fence próprio só é consultado,
sem wait_for_gpu extra; escrita BMP/bin/JSON em threads após concluir a cópia.
Se o disco/fila estiver ocupado, pula e registra o número; QPC expõe os intervalos.
São 36 amostras, não promessa de 36 frames consecutivos com qualquer disco.
Tamanho limitado: 64 MiB por readback/pose. A gravação consome memória/IO e pode
alterar timing; este modo investiga imagem, não serve de benchmark de FPS.
F12 pesado continua disponível e não é necessário durante este teste.
O FSR não é suspenso pela captura F9. Também pode captar 30 FPS como referência;
nenhuma regra de animação ou interpolação de 30 FPS foi alterada.

GPU compute: imagem ainda é capturada, mas final_positions_available=false,
porque não fazemos readback da geometria computada. O arquivo guarda entradas
para análise, sem alegar que elas são as saídas GPU. CPU e blend VF incluem
reconstrução das posições enviadas. Isso não é dump pós-skinning do shader.

## Análise

`python tools/analyze_motion_capture.py <pasta-da-sessão>` produz analysis.json.
Procura componentes não finitos, deslocamentos grandes, arestas que crescem
mais que 2x comparadas aos endpoints temporais e alteração de câmeras das
passagens de sombra reconhecidas. São suspeitas para conferir nas imagens,
não diagnóstico automático nem correção heurística da renderização.

Schema .pose, little-endian, versão 1:
header 16 u32: magic=0x4d435031, versão, nv original, nd, nv packed, modo
(0 CPU / 1 blend VF / 2 compute), parity, scene, stamp, frame,
sizeof(RsxPoseVertex)=20, sizeof(RsxPoseDraw)=24, sizeof(RsxPoseTrack)=16,
32 floats/câmera, número de ops, stride op=320.
Depois, nesta ordem:
- nv RsxPoseVertex (xyz float3, uv/stamp u32), nv float3 finais;
- nd draws packed e nd bindings originais (u64 key; first/count/cb/flags u32);
- nd tracks (double changed_at, float alpha, int active);
- nd int32 vertex_sources e nd int32 camera_sources;
- packed float3 antigos e packed float3 atuais;
- nd*32 floats matrizes antigas, atuais e efetivamente enviadas;
- ops registros 320 bytes: 16 u32 (op, FP hash, VP hash, cb, RT offset,
  RT w/h, viewport w/h, pose aceito, blend, depth test, nk, texmask, clear, copy),
  64 floats com até 16 float4 FP congelados, demais zeros.

O usuário relata falhas anteriores ao FSR durante corrida e sombras do isqueiro.
A causa e a correção dessas falhas permanecem pendentes de capturas de gameplay.
Não confundir esse diagnóstico com a correção comprovada do overlay no FSR.

## 22/10/2026 — boca e cabelo do Steve rasgados no 60 FPS (corrigido)

Sintoma (JP, cutscene 0x80030000, 60 FPS com interpolação CPU): no quadro interpolado, espetos escuros
em volta da boca do Steve e um corte no cabelo perto do olho. O quadro gravado do mesmo lote fica certo,
e no 30 FPS não aparece.

F9 coerente: com a pose fora da thread de render (v4/v5), a F9 copiava o histórico de outro lote
(1.545 de 1.545 desenhos com posição atual diferente da empacotada). Agora, enquanto a F9 grava, o
produtor espera a thread de render terminar o lote antes de começar a pose do seguinte
(`render_frame`, `mc.request || mc.remaining`). Fora da captura nada muda.

Causa: a mesma peça (mechas de cabelo, partes do rosto) é desenhada em várias passadas, e cada passada
casa sozinha com o quadro anterior. Com muitas peças idênticas, passadas diferentes recebem históricos
diferentes, e a coerência marca o grupo como conflito (`vertex_sources=-2`, pose atual). Nas costuras
(`rsx_pose_weld_recipe`), porém, um ponto na pose atual contava como "sem proposta": uma peça vizinha
interpolada puxava só os pontos de borda coincidentes. Na captura, 254 dos 440 desenhos em conflito
tinham parte dos vértices interpolada e parte atual, o que rasga os triângulos.

Tentativas descartadas, todas com a boca certa:
1. Veto dos vértices em conflito (commit 164bb16). As costuras espalham o veto, e o personagem quase
   inteiro deixava de interpolar: de ~23–29 mil para 1,7–3 mil vértices interpolados por quadro. O
   usuário sentiu o 60 FPS menos fluido.
2. Escolher para o grupo o histórico que move menos. Os desacordos com as peças vizinhas aumentam, e
   os vetos das costuras também: média de 18 mil vértices por janela, contra 3 mil antes. Também menos
   fluido.
3. Tirar a peça em conflito das costuras. Fica fluido, mas abre frestas no rosto, nas mãos e no braço.

Correção final (`pose_interpolate_batch_for`, depois das costuras): as costuras, os vetos e o resto da
interpolação ficam como antes. Só um desenho em conflito que saiu das costuras "meio puxado" (parte
dos pontos interpolada, parte atual) move os pontos parados pelo deslocamento médio dos puxados, e
acompanha a vizinha inteiro. Simulação na F9 do modo antigo (2 quadros, 250 desenhos meio puxados):
esticamento máximo dos triângulos de 74x para 1,0x. `CV_POSE_CONFLICT_FOLLOW=0` volta ao comportamento
anterior (atalho de comparação `executar-code-veronica-japones-antes-da-boca.cmd`). Marcador:
`[POSE-CONFLICT] v4 follow=on`. Teste do usuário na JP: boca certa e fluidez igual à de antes.

Medição de compasso: com `CV_FRAME_PROFILE=1`, `[PRESENT-PACE]` mostra os percentis do intervalo
entre apresentações e as duas pernas do replay. Na cutscene de abertura, com e sem a thread de pose:
mediana de 16,6 ms, p90 entre 18,2 e 19,4 ms e 0 a 13 quadros acima de 21 ms a cada 300.
