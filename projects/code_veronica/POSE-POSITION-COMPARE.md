# Comparação exaustiva das posições — v1

## Motivo e escopo

O usuário testou a rotina nativa B630, não percebeu ganho grande de FPS nem
regressão e autorizou mantê-la e seguir para outro custo. Os logs confirmam
`first full native B630 execution` no modo novo e `original` no controle.
As cargas/câmeras variam entre execuções; não há ganho causal de FPS quantificado.

O próximo alvo é parte da etapa `history` da interpolação, que apareceu em torno
de 3–4 ms nas cenas pesadas: a verificação exaustiva das posições depois que uma
correspondência foi encontrada. **Não é toda a etapa history**, nem a busca de
correspondências ou as costuras dos vértices.

Antes: para cada vértice, calcular a distância, rejeitar se maior que 64 e
comparar os bits das três coordenadas para detectar mudança.
Agora: SSE2 verifica quatro vértices por vez. São carregados exatamente 12
floats por grupo, sem ler um quarto componente fora da última posição.
Somas preservam a ordem `(dx*dx + dy*dy) + dz*dz`, sem contração em FMA.
O caminho escalar trata as sobras de um a três vértices e plataformas sem SSE2.
Quando a câmera ou um vértice anterior já marcou mudança, a comparação dos bits
é dispensada; a distância de **todos** os vértices continua sendo verificada.

## Comportamento preservado

- Mesmo limite de distância ao quadrado: `>64.f`.
- Mesma detecção de mudança por bits, incluindo zero com sinal.
- Mesmos pares de malhas, ordem, ambiguidades, limites de busca e fallback.
- Mesmos critérios de cortes e temporização, história, câmeras e 30 Hz da lógica.
- Proteções de rosto/objetos, geometria estacionária, alias e costuras mantidas.
- Sem alteração de shaders, qualidade, resolução, sombra ou iluminação.
- Em rejeição por salto, o indicador parcial de mudança é irrelevante e não é
  usado pelo chamador; a proposta inteira já é descartada.
- Funções de interpolação são usadas somente no caminho de 60 FPS habilitado.
  Executáveis normais JP/US/EU não foram recompilados/substituídos.

`CV_POSE_COMPARE_SCALAR=1` seleciona a verificação escalar anterior. O padrão do
executável de teste usa SSE2. O log registra `[POSE-COMPARE]` com a seleção efetiva.
O controle é somente por processo, sem opção nova persistida no launcher.

## Validação

- **132.096 casos** escalar versus SSE2: quantidades de 0 a 257 vértices,
  mudança prévia, vários alinhamentos, movimentos, limites de 8 unidades e
  seus vizinhos representáveis, zeros com sinal, NaN/Inf e valores grandes.
- Testes com a última posição terminando exatamente antes de uma página sem
  acesso: nenhuma leitura excedente.
- Suíte completa `code_veronica_pose_tests`: passou, incluindo os casos de
  olhos/detalhes, identidade de materiais/fontes, ambiguidades, geometria
  estacionária, costuras, câmeras, limites de busca e salto fora das amostras.
- Teste de estabilidade de 10.000 quadros da suíte existente: passou.

Microbenchmark de 4.096 vértices (somente a função de comparação): escalar
4,24–5,36 µs; vetorial 1,75–2,36 µs nas três rodadas finais. Não representa
aceleração da etapa history inteira nem ganho de FPS medido no jogo.

## Teste e comparação

- `executar-teste-historico-posicoes-jp.cmd`: nova comparação SSE2.
- `executar-teste-historico-posicoes-antigo-jp.cmd`: comparação escalar anterior.

**Ambos mantêm a rotina nativa B630 ligada**, para isolar esta mudança.
Usam 1080p, DXC, interpolação CPU e FSR desligado, no executável separado
`MyGameRecomp_JP_NativeDescriptors.exe`. A versão anterior está preservada em
`D:/ps3recomp/work/spu-descriptors/v1/` com manifesto de hashes.

Entrar na mesma sala/câmera, esperar pelo menos 10 segundos e apertar F12.
Logs separados: `run-log-historico-posicoes-jp.txt` e
`run-log-historico-posicoes-antigo-jp.txt`. Comparar janelas com cargas semelhantes;
o custo do F12 é excluído das médias existentes. Gameplay ainda pendente ao entregar.

Arquivos: `libs/video/rsx_pose_compare.h`, integração em
`rsx_pose_interpolation.h`/`rsx_d3d12_backend.c` e
`projects/code_veronica/tests/test_pose_compare.cpp`.
