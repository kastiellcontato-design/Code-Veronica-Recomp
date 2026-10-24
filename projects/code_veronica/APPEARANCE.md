# Character appearance — JP, US and EU

The standard Windows play builds include `OPTION → RECOMP SETTINGS → APPEARANCE`.
Left/right selects the five original appearances plus 26 zombies and two Alexia forms (33 entries).
Selections made at the title screen apply once Claire is loaded. Original restores
the player's original visual state. The campaign identity and save are unchanged.

The rotating preview uses the local game's geometry, textures and hands, with
arms lowered. Wesker's approved glasses offset is X=0, Y=0.870, Z=-1.050 in head
joint space. His original `WKGRAS2` lens texture comes from `ob_1127` in
`rdx_lnk/rm_1002.arc`, rather than any body or glove texture.

Local derived lens archives live in `appearance-assets/pl03-<language>.arc`.
The preview automatically selects the matching language and the existing title
file-overlay callback serves that copy to the guest loader. Original installed
archives are never overwritten. The derived archive keeps the original logical
and physical lengths, preserves decoded body/motion/texture data, adds the lens
dependency and regenerates validated SFH checksums. These derived game assets
must stay local and must not be committed.

`appearance-assets/prepare_lens_archive.py <language/system> <destination>` derives
the overlay from an installed copy; `inspect_models.py` must be beside that script.
Prepared local languages: jpn, eng, fra, ger, ita and spa.

The gameplay accessory uses the SAME registered full 13-texture bank as the
extended body, with the glasses' local material remapped from 0 to 12. Creating
an unregistered one-entry view of that bank caused missing/fallback materials.
The accessory bypasses the unrelated timed particle callback normally dispatched
for object ID0 and is recreated if a room resets its object-pool entry.

Each region has its own functions/data globals in `cv_appearance_bindings.h`.
Bindings were compared against each region's generated lift, including loader,
head-attachment constructor, texture-bank getter and model-endian global.
No regional addresses are derived from one blanket offset.

Compatible 22-joint base motion slots use the selected male character's motions.
Missing/incompatible slots and separate weapon/equipment archives retain the
existing game path. This does not claim every weapon animation has been ported.

Build switches: `CV_CHARACTERS=ON` enables the feature in standard regional play
builds. `CV_APPEARANCE=ON` stages a separately named regional Characters executable.
The 30/60 FPS clock, interpolation and frame-generation settings are unchanged
by this integration. No automated gameplay test is performed; compilation and
source checks do not establish visual gameplay correctness.

Before deployment the three previous play executables were copied to
`appearance-backups/pre-production-20261021/`. Historical investigations remain
in the task workspace's `work/appearance/STATUS.md`.

Deployment completed 2026-10-21: all three regional candidates compiled successfully and were installed as CodeVeronicaPort.exe / CodeVeronicaPort_JP.exe / CodeVeronicaPort_US.exe. SHA256 copies verified; backup and manifest retained. All build caches restored to CV_APPEARANCE=OFF, CV_CHARACTERS=ON. Gameplay glasses fix remains pending user visual confirmation.

## Atualização para Cláudio — personagens e integração regional (21/10/2026)

Esta seção descreve o estado atual e substitui, para esta entrega, as observações históricas acima sobre não alterar executáveis. A documentação técnica atual está em `projects/code_veronica/APPEARANCE.md`.

### Melhorias entregues

- Primeira opção `APPEARANCE` em `OPTION → RECOMP SETTINGS`, com Claire Original, Claire Alternate, Chris, Steve e Albert Wesker. Textos dessa interface em inglês.
- Prévia 360° maior e mais nítida, com texturas e mãos dos modelos, braços abaixados e rotação de 0,7 rad/s (aproximadamente 9 segundos por volta).
- Troca visual durante o jogo, sem trocar identidade da campanha, inventário ou save; retorno à aparência original restaura os dados visuais salvos.
- Correções de propriedade dos recursos e estados de textura do piscar dos olhos para evitar corpo branco, texturas misturadas e invisibilidade ao restaurar Claire.
- Correções do menu que sumia/piscava: retenção temporária da interface e capacidade do lote de desenho aumentada para 262144 fragmentos.
- Remoção dos elementos auxiliares de cabelo da Claire nos personagens masculinos. Movimentos base compatíveis de 22 articulações usam os movimentos do personagem selecionado. Movimentos incompatíveis e arquivos separados de arma/equipamento continuam pelo caminho existente; não foi feita conversão completa de todas as animações de todas as armas.

