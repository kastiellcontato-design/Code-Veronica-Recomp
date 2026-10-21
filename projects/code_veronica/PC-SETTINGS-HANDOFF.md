# PC Settings nativo: resumo para continuidade

## O que foi implementado

- A opção **PC SETTINGS** foi integrada ao menu OPTION original do jogo, mantendo o fluxo e a navegação nativos.
- A tela oferece resolução, anti-aliasing, modo de exibição, brilho, contraste e saturação. As opções são alteradas pelas setas esquerda/direita e ficam salvas em `cv_launcher.ini`.
- Os textos do menu usam a mesma fonte serifada; a rasterização é feita em 1,5x e seus fragmentos são desenhados em posições fracionárias, sem reduzir a geometria à grade de baixa resolução do jogo.
- O lote gráfico agora cresce conforme a tela precisa, evitando que o maior número de fragmentos da fonte suavizada descarte e deixe invisíveis as opções.
- Brilho varia de -80% a +80% e contraste de 50% a 180%, ambos em passos de 1%. Segurar esquerda/direita acelera o ajuste depois de meio segundo e acelera novamente após 1,2 segundo.
- Brilho e contraste são aplicados no renderizador D3D12. O ajuste combina com o passe de FXAA/SMAA quando um deles está ligado; com AA desligado, só cria um passe de cor quando os valores saem do padrão. Os valores padrão não adicionam esse passe.
- Saturação varia de 0% a 200%, com 100% como padrão sem alteração. O passe de cor envia o valor no registrador correto do bloco HLSL (`colorExtra.x`), combinando o ajuste com AA quando ativo e evitando uma passagem adicional nesse caso.
- A imagem é ajustada no jogo, sem alterar texturas ou arquivos do conteúdo do jogo.

## Arquivos relacionados a esta implementação

- `projects/code_veronica/native_pc_menu.cpp`: linhas e navegação do menu do jogo, controles de cor e rasterização do texto.
- `projects/code_veronica/launcher.cpp`: leitura/gravação dos valores e ligação das opções ao renderizador.
- `libs/video/rsx_d3d12_backend.c`: desenho do texto em lote e ajuste de brilho/contraste/saturação por shader.

O porte anterior do PC SETTINGS também usa `projects/code_veronica/CMakeLists.txt`, `libs/video/rsx_ui_batch.h` e `tools/cv_native_menu_hooks.py` para compilar e ligar o desenho ao menu original.

## Escopo e preservação

Esta tarefa alterou somente o menu PC SETTINGS e o ajuste de cor necessário para ele. As alterações avançadas de controles que já estavam na cópia foram preservadas. Não alterei arquivos do jogo, dumps, executáveis, banner ou ícone; não criei commit nem fiz push.

## Build e teste

- Branch de trabalho: `gpt-experimentos`.
- Compile exclusivamente com `projects/code_veronica/build-release.cmd`.
- Para abrir o jogo compilado, use `projects/code_veronica/executar-code-veronica-otimizado.cmd` dentro desta cópia. O atalho copiado “Abrir Code Veronica” ainda pode apontar para a pasta original.
- O build desta alteração foi validado pelo script acima. A verificação visual do texto e dos controles de cor deve ser feita no jogo; a pessoa usuária controla a navegação.


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
| Japonesa | `MyGameRecomp_JP.exe` |
| Americana | `MyGameRecomp_US.exe` |
| Europeia | `MyGameRecomp.exe` |

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

