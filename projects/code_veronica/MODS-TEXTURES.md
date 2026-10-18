# Mods: pasta padrao e extracao completa de texturas

Data: 2026-10-18. Commit das melhorias recentes autorizado pelo usuario. Implementacao independente da logica do jogo;
nao altera os arquivos originais, saves nem a navegacao entre mapas.

## Comportamento

O padrao do launcher agora e mods/textures ao lado do executavel. Cada versao
usa sua subpasta NPEB00553, NPJB00135 ou NPUB30467; a saida padrao e
mods/texture-dumps/<versao>, independente da pasta de mods. Neste checkout o executavel fica em
projects/code_veronica/build-release/bin. Os seletores de pasta continuam
funcionando; caminhos personalizados ja salvos sao preservados. Nenhuma pasta
antiga foi movida, apagada ou sobrescrita automaticamente. O padrao de outros
ports na biblioteca compartilhada nao foi alterado.

A antiga opcao Extrair texturas passou a descrever as salas visitadas: salva
texturas novas carregadas em gameplay, inclusive personagens, itens, menus e
efeitos carregados junto; nao deve prometer somente o cenario ou salas futuras.

Novo modo Extrair texturas de todos os mapas: selecione-o e clique em Iniciar
extracao. Um worker le os arquivos sob USRDIR/BHCV/nativePS3/biocv_disc da
instalacao selecionada, sem abrir mapas ou enviar comandos ao jogo. Mostra
progresso, existentes/repetidas, ignoradas e erros, e permite cancelar. O modo
nao inicia sozinho ao abrir o launcher. Modos exclusivos: selecionar todos
limpa a extracao em gameplay; selecionar salas limpa todos. Durante um job,
trocar o modo fica bloqueado ate ele terminar ou ser cancelado.

Modos salvos em [texturas] extrair e extrair_todos; ao carregar um INI antigo
ou conflitante, todos tem precedencia e desliga extrair. TEXTURE_DUMP so fica
ligado no modo de gameplay; texpack_flags_changed atualiza a chave na hora.
Substituir texturas continua independente da escolha de extracao.

PNGs existentes nunca sao sobrescritos, incluindo edicoes feitas pelo usuario.
Hashes, nomes e pixels usam os mesmos criterios do renderer. Um CSV por
execucao relaciona arquivo/asset/PNG, incluindo recursos repetidos entre mapas.
Cancelamento mantem PNGs completos, com gravacao temporaria e rename atomico.
Saida dentro da pasta-fonte e recusada. Falhas sao mostradas no painel.

BC1/DXT1 (inclusive normal maps 0x1e), BC2/DXT3, BC3/DXT5 e ARGB sao suportados.
ARGB usa rsx_texture_layout/rsx_texture_decode, incluindo Morton para POT e
ordem dos canais; nao basta copiar as linhas do arquivo. O tamanho alocado do
ARC pode ser maior que o tamanho realmente descomprimido, por padding.

Cubemaps: extrai as seis faces, mas guarda em dump/reference/cubemaps para
consulta, pois o sistema atual de substituicao aceita imagens 2D. A interface
explica essa limitacao. Texturas criadas dinamicamente pelo jogo exigem o modo
de gameplay; a extracao offline somente pode ler imagens armazenadas nos
arquivos. O SFX otherScene_BM.stex e um material/referencia sem imagem embutida.
Nao e tratado como PNG ou como arquivo corrompido. Formatos inesperados sao
contados como ignorados; dados invalidos sao contados como erros.

## Validacao realizada

- Testes sinteticos: ARC cru/SFH, cores e alpha de BC1/2/3/ARGB, pixels Morton
  nao uniformes, hashes do mesmo texpack_hash, duplicatas, preservacao de PNG
  editado, fonte intacta, dados malformados, destino seguro e cancelamento.
- rm_0030 EU: 144 referencias de textura, 134 PNGs unicos; zero erros.
- Varredura integral JP: 718 arquivos, 18.496 referencias, 6.972 PNGs 2D unicos
  e 69 faces de cubemaps unicas; zero ignoradas e zero erros no teste final.
- 129 PNGs do mapa EU comparados com dumps de gameplay: pixels identicos.
- Comparacao ampliada JP: 2.317 nomes encontrados no dump de gameplay. Quatro
  ARGB inicialmente divergiram por Morton; corrigidos com o decoder compartilhado
  e comparados novamente, identicos. Normal map 0x1e tambem comparado, identico.