### Wesker: preservar o ajuste aprovado e verificar a última correção

- Posição aprovada dos óculos na cabeça: X=0, Y=0,870, Z=-1,050. A prévia foi aprovada pelo usuário; não reposicionar novamente sem necessidade.
- A lente correta é `WKGRAS2`, do objeto `ob_1127` em `rdx_lnk/rm_1002.arc`. O banco original do corpo tem 12 texturas e não contém essa lente; o índice antigo 5 era uma textura de luva/cinto.
- São usadas cópias locais derivadas de `pl03-<idioma>.arc`, com a lente original adicionada ao banco como entrada 12 (13 texturas no total). Arquivos originais instalados não são sobrescritos. Há cópias para jpn, eng, fra, ger, ita e spa, selecionadas automaticamente a partir do idioma carregado.
- Causa encontrada na revisão anterior: o registro da textura associa o PONTEIRO da lista ao recurso DDS. Registrar a lista completa e depois entregar aos óculos uma nova lista de uma entrada perdia essa associação. `texaddr=0` isoladamente não comprova falha de carga.
- Última correção: os óculos usam a mesma lista completa registrada do corpo; o material local é remapeado de 0 para 12 na cópia própria dos polígonos. O acessório tem tratamento próprio para não executar o callback de partícula do objeto 0 e é recriado se a sala limpar/reutilizar sua entrada.
- IMPORTANTE: essa última correção compilou, mas ainda aguarda confirmação visual do usuário na gameplay. Não registrar os óculos como definitivamente resolvidos no jogo. Os outros personagens e a prévia foram aprovados pelo usuário antes desta revisão.

### Versões jogáveis e reversão

As três versões compilaram com sucesso e foram instaladas nos nomes normais em `projects/code_veronica/build-release/bin/`:

| Região | Executável |
| --- | --- |
| Japonesa | `CodeVeronicaPort_JP.exe` |
| Americana | `CodeVeronicaPort_US.exe` |
| Europeia | `CodeVeronicaPort.exe` |

O atalho normal é `projects/code_veronica/executar-code-veronica-jogar.cmd`. É necessário reabrir o jogo para carregar a nova versão. As cópias instaladas foram conferidas por SHA256. Backup dos três executáveis anteriores e manifesto: `projects/code_veronica/appearance-backups/pre-production-20261021/`.

As configurações dos três builds foram restauradas para `CV_APPEARANCE=OFF` e `CV_CHARACTERS=ON`. `CV_CHARACTERS` habilita a função nos builds normais; `CV_APPEARANCE` continua disponível para gerar executáveis separados de validação. Esta integração não alterou relógio de 30/60 FPS, interpolação ou frame generation. Nenhum commit/push foi realizado nesta entrega.

### Onde continuar

- `cv_appearance_runtime.cpp/.h`: troca visual, recursos, movimentos compatíveis, cabelo e acessório de cabeça.
- `cv_appearance_menu.cpp`, `cv_appearance_menu_preview.cpp`, `cv_appearance_mesh.cpp`: seleção, prévia e desenho dos modelos.
- `cv_appearance_bindings.h`: funções e globais próprios de JP/US/EU, conferidos contra os códigos recompilados de cada região; não usar um deslocamento único para converter endereços.
- `CMakeLists.txt` e `tools/cv_appearance_hooks.py`: integração dos builds e hooks regionais.
- `appearance-assets/prepare_lens_archive.py` e `inspect_models.py`: preparação local das cópias com a lente correta. Não versionar os arquivos `.arc` derivados nem outros dados extraídos do jogo.
- Histórico detalhado e logs de compilação na área de trabalho do Codex: `work/appearance/STATUS.md` e `work/appearance/native/build-production-{jp,us,eu}.log`; manifesto `work/appearance/native/production-installed.json`.

Próxima validação do usuário: Wesker com óculos texturizados durante movimento e após troca de sala; confirmar que o resultado aprovado dos outros personagens permanece. Não houve teste automatizado de gameplay.


## 2026-10-21 — v13: seletor de texturas do acessório Wesker

Após o teste da v12, o usuário confirmou que os óculos aparecem como pontinhos/recortes no rosto, enquanto a prévia está correta. Não alterar o encaixe aprovado X0/Y0.870/Z-1.050.

