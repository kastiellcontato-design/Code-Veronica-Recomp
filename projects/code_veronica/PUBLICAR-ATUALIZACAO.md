# Publicar atualizações do Veronica Port

O launcher consulta as **Releases estáveis** de
`kastiellcontato-design/re-cvx-ps3recomp`. Fazer commit/push ou criar somente uma
tag não disponibiliza uma build para os jogadores: é preciso publicar a Release
com o ZIP dos executáveis anexado como asset.

## Para quem usa o port

- **Avançado → Procurar atualizações** verifica ao abrir (ligado por padrão).
- **Procurar agora** permite verificar manualmente. Sem internet ou sem release,
  a verificação automática fica silenciosa; a manual explica o resultado.
- Uma versão maior exibe notas e **Instalar / Agora não**. Enter instala; Esc
  adia, fecha o erro ou cancela o download. A opção não oferece instalação durante
  uma partida.
- O download tem progresso e cancelamento; a instalação termina antes de fechar.
  Ao concluir, o launcher reabre e limpa as cópias antigas após o processo anterior sair.
- Os textos acompanham o idioma do launcher, nos 13 idiomas disponíveis.

**Primeira distribuição:** quem ainda usa um launcher sem atualizador precisa
instalar esta build manualmente uma vez. As próximas podem chegar pelo launcher.

## Publicar a próxima versão

1. Em `launcher.cpp`, altere `k_launcher_version` de `L"v1.0"` para uma versão
   numérica maior, por exemplo `L"v1.1.0"`. O mesmo valor aparece no rodapé.
2. Compile as três regiões, mantendo as opções de build já usadas no projeto:

   ```cmd
   set CV_BUILD_JP=1
   set CV_BUILD_US=1
   build-release.cmd
   ```

3. Com Python **3.11 ou mais recente**, execute `empacotar-release.cmd`.
   Se o Python não estiver registrado no `py`, defina `PYTHON` com o caminho
   do interpretador antes de executar o CMD. A ferramenta confere os arquivos
   obrigatórios e recusa executáveis mais antigos que `launcher.cpp`.
4. O pacote fica em `build-release/pacote/CodeVeronica-PC-v1.1.0.zip`, acompanhado
   do `.zip.sha256`. Não há pasta de estágio a apagar e um ZIP já existente não é
   sobrescrito. `empacotar-release.cmd --check-only` apenas verifica os requisitos.
5. No GitHub, crie uma **Release publicada e estável**, com a mesma tag
   `v1.1.0`, título e notas das novidades. Anexe o ZIP e opcionalmente o SHA-256.
   Marque a release como a mais recente. Não use draft ou pre-release para o canal
   estável. Exemplo opcional com GitHub CLI:

   ```cmd
   gh release create v1.1.0 build-release/pacote/CodeVeronica-PC-v1.1.0.zip build-release/pacote/CodeVeronica-PC-v1.1.0.zip.sha256 --latest --title "Veronica Port v1.1.0" --notes-file notas.md
   ```

Nenhuma publicação é feita automaticamente pelo empacotador.

## Conteúdo e proteção dos dados

O ZIP contém os três executáveis, DLLs de FFmpeg/FSR/DXC, avisos de licença e um
`release-manifest.json` com versão e hashes. Configurações, saves e arquivos do
jogo não são incluídos. O launcher aceita arquivos na raiz ou numa única pasta.
Ele prefere o asset `CodeVeronica-PC-*.zip` ou `VeronicaPort-*.zip`; não use mais
de um pacote desse tipo na mesma release. Um `.zip` genérico é aceito como fallback.

O tamanho do download e o SHA-256 informado pelo GitHub são conferidos antes da
extração. O `.sha256` separado serve para conferência manual; o launcher usa o
campo `digest` da API. Arquivos ZIP com caminhos inválidos, links, nomes ambíguos,
multivolume ou ZIP64 são rejeitados. Limites: ZIP até 2 GiB, conteúdo expandido até
8 GiB e 10 mil entradas.

A instalação preserva `*.ini`, logs, `gamedata`, `dev_hdd0`, `dev_flash`, saves,
backups, caches, screenshots, mods, textures e `JOGO`. Arquivos ausentes do pacote
ficam como estavam. Pastas vinculadas/junctions no destino não são seguidas.
Cada arquivo substituído ganha uma cópia `.cvupd-old`; uma falha na cópia reverte
as alterações já feitas. Downloads e extração usam uma pasta temporária por
processo, e a instalação tem exclusão mútua por pasta do port.

## Testes sem publicar

- `CV_UPDATE_URL` permite consultar uma API alternativa ou JSON local. Esse modo
  também permite um arquivo local em `browser_download_url`; não mude a URL
  padrão da distribuição para um servidor de teste.
- `CV_UPDATE_DISABLE=1` desliga a busca automática para diagnósticos.
- `set CV_UPDATER_TESTS=1` antes de `build-release.cmd` compila e executa o teste
  nativo `code_veronica_updater_tests`.
- Para testar rede, pacote inválido e reabertura sem tocar na instalação real:

  ```cmd
  py -3 tools/teste_atualizacao_local.py --test-exe build-release/bin/code_veronica_updater_tests.exe
  ```

O script usa uma porta aleatória em `127.0.0.1` e arquivos fictícios temporários.
Testa busca, download, SHA-256, instalação, dados preservados, pacotes inválidos e
substituição de um executável em execução, reabertura e limpeza. Não abre o jogo.

## Validação de 22/10/2026

Testes nativos e simulação HTTP local concluídos: versões numéricas grandes,
13 tabelas de idioma preenchidas, cancelamento, checksum incorreto, seleção do
asset, paths protegidos, instalação, reversão por arquivo bloqueado, sete ZIPs
inválidos e atualização/reabertura de executável em execução.

Os três launchers regionais foram compilados com o módulo. A compilação usou os
argumentos atuais do CMake para `launcher.cpp` e `cv_updater.cpp`, preservando os
demais objetos do jogo. O link foi concluído com o LINK instalado do MSVC; apenas
a geração de mapa/debug foi omitida nessa finalização local. Logs e backups no
workspace Codex, `work/launcher-updater`. Nenhuma release foi publicada neste
trabalho e nenhuma partida foi iniciada.

Referências oficiais: https://docs.github.com/en/rest/releases/releases e
https://docs.github.com/en/rest/releases/assets.