- Teste final: build-release/texture-export-full-tests.log, dois PASS (mapa EU e
  varredura JP). Tabelas e seis argumentos %u de progresso nos 13 idiomas
  verificados; static_assert confere uma entrada por StrId.
- Builds EU/JP/US pelo script oficial com CV_BUILD_JP=1 e CV_BUILD_US=1; log
  build-release/mods-texture-export-build.log. Previews e PNGs de teste sao
  locais e ficam ignorados dentro do build, nunca entram no commit.
- Sem entrada sintetica no jogo. Validacao visual e botoes do launcher ainda
  dependem do usuario. Nao foi medido FPS: a extracao completa e trabalho de
  arquivo/CPU em segundo plano e pode disputar recursos se a partida for aberta.

## Reproduzir os testes

Fechar jogo e launcher, sempre verificar MyGameRecomp*.exe antes. Somente
projects/code_veronica/build-release.cmd e autorizado para compilar/executar.
Definir CV_BUILD_JP=1, CV_BUILD_US=1 e CV_TEXTURE_EXPORT_TESTS_ONLY=1. O teste
sempre le um mapa EU real; para uma varredura extra, CV_TEXTURE_EXPORT_SOURCE
pode apontar ao biocv_disc de uma instalacao local. Remover a variavel
CV_TEXTURE_EXPORT_TESTS_ONLY para gerar os executaveis do jogo. Nao publicar
arquivos extraidos do jogo. Atalho do usuario permanece
executar-code-veronica-otimizado.cmd.

## Handoff para o Claudio / commit posterior

Novos: cv_texture_export.cpp/.h, tests/test_texture_export.cpp,
tests/texture_export_stb.c e este MD. Trechos de launcher.cpp: configuracao,
IDs, pasta padrao, controles/progresso dos dois modos, handlers, repaint e
cancelamento no fechamento. launcher_i18n.h: novos textos e descricao correta
dos modos nos 13 idiomas. CMakeLists.txt: nova fonte e alvo de teste isolado.
build-release.cmd: bloco CV_TEXTURE_EXPORT_TESTS_ONLY. Nao alterar os demais
hunks existentes nem adicionar os arquivos inteiros sem revisar: ha trabalho
anterior de outros autores no checkout. Nao incluir PNGs, CSVs, logs,
executaveis, pastas do jogo ou mods do usuario. Este documento acompanha o commit autorizado das melhorias recentes de Mods e launcher.

Aviso final pedido pelo usuario: o titulo da pagina Mods mostra Experimental,
traduzido nos 13 idiomas. E somente um aviso visual; nao altera os modos.
Alteracoes de FPS, SPU, sombras e cheats anteriores ficam fora deste commit.

## Correção das pastas de entrada e saída (18/10/2026)

- A primeira linha identifica somente a pasta dos PNGs do mod; o contador mostra apenas substituições.
- A segunda linha se chama "Pasta das texturas extraídas" e é o destino de ambos os modos de extração.
- O padrão de saída passa a ser `mods/texture-dumps/<versão>` ao lado do executável, independente de `mods/textures` e de mudanças da pasta do mod. Caminhos personalizados salvos continuam sendo respeitados.
- A versão escolhida no launcher determina a pasta antes da partida; durante a partida usa-se a versão em execução. Abrir uma pasta de mod direta não cria uma subpasta vazia de outra região.
- Alterar a saída durante a partida atualiza o escritor e a deduplicação; texturas já residentes podem ser extraídas para a nova pasta quando usadas novamente. Cada trabalho pendente conserva seu destino original.
- A varredura de mods exclui a pasta das extraídas. Não é permitido escolher exatamente a mesma pasta como entrada e saída.
- A saída não pode ser trocada enquanto a extração offline estiver em andamento. Trocar a pasta de entrada dos mods requer reiniciar a partida para reconstruir o índice, com aviso no painel.
- Nenhuma extração antiga é movida nem apagada automaticamente. O escritor preserva PNGs existentes, inclusive modificações do usuário.
- Rótulos, contadores e avisos corrigidos nos 13 idiomas; rótulos longos têm mais espaço.
- Testes adicionados ao alvo existente de extração: padrões EU/JP/US, pastas personalizadas, troca de destino com arquivos pendentes, deduplicação por pasta, transparência, preservação de PNGs e exclusão da saída do índice de mods.

Validação da correção: teste de gravação assíncrona com troca de diretórios e alpha aprovado; mapa EU real com 144 referências sem erros; executáveis EU/JP/US recompilados pelo script oficial. Teste interativo do launcher confirmado pelo usuário: pastas de carregamento e extração funcionando corretamente.