Inspeção do passe de modelos auxiliares confirmou uma omissão: JP `00042850` testa `entity+0x88` com `rlwinm(...,0,26,26)`, isto é, bit `0x20`. Sem esse bit o passe usa a tabela global de texturas de partículas/sala, calculada por `+0x4B0/+0x4AC`, e ignora `entity+0x118`. Com o bit, chama o seletor de textura usando diretamente a lista de `+0x118`. Portanto só preencher `+0x118` e `ML_WORK.texP` não era suficiente. Correspondências normalizadas verificadas nos outros dois lifts: US `000428A8`, EU `00042CA8`, com os mesmos campos e teste.

Correção limitada ao acessório próprio: `bind_glasses_bank()` configura a lista registrada completa e faz OR de `0x20` em `+0x88`, preservando os demais bits. É chamada na criação inicial, na recriação após limpeza da sala e antes da submissão de desenho. Mantém a textura original WKGRAS2 e material12. Prévia, posição, corpo, animações, outros personagens e temporização 30/60/FG não foram alterados. Corrigido também comentário antigo que ainda descrevia a lista de uma entrada descartada na v12.

Backup dos três executáveis e do runtime anterior: `appearance-backups/pre-lens-selector-20261021/`. Logs: `work/appearance/native/build-v13-{jp,us,eu}.log`. Esta revisão requer confirmação visual do usuário dentro do jogo; não há teste automatizado de gameplay. Nenhum commit/push nesta revisão.

Builds JP/US/EU concluídos com exit0; executáveis normais atualizados diretamente, sem troca de nomes ou mudança no atalho de jogar.


## 2026-10-21 — validação dos óculos e reinício da prévia (v14)

O usuário confirmou que a correção v13 funcionou na gameplay: rosto correto e óculos visíveis. Essa pendência visual está encerrada conforme o relato do usuário.

Novo ajuste solicitado: cada troca para outra aparência reinicia a prévia de frente. `AppearancePreview::select()` agora reinicia `epoch_` somente quando o índice muda, limpa o desenho em cache e força sua atualização. O ângulo inicial passa de 0,3 para 0 radianos; a velocidade permanece em 0,7 rad/s e a rotação automática continua normalmente. Selecionar o mesmo índice a cada desenho não reinicia o relógio. Corpo, pose, mãos, óculos e gameplay preservados.

Builds JP/US/EU concluídos com exit0; os três executáveis normais de jogar foram atualizados. Logs `work/appearance/native/build-v14-{jp,us,eu}.log`. O usuário fará a confirmação visual do novo comportamento da prévia. Sem commit/push.


## 2026-10-21 — expansão de humanoides (v15)

Solicitação: acrescentar todos os candidatos do catálogo, exceto os dois zumbis enfaixados. Foram preparados 28 novos modelos: 24 variantes padrão de zumbi, anatomista, prisioneiro de cena, Alexia primeira forma e Alexia humana. Os cinco personagens já existentes continuam nas posições 0–4. Catálogo total: 33 aparências. Os recursos en52a00/en52a01 não são incluídos; seus arquivos originais do jogo não são apagados.

### Adaptação de modelos e movimentos

`appearance-assets/prepare_humanoids.py` converte o primeiro corpo de cada recurso para a hierarquia de 22 juntas do jogador. São remapeadas as duas influências de cada vértice, preservando os pesos e o sentinela FF00 de influência ausente. Os corpos 18/20/27 juntas ganham correspondência anatômica com quadril, tronco, cabeça, braços e pernas; auxiliares faciais da Alexia humana acompanham a cabeça. Auxiliares do jaleco do anatomista são presos aos quadris. São mantidas as texturas originais na ordem do DDSP. Geometrias são reunidas em um buffer no espaço de repouso, com SKIN reconstruído para a nova hierarquia. UVN é convertido para UVH sem alterar as coordenadas normalizadas.

Os zumbis masculinos recebem os movimentos base compatíveis de Chris; as duas Alexias mantêm a base de movimentos de Claire. Identidade, inventário, save e lógica da campanha continuam da Claire. A conversão não importa a IA nem ataques dos inimigos. Arquivos separados de armas/equipamentos continuam no caminho já existente do jogador; não se deve afirmar que todas as armas, eventos e animações foram validadas.

### Menu, prévia e recursos

