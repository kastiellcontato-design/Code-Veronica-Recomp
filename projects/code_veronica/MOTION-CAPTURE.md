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
