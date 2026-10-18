# Ajustes do launcher: Cheats e patches personalizados

Data: 2026-10-18. Pedido do usuario: renomear Inventario para Cheats,
permitir retirar o diretorio de patches personalizados e creditar Kastiel nos
patches que mostravam apenas "feito para este port".

## Implementacao

- launcher_i18n.h: S_NAV_ITEMS agora mostra Cheats nos 13 idiomas. Nao altera
  S_SRT_INVENTORY nem o nome do inventario do jogo. As secoes de itens, vida
  e municao continuam na mesma pagina e com as mesmas funcoes.
- S_PATCH_LOGO_C agora reutiliza o texto traduzido de S_CREDIT_KASTIEL de cada
  idioma: em pt-BR, "Feito por Kastiel". Afeta pular logos, vida infinita e
  municao infinita. O credito Kapdap/RPCS3 do patch de portas foi preservado.
- Novos textos S_PATCH_CLEAR_PATH e S_PATCH_CLEAR_PATH_HINT traduzidos nos 13
  idiomas: "Limpar caminho" e "Os arquivos da pasta nao serao apagados".
- launcher.cpp: ID_PATCH_DIR_CLEAR e botao abaixo do seletor de pasta. Desabilitado
  quando nao ha caminho. A acao limpa apenas g.s.patch_dir e patch_files_on,
  refaz a lista, zera sua rolagem e salva usando changed(). Nao apaga arquivos,
  nao remove diretorios e nao muda os patches embutidos do port. Tambem funciona
  se a pasta selecionada nao existe ou nao tem patches compativeis.

## Validacao

Tabelas dos 13 idiomas verificadas: uma entrada por StrId, novo nome presente,
credito igual a traducao de Kastiel. Static_asserts do header conferem o tamanho
no build. Compilacao EU/JP/US pelo script oficial, CV_BUILD_JP=1 e CV_BUILD_US=1.
Log local: build-release/launcher-cheats-labels-build.log. Nenhum comando foi
enviado ao jogo e nenhum arquivo de patch do usuario foi apagado. Conferencia
visual e clique no novo botao dentro do launcher ainda dependem do usuario.

## Para o commit posterior pelo Claudio

Selecionar SOMENTE estes hunks de launcher.cpp (ID, comentario Cheats, botao
na pagina Patches e handler de limpeza) e launcher_i18n.h (nome da aba, credito
e duas novas traducoes por idioma), alem deste MD. Os arquivos ja continham
outros trabalhos: revisar o diff e nao adicionar arquivos inteiros por arrasto.
Nao incluir executaveis, logs ou arquivos do jogo. O usuario autorizou o commit das melhorias recentes de launcher/Mods.
A implementacao anterior dos cheats e seus hooks continuam separada deste commit.
Atalho continua executar-code-veronica-otimizado.cmd.