As novas prévias são carregadas sob demanda, preservando os braços abaixados, rotação automática de 0,7 rad/s e reinício de frente ao trocar a seleção. Alexia humana recebe as mãos independentes de Claire na prévia; os demais novos corpos já contêm mãos. Textos em inglês. Os modelos adicionados usam nomes privados cvskin05–cvskin32; a resolução do basename é interceptada somente no escopo das chamadas de carga desta função, conservando diretório e tipo ALD. A resolução original é mantida fora desse escopo, sem reescrever tabelas do jogo.

Os arquivos locais derivados ficam em `appearance-assets/humanoids/<idioma>/`, com 28 arquivos por idioma: jpn, eng, fra, ger, ita e spa. O overlay de arquivos atende esses nomes privados. Stat e Open agora consultam a mesma resolução de overlay, para que o tamanho informado corresponda ao arquivo efetivamente lido. Nenhum arquivo original do jogo é sobrescrito. Essa pasta de dados derivados foi adicionada ao .gitignore; nunca enviar esses arquivos extraídos ao Git.

Regeneração local: `python appearance-assets/prepare_humanoids.py <diretório system do jogo> appearance-assets/humanoids/<idioma> appearance-assets/humanoid_catalog.json`. O script usa prepare_lens_archive.py e inspect_models.py da mesma pasta. Não é preciso Python durante a execução do jogo depois de preparar os arquivos.

### Validação e preservação

A preparação conferiu limites, hierarquia, tipos e índices nos seis idiomas. As 28 prévias convertidas foram abertas e renderizadas pelo código nativo de prévia; todas apresentam 22 juntas e hierarquia compatível, preservando as contagens de triângulos. A maior prévia inspecionada usa 82674 quads, abaixo do limite de 262144. Essa verificação não é teste de gameplay nem prova de movimento perfeito.

Backup anterior: `appearance-backups/pre-humanoids-20261021/`, com os três executáveis normais e fontes alterados. O relógio 30/60 FPS, interpolação, frame generation e a posição/material dos óculos do Wesker não foram alterados por esta expansão. Logs regionais: `work/appearance/expanded/build-{jp,us,eu}.log`. A confirmação de gameplay (corrida, armas, menus, troca de sala e restauração) será do usuário. Sem commit/push solicitado nesta etapa.



V15 — integração concluída: builds JP/US/EU retornaram exit0. Candidatos instalados nos três executáveis normais de jogar; cópias verificadas por SHA256 e manifesto salvo em work/appearance/expanded/production-installed.json. As três configurações foram restauradas com sucesso para CV_APPEARANCE=OFF / CV_CHARACTERS=ON. Os 168 arquivos finais (28 modelos × 6 idiomas) usam o indicador original de compressão zlib do ARC (2) e estão instalados em appearance-assets/humanoids. Os modelos anteriores e o atalho de jogar continuam disponíveis. Ainda não houve execução de gameplay desta expansão; o usuário fará a validação visual e dos movimentos. Sem commit/push.


## 2026-10-21 — v16: correção após gameplay invisível e prévia deformada

O usuário rejeitou a v15: os modelos novos ficavam invisíveis na gameplay e o Prisoner (Scene) deformava na prévia. A aprovação anterior dos cinco personagens originais continua válida; a expansão NÃO estava validada em jogo.

Causas identificadas e correções:

- O conversor reservava só 8 bytes para NJS_CNK_MODEL. O formato tem 24 bytes, incluindo centro e raio de recorte. A rotina original JP 00031BE0 só transforma os ponteiros relativos em absolutos quando o raio é negativo. Com o descritor incompleto, a rotina não fazia essa transformação. O gerador agora grava os 24 bytes, centro calculado e raio negativo válido. O runtime verifica que geometria, vértices e polígonos foram relocados para dentro da cópia própria antes de instalar um modelo novo; falha conserva o original.
- As novas ARC usavam os nomes de recurso DDS e SCRA do doador Claire/Chris. O log confirmou que cvskin31/32 retornavam o mesmo DDS da Claire e cvskin05 o do Chris. Agora o recurso ALD principal tem nome privado cvskinXX; DDS e SCRA conservam os nomes e dependências originais do inimigo, evitando a colisão com os personagens doadores. Texturas e seus conteúdos não são substituídos por cores artificiais.
- en51a00 (Prisoner Scene) tem18 juntas, mas uma ordem diferente dos zumbis en01. Seu mapa anatômico foi corrigido separadamente. O gerador verifica posições de braços e pernas para rejeitar esse tipo de inversão.
- A prévia dos modelos convertidos usa os índices das juntas e seus coeficientes SKIN em vez da heurística espacial da Claire. Coeficiente zero não significa ausência de vínculo: o vértice ainda herda a rotação do braço pai. Isso remove os vértices que ficavam esticados quando o modelo girava. O caminho dos cinco personagens antigos não recebe esse novo ajuste.

Os 28 modelos foram regenerados nos seis idiomas. As 28 prévias foram renderizadas com o código nativo; foram inspecionados os modelos de frente/ângulo, incluindo Prisoner Scene e Alexia primeira forma, sem as deformações reproduzidas. Isso ainda não confirma resultado de gameplay. Logs: work/appearance/fix-humanoids/build-final-{jp,us,eu}.log (exit0 nos três). Backup: appearance-backups/pre-humanoid-fix-20261021. Sem mudança de movimentos, identidade de campanha, relógio 30/60, frame generation ou óculos do Wesker. Sem commit/push.

Instalação concluída: executáveis normais JP/US/EU substituídos pelas compilações corrigidas, cópias SHA256 verificadas. Manifesto: work/appearance/fix-humanoids/production-installed.json. Caches dos três builds restaurados para CV_APPEARANCE=OFF e CV_CHARACTERS=ON. É necessário reiniciar o jogo para descartar os modelos e texturas em cache. Aguardando confirmação visual de gameplay do usuário.


## 2026-10-21 — v17: Hazmat, mãos/braços dos zumbis e Alexias

Relato do usuário após a v16: os zumbis aparecem na gameplay, mas (1) a prévia do Hazmat explodia em vértices; (2) em todos os zumbis a mão/dedos que seguram a faca e a animação dos braços estavam errados; (3) as duas Alexias mostravam a Claire. O Codex começou estas correções e parou por limite de uso; o Claude concluiu, revisou e compilou.

- **Prévia do Hazmat** (`cv_appearance_mesh.cpp`): na tabela SKIN convertida, só o primeiro par (junta, coeficiente) de cada vértice posiciona o vértice. O segundo par pertence ao fluxo alternativo e não é uma segunda influência de posição. O Hazmat (en01a39) tem esse segundo par diferente do primeiro em parte dos vértices, inclusive na cabeça; tratá-lo como peso de posição puxava esses vértices para longe. A prévia nativa das 28 conversões foi renderizada de novo e o Hazmat aparece inteiro. Gameplay não usa esse código: o jogo faz o próprio skinning a partir da tabela SKIN.
- **Mão dupla atravessando a faca** (`equipment_hands()` em `cv_appearance_runtime.cpp`): as mãos do jogador (sem arma e com cada arma) vêm do equipamento, presas às juntas 9/13. Os corpos de inimigo também têm mãos abertas, e as duas eram desenhadas juntas. Na cópia de gameplay, os triângulos cujos três vértices estão nas juntas 9 ou 13 são omitidos; o restante dos strips é regravado como triângulos individuais com o mesmo sentido de vértices usado pela prévia. Vale para os zumbis e para a Alexia primeira forma; a Alexia humana não tem mãos próprias. A prévia do menu continua com o modelo completo.
- **Braços** (`install()`): nos modelos convertidos, a variante usa a mesma área de matrizes das juntas do ator original (`actor+0x16C`), em vez de uma cópia tirada na instalação. A arma presa à mão lê a matriz pela cadeia `entity+0x2E4 → +0x2D8 → +0x14 + junta*72` (JP `0x000402F0`). Os cinco personagens originais continuam com a cópia própria, como aprovados.
- **Alexias virando Claire** (`allocate()`/`prepare()`): cada aparência aplicada guarda um bloco próprio que não é liberado, porque o jogo continua referenciando o arquivo carregado e as texturas registradas. Eram 2 MiB por aparência, e na faixa 0x4CA00000–0x50000000 cabem só 27 blocos (54 MiB livres depois dos 0xCA00000 do jogo). Percorrer o seletor na gameplay esgotava esse espaço e as Alexias (31/32), as últimas da lista, falhavam com "Memory unavailable", mantendo a Claire. Agora os modelos convertidos reservam o tamanho medido em páginas de 64 KiB: arquivo carregado + 0x1000, modelo 0x30000, trabalho 0x10000, movimentos 0x40000 e tabela. O total dos 28 é ~39 MiB; com os 4 originais (8 MiB) dá ~47 MiB, abaixo dos 54. Os endereços continuam fixos depois da primeira carga.
- **Proteção do espaço reservado**: antes de pedir o arquivo ao carregador, os primeiros 64 KiB da área do modelo são marcados. Se o carregador gravar além do espaço medido, a aplicação falha com `[appearance] archive load exceeded reserved area` no log e a Claire é mantida, em vez de corromper o modelo.

Builds JP/US/EU com `build-release.cmd` (`CV_BUILD_JP=1`, `CV_BUILD_US=1`); caches com `CV_APPEARANCE=OFF` e `CV_CHARACTERS=ON`, gerando os executáveis normais de jogar. Ainda falta o teste do usuário na gameplay: mãos/faca e braços dos zumbis, as duas Alexias carregando depois de percorrer todo o seletor, e o Hazmat na prévia. Sem commit.

## 2026-10-21 — v18: clarão branco ao atirar com zumbis/Alexias

Teste do usuário na v17: mãos, braços e Alexias funcionaram. Restou um problema só nos modelos convertidos (zumbis e Alexias): ao atirar, o clarão da arma deixava a tela toda branca por um instante. Claire, Chris, Steve e Wesker estavam corretos.

Diferença encontrada entre os corpos originais (pl00–pl04) e os convertidos: no jogador, o corpo inteiro fica num único buffer no **nó 1 (quadril)**, com vértices relativos ao quadril. O nó raiz não tem geometria nem entrada SKIN e cada nó conserva seus próprios indicadores de avaliação (0xF/0x6/0xE/0x1E, incluindo ocultar). O conversor punha o buffer no **nó 0**, com vértices em espaço de mundo, SKIN com tabela na raiz e indicador 0x6 em todos os nós. Um código que espera o corpo do jogador no nó 1 encontrava ali um ponteiro de geometria vazio.

Correção em `prepare_humanoids.py`: a geometria fica no nó 1, com vértices relativos à posição de repouso do quadril. Os indicadores de nó são copiados do esqueleto da Claire, e o gerador verifica que só o nó 1 é visível e que nós com posição unitária não têm deslocamento. O SKIN deixa a raiz sem entrada e põe a tabela de influências no nó 1, e o gerador verifica que nenhum vértice usa a raiz. As mãos (9/13) mantêm entrada porque as mãos dos inimigos são vinculadas a elas. `equipment_hands()` no runtime passou a ler a geometria do nó 1 e o novo início da tabela SKIN.

Os 28 modelos foram regenerados nos seis idiomas e o formato foi conferido em todos. As prévias nativas ficaram idênticas pixel a pixel às da v17 (contagens e imagens de 05, 25, 29–32). Backup dos modelos anteriores e do gerador: `appearance-backups/pre-node1-body-20261021/`. A hipótese da causa ainda depende do teste do usuário: atirar com zumbis e Alexias. Sem commit.

## 2026-10-21 — v19: clarão da Alexia primeira forma

O usuário confirmou que a v18 corrigiu o clarão branco de todos os zumbis e da Alexia humana. Só a Alexia primeira forma (cvskin31, en12a00) continuava com o problema.

Diferença encontrada: a lista de texturas dela tem só 4 entradas. Todos os bancos de jogador têm pelo menos 5 (pl00 tem exatamente 5; pl01 6, pl02 5, pl03 12, pl04 6), e as outras 27 conversões têm de 5 a 15. A suspeita é que o efeito do tiro use a 5ª entrada do banco do jogador. O gerador agora completa até 5 entradas repetindo a última textura, que nenhum polígono seleciona. Isso vale também para as duas listas de piscar.

Só cvskin31 mudou, nos seis idiomas; os outros 27 arquivos ficaram idênticos byte a byte. A prévia nativa dela também ficou idêntica pixel a pixel. Não é preciso recompilar: o runtime não mudou e os arquivos são lidos de `appearance-assets/humanoids` ao abrir o jogo. Aguarda o teste do usuário atirando com a Alexia primeira forma. Sem commit.

## 2026-10-21 — v20: segundo vínculo de pele (isqueiro e tiro)

Teste da v19: a Alexia primeira forma continuou deixando a tela branca ao atirar e também ao acender o isqueiro. A hipótese das 5 texturas da v19 estava errada, e o preenchimento da lista foi removido do gerador.

Nova diferença encontrada na tabela SKIN: no corpo do jogador (pl00), todo vértice tem o segundo par (junta, coeficiente) e ele repete o primeiro. Na Alexia primeira forma, os 964 vértices têm o segundo par ausente (FF00). Nos outros convertidos, apenas parte deles: Hazmat, Anatomista, Prisoner Scene e Alexia humana. O clarão do tiro e o isqueiro são luzes pontuais, então o segundo par provavelmente vincula o fluxo usado na iluminação. Correção em `prepare_humanoids.py`: um segundo par ausente passa a repetir o primeiro, como no jogador. Pares já existentes não mudam.

Mudaram cvskin25, 29, 30, 31 e 32, nos seis idiomas; os demais ficaram idênticos. As prévias nativas ficaram idênticas pixel a pixel, porque a prévia usa só o primeiro par. Sem recompilação: o runtime não mudou. Aguarda o teste do usuário: isqueiro e tiro com a Alexia primeira forma, e conferir de novo esses cinco modelos. Sem commit.

## 2026-10-21 — v19/v20 revertidas; Alexia primeira forma ainda pendente

Teste da v20: a Alexia primeira forma continua branca com o tiro e com o isqueiro. As duas hipóteses estavam erradas: 5 texturas (v19) e repetir o segundo par de pele (v20). Além disso, a v20 alterou à toa quatro modelos que já estavam corretos. Tudo voltou ao estado da v18: o gerador volta a produzir exatamente os arquivos da v18, conferidos byte a byte, e os 168 arquivos instalados também são os da v18. Zumbis e Alexia humana estão corretos na v18, segundo o usuário.

Próximo passo, sem novas tentativas às cegas: captura F12 (`rsxcap/frame_N`) com a Alexia primeira forma e o isqueiro aceso (tela branca). A captura registra cada passe. O isqueiro desenha o jogador num atlas de sombra 768x512 (ver `SOMBRA-ISQUEIRO.md`), e é preciso localizar o desenho em que a imagem fica branca. Diferenças já descartadas entre cvskin31 e os modelos que funcionam: normais (todas unitárias), formatos de textura (DXT1), raio e centro, tamanho de chunks e strips, nós e SKIN no formato do jogador.

## 2026-10-21 — v21: causa do clarão da Alexia primeira forma (captura F12)

A captura `rsxcap/frame_90` (Alexia primeira forma, isqueiro aceso, tela branca) mostrou 16420 desenhos no quadro, sendo 10703 no atlas de sombra 768x512 do isqueiro (0x07D90000), quase todos com n=3. Numa captura antiga de outra cena, eram cerca de 3000 desenhos de sombra. Cada strip de polígono vira um desenho em cada uma das 6 faces da sombra.

A causa estava no runtime, em `equipment_hands()` (v17): para retirar a mão aberta, a função regravava o corpo inteiro como strips de um único triângulo. A Alexia primeira forma tem 1920 triângulos, o que dava 1744 strips × 6 faces ≈ 10.5 mil desenhos, e a luz pontual estourava. Os zumbis têm cerca de metade e ficavam abaixo do limite.

Correção: os strips originais são mantidos e cortados só em volta dos triângulos da mão. Cada trecho começa no vértice original, e o sinal do strip compensa a paridade quando o trecho começa num vértice ímpar. Os chunks conservam opcode, flags e bits altos do cabeçalho, com tamanho múltiplo de 4. Validação em Python com a mesma lógica nos 28 modelos v18: triângulos e orientação idênticos aos originais sem os da mão, e menos strips que o original (Alexia primeira forma 376 → 327; zumbis ~240 → ~210). Os modelos e o gerador continuam na v18; só o runtime mudou. Aguarda o teste do usuário com isqueiro e tiro na Alexia primeira forma e nos zumbis. Sem commit.

## 2026-10-22 — aparência também para Chris, Steve e Wesker

Antes, `cv_appearance_runtime.cpp` só aplicava a troca quando o marcador RDX era o da Claire (0x3D650). Agora `player()` aceita os quatro personagens jogáveis pelo mesmo teste do SRT e do inventário (`CV_SRT_CHARACTER` + marcador RDX: 0x3D650, 0x3C4FC, 0x3A4D0, 0x3E814), nas três regiões.

- Claire: regra antiga intacta. Entrada 0 é o original, entrada 1 o traje alternativo, e de 2 em diante oculta o cabelo dela e usa os movimentos do escolhido.
- Chris, Steve e Wesker: a entrada que é o próprio corpo deles (CHRIS, STEVE, WESKER) devolve o original. Qualquer outra vira troca, ocultando as árvores auxiliares do jogador e usando os movimentos do escolhido quando o modelo os tem (Claire e Alexias mantêm os movimentos do personagem jogado).
- Entrada 0 (CLAIRE - ORIGINAL) só vira corpo da Claire nos outros personagens depois que o menu foi usado na sessão. Sem tocar no menu, todo mundo continua com o próprio corpo.
- A escolha é preparada mesmo quando é o corpo do personagem atual, para valer depois ao carregar um save de outro personagem.
- Log: `[appearance] playing character N actor=… slots=…` ao trocar de personagem, e `character=N choice=M` na aplicação.

O primeiro build ainda falhava com o Chris: o teste antigo exigia `sys+0x94==0`, que é o próprio `CV_SRT_CHARACTER` (Claire = 0). Esse teste foi retirado; se outra condição barrar um personagem, o log mostra `[appearance] player N not ready: ...`.

Teste do usuário (JP): com o Chris, Alexia humana, as duas Claires, Steve e Wesker (com óculos) foram aplicados e ficaram corretos na tela. Steve como personagem jogável não foi testado em jogo. Sem commit.

## Pacote de lançamento: por que a troca de aparência não funcionava (23/10/2026)

**Sintoma:** no ZIP de lançamento, o menu `OPTION → RECOMP SETTINGS → APPEARANCE` não mostra a prévia, e a troca não acontece ("original appearance retained").

**Causa:** os modelos não estão dentro do exe. Os executáveis de desenvolvimento leem arquivos **derivados do jogo**, gerados localmente pelos scripts Python desta pasta e guardados ao lado do projeto:

- `appearance-assets/humanoids/<idioma>/` — os 28 modelos (zumbis, Alexia etc.) por idioma (jpn, eng, fra, ger, ita, spa), feitos por `prepare_humanoids.py`;
- `appearance-assets/pl03-<idioma>.arc` — os óculos do Wesker, feitos por `prepare_lens_archive.py` (antes ligados por `CV_WESKER_LENS_ARC` nos atalhos `.cmd`).

`tools/empacotar_release.py` não leva esses arquivos, **e não deve levar**: são dados do jogo convertidos (ficam no `.gitignore`), e o pacote não pode conter nada do jogo. Por isso, no PC de quem baixa a release, os modelos não existem.

**Solução certa (sem mandar arquivos do jogo):** o exe precisa gerar esses arquivos sozinho, a partir da cópia do jogo do próprio jogador, na primeira vez em que a aparência é escolhida. É o que `cv_appearance_prepare.cpp` começou a fazer (sem commit em 23/10): ele lê os arquivos originais da pasta `system` do jogo (só leitura), converte em C++ (sem Python), e guarda o resultado em `appearance_cache/v1/` ao lado do exe.

**Antes de publicar uma release com a troca de aparência, conferir:**

1. Toda conversão que hoje é feita por `prepare_humanoids.py` e `prepare_lens_archive.py` foi portada para o exe (os 33 itens do catálogo, os óculos do Wesker e os 6 idiomas do jogo).
2. Nenhum caminho do runtime depende de `appearance-assets/` nem de variáveis de ambiente dos `.cmd` (`CV_WESKER_LENS_ARC` etc.).
3. Teste numa pasta limpa: extrair o ZIP recém-gerado em outra pasta (sem `appearance-assets/`, sem `appearance_cache/`), abrir o jogo nas três regiões, escolher várias aparências (um zumbi, a Alexia humana, o Wesker com óculos) e conferir que a prévia aparece e a troca vale no jogo.
4. `appearance_cache/` fica fora do ZIP e fora do Git; o atualizador não deve apagá-la.
5. Se faltar algo, a mensagem do menu precisa dizer o motivo (por exemplo, "arquivo do jogo não encontrado") em vez de só "original appearance retained".

Enquanto isso não estiver pronto, a release funciona normalmente, mas a troca de aparência só funciona na máquina de desenvolvimento.
